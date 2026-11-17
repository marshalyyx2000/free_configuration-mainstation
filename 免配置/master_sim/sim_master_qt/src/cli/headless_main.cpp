#include "cli/headless_main.h"
#include "cli/cli_console.h"
#include "cli/mms_session_client.h"
#include "cli/mms_session_server.h"
#include "ops/lab_operations.h"
#include "util/app_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStringList>

#include <cstdio>
#include <stdexcept>

namespace {

void printHelp()
{
    CliConsole::writeLine(QStringLiteral(
        "sim_master_qt.exe  命令行（无窗口）\n"
        "  deploy --scd <SCD> [--host IP] [--maintain-port 2405] [--mms-port 102] [--out DIR] [--wait-102|--no-wait-102]\n"
        "  connect [--host IP] [--mms-port 102] [--ied-list FILE] [--smcd FILE] [--prefer-smcd-fcda]\n"
        "  status\n"
        "  subscribe --all | --ied <iedName> | --rcb <handlerRef>\n"
        "  gi        --all | --ied <iedName> | --rcb <handlerRef>\n"
        "  disconnect\n"
        "connect 保持 MMS 关联；subscribe/gi 在另一终端发给该会话。"));
}

QString persistOutDir(const QString& outDir)
{
    QSettings s(QStringLiteral("master_sim"), QStringLiteral("sim_master_qt"));
    s.setValue(QStringLiteral("lastCliOutDir"), outDir);
    return outDir;
}

QString lastOutDir()
{
    QSettings s(QStringLiteral("master_sim"), QStringLiteral("sim_master_qt"));
    const QString saved = s.value(QStringLiteral("lastCliOutDir")).toString();
    if (!saved.isEmpty() && QDir(saved).exists())
        return saved;
    return AppPaths::outDir();
}

int runDeploy(const QStringList& args)
{
    QString scd;
    QString host = QStringLiteral("192.168.0.10");
    int maintainPort = 2405;
    int mmsPort = 102;
    QString outDir = AppPaths::outDir();
    bool wait102 = true;
    bool mmsCheck = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);
        auto next = [&]() -> QString {
            if (i + 1 >= args.size())
                return {};
            return args.at(++i);
        };
        if (a == QStringLiteral("--scd"))
            scd = next();
        else if (a == QStringLiteral("--host"))
            host = next();
        else if (a == QStringLiteral("--maintain-port"))
            maintainPort = next().toInt();
        else if (a == QStringLiteral("--mms-port"))
            mmsPort = next().toInt();
        else if (a == QStringLiteral("--out"))
            outDir = next();
        else if (a == QStringLiteral("--wait-102"))
            wait102 = true;
        else if (a == QStringLiteral("--no-wait-102"))
            wait102 = false;
        else if (a == QStringLiteral("--mms-check"))
            mmsCheck = true;
        else if (a == QStringLiteral("deploy"))
            continue;
        else if (a.startsWith(QLatin1Char('-'))) {
            CliConsole::writeLine(QStringLiteral("unknown option %1").arg(a));
            return 2;
        }
    }
    if (scd.isEmpty())
        scd = AppPaths::defaultScdPath();
    if (scd.isEmpty() || !QFileInfo::exists(scd)) {
        CliConsole::writeLine(QStringLiteral("{\"ok\":false,\"error\":\"missing --scd\"}"));
        return 2;
    }
    QDir().mkpath(outDir);
    persistOutDir(outDir);
    const auto log = [](const QString& msg) { CliConsole::writeLine(QStringLiteral("# ") + msg); };
    try {
        const int rc = LabOps::runFullPipeline(host, maintainPort, scd, outDir, mmsCheck, wait102, mmsPort, nullptr, log);
        QJsonObject result{
            { QStringLiteral("ok"), rc == 0 },
            { QStringLiteral("cmd"), QStringLiteral("deploy") },
            { QStringLiteral("scd"), scd },
            { QStringLiteral("out"), outDir },
        };
        CliConsole::writeLine(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
        return rc;
    } catch (const std::exception& ex) {
        CliConsole::writeLine(QStringLiteral("{\"ok\":false,\"error\":\"%1\"}").arg(QString::fromUtf8(ex.what())));
        return 1;
    }
}

