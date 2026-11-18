#include "ops/config_generator.h"
#include "ops/station_zip_writer.h"
#include "util/app_paths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {

void writeBinaryFileReplace(const QString& path, const QByteArray& data);

QString localTag(const QString& name)
{
    const int idx = name.lastIndexOf(QLatin1Char('}'));
    return idx >= 0 ? name.mid(idx + 1) : name;
}

QHash<QString, QString> deviceDefaults()
{
    QHash<QString, QString> d;
    d.insert(QStringLiteral("rptno"), QStringLiteral("9"));
    d.insert(QStringLiteral("BRCB_TrgOps"), QStringLiteral("44"));
    d.insert(QStringLiteral("URCB_TrgOps"), QStringLiteral("4C"));
    d.insert(QStringLiteral("BRCB_OptFlds"), QStringLiteral("3900"));
    d.insert(QStringLiteral("URCB_OptFlds"), QStringLiteral("3800"));
    d.insert(QStringLiteral("wave_dir"), QStringLiteral("/COMTRADE/"));
    d.insert(QStringLiteral("wave_type"), QStringLiteral("7"));
    d.insert(QStringLiteral("file_suff_capital"), QStringLiteral("0"));
    d.insert(QStringLiteral("wave_path"), QStringLiteral("/COMTRADE/"));
    d.insert(QStringLiteral("n61850_version"), QStringLiteral("1"));
    return d;
}

int connectedApRank(const QString& ip)
{
    /* 生产 172.16 A > 172.17 B > 实验室 SCDPro 10.100.100 A > 其它。
     * 26579 的 11.100.100 是 B 网，不能压过 10.100.100。 */
    if (ip.startsWith(QStringLiteral("172.16.")))
        return 4;
    if (ip.startsWith(QStringLiteral("172.17.")))
        return 3;
    if (ip.startsWith(QStringLiteral("10.100.100.")))
        return 2;
    if (ip.startsWith(QStringLiteral("11.100.100.")))
        return 0;
    if (ip.startsWith(QStringLiteral("10.")))
        return 1;
    return 1;
}

QVector<ConnectedDeviceEntry> dedupeDevices(QVector<ConnectedDeviceEntry> devices)
{
    QHash<QString, ConnectedDeviceEntry> picked;
    for (const ConnectedDeviceEntry& dev : devices) {
        if (!picked.contains(dev.iedName)) {
            picked.insert(dev.iedName, dev);
            continue;
        }
        const ConnectedDeviceEntry& old = picked[dev.iedName];
        if (connectedApRank(dev.ip) > connectedApRank(old.ip))
            picked[dev.iedName] = dev;
    }
    QVector<ConnectedDeviceEntry> result;
    result.reserve(picked.size());
    for (auto it = picked.constBegin(); it != picked.constEnd(); ++it)
        result.append(it.value());
    std::sort(result.begin(), result.end(),
        [](const ConnectedDeviceEntry& a, const ConnectedDeviceEntry& b) { return a.iedName < b.iedName; });
    return result;
}

