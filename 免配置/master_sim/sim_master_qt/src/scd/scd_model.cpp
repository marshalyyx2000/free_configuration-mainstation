#include "scd/scd_model.h"

#include <QDomDocument>
#include <QFile>
#include <QHash>
#include <QSet>
#include <algorithm>

static QString localTag(const QString& tag)
{
    const int idx = tag.indexOf(QLatin1Char('}'));
    return idx >= 0 ? tag.mid(idx + 1) : tag;
}

static QString ldeviceInstOf(const QDomElement& el)
{
    QDomNode n = el.parentNode();
    while (!n.isNull()) {
        if (n.isElement() && localTag(n.toElement().tagName()) == QStringLiteral("LDevice"))
            return n.toElement().attribute(QStringLiteral("inst"), QStringLiteral("LD0"));
        n = n.parentNode();
    }
    return QStringLiteral("LD0");
}

static QString doiLookupKey(const QString& ldInst, const QString& prefix, const QString& lnClass,
    const QString& lnInst, const QString& doName)
{
    return ldInst + QLatin1Char('|') + prefix + QLatin1Char('|') + lnClass + QLatin1Char('|') + lnInst
        + QLatin1Char('|') + doName;
}

static QString daiDuText(const QDomElement& doi)
{
    const QDomNodeList dais = doi.elementsByTagName(QStringLiteral("DAI"));
    for (int i = 0; i < dais.count(); ++i) {
        const QDomElement dai = dais.at(i).toElement();
        if (dai.attribute(QStringLiteral("name")) != QStringLiteral("dU"))
            continue;
        const QDomNodeList vals = dai.elementsByTagName(QStringLiteral("Val"));
        if (vals.isEmpty())
            continue;
        const QString t = vals.at(0).toElement().text().trimmed();
        if (!t.isEmpty())
            return t;
    }
    return QString();
}

static void collectDoiDesc(const QDomElement& lnEl, const QString& ldInst, QHash<QString, QString>& out)
{
    const QString tag = localTag(lnEl.tagName());
    QString lnClass;
    QString prefix;
    QString lnInst;
    if (tag == QStringLiteral("LN0")) {
        lnClass = lnEl.attribute(QStringLiteral("lnClass"), QStringLiteral("LLN0"));
        prefix = lnEl.attribute(QStringLiteral("prefix"));
        lnInst = lnEl.attribute(QStringLiteral("inst"));
    } else if (tag == QStringLiteral("LN")) {
        lnClass = lnEl.attribute(QStringLiteral("lnClass"));
        prefix = lnEl.attribute(QStringLiteral("prefix"));
        lnInst = lnEl.attribute(QStringLiteral("inst"));
    } else {
        return;
    }
    const QDomNodeList dois = lnEl.elementsByTagName(QStringLiteral("DOI"));
    for (int i = 0; i < dois.count(); ++i) {
        const QDomElement doi = dois.at(i).toElement();
        const QString doName = doi.attribute(QStringLiteral("name"));
        if (doName.isEmpty())
            continue;
        QString desc = doi.attribute(QStringLiteral("desc"));
        if (desc.isEmpty())
            desc = daiDuText(doi);
        if (desc.isEmpty())
            continue;
        out.insert(doiLookupKey(ldInst, prefix, lnClass, lnInst, doName), desc);
    }
}