int runConnect(const QStringList& args)
{
    MmsSessionServer::Options opt;
    const QString out = lastOutDir();
    opt.iedList = QDir(out).absoluteFilePath(QStringLiteral("IEDList.xml"));
    opt.smcd = QDir(out).absoluteFilePath(QStringLiteral("station.SMCD"));
    if (!QFileInfo::exists(opt.iedList))
        opt.iedList = AppPaths::iedListPath();
    if (!QFileInfo::exists(opt.smcd))
        opt.smcd = AppPaths::defaultSmcdPath();
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);
        auto next = [&]() -> QString {
            if (i + 1 >= args.size())
                return {};
            return args.at(++i);
        };
        if (a == QStringLiteral("--host"))
            opt.host = next();
        else if (a == QStringLiteral("--mms-port") || a == QStringLiteral("--port"))
            opt.mmsPort = next().toInt();
        else if (a == QStringLiteral("--ied-list"))
            opt.iedList = next();
        else if (a == QStringLiteral("--smcd"))
            opt.smcd = next();
        else if (a == QStringLiteral("--prefer-smcd-fcda"))
            opt.preferSmcdFcda = true;
        else if (a == QStringLiteral("connect"))
            continue;
    }
    MmsSessionServer server(opt);
    return server.execConnect();
}

QJsonObject scopeCommand(const QString& cmd, const QStringList& args)
{
    QJsonObject obj{ { QStringLiteral("cmd"), cmd } };
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == QStringLiteral("--all"))
            obj.insert(QStringLiteral("scope"), QStringLiteral("all"));
        else if (a == QStringLiteral("--ied") && i + 1 < args.size()) {
            obj.insert(QStringLiteral("scope"), QStringLiteral("ied"));
            obj.insert(QStringLiteral("ied"), args.at(++i));
        } else if (a == QStringLiteral("--rcb") && i + 1 < args.size()) {
            obj.insert(QStringLiteral("scope"), QStringLiteral("rcb"));
            obj.insert(QStringLiteral("rcb"), args.at(++i));
        }
    }
    if (!obj.contains(QStringLiteral("scope")))
        obj.insert(QStringLiteral("scope"), QStringLiteral("all"));
    return obj;
}

} // namespace

bool isHeadlessSubcommand(const QString& arg)
{
    return arg == QStringLiteral("deploy") || arg == QStringLiteral("connect")
        || arg == QStringLiteral("status") || arg == QStringLiteral("subscribe")
        || arg == QStringLiteral("gi") || arg == QStringLiteral("disconnect")
        || arg == QStringLiteral("help") || arg == QStringLiteral("--help")
        || arg == QStringLiteral("-h");
}

int runHeadlessCli(int argc, char* argv[])
{
    CliConsole::attach();
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("免配置仿真主站"));
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args.append(QString::fromLocal8Bit(argv[i]));
    if (args.isEmpty()) {
        printHelp();
        return 2;
    }
    const QString cmd = args.first();
    if (cmd == QStringLiteral("help") || cmd == QStringLiteral("--help") || cmd == QStringLiteral("-h")) {
        printHelp();
        return 0;
    }
    if (cmd == QStringLiteral("deploy"))
        return runDeploy(args);
    if (cmd == QStringLiteral("connect"))
        return runConnect(args);
    if (cmd == QStringLiteral("status"))
        return runSessionClient(QJsonObject{ { QStringLiteral("cmd"), QStringLiteral("status") } }, 10000);
    if (cmd == QStringLiteral("subscribe") || cmd == QStringLiteral("gi"))
        return runSessionClient(scopeCommand(cmd, args));
    if (cmd == QStringLiteral("disconnect"))
        return runSessionClient(QJsonObject{ { QStringLiteral("cmd"), QStringLiteral("disconnect") } }, 10000);
    printHelp();
    return 2;
}