QString buildIedListXml(const QVector<ConnectedDeviceEntry>& devices)
{
    const QString now = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    QStringList lines;
    lines << QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    lines << QStringLiteral("<CONFIG>");
    lines << QStringLiteral("  <Header desc=\"仿真主站生成\" version=\"1.0\" CRC=\"0\" date=\"%1\">").arg(now);
    lines << QStringLiteral("    <History>");
    lines << QStringLiteral("      <Hitem version=\"1.0\" revision=\"0\" when=\"%1\" who=\"master_sim\" what=\"自动生成\" />")
                 .arg(now);
    lines << QStringLiteral("    </History>");
    lines << QStringLiteral("  </Header>");
    lines << QStringLiteral("  <IEDList>");
    const QHash<QString, QString> defs = deviceDefaults();
    int no = 1;
    for (const ConnectedDeviceEntry& dev : devices) {
        QString escName = dev.desc;
        escName.replace(QLatin1Char('"'), QLatin1Char('\''));
        const QString ch = QStringLiteral("%1:102").arg(dev.ip);
        QStringList attrParts;
        for (auto it = defs.constBegin(); it != defs.constEnd(); ++it)
            attrParts << QStringLiteral("%1=\"%2\"").arg(it.key(), it.value());
        lines << QStringLiteral("    <Device no=\"%1\" name=\"%2\" ied_name=\"%3\" addr=\"0\" %4 ch_para=\"%5\" p_type=\"1\" />")
                     .arg(no++)
                     .arg(escName, dev.iedName, attrParts.join(QLatin1Char(' ')), ch);
    }
    lines << QStringLiteral("  </IEDList>");
    lines << QStringLiteral("</CONFIG>");
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString resolveLln0LnType(const QString& scdPath)
{
    struct Lln0Stat {
        int n = 0;
        int maxRcb = 0;
        bool badRcb = false;
    };

    auto scanLln0Stats = [&](const QSet<QString>& templateIds) -> QHash<QString, Lln0Stat> {
        QHash<QString, Lln0Stat> stats;
        QFile f(scdPath);
        if (!f.open(QIODevice::ReadOnly))
            throw std::runtime_error(
                QStringLiteral("无法读取 SCD: %1").arg(scdPath).toUtf8().constData());
        QXmlStreamReader xml(&f);
        while (!xml.atEnd()) {
            if (xml.readNext() != QXmlStreamReader::StartElement)
                continue;
            if (localTag(xml.name().toString()) != QStringLiteral("LN0"))
                continue;
            const QString lnType = xml.attributes().value(QStringLiteral("lnType")).toString();
            if (lnType.isEmpty() || !templateIds.contains(lnType))
                continue;
            Lln0Stat& st = stats[lnType];
            st.n++;
            int rcbs = 0;
            while (xml.readNext() != QXmlStreamReader::Invalid) {
                if (xml.isEndElement() && localTag(xml.name().toString()) == QStringLiteral("LN0"))
                    break;
                if (!xml.isStartElement())
                    continue;
                if (localTag(xml.name().toString()) != QStringLiteral("ReportControl"))
                    continue;
                rcbs++;
                if (xml.attributes().value(QStringLiteral("confRev")).isEmpty())
                    st.badRcb = true;
            }
            st.maxRcb = qMax(st.maxRcb, rcbs);
        }
        if (xml.hasError())
            throw std::runtime_error(
                QStringLiteral("解析 SCD LN0 失败: %1").arg(xml.errorString()).toUtf8().constData());
        return stats;
    };

    QSet<QString> templateIds;
    {
        QFile f(scdPath);
        if (!f.open(QIODevice::ReadOnly))
            throw std::runtime_error(
                QStringLiteral("无法读取 SCD 以解析 DataTypeTemplates: %1").arg(scdPath).toUtf8().constData());
        QXmlStreamReader xml(&f);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && localTag(xml.name().toString()) == QStringLiteral("LNodeType"))
                templateIds.insert(xml.attributes().value(QStringLiteral("id")).toString());
        }
        if (xml.hasError())
            throw std::runtime_error(
                QStringLiteral("解析 SCD DataTypeTemplates 失败: %1").arg(xml.errorString()).toUtf8().constData());
    }

    const QHash<QString, Lln0Stat> lln0Stats = scanLln0Stats(templateIds);
    QString best;
    int bestN = 0;
    bool poolZeroRcb = false;
    for (auto it = lln0Stats.constBegin(); it != lln0Stats.constEnd(); ++it) {
        if (!it.value().badRcb)
            poolZeroRcb = poolZeroRcb || (it.value().maxRcb == 0);
    }
    for (auto it = lln0Stats.constBegin(); it != lln0Stats.constEnd(); ++it) {
        const Lln0Stat& st = it.value();
        if (st.badRcb)
            continue;
        if (poolZeroRcb && st.maxRcb != 0)
            continue;
        if (st.n > bestN) {
            bestN = st.n;
            best = it.key();
        }
    }
    if (best.isEmpty()) {
        throw std::runtime_error(
            "SCD 中找不到可用于 SubStation 的 LLN0 lnType（DataTypeTemplates 与 LN0 不匹配）");
    }
    return best;
}

QString xmlEsc(const QString& s)
{
    QString o = s;
    o.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    o.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    o.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    o.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return o;
}

