#ifndef EVENT_FORMAT_H
#define EVENT_FORMAT_H

#include "event/event_store.h"

#include <QJsonValue>
#include <QString>
#include <QStringList>

QString formatReportValue(const QJsonValue& v);
/** 按参引成员名展开。members 为空时仍保留数组里的每一项，不再只留 stVal。 */
QString formatReportMembers(const QJsonValue& v, const QStringList& members);
QString formatReportReason(int reasonCode);
QString stationEventDetailText(const StationEvent& ev);

namespace StationEventCategory {
QString pointChange();
QString rcbSubscribe();
QString rcbGi();
QString rcbUnsubscribe();
QString giReport();
QString datasetRead();
QString datasetReadDone();
QString linkUp();
QString linkDown();
} // namespace StationEventCategory

#endif
