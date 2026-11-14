#ifndef MMS_MASTER_WIDGET_H
#define MMS_MASTER_WIDGET_H

#include "event/event_store.h"
#include "mms/mms_client_bridge.h"
#include "scd/scd_model.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QSpinBox;
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
    void prepareForConfigGeneration();
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
    void onWatchTimeout();
    void onPeriodicGiTimeout();

private:
    struct IedPageSet {
        QTabWidget* tabs = nullptr;
        StationEventListWidget* events = nullptr;
        QVector<ReportCtrlPanel*> panels;
    };

    void loadDefaultData();
    void refreshCurrentIed();
    void updateSubscribedUi();
    void appendEventFromReport(const QJsonObject& evt);
    void appendMmsLog(const QString& msg);
    void appendRcbControlEvent(const QString& category, const QString& handlerRef, const QString& summary);
    void appendDeviceEvent(const QString& iedName, const QString& category, const QString& dsRef,
        const QString& summary);
    void subscribeRcbDescriptors(const QVector<RcbDescriptor>& list, QProgressDialog* prog);
    void dispatchReportToPanels(const QString& iedName, const QJsonObject& evt);
    void flushPendingReports(const QString& iedName);
    IedPageSet* pagesFor(const QString& iedName);
    ReportCtrlPanel* panelForIed(const QString& iedName, ReportPanelType type);
    void subscribeAllStationInternal(bool showProgress);
    void giAllStationInternal(bool userInitiated);
    void markLinkLost(const QString& reason);
    void reconnectAndRecover();
    void onConnectedAutoRecover();
    void applyWatchTimers(bool startIfConnected);

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
    QHash<QString, QString> m_subscribeFailures;
    QHash<QString, bool> m_commState;
    QHash<QString, QString> m_lastValues;
    QHash<QString, QVector<QJsonObject>> m_pendingReportsByIed;
    QString m_smcdPath;
    QTimer m_watchTimer;
    QTimer m_giTimer;
    QSpinBox* m_linkWatchSpin = nullptr;
    QSpinBox* m_stationGiSpin = nullptr;
    bool m_userDisconnect = false;
    bool m_recoverRunning = false;
    bool m_linkLostHandled = false;
    bool m_pendingAutoRecover = false;
    bool m_batchSubscribing = false;

    static bool isBenignSubscribeError(const QString& msg);
};

#endif