QString commStatusDu(const QString& desc)
{
    const QString d = desc.trimmed().isEmpty() ? QStringLiteral("装置") : desc.trimmed();
    if (d.contains(QStringLiteral("通信状态")))
        return d;
    return d + QStringLiteral("通信状态");
}

QString sddiLnXml(const QString& prefix, int inst, const QString& desc)
{
    const QString du = xmlEsc(commStatusDu(desc));
    return QStringLiteral(
        "          <LN desc=\"状态监视信息\" prefix=\"%1\" inst=\"%2\" lnClass=\"SDDI\" lnType=\"SUBSTATION_SDDI_V2.00\">\n"
        "            <DOI name=\"Mod\" desc=\"Mode\">\n"
        "              <DAI name=\"stVal\"><Val>on</Val></DAI>\n"
        "              <DAI name=\"ctlModel\"><Val>status-only</Val></DAI>\n"
        "            </DOI>\n"
        "            <DOI name=\"Beh\" desc=\"Behaviour\">\n"
        "              <DAI name=\"stVal\"><Val>on</Val></DAI>\n"
        "            </DOI>\n"
        "            <DOI name=\"Health\" desc=\"Health\">\n"
        "              <DAI name=\"stVal\"><Val>Ok</Val></DAI>\n"
        "            </DOI>\n"
        "            <DOI name=\"NamPlt\" desc=\"Name Plate\">\n"
        "              <DAI name=\"vendor\"><Val>NR</Val></DAI>\n"
        "              <DAI name=\"swRev\"><Val>R1.00</Val></DAI>\n"
        "              <DAI name=\"d\"><Val>Name Plate</Val></DAI>\n"
        "              <DAI name=\"dU\"><Val>Name Plate</Val></DAI>\n"
        "              <DAI name=\"lnNs\"><Val>NR PCS979X:2015</Val></DAI>\n"
        "            </DOI>\n"
        "            <DOI desc=\"%3\" name=\"ComStatus\">\n"
        "              <DAI name=\"stVal\"/>\n"
        "              <DAI name=\"dU\"><Val>%3</Val></DAI>\n"
        "              <DAI name=\"dataNs\"><Val>NR PCS979X:2015</Val></DAI>\n"
        "            </DOI>\n"
        "          </LN>\n")
        .arg(xmlEsc(prefix), QString::number(inst), du);
}

QString substationIedXml(const QVector<ConnectedDeviceEntry>& devices, const QString& lln0LnType)
{
    QStringList fcdaLines;
    QStringList lnLines;
    int no = 1;
    for (const ConnectedDeviceEntry& dev : devices) {
        fcdaLines << QStringLiteral(
                         "            <FCDA doName=\"ComStatus\" fc=\"ST\" ldInst=\"LD0\" lnClass=\"SDDI\" lnInst=\"%1\" prefix=\"%2\"/>")
                         .arg(no)
                         .arg(xmlEsc(dev.iedName));
        lnLines << sddiLnXml(dev.iedName, no, dev.desc);
        ++no;
    }
    return QStringLiteral(
        "  <IED configVersion=\"V1.00\" desc=\"保信子站\" manufacturer=\"master_sim\" name=\"SubStation\" type=\"virtual\">\n"
        "    <AccessPoint desc=\"站控层\" name=\"S1\">\n"
        "      <Server timeout=\"30\">\n"
        "        <Authentication/>\n"
        "        <LDevice desc=\"子站\" inst=\"LD0\">\n"
        "          <LN0 desc=\"管理逻辑节点\" inst=\"\" lnClass=\"LLN0\" lnType=\"%2\">\n"
        "            <DataSet desc=\"通信工况\" name=\"dsCommState\">\n"
        "%1\n"
        "            </DataSet>\n"
        "            <ReportControl name=\"brcbCommState\" datSet=\"dsCommState\" intgPd=\"0\" rptID=\"LD0/LLN0$BR$brcbCommState\" confRev=\"1\" buffered=\"true\" bufTime=\"0\">\n"
        "              <TrgOps dchg=\"true\" qchg=\"false\" dupd=\"false\" period=\"true\"/>\n"
        "              <OptFields seqNum=\"false\" timeStamp=\"true\" dataSet=\"true\" reasonCode=\"true\" dataRef=\"true\" entryID=\"true\" configRef=\"true\"/>\n"
        "              <RptEnabled max=\"1\"/>\n"
        "            </ReportControl>\n"
        "          </LN0>\n"
        "%3"
        "        </LDevice>\n"
        "      </Server>\n"
        "    </AccessPoint>\n"
        "  </IED>\n")
        .arg(fcdaLines.join(QLatin1Char('\n')), xmlEsc(lln0LnType), lnLines.join(QString()));
}

