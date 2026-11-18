#include "event/event_format.h"
#include "event/event_store.h"

#include <QJsonArray>
#include <QJsonObject>

QString formatReportValue(const QJsonValue& v)
{
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isDouble())
        return QString::number(v.toDouble(), 'g', 12);
    if (v.isString())
        return v.toString();
    if (v.isArray()) {
        const QJsonArray arr = v.toArray();
        if (arr.isEmpty())
            return QString();
        if (arr.size() == 1)
            return formatReportValue(arr.at(0));
        /* 数据集读值和总召唤上送的 CDC 通常是 [数据值, 品质, 时标]。表格里只填数据值。 */
        if (arr.size() <= 4) {
            const QString head = formatReportValue(arr.at(0));
            if (!head.isEmpty())
                return head;
        }
        QStringList parts;
        parts.reserve(arr.size());
        for (const QJsonValue& el : arr)
            parts.append(formatReportValue(el));
        return parts.join(QStringLiteral(" | "));
    }
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        const QString keys[] = {
            QStringLiteral("stVal"), QStringLiteral("mag"), QStringLiteral("instMag"),
            QStringLiteral("setVal"), QStringLiteral("setMag"), QStringLiteral("general"),
            QStringLiteral("f"), QStringLiteral("i"),
        };
        for (const QString& key : keys) {
            if (o.contains(key))
                return formatReportValue(o.value(key));
        }
    }
    return v.toVariant().toString();
}

QString formatReportReason(int reasonCode)
{
    switch (reasonCode) {
    case 1:
        return QStringLiteral("突发上送");
    case 2:
        return QStringLiteral("品质变化");
    case 4:
        return QStringLiteral("数据更新");
    case 8:
        return QStringLiteral("完整性周期");
    case 16:
        return QStringLiteral("总召唤");
    case 32:
        return QStringLiteral("应用触发");
    case 64:
        return QStringLiteral("连接丢失");
    case 7:
        return QStringLiteral("数据变化");
    default:
        if (reasonCode == 0)
            return QStringLiteral("-");
        return QStringLiteral("0x%1").arg(reasonCode, 0, 16);
    }
}

namespace StationEventCategory {
QString pointChange()
{
    return QStringLiteral("变位");
}
QString rcbSubscribe()
{
    return QStringLiteral("订阅");
}
QString rcbGi()
{
    return QStringLiteral("总召唤");
}
QString rcbUnsubscribe()
{
    return QStringLiteral("取消订阅");
}
QString giReport()
{
    return QStringLiteral("总召唤上送");
}
QString datasetRead()
{
    return QStringLiteral("召唤");
}
QString datasetReadDone()
{
    return QStringLiteral("召唤完成");
}
QString linkUp()
{
    return QStringLiteral("连接");
}
QString linkDown()
{
    return QStringLiteral("断开");
}
} // namespace StationEventCategory

QString stationEventDetailText(const StationEvent& ev)
{
    const QString desc = ev.pointName.isEmpty() ? ev.address : ev.pointName;
    if (ev.category == StationEventCategory::rcbSubscribe() || ev.category == StationEventCategory::rcbGi()
        || ev.category == StationEventCategory::rcbUnsubscribe() || ev.category == StationEventCategory::linkUp()
        || ev.category == StationEventCategory::linkDown()
        || ev.category == StationEventCategory::datasetRead()
        || ev.category == StationEventCategory::datasetReadDone()) {
        return QStringLiteral(
                   "时间：%1\n"
                   "事件类别：%2\n"
                   "装置：%3（%4）\n"
                   "报告控制块：%5\n"
                   "事件描述：%6")
            .arg(ev.time.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")),
                ev.category,
                ev.iedDesc.isEmpty() ? ev.iedName : ev.iedDesc,
                ev.iedName,
                ev.rcbName.isEmpty() ? QStringLiteral("-") : ev.rcbName,
                desc);
    }
    return QStringLiteral(
               "时间：%1\n"
               "事件类别：%2\n"
               "装置：%3（%4）\n"
               "报告控制块：%5\n"
               "事件描述：%6\n"
               "信息点地址：%7\n"
               "前值：%8\n"
               "后值：%9\n"
               "触发原因：%10")
        .arg(ev.time.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")),
            ev.category.isEmpty() ? StationEventCategory::pointChange() : ev.category,
            ev.iedDesc.isEmpty() ? ev.iedName : ev.iedDesc,
            ev.iedName,
            ev.rcbName,
            desc,
            ev.address,
            ev.oldValue.isEmpty() ? QStringLiteral("-") : ev.oldValue,
            ev.newValue,
            ev.reasonText.isEmpty() ? formatReportReason(ev.reason) : ev.reasonText);
}
