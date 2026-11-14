#include "ui/mms_master_widget.h"
#include "event/station_event_list_widget.h"
#include "settings/shared_connection_settings.h"
#include "ui/ied_tree_widget.h"
#include "ui/report_ctrl_panel.h"
#include "event/report_event_logic.h"
#include "event/event_format.h"
#include "scd/scd_model.h"
#include "util/app_paths.h"

#include <QEventLoop>
#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QProcessEnvironment>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSplitter>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

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

    loadDefaultData();
}

bool MmsMasterWidget::runSelfTest()
{
    return m_model.devices().size() >= 1;
}

void MmsMasterWidget::prepareForConfigGeneration()
{
    // Parsing a ~100 MB SMCD keeps the file busy on some Windows setups; drop
    // the in-memory tree before the worker thread rewrites out/station.SMCD.
    m_model.clearLoadedModels();
    m_iedTree->setDevices({});
    m_iedStack->setCurrentIndex(0);
}

void MmsMasterWidget::reloadModelData()
{
    m_model.loadIedList(AppPaths::iedListPath());
    m_smcdPath = AppPaths::defaultSmcdPath();
    if (QFileInfo::exists(m_smcdPath))
        m_model.loadSmcd(m_smcdPath);
    m_iedTree->setDevices(m_model.devices());
    if (!m_model.devices().isEmpty())
        m_iedTree->selectIed(m_model.devices().first().iedName);
    refreshCurrentIed();
}

void MmsMasterWidget::loadDefaultData()
{
    m_smcdPath = AppPaths::defaultSmcdPath();
    m_model.loadIedList(AppPaths::iedListPath());
    if (QFileInfo::exists(m_smcdPath))
        m_model.loadSmcd(m_smcdPath);
    else
        appendMmsLog(tr("未找到 station.SMCD: %1").arg(m_smcdPath));
    m_iedTree->setDevices(m_model.devices());
    if (!m_model.devices().isEmpty())
        m_iedTree->selectIed(m_model.devices().first().iedName);
    refreshCurrentIed();
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
    m_model.loadSmcd(path);
    refreshCurrentIed();
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
            QProgressDialog prog(tr("装置 %1 订阅…").arg(ied), tr("取消"), 0, list.size(), this);
            prog.setWindowModality(Qt::WindowModal);
            subscribeRcbDescriptors(list, &prog);
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

void MmsMasterWidget::subscribeRcbDescriptors(const QVector<RcbDescriptor>& list, QProgressDialog* prog)
{
    int okCount = 0;
    int failCount = 0;
    int skippedOffline = 0;
    const bool prevBatch = m_batchSubscribing;
    m_batchSubscribing = true;
    for (int i = 0; i < list.size(); ++i) {
        if (prog) {
            prog->setValue(i);
            if (prog->wasCanceled())
                break;
        }
        const RcbDescriptor& d = list[i];
        /* 只订阅通信工况为“合”的装置；须在 comm_state 到达后再批量订阅（见 onMmsEvent）。 */
        if (!m_commState.isEmpty() && m_commState.contains(d.iedName)
            && !m_commState.value(d.iedName)) {
            ++skippedOffline;
            m_subscribeFailures.remove(d.handlerRef);
            continue;
        }
        if (m_subscribedHandlers.contains(d.handlerRef)) {
            okCount++;
            continue;
        }
        QString lastErr;
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        timeout.setInterval(12000);
        const QMetaObject::Connection conn = connect(
            &m_mms, &MmsClientBridge::eventReceived, &loop,
            [&](const QJsonObject& evt) {
                const QString t = evt.value(QStringLiteral("evt")).toString();
                if (t == QStringLiteral("rcb_enabled")
                    && evt.value(QStringLiteral("handlerRef")).toString() == d.handlerRef) {
                    loop.quit();
                }
                if (t == QStringLiteral("error")) {
                    lastErr = evt.value(QStringLiteral("msg")).toString();
                    loop.quit();
                }
            });
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        m_mms.enableRcb(QJsonObject{
            { QStringLiteral("handlerRef"), d.handlerRef },
            { QStringLiteral("rcbGetRef"), d.getRef },
            { QStringLiteral("dsRef"), d.dsRef },
        });
        timeout.start();
        loop.exec();
        disconnect(conn);
        QApplication::processEvents();

        if (m_subscribedHandlers.contains(d.handlerRef)) {
            m_subscribeFailures.remove(d.handlerRef);
            okCount++;
        } else {
            if (!lastErr.isEmpty())
                m_subscribeFailures.insert(d.handlerRef, lastErr);
            failCount++;
        }

        if (lastErr == QStringLiteral("too many active rcbs")) {
            appendMmsLog(tr("全站订阅已达客户端 RCB 上限，已停止后续订阅"));
            break;
        }
    }
    if (prog)
        prog->setValue(list.size());
    updateSubscribedUi();
    if (list.size() > 1)
        appendMmsLog(tr("批量订阅：成功 %1，失败 %2").arg(okCount).arg(failCount));
    if (skippedOffline > 0)
        appendMmsLog(tr("跳过 %1 个未通信或通信工况为分的装置").arg(skippedOffline));
    m_batchSubscribing = prevBatch;
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

void MmsMasterWidget::subscribeAllStationInternal(bool showProgress)
{
    if (!m_mms.connected())
        return;
    const QVector<RcbDescriptor> list = m_model.allSubscribeRcbs();
    if (showProgress) {
        QProgressDialog prog(tr("全站订阅…"), tr("取消"), 0, list.size(), this);
        prog.setWindowModality(Qt::WindowModal);
        subscribeRcbDescriptors(list, &prog);
    } else {
        subscribeRcbDescriptors(list, nullptr);
    }
}

void MmsMasterWidget::unsubscribeAllStation()
{
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
    if (m_userDisconnect || m_recoverRunning)
        return;
    if (m_commState.isEmpty()) {
        m_pendingAutoRecover = true;
        m_mms.readCommState();
        return;
    }
    m_recoverRunning = true;
    m_linkLostHandled = false;
    appendMmsLog(tr("连接就绪：自动全站订阅与总召唤…"));
    subscribeAllStationInternal(false);
    giAllStationInternal(false);
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
        if (!handler.isEmpty())
            appendMmsLog(tr("报告控制块已启用 %1").arg(handler));
        if (!handler.isEmpty() && !wasSub) {
            appendRcbControlEvent(StationEventCategory::rcbSubscribe(), handler,
                tr("报告控制块已订阅：%1").arg(handler.section(QLatin1Char('$'), -1)));
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
        QHash<QString, bool> st;
        const QJsonArray arr = evt.value(QStringLiteral("entries")).toArray();
        const QVector<IedDeviceEntry>& devs = m_model.devices();
        for (int i = 0; i < arr.size() && i < devs.size(); ++i)
            st.insert(devs[i].iedName, arr[i].toObject().value(QStringLiteral("stVal")).toBool());
        m_commState = st;
        m_iedTree->setCommState(st);
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
        m_pendingAutoRecover = !m_userDisconnect;
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
        if (msg == QStringLiteral("rcb already enabled"))
            return;
        if (msg == QStringLiteral("too many active rcbs"))
            return;
        if ((m_batchSubscribing || m_recoverRunning) && isBenignSubscribeError(msg))
            return;
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
