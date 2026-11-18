#include "ui/mms_master_widget.h"
#include "event/station_event_list_widget.h"
#include "mms/comm_state_support.h"
#include "settings/shared_connection_settings.h"
#include "ui/ied_tree_widget.h"
#include "ui/report_ctrl_panel.h"
#include "event/report_event_logic.h"
#include "event/event_format.h"
#include "scd/scd_model.h"
#include "util/app_paths.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QProcessEnvironment>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QAction>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSet>
#include <QSplitter>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <QFutureWatcher>

MmsMasterWidget::MmsMasterWidget(SharedConnectionSettings* settings, QWidget* parent)
    : QWidget(parent)
    , m_settings(settings)
{
    auto* tb = new QToolBar(tr("连接"), this);
    tb->addAction(tr("连接 SubStation"), this, &MmsMasterWidget::connectSubstation);
    tb->addAction(tr("断开"), this, &MmsMasterWidget::disconnectSubstation);
    tb->addSeparator();
    tb->addAction(tr("全站订阅"), this, &MmsMasterWidget::subscribeAllStation);
    tb->addAction(tr("取消全站订阅"), this, &MmsMasterWidget::unsubscribeAllStation);
    tb->addAction(tr("全站总召唤"), this, &MmsMasterWidget::giAllStation);
    tb->addSeparator();
    tb->addAction(tr("打开 SCD/SMCD…"), this, &MmsMasterWidget::openScd);
    tb->addSeparator();
    m_linkStatus = new QLabel(tr("未连接"), this);
    m_linkStatus->setStyleSheet(QStringLiteral("color:#a00; font-weight:bold; padding-left:8px;"));
    tb->addWidget(m_linkStatus);

    auto* cfgTb = new QToolBar(tr("配置"), this);
    cfgTb->addWidget(new QLabel(tr("链路监测(秒)"), this));
    m_linkWatchSpin = new QSpinBox(this);
    m_linkWatchSpin->setRange(30, 86400);
    m_linkWatchSpin->setToolTip(tr("定时 ping 子站；失败则记录原因并自动重连、全站订阅与总召唤"));
    cfgTb->addWidget(m_linkWatchSpin);
    cfgTb->addWidget(new QLabel(tr("  周期总召(秒)"), this));
    m_stationGiSpin = new QSpinBox(this);
    m_stationGiSpin->setRange(60, 86400);
    m_stationGiSpin->setToolTip(tr("已连接且已订阅时周期性全站总召唤"));
    cfgTb->addWidget(m_stationGiSpin);
    cfgTb->addWidget(new QLabel(tr("  工况轮询(秒)"), this));
    m_commStatePollSpin = new QSpinBox(this);
    m_commStatePollSpin->setRange(0, 3600);
    m_commStatePollSpin->setSpecialValueText(tr("关"));
    m_commStatePollSpin->setToolTip(tr("已连接时周期读取 dsCommState；保护由「合」变「分」时自动退订该装置报告"));
    cfgTb->addWidget(m_commStatePollSpin);
    if (m_settings) {
        m_linkWatchSpin->setValue(m_settings->linkWatchSec());
        m_stationGiSpin->setValue(m_settings->stationGiSec());
        m_commStatePollSpin->setValue(m_settings->commStatePollSec());
    }
    connect(m_linkWatchSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_settings)
            m_settings->setLinkWatchSec(v);
    });
    connect(m_stationGiSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_settings)
            m_settings->setStationGiSec(v);
    });
    connect(m_commStatePollSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_settings)
            m_settings->setCommStatePollSec(v);
    });

    m_iedInfo = new QLabel(tr("未选择装置"), this);

    m_iedTree = new IedTreeWidget(this);
    connect(m_iedTree, &IedTreeWidget::iedSelected, this, &MmsMasterWidget::onIedSelected);

    m_iedStack = new QStackedWidget(this);
    auto* rightTabs = new QTabWidget(this);
    auto* detailWrap = new QWidget(this);
    auto* detailLayout = new QVBoxLayout(detailWrap);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->addWidget(m_iedStack, 1);
    rightTabs->addTab(detailWrap, tr("装置详情"));

    m_eventList = new StationEventListWidget(&m_events, this);
    connect(m_eventList, &StationEventListWidget::eventActivated, this, [this](const StationEvent& ev) {
        m_iedTree->selectIed(ev.iedName);
    });
    rightTabs->addTab(m_eventList, tr("全站事件"));

    m_log = new QTextEdit(this);
    m_log->setObjectName(QStringLiteral("MmsMasterLog"));
    m_log->setReadOnly(true);
    m_log->setMinimumHeight(120);
    m_log->setMaximumHeight(220);
    m_log->setPlaceholderText(tr("连接 SubStation、订阅 RCB 等操作日志将显示在此处"));
    m_log->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_log, &QTextEdit::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(m_log);
        QAction* clearAct = menu.addAction(tr("清空日志"));
        const QAction* chosen = menu.exec(m_log->mapToGlobal(pos));
        if (chosen == clearAct)
            m_log->clear();
    });

    auto* split = new QSplitter(this);
    split->addWidget(m_iedTree);
    split->addWidget(rightTabs);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 4);

    auto* v = new QVBoxLayout(this);
    v->addWidget(tb);
    v->addWidget(cfgTb);
    v->addWidget(m_iedInfo);
    v->addWidget(split, 1);
    v->addWidget(new QLabel(tr("MMS 连接日志（与运维页「探测端口」无关，需本页连接 SubStation）"), this));
    v->addWidget(m_log);

    connect(&m_mms, &MmsClientBridge::eventReceived, this, &MmsMasterWidget::onMmsEvent);
    connect(&m_mms, &MmsClientBridge::logMessage, this, [this](const QString& msg) { appendMmsLog(msg); });
    m_mms.setAutoReadCommState(false);
    m_mms.setPreferSmcdFcda(true);
    if (m_settings)
        connect(m_settings, &SharedConnectionSettings::settingsChanged, this, &MmsMasterWidget::onSettingsChanged);

    m_watchTimer.setSingleShot(false);
    connect(&m_watchTimer, &QTimer::timeout, this, &MmsMasterWidget::onWatchTimeout);
    m_giTimer.setSingleShot(false);
    connect(&m_giTimer, &QTimer::timeout, this, &MmsMasterWidget::onPeriodicGiTimeout);
    m_commStateTimer.setSingleShot(false);
    connect(&m_commStateTimer, &QTimer::timeout, this, &MmsMasterWidget::onCommStatePollTimeout);
    m_subWaitTimer.setSingleShot(true);
    connect(&m_subWaitTimer, &QTimer::timeout, this, [this]() {
        if (m_subWaitingHandler.isEmpty() || !m_batchSubscribing)
            return;
        const QString handler = m_subWaitingHandler;
        m_subLatePending.insert(handler);
        m_subscribeFailures.insert(handler, QStringLiteral("TIMEOUT"));
        if (m_subIndex >= 0 && m_subIndex < m_subQueue.size()) {
            const RcbDescriptor& d = m_subQueue[m_subIndex];
            recordSubscribeFailure(d.iedName, handler, d.getRef, QStringLiteral("TIMEOUT"));
        }
        m_subFail++;
        m_subWaitingHandler.clear();
        ++m_subIndex;
        pumpSubscribeNext();
    });

    if (!QCoreApplication::arguments().contains(QStringLiteral("--verify-gui")))
        loadDefaultData();
}

bool MmsMasterWidget::runSelfTest()
{
    return m_model.devices().size() >= 1;
}

void MmsMasterWidget::destroyIedPages()
{
    for (auto it = m_pages.begin(); it != m_pages.end(); ++it) {
        for (ReportCtrlPanel* p : it->panels) {
            if (p)
                p->setIed(IedDeviceEntry{}, nullptr);
        }
    }
    m_pages.clear();
    while (m_iedStack && m_iedStack->count() > 0) {
        QWidget* w = m_iedStack->widget(0);
        m_iedStack->removeWidget(w);
        delete w;
    }
    m_currentDev = {};
}

void MmsMasterWidget::unloadStationModel(const QString& reasonLog)
{
    ++m_smcdLoadGen;
    cancelSubscribeQueue();
    m_watchTimer.stop();
    m_giTimer.stop();
    m_commStateTimer.stop();
    m_pendingReportsByIed.clear();
    m_subscribedHandlers.clear();
    m_subscribeFailures.clear();
    m_lastValues.clear();
    m_enrichedDoiIeds.clear();
    m_enrichingDoiIeds.clear();
    m_smcdIedOffsets.clear();
    m_doiPatchCache.clear();
    m_doiWarmQueue.clear();
    m_doiWarmActive = false;
    ++m_smcdIndexGen;
    ++m_smcdModelGen;
    destroyIedPages();
    if (m_iedTree) {
        m_iedTree->blockSignals(true);
        m_iedTree->setDevices({});
        m_iedTree->blockSignals(false);
    }
    m_model.clearLoadedModels();
    if (m_iedStack)
        m_iedStack->setCurrentIndex(0);
    if (m_iedInfo)
        m_iedInfo->setText(tr("站级模型已卸载"));
    if (!reasonLog.isEmpty())
        appendMmsLog(reasonLog);
}

