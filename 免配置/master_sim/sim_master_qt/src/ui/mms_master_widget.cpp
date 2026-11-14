#include "ui/mms_master_widget.h"
#include "event/station_event_list_widget.h"
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
    if (m_settings) {
        m_linkWatchSpin->setValue(m_settings->linkWatchSec());
        m_stationGiSpin->setValue(m_settings->stationGiSec());
    }
    connect(m_linkWatchSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_settings)
            m_settings->setLinkWatchSec(v);
    });
    connect(m_stationGiSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_settings)
            m_settings->setStationGiSec(v);
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
    if (m_settings)
        connect(m_settings, &SharedConnectionSettings::settingsChanged, this, &MmsMasterWidget::onSettingsChanged);

    m_watchTimer.setSingleShot(false);
    connect(&m_watchTimer, &QTimer::timeout, this, &MmsMasterWidget::onWatchTimeout);
    m_giTimer.setSingleShot(false);
    connect(&m_giTimer, &QTimer::timeout, this, &MmsMasterWidget::onPeriodicGiTimeout);
    m_subWaitTimer.setSingleShot(true);
    connect(&m_subWaitTimer, &QTimer::timeout, this, [this]() {
        if (m_subWaitingHandler.isEmpty() || !m_batchSubscribing)
            return;
        const QString handler = m_subWaitingHandler;
        m_subscribeFailures.insert(handler, QStringLiteral("TIMEOUT"));
        if (m_subIndex >= 0 && m_subIndex < m_subQueue.size()) {
            const QString ied = m_subQueue[m_subIndex].iedName;
            const int n = ++m_subFailByIed[ied];
            if (n == 1) {
                const QString ref = m_subQueue[m_subIndex].getRef;
                appendMmsLog(tr("订阅失败 %1：TIMEOUT（%2）").arg(ied, ref));
            }
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
    m_pendingReportsByIed.clear();
    m_subscribedHandlers.clear();
    m_subscribeFailures.clear();
    m_lastValues.clear();
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
    m_iedTree->setDevices(m_model.devices());
    if (!m_model.devices().isEmpty()) {
        m_iedTree->selectIed(m_model.devices().first().iedName);
        refreshCurrentIed();
    }
    m_iedInfo->setText(tr("站级模型已加载（%1 台装置）").arg(m_model.devices().size()));
    appendMmsLog(tr("站级模型加载完成"));
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
    m_userDisconnect = true;
    m_userHasSubscribed = false;
    m_pendingAutoRecover = false;
    cancelSubscribeQueue();
    m_watchTimer.stop();
    m_giTimer.stop();
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
    refreshCurrentIed();
    flushPendingReports(dev.iedName);
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
    if (ld.endsWith(QStringLiteral("LD0")) && ld.size() > 3)
        return ld.left(ld.size() - 3);
    return ld;
}

bool MmsMasterWidget::shouldSkipOffline(const QString& iedName) const
{
    if (!m_commStateAligned || iedName.isEmpty())
        return false;
    if (!m_commState.contains(iedName))
        return false;
    return !m_commState.value(iedName);
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
        m_subWaitTimer.start(12000);
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
        for (auto it = m_subFailByIed.constBegin(); it != m_subFailByIed.constEnd(); ++it)
            appendMmsLog(tr("订阅失败汇总 %1：%2 个 RCB").arg(it.key()).arg(it.value()));
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
    if (m_commState.isEmpty()) {
        appendMmsLog(tr("正在读取通信工况，完成后自动全站订阅…"));
        m_pendingAutoRecover = false;
        connect(
            &m_mms, &MmsClientBridge::eventReceived, this,
            [this](const QJsonObject& evt) {
                if (evt.value(QStringLiteral("evt")).toString() != QStringLiteral("comm_state"))
                    return;
                subscribeAllStationInternal(true);
            },
            Qt::SingleShotConnection);
        m_mms.readCommState();
        return;
    }
    subscribeAllStationInternal(true);
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
        m_pendingAutoRecover = true;
        m_mms.readCommState();
        return;
    }
    appendMmsLog(tr("正在自动重连 SubStation…"));
    connectSubstation();
}

void MmsMasterWidget::onConnectedAutoRecover()
{
    if (m_userDisconnect || m_recoverRunning || !m_userHasSubscribed)
        return;
    if (m_commState.isEmpty()) {
        m_pendingAutoRecover = true;
        m_mms.readCommState();
        return;
    }
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
    if (startIfConnected && m_mms.connected() && !m_userDisconnect) {
        m_watchTimer.start();
        m_giTimer.start();
    } else {
        m_watchTimer.stop();
        m_giTimer.stop();
    }
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

void MmsMasterWidget::applyCommStateEvent(const QJsonObject& evt)
{
    QHash<QString, bool> st;
    const QJsonArray arr = evt.value(QStringLiteral("entries")).toArray();
    int named = 0;
    int on = 0;
    int off = 0;
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
    m_commStateAligned = named == arr.size() && named > 0;
    m_commState = st;
    m_iedTree->setCommState(st);
    appendMmsLog(tr("通信工况 合=%1 分=%2 未对齐=%3（台账 %4 / 数据集 %5）")
            .arg(on)
            .arg(off)
            .arg(m_commStateAligned ? 0 : arr.size() - named)
            .arg(m_model.devices().size())
            .arg(arr.size()));
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
    if (!m_commStateAligned)
        appendMmsLog(tr("工况未按域名对齐，全站订阅不按「分」跳过"));
}

void MmsMasterWidget::onMmsEvent(const QJsonObject& evt)
{
    const QString type = evt.value(QStringLiteral("evt")).toString();
    if (type == QStringLiteral("rcb_enabled")) {
        const QString handler = evt.value(QStringLiteral("handlerRef")).toString();
        const bool wasSub = !handler.isEmpty() && m_subscribedHandlers.contains(handler);
        if (!handler.isEmpty())
            m_subscribeFailures.remove(handler);
        if (!handler.isEmpty())
            m_subscribedHandlers.insert(handler);
        updateSubscribedUi();
        if (!handler.isEmpty() && !m_batchSubscribing)
            appendMmsLog(tr("报告控制块已启用 %1").arg(handler));
        if (!handler.isEmpty() && !wasSub) {
            appendRcbControlEvent(StationEventCategory::rcbSubscribe(), handler,
                tr("报告控制块已订阅：%1").arg(handler.section(QLatin1Char('$'), -1)));
        }
        if (m_batchSubscribing && !handler.isEmpty() && handler == m_subWaitingHandler) {
            m_subWaitTimer.stop();
            m_subWaitingHandler.clear();
            ++m_subOk;
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
        m_linkStatus->setText(tr("已连接 %1:%2").arg(host).arg(port));
        m_linkStatus->setStyleSheet(QStringLiteral("color:#080; font-weight:bold; padding-left:8px;"));
        appendMmsLog(tr("SubStation MMS 已连接 %1:%2").arg(host).arg(port));
        appendRcbControlEvent(StationEventCategory::linkUp(), QString(),
            tr("已连接 SubStation %1:%2").arg(host).arg(port));
        for (auto it = m_pages.begin(); it != m_pages.end(); ++it) {
            for (ReportCtrlPanel* p : it->panels)
                p->onConnectionReady();
        }
        m_pendingAutoRecover = !m_userDisconnect && m_userHasSubscribed;
        if (!m_userHasSubscribed)
            appendMmsLog(tr("已连接，请点击「全站订阅」后再收变位"));
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
            QString ied;
            if (m_subIndex >= 0 && m_subIndex < m_subQueue.size())
                ied = m_subQueue[m_subIndex].iedName;
            if (!ied.isEmpty()) {
                const int n = ++m_subFailByIed[ied];
                if (n == 1) {
                    const QString ref = m_subQueue[m_subIndex].getRef;
                    appendMmsLog(tr("订阅失败 %1：%2（%3）").arg(ied, msg, ref));
                }
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
        if (msg == QStringLiteral("rcb already enabled"))
            return;
        if (msg == QStringLiteral("too many active rcbs"))
            return;
        if ((m_batchSubscribing || m_recoverRunning) && isBenignSubscribeError(msg))
            return;
        if (isBenignSubscribeError(msg)) {
            appendMmsLog(tr("订阅失败：%1（报告控制块忙或已被占用；可先「取消全站订阅」后再订，总召请用「总召唤当前数据集」）")
                    .arg(msg));
            return;
        }
        appendMmsLog(tr("MMS 错误: %1").arg(msg));
        if (msg == QStringLiteral("CONNECTION_LOST") || msg == QStringLiteral("NOT_CONNECTED")) {
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
