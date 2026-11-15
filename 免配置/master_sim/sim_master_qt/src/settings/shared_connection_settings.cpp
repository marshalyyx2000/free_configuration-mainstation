#include "settings/shared_connection_settings.h"

#include <QSettings>
#include <QtGlobal>

SharedConnectionSettings::SharedConnectionSettings(QObject* parent)
    : QObject(parent)
{
    QSettings settings(QStringLiteral("master_sim"), QStringLiteral("sim_master_qt"));
    m_linkWatchSec = settings.value(QStringLiteral("linkWatchSec"), kDefaultLinkWatchSec).toInt();
    m_stationGiSec = settings.value(QStringLiteral("stationGiSec"), kDefaultStationGiSec).toInt();
    m_commStatePollSec = settings.value(QStringLiteral("commStatePollSec"), kDefaultCommStatePollSec).toInt();
    if (m_linkWatchSec < 30)
        m_linkWatchSec = kDefaultLinkWatchSec;
    if (m_stationGiSec < 60)
        m_stationGiSec = kDefaultStationGiSec;
    if (m_commStatePollSec != 0 && m_commStatePollSec < 15)
        m_commStatePollSec = kDefaultCommStatePollSec;
}

void SharedConnectionSettings::persistInt(const char* key, int value)
{
    QSettings settings(QStringLiteral("master_sim"), QStringLiteral("sim_master_qt"));
    settings.setValue(QString::fromLatin1(key), value);
}

void SharedConnectionSettings::setHost(const QString& host)
{
    const QString h = host.trimmed();
    if (h == m_host)
        return;
    m_host = h.isEmpty() ? QStringLiteral("192.168.0.10") : h;
    emit settingsChanged();
}

void SharedConnectionSettings::setMaintainPort(int port)
{
    if (port == m_maintainPort)
        return;
    m_maintainPort = port;
    emit settingsChanged();
}

void SharedConnectionSettings::setMmsPort(int port)
{
    if (port == m_mmsPort)
        return;
    m_mmsPort = port;
    emit settingsChanged();
}

void SharedConnectionSettings::setLinkWatchSec(int sec)
{
    const int v = qBound(30, sec, 86400);
    if (v == m_linkWatchSec)
        return;
    m_linkWatchSec = v;
    persistInt("linkWatchSec", v);
    emit settingsChanged();
}

void SharedConnectionSettings::setStationGiSec(int sec)
{
    const int v = qBound(60, sec, 86400);
    if (v == m_stationGiSec)
        return;
    m_stationGiSec = v;
    persistInt("stationGiSec", v);
    emit settingsChanged();
}

void SharedConnectionSettings::setCommStatePollSec(int sec)
{
    int v = sec <= 0 ? 0 : qBound(15, sec, 3600);
    if (v == m_commStatePollSec)
        return;
    m_commStatePollSec = v;
    persistInt("commStatePollSec", v);
    emit settingsChanged();
}
