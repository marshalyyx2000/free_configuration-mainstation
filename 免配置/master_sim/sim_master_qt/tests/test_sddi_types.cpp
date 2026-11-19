#include "ops/config_generator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

static int countId(const QByteArray& xml, const char* id)
{
    const QByteArray needle = QByteArray("id=\"") + id + "\"";
    int n = 0;
    int pos = 0;
    while ((pos = xml.indexOf(needle, pos)) >= 0) {
        ++n;
        pos += needle.size();
    }
    return n;
}

class TestSddiTypes : public QObject {
    Q_OBJECT
private slots:
    void missingTypeIsInsertedOnce()
    {
        const QByteArray smcd =
            "<?xml version=\"1.0\"?>\n<SCL>\n"
            "  <IED name=\"SubStation\"><AccessPoint><Server><LDevice inst=\"LD0\">\n"
            "    <LN lnClass=\"SDDI\" lnType=\"SUBSTATION_SDDI_V2.00\" prefix=\"E1\" inst=\"1\"/>\n"
            "  </LDevice></Server></AccessPoint></IED>\n"
            "  <DataTypeTemplates>\n"
            "    <EnumType id=\"Mod\"><EnumVal ord=\"1\">on</EnumVal></EnumType>\n"
            "  </DataTypeTemplates>\n</SCL>\n";
        const QByteArray out = ConfigGenerator::ensureSubstationSddiTypes(smcd);
        QCOMPARE(countId(out, "SUBSTATION_SDDI_V2.00"), 1);
        QCOMPARE(countId(out, "SUBSTATION_SPS_EX"), 1);
        QCOMPARE(countId(out, "SUBSTATION_INC_Mod"), 1);
        QCOMPARE(countId(out, "Mod"), 1);
        QCOMPARE(countId(out, "ctlModel"), 1);
        const QByteArray again = ConfigGenerator::ensureSubstationSddiTypes(out);
        QCOMPARE(again, out);
    }

    void stationScdGainsSddiType()
    {
        const QDir root(QStringLiteral(SIM_SCD_DIR));
        if (!root.exists())
            QSKIP("子站 SCD 目录不在本机");
        QStringList scds;
        const auto collect = [&](const QDir& dir) {
            const auto files = dir.entryInfoList({ QStringLiteral("*.scd"), QStringLiteral("*.SCD") }, QDir::Files);
            for (const QFileInfo& fi : files)
                scds.append(fi.absoluteFilePath());
        };
        collect(root);
        const auto subs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& sub : subs)
            collect(QDir(sub.absoluteFilePath()));
        if (scds.isEmpty())
            QSKIP("没有可用于生成的 SCD");

        for (const QString& scdPath : scds) {
            QTemporaryDir dir;
            QVERIFY2(dir.isValid(), qPrintable(scdPath));
            const QVector<ConnectedDeviceEntry> devices = ConfigGenerator::generate(scdPath, dir.path());
            QVERIFY2(!devices.isEmpty(), qPrintable(scdPath));
            QFile smcd(dir.filePath(QStringLiteral("station.SMCD")));
            QVERIFY2(smcd.open(QIODevice::ReadOnly), qPrintable(scdPath));
            const QByteArray data = smcd.readAll();
            QCOMPARE(countId(data, "SUBSTATION_SDDI_V2.00"), 1);
            QVERIFY2(data.contains("lnType=\"SUBSTATION_SDDI_V2.00\""), qPrintable(scdPath));
            QCOMPARE(countId(data, "SUBSTATION_SPS_EX"), 1);
        }
    }
};

QTEST_GUILESS_MAIN(TestSddiTypes)
#include "test_sddi_types.moc"
