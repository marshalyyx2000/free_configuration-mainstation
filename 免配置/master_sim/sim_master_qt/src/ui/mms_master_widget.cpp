#include "ui/mms_master_widget.h"
#include "event/station_event_list_widget.h"
#include "settings/shared_connection_settings.h"
#include "ui/ied_tree_widget.h"
#include "ui/report_ctrl_panel.h"
#include "util/app_paths.h"

#include <QApplication>
#include <QDateTime>
#include <QFileDialog>
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
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(120);

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
    connect(&m_mms, &MmsClientBridge::logMessage, this, [this](const QString& msg) { m_log->append(msg); });
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
    m_iedTree->setDevices(m_model.devices());
    if (!m_model.devices().isEmpty())
        m_iedTree->selectIed(m_model.devices().first().iedName);
    QTimer::singleShot(0, this, [this]() {
        if (QFileInfo::exists(m_smcdPath))
            m_model.loadSmcd(m_smcdPath);
        refreshCurrentIed();
    });
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
    m_mms.connectHost(m_settings->host(), m_settings->mmsPort());
}

void MmsMasterWidget::disconnectSubstation()
{
    m_mms.disconnectHost();
    m_subscribedHandlers.clear();
    updateSubscribedUi();
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
        connect(panel, &ReportCtrlPanel::subscribeAllForIed, this, [this](const QString& ied) {
            const IedModel* model = m_model.modelFor(ied);
            if (!model)
                return;
            const IedDeviceEntry* dev = nullptr;
            for (const IedDeviceEntry& e : m_model.devices()) {
                if (e.iedName == ied) {
                    dev = &e;
                    break;
                }
            }
            if (!dev)
                return;
            for (const ReportEntry& rpt : model->reports) {
                if (!ScdModelLoader::reportMatchesPanel(rpt, ReportPanelType::Din)
                    && !ScdModelLoader::reportMatchesPanel(rpt, ReportPanelType::RelayEna)
                    && !ScdModelLoader::reportMatchesPanel(rpt, ReportPanelType::Ain))
                    continue;
                const QString handler = ScdModelLoader::rcbHandlerRef(ied, rpt.name, rpt.buffered);
                m_mms.enableRcb(QJsonObject{
                    { QStringLiteral("handlerRef"), handler },
                    { QStringLiteral("rcbGetRef"),
                        ScdModelLoader::rcbGetRef(ied, rpt.name, dev->rptno, rpt.buffered) },
                    { QStringLiteral("dsRef"), ScdModelLoader::datasetRef(ied, rpt.datSet) },
                });
            }
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

void MmsMasterWidget::subscribeAllStation()
{
    if (!m_mms.connected()) {
        QMessageBox::warning(this, tr("提示"), tr("请先连接 SubStation"));
        return;
    }
    const QVector<RcbDescriptor> list = m_model.allSubscribeRcbs();
    QProgressDialog prog(tr("全站订阅…"), tr("取消"), 0, list.size(), this);
    prog.setWindowModality(Qt::WindowModal);
    for (int i = 0; i < list.size(); ++i) {
        prog.setValue(i);
        if (prog.wasCanceled())
            break;
        const RcbDescriptor& d = list[i];
        m_mms.enableRcb(QJsonObject{
            { QStringLiteral("handlerRef"), d.handlerRef },
            { QStringLiteral("rcbGetRef"), d.getRef },
            { QStringLiteral("dsRef"), d.dsRef },
        });
        QApplication::processEvents();
    }
    prog.setValue(list.size());
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
        m_events.append(se);
    }
    m_eventList->refreshView();
}

void MmsMasterWidget::onMmsEvent(const QJsonObject& evt)
{
    const QString type = evt.value(QStringLiteral("evt")).toString();
    if (type == QStringLiteral("rcb_enabled")) {
        const QString handler = evt.value(QStringLiteral("handlerRef")).toString();
        if (!handler.isEmpty())
            m_subscribedHandlers.insert(handler);
        updateSubscribedUi();
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
    if (type == QStringLiteral("report"))
        appendEventFromReport(evt);

    for (auto it = m_pages.begin(); it != m_pages.end(); ++it) {
        for (ReportCtrlPanel* p : it->panels)
            p->handleMmsEvent(evt);
    }
}
