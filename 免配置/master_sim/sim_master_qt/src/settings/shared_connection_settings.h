#ifndef SHARED_CONNECTION_SETTINGS_H
#define SHARED_CONNECTION_SETTINGS_H

#include <QObject>
#include <QString>

class SharedConnectionSettings : public QObject {
    Q_OBJECT
public:
    explicit SharedConnectionSettings(QObject* parent = nullptr);

    QString host() const { return m_host; }
    int maintainPort() const { return m_maintainPort; }
    int mmsPort() const { return m_mmsPort; }

    void setHost(const QString& host);
    void setMaintainPort(int port);
    void setMmsPort(int port);

signals:
    void settingsChanged();

private:
    QString m_host = QStringLiteral("192.168.0.10");
    int m_maintainPort = 2405;
    int m_mmsPort = 102;
};

#endif
