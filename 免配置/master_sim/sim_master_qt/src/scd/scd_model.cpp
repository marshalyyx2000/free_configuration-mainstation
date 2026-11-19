#include "scd/scd_model.h"

#include <QDomDocument>
#include <QFile>
#include <QPair>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QElapsedTimer>
#include <QXmlStreamReader>
#include <algorithm>
#include <cstdio>

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
        out.insert(doiLookupKey(ldInst, prefix, lnClass, lnInst, doName), desc.trimmed());
    }
}

static QString fcdaDoiKey(const QDomElement& fcda)
{
    const QString ldInst = fcda.attribute(QStringLiteral("ldInst"), QStringLiteral("LD0"));
    const QString prefix = fcda.attribute(QStringLiteral("prefix"));
    const QString lnClass = fcda.attribute(QStringLiteral("lnClass"));
    const QString lnInst = fcda.attribute(QStringLiteral("lnInst"));
    const QString doName = fcda.attribute(QStringLiteral("doName"));
    return doiLookupKey(ldInst, prefix, lnClass, lnInst, doName);
}

static QString memberDescFromDoiMap(const QHash<QString, QString>& doiDesc, const DataSetMember& member)
{
    QString desc = doiDesc.value(member.doiKey);
    if (desc.isEmpty() && member.doiKey.contains(QLatin1Char('.'))) {
        const QString parentKey = member.doiKey.section(QLatin1Char('.'), 0, 0);
        desc = doiDesc.value(parentKey);
    }
    return desc;
}

