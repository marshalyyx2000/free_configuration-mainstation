#include "mms/comm_state_support.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace CommStateSupport {

QString resolveStationSmcdForCli()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString dataOut = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                                .absoluteFilePath(QStringLiteral("SimMasterQt/out/station.SMCD"));
    const QStringList candidates = {
        dataOut,
        appDir.absoluteFilePath(QStringLiteral("../share/sim_master_qt/station.SMCD")),
        appDir.absoluteFilePath(QStringLiteral("../share/station.SMCD")),
        appDir.absoluteFilePath(QStringLiteral("../../out/station.SMCD")),
        appDir.absoluteFilePath(QStringLiteral("../out/station.SMCD")),
    };
    QString smcd;
    for (const QString& cand : candidates) {
        if (QFileInfo::exists(cand)) {
            smcd = cand;
            break;
        }
    }
    if (smcd.isEmpty())
        return {};

    const QString dst = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QLatin1Char('/') + QLatin1String(kTempSmcdFileName);
    if (!QFileInfo::exists(dst) || QFileInfo(smcd).lastModified() > QFileInfo(dst).lastModified()) {
        QFile::remove(dst);
        QFile::copy(smcd, dst);
    }
    return QFileInfo::exists(dst) ? dst : smcd;
}

QJsonObject buildReadCommStateCommand(int timeoutMs, bool iedDataSetOnly, bool preferSmcdFcda)
{
    QJsonObject cmd{
        { QStringLiteral("cmd"), QStringLiteral("read_comm_state") },
        { QStringLiteral("ref"), QString::fromUtf8(kDsCommStateRef) },
        { QStringLiteral("timeoutMs"), timeoutMs },
    };
    const QString smcd = resolveStationSmcdForCli();
    if (!smcd.isEmpty())
        cmd.insert(QStringLiteral("smcd"), smcd);
    if (iedDataSetOnly)
        cmd.insert(QStringLiteral("iedDataSetOnly"), 1);
    if (preferSmcdFcda)
        cmd.insert(QStringLiteral("preferSmcdFcda"), true);
    return cmd;
}

} // namespace CommStateSupport
