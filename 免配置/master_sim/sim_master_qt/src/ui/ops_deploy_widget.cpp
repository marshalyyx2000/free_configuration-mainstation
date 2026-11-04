#include "ui/ops_deploy_widget.h"
#include "ops/lab_operations.h"
#include "settings/shared_connection_settings.h"
#include "util/app_paths.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

OpsDeployWidget::OpsDeployWidget(SharedConnectionSettings* settings, LabWorker* worker, QWidget* parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_worker(worker)
{
    auto* root = new QVBoxLayout(this);

    auto* conn = new QGroupBox(tr("连接"), this);
    auto* connGrid = new QGridLayout(conn);
    connGrid->addWidget(new QLabel(tr("子站 IP"), conn), 0, 0);
    m_hostEdit = new QLineEdit(m_settings->host(), conn);
    connGrid->addWidget(m_hostEdit, 0, 1);
    connGrid->addWidget(new QLabel(tr("运维端口"), conn), 0, 2);
    m_maintPortEdit = new QLineEdit(QString::number(m_settings->maintainPort()), conn);
    m_maintPortEdit->setMaximumWidth(80);
    connGrid->addWidget(m_maintPortEdit, 0, 3);
    connGrid->addWidget(new QLabel(tr("MMS 端口"), conn), 0, 4);
    m_mmsPortEdit = new QLineEdit(QString::number(m_settings->mmsPort()), conn);
    m_mmsPortEdit->setMaximumWidth(80);
    connGrid->addWidget(m_mmsPortEdit, 0, 5);
    auto* btnProbe = new QPushButton(tr("探测端口"), conn);
    connect(btnProbe, &QPushButton::clicked, this, &OpsDeployWidget::onProbe);
    connGrid->addWidget(btnProbe, 0, 6);
    root->addWidget(conn);

    auto* scdBox = new QGroupBox(tr("SCD / 输出"), this);
    auto* scdRow = new QHBoxLayout(scdBox);
    m_scdEdit = new QLineEdit(AppPaths::defaultScdPath(), scdBox);
    scdRow->addWidget(m_scdEdit, 1);
    auto* btnBrowse = new QPushButton(tr("浏览…"), scdBox);
    connect(btnBrowse, &QPushButton::clicked, this, &OpsDeployWidget::pickScd);
    scdRow->addWidget(btnBrowse);
    scdRow->addWidget(new QLabel(tr("  输出: %1").arg(AppPaths::outDir()), scdBox));
    root->addWidget(scdBox);

    auto* info = new QGroupBox(tr("装置 genconfig.jar（模型生成）"), this);
    auto* infoLayout = new QVBoxLayout(info);
    m_modelSize = new QLabel(tr("模型大小: —"), info);
    QFont bold = m_modelSize->font();
    bold.setBold(true);
    m_modelSize->setFont(bold);
    infoLayout->addWidget(m_modelSize);
    m_genconfigTime = new QLabel(
        tr("装置耗时: 未测（预期 %1）").arg(QString::fromUtf8(LabOps::GENCONFIG_HINT_TEXT)), info);
    m_genconfigTime->setFont(bold);
    infoLayout->addWidget(m_genconfigTime);
    infoLayout->addWidget(new QLabel(
        tr("说明: 下装生效后，装置用 genconfig.jar 把站级 SCL 展开为 supervision.cfg；"
           "「装置耗时」从生效计到 102 监听。日志中每一步会标 ▶/✔ 与耗时，结尾有汇总。"),
        info));
    root->addWidget(info);

    auto* actions = new QGroupBox(tr("操作"), this);
    auto* actRow = new QHBoxLayout(actions);
    auto* btnGen = new QPushButton(tr("1. 生成配置"), actions);
    auto* btnUp = new QPushButton(tr("2. 下装并生效"), actions);
    auto* btnFull = new QPushButton(tr("一键全流程"), actions);
    auto* btnMms = new QPushButton(tr("检测 MMS(102)"), actions);
    auto* btnOpen = new QPushButton(tr("打开输出目录"), actions);
    connect(btnGen, &QPushButton::clicked, this, [this]() { startJob(LabJobKind::Generate); });
    connect(btnUp, &QPushButton::clicked, this, [this]() { startJob(LabJobKind::Upload); });
    connect(btnFull, &QPushButton::clicked, this, [this]() { startJob(LabJobKind::FullPipeline); });
    connect(btnMms, &QPushButton::clicked, this, [this]() { startJob(LabJobKind::MmsCheck); });
    connect(btnOpen, &QPushButton::clicked, this, &OpsDeployWidget::openOutDir);
    actRow->addWidget(btnGen);
    actRow->addWidget(btnUp);
    actRow->addWidget(btnFull);
    actRow->addWidget(btnMms);
    actRow->addWidget(btnOpen);
    root->addWidget(actions);

    auto* opts = new QHBoxLayout;
    m_waitGenconfig = new QCheckBox(tr("下装后等待并计时装置 genconfig（至 102 监听）"), this);
    m_waitGenconfig->setChecked(true);
    opts->addWidget(m_waitGenconfig);
    m_mmsAfterFull = new QCheckBox(tr("一键流程后自动检测 MMS 端口"), this);
    m_mmsAfterFull->setChecked(true);
    opts->addWidget(m_mmsAfterFull);
    root->addLayout(opts);

    auto* logBox = new QGroupBox(tr("日志"), this);
    auto* logLayout = new QVBoxLayout(logBox);
    auto* logBar = new QHBoxLayout;
    logBar->addWidget(new QLabel(tr("运行日志"), logBox));
    logBar->addStretch();
    auto* btnClear = new QPushButton(tr("清除日志"), logBox);
    connect(btnClear, &QPushButton::clicked, this, &OpsDeployWidget::clearLog);
    logBar->addWidget(btnClear);
    logLayout->addLayout(logBar);
    m_log = new QTextEdit(logBox);
    m_log->setReadOnly(true);
    m_log->setFont(QFont(QStringLiteral("Consolas"), 10));
    logLayout->addWidget(m_log);
    root->addWidget(logBox, 1);

    m_status = new QLabel(tr("就绪"), this);
    root->addWidget(m_status);

    connect(m_hostEdit, &QLineEdit::editingFinished, this, [this]() { m_settings->setHost(m_hostEdit->text()); });
    connect(m_maintPortEdit, &QLineEdit::editingFinished, this, [this]() {
        m_settings->setMaintainPort(m_maintPortEdit->text().toInt());
    });
    connect(m_mmsPortEdit, &QLineEdit::editingFinished, this, [this]() {
        m_settings->setMmsPort(m_mmsPortEdit->text().toInt());
    });

    if (m_worker) {
        connect(m_worker, &LabWorker::logLine, this, &OpsDeployWidget::appendLog);
        connect(m_worker, &LabWorker::finished, this, &OpsDeployWidget::onWorkerFinished);
        connect(m_worker, &LabWorker::modelOutputsChanged, this, &OpsDeployWidget::modelOutputsChanged);
    }

    refreshModelInfo();
}

void OpsDeployWidget::onProbe()
{
    const QString host = m_hostEdit->text().trimmed();
    bool ok1 = false, ok2 = false;
    int mp = 0, mms = 0;
    mp = m_maintPortEdit->text().toInt(&ok1);
    mms = m_mmsPortEdit->text().toInt(&ok2);
    if (!ok1 || !ok2) {
        QMessageBox::warning(this, tr("参数"), tr("端口必须为数字"));
        return;
    }
    appendLog(tr("探测 %1 …").arg(host));
    appendLog(tr("  运维 %1: %2").arg(mp).arg(LabOps::tcpReachable(host, mp) ? tr("通") : tr("不通")));
    appendLog(tr("  MMS %1: %2").arg(mms).arg(LabOps::tcpReachable(host, mms) ? tr("通") : tr("不通")));
}

void OpsDeployWidget::pickScd()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("选择 SCD"), QFileInfo(m_scdEdit->text()).absolutePath(),
        tr("SCD/SCL (*.SCD *.scd *.xml);;所有文件 (*.*)"));
    if (!path.isEmpty()) {
        m_scdEdit->setText(path);
        QSettings settings(QStringLiteral("master_sim"), QStringLiteral("sim_master_qt"));
        settings.setValue(QStringLiteral("scdPath"), path);
    }
}