QString findSubstationIcdTemplate(const QString& scdPath)
{
    QStringList cands;
    if (!scdPath.isEmpty())
        cands << QDir(QFileInfo(scdPath).absolutePath()).absoluteFilePath(QStringLiteral("../SubStation.icd"));
    const QString root = AppPaths::masterSimRoot();
    if (!root.isEmpty())
        cands << QDir(root).absoluteFilePath(QStringLiteral("../SubStation.icd"));
    cands << QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        QStringLiteral("../share/sim_master_qt/SubStation.icd"));
    for (const QString& p : cands) {
        if (QFileInfo::exists(p))
            return QFileInfo(p).absoluteFilePath();
    }
    return {};
}

QString replaceSddiLns(QString text, const QVector<ConnectedDeviceEntry>& devices)
{
    QRegularExpression re(QStringLiteral(R"(<LN\b[^>]*\blnClass="SDDI")"));
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    int first = -1;
    int lastStart = -1;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (first < 0)
            first = m.capturedStart();
        lastStart = m.capturedStart();
    }
    if (first < 0)
        throw std::runtime_error("SubStation.icd 模板中没有 SDDI LN");
    const int ldClose = text.indexOf(QStringLiteral("</LDevice>"), lastStart);
    if (ldClose < 0)
        throw std::runtime_error("SubStation.icd 找不到 </LDevice>");
    const int lastLn = text.lastIndexOf(QStringLiteral("</LN>"), ldClose);
    if (lastLn < first)
        throw std::runtime_error("SubStation.icd 找不到 SDDI </LN>");
    QString block;
    int no = 1;
    for (const ConnectedDeviceEntry& dev : devices)
        block += sddiLnXml(dev.iedName, no++, dev.desc);
    text.replace(first, lastLn + int(strlen("</LN>")) - first, block);
    return text;
}