void MmsMasterWidget::prepareForConfigGeneration()
{
    // 一键全流程会重写 station.SMCD 并让子站重载模型。若此时仍保持 MMS
    // 订阅，ReportCtrlPanel 里的 IedModel* 会在 clearLoadedModels 后悬空，
    // 随后到报即在 Qt6Core 访问违例（WER APPCRASH c0000005）。
    const bool hadLink = m_mms.connected();
    disconnectSubstation();
    unloadStationModel(hadLink
            ? tr("生成/下装前已断开 MMS 并卸载站级模型（子站将重载 supervision.cfg）")
            : tr("生成/下装前已卸载站级模型"));
}

bool MmsMasterWidget::runPrepareGenerationSafetyTest()
{
    IedDeviceEntry dev;
    dev.no = 1;
    dev.iedName = QStringLiteral("TESTIED");
    dev.desc = QStringLiteral("pipeline-safety");
    dev.chPara = QStringLiteral("127.0.0.1:102");

    IedModel im;
    im.iedName = dev.iedName;
    im.desc = dev.desc;
    DataSetEntry ds;
    ds.name = QStringLiteral("dsWarning");
    ds.ldInst = QStringLiteral("LD0");
    DataSetMember mem;
    mem.desc = QStringLiteral("Alm1");
    mem.address = QStringLiteral("TESTIEDLD0/LLN0.Alm1[ST]");
    ds.members.append(mem);
    im.datasets.append(ds);
    ReportEntry rpt;
    rpt.name = QStringLiteral("brcbWarning");
    rpt.datSet = ds.name;
    rpt.ldInst = ds.ldInst;
    rpt.buffered = true;
    im.reports.append(rpt);
    m_model.adoptParsedModels(QVector<IedModel>{ im });

    IedPageSet* set = pagesFor(dev.iedName);
    if (!set || set->panels.isEmpty())
        return false;
    for (ReportCtrlPanel* p : set->panels)
        p->setIed(dev, m_model.modelFor(dev.iedName));

    const QJsonObject report{
        { QStringLiteral("evt"), QStringLiteral("report") },
        { QStringLiteral("rcb"), QStringLiteral("TESTIEDLD0/LLN0$BR$brcbWarning01") },
        { QStringLiteral("ds"), QStringLiteral("TESTIEDLD0/LLN0$dsWarning") },
        { QStringLiteral("items"),
            QJsonArray{ QJsonObject{
                { QStringLiteral("name"), QStringLiteral("TESTIEDLD0/LLN0$ST$Alm1") },
                { QStringLiteral("idx"), 0 },
                { QStringLiteral("value"), QStringLiteral("true") },
                { QStringLiteral("reason"), 1 },
            } } },
    };
    onMmsEvent(report);
    prepareForConfigGeneration();
    onMmsEvent(report);
    onMmsEvent(QJsonObject{
        { QStringLiteral("evt"), QStringLiteral("dataset") },
        { QStringLiteral("ref"), QStringLiteral("TESTIEDLD0/LLN0$dsWarning") },
        { QStringLiteral("rows"), QJsonArray{} },
    });
    onMmsEvent(QJsonObject{
        { QStringLiteral("evt"), QStringLiteral("disconnected") },
        { QStringLiteral("reason"), QStringLiteral("user") },
    });
    QApplication::processEvents();
    return m_pages.isEmpty() && m_model.modelFor(dev.iedName) == nullptr;
}

void MmsMasterWidget::applySmcdModels(QVector<IedModel> models, quint64 generation)
{
    if (generation != m_smcdLoadGen)
        return;
    if (m_model.devices().isEmpty())
        m_model.loadIedList(AppPaths::iedListPath());
    m_model.adoptParsedModels(std::move(models));
    m_enrichedDoiIeds.clear();
    m_enrichingDoiIeds.clear();
    m_smcdIedOffsets.clear();
    m_doiPatchCache.clear();
    m_doiWarmQueue.clear();
    m_doiWarmActive = false;
    ++m_smcdModelGen;
    m_iedTree->setDevices(m_model.devices());
    if (!m_model.devices().isEmpty()) {
        m_iedTree->selectIed(m_model.devices().first().iedName);
        refreshCurrentIed();
    }
    m_iedInfo->setText(tr("站级模型已加载（%1 台装置）").arg(m_model.devices().size()));
    appendMmsLog(tr("站级模型加载完成"));
    const QString smcd = m_smcdPath.isEmpty() ? AppPaths::defaultSmcdPath() : m_smcdPath;
    if (QFileInfo::exists(smcd))
        beginSmcdDoiIndex(smcd);
}

void MmsMasterWidget::reloadModelData()
{
    unloadStationModel(tr("正在重新加载站级模型…"));
    m_model.loadIedList(AppPaths::iedListPath());
    m_smcdPath = AppPaths::defaultSmcdPath();
    if (m_iedTree) {
        m_iedTree->blockSignals(true);
        m_iedTree->setDevices(m_model.devices());
        m_iedTree->blockSignals(false);
    }
    if (QFileInfo::exists(m_smcdPath)) {
        m_iedInfo->setText(tr("正在重新加载站级模型…"));
        beginSmcdLoad(m_smcdPath);
    } else {
        appendMmsLog(tr("未找到 station.SMCD: %1").arg(m_smcdPath));
    }
}

void MmsMasterWidget::loadDefaultData()
{
    m_smcdPath = AppPaths::defaultSmcdPath();
    m_model.loadIedList(AppPaths::iedListPath());
    m_iedTree->setDevices(m_model.devices());
    if (QFileInfo::exists(m_smcdPath)) {
        m_iedInfo->setText(tr("装置列表已就绪，正在后台加载站级模型（可切换至运维页）…"));
        beginSmcdLoad(m_smcdPath);
    } else {
        appendMmsLog(tr("未找到 station.SMCD: %1").arg(m_smcdPath));
    }
}

void MmsMasterWidget::beginSmcdLoad(const QString& path, bool uiFast)
{
    const quint64 gen = ++m_smcdLoadGen;
    auto* watcher = new QFutureWatcher<QVector<IedModel>>(this);
    connect(watcher, &QFutureWatcher<QVector<IedModel>>::finished, this,
        [this, watcher, gen, path]() {
            const QVector<IedModel> models = watcher->result();
            watcher->deleteLater();
            if (models.isEmpty()) {
                if (gen == m_smcdLoadGen) {
                    appendMmsLog(tr("站级模型加载失败: %1").arg(path));
                    m_iedInfo->setText(tr("未加载站级模型"));
                }
                return;
            }
            applySmcdModels(models, gen);
        });
    watcher->setFuture(QtConcurrent::run([path, uiFast]() {
        return ScdModelLoader::parseModelsFromSmcd(path, uiFast);
    }));
}

void MmsMasterWidget::appendMmsLog(const QString& msg)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString line = QStringLiteral("[%1] %2").arg(stamp, msg);
    m_log->append(line);
    const QString path = qEnvironmentVariable("SIM_MASTER_TEST_LOG");
    if (path.isEmpty())
        return;
    static QFile file;
    if (!file.isOpen()) {
        file.setFileName(path);
        file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
    if (file.isOpen()) {
        file.write(line.toUtf8());
        file.write("\n");
        file.flush();
    }
}

void MmsMasterWidget::onSettingsChanged()
{
    if (m_linkWatchSpin && m_settings && m_linkWatchSpin->value() != m_settings->linkWatchSec())
        m_linkWatchSpin->setValue(m_settings->linkWatchSec());
    if (m_stationGiSpin && m_settings && m_stationGiSpin->value() != m_settings->stationGiSec())
        m_stationGiSpin->setValue(m_settings->stationGiSec());
    if (m_commStatePollSpin && m_settings && m_commStatePollSpin->value() != m_settings->commStatePollSec())
        m_commStatePollSpin->setValue(m_settings->commStatePollSec());
    applyWatchTimers(m_mms.connected() && !m_userDisconnect);
}