static bool buildPatchesFromDoiMap(const IedModel& model, const QHash<QString, QString>& doiDesc,
    QVector<IedMemberDescPatch>* out)
{
    if (!out)
        return false;
    out->clear();
    for (const DataSetEntry& ds : model.datasets) {
        IedMemberDescPatch patch;
        patch.dsName = ds.name;
        patch.ldInst = ds.ldInst;
        patch.memberDescs.reserve(ds.members.size());
        for (const DataSetMember& m : ds.members) {
            QString desc = memberDescFromDoiMap(doiDesc, m);
            if (desc.isEmpty())
                desc = m.desc;
            patch.memberDescs.append(desc);
        }
        out->append(patch);
    }
    return true;
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
        d.chPara.replace(QStringLiteral("11.100.100."), QStringLiteral("10.100.100."));
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

struct SclBda {
    QString name;
    QString bType;
    QString typeId;
};

struct SclDa {
    QString name;
    QString fc;
    QString bType;
    QString typeId;
};

struct SclTypes {
    QHash<QString, QHash<QString, QString>> lnDo;
    QHash<QString, QVector<QPair<QString, QString>>> doSdo;
    QHash<QString, QVector<SclDa>> doDa;
    QHash<QString, QVector<SclBda>> daBda;
};

static QString specText(const QString& name, const QString& bType, const QString& typeId, const SclTypes& types, int depth)
{
    const bool isStruct = bType.compare(QLatin1String("Struct"), Qt::CaseInsensitive) == 0;
    if (!isStruct || depth >= 4 || typeId.isEmpty() || !types.daBda.contains(typeId))
        return name;
    QStringList kids;
    const QVector<SclBda> bdas = types.daBda.value(typeId);
    kids.reserve(bdas.size());
    for (const SclBda& bda : bdas)
        kids.append(specText(bda.name, bda.bType, bda.typeId, types, depth + 1));
    if (kids.isEmpty())
        return name;
    return name + QLatin1Char('{') + kids.join(QLatin1Char(',')) + QLatin1Char('}');
}

static SclTypes indexSclTypes(const QDomElement& root)
{
    SclTypes types;
    QDomElement templates;
    const QDomNodeList templateNodes = root.elementsByTagName(QStringLiteral("DataTypeTemplates"));
    if (!templateNodes.isEmpty())
        templates = templateNodes.at(0).toElement();
    if (templates.isNull())
        return types;

    const QDomNodeList lns = templates.elementsByTagName(QStringLiteral("LNodeType"));
    for (int i = 0; i < lns.count(); ++i) {
        const QDomElement ln = lns.at(i).toElement();
        const QString id = ln.attribute(QStringLiteral("id"));
        if (id.isEmpty())
            continue;
        QHash<QString, QString> dos;
        for (QDomNode ch = ln.firstChild(); !ch.isNull(); ch = ch.nextSibling()) {
            if (!ch.isElement())
                continue;
            const QDomElement el = ch.toElement();
            if (localTag(el.tagName()) != QStringLiteral("DO"))
                continue;
            const QString name = el.attribute(QStringLiteral("name"));
            const QString typeId = el.attribute(QStringLiteral("type"));
            if (!name.isEmpty() && !typeId.isEmpty())
                dos.insert(name, typeId);
        }
        if (!dos.isEmpty())
            types.lnDo.insert(id, dos);
    }

    const QDomNodeList doTypes = templates.elementsByTagName(QStringLiteral("DOType"));
    for (int i = 0; i < doTypes.count(); ++i) {
        const QDomElement dt = doTypes.at(i).toElement();
        const QString id = dt.attribute(QStringLiteral("id"));
        if (id.isEmpty())
            continue;
        QVector<SclDa> das;
        QVector<QPair<QString, QString>> sdos;
        for (QDomNode ch = dt.firstChild(); !ch.isNull(); ch = ch.nextSibling()) {
            if (!ch.isElement())
                continue;
            const QDomElement el = ch.toElement();
            const QString tag = localTag(el.tagName());
            if (tag == QStringLiteral("DA")) {
                SclDa da;
                da.name = el.attribute(QStringLiteral("name"));
                da.fc = el.attribute(QStringLiteral("fc"));
                da.bType = el.attribute(QStringLiteral("bType"));
                da.typeId = el.attribute(QStringLiteral("type"));
                if (!da.name.isEmpty())
                    das.append(da);
            } else if (tag == QStringLiteral("SDO")) {
                const QString name = el.attribute(QStringLiteral("name"));
                const QString typeId = el.attribute(QStringLiteral("type"));
                if (!name.isEmpty() && !typeId.isEmpty())
                    sdos.append(qMakePair(name, typeId));
            }
        }
        if (!das.isEmpty())
            types.doDa.insert(id, das);
        if (!sdos.isEmpty())
            types.doSdo.insert(id, sdos);
    }

    const QDomNodeList daTypes = templates.elementsByTagName(QStringLiteral("DAType"));
    for (int i = 0; i < daTypes.count(); ++i) {
        const QDomElement dt = daTypes.at(i).toElement();
        const QString id = dt.attribute(QStringLiteral("id"));
        if (id.isEmpty())
            continue;
        QVector<SclBda> bdas;
        for (QDomNode ch = dt.firstChild(); !ch.isNull(); ch = ch.nextSibling()) {
            if (!ch.isElement())
                continue;
            const QDomElement el = ch.toElement();
            if (localTag(el.tagName()) != QStringLiteral("BDA"))
                continue;
            SclBda bda;
            bda.name = el.attribute(QStringLiteral("name"));
            bda.bType = el.attribute(QStringLiteral("bType"));
            bda.typeId = el.attribute(QStringLiteral("type"));
            if (!bda.name.isEmpty())
                bdas.append(bda);
        }
        if (!bdas.isEmpty())
            types.daBda.insert(id, bdas);
    }
    return types;
}

static bool lnAttrsMatch(const QDomElement& ln, const QString& prefix, const QString& lnClass, const QString& lnInst)
{
    const QString tag = localTag(ln.tagName());
    if (tag != QStringLiteral("LN0") && tag != QStringLiteral("LN"))
        return false;
    QString elClass = ln.attribute(QStringLiteral("lnClass"));
    if (elClass.isEmpty() && tag == QStringLiteral("LN0"))
        elClass = QStringLiteral("LLN0");
    return ln.attribute(QStringLiteral("prefix")) == prefix && ln.attribute(QStringLiteral("inst")) == lnInst
        && elClass == lnClass;
}

static QDomElement ldeviceForFcda(const QDomElement& fcda)
{
    const QString ldInst = fcda.attribute(QStringLiteral("ldInst"), QStringLiteral("LD0"));
    for (QDomNode n = fcda.parentNode(); !n.isNull(); n = n.parentNode()) {
        if (!n.isElement() || localTag(n.toElement().tagName()) != QStringLiteral("LDevice"))
            continue;
        const QDomElement ld = n.toElement();
        if (ld.attribute(QStringLiteral("inst"), QStringLiteral("LD0")) == ldInst)
            return ld;
        break;
    }
    QDomElement ied;
    for (QDomNode n = fcda.parentNode(); !n.isNull(); n = n.parentNode()) {
        if (n.isElement() && localTag(n.toElement().tagName()) == QStringLiteral("IED")) {
            ied = n.toElement();
            break;
        }
    }
    if (ied.isNull())
        return {};
    const QDomNodeList lds = ied.elementsByTagName(QStringLiteral("LDevice"));
    for (int i = 0; i < lds.count(); ++i) {
        const QDomElement ld = lds.at(i).toElement();
        if (ld.attribute(QStringLiteral("inst"), QStringLiteral("LD0")) == ldInst)
            return ld;
    }
    return {};
}

static QString lnTypeForFcda(const QDomElement& fcda)
{
    const QDomElement ld = ldeviceForFcda(fcda);
    if (ld.isNull())
        return {};
    const QString prefix = fcda.attribute(QStringLiteral("prefix"));
    const QString lnClass = fcda.attribute(QStringLiteral("lnClass"));
    const QString lnInst = fcda.attribute(QStringLiteral("lnInst"));
    for (QDomNode ch = ld.firstChild(); !ch.isNull(); ch = ch.nextSibling()) {
        if (!ch.isElement())
            continue;
        const QDomElement ln = ch.toElement();
        if (lnAttrsMatch(ln, prefix, lnClass, lnInst))
            return ln.attribute(QStringLiteral("lnType"));
    }
    return {};
}

static QString doTypeForPath(const SclTypes& types, const QString& lnType, const QString& doName)
{
    const QStringList segs = doName.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (segs.isEmpty() || lnType.isEmpty())
        return {};
    QString doType = types.lnDo.value(lnType).value(segs.at(0));
    for (int i = 1; i < segs.size() && !doType.isEmpty(); ++i) {
        QString next;
        const QVector<QPair<QString, QString>> sdos = types.doSdo.value(doType);
        for (const QPair<QString, QString>& sdo : sdos) {
            if (sdo.first == segs.at(i)) {
                next = sdo.second;
                break;
            }
        }
        doType = next;
    }
    return doType;
}

static QStringList membersForDaName(const SclTypes& types, const QVector<SclDa>& das, const QString& daName)
{
    const QStringList path = daName.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (path.isEmpty())
        return {};
    const SclDa* found = nullptr;
    for (const SclDa& da : das) {
        if (da.name == path.at(0)) {
            found = &da;
            break;
        }
    }
    if (!found)
        return { daName };
    if (path.size() == 1)
        return { specText(found->name, found->bType, found->typeId, types, 0) };

    QString bType = found->bType;
    QString typeId = found->typeId;
    QString prefix = found->name;
    for (int i = 1; i < path.size(); ++i) {
        if (bType.compare(QLatin1String("Struct"), Qt::CaseInsensitive) != 0)
            return { daName };
        const QVector<SclBda> bdas = types.daBda.value(typeId);
        bool hit = false;
        for (const SclBda& bda : bdas) {
            if (bda.name != path.at(i))
                continue;
            prefix += QLatin1Char('.') + bda.name;
            bType = bda.bType;
            typeId = bda.typeId;
            hit = true;
            if (i + 1 == path.size())
                return { specText(prefix, bType, typeId, types, 0) };
            break;
        }
        if (!hit)
            return { daName };
    }
    return { daName };
}

static QStringList fcdaValueMembers(const SclTypes& types, const QDomElement& fcda)
{
    if (types.lnDo.isEmpty())
        return {};
    const QString lnType = lnTypeForFcda(fcda);
    const QString doType = doTypeForPath(types, lnType, fcda.attribute(QStringLiteral("doName")));
    if (doType.isEmpty())
        return {};
    const QVector<SclDa> das = types.doDa.value(doType);
    const QString daName = fcda.attribute(QStringLiteral("daName"));
    if (!daName.isEmpty())
        return membersForDaName(types, das, daName);
    const QString fc = fcda.attribute(QStringLiteral("fc"));
    QStringList out;
    for (const SclDa& da : das) {
        if (fc.isEmpty() || da.fc.compare(fc, Qt::CaseInsensitive) == 0)
            out.append(specText(da.name, da.bType, da.typeId, types, 0));
    }
    return out;
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
    const SclTypes sclTypes = indexSclTypes(root);
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
                        member.doiKey = fcdaDoiKey(fcdaEl);
                        member.address = fcdaMmsAddress(name, fcdaEl);
                        member.valueMembers = fcdaValueMembers(sclTypes, fcdaEl);
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

static QDomElement findIedElement(const QDomElement& root, const QString& iedName)
{
    const QDomNodeList ieds = root.elementsByTagName(QStringLiteral("IED"));
    for (int i = 0; i < ieds.count(); ++i) {
        const QDomElement iedEl = ieds.at(i).toElement();
        if (iedEl.attribute(QStringLiteral("name")) == iedName)
            return iedEl;
    }
    return {};
}

static QDomElement findDataSetInIed(const QDomElement& iedEl, const QString& dsName, const QString& ldInst)
{
    const QDomNodeList ldevices = iedEl.elementsByTagName(QStringLiteral("LDevice"));
    for (int d = 0; d < ldevices.count(); ++d) {
        const QDomElement ldEl = ldevices.at(d).toElement();
        const QString inst = ldEl.attribute(QStringLiteral("inst"), QStringLiteral("LD0"));
        QString ldKey = inst;
        if (!iedEl.attribute(QStringLiteral("name")).isEmpty() && !inst.startsWith(iedEl.attribute(QStringLiteral("name"))))
            ldKey = inst;
        if (ldKey != ldInst && inst != ldInst)
            continue;
        const QDomNodeList ln0s = ldEl.elementsByTagName(QStringLiteral("LN0"));
        for (int j = 0; j < ln0s.count(); ++j) {
            const QDomElement ln0 = ln0s.at(j).toElement();
            for (QDomNode child = ln0.firstChild(); !child.isNull(); child = child.nextSibling()) {
                if (!child.isElement())
                    continue;
                const QDomElement el = child.toElement();
                if (localTag(el.tagName()) == QStringLiteral("DataSet") && el.attribute(QStringLiteral("name")) == dsName)
                    return el;
            }
        }
    }
    return {};
}

static QString iedNameFromTagBytes(const QByteArray& window, int iedTagIdx)
{
    const int namePos = window.indexOf("name=\"", iedTagIdx);
    if (namePos < 0 || namePos > iedTagIdx + 600)
        return {};
    const int valStart = namePos + 6;
    const int valEnd = window.indexOf('"', valStart);
    if (valEnd <= valStart)
        return {};
    return QString::fromUtf8(window.mid(valStart, valEnd - valStart));
}

QHash<QString, qint64> ScdModelLoader::smcdIedOffsets(const QString& path)
{
    QHash<QString, qint64> out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return out;
    const QByteArray needle = "<IED ";
    QByteArray carry;
    qint64 basePos = 0;
    while (true) {
        const QByteArray chunk = file.read(4 * 1024 * 1024);
        if (chunk.isEmpty() && carry.isEmpty())
            break;
        const QByteArray window = carry + chunk;
        int idx = 0;
        while ((idx = window.indexOf(needle, idx)) >= 0) {
            const QString name = iedNameFromTagBytes(window, idx);
            if (!name.isEmpty() && name != QStringLiteral("SubStation")) {
                const qint64 byteOff = basePos - carry.size() + idx;
                out.insert(name, byteOff);
            }
            idx += needle.size();
        }
        carry = window.right(qMin(window.size(), needle.size() + 600));
        basePos += chunk.size();
        if (chunk.size() < 4 * 1024 * 1024)
            break;
    }
    return out;
}

bool ScdModelLoader::streamIedDoiDescMap(const QString& path, qint64 iedOffset, const QString& iedName,
    QHash<QString, QString>* out)
{
    if (!out)
        return false;
    out->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    if (iedOffset >= 0) {
        if (!file.seek(iedOffset))
            return false;
        const qint64 at = file.pos();
        const QByteArray head = file.read(8);
        file.seek(at);
        if (!head.startsWith('<')) {
            qint64 back = qMin(iedOffset, qint64(32));
            file.seek(iedOffset - back);
            const QByteArray scan = file.read(back + 64);
            const int rel = scan.lastIndexOf("<IED ");
            if (rel < 0)
                return false;
            if (!file.seek(iedOffset - back + rel))
                return false;
        }
    }

    QXmlStreamReader xml(&file);
    bool inIed = false;
    int iedDepth = 0;
    QString ldInst = QStringLiteral("LD0");
    QString lnClass;
    QString prefix;
    QString lnInst;
    int lnDepth = 0;

    QString curDoName;
    QString curDoDesc;
    bool inDoi = false;
    bool inDu = false;
    QString duText;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString tag = xml.name().toString();
            if (tag == QLatin1String("IED")) {
                if (!inIed) {
                    if (xml.attributes().value(QLatin1String("name")).toString() != iedName)
                        return false;
                    inIed = true;
                    iedDepth = 1;
                } else {
                    ++iedDepth;
                }
            } else if (inIed) {
                if (tag == QLatin1String("LDevice")) {
                    ldInst = xml.attributes().value(QLatin1String("inst")).toString();
                    if (ldInst.isEmpty())
                        ldInst = QStringLiteral("LD0");
                } else if (tag == QLatin1String("LN0") || tag == QLatin1String("LN")) {
                    ++lnDepth;
                    if (tag == QLatin1String("LN0")) {
                        lnClass = xml.attributes().value(QLatin1String("lnClass")).toString();
                        if (lnClass.isEmpty())
                            lnClass = QStringLiteral("LLN0");
                    } else {
                        lnClass = xml.attributes().value(QLatin1String("lnClass")).toString();
                    }
                    prefix = xml.attributes().value(QLatin1String("prefix")).toString();
                    lnInst = xml.attributes().value(QLatin1String("inst")).toString();
                } else if (tag == QLatin1String("DOI") && lnDepth > 0) {
                    inDoi = true;
                    curDoName = xml.attributes().value(QLatin1String("name")).toString();
                    curDoDesc = xml.attributes().value(QLatin1String("desc")).toString();
                    inDu = false;
                    duText.clear();
                } else if (inDoi && tag == QLatin1String("DAI")
                    && xml.attributes().value(QLatin1String("name")) == QLatin1String("dU")) {
                    inDu = true;
                    duText.clear();
                }
            }
        } else if (xml.isEndElement()) {
            const QString tag = xml.name().toString();
            if (tag == QLatin1String("IED") && inIed) {
                if (--iedDepth == 0)
                    return true;
            }
            if (!inIed)
                continue;
            if (tag == QLatin1String("LN0") || tag == QLatin1String("LN"))
                --lnDepth;
            if (tag == QLatin1String("DAI") && inDu)
                inDu = false;
            if (tag == QLatin1String("DOI") && inDoi) {
                QString desc = curDoDesc;
                if (desc.isEmpty())
                    desc = duText;
                if (!desc.isEmpty() && !curDoName.isEmpty())
                    out->insert(doiLookupKey(ldInst, prefix, lnClass, lnInst, curDoName), desc.trimmed());
                inDoi = false;
                curDoName.clear();
            }
        } else if (xml.isCharacters() && inDu && !xml.isWhitespace()) {
            const QString t = xml.text().toString().trimmed();
            if (!t.isEmpty())
                duText = t;
        }
    }
    return inIed;
}

