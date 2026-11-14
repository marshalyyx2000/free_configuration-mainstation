#include "scd/scd_model.h"

#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
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

void ScdModelLoader::clearLoadedModels()
{
    m_devices.clear();
    m_models.clear();
    m_modelByIed.clear();
}

void ScdModelLoader::rebuildModelIndex()
{
    m_modelByIed.clear();
    for (int i = 0; i < m_models.size(); ++i)
        m_modelByIed.insert(m_models.at(i).iedName, i);
}

QVector<IedModel> ScdModelLoader::parseModelsFromSmcd(const QString& path, bool uiFast)
{
    QVector<IedModel> out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return out;
    QDomDocument doc;
    if (!doc.setContent(&file))
        return out;
    const QDomElement root = doc.documentElement();
    const QDomNodeList ieds = root.elementsByTagName(QStringLiteral("IED"));
    out.reserve(ieds.count());
    for (int i = 0; i < ieds.count(); ++i) {
        const QDomElement iedEl = ieds.at(i).toElement();
        const QString name = iedEl.attribute(QStringLiteral("name"));
        if (name.isEmpty() || name == QStringLiteral("SubStation"))
            continue;
        IedModel model;
        model.iedName = name;
        model.desc = iedEl.attribute(QStringLiteral("desc"), name);
        QHash<QString, QString> doiDesc;
        if (!uiFast) {
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
            out.append(model);
    }
    return out;
}

bool ScdModelLoader::loadSmcd(const QString& path, bool uiFast)
{
    QVector<IedModel> parsed = parseModelsFromSmcd(path, uiFast);
    if (parsed.isEmpty())
        return false;
    adoptParsedModels(std::move(parsed));
    return true;
}

void ScdModelLoader::adoptParsedModels(QVector<IedModel> models)
{
    m_models = std::move(models);
    rebuildModelIndex();
    for (IedDeviceEntry& d : m_devices) {
        const IedModel* im = modelFor(d.iedName);
        if (im && !im->desc.isEmpty())
            d.desc = im->desc;
    }
}

const IedModel* ScdModelLoader::modelFor(const QString& iedName) const
{
    const auto it = m_modelByIed.constFind(iedName);
    if (it == m_modelByIed.constEnd())
        return nullptr;
    return &m_models.at(it.value());
}

QString ScdModelLoader::resolveIedFromMmsRef(const QString& mmsRef) const
{
    const QString ldPart = mmsRef.section(QLatin1Char('/'), 0, 0);
    if (ldPart.isEmpty())
        return {};
    QString best;
    int bestLen = 0;
    for (const IedDeviceEntry& d : m_devices) {
        if (d.iedName.isEmpty() || !ldPart.startsWith(d.iedName))
            continue;
        if (d.iedName.length() > bestLen) {
            bestLen = d.iedName.length();
            best = d.iedName;
        }
    }
    return best;
}

static bool rcbHandlerTokenMatches(const QString& handlerToken, const QString& reportName)
{
    if (handlerToken == reportName)
        return true;
    if (handlerToken.startsWith(reportName) && handlerToken.length() > reportName.length()) {
        const QChar next = handlerToken.at(reportName.length());
        return next.isDigit();
    }
    return false;
}

const ReportEntry* ScdModelLoader::findReportForHandler(const IedModel* model, const QString& handlerRef)
{
    if (!model || handlerRef.isEmpty())
        return nullptr;
    const QString rcbToken = handlerRef.section(QLatin1Char('$'), -1);
    const ReportEntry* fallback = nullptr;
    for (const ReportEntry& r : model->reports) {
        if (!rcbHandlerTokenMatches(rcbToken, r.name))
            continue;
        if (!fallback)
            fallback = &r;
        const QString ldPrefix = ldName(model->iedName, r.ldInst) + QStringLiteral("/LLN0$");
        if (handlerRef.startsWith(ldPrefix))
            return &r;
    }
    return fallback;
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

bool ScdModelLoader::rcbReportRefMatches(const QString& reportRcb, const QString& handlerRef, const QString& getRef)
{
    if (reportRcb.isEmpty())
        return false;
    if (reportRcb == handlerRef || (!getRef.isEmpty() && reportRcb == getRef))
        return true;
    const QString reportTok = reportRcb.section(QLatin1Char('$'), -1);
    const QString handlerTok = handlerRef.section(QLatin1Char('$'), -1);
    if (!handlerTok.isEmpty() && rcbHandlerTokenMatches(reportTok, handlerTok))
        return true;
    const QString getTok = getRef.section(QLatin1Char('$'), -1);
    if (!getTok.isEmpty() && (reportTok == getTok || rcbHandlerTokenMatches(reportTok, getTok)))
        return true;
    return false;
}

bool ScdModelLoader::addressMatchesMmsDataRef(const QString& scdAddress, const QString& mmsDataRef)
{
    if (scdAddress.isEmpty() || mmsDataRef.isEmpty())
        return false;
    if (scdAddress == mmsDataRef)
        return true;
    const QString itemId = mmsItemIdFromAddress(scdAddress);
    QString mmsRest = mmsDataRef;
    const int slash = mmsRest.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        mmsRest = mmsRest.mid(slash + 1);
    if (itemId == mmsRest)
        return true;
    if (mmsRest.startsWith(itemId + QLatin1Char('$')) || itemId.startsWith(mmsRest + QLatin1Char('$')))
        return true;
    const QString ldPart = scdAddress.section(QLatin1Char('/'), 0, 0);
    if (!ldPart.isEmpty() && mmsDataRef == ldPart + QLatin1Char('/') + itemId)
        return true;
    const QString fullItem = ldPart + QLatin1Char('/') + itemId;
    if (mmsDataRef.startsWith(fullItem + QLatin1Char('$')) || fullItem.startsWith(mmsDataRef + QLatin1Char('$')))
        return true;
    return false;
}

bool ScdModelLoader::datasetRefMatches(const QString& a, const QString& b)
{
    auto norm = [](QString s) {
        s = s.trimmed();
        if (s.startsWith(QLatin1Char('@')))
            s = s.mid(1);
        return s;
    };
    const QString na = norm(a);
    const QString nb = norm(b);
    return !na.isEmpty() && na == nb;
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
        return !datasetMatchesPanel(n, ReportPanelType::Setting)
            && !datasetMatchesPanel(n, ReportPanelType::RelayEna)
            && !datasetMatchesPanel(n, ReportPanelType::Din)
            && !datasetMatchesPanel(n, ReportPanelType::Ain);
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
    for (const IedDeviceEntry& dev : m_devices) {
        const IedModel* model = modelFor(dev.iedName);
        if (!model)
            continue;
        for (const ReportEntry& rpt : model->reports) {
            if (rpt.datSet.isEmpty() || rpt.name.isEmpty())
                continue;
            RcbDescriptor d;
            d.iedName = dev.iedName;
            d.rcbName = rpt.name;
            d.dsName = rpt.datSet;
            d.buffered = rpt.buffered;
            // 对上订阅走子站代理模型，RCB 实例为 01 起；IEDList.rptno 仅用于下联装置
            d.inst = 1;
            d.handlerRef = rcbHandlerRef(dev.iedName, rpt.name, rpt.buffered, rpt.ldInst);
            d.getRef = rcbGetRef(dev.iedName, rpt.name, upstreamRptInst(), rpt.buffered, rpt.ldInst);
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