void MmsMasterWidget::openScd()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("打开 SCD/SMCD"), AppPaths::masterSimRoot(),
        tr("SCD/SMCD (*.scd *.SCD *.smcd *.SMCD);;All (*.*)"));
    if (path.isEmpty())
        return;
    m_smcdPath = path;
    destroyIedPages();
    m_iedInfo->setText(tr("正在加载站级模型…"));
    beginSmcdLoad(path);
}

void MmsMasterWidget::connectSubstation()
{
    if (!m_settings)
        return;
    m_userDisconnect = false;
    m_linkLostHandled = false;
    const QString host = m_settings->host();
    const int port = m_settings->mmsPort();
    if (host.trimmed().isEmpty()) {
        appendMmsLog(tr("子站 IP 为空，请先在「运维 / 下装」页填写并保存"));
        return;
    }
    if (m_mms.connected()) {
        appendMmsLog(tr("已连接 %1:%2").arg(host).arg(port));
        return;
    }
    if (!m_mms.available()) {
        const QString msg = tr("未找到 mms_master_cli.exe（与 sim_master_qt.exe 同目录）\n%1\n请重新运行 install_sim_master.ps1 完整安装。")
                                .arg(m_mms.cliPath());
        appendMmsLog(msg);
        QMessageBox::warning(this, tr("无法连接"), msg);
        return;
    }
    appendMmsLog(tr("开始 MMS 连接 %1:%2，CLI: %3").arg(host).arg(port).arg(m_mms.cliPath()));
    m_linkStatus->setText(tr("正在连接 %1:%2 …").arg(host).arg(port));
    m_linkStatus->setStyleSheet(QStringLiteral("color:#a60; font-weight:bold; padding-left:8px;"));
    m_mms.connectHost(host, port);
    QTimer::singleShot(35000, this, [this, host, port]() {
        if (m_mms.connected())
            return;
        if (!m_linkStatus->text().contains(QStringLiteral("正在连接")))
            return;
        appendMmsLog(tr("连接超时：35s 内未收到 MMS 握手成功（TCP 102 通 ≠ MMS 已关联）"));
        m_linkStatus->setText(tr("连接超时"));
        m_linkStatus->setStyleSheet(QStringLiteral("color:#a00; font-weight:bold; padding-left:8px;"));
    });
}

void MmsMasterWidget::disconnectSubstation()
{
    m_mmsConnectedAt = QDateTime();
    m_userDisconnect = true;
    m_userHasSubscribed = false;
    m_pendingAutoRecover = false;
    cancelSubscribeQueue();
    m_watchTimer.stop();
    m_giTimer.stop();
    m_commStateTimer.stop();
    clearCommStateSnapshot();
    m_mms.disconnectHost();
    m_subscribedHandlers.clear();
    m_subscribeFailures.clear();
    updateSubscribedUi();
    m_linkStatus->setText(tr("未连接"));
    m_linkStatus->setStyleSheet(QStringLiteral("color:#a00; font-weight:bold; padding-left:8px;"));
}

MmsMasterWidget::IedPageSet* MmsMasterWidget::pagesFor(const QString& iedName)
{
    if (m_pages.contains(iedName))
        return &m_pages[iedName];
    IedPageSet set;
    set.tabs = new QTabWidget;
    set.events = new StationEventListWidget(&m_events, set.tabs);
    set.events->setIedFilter(iedName);
    set.tabs->addTab(set.events, tr("装置事件"));
    const struct {
        ReportPanelType type;
        const char* title;
    } defs[] = {
        { ReportPanelType::Setting, "定值" },
        { ReportPanelType::RelayEna, "软压板" },
        { ReportPanelType::Din, "遥信" },
        { ReportPanelType::Ain, "遥测" },
        { ReportPanelType::Other, "其它" },
        { ReportPanelType::ReportInst, "装置报告控制块实例" },
        { ReportPanelType::Wave, "录波文件" },
    };
    for (const auto& d : defs) {
        auto* panel = new ReportCtrlPanel(d.type, &m_mms, set.tabs);
        connect(panel, &ReportCtrlPanel::logLine, this, [this](const QString& msg) { appendMmsLog(msg); });
        connect(panel, &ReportCtrlPanel::deviceEvent, this,
            [this, iedName](const QString& cat, const QString& dsRef, const QString& summary) {
                appendDeviceEvent(iedName, cat, dsRef, summary);
            });
        connect(panel, &ReportCtrlPanel::subscribeAllForIed, this, [this](const QString& ied) {
            if (!m_mms.connected())
                return;
            QVector<RcbDescriptor> list;
            for (const RcbDescriptor& d : m_model.allSubscribeRcbs()) {
                if (d.iedName == ied)
                    list.append(d);
            }
            if (list.isEmpty())
                return;
            startSubscribeQueue(list, true, false);
        });
        set.tabs->addTab(panel, QString::fromUtf8(d.title));
        set.panels.append(panel);
    }
    m_pages.insert(iedName, set);
    m_iedStack->addWidget(set.tabs);
    return &m_pages[iedName];
}

ReportCtrlPanel* MmsMasterWidget::panelForIed(const QString& iedName, ReportPanelType type)
{
    IedPageSet* set = pagesFor(iedName);
    const struct {
        ReportPanelType type;
        int index;
    } map[] = {
        { ReportPanelType::Setting, 1 },
        { ReportPanelType::RelayEna, 2 },
        { ReportPanelType::Din, 3 },
        { ReportPanelType::Ain, 4 },
        { ReportPanelType::Other, 5 },
        { ReportPanelType::ReportInst, 6 },
        { ReportPanelType::Wave, 7 },
    };
    for (const auto& m : map) {
        if (m.type == type && m.index < set->panels.size())
            return set->panels[m.index];
    }
    return nullptr;
}

void MmsMasterWidget::onIedSelected(const IedDeviceEntry& dev)
{
    m_currentDev = dev;
    m_iedInfo->setText(QStringLiteral("%1  IP: %2").arg(dev.iedName, dev.chPara));
    IedPageSet* set = pagesFor(dev.iedName);
    m_iedStack->setCurrentWidget(set->tabs);
    maybeEnrichIedDoi(dev.iedName);
    refreshCurrentIed();
    flushPendingReports(dev.iedName);
}

void MmsMasterWidget::applyDoiEnrichFromCache(const QString& iedName)
{
    const IedDoiEnrichResult cached = m_doiPatchCache.value(iedName);
    if (cached.patches.isEmpty() && cached.doiDesc.isEmpty())
        return;
    finalizeDoiEnrich(iedName, cached, true);
}

void MmsMasterWidget::finalizeDoiEnrich(const QString& iedName, const IedDoiEnrichResult& result, bool quiet)
{
    if (result.patches.isEmpty() && result.doiDesc.isEmpty())
        return;
    m_doiPatchCache.insert(iedName, result);
    const bool changed = m_model.applyIedDoiDescPatches(iedName, result.patches, result.doiDesc);
    if (!quiet && changed)
        appendMmsLog(tr("%1 点描述已补全").arg(iedName));
    m_enrichedDoiIeds.insert(iedName);
    if (m_currentDev.iedName == iedName)
        refreshCurrentIed();
    else if (m_pages.contains(iedName)) {
        IedDeviceEntry dev;
        for (const IedDeviceEntry& d : m_model.devices()) {
            if (d.iedName == iedName) {
                dev = d;
                break;
            }
        }
        const IedModel* model = m_model.modelFor(iedName);
        for (ReportCtrlPanel* p : m_pages[iedName].panels)
            p->setIed(dev, model);
    }
    if (quiet && m_doiEnrichInFlight < kMaxDoiEnrichParallel)
        pumpDoiWarmQueue();
}

