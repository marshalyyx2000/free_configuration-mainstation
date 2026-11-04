#include "ui/mms_master_widget.h"
#include "event/station_event_list_widget.h"
#include "settings/shared_connection_settings.h"
#include "ui/ied_tree_widget.h"
#include "ui/report_ctrl_panel.h"
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
    tb->addSeparator();
    tb->addAction(tr("打开 SCD/SMCD…"), this, &MmsMasterWidget::openScd);
    tb->addSeparator();
    m_linkStatus = new QLabel(tr("未连接"), this);
    m_linkStatus->setStyleSheet(QStringLiteral("color:#a00; font-weight:bold; padding-left:8px;"));
    tb->addWidget(m_linkStatus);

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
    m_log->setMinimumHeight(90);
    m_log->setMaximumHeight(180);

    auto* split = new QSplitter(this);
    split->addWidget(m_iedTree);
    split->addWidget(rightTabs);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 4);

    auto* v = new QVBoxLayout(this);
    v->addWidget(tb);
    v->addWidget(m_iedInfo);
    v->addWidget(split, 1);
    v->addWidget(m_log);

    connect(&m_mms, &MmsClientBridge::eventReceived, this, &MmsMasterWidget::onMmsEvent);
    connect(&m_mms, &MmsClientBridge::logMessage, this, [this](const QString& msg) { appendMmsLog(msg); });
    if (m_settings)
        connect(m_settings, &SharedConnectionSettings::settingsChanged, this, &MmsMasterWidget::onSettingsChanged);

    loadDefaultData();
}

bool MmsMasterWidget::runSelfTest()
{
    return m_model.devices().size() >= 1;
}

void MmsMasterWidget::reloadModelData()
{
    m_model.loadIedList(AppPaths::iedListPath());
    m_iedTree->setDevices(m_model.devices());
    m_smcdPath = AppPaths::defaultSmcdPath();
    if (QFileInfo::exists(m_smcdPath))
        m_model.loadSmcd(m_smcdPath);
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
    m_log->append(msg);
    const QString path = qEnvironmentVariable("SIM_MASTER_TEST_LOG");
    if (path.isEmpty())
        return;
    static QFile file;
    if (!file.isOpen()) {
        file.setFileName(path);
        file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
    if (file.isOpen()) {
        file.write(msg.toUtf8());
        file.write("\n");
        file.flush();
    }
}

void MmsMasterWidget::onSettingsChanged()
{
    Q_UNUSED(m_settings);
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
    if (m_mms.connected()) {
        appendMmsLog(tr("已连接 %1:%2").arg(m_settings->host()).arg(m_settings->mmsPort()));
        return;
    }
    if (!m_mms.available()) {
        const QString msg = tr("未找到 mms_master_cli.exe\n%1").arg(m_mms.cliPath());
        appendMmsLog(msg);
        QMessageBox::warning(this, tr("无法连接"), msg);
        return;
    }
    m_linkStatus->setText(tr("正在连接 %1:%2 …").arg(m_settings->host()).arg(m_settings->mmsPort()));
    m_linkStatus->setStyleSheet(QStringLiteral("color:#a60; font-weight:bold; padding-left:8px;"));
    m_mms.connectHost(m_settings->host(), m_settings->mmsPort());
}

void MmsMasterWidget::disconnectSubstation()
{
    m_mms.disconnectHost();
    m_subscribedHandlers.clear();
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
        { ReportPanelType::Setting, 0 },
        { ReportPanelType::RelayEna, 1 },
        { ReportPanelType::Din, 2 },
        { ReportPanelType::Ain, 3 },
        { ReportPanelType::Other, 4 },
        { ReportPanelType::ReportInst, 5 },
        { ReportPanelType::Wave, 6 },
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
            p->setSubscribedHandlers(m_subscribedHandlers);
    }
}

void MmsMasterWidget::subscribeRcbDescriptors(const QVector<RcbDescriptor>& list, QProgressDialog* prog)
{
    int okCount = 0;
    int failCount = 0;
    for (int i = 0; i < list.size(); ++i) {
        if (prog) {
            prog->setValue(i);
            if (prog->wasCanceled())
                break;
        }
        const RcbDescriptor& d = list[i];
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

        if (m_subscribedHandlers.contains(d.handlerRef))
            okCount++;
        else
            failCount++;

        if (lastErr == QStringLiteral("too many active rcbs")) {
            appendMmsLog(tr("全站订阅已达客户端 RCB 上限，已停止后续订阅"));
            break;
        }
    }
    if (prog)
        prog->setValue(list.size());
    if (list.size() > 1)
        appendMmsLog(tr("批量订阅：成功 %1，失败 %2").arg(okCount).arg(failCount));
}

void MmsMasterWidget::subscribeAllStation()
{
    if (!m_mms.connected()) {
        QMessageBox::warning(this, tr("提示"), tr("请先连接 SubStation"));
        return;
    }
    const QVector<RcbDescriptor> list = m_model.allSubscribeRcbs();
    QProgressDialog prog(tr("全站订阅…"), tr("取消"), 0, list.size(), this);
    prog.setWindowModality(Qt::WindowModal);
    subscribeRcbDescriptors(list, &prog);
}

