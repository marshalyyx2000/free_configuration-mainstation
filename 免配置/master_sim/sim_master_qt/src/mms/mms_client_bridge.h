#ifndef MMS_CLIENT_BRIDGE_H
#define MMS_CLIENT_BRIDGE_H

#include <QObject>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>
#include <QVector>

class MmsClientBridge : public QObject {
    Q_OBJECT
public:
    explicit MmsClientBridge(QObject* parent = nullptr);
    bool available() const;
    bool connected() const { return m_connected; }
    QString cliPath() const { return m_cliPath; }
    void setCliPath(const QString& path) { m_cliPath = path; }

public slots:
    void startCli();
    void stopCli();
    void connectHost(const QString& host, int port = 102);
    void disconnectHost();
    void readDataSet(const QString& dsRef, const QString& requestId, const QStringList& items = {});
    void enableRcb(const QJsonObject& params);
    void disableRcb(const QString& handlerRef, const QString& rcbGetRef = QString());
    void triggerGi(const QString& handlerRef);
    void readCommState();
    void listFiles(const QString& path, const QString& requestId);
    void ping();

signals:
    void eventReceived(const QJsonObject& evt);
    void logMessage(const QString& msg);

private slots:
    void onReadyRead();
    void onReadyReadStderr();
    void onProcessFinished(int code, QProcess::ExitStatus status);

private:
    void sendCommand(const QJsonObject& cmd);
    void flushPending();
    void handleLine(const QByteArray& line);

    QProcess m_proc;
    QString m_cliPath;
    bool m_connected = false;
    bool m_cliReady = false;
    bool m_signalsHooked = false;
    QByteArray m_readBuffer;
    QVector<QJsonObject> m_pending;
};

#endif