void MmsMasterWidget::scheduleDoiEnrichAsync(const QString& iedName, bool quiet)
{
    QString path = m_smcdPath;
    if (path.isEmpty())
        path = AppPaths::defaultSmcdPath();
    const IedModel* iedModel = m_model.modelFor(iedName);
    if (!QFileInfo::exists(path) || !iedModel)
        return;
  const qint64 off = m_smcdIedOffsets.value(iedName, -1);
    if (off < 0)
        return;

    ++m_doiEnrichInFlight;
    m_enrichingDoiIeds.insert(iedName);
    if (!quiet)
        appendMmsLog(tr("正在补全 %1 点描述…").arg(iedName));

    const quint64 modelGen = m_smcdModelGen;
    const IedModel modelCopy = *iedModel;
    auto* watcher = new QFutureWatcher<IedDoiEnrichResult>(this);
    connect(watcher, &QFutureWatcher<IedDoiEnrichResult>::finished, this,
        [this, watcher, modelGen, iedName, quiet]() {
            const IedDoiEnrichResult result = watcher->result();
            watcher->deleteLater();
            m_enrichingDoiIeds.remove(iedName);
            m_doiEnrichInFlight = qMax(0, m_doiEnrichInFlight - 1);
            if (modelGen != m_smcdModelGen) {
                m_doiWarmActive = false;
                return;
            }
            if (result.patches.isEmpty()) {
                if (quiet) {
                    m_doiWarmActive = false;
                    pumpDoiWarmQueue();
                } else {
                    appendMmsLog(tr("%1 点描述补全失败（流式解析无结果，请检查 SMCD）").arg(iedName));
                }
                return;
            }
            finalizeDoiEnrich(iedName, result, quiet);
            if (quiet)
                pumpDoiWarmQueue();
        });
    const QString smcdPath = path;
    watcher->setFuture(QtConcurrent::run([smcdPath, off, iedName, modelCopy]() {
        IedDoiEnrichResult out;
        ScdModelLoader::buildIedDoiDescPatchesAtOffset(smcdPath, off, iedName, modelCopy, &out.patches, &out.doiDesc);
        return out;
    }));
}

void MmsMasterWidget::pumpDoiWarmQueue()
{
    if (m_doiWarmQueue.isEmpty() || m_doiWarmModelGen != m_smcdModelGen || m_doiWarmPath.isEmpty())
        return;

    m_doiWarmActive = true;
    m_smcdPath = m_doiWarmPath;
    while (!m_doiWarmQueue.isEmpty() && m_doiEnrichInFlight < kMaxDoiEnrichParallel) {
        const QString ied = m_doiWarmQueue.first();
        if (m_doiPatchCache.contains(ied) || m_enrichedDoiIeds.contains(ied)) {
            m_doiWarmQueue.removeFirst();
            continue;
        }
        if (m_enrichingDoiIeds.contains(ied)) {
            m_doiWarmQueue.removeFirst();
            continue;
        }
        if (!m_smcdIedOffsets.contains(ied)) {
            m_doiWarmQueue.removeFirst();
            continue;
        }
        m_doiWarmQueue.removeFirst();
        scheduleDoiEnrichAsync(ied, true);
    }
    if (m_doiEnrichInFlight == 0 && m_doiWarmQueue.isEmpty()) {
        if (m_doiWarmActive)
            appendMmsLog(tr("全站点描述预热完成（切换装置应即时显示中文）"));
        m_doiWarmActive = false;
    }
}

void MmsMasterWidget::beginSmcdDoiIndex(const QString& path)
{
    const quint64 idxGen = ++m_smcdIndexGen;
    const quint64 modelGen = m_smcdModelGen;
    m_doiWarmPath = path;
    m_doiWarmModelGen = modelGen;
    m_doiWarmQueue.clear();
    m_doiWarmActive = false;

    auto* watcher = new QFutureWatcher<QHash<QString, qint64>>(this);
    connect(watcher, &QFutureWatcher<QHash<QString, qint64>>::finished, this,
        [this, watcher, idxGen, modelGen, path]() {
            if (idxGen != m_smcdIndexGen) {
                watcher->deleteLater();
                return;
            }
            m_smcdIedOffsets = watcher->result();
            watcher->deleteLater();
            appendMmsLog(tr("点描述索引就绪（%1 台装置），后台预热中…").arg(m_smcdIedOffsets.size()));

            QSet<QString> seen;
            const QString cur = m_currentDev.iedName;
            if (!cur.isEmpty() && m_model.modelFor(cur)) {
                m_doiWarmQueue.append(cur);
                seen.insert(cur);
            }
            for (const IedDeviceEntry& d : m_model.devices()) {
                if (!m_model.modelFor(d.iedName) || seen.contains(d.iedName))
                    continue;
                m_doiWarmQueue.append(d.iedName);
                seen.insert(d.iedName);
            }
            pumpDoiWarmQueue();
            if (!cur.isEmpty())
                maybeEnrichIedDoi(cur);
        });
    watcher->setFuture(QtConcurrent::run([path]() { return ScdModelLoader::smcdIedOffsets(path); }));
}

void MmsMasterWidget::maybeEnrichIedDoi(const QString& iedName)
{
    if (iedName.isEmpty() || m_enrichedDoiIeds.contains(iedName))
        return;
    if (m_doiPatchCache.contains(iedName)) {
        applyDoiEnrichFromCache(iedName);
        return;
    }
    if (m_enrichingDoiIeds.contains(iedName))
        return;

    if (!m_smcdIedOffsets.isEmpty() && m_smcdIedOffsets.contains(iedName)) {
        scheduleDoiEnrichAsync(iedName, false);
        return;
    }

    m_doiWarmQueue.removeAll(iedName);
    m_doiWarmQueue.prepend(iedName);
    if (!m_doiWarmActive && !m_smcdIedOffsets.isEmpty())
        pumpDoiWarmQueue();
}

void MmsMasterWidget::refreshCurrentIed()
{
    if (m_currentDev.iedName.isEmpty())
        return;
    const IedModel* model = m_model.modelFor(m_currentDev.iedName);
    IedPageSet* set = pagesFor(m_currentDev.iedName);
    for (ReportCtrlPanel* p : set->panels)
        p->setIed(m_currentDev, model);
    updateSubscribedUi();
}

void MmsMasterWidget::updateSubscribedUi()
{
    for (auto it = m_pages.begin(); it != m_pages.end(); ++it) {
        for (ReportCtrlPanel* p : it->panels)
            p->setSubscribedHandlers(m_subscribedHandlers, m_subscribeFailures);
    }
}

void MmsMasterWidget::dispatchReportToPanels(const QString& iedName, const QJsonObject& evt)
{
    if (iedName.isEmpty())
        return;
    if (!m_pages.contains(iedName)) {
        auto& queued = m_pendingReportsByIed[iedName];
        if (queued.size() < 64)
            queued.append(evt);
        return;
    }
    for (ReportCtrlPanel* p : m_pages[iedName].panels)
        p->handleMmsEvent(evt);
}

void MmsMasterWidget::flushPendingReports(const QString& iedName)
{
    if (iedName.isEmpty() || !m_pendingReportsByIed.contains(iedName))
        return;
    const QVector<QJsonObject> queued = m_pendingReportsByIed.take(iedName);
    for (const QJsonObject& evt : queued)
        dispatchReportToPanels(iedName, evt);
}

QString MmsMasterWidget::iedNameFromLd(const QString& ld)
{
    return CommStateSupport::iedNameFromLd(ld);
}

void MmsMasterWidget::appendMmsLogChunked(const QString& title, const QStringList& lines, int chunk)
{
    if (lines.isEmpty()) {
        appendMmsLog(title + tr("（无）"));
        return;
    }
    for (int i = 0; i < lines.size(); i += chunk) {
        const QStringList part = lines.mid(i, chunk);
        const QString prefix = i == 0 ? title : QStringLiteral("    ");
        appendMmsLog(prefix + part.join(QStringLiteral("\n    ")));
    }
}

void MmsMasterWidget::retractSubscribeFailure(const QString& handlerRef)
{
    if (handlerRef.isEmpty())
        return;
    m_subscribeFailures.remove(handlerRef);
    for (int i = m_subFailEntries.size() - 1; i >= 0; --i) {
        if (m_subFailEntries.at(i).handlerRef == handlerRef)
            m_subFailEntries.removeAt(i);
    }
}

void MmsMasterWidget::pruneSubFailEntriesForSubscribed()
{
    for (int i = m_subFailEntries.size() - 1; i >= 0; --i) {
        if (m_subscribedHandlers.contains(m_subFailEntries.at(i).handlerRef))
            m_subFailEntries.removeAt(i);
    }
    for (auto it = m_subscribeFailures.constBegin(); it != m_subscribeFailures.constEnd();) {
        if (m_subscribedHandlers.contains(it.key()))
            it = m_subscribeFailures.erase(it);
        else
            ++it;
    }
}

void MmsMasterWidget::recordSubscribeFailure(const QString& iedName, const QString& handlerRef, const QString& getRef,
    const QString& msg)
{
    SubFailEntry e;
    e.iedName = iedName;
    e.handlerRef = handlerRef;
    e.getRef = getRef;
    e.msg = msg;
    m_subFailEntries.append(e);
    if (!iedName.isEmpty())
        ++m_subFailByIed[iedName];
}

