#include "settings/shared_connection_settings.h"

SharedConnectionSettings::SharedConnectionSettings(QObject* parent)
    : QObject(parent)
{
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
