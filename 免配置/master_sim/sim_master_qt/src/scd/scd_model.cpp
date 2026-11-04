#include "scd/scd_model.h"

#include <QDomDocument>
#include <QFile>
#include <algorithm>

static QString localTag(const QString& tag)
{
    const int idx = tag.indexOf(QLatin1Char('}'));
    return idx >= 0 ? tag.mid(idx + 1) : tag;
}

bool ScdModelLoader::loadIedList(const QString& path)
{
    m_devices.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QDomDocument doc;
    if (!doc.setContent(&file))
        return false;
    const QDomElement root = doc.documentElement();
    const QDomNodeList nodes = root.elementsByTagName(QStringLiteral("Device"));
    for (int i = 0; i < nodes.count(); ++i) {
        const QDomElement el = nodes.at(i).toElement();
        if (el.attribute(QStringLiteral("p_type")) != QStringLiteral("1"))
            continue;
        IedDeviceEntry d;
        d.no = el.attribute(QStringLiteral("no")).toInt();
        d.iedName = el.attribute(QStringLiteral("ied_name"));
        d.desc = el.attribute(QStringLiteral("name"));
        d.chPara = el.attribute(QStringLiteral("ch_para"));
        d.rptno = el.attribute(QStringLiteral("rptno"), QStringLiteral("9"));
        d.wavePath = el.attribute(QStringLiteral("wave_path"), QStringLiteral("/COMTRADE/"));
        if (!d.iedName.isEmpty())
            m_devices.append(d);
    }
    std::sort(m_devices.begin(), m_devices.end(), [](const IedDeviceEntry& a, const IedDeviceEntry& b) {
        return a.no < b.no;
    });
    return !m_devices.isEmpty();
}

bool ScdModelLoader::loadSmcd(const QString& path)
{
    m_models.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QDomDocument doc;
    if (!doc.setContent(&file))
        return false;
    const QDomElement root = doc.documentElement();
    QDomNodeList ieds = root.elementsByTagName(QStringLiteral("IED"));
    for (int i = 0; i < ieds.count(); ++i) {
        const QDomElement iedEl = ieds.at(i).toElement();
        const QString name = iedEl.attribute(QStringLiteral("name"));
        if (name.isEmpty() || name == QStringLiteral("SubStation"))
            continue;
        IedModel model;
        model.iedName = name;
        model.desc = iedEl.attribute(QStringLiteral("desc"), name);
        const QDomNodeList ln0s = iedEl.elementsByTagName(QStringLiteral("LN0"));
        for (int j = 0; j < ln0s.count(); ++j) {
            const QDomElement ln0 = ln0s.at(j).toElement();
            for (QDomNode child = ln0.firstChild(); !child.isNull(); child = child.nextSibling()) {
                if (!child.isElement())
                    continue;
                const QDomElement el = child.toElement();
                const QString tag = localTag(el.tagName());
                if (tag == QStringLiteral("DataSet")) {
                    DataSetEntry ds;
                    ds.name = el.attribute(QStringLiteral("name"));
                    ds.desc = el.attribute(QStringLiteral("desc"), ds.name);
                    if (!ds.name.isEmpty())
                        model.datasets.append(ds);
                } else if (tag == QStringLiteral("ReportControl")) {
                    ReportEntry rpt;
                    rpt.name = el.attribute(QStringLiteral("name"));
                    rpt.desc = el.attribute(QStringLiteral("desc"), rpt.name);
                    rpt.datSet = el.attribute(QStringLiteral("datSet"));
                    rpt.buffered = el.attribute(QStringLiteral("buffered")).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
                    if (!rpt.name.isEmpty())
                        model.reports.append(rpt);
                }
            }
        }
        if (!model.datasets.isEmpty() || !model.reports.isEmpty())
            m_models.append(model);
    }
    return true;
}

const IedModel* ScdModelLoader::modelFor(const QString& iedName) const
{
    for (const IedModel& m : m_models) {
        if (m.iedName == iedName)
            return &m;
    }
    return nullptr;
}