void MmsMasterWidget::logSubscribeFailuresByReason(const QString& msg)
{
    QStringList lines;
    QSet<QString> seen;
    for (const SubFailEntry& e : m_subFailEntries) {
        if (e.msg != msg || m_subscribedHandlers.contains(e.handlerRef))
            continue;
        const QString line =
            QStringLiteral("%1 handler=%2 getRef=%3").arg(e.iedName, e.handlerRef, e.getRef);
        if (!seen.contains(line)) {
            seen.insert(line);
            lines.append(line);
        }
    }
    for (auto it = m_subscribeFailures.constBegin(); it != m_subscribeFailures.constEnd(); ++it) {
        if (it.value() != msg || m_subscribedHandlers.contains(it.key()))
            continue;
        const QString line = QStringLiteral("handler=%1").arg(it.key());
        if (!seen.contains(line)) {
            seen.insert(line);
            lines.append(line);
        }
    }
    if (lines.isEmpty())
        return;
    appendMmsLogChunked(tr("涉及报告控制块（%1，共 %2）：").arg(msg).arg(lines.size()), lines);
}

bool MmsMasterWidget::shouldSkipOffline(const QString& iedName) const
{
    return CommStateSupport::shouldSkipOfflineIed(m_commState, m_commStateAligned, iedName);
}

void MmsMasterWidget::clearCommStateSnapshot()
{
    m_commState.clear();
    m_commStateAligned = false;
    if (m_iedTree)
        m_iedTree->setCommState({});
}

void MmsMasterWidget::startSubscribeQueue(const QVector<RcbDescriptor>& list, bool showProgress, bool giAfter,
    int skippedNonProxy)
{
    if (!m_mms.connected() || list.isEmpty())
        return;
    cancelSubscribeQueue();
    m_subQueue = list;
    m_subIndex = 0;
    m_subOk = 0;
    m_subFail = 0;
    m_subSkip = 0;
    m_subGiAfter = giAfter;
    m_subFailByIed.clear();
    m_subFailEntries.clear();
    m_subLatePending.clear();
    m_subSkipIeds.clear();
    m_batchSubscribing = true;
    m_userHasSubscribed = true;
    int onlineEst = 0;
    for (const RcbDescriptor& d : list) {
        if (!shouldSkipOffline(d.iedName))
            ++onlineEst;
    }
    int withModel = 0;
    for (const IedDeviceEntry& dev : m_model.devices()) {
        if (m_model.modelFor(dev.iedName))
            ++withModel;
    }
    int commOn = 0;
    for (auto it = m_commState.constBegin(); it != m_commState.constEnd(); ++it) {
        if (it.value())
            ++commOn;
    }
    appendMmsLog(tr("台账装置 %1 / 有 SMCD 模型 %2 / 本批 RCB %3 / 工况合 %4 / 实际将 enable 约 %5")
            .arg(m_model.devices().size())
            .arg(withModel)
            .arg(list.size())
            .arg(commOn)
            .arg(onlineEst));
    appendMmsLog(tr("全站应订阅 %1（模型），其中未按「分」跳过 %2，工况对齐=%3")
            .arg(list.size())
            .arg(onlineEst)
            .arg(m_commStateAligned ? tr("是") : tr("否（不按分跳过）")));
    if (commOn == 0 && !m_commState.isEmpty())
        appendMmsLog(tr("工况合=0（快照不可信或子站未刷新），不按「分」跳过，将尝试 enable 全部 RCB"));
    if (skippedNonProxy > 0)
        appendMmsLog(tr("已跳过非代理 LD（PIGO/RPIT 等 GOOSE）%1 条，不向子站 enable").arg(skippedNonProxy));
    if (showProgress) {
        m_subProg = new QProgressDialog(tr("全站订阅…"), tr("取消"), 0, list.size(), this);
        m_subProg->setWindowModality(Qt::WindowModal);
        m_subProg->setMinimumDuration(0);
        m_subProg->setValue(0);
        connect(m_subProg, &QProgressDialog::canceled, this, [this]() { cancelSubscribeQueue(); });
    }
    pumpSubscribeNext();
}

void MmsMasterWidget::pumpSubscribeNext()
{
    if (!m_batchSubscribing)
        return;
    while (m_subIndex < m_subQueue.size()) {
        if (m_subProg) {
            m_subProg->setValue(m_subIndex);
            if (m_subProg->wasCanceled()) {
                finishSubscribeQueue();
                return;
            }
        }
        const RcbDescriptor& d = m_subQueue[m_subIndex];
        if (shouldSkipOffline(d.iedName)) {
            ++m_subSkip;
            m_subscribeFailures.remove(d.handlerRef);
            if (!m_subSkipIeds.contains(d.iedName))
                m_subSkipIeds.append(d.iedName);
            ++m_subIndex;
            continue;
        }
        if (m_subscribedHandlers.contains(d.handlerRef)) {
            m_subscribeFailures.remove(d.handlerRef);
            ++m_subOk;
            ++m_subIndex;
            continue;
        }
        if (m_subIndex > 0 && (m_subIndex % 50) == 0)
            appendMmsLog(tr("全站订阅进度 %1/%2（成功 %3 失败 %4 跳过 %5）")
                    .arg(m_subIndex)
                    .arg(m_subQueue.size())
                    .arg(m_subOk)
                    .arg(m_subFail)
                    .arg(m_subSkip));
        m_subWaitingHandler = d.handlerRef;
        m_subWaitTimer.start(45000);
        m_mms.enableRcb(QJsonObject{
            { QStringLiteral("handlerRef"), d.handlerRef },
            { QStringLiteral("rcbGetRef"), d.getRef },
            { QStringLiteral("dsRef"), d.dsRef },
        });
        return;
    }
    finishSubscribeQueue();
}

void MmsMasterWidget::finishSubscribeQueue()
{
    m_subWaitTimer.stop();
    m_subWaitingHandler.clear();
    pruneSubFailEntriesForSubscribed();
    const int total = m_subQueue.size();
    const bool giAfter = m_subGiAfter;
    if (m_subProg) {
        m_subProg->setValue(total);
        m_subProg->deleteLater();
        m_subProg = nullptr;
    }
    m_batchSubscribing = false;
    m_subGiAfter = false;
    updateSubscribedUi();
    if (total > 1) {
        appendMmsLog(tr("批量订阅：成功 %1，失败 %2").arg(m_subOk).arg(m_subFail));
        if (m_subSkip > 0) {
            const int shown = qMin(20, m_subSkipIeds.size());
            appendMmsLog(tr("跳过 %1 个未通信或通信工况为分的条目（装置 %2 台：%3%4）")
                    .arg(m_subSkip)
                    .arg(m_subSkipIeds.size())
                    .arg(m_subSkipIeds.mid(0, shown).join(QStringLiteral(", ")))
                    .arg(m_subSkipIeds.size() > shown ? tr("…") : QString()));
        }
        QHash<QString, QStringList> failLinesByIed;
        for (const SubFailEntry& e : m_subFailEntries) {
            if (m_subscribedHandlers.contains(e.handlerRef))
                continue;
            const QString line = QStringLiteral("%1 %2").arg(e.handlerRef, e.getRef);
            if (!e.msg.isEmpty())
                failLinesByIed[e.iedName].append(line + QStringLiteral(" [") + e.msg + QLatin1Char(']'));
            else
                failLinesByIed[e.iedName].append(line);
        }
        for (auto it = failLinesByIed.constBegin(); it != failLinesByIed.constEnd(); ++it) {
            appendMmsLog(tr("订阅失败汇总 %1：%2 个 RCB").arg(it.key()).arg(it.value().size()));
            appendMmsLogChunked(QStringLiteral("    "), it.value(), 10);
        }
        QHash<QString, int> byReason;
        for (const SubFailEntry& e : m_subFailEntries)
            ++byReason[e.msg];
        for (auto it = byReason.constBegin(); it != byReason.constEnd(); ++it) {
            if (isBenignSubscribeError(it.key()) || it.key() == QStringLiteral("TIMEOUT")
                || it.key() == QStringLiteral("RPTENA_VERIFY_FAILED"))
                logSubscribeFailuresByReason(it.key());
        }
    }
    m_subQueue.clear();
    m_subIndex = 0;
    if (giAfter)
        giAllStationInternal(false);
}

void MmsMasterWidget::cancelSubscribeQueue()
{
    if (!m_batchSubscribing && m_subQueue.isEmpty())
        return;
    m_subWaitTimer.stop();
    m_subWaitingHandler.clear();
    m_subGiAfter = false;
    if (m_subProg) {
        m_subProg->deleteLater();
        m_subProg = nullptr;
    }
    m_batchSubscribing = false;
    m_subQueue.clear();
    m_subIndex = 0;
    updateSubscribedUi();
    appendMmsLog(tr("全站订阅已取消（已成功 %1，失败 %2）").arg(m_subOk).arg(m_subFail));
}