bool ScdModelLoader::buildIedDoiDescPatchesAtOffset(const QString& path, qint64 iedOffset, const QString& iedName,
    const IedModel& model, QVector<IedMemberDescPatch>* out, QHash<QString, QString>* doiOut)
{
    QHash<QString, QString> doiDesc;
    if (!streamIedDoiDescMap(path, iedOffset, iedName, &doiDesc))
        return false;
    if (doiOut)
        *doiOut = doiDesc;
    return buildPatchesFromDoiMap(model, doiDesc, out);
}

bool ScdModelLoader::buildIedDoiDescPatches(const QString& path, const QString& iedName, const IedModel& model,
    QVector<IedMemberDescPatch>* out, QHash<QString, QString>* doiOut)
{
    const QHash<QString, qint64> offs = smcdIedOffsets(path);
    const qint64 iedOff = offs.value(iedName, -1);
    if (iedOff >= 0)
        return buildIedDoiDescPatchesAtOffset(path, iedOff, iedName, model, out, doiOut);

    if (!out)
        return false;
    out->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QDomDocument doc;
    if (!doc.setContent(&file))
        return false;
    const QDomElement iedEl = findIedElement(doc.documentElement(), iedName);
    if (iedEl.isNull())
        return false;

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
    if (doiOut)
        *doiOut = doiDesc;
    return buildPatchesFromDoiMap(model, doiDesc, out);
}