QString ScdModelLoader::datasetRef(const QString& iedName, const QString& dsName)
{
    return QStringLiteral("%1LD0/LLN0$%2").arg(iedName, dsName);
}

QString ScdModelLoader::rcbHandlerRef(const QString& iedName, const QString& rcbName, bool buffered)
{
    const QString br = buffered ? QStringLiteral("$BR$") : QStringLiteral("$RP$");
    return QStringLiteral("%1LD0/LLN0%2%3").arg(iedName, br, rcbName);
}

QString ScdModelLoader::rcbGetRef(const QString& iedName, const QString& rcbName, const QString& rptno, bool buffered)
{
    bool ok = false;
    int inst = rptno.toInt(&ok);
    if (!ok)
        inst = 9;
    const QString br = buffered ? QStringLiteral("$BR$") : QStringLiteral("$RP$");
    return QStringLiteral("%1LD0/LLN0%2%3%4").arg(iedName, br, rcbName).arg(inst, 2, 10, QChar('0'));
}

bool ScdModelLoader::datasetMatchesPanel(const QString& dsName, ReportPanelType type)
{
    const QString n = dsName;
    switch (type) {
    case ReportPanelType::RelayEna:
        return n.contains(QStringLiteral("RelayEna"), Qt::CaseInsensitive);
    case ReportPanelType::Din:
        return n.contains(QStringLiteral("RelayDin"), Qt::CaseInsensitive)
            || n.contains(QStringLiteral("Alarm"), Qt::CaseInsensitive)
            || n.contains(QStringLiteral("Warning"), Qt::CaseInsensitive)
            || n.startsWith(QStringLiteral("dsDin"), Qt::CaseInsensitive);
    case ReportPanelType::Ain:
        return n.contains(QStringLiteral("Ain"), Qt::CaseInsensitive);
    case ReportPanelType::Setting:
        return n.contains(QStringLiteral("Setting"), Qt::CaseInsensitive)
            || n.contains(QStringLiteral("SetGrp"), Qt::CaseInsensitive);
    case ReportPanelType::Other:
        return true;
    default:
        return false;
    }
}

bool ScdModelLoader::reportMatchesPanel(const ReportEntry& rpt, ReportPanelType type)
{
    if (type == ReportPanelType::Wave || type == ReportPanelType::ReportInst)
        return false;
    if (rpt.datSet.isEmpty())
        return false;
    return datasetMatchesPanel(rpt.datSet, type);
}

QVector<RcbDescriptor> ScdModelLoader::allSubscribeRcbs() const
{
    QVector<RcbDescriptor> out;
    const ReportPanelType types[] = { ReportPanelType::RelayEna, ReportPanelType::Din, ReportPanelType::Ain };
    for (const IedDeviceEntry& dev : m_devices) {
        const IedModel* model = modelFor(dev.iedName);
        if (!model)
            continue;
        for (const ReportEntry& rpt : model->reports) {
            bool match = false;
            for (ReportPanelType t : types) {
                if (reportMatchesPanel(rpt, t)) {
                    match = true;
                    break;
                }
            }
            if (!match)
                continue;
            RcbDescriptor d;
            d.iedName = dev.iedName;
            d.rcbName = rpt.name;
            d.dsName = rpt.datSet;
            d.buffered = rpt.buffered;
            d.inst = dev.rptno.toInt();
            d.handlerRef = rcbHandlerRef(dev.iedName, rpt.name, rpt.buffered);
            d.getRef = rcbGetRef(dev.iedName, rpt.name, dev.rptno, rpt.buffered);
            d.dsRef = datasetRef(dev.iedName, rpt.datSet);
            d.trgOps = rpt.buffered ? 0x44 : 0x4C;
            d.optFlds = rpt.buffered ? 0x3900 : 0x3800;
            out.append(d);
        }
    }
    return out;
}