void OpsDeployWidget::openOutDir()
{
    QDir().mkpath(AppPaths::outDir());
    QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::outDir()));
}

void OpsDeployWidget::clearLog()
{
    m_log->clear();
    appendLog(tr("日志已清除"));
}

void OpsDeployWidget::appendLog(const QString& msg)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_log->append(QStringLiteral("[%1] %2").arg(stamp, msg));
}

void OpsDeployWidget::refreshModelInfo(std::optional<double> measuredSec)
{
    const qint64 size = LabOps::modelSizeBytes(AppPaths::outDir());
    m_modelSize->setText(tr("模型大小: %1（station.SMCD，供装置 genconfig.jar）").arg(LabOps::formatMb(size)));
    if (measuredSec.has_value()) {
        m_lastGenconfigSec = *measuredSec;
        m_genconfigTime->setText(tr("装置耗时: %1（本次实测，模型 %2）")
                                     .arg(LabOps::formatDuration(*measuredSec), LabOps::formatMb(size)));
    } else if (m_lastGenconfigSec.has_value()) {
        m_genconfigTime->setText(tr("装置耗时: %1（上次实测，模型 %2；预期 %3）")
                                     .arg(LabOps::formatDuration(*m_lastGenconfigSec), LabOps::formatMb(size),
                                         QString::fromUtf8(LabOps::GENCONFIG_HINT_TEXT)));
    } else {
        m_genconfigTime->setText(tr("装置耗时: 未测（预期 %1，模型 %2）")
                                     .arg(QString::fromUtf8(LabOps::GENCONFIG_HINT_TEXT), LabOps::formatMb(size)));
    }
}