void MmsMasterWidget::unsubscribeAllStation()
{
    const QSet<QString> handlers = m_subscribedHandlers;
    for (const QString& h : handlers)
        m_mms.disableRcb(h);
    m_subscribedHandlers.clear();
    updateSubscribedUi();
}

void MmsMasterWidget::appendEventFromReport(const QJsonObject& evt)
{
    const QString rcbRef = evt.value(QStringLiteral("rcb")).toString();
    QString iedName;
    for (const IedDeviceEntry& d : m_model.devices()) {
        if (rcbRef.startsWith(d.iedName)) {
            iedName = d.iedName;
            break;
        }
    }
    const IedDeviceEntry* dev = nullptr;
    for (const IedDeviceEntry& d : m_model.devices()) {
        if (d.iedName == iedName) {
            dev = &d;
            break;
        }
    }
    const QJsonArray items = evt.value(QStringLiteral("items")).toArray();
    QVector<StationEvent> batch;
    batch.reserve(items.size());
    for (const QJsonValue& v : items) {
        const QJsonObject it = v.toObject();
        const QString name = it.value(QStringLiteral("name")).toString();
        const QString val = it.value(QStringLiteral("value")).toString();
        const QString key = iedName + QLatin1Char('|') + name;
        const QString oldVal = m_lastValues.value(key);
        if (oldVal == val && !oldVal.isEmpty())
            continue;
        m_lastValues.insert(key, val);
        StationEvent se;
        se.time = QDateTime::currentDateTime();
        if (it.contains(QStringLiteral("ts_ms")))
            se.time = QDateTime::fromMSecsSinceEpoch(it.value(QStringLiteral("ts_ms")).toVariant().toLongLong());
        se.iedName = iedName;
        se.iedDesc = dev ? dev->desc : iedName;
        se.rcbRef = rcbRef;
        se.rcbName = rcbRef.section(QLatin1Char('$'), -1);
        se.pointName = name;
        se.address = name;
        se.oldValue = oldVal;
        se.newValue = val;
        se.reason = it.value(QStringLiteral("reason")).toInt();
        batch.append(se);
    }
    if (!batch.isEmpty())
        m_events.appendBatch(batch);
}

void MmsMasterWidget::onMmsEvent(const QJsonObject& evt)
{
    const QString type = evt.value(QStringLiteral("evt")).toString();
    if (type == QStringLiteral("rcb_enabled")) {
        const QString handler = evt.value(QStringLiteral("handlerRef")).toString();
        if (!handler.isEmpty())
            m_subscribedHandlers.insert(handler);
        updateSubscribedUi();
        if (!handler.isEmpty())
            appendMmsLog(tr("报告控制块已启用 %1").arg(handler));
    }
    if (type == QStringLiteral("ok")) {
        const QString cmd = evt.value(QStringLiteral("cmd")).toString();
        if (cmd == QStringLiteral("gi"))
            appendMmsLog(tr("总召唤已发送"));
        else if (cmd == QStringLiteral("disable_rcb"))
            appendMmsLog(tr("报告控制块已取消订阅"));
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
        const QString rcb = evt.value(QStringLiteral("rcb")).toString();
        QString iedName;
        for (const IedDeviceEntry& d : m_model.devices()) {
            if (rcb.startsWith(d.iedName)) {
                iedName = d.iedName;
                break;
            }
        }
        if (!iedName.isEmpty() && m_pages.contains(iedName)) {
            for (ReportCtrlPanel* p : m_pages[iedName].panels)
                p->handleMmsEvent(evt);
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
    }
    if (type == QStringLiteral("connected")) {
        const QString host = evt.value(QStringLiteral("host")).toString();
        const int port = evt.value(QStringLiteral("port")).toInt();
        m_linkStatus->setText(tr("已连接 %1:%2").arg(host).arg(port));
        m_linkStatus->setStyleSheet(QStringLiteral("color:#080; font-weight:bold; padding-left:8px;"));
        appendMmsLog(tr("SubStation MMS 已连接 %1:%2").arg(host).arg(port));
        for (auto it = m_pages.begin(); it != m_pages.end(); ++it) {
            for (ReportCtrlPanel* p : it->panels)
                p->onConnectionReady();
        }
    }
    if (type == QStringLiteral("disconnected")) {
        m_linkStatus->setText(tr("未连接"));
        m_linkStatus->setStyleSheet(QStringLiteral("color:#a00; font-weight:bold; padding-left:8px;"));
    }
    if (type == QStringLiteral("error")) {
        const QString msg = evt.value(QStringLiteral("msg")).toString();
        if (msg == QStringLiteral("rcb already enabled"))
            return;
        if (msg == QStringLiteral("too many active rcbs"))
            return;
        appendMmsLog(tr("MMS 错误: %1").arg(msg));
        if (!m_mms.connected()) {
            m_linkStatus->setText(tr("连接失败: %1").arg(msg));
            m_linkStatus->setStyleSheet(QStringLiteral("color:#a00; font-weight:bold; padding-left:8px;"));
        }
    }

    for (auto it = m_pages.begin(); it != m_pages.end(); ++it) {
        for (ReportCtrlPanel* p : it->panels)
            p->handleMmsEvent(evt);
    }
}