bool ScdModelLoader::isHumanPointDesc(const QString& desc)
{
    if (desc.isEmpty() || desc.contains(QLatin1Char('$')) || desc.contains(QLatin1Char('/')))
        return false;
    for (const QChar c : desc) {
        if (c.unicode() >= 0x80)
            return true;
    }
    return false;
}

static bool descDoNameMatchesRef(const QString& ref, const QString& doName)
{
    if (ref.isEmpty() || doName.isEmpty())
        return false;
    if (ref.endsWith(QLatin1Char('$') + doName) || ref.endsWith(QLatin1Char('.') + doName))
        return true;
    if (ref.contains(QLatin1Char('$') + doName + QLatin1Char('$'))
        || ref.contains(QLatin1Char('.') + doName + QLatin1Char('[')))
        return true;
    return false;
}

QString ScdModelLoader::descForDataRef(const IedModel* model, const QString& ref)
{
    if (!model || ref.isEmpty())
        return {};
    for (const DataSetEntry& ds : model->datasets) {
        for (const DataSetMember& m : ds.members) {
            if (!isHumanPointDesc(m.desc))
                continue;
            if (m.address == ref || addressMatchesMmsDataRef(m.address, ref))
                return m.desc;
        }
    }
    for (auto it = model->doiDesc.constBegin(); it != model->doiDesc.constEnd(); ++it) {
        if (!isHumanPointDesc(it.value()))
            continue;
        const QStringList p = it.key().split(QLatin1Char('|'));
        if (p.size() < 5)
            continue;
        const QString ln = p.at(1) + p.at(2) + p.at(3);
        if (ln.isEmpty()
            || (!ref.contains(QLatin1Char('/') + ln + QLatin1Char('$'))
                && !ref.contains(QLatin1Char('/') + ln + QLatin1Char('.'))))
            continue;
        if (!p.at(0).isEmpty() && !ref.contains(p.at(0)))
            continue;
        if (descDoNameMatchesRef(ref, p.at(4)))
            return it.value();
    }
    return {};
}

