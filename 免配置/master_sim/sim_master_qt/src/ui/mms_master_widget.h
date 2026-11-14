#ifndef MMS_MASTER_WIDGET_H
#define MMS_MASTER_WIDGET_H

#include "event/event_store.h"
#include "mms/mms_client_bridge.h"
#include "scd/scd_model.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
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
    void beginSmcdLoad(const QString& path, bool uiFast = true);
    void applySmcdModels(QVector<IedModel> models, quint64 generation);
    void refreshCurrentIed();
    void updateSubscribedUi();
    void appendEventFromReport(const QJsonObject& evt);
    void appendMmsLog(const QString& msg);
    void appendRcbControlEvent(const QString& category, const QString& handlerRef, const QString& summary);
    void appendDeviceEvent(const QString& iedName, const QString& category, const QString& dsRef,
        const QString& summary);
    void startSubscribeQueue(const QVector<RcbDescriptor>& list, bool showProgress, bool giAfter);
    void pumpSubscribeNext();
    void finishSubscribeQueue();
    void cancelSubscribeQueue();
    void applyCommStateEvent(const QJsonObject& evt);
    static QString iedNameFromLd(const QString& ld);
    bool shouldSkipOffline(const QString& iedName) const;
    void dispatchReportToPanels(const QString& iedName, const QJsonObject& evt);
    void flushPendingReports(const QString& iedName);
    IedPageSet* pagesFor(const QString& iedName);
    ReportCtrlPanel* panelForIed(const QString& iedName, ReportPanelType type);
    void subscribeAllStationInternal(bool showProgress, bool giAfter = false);
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
    bool m_commStateAligned = false;
    QHash<QString, QString> m_lastValues;
    QHash<QString, QVector<QJsonObject>> m_pendingReportsByIed;
    QString m_smcdPath;
    quint64 m_smcdLoadGen = 0;
    QTimer m_watchTimer;
    QTimer m_giTimer;
    QSpinBox* m_linkWatchSpin = nullptr;
    QSpinBox* m_stationGiSpin = nullptr;
    bool m_userDisconnect = false;
    bool m_recoverRunning = false;
    bool m_linkLostHandled = false;
    bool m_pendingAutoRecover = false;
    bool m_userHasSubscribed = false;
    bool m_batchSubscribing = false;
    QVector<RcbDescriptor> m_subQueue;
    int m_subIndex = 0;
    int m_subOk = 0;
    int m_subFail = 0;
    int m_subSkip = 0;
    bool m_subGiAfter = false;
    QString m_subWaitingHandler;
    QTimer m_subWaitTimer;
    QProgressDialog* m_subProg = nullptr;
    QHash<QString, int> m_subFailByIed;
    QStringList m_subSkipIeds;

    static bool isBenignSubscribeError(const QString& msg);
};

#endif
