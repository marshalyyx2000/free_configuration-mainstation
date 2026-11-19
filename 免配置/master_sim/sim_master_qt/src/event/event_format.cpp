#include "event/event_format.h"
#include "event/event_store.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QVector>

namespace {

struct MemberSpec {
    QString name;
    QVector<MemberSpec> children;
};

QString scalarText(const QJsonValue& v)
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
            return scalarText(arr.at(0));
        QStringList parts;
        parts.reserve(arr.size());
        for (const QJsonValue& el : arr)
            parts.append(scalarText(el));
        return parts.join(QStringLiteral(" | "));
    }
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        QStringList parts;
        parts.reserve(o.size());
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            parts.append(it.key() + QLatin1Char('=') + scalarText(it.value()));
        return parts.join(QLatin1Char(' '));
    }
    if (v.isNull() || v.isUndefined())
        return QString();
    return v.toVariant().toString();
}

MemberSpec parseSpecAt(const QString& text, int& i)
{
    MemberSpec spec;
    while (i < text.size()) {
        const QChar ch = text.at(i);
        if (!(ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('.')))
            break;
        spec.name.append(ch);
        ++i;
    }
    if (i < text.size() && text.at(i) == QLatin1Char('{')) {
        ++i;
        while (i < text.size() && text.at(i) != QLatin1Char('}')) {
            if (text.at(i) == QLatin1Char(',')) {
                ++i;
                continue;
            }
            spec.children.append(parseSpecAt(text, i));
        }
        if (i < text.size() && text.at(i) == QLatin1Char('}'))
            ++i;
    }
    return spec;
}

QString formatWithSpec(const QJsonValue& v, const MemberSpec& spec)
{
    if (spec.children.isEmpty()) {
        const QString text = scalarText(v);
        if (spec.name.isEmpty())
            return text;
        return spec.name + QLatin1Char('=') + text;
    }
    QStringList parts;
    if (v.isArray()) {
        const QJsonArray arr = v.toArray();
        const int n = qMin(arr.size(), spec.children.size());
        for (int i = 0; i < n; ++i)
            parts.append(formatWithSpec(arr.at(i), spec.children.at(i)));
        for (int i = n; i < arr.size(); ++i)
            parts.append(scalarText(arr.at(i)));
    } else if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (const MemberSpec& child : spec.children) {
            if (o.contains(child.name))
                parts.append(formatWithSpec(o.value(child.name), child));
        }
    } else {
        return spec.name + QLatin1Char('=') + scalarText(v);
    }
    if (parts.isEmpty())
        return spec.name + QLatin1Char('=') + scalarText(v);
    if (spec.name.isEmpty())
        return parts.join(QLatin1Char(' '));
    return spec.name + QLatin1String("={") + parts.join(QLatin1Char(' ')) + QLatin1Char('}');
}

} // namespace

QString formatReportMembers(const QJsonValue& v, const QStringList& members)
{
    if (members.isEmpty())
        return scalarText(v);
    QVector<MemberSpec> specs;
    specs.reserve(members.size());
    for (const QString& member : members) {
        int i = 0;
        const MemberSpec spec = parseSpecAt(member, i);
        if (!spec.name.isEmpty())
            specs.append(spec);
    }
    if (specs.isEmpty())
        return scalarText(v);
    if (v.isArray()) {
        const QJsonArray arr = v.toArray();
        QStringList parts;
        const int n = qMin(arr.size(), specs.size());
        for (int i = 0; i < n; ++i)
            parts.append(formatWithSpec(arr.at(i), specs.at(i)));
        for (int i = n; i < arr.size(); ++i)
            parts.append(scalarText(arr.at(i)));
        return parts.join(QLatin1Char(' '));
    }
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        QStringList parts;
        QSet<QString> used;
        for (const MemberSpec& spec : specs) {
            if (!o.contains(spec.name))
                continue;
            parts.append(formatWithSpec(o.value(spec.name), spec));
            used.insert(spec.name);
        }
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            if (!used.contains(it.key()))
                parts.append(it.key() + QLatin1Char('=') + scalarText(it.value()));
        }
        if (parts.isEmpty())
            return scalarText(v);
        return parts.join(QLatin1Char(' '));
    }
    if (specs.size() == 1)
        return formatWithSpec(v, specs.at(0));
    return scalarText(v);
}

QString formatReportValue(const QJsonValue& v)
{
    return formatReportMembers(v, {});
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
