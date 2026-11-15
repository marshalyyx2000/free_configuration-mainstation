#include "event/event_store.h"
#include "event/event_format.h"

#include <QMutexLocker>

QString StationEvent::searchText() const
{
    return QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8 %9")
        .arg(category, iedName, iedDesc, rcbName, rcbRef, pointName, address, newValue, reasonText);
}

QString StationEvent::detailText() const
{
    return stationEventDetailText(*this);
}

EventStore::EventStore(QObject* parent)
    : QObject(parent)
{
}

void EventStore::append(const StationEvent& ev)
{
    appendBatch({ ev });
}

void EventStore::appendBatch(const QVector<StationEvent>& evs)
{
    if (evs.isEmpty())
        return;
    QMutexLocker locker(&m_mutex);
    for (const StationEvent& ev : evs) {
        m_events.append(ev);
        while (m_events.size() > kMaxEvents)
            m_events.removeFirst();
    }
    emit eventAppended();
}

QVector<StationEvent> EventStore::snapshot() const
{
    QMutexLocker locker(&m_mutex);
    return m_events;
}

QVector<StationEvent> EventStore::snapshotForIed(const QString& iedName) const
{
    if (iedName.isEmpty())
        return {};
    QMutexLocker locker(&m_mutex);
    QVector<StationEvent> out;
    out.reserve(m_events.size());
    for (const StationEvent& ev : m_events) {
        if (ev.iedName == iedName)
            out.append(ev);
    }
    return out;
}
