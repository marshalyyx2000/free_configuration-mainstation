#include "scd/scd_model.h"
#include "settings/shared_connection_settings.h"
#include "util/app_paths.h"
#include "event/event_store.h"
#include "event/event_format.h"
#include "event/report_event_logic.h"
#include "ops/config_generator.h"
#include "ops/lab_operations.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

static bool runVerifySelfTest()
{
    if (!ScdModelLoader::rcbReportRefMatches(QStringLiteral("E1Q1SB48PROT/LLN0$BR$brcbRelayEna09"),
            QStringLiteral("E1Q1SB48PROT/LLN0$BR$brcbRelayEna"),
            QStringLiteral("E1Q1SB48PROT/LLN0$BR$brcbRelayEna09")))
        return false;
    if (!ScdModelLoader::addressMatchesMmsDataRef(QStringLiteral("E1Q1SB48PROT/LLN0.FuncEna1[ST]"),
            QStringLiteral("E1Q1SB48PROT/LLN0$ST$FuncEna1")))
        return false;
    if (!ScdModelLoader::addressMatchesMmsDataRef(QStringLiteral("E1Q1SB23CTRL/CSWI1.Pos[ST]"),
            QStringLiteral("E1Q1SB23CTRL/CSWI1$ST$Pos$stVal")))
        return false;
    if (!ScdModelLoader::datasetRefMatches(QStringLiteral("@E1Q1SB23CTRL/LLN0$dsDin1"),
            QStringLiteral("E1Q1SB23CTRL/LLN0$dsDin1")))
        return false;
    if (!ScdModelLoader::datasetMatchesPanel(QStringLiteral("dsRelayFunEn"), ReportPanelType::Other)
        || !ScdModelLoader::datasetMatchesPanel(QStringLiteral("dsRelayState"), ReportPanelType::Other)
        || ScdModelLoader::datasetMatchesPanel(QStringLiteral("dsRelayEna"), ReportPanelType::Other)
        || !ScdModelLoader::datasetMatchesPanel(QStringLiteral("dsRelayEna"), ReportPanelType::RelayEna))
        return false;

    ScdModelLoader model;
    if (!model.loadIedList(AppPaths::iedListPath()))
        return false;
    if (model.devices().size() < 1)
        return false;
    const QString smcd = AppPaths::defaultSmcdPath();
    if (QFileInfo::exists(smcd))
        model.loadSmcd(smcd);
    EventStore store;
    if (!runReportEventSelfTest(model, store))
        return false;
    {
        // Device descriptions must reach the device list. The concrete IED name is
        // station specific (the shipped model may be any SCD), so probe whatever
        // device the loaded SMCD actually contains.
        for (const IedDeviceEntry& dev : model.devices()) {
            const IedModel* im = model.modelFor(dev.iedName);
            if (!im || im->desc.isEmpty() || im->desc == dev.iedName)
                continue;
            bool labeled = false;
            for (const IedDeviceEntry& d : model.devices()) {
                if (d.iedName == dev.iedName && d.desc == im->desc)
                    labeled = true;
            }
            if (!labeled)
                return false;
            break;
        }
    }

    bool modelHasOtherRcb = false;
    bool subscribedOtherRcb = false;
    for (const IedDeviceEntry& dev : model.devices()) {
        const IedModel* im = model.modelFor(dev.iedName);
        if (!im)
            continue;
        for (const ReportEntry& rpt : im->reports) {
            if (ScdModelLoader::datasetMatchesPanel(rpt.datSet, ReportPanelType::Other))
                modelHasOtherRcb = true;
        }
    }
    for (const RcbDescriptor& d : model.allSubscribeRcbs()) {
        if (ScdModelLoader::datasetMatchesPanel(d.dsName, ReportPanelType::Other))
            subscribedOtherRcb = true;
    }
    if (modelHasOtherRcb && !subscribedOtherRcb)
        return false;

    {
        // Any event written by the report self-test is enough to exercise the
        // per-IED filter; take the last one instead of naming a fixed device.
        const QVector<StationEvent> all = store.snapshot();
        if (all.isEmpty())
            return false;
        const QString probeIed = all.last().iedName;
        const QVector<StationEvent> iedEvents = store.snapshotForIed(probeIed);
        if (iedEvents.isEmpty())
            return false;
        for (const StationEvent& e : iedEvents) {
            if (e.iedName != probeIed)
                return false;
        }
        if (!store.snapshotForIed(QStringLiteral("NOT_EXIST_DEV")).isEmpty())
            return false;
        if (!store.snapshotForIed(QString()).isEmpty())
            return false;
        if (StationEventCategory::datasetRead() != QStringLiteral("召唤")
            || StationEventCategory::datasetReadDone() != QStringLiteral("召唤完成"))
            return false;
        StationEvent readEv;
        readEv.time = QDateTime::currentDateTime();
        readEv.category = StationEventCategory::datasetRead();
        readEv.iedName = QStringLiteral("E1Q1SB41");
        readEv.iedDesc = QStringLiteral("test");
        readEv.pointName = QStringLiteral("召唤数据集");
        readEv.address = QStringLiteral("E1Q1SB41PROT/LLN0$dsDin1");
        const QString det = stationEventDetailText(readEv);
        if (!det.contains(StationEventCategory::datasetRead()) || det.contains(QStringLiteral("前值")))
            return false;
    }

    SharedConnectionSettings conn;
    if (conn.linkWatchSec() < 30 || conn.stationGiSec() < 60)
        return false;

    {
        // After 一键全流程 unload, leftover MMS reports must not touch freed IedModel.
        ScdModelLoader cleared;
        EventStore leftover;
        QHash<QString, QString> last;
        QJsonObject evt;
        evt.insert(QStringLiteral("evt"), QStringLiteral("report"));
        evt.insert(QStringLiteral("rcb"), QStringLiteral("TESTIEDLD0/LLN0$BR$brcbWarning01"));
        evt.insert(QStringLiteral("items"), QJsonArray());
        ingestMmsReportEvent(cleared, leftover, last, evt);
    }

    if (QFileInfo::exists(smcd)) {
        ScdModelLoader enrichLoader;
        enrichLoader.loadIedList(AppPaths::iedListPath());
        QVector<IedModel> parsed = ScdModelLoader::parseModelsFromSmcd(smcd, true);
        if (!parsed.isEmpty()) {
            enrichLoader.adoptParsedModels(std::move(parsed));
            QString probeIed;
            for (const IedDeviceEntry& d : enrichLoader.devices()) {
                if (enrichLoader.modelFor(d.iedName)) {
                    probeIed = d.iedName;
                    break;
                }
            }
            if (!probeIed.isEmpty())
                enrichLoader.enrichIedDoiDescFromSmcd(smcd, probeIed);
        }
        if (!ScdModelLoader::selfTestDoiEnrich(smcd))
            return false;
    }
    return true;
}

