#include "ui/master_sim_shell.h"
#include "ui/mms_master_widget.h"
#include "ui/ops_deploy_widget.h"
#include "ui/lab_worker.h"

#include <QMetaObject>
#include "settings/shared_connection_settings.h"

#include <QAction>
#include <QApplication>
#include <QFont>
#include <QtGlobal>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QTabWidget>
#include <QThread>
#include <QTimer>

MasterSimShell::MasterSimShell(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("免配置仿真主站"));
    resize(1280, 820);

    {
        const QFont appFont = QApplication::font();
        m_uiFontDefaultPt = appFont.pointSize() > 0 ? appFont.pointSize() : 9;
        QSettings settings(QStringLiteral("master_sim"), QStringLiteral("sim_master_qt"));
        m_uiFontPt = settings.value(QStringLiteral("uiFontPointSize"), m_uiFontDefaultPt).toInt();
        if (m_uiFontPt != m_uiFontDefaultPt)
            applyUiFont(m_uiFontPt);
        else
            m_uiFontPt = m_uiFontDefaultPt;
    }

    auto* viewMenu = menuBar()->addMenu(tr("查看(&V)"));
    auto* zoomInAct = viewMenu->addAction(tr("增大字体"));
    zoomInAct->setShortcuts({ QKeySequence::ZoomIn, QKeySequence(QStringLiteral("Ctrl+=")),
        QKeySequence(QStringLiteral("Ctrl++")) });
    zoomInAct->setShortcutContext(Qt::ApplicationShortcut);
    addAction(zoomInAct);
    connect(zoomInAct, &QAction::triggered, this, [this]() { adjustUiFont(1); });

    auto* zoomOutAct = viewMenu->addAction(tr("减小字体"));
    zoomOutAct->setShortcuts({ QKeySequence::ZoomOut, QKeySequence(QStringLiteral("Ctrl+-")) });
    zoomOutAct->setShortcutContext(Qt::ApplicationShortcut);
    addAction(zoomOutAct);
    connect(zoomOutAct, &QAction::triggered, this, [this]() { adjustUiFont(-1); });

    auto* helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    auto* aboutAct = helpMenu->addAction(tr("关于 SimMasterQt(&A)..."));
    connect(aboutAct, &QAction::triggered, this, []() {
        const QString ver = QApplication::applicationVersion();
        QMessageBox::about(
            nullptr, tr("SimMasterQt"),
            tr("<h3>SimMasterQt</h3>"
               "<p>版本 %1</p>"
               "<p>免配置仿真主站 — 61850 MMS 主站与运维下装。</p>"
               "<p>安装路径默认：<code>C:\\Program Files\\SimMasterQt</code></p>")
                .arg(ver.isEmpty() ? QStringLiteral("—") : ver));
    });

    m_settings = new SharedConnectionSettings(this);

    m_workerThread = new QThread(this);
    m_worker = new LabWorker(m_settings, nullptr);
    m_worker->moveToThread(m_workerThread);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_workerThread->start();

    auto* tabs = new QTabWidget(this);
    m_ops = new OpsDeployWidget(m_settings, m_worker, tabs);
    m_mms = new MmsMasterWidget(m_settings, tabs);
    tabs->addTab(m_ops, tr("运维 / 下装"));
    tabs->addTab(m_mms, tr("61850 主站"));
    setCentralWidget(tabs);
    connect(tabs, &QTabWidget::currentChanged, this, [this, tabs](int index) {
        if (tabs->widget(index) == m_mms && m_ops)
            m_ops->syncConnectionFieldsToSettings();
    });

    connect(m_ops, &OpsDeployWidget::requestJob, this, [this](LabJobKind kind) {
        if (kind == LabJobKind::Generate || kind == LabJobKind::FullPipeline || kind == LabJobKind::Upload)
            m_mms->prepareForConfigGeneration();
        QMetaObject::invokeMethod(m_worker, "runJob", Qt::QueuedConnection, Q_ARG(LabJobKind, kind));
    });
    connect(m_worker, &LabWorker::modelOutputsChanged, m_mms, &MmsMasterWidget::reloadModelData);

    QTimer::singleShot(0, this, [this]() {
        if (m_ops)
            m_ops->syncConnectionFieldsToSettings();
    });
}

MasterSimShell::~MasterSimShell()
{
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(3000);
    }
}

bool MasterSimShell::runSelfTest()
{
    return m_mms && m_mms->runPrepareGenerationSafetyTest();
}

void MasterSimShell::adjustUiFont(int delta)
{
    applyUiFont(m_uiFontPt + delta);
}

void MasterSimShell::applyUiFont(int pointSize)
{
    const int pt = qBound(8, pointSize, 28);
    m_uiFontPt = pt;
    QFont f = QApplication::font();
    f.setPointSize(pt);
    QApplication::setFont(f);
    setFont(f);
    QSettings settings(QStringLiteral("master_sim"), QStringLiteral("sim_master_qt"));
    settings.setValue(QStringLiteral("uiFontPointSize"), pt);
}