bool MmsMasterWidget::isBenignSubscribeError(const QString& msg)
{
    return msg == QStringLiteral("IED_UNKNOWN") || msg == QStringLiteral("OBJECT_DOES_NOT_EXIST")
        || msg == QStringLiteral("TEMPORARILY_UNAVAILABLE") || msg == QStringLiteral("TIMEOUT")
        || msg == QStringLiteral("OBJECT_REFERENCE_INVALID")
        || msg == QStringLiteral("OBJECT_ACCESS_UNSUPPORTED");
}

void MmsMasterWidget::subscribeAllStation()
{
    if (!m_mms.connected()) {
        QMessageBox::warning(this, tr("提示"), tr("请先连接 SubStation"));
        return;
    }
    /* 先订报告控制块。关联后立刻 getDataSetDirectory 读工况会冲掉 102。 */
    subscribeAllStationInternal(true);
}

void MmsMasterWidget::requestCommStateThenSubscribe()
{
    constexpr int kPostConnectSettleMs = CommStateSupport::kPostConnectDelayMs;
    auto doRead = [this]() {
        if (!m_mms.connected())
            return;
        m_mms.readCommState();
    };
    if (!m_mmsConnectedAt.isValid()) {
        doRead();
        return;
    }
    const qint64 elapsed = m_mmsConnectedAt.msecsTo(QDateTime::currentDateTime());
    const int waitMs = kPostConnectSettleMs - int(elapsed);
    if (waitMs > 0) {
        appendMmsLog(tr("大站 CID 模型：MMS 关联后约 %1 秒再读工况，避免冲击子站…")
                         .arg((waitMs + 999) / 1000));
        QTimer::singleShot(waitMs, this, doRead);
    } else {
        doRead();
    }
}

void MmsMasterWidget::subscribeAllStationInternal(bool showProgress, bool giAfter)
{
    if (!m_mms.connected())
        return;
    int skippedNonProxy = 0;
    startSubscribeQueue(m_model.allSubscribeRcbs(&skippedNonProxy), showProgress, giAfter, skippedNonProxy);
}

void MmsMasterWidget::unsubscribeAllStation()
{
    m_userHasSubscribed = false;
    cancelSubscribeQueue();
    const QSet<QString> handlers = m_subscribedHandlers;
    for (const QString& h : handlers)
        m_mms.disableRcb(h);
    m_subscribedHandlers.clear();
    m_subscribeFailures.clear();
    updateSubscribedUi();
}

void MmsMasterWidget::giAllStation()
{
    giAllStationInternal(true);
}

void MmsMasterWidget::giAllStationInternal(bool userInitiated)
{
    if (!m_mms.connected()) {
        if (userInitiated)
            QMessageBox::warning(this, tr("提示"), tr("请先连接 SubStation"));
        return;
    }
    if (m_subscribedHandlers.isEmpty()) {
        if (userInitiated)
            QMessageBox::warning(this, tr("提示"), tr("全站总召唤未执行：请先全站订阅"));
        return;
    }
    const QSet<QString> handlers = m_subscribedHandlers;
    for (const QString& h : handlers)
        m_mms.triggerGi(h);
    appendMmsLog(tr("全站总召唤：已向 %1 个报告控制块发出总召唤").arg(handlers.size()));
}

void MmsMasterWidget::markLinkLost(const QString& reason)
{
    if (m_linkLostHandled && !m_mms.connected())
        return;
    m_linkLostHandled = true;
    cancelSubscribeQueue();
    m_watchTimer.stop();
    m_giTimer.stop();
    m_commStateTimer.stop();
    clearCommStateSnapshot();
    m_subscribedHandlers.clear();
    m_subscribeFailures.clear();
    updateSubscribedUi();
    m_linkStatus->setText(tr("未连接"));
    m_linkStatus->setStyleSheet(QStringLiteral("color:#a00; font-weight:bold; padding-left:8px;"));
    const QString r = reason.isEmpty() ? tr("未知") : reason;
    appendMmsLog(tr("MMS 链路断开：%1").arg(r));
    appendRcbControlEvent(StationEventCategory::linkDown(), QString(),
        tr("SubStation 连接断开（%1）").arg(r));
    if (!m_userDisconnect)
        QTimer::singleShot(500, this, &MmsMasterWidget::reconnectAndRecover);
}

void MmsMasterWidget::reconnectAndRecover()
{
    if (m_userDisconnect)
        return;
    if (!m_userHasSubscribed)
        return;
    if (m_mms.connected()) {
        onConnectedAutoRecover();
        return;
    }
    appendMmsLog(tr("正在自动重连 SubStation…"));
    connectSubstation();
}

void MmsMasterWidget::onConnectedAutoRecover()
{
    if (m_userDisconnect || m_recoverRunning || !m_userHasSubscribed)
        return;
    m_recoverRunning = true;
    m_linkLostHandled = false;
    appendMmsLog(tr("连接已恢复：继续全站订阅与总召唤…"));
    subscribeAllStationInternal(true, true);
    applyWatchTimers(true);
    m_recoverRunning = false;
}

void MmsMasterWidget::applyWatchTimers(bool startIfConnected)
{
    if (!m_settings)
        return;
    m_watchTimer.setInterval(m_settings->linkWatchSec() * 1000);
    m_giTimer.setInterval(m_settings->stationGiSec() * 1000);
    const int pollSec = m_settings->commStatePollSec();
    if (pollSec > 0)
        m_commStateTimer.setInterval(pollSec * 1000);
    if (startIfConnected && m_mms.connected() && !m_userDisconnect) {
        m_watchTimer.start();
        m_giTimer.start();
        if (pollSec > 0 && qEnvironmentVariable("SIM_MASTER_TEST_LOG").isEmpty())
            m_commStateTimer.start();
        else
            m_commStateTimer.stop();
    } else {
        m_watchTimer.stop();
        m_giTimer.stop();
        m_commStateTimer.stop();
    }
}

void MmsMasterWidget::onCommStatePollTimeout()
{
    if (m_userDisconnect || !m_mms.connected() || m_batchSubscribing)
        return;
    m_mms.readCommState();
}

void MmsMasterWidget::onWatchTimeout()
{
    if (m_userDisconnect)
        return;
    if (!m_mms.connected()) {
        markLinkLost(QStringLiteral("watch_not_connected"));
        return;
    }
    m_mms.ping();
}

void MmsMasterWidget::onPeriodicGiTimeout()
{
    if (m_userDisconnect || !m_mms.connected() || m_subscribedHandlers.isEmpty())
        return;
    appendMmsLog(tr("周期全站总召唤…"));
    giAllStationInternal(false);
}

void MmsMasterWidget::appendEventFromReport(const QJsonObject& evt)
{
    ingestMmsReportEvent(m_model, m_events, m_lastValues, evt);
}

void MmsMasterWidget::appendRcbControlEvent(const QString& category, const QString& handlerRef,
    const QString& summary)
{
    StationEvent se;
    se.time = QDateTime::currentDateTime();
    se.category = category;
    se.pointName = summary;
    se.reasonText = category;
    if (!handlerRef.isEmpty()) {
        const QString ied = m_model.resolveIedFromMmsRef(handlerRef);
        if (!ied.isEmpty()) {
            for (const IedDeviceEntry& d : m_model.devices()) {
                if (d.iedName == ied) {
                    se.iedName = d.iedName;
                    se.iedDesc = d.desc.isEmpty() ? d.iedName : d.desc;
                    break;
                }
            }
        } else {
            for (const IedDeviceEntry& d : m_model.devices()) {
                if (handlerRef.startsWith(d.iedName)) {
                    se.iedName = d.iedName;
                    se.iedDesc = d.desc.isEmpty() ? d.iedName : d.desc;
                    break;
                }
            }
        }
        se.rcbRef = handlerRef;
        se.rcbName = handlerRef.section(QLatin1Char('$'), -1);
        const IedModel* model = m_model.modelFor(se.iedName);
        const ReportEntry* rpt = ScdModelLoader::findReportForHandler(model, handlerRef);
        if (rpt && !rpt->desc.isEmpty())
            se.rcbName = rpt->desc + QStringLiteral(" (") + se.rcbName + QLatin1Char(')');
    }
    m_events.append(se);
}

void MmsMasterWidget::appendDeviceEvent(const QString& iedName, const QString& category,
    const QString& dsRef, const QString& summary)
{
    if (iedName.isEmpty())
        return;
    StationEvent se;
    se.time = QDateTime::currentDateTime();
    se.category = category;
    se.iedName = iedName;
    for (const IedDeviceEntry& d : m_model.devices()) {
        if (d.iedName == iedName) {
            se.iedDesc = d.desc.isEmpty() ? d.iedName : d.desc;
            break;
        }
    }
    if (se.iedDesc.isEmpty())
        se.iedDesc = iedName;
    se.pointName = summary;
    se.address = dsRef;
    se.reasonText = category;
    m_events.append(se);
}

