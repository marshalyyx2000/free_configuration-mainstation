#include "event/report_event_logic.h"

#include "event/event_format.h"
#include "event/event_store.h"

#include <QJsonArray>

bool shouldSkipReportEventItem(const QString& oldVal, const QString& newVal, int reason)
{
    Q_UNUSED(newVal);
    // Only suppress unchanged integrity-period uploads; never drop data-change / GI / unknown-reason bursts.
    if (oldVal.isEmpty())
        return false;
    if (reason == 16)
        return false;
    if ((reason & (1 | 2 | 4 | 32)) != 0)
        return false;
    if (reason == 7)
        return false;
    return reason == 8;
}

void ingestMmsReportEvent(ScdModelLoader& model, EventStore& store, QHash<QString, QString>& lastValues,
    const QJsonObject& evt)
{
    const QString rcbRef = evt.value(QStringLiteral("rcb")).toString();
    const QString iedName = model.resolveIedFromMmsRef(rcbRef);
    const IedDeviceEntry* dev = nullptr;
    for (const IedDeviceEntry& d : model.devices()) {
        if (d.iedName == iedName) {
            dev = &d;
            break;
        }
    }
    const IedModel* im = model.modelFor(iedName);
    const ReportEntry* rpt = ScdModelLoader::findReportForHandler(im, rcbRef);
    const DataSetEntry* ds = rpt ? ScdModelLoader::findDataset(im, rpt->datSet, rpt->ldInst) : nullptr;
    const QJsonArray items = evt.value(QStringLiteral("items")).toArray();
    QVector<StationEvent> batch;
    batch.reserve(items.size());
    for (const QJsonValue& v : items) {
        const QJsonObject it = v.toObject();
        const QString name = it.value(QStringLiteral("name")).toString();
        const int idx = it.value(QStringLiteral("idx")).toInt(-1);
        QString addr = name;
        QString desc;
        if (ds && idx >= 0 && idx < ds->members.size()) {
            addr = ds->members[idx].address;
            desc = ds->members[idx].desc;
        } else if (!name.isEmpty()) {
            addr = name;
            desc = name;
        } else if (idx >= 0) {
            desc = QStringLiteral("数据集条目 %1").arg(idx + 1);
            addr = QStringLiteral("#%1").arg(idx);
        }
        const QString val = formatReportValue(it.value(QStringLiteral("value")));
        const int reason = it.value(QStringLiteral("reason")).toInt();
        const QString key = iedName + QLatin1Char('|') + addr;
        const QString oldVal = lastValues.value(key);
        if (shouldSkipReportEventItem(oldVal, val, reason))
            continue;
        lastValues.insert(key, val);
        StationEvent se;
        se.time = QDateTime::currentDateTime();
        if (it.contains(QStringLiteral("ts_ms")))
            se.time = QDateTime::fromMSecsSinceEpoch(it.value(QStringLiteral("ts_ms")).toVariant().toLongLong());
        se.category = reason == 16 ? StationEventCategory::giReport() : StationEventCategory::pointChange();
        se.iedName = iedName;
        se.iedDesc = dev ? dev->desc : iedName;
        se.rcbRef = rcbRef;
        se.rcbName = rcbRef.section(QLatin1Char('$'), -1);
        if (rpt && !rpt->desc.isEmpty())
            se.rcbName = rpt->desc + QStringLiteral(" (") + se.rcbName + QLatin1Char(')');
        se.pointName = desc;
        se.address = addr;
        se.oldValue = oldVal;
        se.newValue = val;
        se.reason = reason;
        se.reasonText = formatReportReason(reason);
        batch.append(se);
    }
    if (!batch.isEmpty())
        store.appendBatch(batch);
}

bool runReportEventSelfTest(ScdModelLoader& model, EventStore& store)
{
    // Pick the device under test from the loaded model instead of hard-coding an
    // IED name: the shipped station.SMCD may come from any SCD (the old 30-device
    // station or a ~100 MB station with hundreds of devices). Prefer a device that
    // has a RelayEna report, fall back to the first device with any report.
    QString ied;
    const ReportEntry* rptPick = nullptr;
    for (const IedDeviceEntry& dev : model.devices()) {
        const IedModel* m = model.modelFor(dev.iedName);
        if (!m || m->reports.isEmpty())
            continue;
        if (ied.isEmpty()) {
            ied = dev.iedName;
            rptPick = &m->reports.first();
        }
        for (const ReportEntry& r : m->reports) {
            if (r.name.contains(QStringLiteral("RelayEna"), Qt::CaseInsensitive)) {
                ied = dev.iedName;
                rptPick = &r;
                break;
            }
        }
        if (rptPick && rptPick->name.contains(QStringLiteral("RelayEna"), Qt::CaseInsensitive))
            break;
    }
    if (ied.isEmpty() || !rptPick)
        return false;

    const IedModel* im = model.modelFor(ied);
    if (!im)
        return false;
    const ReportEntry* rpt = nullptr;
    for (const ReportEntry& r : im->reports) {
        if (r.name == rptPick->name) {
            rpt = &r;
            break;
        }
    }
    if (!rpt)
        return false;
    const QString rcb = ScdModelLoader::rcbGetRef(ied, rpt->name, QStringLiteral("9"), rpt->buffered, rpt->ldInst);
    if (model.resolveIedFromMmsRef(rcb) != ied)
        return false;
    if (!ScdModelLoader::findReportForHandler(im, rcb))
        return false;
    const DataSetEntry* ds = ScdModelLoader::findDataset(im, rpt->datSet, rpt->ldInst);
    const QString pointRef = ds && !ds->members.isEmpty()
        ? ds->members.first().address
        : ied + QStringLiteral("PROT/LLN0.ST.FuncEna3");

    if (shouldSkipReportEventItem(QStringLiteral("true"), QStringLiteral("true"), 1))
        return false;
    if (!shouldSkipReportEventItem(QStringLiteral("true"), QStringLiteral("true"), 8))
        return false;
    if (shouldSkipReportEventItem(QStringLiteral("true"), QStringLiteral("true"), 7))
        return false;

    QHash<QString, QString> cache;
    QJsonObject evt{
        { QStringLiteral("rcb"), rcb },
        { QStringLiteral("items"),
            QJsonArray{
                QJsonObject{
                    { QStringLiteral("idx"), 0 },
                    { QStringLiteral("name"), pointRef },
                    { QStringLiteral("reason"), 1 },
                    { QStringLiteral("value"), true },
                },
            } },
    };
    ingestMmsReportEvent(model, store, cache, evt);
    const QVector<StationEvent> snap = store.snapshot();
    if (snap.isEmpty() || snap.last().category != StationEventCategory::pointChange())
        return false;
    if (snap.last().iedName != ied || snap.last().newValue != QStringLiteral("true"))
        return false;

    ingestMmsReportEvent(model, store, cache, evt);
    if (store.snapshot().size() != snap.size() + 1)
        return false;

    QJsonObject integrityDup = evt;
    QJsonArray arr = integrityDup.value(QStringLiteral("items")).toArray();
    QJsonObject item = arr.at(0).toObject();
    item.insert(QStringLiteral("reason"), 8);
    arr[0] = item;
    integrityDup.insert(QStringLiteral("items"), arr);
    ingestMmsReportEvent(model, store, cache, integrityDup);
    if (store.snapshot().size() != snap.size() + 1)
        return false;

    return true;
}