static int runMaintainUploadTest(int argc, char* argv[])
{
    QCoreApplication core(argc, argv);
    QString host = QStringLiteral("192.168.0.10");
    int port = 2405;
    bool waitGenconfig = false;
    QString outDir = AppPaths::outDir();
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--maintain-test"))
            continue;
        if (arg == QStringLiteral("--wait-genconfig")) {
            waitGenconfig = true;
            continue;
        }
        if (arg == QStringLiteral("--maintain-port") && i + 1 < argc) {
            port = QString::fromLocal8Bit(argv[++i]).toInt();
            continue;
        }
        if (arg == QStringLiteral("--out") && i + 1 < argc) {
            outDir = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        if (arg.startsWith(QLatin1Char('-')))
            continue;
        host = arg;
    }
    const auto log = [](const QString& msg) {
        fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
    };
    try {
        LabOps::runUpload(host, port, outDir, waitGenconfig, 102, log);
        fprintf(stderr, "maintain-test OK\n");
        return 0;
    } catch (const std::exception& ex) {
        fprintf(stderr, "maintain-test failed: %s\n", ex.what());
        return 1;
    }
}

static int runGenerateConfig(int argc, char* argv[])
{
    QCoreApplication core(argc, argv);
    QString scd = AppPaths::defaultScdPath();
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--generate-config") && i + 1 < argc)
            scd = QString::fromLocal8Bit(argv[++i]);
    }
    try {
        ConfigGenerator::generate(scd, AppPaths::outDir());
    } catch (const std::exception& ex) {
        fprintf(stderr, "generate failed: %s\n", ex.what());
        return 1;
    }
    return 0;
}

#include "ui/master_sim_shell.h"
#include "ui/mms_master_widget.h"

#include <QApplication>
#include <QMetaType>

#include <cstdio>
#include <stdexcept>

#include "ui/lab_worker.h"
#include "cli/headless_main.h"

int main(int argc, char* argv[])
{
    if (argc >= 2 && isHeadlessSubcommand(QString::fromLocal8Bit(argv[1])))
        return runHeadlessCli(argc, argv);

    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--verify")) {
            QCoreApplication core(argc, argv);
            return runVerifySelfTest() ? 0 : 1;
        }
        if (arg == QStringLiteral("--verify-doi")) {
            QCoreApplication core(argc, argv);
            const QString smcd = AppPaths::defaultSmcdPath();
            if (!QFileInfo::exists(smcd))
                return 1;
            return ScdModelLoader::selfTestDoiEnrich(smcd, 8000, 20000) ? 0 : 1;
        }
        if (arg == QStringLiteral("--verify-gui")) {
            QApplication app(argc, argv);
            QApplication::setApplicationName(QStringLiteral("免配置仿真主站"));
            qRegisterMetaType<StationEvent>("StationEvent");
            qRegisterMetaType<LabJobKind>("LabJobKind");
            SharedConnectionSettings settings;
            MmsMasterWidget w(&settings);
            return w.runPrepareGenerationSafetyTest() ? 0 : 1;
        }
        if (arg == QStringLiteral("--generate-config"))
            return runGenerateConfig(argc, argv);
        if (arg == QStringLiteral("--maintain-test"))
            return runMaintainUploadTest(argc, argv);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("免配置仿真主站"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION_DISPLAY));

    qRegisterMetaType<StationEvent>("StationEvent");
    qRegisterMetaType<LabJobKind>("LabJobKind");

    MasterSimShell w;
    w.show();
    return app.exec();
}
