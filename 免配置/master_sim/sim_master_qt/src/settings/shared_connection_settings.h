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
    int linkWatchSec() const { return m_linkWatchSec; }
    int stationGiSec() const { return m_stationGiSec; }
    int commStatePollSec() const { return m_commStatePollSec; }

    void setHost(const QString& host);
    void setMaintainPort(int port);
    void setMmsPort(int port);
    void setLinkWatchSec(int sec);
    void setStationGiSec(int sec);
    void setCommStatePollSec(int sec);

    static constexpr int kDefaultLinkWatchSec = 300;
    static constexpr int kDefaultStationGiSec = 3600;
    static constexpr int kDefaultCommStatePollSec = 60;

signals:
    void settingsChanged();

private:
    void persistInt(const char* key, int value);

    QString m_host = QStringLiteral("192.168.0.10");
    int m_maintainPort = 2405;
    int m_mmsPort = 102;
    int m_linkWatchSec = kDefaultLinkWatchSec;
    int m_stationGiSec = kDefaultStationGiSec;
    int m_commStatePollSec = kDefaultCommStatePollSec;
};

#endif
