#include "scd/scd_model.h"
#include "util/app_paths.h"
#include "event/event_store.h"
#include "event/report_event_logic.h"
#include "ops/config_generator.h"
#include "ops/lab_operations.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

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
        const IedModel* im = model.modelFor(QStringLiteral("E1Q1SB20"));
        if (im && !im->desc.isEmpty() && im->desc != QStringLiteral("E1Q1SB20")) {
            bool labeled = false;
            for (const IedDeviceEntry& d : model.devices()) {
                if (d.iedName == QStringLiteral("E1Q1SB20") && d.desc == im->desc)
                    labeled = true;
            }
            if (!labeled)
                return false;
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
    return true;
}

static int runMaintainUploadTest(int argc, char* argv[])
{
    QCoreApplication core(argc, argv);
    QString host = QStringLiteral("192.168.0.10");
    int port = 2405;
    QString outDir = AppPaths::outDir();
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--maintain-test") && i + 1 < argc)
            host = QString::fromLocal8Bit(argv[++i]);
        else if (arg == QStringLiteral("--maintain-port") && i + 1 < argc)
            port = QString::fromLocal8Bit(argv[++i]).toInt();
        else if (arg == QStringLiteral("--out") && i + 1 < argc)
            outDir = QString::fromLocal8Bit(argv[++i]);
    }
    const auto log = [](const QString& msg) {
        fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
    };
    try {
        LabOps::runUpload(host, port, outDir, false, 102, log);
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

#include <QApplication>
#include <QMetaType>

#include <cstdio>
#include <stdexcept>

#include "ui/lab_worker.h"

int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--verify")) {
            QCoreApplication core(argc, argv);
            return runVerifySelfTest() ? 0 : 1;
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
