#include "event/event_store.h"

#include <QMutexLocker>

QString StationEvent::searchText() const
{
    return QStringLiteral("%1 %2 %3 %4 %5 %6").arg(iedName, iedDesc, rcbName, rcbRef, pointName, address);
}

EventStore::EventStore(QObject* parent)
    : QObject(parent)
{
}

void EventStore::append(const StationEvent& ev)
{
    QMutexLocker locker(&m_mutex);
    m_events.append(ev);
    while (m_events.size() > kMaxEvents)
        m_events.removeFirst();
    emit eventAppended();
}

QVector<StationEvent> EventStore::snapshot() const
{
    QMutexLocker locker(&m_mutex);
    return m_events;
}
