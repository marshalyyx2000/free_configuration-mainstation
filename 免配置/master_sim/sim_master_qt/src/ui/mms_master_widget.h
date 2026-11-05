#ifndef MMS_MASTER_WIDGET_H
#define MMS_MASTER_WIDGET_H

#include "event/event_store.h"
#include "mms/mms_client_bridge.h"
#include "scd/scd_model.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QVector>
#include <QWidget>

class SharedConnectionSettings;
class IedTreeWidget;
class StationEventListWidget;
class ReportCtrlPanel;
class QStackedWidget;
class QTabWidget;
class QProgressDialog;
class QLabel;
class QTextEdit;

class MmsMasterWidget : public QWidget {
    Q_OBJECT
public:
    explicit MmsMasterWidget(SharedConnectionSettings* settings, QWidget* parent = nullptr);
    bool runSelfTest();

public slots:
    void reloadModelData();
    void connectSubstation();
    void disconnectSubstation();

private slots:
    void openScd();
    void subscribeAllStation();
    void unsubscribeAllStation();
    void giAllStation();
    void onIedSelected(const IedDeviceEntry& dev);
    void onMmsEvent(const QJsonObject& evt);
    void onSettingsChanged();

private:
    struct IedPageSet {
        QTabWidget* tabs = nullptr;
        QVector<ReportCtrlPanel*> panels;
    };

    void loadDefaultData();
    void refreshCurrentIed();
    void updateSubscribedUi();
    void appendEventFromReport(const QJsonObject& evt);
    void appendMmsLog(const QString& msg);
    void appendRcbControlEvent(const QString& category, const QString& handlerRef, const QString& summary);
    void subscribeRcbDescriptors(const QVector<RcbDescriptor>& list, QProgressDialog* prog);
    void dispatchReportToPanels(const QString& iedName, const QJsonObject& evt);
    void flushPendingReports(const QString& iedName);
    IedPageSet* pagesFor(const QString& iedName);
    ReportCtrlPanel* panelForIed(const QString& iedName, ReportPanelType type);

    SharedConnectionSettings* m_settings = nullptr;
    ScdModelLoader m_model;
    MmsClientBridge m_mms;
    EventStore m_events;
    IedTreeWidget* m_iedTree = nullptr;
    StationEventListWidget* m_eventList = nullptr;
    QStackedWidget* m_iedStack = nullptr;
    QLabel* m_iedInfo = nullptr;
    QLabel* m_linkStatus = nullptr;
    QTextEdit* m_log = nullptr;
    IedDeviceEntry m_currentDev;
    QHash<QString, IedPageSet> m_pages;
    QSet<QString> m_subscribedHandlers;
    QHash<QString, bool> m_commState;
    QHash<QString, QString> m_lastValues;
    QHash<QString, QVector<QJsonObject>> m_pendingReportsByIed;
    QString m_smcdPath;
};

#endif
