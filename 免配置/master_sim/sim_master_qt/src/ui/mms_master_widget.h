#ifndef MMS_MASTER_WIDGET_H
#define MMS_MASTER_WIDGET_H

#include "event/event_store.h"
#include "mms/mms_client_bridge.h"
#include "scd/scd_model.h"

#include <QDateTime>
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
    /** 模拟「一键全流程」卸载模型后再灌 MMS 事件，不得崩溃。 */
    bool runPrepareGenerationSafetyTest();

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
    void onCommStatePollTimeout();

private:
    struct IedPageSet {
        QTabWidget* tabs = nullptr;
        StationEventListWidget* events = nullptr;
        QVector<ReportCtrlPanel*> panels;
    };

    void loadDefaultData();
    void beginSmcdLoad(const QString& path, bool uiFast = true);
    void applySmcdModels(QVector<IedModel> models, quint64 generation);
    void destroyIedPages();
    void unloadStationModel(const QString& reasonLog);
    void refreshCurrentIed();
    void updateSubscribedUi();
    void appendEventFromReport(const QJsonObject& evt);
    void appendMmsLog(const QString& msg);
    void appendRcbControlEvent(const QString& category, const QString& handlerRef, const QString& summary);
    void appendDeviceEvent(const QString& iedName, const QString& category, const QString& dsRef,
        const QString& summary);
    void startSubscribeQueue(const QVector<RcbDescriptor>& list, bool showProgress, bool giAfter,
        int skippedNonProxy = 0);
    void pumpSubscribeNext();
    void finishSubscribeQueue();
    void cancelSubscribeQueue();
    void applyCommStateEvent(const QJsonObject& evt);
    void clearCommStateSnapshot();
    void requestCommStateThenSubscribe();
    void unsubscribeRcbsForIed(const QString& iedName);
    static bool commStateMapsEqual(const QHash<QString, bool>& a, const QHash<QString, bool>& b);
    static QString iedNameFromLd(const QString& ld);
    bool shouldSkipOffline(const QString& iedName) const;
    void dispatchReportToPanels(const QString& iedName, const QJsonObject& evt);
    void flushPendingReports(const QString& iedName);
    IedPageSet* pagesFor(const QString& iedName);
    ReportCtrlPanel* panelForIed(const QString& iedName, ReportPanelType type);
    void subscribeAllStationInternal(bool showProgress, bool giAfter = false);
    void giAllStationInternal(bool userInitiated);
    void pumpGiNext();
    void finishGiQueue(const QString& reason);
    void markLinkLost(const QString& reason);
    void reconnectAndRecover();
    void onConnectedAutoRecover();
    void applyWatchTimers(bool startIfConnected);
    void maybeEnrichIedDoi(const QString& iedName);
    void beginSmcdDoiIndex(const QString& path);
    void applyDoiEnrichFromCache(const QString& iedName);
    void finalizeDoiEnrich(const QString& iedName, const IedDoiEnrichResult& result, bool quiet);
    void pumpDoiWarmQueue();
    void scheduleDoiEnrichAsync(const QString& iedName, bool quiet);
    void recordSubscribeFailure(const QString& iedName, const QString& handlerRef, const QString& getRef,
        const QString& msg);
    void retractSubscribeFailure(const QString& handlerRef);
    void pruneSubFailEntriesForSubscribed();
    void appendMmsLogChunked(const QString& title, const QStringList& lines, int chunk = 15);
    void logSubscribeFailuresByReason(const QString& msg);

    struct SubFailEntry {
        QString iedName;
        QString handlerRef;
        QString getRef;
        QString msg;
    };

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
    QTimer m_commStateTimer;
    QSpinBox* m_linkWatchSpin = nullptr;
    QSpinBox* m_stationGiSpin = nullptr;
    QSpinBox* m_commStatePollSpin = nullptr;
    QDateTime m_mmsConnectedAt;
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
    QSet<QString> m_subLatePending;
    QTimer m_subWaitTimer;
    QStringList m_giQueue;
    int m_giTotal = 0;
    int m_giOk = 0;
    int m_giFail = 0;
    bool m_giActive = false;
    bool m_giInFlight = false;
    QTimer m_giWaitTimer;
    QProgressDialog* m_giProg = nullptr;
    QProgressDialog* m_subProg = nullptr;
    QHash<QString, int> m_subFailByIed;
    QVector<SubFailEntry> m_subFailEntries;
    QStringList m_subSkipIeds;
    QSet<QString> m_enrichedDoiIeds;
    QSet<QString> m_enrichingDoiIeds;
    /** 站级模型代际；变更时丢弃进行中的点描述补全（不把装置误标为已补全）。 */
    quint64 m_smcdModelGen = 0;
    QHash<QString, qint64> m_smcdIedOffsets;
    QHash<QString, IedDoiEnrichResult> m_doiPatchCache;
    quint64 m_smcdIndexGen = 0;
    QString m_doiWarmPath;
    quint64 m_doiWarmModelGen = 0;
    QStringList m_doiWarmQueue;
    bool m_doiWarmActive = false;
    int m_doiEnrichInFlight = 0;
    static constexpr int kMaxDoiEnrichParallel = 3;

    static bool isBenignSubscribeError(const QString& msg);
};

#endif