void writeSubstationIcd(const QString& templatePath, const QVector<ConnectedDeviceEntry>& devices,
    const QString& destPath)
{
    QFile in(templatePath);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        throw std::runtime_error(
            QStringLiteral("无法读取 SubStation.icd 模板: %1").arg(templatePath).toUtf8().constData());
    QString text = QString::fromUtf8(in.readAll());
    in.close();
    text.replace(QStringLiteral("iedName=\"TEMPLATE\""), QStringLiteral("iedName=\"SubStation\""));
    text.replace(QStringLiteral("IED name=\"TEMPLATE\""), QStringLiteral("IED name=\"SubStation\""));
    text.replace(QStringLiteral("id=\"template\""), QStringLiteral("id=\"26579\""));
    text = replaceSddiLns(text, devices);

    QStringList fcdaLines;
    int no = 1;
    for (const ConnectedDeviceEntry& dev : devices) {
        fcdaLines << QStringLiteral(
                         "              <FCDA doName=\"ComStatus\" fc=\"ST\" ldInst=\"LD0\" lnClass=\"SDDI\" lnInst=\"%1\" prefix=\"%2\" />")
                         .arg(no++)
                         .arg(xmlEsc(dev.iedName));
    }
    const QRegularExpression dsRe(
        QStringLiteral(R"(<DataSet\b[^>]*\bname="dsCommState"[^>]*>.*?</DataSet>)"),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch ds = dsRe.match(text);
    if (!ds.hasMatch())
        throw std::runtime_error("SubStation.icd 模板中没有 dsCommState");
    const QString open = ds.captured().left(ds.captured().indexOf(QLatin1Char('>')) + 1);
    const QString rebuilt = open + QLatin1Char('\n') + fcdaLines.join(QLatin1Char('\n'))
        + QStringLiteral("\n            </DataSet>");
    text.replace(ds.capturedStart(), ds.capturedLength(), rebuilt);

    QDir().mkpath(QFileInfo(destPath).absolutePath());
    writeBinaryFileReplace(destPath, text.toUtf8());
}

QByteArray mergeIcdDataTypes(QByteArray smcd, const QByteArray& icd)
{
    const int icdDt = icd.indexOf("<DataTypeTemplates");
    if (icdDt < 0)
        return smcd;
    const int icdInner = icd.indexOf('>', icdDt);
    const int icdEnd = icd.indexOf("</DataTypeTemplates>", icdInner);
    if (icdInner < 0 || icdEnd < 0)
        return smcd;
    const QByteArray types = icd.mid(icdInner + 1, icdEnd - (icdInner + 1));
    const char* tags[] = {"LNodeType", "DOType", "DAType", "EnumType", nullptr};
    QByteArray insert;
    int pos = 0;
    while (pos < types.size()) {
        int next = -1;
        const char* found = nullptr;
        for (int t = 0; tags[t]; ++t) {
            const QByteArray open = QByteArray("<") + tags[t];
            const int p = types.indexOf(open, pos);
            if (p >= 0 && (next < 0 || p < next)) {
                next = p;
                found = tags[t];
            }
        }
        if (next < 0 || !found)
            break;
        const int idPos = types.indexOf("id=\"", next);
        const QByteArray close = QByteArray("</") + found + ">";
        const int closePos = types.indexOf(close, next);
        if (idPos < 0 || closePos < 0)
            break;
        const int idStart = idPos + 4;
        const int idEnd = types.indexOf('"', idStart);
        if (idEnd < 0 || idEnd > closePos)
            break;
        const QByteArray id = types.mid(idStart, idEnd - idStart);
        const int blockEnd = closePos + close.size();
        const QByteArray needle = QByteArray("id=\"") + id + "\"";
        if (smcd.indexOf(needle) < 0) {
            insert.append(types.mid(next, blockEnd - next));
            insert.append('\n');
        }
        pos = blockEnd;
    }
    if (insert.isEmpty())
        return smcd;
    const int smcdEnd = smcd.indexOf("</DataTypeTemplates>");
    if (smcdEnd >= 0)
        return smcd.insert(smcdEnd, insert);
    smcd.append("\n  <DataTypeTemplates>\n");
    smcd.append(insert);
    smcd.append("  </DataTypeTemplates>\n");
    return smcd;
}

QByteArray clampRptEnabledMax(QByteArray data)
{
    // 代理子站每个 RCB 只需一个实例；SCD 常见 max=16/12/18，会让 genconfig
    // 生成数万个 RC，router 启动与每 10ms 的报告扫描被拖垮。
    int pos = 0;
    while ((pos = data.indexOf("<RptEnabled", pos)) >= 0) {
        const int tagEnd = data.indexOf('>', pos);
        if (tagEnd < 0)
            break;
        const int maxPos = data.indexOf("max=\"", pos);
        if (maxPos >= 0 && maxPos < tagEnd) {
            const int valueStart = maxPos + 5;
            const int valueEnd = data.indexOf('"', valueStart);
            if (valueEnd > valueStart && valueEnd < tagEnd) {
                data.replace(valueStart, valueEnd - valueStart, "1");
                pos = valueStart + 1;
                continue;
            }
        }
        pos = tagEnd + 1;
    }
    return data;
}

QByteArray remapLabBNetToScdPro(QByteArray data)
{
    /* 26579 NETA=10.100.100 / NETB=11.100.100。SCDPro 只绑 10.100.100。
     * 装置侧若按 ConnectedAP 后写覆盖 IEDList，会把 ch_para 改回 11.x。 */
    data.replace("11.100.100.", "10.100.100.");
    return data;
}

QByteArray sanitizeSclForGenconfig(QByteArray data)
{
    data.replace("intgPd=\"\"", "intgPd=\"0\"");
    data.replace("bufTime=\"\"", "bufTime=\"0\"");
    data.replace("confRev=\"\"", "confRev=\"1\"");
    // 部分 ICD 把 LD0 写成 LDO（字母 O）；convert2CID 拼 IED 名后变成 P_xxxALDO/
    data.replace("LDO/LLN0$", "LD0/LLN0$");
    return clampRptEnabledMax(data);
}

void writeBinaryFileReplace(const QString& path, const QByteArray& data)
{
    if (QFileInfo::exists(path))
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);

    QFile direct(path);
    if (direct.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const qint64 n = direct.write(data);
        direct.close();
        if (n == data.size())
            return;
    }

    const QString partPath = path + QStringLiteral(".part");
    QFile::remove(partPath);
    QFile part(partPath);
    if (!part.open(QIODevice::WriteOnly))
        throw std::runtime_error(
            QStringLiteral("无法写入临时文件: %1").arg(partPath).toUtf8().constData());
    if (part.write(data) != data.size()) {
        part.close();
        QFile::remove(partPath);
        throw std::runtime_error(
            QStringLiteral("写入 %1 不完整").arg(partPath).toUtf8().constData());
    }
    part.close();

    const QString bakPath = path + QStringLiteral(".bak");
    QFile::remove(bakPath);
    if (QFile::exists(path)) {
        if (!QFile::remove(path) && !QFile::rename(path, bakPath)) {
            QFile::remove(partPath);
            throw std::runtime_error(
                QStringLiteral("无法覆盖 %1（可能被资源管理器预览、杀毒或其它程序占用；请关闭后重试）")
                    .arg(QFileInfo(path).fileName())
                    .toUtf8()
                    .constData());
        }
    }
    if (!QFile::rename(partPath, path)) {
        QFile::remove(partPath);
        throw std::runtime_error(
            QStringLiteral("无法替换 %1").arg(path).toUtf8().constData());
    }
    QFile::remove(bakPath);
}