bool MmsMasterWidget::commStateMapsEqual(const QHash<QString, bool>& a, const QHash<QString, bool>& b)
{
    if (a.size() != b.size())
        return false;
    for (auto it = a.constBegin(); it != a.constEnd(); ++it) {
        if (!b.contains(it.key()) || b.value(it.key()) != it.value())
            return false;
    }
    return true;
}

void MmsMasterWidget::unsubscribeRcbsForIed(const QString& iedName)
{
    if (iedName.isEmpty() || !m_mms.connected())
        return;
    QStringList toDisable;
    for (const QString& h : m_subscribedHandlers) {
        if (m_model.resolveIedFromMmsRef(h) == iedName)
            toDisable.append(h);
    }
    if (toDisable.isEmpty())
        return;
    for (const QString& h : toDisable) {
        m_mms.disableRcb(h);
        m_subscribedHandlers.remove(h);
        m_subscribeFailures.remove(h);
        appendRcbControlEvent(StationEventCategory::rcbUnsubscribe(), h,
            tr("通信分自动退订：%1").arg(h.section(QLatin1Char('$'), -1)));
    }
    updateSubscribedUi();
    appendMmsLog(tr("通信分：已自动退订 %1 的 %2 个报告控制块").arg(iedName).arg(toDisable.size()));
    appendDeviceEvent(iedName, tr("通信分"), QString(),
        tr("保护通信分，已自动退订 %1 个报告控制块").arg(toDisable.size()));
}

void MmsMasterWidget::applyCommStateEvent(const QJsonObject& evt)
{
    const QHash<QString, bool> prev = m_commState;
    QHash<QString, bool> st;
    const QJsonArray arr = evt.value(QStringLiteral("entries")).toArray();
    int named = 0;
    int on = 0;
    int off = 0;
    int invalid = 0;
    QSet<QString> known;
    QStringList unknown;
    for (const IedDeviceEntry& d : m_model.devices())
        known.insert(d.iedName);
    for (const QJsonValue& v : arr) {
        const QJsonObject e = v.toObject();
        QString ied = e.value(QStringLiteral("ied")).toString();
        if (ied.isEmpty())
            ied = iedNameFromLd(e.value(QStringLiteral("ld")).toString());
        if (ied.isEmpty())
            continue;
        const bool entryValid = !e.contains(QStringLiteral("valid")) || e.value(QStringLiteral("valid")).toBool();
        if (!entryValid) {
            ++invalid;
            continue;
        }
        ++named;
        const bool sv = e.value(QStringLiteral("stVal")).toBool();
        st.insert(ied, sv);
        if (sv)
            ++on;
        else
            ++off;
        if (!known.isEmpty() && !known.contains(ied) && unknown.size() < 12)
            unknown.append(ied);
    }
    m_commStateAligned = named == arr.size() && named > 0 && invalid == 0;

    const QString source = evt.value(QStringLiteral("source")).toString();
    const QString ref = evt.value(QStringLiteral("ref")).toString();
    appendMmsLog(tr("工况 ref=%1 source=%2，共 %3 条，在线 %4 / 离线 %5")
            .arg(ref.isEmpty() ? QString::fromUtf8(CommStateSupport::kDsCommStateRef) : ref)
            .arg(source.isEmpty() ? QStringLiteral("?") : source)
            .arg(arr.size())
            .arg(on)
            .arg(off));
    if (source == QStringLiteral("smcd_fcda"))
        appendMmsLog(tr("后备路径（SMCD FCDA），建议核对全量读集"));

    QStringList wentOffline;
    QStringList wentOnline;
    if (m_commStateAligned && !prev.isEmpty()) {
        for (auto it = st.constBegin(); it != st.constEnd(); ++it) {
            const QString& ied = it.key();
            if (!prev.contains(ied))
                continue;
            if (prev.value(ied) && !it.value())
                wentOffline.append(ied);
            else if (!prev.value(ied) && it.value())
                wentOnline.append(ied);
        }
    }

    const bool stateChanged = !commStateMapsEqual(prev, st);
    const bool firstSnapshot = prev.isEmpty();

    m_commState = st;
    m_iedTree->setCommState(st);

    for (const QString& ied : wentOffline)
        unsubscribeRcbsForIed(ied);

    if (!stateChanged && !firstSnapshot) {
        appendMmsLog(tr("通信工况无变化（合=%1 分=%2）").arg(on).arg(off));
        return;
    }

    appendMmsLog(tr("通信工况 合=%1 分=%2 未对齐=%3（台账 %4 / 数据集 %5）")
            .arg(on)
            .arg(off)
            .arg(m_commStateAligned ? 0 : arr.size() - named)
            .arg(m_model.devices().size())
            .arg(arr.size()));
    if (!wentOffline.isEmpty())
        appendMmsLog(tr("离线退订：%1").arg(wentOffline.join(QStringLiteral(", "))));
    if (!wentOnline.isEmpty())
        appendMmsLog(tr("恢复通信（未自动再订）：%1").arg(wentOnline.join(QStringLiteral(", "))));
    QStringList onNames;
    QStringList offNames;
    for (const IedDeviceEntry& d : m_model.devices()) {
        if (!st.contains(d.iedName))
            continue;
        const int seq = d.no > 0 ? d.no : 0;
        const QString item = seq > 0
            ? QStringLiteral("%1 %2").arg(seq).arg(d.iedName)
            : d.iedName;
        if (st.value(d.iedName))
            onNames.append(item);
        else
            offNames.append(item);
    }
    auto dumpChunked = [this](const QString& title, const QStringList& names) {
        if (names.isEmpty()) {
            appendMmsLog(title + tr("（无）"));
            return;
        }
        const int chunk = 20;
        for (int i = 0; i < names.size(); i += chunk) {
            const QStringList part = names.mid(i, chunk);
            const QString prefix = i == 0 ? title : QStringLiteral("    ");
            appendMmsLog(prefix + part.join(QStringLiteral(", ")));
        }
    };
    dumpChunked(tr("合（%1）：").arg(onNames.size()), onNames);
    dumpChunked(tr("分（%1）：").arg(offNames.size()), offNames);
    if (!unknown.isEmpty())
        appendMmsLog(tr("工况域名未出现在台账：%1").arg(unknown.join(QStringLiteral(", "))));
    if (invalid > 0)
        appendMmsLog(tr("工况读失败 %1 条（不当作「分」，全站订阅不跳过这些装置）").arg(invalid));
    if (!m_commStateAligned)
        appendMmsLog(tr("工况未按域名对齐，全站订阅不按「分」跳过"));
}

