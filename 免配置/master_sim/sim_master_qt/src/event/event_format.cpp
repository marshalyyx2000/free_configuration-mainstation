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
        QStringList parts;
        parts.reserve(arr.size());
        for (const QJsonValue& el : arr)
            parts.append(formatReportValue(el));
        return parts.join(QStringLiteral(" | "));
    }
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("stVal")))
            return formatReportValue(o.value(QStringLiteral("stVal")));
    }
    return v.toVariant().toString();
}

QString formatReportReason(int reasonCode)
{
    switch (reasonCode) {
    case 1:
        return QStringLiteral("数据变化");
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
        || ev.category == StationEventCategory::linkDown()) {
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
