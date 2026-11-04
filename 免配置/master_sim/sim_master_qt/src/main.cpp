#include "scd/scd_model.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

static QString masterSimRoot()
{
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (QFileInfo::exists(d.absoluteFilePath(QStringLiteral("out/IEDList.xml"))))
            return d.absolutePath();
        const QString shareList = d.absoluteFilePath(QStringLiteral("share/sim_master_qt/IEDList.xml"));
        if (QFileInfo::exists(shareList))
            return d.absolutePath();
        if (!d.cdUp())
            break;
    }
    return QCoreApplication::applicationDirPath();
}

static QString iedListPath()
{
    const QDir root(masterSimRoot());
    const QString inOut = root.absoluteFilePath(QStringLiteral("out/IEDList.xml"));
    if (QFileInfo::exists(inOut))
        return inOut;
    return root.absoluteFilePath(QStringLiteral("share/sim_master_qt/IEDList.xml"));
}

static QString defaultSmcdPath()
{
    const QDir root(masterSimRoot());
    const QString inOut = root.absoluteFilePath(QStringLiteral("out/station.SMCD"));
    if (QFileInfo::exists(inOut))
        return inOut;
    return root.absoluteFilePath(QStringLiteral("share/sim_master_qt/station.SMCD"));
}

static bool runVerifySelfTest()
{
    const QString root = masterSimRoot();
    ScdModelLoader model;
    if (!model.loadIedList(iedListPath()))
        return false;
    return model.devices().size() >= 1;
}

#include "ui/mainwindow.h"

#include <QApplication>
#include <QMetaType>

int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--verify")) {
            QCoreApplication core(argc, argv);
            return runVerifySelfTest() ? 0 : 1;
        }
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("61850仿真主站"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION_DISPLAY));

    qRegisterMetaType<StationEvent>("StationEvent");

    MainWindow w;
    w.show();
    return app.exec();
}