QString ScdModelLoader::displayPointDesc(const IedModel* model, const QString& ref, const QString& modelDesc)
{
    if (isHumanPointDesc(modelDesc))
        return modelDesc;
    const QString looked = descForDataRef(model, ref);
    if (isHumanPointDesc(looked))
        return looked;
    if (isHumanPointDesc(ref))
        return ref;
    QString tok;
    if (ref.contains(QLatin1Char('$')))
        tok = ref.section(QLatin1Char('$'), -1);
    else
        tok = ref.section(QLatin1Char('.'), -1).section(QLatin1Char('['), 0, 0);
    if (!tok.isEmpty() && tok != ref && isHumanPointDesc(tok))
        return tok;
    if (!tok.isEmpty() && !tok.contains(QLatin1Char('/')) && !tok.contains(QLatin1Char('$')))
        return tok;
    return {};
}

bool ScdModelLoader::applyIedDoiDescPatches(const QString& iedName, const QVector<IedMemberDescPatch>& patches,
    const QHash<QString, QString>& doiDesc)
{
    const int idx = m_modelByIed.value(iedName, -1);
    if (idx < 0 || idx >= m_models.size())
        return false;
    IedModel& model = m_models[idx];
    bool changed = false;
    if (!doiDesc.isEmpty() && model.doiDesc != doiDesc) {
        model.doiDesc = doiDesc;
        changed = true;
    }
    for (const IedMemberDescPatch& patch : patches) {
        for (DataSetEntry& ds : model.datasets) {
            if (ds.name != patch.dsName || ds.ldInst != patch.ldInst)
                continue;
            const int n = qMin(patch.memberDescs.size(), ds.members.size());
            for (int k = 0; k < n; ++k) {
                const QString& desc = patch.memberDescs.at(k);
                if (!desc.isEmpty() && desc != ds.members[k].desc) {
                    ds.members[k].desc = desc;
                    changed = true;
                }
            }
        }
    }
    return changed;
}