void injectSubstation(const QString& scdPath, const QString& destSmcd, const QVector<ConnectedDeviceEntry>& devices,
    const QString& lln0LnType)
{
    QFile in(scdPath);
    if (!in.open(QIODevice::ReadOnly))
        throw std::runtime_error(
            QStringLiteral("无法读取 SCD: %1").arg(scdPath).toUtf8().constData());
    // Binary on purpose: the SCD may be ~100 MB and its element order, entity
    // escaping and line endings must survive byte for byte for genconfig.jar.
    QByteArray data = in.readAll();
    in.close();

    static const QByteArray kSubstationMarker = QByteArrayLiteral(
        "  <IED configVersion=\"V1.00\" desc=\"保信子站\" manufacturer=\"master_sim\" name=\"SubStation\" type=\"virtual\">");
    const int existing = data.indexOf(kSubstationMarker);
    if (existing >= 0) {
        const int close = data.indexOf("</IED>", existing);
        if (close >= 0)
            data.remove(existing, close + int(strlen("</IED>")) - existing);
    }

    const int pos = data.indexOf("<IED ");
    if (pos < 0)
        throw std::runtime_error("SCD 中未找到 <IED>，无法插入 SubStation");
    data.insert(pos, substationIedXml(devices, lln0LnType).toUtf8());

    const QString icdTemplate = findSubstationIcdTemplate(scdPath);
    if (!icdTemplate.isEmpty()) {
        QFile icdFile(icdTemplate);
        if (icdFile.open(QIODevice::ReadOnly))
            data = mergeIcdDataTypes(data, icdFile.readAll());
    }

    QByteArray out = sanitizeSclForGenconfig(data);
    bool labScdPro = false;
    for (const ConnectedDeviceEntry& d : devices) {
        if (d.ip.startsWith(QLatin1String("10.100.100."))) {
            labScdPro = true;
            break;
        }
    }
    if (labScdPro)
        out = remapLabBNetToScdPro(out);
    writeBinaryFileReplace(destSmcd, out);
}

} // namespace

