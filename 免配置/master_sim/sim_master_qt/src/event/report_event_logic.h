#ifndef REPORT_EVENT_LOGIC_H
#define REPORT_EVENT_LOGIC_H

#include "scd/scd_model.h"

#include <QJsonObject>
#include <QVector>

class EventStore;

/** True when an included report element should not create a station event row. */
bool shouldSkipReportEventItem(const QString& oldVal, const QString& newVal, int reason);

/** Ingest one MMS report JSON object into the event store (same rules as live UI). */
void ingestMmsReportEvent(ScdModelLoader& model, EventStore& store, QHash<QString, QString>& lastValues,
    const QJsonObject& evt);

/** Headless checks for IED/RCB mapping and event ingestion; used by --verify. */
bool runReportEventSelfTest(ScdModelLoader& model, EventStore& store);

#endif
