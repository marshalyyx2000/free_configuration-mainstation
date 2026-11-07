#include "ops/config_generator.h"
#include "ops/station_zip_writer.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

#include <algorithm>
#include <stdexcept>

namespace {

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

QVector<ConnectedDeviceEntry> dedupeDevices(QVector<ConnectedDeviceEntry> devices)
{
    QHash<QString, ConnectedDeviceEntry> picked;
    for (const ConnectedDeviceEntry& dev : devices) {
        if (!picked.contains(dev.iedName)) {
            picked.insert(dev.iedName, dev);
            continue;
        }
        const ConnectedDeviceEntry& old = picked[dev.iedName];
        if (!old.ip.startsWith(QStringLiteral("10.")) && dev.ip.startsWith(QStringLiteral("10.")))
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

QString substationIedXml(const QVector<ConnectedDeviceEntry>& devices)
{
    QStringList fcdaLines;
    for (const ConnectedDeviceEntry& dev : devices) {
        fcdaLines << QStringLiteral("            <FCDA doName=\"Proxy\" fc=\"ST\" ldInst=\"%1LD0\" lnClass=\"LPHD\" lnInst=\"1\"/>")
                         .arg(dev.iedName);
    }
    const QString fcdaBlock = fcdaLines.join(QLatin1Char('\n'));
    return QStringLiteral(
        "  <IED configVersion=\"V1.00\" desc=\"保信子站\" manufacturer=\"master_sim\" name=\"SubStation\" type=\"virtual\">\n"
        "    <AccessPoint desc=\"站控层\" name=\"S1\">\n"
        "      <Server timeout=\"30\">\n"
        "        <Authentication/>\n"
        "        <LDevice desc=\"子站\" inst=\"LD0\">\n"
        "          <LN0 desc=\"管理逻辑节点\" inst=\"\" lnClass=\"LLN0\" lnType=\"XJ_PAC852GG_LD0_V01_LLN0\">\n"
        "            <DataSet desc=\"通信工况\" name=\"dsCommState\">\n"
        "%1\n"
        "            </DataSet>\n"
        "          </LN0>\n"
        "        </LDevice>\n"
        "      </Server>\n"
        "    </AccessPoint>\n"
        "  </IED>\n")
        .arg(fcdaBlock);
}

void injectSubstation(const QString& scdPath, const QString& destSmcd, const QVector<ConnectedDeviceEntry>& devices)
{
    // QFile::copy() fails when the destination already exists. A station.SMCD
    // left over from a previous run therefore survived silently, so the new SCD
    // never reached the device: IEDList.xml described the new station while
    // station.zip still shipped the old (much smaller) model.
    if (QFileInfo::exists(destSmcd) && !QFile::remove(destSmcd))
        throw std::runtime_error(
            QStringLiteral("无法覆盖旧的 station.SMCD: %1").arg(destSmcd).toUtf8().constData());

    QFile in(scdPath);
    if (!in.open(QIODevice::ReadOnly))
        throw std::runtime_error(
            QStringLiteral("无法读取 SCD: %1").arg(scdPath).toUtf8().constData());
    // Binary on purpose: the SCD may be ~100 MB and its element order, entity
    // escaping and line endings must survive byte for byte for genconfig.jar.
    QByteArray data = in.readAll();
    in.close();

    if (!data.contains("name=\"SubStation\"")) {
        const int pos = data.indexOf("<IED ");
        if (pos < 0)
            throw std::runtime_error("SCD 中未找到 <IED>，无法插入 SubStation");
        data.insert(pos, substationIedXml(devices).toUtf8());
    }

    QFile out(destSmcd);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        throw std::runtime_error(
            QStringLiteral("无法写入 station.SMCD: %1").arg(destSmcd).toUtf8().constData());
    if (out.write(data) != data.size())
        throw std::runtime_error(
            QStringLiteral("写入 station.SMCD 不完整: %1").arg(destSmcd).toUtf8().constData());
    out.close();
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

    const QString smcdPath = QDir(outDirPath).absoluteFilePath(QStringLiteral("station.SMCD"));
    injectSubstation(scdPath, smcdPath, devices);

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
