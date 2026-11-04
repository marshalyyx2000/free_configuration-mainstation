#include "util/app_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace AppPaths {

namespace {

QString devMasterSimRoot()
{
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 12; ++i) {
        if (QFileInfo::exists(d.absoluteFilePath(QStringLiteral("gui.py"))))
            return d.absolutePath();
        if (QFileInfo::exists(d.absoluteFilePath(QStringLiteral("out/IEDList.xml"))))
            return d.absolutePath();
        if (!d.cdUp())
            break;
    }
    return QString();
}

QString bundledShareDir()
{
    QDir bin(QCoreApplication::applicationDirPath());
    const QString share = bin.absoluteFilePath(QStringLiteral("../share/sim_master_qt"));
    if (QDir(share).exists())
        return QFileInfo(share).absoluteFilePath();
    const QString dev = devMasterSimRoot();
    if (!dev.isEmpty())
        return QDir(dev).absoluteFilePath(QStringLiteral("out"));
    return share;
}

} // namespace

QString applicationRoot()
{
    return QCoreApplication::applicationDirPath();
}

QString writableDataRoot()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    base = QDir(base).absoluteFilePath(QStringLiteral("SimMasterQt"));
    QDir().mkpath(base);
    return base;
}

QString masterSimRoot()
{
    const QString dev = devMasterSimRoot();
    if (!dev.isEmpty())
        return dev;
    return writableDataRoot();
}

QString outDir()
{
    const QString out = QDir(writableDataRoot()).absoluteFilePath(QStringLiteral("out"));
    QDir().mkpath(out);
    return out;
}

QString iedListPath()
{
    const QString inOut = QDir(outDir()).absoluteFilePath(QStringLiteral("IEDList.xml"));
    if (QFileInfo::exists(inOut))
        return inOut;
    const QString share = QDir(bundledShareDir()).absoluteFilePath(QStringLiteral("IEDList.xml"));
    if (QFileInfo::exists(share))
        return share;
    return inOut;
}

QString defaultSmcdPath()
{
    const QString inOut = QDir(outDir()).absoluteFilePath(QStringLiteral("station.SMCD"));
    if (QFileInfo::exists(inOut))
        return inOut;
    const QString share = QDir(bundledShareDir()).absoluteFilePath(QStringLiteral("station.SMCD"));
    if (QFileInfo::exists(share))
        return share;
    const QString scd = defaultScdPath();
    if (!scd.isEmpty() && QFileInfo::exists(scd))
        return scd;
    return inOut;
}

QString defaultScdPath()
{
    QSettings settings(QStringLiteral("master_sim"), QStringLiteral("sim_master_qt"));
    const QString saved = settings.value(QStringLiteral("scdPath")).toString();
    if (!saved.isEmpty() && QFileInfo::exists(saved))
        return QFileInfo(saved).absoluteFilePath();

    const QString dev = devMasterSimRoot();
    if (!dev.isEmpty()) {
        const QString p = QDir(dev).absoluteFilePath(QStringLiteral("../子站scd/26579.SCD"));
        if (QFileInfo::exists(p))
            return QFileInfo(p).absoluteFilePath();
    }
    return QString();
}

} // namespace AppPaths