static QString fcdaDescFromMap(const QHash<QString, QString>& doiDesc, const QDomElement& fcda)
{
    const QString ldInst = fcda.attribute(QStringLiteral("ldInst"), QStringLiteral("LD0"));
    const QString prefix = fcda.attribute(QStringLiteral("prefix"));
    const QString lnClass = fcda.attribute(QStringLiteral("lnClass"));
    const QString lnInst = fcda.attribute(QStringLiteral("lnInst"));
    const QString doName = fcda.attribute(QStringLiteral("doName"));
    QString desc = doiDesc.value(doiLookupKey(ldInst, prefix, lnClass, lnInst, doName));
    if (desc.isEmpty() && doName.contains(QLatin1Char('.')))
        desc = doiDesc.value(doiLookupKey(ldInst, prefix, lnClass, lnInst, doName.section(QLatin1Char('.'), 0, 0)));
    if (desc.isEmpty())
        desc = fcda.attribute(QStringLiteral("desc"));
    if (desc.isEmpty())
        desc = doName;
    return desc;
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
        QHash<QString, QString> doiDesc;
        const QDomNodeList ldevices = iedEl.elementsByTagName(QStringLiteral("LDevice"));
        for (int d = 0; d < ldevices.count(); ++d) {
            const QDomElement ldEl = ldevices.at(d).toElement();
            const QString ldInst = ldEl.attribute(QStringLiteral("inst"), QStringLiteral("LD0"));
            for (QDomNode ch = ldEl.firstChild(); !ch.isNull(); ch = ch.nextSibling()) {
                if (!ch.isElement())
                    continue;
                collectDoiDesc(ch.toElement(), ldInst, doiDesc);
            }
        }
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
                    ds.ldInst = ldeviceInstOf(el);
                    const QDomNodeList fcdas = el.elementsByTagName(QStringLiteral("FCDA"));
                    for (int k = 0; k < fcdas.count(); ++k) {
                        const QDomElement fcdaEl = fcdas.at(k).toElement();
                        DataSetMember member;
                        member.address = fcdaMmsAddress(name, fcdaEl);
                        member.desc = fcdaDescFromMap(doiDesc, fcdaEl);
                        if (member.desc.isEmpty())
                            member.desc = member.address;
                        ds.members.append(member);
                    }
                    if (!ds.name.isEmpty())
                        model.datasets.append(ds);
                } else if (tag == QStringLiteral("ReportControl")) {
                    ReportEntry rpt;
                    rpt.name = el.attribute(QStringLiteral("name"));
                    rpt.desc = el.attribute(QStringLiteral("desc"), rpt.name);
                    rpt.datSet = el.attribute(QStringLiteral("datSet"));
                    rpt.ldInst = ldeviceInstOf(el);
                    rpt.rptId = el.attribute(QStringLiteral("rptID"));
                    rpt.confRev = el.attribute(QStringLiteral("confRev")).toInt();
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

QString ScdModelLoader::ldName(const QString& iedName, const QString& ldInst)
{
    const QString inst = ldInst.isEmpty() ? QStringLiteral("LD0") : ldInst;
    if (inst.startsWith(iedName))
        return inst;
    return iedName + inst;
}

QString ScdModelLoader::datasetRef(const QString& iedName, const QString& dsName, const QString& ldInst)
{
    return QStringLiteral("%1/LLN0$%2").arg(ldName(iedName, ldInst), dsName);
}

QString ScdModelLoader::rcbHandlerRef(const QString& iedName, const QString& rcbName, bool buffered,
    const QString& ldInst)
{
    const QString br = buffered ? QStringLiteral("$BR$") : QStringLiteral("$RP$");
    return QStringLiteral("%1/LLN0%2%3").arg(ldName(iedName, ldInst), br, rcbName);
}

QString ScdModelLoader::rcbGetRef(const QString& iedName, const QString& rcbName, const QString& rptno, bool buffered,
    const QString& ldInst)
{
    bool ok = false;
    int inst = rptno.toInt(&ok);
    if (!ok)
        inst = 9;
    const QString br = buffered ? QStringLiteral("$BR$") : QStringLiteral("$RP$");
    return QStringLiteral("%1/LLN0%2%3%4").arg(ldName(iedName, ldInst), br, rcbName).arg(inst, 2, 10, QChar('0'));
}

QString ScdModelLoader::fcdaMmsAddress(const QString& iedName, const QDomElement& fcda)
{
    const QString ld = ldName(iedName, fcda.attribute(QStringLiteral("ldInst"), QStringLiteral("LD0")));
    const QString prefix = fcda.attribute(QStringLiteral("prefix"));
    const QString lnClass = fcda.attribute(QStringLiteral("lnClass"));
    const QString lnInst = fcda.attribute(QStringLiteral("lnInst"));
    const QString doName = fcda.attribute(QStringLiteral("doName"));
    const QString fc = fcda.attribute(QStringLiteral("fc"));
    const QString ln = prefix + lnClass + lnInst;
    return QStringLiteral("%1/%2.%3[%4]").arg(ld, ln, doName, fc);
}

QString ScdModelLoader::mmsItemIdFromAddress(const QString& address)
{
    QString rest = address;
    const int slash = rest.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        rest = rest.mid(slash + 1);
    if (rest.contains(QLatin1Char('$')) && !rest.contains(QLatin1Char('[')))
        return rest;
    QString fc;
    const int lb = rest.lastIndexOf(QLatin1Char('['));
    const int rb = rest.lastIndexOf(QLatin1Char(']'));
    if (lb >= 0 && rb > lb) {
        fc = rest.mid(lb + 1, rb - lb - 1);
        rest = rest.left(lb);
    }
    const QString ln = rest.section(QLatin1Char('.'), 0, 0);
    const QString doPath = rest.section(QLatin1Char('.'), 1).replace(QLatin1Char('.'), QLatin1Char('$'));
    if (fc.isEmpty())
        return rest.replace(QLatin1Char('.'), QLatin1Char('$'));
    if (doPath.isEmpty())
        return ln + QLatin1Char('$') + fc;
    return ln + QLatin1Char('$') + fc + QLatin1Char('$') + doPath;
}

const DataSetEntry* ScdModelLoader::findDataset(const IedModel* model, const QString& dsName, const QString& ldInst)
{
    if (!model)
        return nullptr;
    for (const DataSetEntry& ds : model->datasets) {
        if (ds.name == dsName && ds.ldInst == ldInst)
            return &ds;
    }
    for (const DataSetEntry& ds : model->datasets) {
        if (ds.name == dsName)
            return &ds;
    }
    return nullptr;
}

const ReportEntry* ScdModelLoader::findReportForHandler(const IedModel* model, const QString& handlerRef)
{
    if (!model || handlerRef.isEmpty())
        return nullptr;
    const QString rcbName = handlerRef.section(QLatin1Char('$'), -1);
    const ReportEntry* fallback = nullptr;
    for (const ReportEntry& r : model->reports) {
        if (r.name != rcbName)
            continue;
        if (!fallback)
            fallback = &r;
        const QString ldPrefix = ldName(model->iedName, r.ldInst) + QStringLiteral("/LLN0$");
        if (handlerRef.startsWith(ldPrefix))
            return &r;
    }
    return fallback;
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
    QSet<QString> seenHandler;
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
            d.handlerRef = rcbHandlerRef(dev.iedName, rpt.name, rpt.buffered, rpt.ldInst);
            d.getRef = rcbGetRef(dev.iedName, rpt.name, dev.rptno, rpt.buffered, rpt.ldInst);
            d.dsRef = datasetRef(dev.iedName, rpt.datSet, rpt.ldInst);
            d.trgOps = rpt.buffered ? 0x44 : 0x4C;
            d.optFlds = rpt.buffered ? 0x3900 : 0x3800;
            if (seenHandler.contains(d.handlerRef))
                continue;
            seenHandler.insert(d.handlerRef);
            out.append(d);
        }
    }
    return out;
}