void MmsMasterWidget::onMmsEvent(const QJsonObject& evt)
{
    const QString type = evt.value(QStringLiteral("evt")).toString();
    if (type == QStringLiteral("rcb_enabled")) {
        const QString handler = evt.value(QStringLiteral("handlerRef")).toString();
        const QString getRef = evt.value(QStringLiteral("rcbGetRef")).toString();
        const QString dsRef = evt.value(QStringLiteral("dsRef")).toString();
        const bool verified =
            evt.value(QStringLiteral("verified")).isUndefined() || evt.value(QStringLiteral("verified")).toBool();
        const bool wasSub = !handler.isEmpty() && m_subscribedHandlers.contains(handler);
        if (!handler.isEmpty()) {
            if (verified) {
                retractSubscribeFailure(handler);
                m_subscribedHandlers.insert(handler);
                if (m_subLatePending.remove(handler)) {
                    if (m_subFail > 0)
                        --m_subFail;
                    ++m_subOk;
                }
            } else {
                m_subscribedHandlers.remove(handler);
                m_subscribeFailures.insert(handler, QStringLiteral("RPTENA_VERIFY_FAILED"));
            }
        }
        updateSubscribedUi();
        if (!handler.isEmpty() && !m_batchSubscribing) {
            if (verified)
                appendMmsLog(tr("报告控制块已启用 handler=%1 getRef=%2 dsRef=%3")
                                 .arg(handler, getRef, dsRef));
            else
                appendMmsLog(tr("订阅未生效（子站 RptEna 回读为 false）handler=%1 getRef=%2 dsRef=%3")
                                 .arg(handler, getRef, dsRef));
        }
        if (!handler.isEmpty() && verified && !wasSub) {
            appendRcbControlEvent(StationEventCategory::rcbSubscribe(), handler,
                tr("报告控制块已订阅：%1").arg(handler.section(QLatin1Char('$'), -1)));
        }
        if (m_batchSubscribing && !handler.isEmpty() && handler == m_subWaitingHandler) {
            m_subWaitTimer.stop();
            m_subWaitingHandler.clear();
            if (verified) {
                ++m_subOk;
            } else {
                ++m_subFail;
                if (m_subIndex >= 0 && m_subIndex < m_subQueue.size()) {
                    const RcbDescriptor& d = m_subQueue[m_subIndex];
                    recordSubscribeFailure(d.iedName, handler, getRef.isEmpty() ? d.getRef : getRef,
                        QStringLiteral("RPTENA_VERIFY_FAILED"));
                }
            }
            ++m_subIndex;
            QTimer::singleShot(0, this, &MmsMasterWidget::pumpSubscribeNext);
        }
    }
    if (type == QStringLiteral("ok")) {
        const QString cmd = evt.value(QStringLiteral("cmd")).toString();
        const QString handler = evt.value(QStringLiteral("handlerRef")).toString();
        if (cmd == QStringLiteral("gi")) {
            appendMmsLog(tr("总召唤已发送"));
            if (!handler.isEmpty()) {
                appendRcbControlEvent(StationEventCategory::rcbGi(), handler,
                    tr("已向报告控制块发出总召唤：%1").arg(handler.section(QLatin1Char('$'), -1)));
            }
        } else if (cmd == QStringLiteral("disable_rcb")) {
            appendMmsLog(tr("报告控制块已取消订阅"));
            if (!handler.isEmpty()) {
                m_subscribedHandlers.remove(handler);
                updateSubscribedUi();
                appendRcbControlEvent(StationEventCategory::rcbUnsubscribe(), handler,
                    tr("报告控制块已取消订阅：%1").arg(handler.section(QLatin1Char('$'), -1)));
            }
        }
    }
    if (type == QStringLiteral("files")) {
        const QString fp = evt.value(QStringLiteral("path")).toString();
        const int n = evt.value(QStringLiteral("entries")).toArray().size();
        const QString warn = evt.value(QStringLiteral("warn")).toString();
        if (warn.isEmpty())
            appendMmsLog(tr("录波目录 %1，%2 个文件").arg(fp).arg(n));
        else
            appendMmsLog(tr("录波目录 %1：无列表 (%2)").arg(fp, warn));
    }
    if (type == QStringLiteral("report")) {
        appendEventFromReport(evt);
        const QJsonArray repItems = evt.value(QStringLiteral("items")).toArray();
        if (repItems.isEmpty()) {
            appendMmsLog(tr("收到报告 %1，但无上送条目（请确认已用最新 mms_master_cli）")
                .arg(evt.value(QStringLiteral("rcb")).toString()));
        }
        const QString rcb = evt.value(QStringLiteral("rcb")).toString();
        const QString ds = evt.value(QStringLiteral("ds")).toString();
        QString iedName = m_model.resolveIedFromMmsRef(rcb);
        if (iedName.isEmpty())
            iedName = m_model.resolveIedFromMmsRef(ds);
        if (!iedName.isEmpty()) {
            dispatchReportToPanels(iedName, evt);
        } else {
            for (auto it = m_pages.begin(); it != m_pages.end(); ++it) {
                for (ReportCtrlPanel* p : it->panels)
                    p->handleMmsEvent(evt);
            }
        }
        return;
    }
    if (type == QStringLiteral("comm_state")) {
        applyCommStateEvent(evt);
        if (m_pendingAutoRecover) {
            m_pendingAutoRecover = false;
            onConnectedAutoRecover();
        }
    }
    if (type == QStringLiteral("connected")) {
        const QString host = evt.value(QStringLiteral("host")).toString();
        const int port = evt.value(QStringLiteral("port")).toInt();
        m_mmsConnectedAt = QDateTime::currentDateTime();
        clearCommStateSnapshot();
        m_linkStatus->setText(tr("已连接 %1:%2").arg(host).arg(port));
        m_linkStatus->setStyleSheet(QStringLiteral("color:#080; font-weight:bold; padding-left:8px;"));
        appendMmsLog(tr("SubStation MMS 已连接 %1:%2").arg(host).arg(port));
        appendRcbControlEvent(StationEventCategory::linkUp(), QString(),
            tr("已连接 SubStation %1:%2").arg(host).arg(port));
        for (auto it = m_pages.begin(); it != m_pages.end(); ++it) {
            for (ReportCtrlPanel* p : it->panels)
                p->onConnectionReady();
        }
        m_pendingAutoRecover = false;
        if (!m_userHasSubscribed)
            appendMmsLog(tr("已连接，请点击「全站订阅」后再收变位"));
        else
            QTimer::singleShot(0, this, &MmsMasterWidget::onConnectedAutoRecover);
        applyWatchTimers(true);
    }
    if (type == QStringLiteral("disconnected")) {
        const QString reason = evt.value(QStringLiteral("reason")).toString();
        if (m_userDisconnect || reason == QStringLiteral("user") || reason == QStringLiteral("quit")) {
            m_linkStatus->setText(tr("未连接"));
            m_linkStatus->setStyleSheet(QStringLiteral("color:#a00; font-weight:bold; padding-left:8px;"));
            appendRcbControlEvent(StationEventCategory::linkDown(), QString(), tr("已断开 SubStation 连接"));
        } else {
            markLinkLost(reason.isEmpty() ? QStringLiteral("disconnected") : reason);
        }
    }
    if (type == QStringLiteral("error")) {
        const QString msg = evt.value(QStringLiteral("msg")).toString();
        if (m_batchSubscribing && !m_subWaitingHandler.isEmpty()
            && msg != QStringLiteral("CONNECTION_LOST") && msg != QStringLiteral("NOT_CONNECTED")) {
            const QString handler = m_subWaitingHandler;
            m_subWaitTimer.stop();
            m_subWaitingHandler.clear();
            m_subscribeFailures.insert(handler, msg);
            if (m_subIndex >= 0 && m_subIndex < m_subQueue.size()) {
                const RcbDescriptor& d = m_subQueue[m_subIndex];
                recordSubscribeFailure(d.iedName, handler, d.getRef, msg);
            }
            ++m_subFail;
            if (msg == QStringLiteral("too many active rcbs")) {
                appendMmsLog(tr("全站订阅已达客户端 RCB 上限，已停止后续订阅"));
                finishSubscribeQueue();
                return;
            }
            ++m_subIndex;
            QTimer::singleShot(0, this, &MmsMasterWidget::pumpSubscribeNext);
            if (isBenignSubscribeError(msg) || msg == QStringLiteral("rcb already enabled"))
                return;
        }
        if (msg == QStringLiteral("OK") || msg.compare(QStringLiteral("ok"), Qt::CaseInsensitive) == 0)
            return;
        if (msg == QStringLiteral("rcb already enabled"))
            return;
        if (msg == QStringLiteral("too many active rcbs"))
            return;
        if ((m_batchSubscribing || m_recoverRunning) && isBenignSubscribeError(msg))
            return;
        if (isBenignSubscribeError(msg)) {
            appendMmsLog(tr("订阅失败：%1（报告控制块忙或已被占用；可先「取消全站订阅」后再订，总召请用「总召唤当前数据集」）")
                    .arg(msg));
            if (!m_subWaitingHandler.isEmpty()) {
                QString getRef;
                if (m_subIndex >= 0 && m_subIndex < m_subQueue.size())
                    getRef = m_subQueue[m_subIndex].getRef;
                appendMmsLog(tr("  当前尝试 handler=%1 getRef=%2").arg(m_subWaitingHandler, getRef));
            }
            logSubscribeFailuresByReason(msg);
            return;
        }
        appendMmsLog(tr("MMS 错误: %1").arg(msg));
        if (msg == QStringLiteral("CONNECTION_LOST") || msg == QStringLiteral("NOT_CONNECTED")) {
            appendMmsLog(
                tr("提示：若子站刚连上就断，多为 102 进程退出；大 SMCD 冷启动约 8–15 分钟，勿在关联后立刻全站订阅。"));
            if (!m_userDisconnect)
                markLinkLost(msg);
        } else if (!m_mms.connected()) {
            m_linkStatus->setText(tr("连接失败: %1").arg(msg));
            m_linkStatus->setStyleSheet(QStringLiteral("color:#a00; font-weight:bold; padding-left:8px;"));
        }
    }

    for (auto it = m_pages.begin(); it != m_pages.end(); ++it) {
        for (ReportCtrlPanel* p : it->panels)
            p->handleMmsEvent(evt);
    }
}