void OpsDeployWidget::setBusy(bool busy)
{
    m_busy = busy;
    m_status->setText(busy ? tr("运行中…") : tr("就绪"));
}

void OpsDeployWidget::startJob(LabJobKind kind)
{
    if (m_busy) {
        QMessageBox::warning(this, tr("忙"), tr("请等待当前任务结束"));
        return;
    }
    m_settings->setHost(m_hostEdit->text());
    m_settings->setMaintainPort(m_maintPortEdit->text().toInt());
    m_settings->setMmsPort(m_mmsPortEdit->text().toInt());
    const QString scd = m_scdEdit->text().trimmed();
    if (kind == LabJobKind::Generate || kind == LabJobKind::FullPipeline) {
        if (scd.isEmpty() || !QFileInfo::exists(scd)) {
            QMessageBox::warning(this, tr("SCD"), tr("请选择存在的 SCD 文件（浏览…）"));
            return;
        }
    }
    m_worker->setScdPath(scd);
    m_worker->setWaitGenconfig(m_waitGenconfig->isChecked());
    m_worker->setMmsAfterFull(m_mmsAfterFull->isChecked());
    setBusy(true);
    emit requestJob(kind);
}

void OpsDeployWidget::onWorkerFinished(bool ok, double genSec, bool hasGenSec)
{
    setBusy(false);
    if (hasGenSec)
        refreshModelInfo(genSec);
    else
        refreshModelInfo();
    m_status->setText(ok ? tr("完成") : tr("失败"));
    if (!ok)
        QMessageBox::critical(this, tr("失败"), tr("任务失败，请查看日志"));
}
