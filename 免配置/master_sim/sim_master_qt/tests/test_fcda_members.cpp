#include "scd/scd_model.h"

#include <QFile>
#include <QTemporaryFile>
#include <QtTest>

static const char kTinyScd[] = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<SCL>
  <DataTypeTemplates>
    <LNodeType id="LN_LLN0" lnClass="LLN0">
      <DO name="FunEna1" type="CN_SPC"/>
      <DO name="A" type="CN_WYE"/>
    </LNodeType>
    <LNodeType id="LN_GGIO" lnClass="GGIO">
      <DO name="Alm1" type="CN_SPS"/>
    </LNodeType>
    <DOType id="CN_SPC" cdc="SPC">
      <DA name="stVal" bType="BOOLEAN" fc="ST"/>
      <DA name="q" bType="Quality" fc="ST"/>
      <DA name="t" bType="Timestamp" fc="ST"/>
      <DA name="ctlModel" bType="Enum" fc="CF"/>
    </DOType>
    <DOType id="CN_SPS" cdc="SPS">
      <DA name="stVal" bType="BOOLEAN" fc="ST"/>
      <DA name="q" bType="Quality" fc="ST"/>
      <DA name="t" bType="Timestamp" fc="ST"/>
    </DOType>
    <DOType id="CN_WYE" cdc="WYE">
      <SDO name="phsA" type="CN_CMV"/>
    </DOType>
    <DOType id="CN_CMV" cdc="CMV">
      <DA name="cVal" bType="Struct" type="CN_Vector" fc="MX"/>
      <DA name="q" bType="Quality" fc="MX"/>
      <DA name="t" bType="Timestamp" fc="MX"/>
    </DOType>
    <DAType id="CN_Vector">
      <BDA name="mag" bType="Struct" type="CN_Analogue"/>
      <BDA name="ang" bType="Struct" type="CN_Analogue"/>
    </DAType>
    <DAType id="CN_Analogue">
      <BDA name="f" bType="FLOAT32"/>
    </DAType>
  </DataTypeTemplates>
  <IED name="E1">
    <AccessPoint name="S1">
      <Server>
        <LDevice inst="LD0">
          <LN0 lnType="LN_LLN0" lnClass="LLN0" inst="">
            <DataSet name="dsRelayEna">
              <FCDA ldInst="LD0" lnClass="LLN0" doName="FunEna1" fc="ST"/>
              <FCDA ldInst="LD0" lnClass="LLN0" doName="FunEna1" daName="stVal" fc="ST"/>
              <FCDA ldInst="LD0" lnClass="LLN0" doName="A.phsA" fc="MX"/>
              <FCDA ldInst="LD0" lnClass="LLN0" doName="A.phsA" daName="cVal.mag.f" fc="MX"/>
              <FCDA ldInst="LD0" prefix="Alm" lnClass="GGIO" lnInst="1" doName="Alm1" fc="ST"/>
            </DataSet>
          </LN0>
          <LN prefix="Alm" lnClass="GGIO" inst="1" lnType="LN_GGIO"/>
        </LDevice>
      </Server>
    </AccessPoint>
  </IED>
</SCL>
)xml";

class TestFcdaMembers : public QObject {
    Q_OBJECT
private slots:
    void tinyModelMembersFollowReference()
    {
        QTemporaryFile file(QStringLiteral("fcdaXXXXXX.smcd"));
        QVERIFY(file.open());
        file.write(kTinyScd);
        file.close();
        const QVector<IedModel> models = ScdModelLoader::parseModelsFromSmcd(file.fileName(), true);
        QCOMPARE(models.size(), 1);
        const DataSetEntry* ds = ScdModelLoader::findDataset(&models.at(0), QStringLiteral("dsRelayEna"));
        QVERIFY(ds);
        QCOMPARE(ds->members.size(), 5);
        QCOMPARE(ds->members.at(0).valueMembers,
            QStringList({ QStringLiteral("stVal"), QStringLiteral("q"), QStringLiteral("t") }));
        QCOMPARE(ds->members.at(1).valueMembers, QStringList({ QStringLiteral("stVal") }));
        QCOMPARE(ds->members.at(2).valueMembers,
            QStringList({ QStringLiteral("cVal{mag{f},ang{f}}"), QStringLiteral("q"), QStringLiteral("t") }));
        QCOMPARE(ds->members.at(3).valueMembers, QStringList({ QStringLiteral("cVal.mag.f") }));
        QCOMPARE(ds->members.at(4).valueMembers,
            QStringList({ QStringLiteral("stVal"), QStringLiteral("q"), QStringLiteral("t") }));
    }

    void stationSmcdFunEnaIncludesQualityAndTime()
    {
        const QString path = QStringLiteral(SIM_STATION_SMCD);
        if (!QFile::exists(path))
            QSKIP("station.SMCD is not in the workspace");
        const QVector<IedModel> models = ScdModelLoader::parseModelsFromSmcd(path, true);
        QVERIFY(!models.isEmpty());
        const DataSetMember* fun = nullptr;
        for (const IedModel& model : models) {
            for (const DataSetEntry& ds : model.datasets) {
                for (const DataSetMember& mem : ds.members) {
                    if (mem.address.contains(QStringLiteral(".FunEna1[ST]"))) {
                        fun = &mem;
                        break;
                    }
                }
                if (fun)
                    break;
            }
            if (fun)
                break;
        }
        QVERIFY(fun);
        QCOMPARE(fun->valueMembers, QStringList({ QStringLiteral("stVal"), QStringLiteral("q"), QStringLiteral("t") }));
    }
};

QTEST_GUILESS_MAIN(TestFcdaMembers)
#include "test_fcda_members.moc"
