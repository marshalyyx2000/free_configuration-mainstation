#include "scd/scd_model.h"
#include "util/app_paths.h"
#include "event/event_store.h"
#include "ops/config_generator.h"

#include <QCoreApplication>
#include <QFileInfo>

static bool runVerifySelfTest()
{
    ScdModelLoader model;
    if (!model.loadIedList(AppPaths::iedListPath()))
        return false;
    return model.devices().size() >= 1;
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
