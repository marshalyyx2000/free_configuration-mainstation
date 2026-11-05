#ifndef EVENT_FORMAT_H
#define EVENT_FORMAT_H

#include "event/event_store.h"

#include <QJsonValue>
#include <QString>

QString formatReportValue(const QJsonValue& v);
QString formatReportReason(int reasonCode);
QString stationEventDetailText(const StationEvent& ev);

namespace StationEventCategory {
QString pointChange();
QString rcbSubscribe();
QString rcbGi();
QString rcbUnsubscribe();
QString giReport();
QString linkUp();
QString linkDown();
} // namespace StationEventCategory

#endif