QVector<ConnectedDeviceEntry> ConfigGenerator::parseConnectedAps(const QString& scdPath)
{
    QFile file(scdPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QHash<QString, QString> iedDesc;
    QVector<ConnectedDeviceEntry> devices;

    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;
        const QString tag = localTag(xml.name().toString());
        if (tag == QStringLiteral("IED")) {
            const QString name = xml.attributes().value(QStringLiteral("name")).toString();
            if (!name.isEmpty())
                iedDesc[name] = xml.attributes().value(QStringLiteral("desc")).toString();
            xml.skipCurrentElement();
        } else if (tag == QStringLiteral("ConnectedAP")) {
            const QString ied = xml.attributes().value(QStringLiteral("iedName")).toString();
            QString ip;
            while (xml.readNext()) {
                if (xml.isEndElement() && localTag(xml.name().toString()) == QStringLiteral("ConnectedAP"))
                    break;
                if (!xml.isStartElement())
                    continue;
                if (localTag(xml.name().toString()) == QStringLiteral("Address")) {
                    while (xml.readNext()) {
                        if (xml.isEndElement() && localTag(xml.name().toString()) == QStringLiteral("Address"))
                            break;
                        if (xml.isStartElement() && localTag(xml.name().toString()) == QStringLiteral("P")) {
                            if (xml.attributes().value(QStringLiteral("type")).toString() == QStringLiteral("IP"))
                                ip = xml.readElementText().trimmed();
                        }
                    }
                }
            }
            if (!ied.isEmpty() && !ip.isEmpty()) {
                ConnectedDeviceEntry d;
                d.iedName = ied;
                d.ip = ip;
                d.desc = iedDesc.value(ied, ied);
                devices.append(d);
            }
        }
    }
    for (ConnectedDeviceEntry& d : devices) {
        const QString desc = iedDesc.value(d.iedName);
        if (!desc.isEmpty())
            d.desc = desc;
    }
    return dedupeDevices(devices);
}

QVector<ConnectedDeviceEntry> ConfigGenerator::generate(const QString& scdPath, const QString& outDirPath)
{
    if (!QFileInfo::exists(scdPath))
        throw std::runtime_error(
            QStringLiteral("SCD 不存在: %1").arg(scdPath).toUtf8().constData());

    QDir().mkpath(outDirPath);
    QVector<ConnectedDeviceEntry> devices = parseConnectedAps(scdPath);
    if (devices.isEmpty())
        throw std::runtime_error("未从 SCD 解析到任何 ConnectedAP");

    const QString iedPath = QDir(outDirPath).absoluteFilePath(QStringLiteral("IEDList.xml"));
    QFile iedFile(iedPath);
    if (!iedFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        throw std::runtime_error("无法写入 IEDList.xml");
    iedFile.write(buildIedListXml(devices).toUtf8());
    iedFile.close();

    const QString icdTemplate = findSubstationIcdTemplate(scdPath);
    if (!icdTemplate.isEmpty()) {
        const QString icdName = QStringLiteral("SubStation_26579.icd");
        const QString icdOut = QDir(outDirPath).absoluteFilePath(icdName);
        writeSubstationIcd(icdTemplate, devices, icdOut);
        const QString icdBeside = QDir(QFileInfo(icdTemplate).absolutePath()).absoluteFilePath(icdName);
        if (QFileInfo(icdBeside).absoluteFilePath() != QFileInfo(icdOut).absoluteFilePath()) {
            QFile::remove(icdBeside);
            QFile::copy(icdOut, icdBeside);
        }
    }

    const QString lln0LnType = resolveLln0LnType(scdPath);
    const QString smcdPath = QDir(outDirPath).absoluteFilePath(QStringLiteral("station.SMCD"));
    injectSubstation(scdPath, smcdPath, devices, lln0LnType);

    // Guard against a stale/short model: the SMCD is the SCD plus the injected
    // SubStation IED, so it can never be smaller than the source file.
    const qint64 scdSize = QFileInfo(scdPath).size();
    const qint64 smcdSize = QFileInfo(smcdPath).size();
    if (smcdSize < scdSize)
        throw std::runtime_error(
            QStringLiteral("station.SMCD 大小异常（%1 < SCD %2），疑似未覆盖旧文件")
                .arg(smcdSize)
                .arg(scdSize)
                .toUtf8()
                .constData());

    const QString zipPath = QDir(outDirPath).absoluteFilePath(QStringLiteral("station.zip"));
    if (!writeDeflatedZipSingleFile(zipPath, QStringLiteral("station.SMCD"), smcdPath))
        throw std::runtime_error("无法写入 station.zip");

    return devices;
}