bool ScdModelLoader::selfTestDoiEnrich(const QString& smcdPath, int maxSingleIedMs, int maxIndexMs)
{
    const auto fail = [](const char* msg) {
        fprintf(stderr, "selfTestDoiEnrich: %s\n", msg);
        return false;
    };
    if (!QFileInfo::exists(smcdPath))
        return true;
    QElapsedTimer t;
    t.start();
    const QHash<QString, qint64> offs = smcdIedOffsets(smcdPath);
    if (offs.isEmpty())
        return fail("smcdIedOffsets empty");
    if (t.elapsed() > maxIndexMs)
        return fail("index timeout");

    const auto assertIedTag = [&](const QString& iedName) {
        const qint64 off = offs.value(iedName, -1);
        if (off < 0)
            return false;
        QFile f(smcdPath);
        if (!f.open(QIODevice::ReadOnly) || !f.seek(off))
            return false;
        return f.read(5).startsWith("<IED ");
    };
    /* 对照样例是江西站 C_B5012X/C_B5013X；26579 等小站没有这些 IED，跳过而非失败。 */
    if (!offs.contains(QStringLiteral("C_B5012X")) || !offs.contains(QStringLiteral("C_B5013X")))
        return true;
    if (!assertIedTag(QStringLiteral("C_B5012X")) || !assertIedTag(QStringLiteral("C_B5013X")))
        return fail("IED byte offset not at <IED ");

    ScdModelLoader loader;
    QVector<IedModel> parsed = parseModelsFromSmcd(smcdPath, true);
    if (parsed.isEmpty())
        return fail("parseModelsFromSmcd empty");
    loader.adoptParsedModels(std::move(parsed));

    for (const QString& iedName :
        { QStringLiteral("C_B5013X"), QStringLiteral("C_B5012X"), QStringLiteral("P_L5053B") }) {
        const IedModel* im = loader.modelFor(iedName);
        const qint64 off = offs.value(iedName, -1);
        if (!im || off < 0)
            return fail("missing IED model or offset");
        QVector<IedMemberDescPatch> patches;
        QHash<QString, QString> doiMap;
        t.restart();
        if (!buildIedDoiDescPatchesAtOffset(smcdPath, off, iedName, *im, &patches, &doiMap) || patches.isEmpty())
            return fail("buildIedDoiDescPatchesAtOffset failed");
        if (!loader.applyIedDoiDescPatches(iedName, patches, doiMap))
            return fail("applyIedDoiDescPatches made no change");
        if (t.elapsed() > maxSingleIedMs)
            return fail("single IED enrich timeout");
    }

    const IedModel* b5013 = loader.modelFor(QStringLiteral("C_B5013X"));
    const DataSetEntry* warn =
        findDataset(b5013, QStringLiteral("dsWarning"), QStringLiteral("LD0"));
    if (!warn || warn->members.isEmpty() || warn->members.at(0).desc != QStringLiteral("对时信号状态"))
        return fail("C_B5013X dsWarning[0] != 对时信号状态");

    const IedModel* b5012 = loader.modelFor(QStringLiteral("C_B5012X"));
    const DataSetEntry* din1 =
        findDataset(b5012, QStringLiteral("dsDin1"), QStringLiteral("CTRL"));
    if (!din1)
        din1 = findDataset(b5012, QStringLiteral("dsDin1"), QStringLiteral("LD0"));
    if (!din1 || din1->members.isEmpty() || din1->members.at(0).desc != QStringLiteral("GO手合信号"))
        return fail("C_B5012X dsDin1[0] != GO手合信号");

    for (const DataSetMember& m : din1->members) {
        if (m.desc == QStringLiteral("Ind1") || m.desc == QStringLiteral("Ind2"))
            return fail("C_B5012X still has Ind1/Ind2 desc");
    }

    const IedModel* pl = loader.modelFor(QStringLiteral("P_L5053B"));
    if (!pl)
        return fail("P_L5053B model missing after enrich");
    int relayDinCount = 0;
    for (const DataSetEntry& ds : pl->datasets) {
        if (ds.name == QStringLiteral("dsRelayDin"))
            ++relayDinCount;
    }
    if (relayDinCount < 2)
        return fail("P_L5053B expected two dsRelayDin (LD0 and PROT)");
    const DataSetEntry* ld0Din =
        findDataset(pl, QStringLiteral("dsRelayDin"), QStringLiteral("LD0"));
    if (!ld0Din || ld0Din->members.isEmpty()
        || ld0Din->members.at(0).desc != QStringLiteral("远方操作硬压板"))
        return fail("P_L5053B LD0 dsRelayDin[0] != 远方操作硬压板");
    const DataSetEntry* protDin =
        findDataset(pl, QStringLiteral("dsRelayDin"), QStringLiteral("PROT"));
    if (!protDin || protDin->members.size() < 6)
        return fail("P_L5053B PROT dsRelayDin missing or too small");
    bool foundPsch = false;
    bool foundGgio3 = false;
    for (const DataSetMember& m : protDin->members) {
        if (addressMatchesMmsDataRef(m.address, QStringLiteral("P_L5053BPROT/PSCH1$ST$Ind2"))) {
            if (m.desc != QStringLiteral("远传2开出"))
                return fail("P_L5053B PROT PSCH1.Ind2 desc != 远传2开出");
            foundPsch = true;
        }
        if (addressMatchesMmsDataRef(m.address, QStringLiteral("P_L5053BPROT/GGIO3$ST$Ind3"))) {
            if (m.desc != QStringLiteral("远传1-1"))
                return fail("P_L5053B PROT GGIO3.Ind3 desc != 远传1-1");
            foundGgio3 = true;
        }
    }
    if (!foundPsch || !foundGgio3)
        return fail("P_L5053B PROT dsRelayDin missing PSCH1.Ind2 or GGIO3.Ind3");
    if (descForDataRef(pl, QStringLiteral("P_L5053BPROT/PSCH1$ST$Ind2")) != QStringLiteral("远传2开出"))
        return fail("descForDataRef PSCH1$ST$Ind2");
    if (descForDataRef(pl, QStringLiteral("P_L5053BPROT/GGIO3$ST$Ind3")) != QStringLiteral("远传1-1"))
        return fail("descForDataRef GGIO3$ST$Ind3");
    if (displayPointDesc(pl, QStringLiteral("P_L5053BPROT/PSCH1$ST$Ind2"),
            QStringLiteral("P_L5053BPROT/PSCH1$ST$Ind2"))
        != QStringLiteral("远传2开出"))
        return fail("displayPointDesc MMS-path fallback PSCH1");
    if (displayPointDesc(pl, QStringLiteral("P_L5053BPROT/GGIO3$ST$Ind3"), QStringLiteral("Ind3"))
        != QStringLiteral("远传1-1"))
        return fail("displayPointDesc Ind3 fallback");
    if (displayPointDesc(pl, QStringLiteral("P_L5053BPROT/GGIO3$ST$Ind4"), QString())
        != QStringLiteral("远传1-2"))
        return fail("displayPointDesc GGIO3$ST$Ind4");
    if (isHumanPointDesc(QStringLiteral("P_L5053BPROT/PSCH1$ST$Ind2")))
        return fail("MMS path must not count as human desc");
    if (rcbReportRefMatches(QStringLiteral("P_L5053BPROT/LLN0$BR$brcbRelayDin01"),
            QStringLiteral("P_L5053BLD0/LLN0$BR$brcbRelayDin"),
            QStringLiteral("P_L5053BLD0/LLN0$BR$brcbRelayDin01")))
        return fail("rcbReportRefMatches must not cross LD0 vs PROT");
    return true;
}

