#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "event/event_store.h"
#include "mms/mms_client_bridge.h"
#include "scd/scd_model.h"

#include <QMainWindow>
#include <QSet>

class IedTreeWidget;
class StationEventListWidget;
class ReportCtrlPanel;
class QStackedWidget;
class QTabWidget;
class QLabel;
class QTextEdit;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    bool runSelfTest();

private slots:
    void openScd();
    void connectSubstation();
    void disconnectSubstation();
    void subscribeAllStation();
    void unsubscribeAllStation();
    void onIedSelected(const IedDeviceEntry& dev);
    void onMmsEvent(const QJsonObject& evt);
    void showAbout();

private:
    struct IedPageSet {
        QTabWidget* tabs = nullptr;
        QVector<ReportCtrlPanel*> panels;
    };

    void loadDefaultData();
    void refreshCurrentIed();
    void updateSubscribedUi();
    void appendEventFromReport(const QJsonObject& evt);
    IedPageSet* pagesFor(const QString& iedName);
    ReportCtrlPanel* panelForIed(const QString& iedName, ReportPanelType type);

    ScdModelLoader m_model;
    MmsClientBridge m_mms;
    EventStore m_events;
    IedTreeWidget* m_iedTree = nullptr;
    StationEventListWidget* m_eventList = nullptr;
    QStackedWidget* m_iedStack = nullptr;
    QLabel* m_iedInfo = nullptr;
    QTextEdit* m_log = nullptr;
    IedDeviceEntry m_currentDev;
    QHash<QString, IedPageSet> m_pages;
    QSet<QString> m_subscribedHandlers;
    QHash<QString, bool> m_commState;
    QHash<QString, QString> m_lastValues;
    QString m_smcdPath;
};

#endif
