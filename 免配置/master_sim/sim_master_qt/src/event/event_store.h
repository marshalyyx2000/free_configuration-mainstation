#ifndef EVENT_STORE_H
#define EVENT_STORE_H

#include <QDateTime>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>

struct StationEvent {
    QDateTime time;
    /** 变位 / 订阅 / 总召唤 / 取消订阅 / 总召唤上送 / 连接 / 断开 等 */
    QString category;
    QString iedName;
    QString iedDesc;
    QString rcbRef;
    QString rcbName;
    QString pointName;
    QString address;
    QString oldValue;
    QString newValue;
    int reason = 0;
    QString reasonText;
    QString searchText() const;
    QString detailText() const;
};

Q_DECLARE_METATYPE(StationEvent)

class EventStore : public QObject {
    Q_OBJECT
public:
    explicit EventStore(QObject* parent = nullptr);
    void append(const StationEvent& ev);
    void appendBatch(const QVector<StationEvent>& evs);
    QVector<StationEvent> snapshot() const;
    /** Events belonging to one IED only (station-level rows with empty iedName are excluded). */
    QVector<StationEvent> snapshotForIed(const QString& iedName) const;
    static constexpr int kMaxEvents = 10000;

signals:
    void eventAppended();

private:
    mutable QMutex m_mutex;
    QVector<StationEvent> m_events;
};

#endif
