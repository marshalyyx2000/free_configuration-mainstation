#ifndef MMS_SESSION_SERVER_H
#define MMS_SESSION_SERVER_H

#include "mms/mms_client_bridge.h"
#include "scd/scd_model.h"

#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

class QLocalSocket;

class MmsSessionServer : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString host = QStringLiteral("192.168.0.10");
        int mmsPort = 102;
        QString iedList;
        QString smcd;
        bool preferSmcdFcda = false;
    };

    explicit MmsSessionServer(const Options& opt, QObject* parent = nullptr);
    int execConnect();

private slots:
    void onNewConnection();
    void onMmsEvent(const QJsonObject& evt);
    void onMmsLog(const QString& msg);

private:
    QJsonObject handleCommand(const QJsonObject& cmd);
    QJsonObject cmdStatus() const;
    QJsonObject cmdSubscribe(const QJsonObject& cmd);
    QJsonObject cmdGi(const QJsonObject& cmd);
    QJsonObject cmdDisconnect();

    QVector<RcbDescriptor> resolveTargets(const QString& scope, const QString& ied, const QString& rcb,
        QString* err) const;
    bool waitConnected(int timeoutMs);
    bool waitCommState(int timeoutMs);
    bool enableOne(const RcbDescriptor& d, QString* err);
    bool giOne(const QString& handlerRef, QString* err);
    void applyCommState(const QJsonObject& evt);
    bool shouldSkipOffline(const QString& iedName) const;
    static QString iedNameFromLd(const QString& ld);
    void reply(QLocalSocket* sock, const QJsonObject& obj);

    Options m_opt;
    QLocalServer m_server;
    MmsClientBridge m_mms;
    ScdModelLoader m_model;
    QHash<QString, bool> m_commState;
    bool m_commStateAligned = false;
    bool m_gotCommState = false;
    QSet<QString> m_subscribed;
    QHash<QString, RcbDescriptor> m_subByHandler;

    QString m_waitKind;
    QString m_waitHandler;
    bool m_waitOk = false;
    QString m_waitError;
    class QEventLoop* m_waitLoop = nullptr;
    bool m_quitRequested = false;
};

#endif