bool ScdModelLoader::enrichIedDoiDescFromSmcd(const QString& path, const QString& iedName)
{
    const IedModel* model = modelFor(iedName);
    if (!model)
        return false;
    QVector<IedMemberDescPatch> patches;
    QHash<QString, QString> doiMap;
    if (!buildIedDoiDescPatches(path, iedName, *model, &patches, &doiMap))
        return false;
    return applyIedDoiDescPatches(iedName, patches, doiMap);
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
    const auto ldOf = [](const QString& r) { return r.section(QLatin1Char('/'), 0, 0); };
    const QString reportLd = ldOf(reportRcb);
    if (!reportLd.isEmpty() && reportRcb.contains(QLatin1Char('/'))) {
        const QString handlerLd = ldOf(handlerRef);
        const QString getLd = ldOf(getRef);
        const bool handlerLdOk = handlerLd.isEmpty() || handlerLd == reportLd;
        const bool getLdOk = getLd.isEmpty() || getLd == reportLd;
        if (!handlerLdOk && !getLdOk)
            return false;
    }
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
    auto ldAlias = [&](const QString& a, const QString& b) {
        if (a == b)
            return true;
        if (model->iedName.isEmpty())
            return false;
        return a == model->iedName + b || b == model->iedName + a;
    };
    for (const DataSetEntry& ds : model->datasets) {
        if (ds.name == dsName && ldAlias(ds.ldInst, ldInst))
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

bool ScdModelLoader::isUpstreamProxyLd(const QString& iedName, const QString& ldInst)
{
    const QString inst = ldInst.isEmpty() ? QStringLiteral("LD0") : ldInst;
    static const QString kKeep[] = {
        QStringLiteral("LD0"),
        QStringLiteral("PROT"),
        QStringLiteral("CTRL"),
        QStringLiteral("MEAS"),
        QStringLiteral("RCD"),
    };
    for (const QString& suffix : kKeep) {
        if (inst == suffix)
            return true;
        if (!iedName.isEmpty() && inst == iedName + suffix)
            return true;
    }
    return false;
}

QVector<RcbDescriptor> ScdModelLoader::allSubscribeRcbs(int* skippedNonProxy) const
{
    QVector<RcbDescriptor> out;
    QSet<QString> seenHandler;
    int skipped = 0;
    for (const IedDeviceEntry& dev : m_devices) {
        const IedModel* model = modelFor(dev.iedName);
        if (!model)
            continue;
        for (const ReportEntry& rpt : model->reports) {
            if (rpt.datSet.isEmpty() || rpt.name.isEmpty())
                continue;
            if (!isUpstreamProxyLd(dev.iedName, rpt.ldInst)) {
                ++skipped;
                continue;
            }
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
    if (skippedNonProxy)
        *skippedNonProxy = skipped;
    return out;
}
