#include "scd/scd_model.h"
#include "util/app_paths.h"
#include "event/event_store.h"
#include "ops/config_generator.h"
#include "ops/lab_operations.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

static bool runVerifySelfTest()
{
    ScdModelLoader model;
    if (!model.loadIedList(AppPaths::iedListPath()))
        return false;
    return model.devices().size() >= 1;
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
