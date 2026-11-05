#include "ops/lab_operations.h"
#include "ops/config_generator.h"
#include "ops/iec104_maintain_client.h"
#include "mms/mms_client_bridge.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <optional>
#include <stdexcept>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace LabOps {

namespace {

void noopLog(const QString&) {}

class StepTimer {
public:
    explicit StepTimer(const LabLogFn& log)
        : m_log(log ? log : noopLog)
    {
        m_t0.start();
    }

    template <typename Fn>
    auto run(const QString& name, Fn fn) -> decltype(fn())
    {
        m_log(QStringLiteral("▶ %1 …").arg(name));
        QElapsedTimer t;
        t.start();
        auto result = fn();
        const double dt = t.elapsed() / 1000.0;
        m_steps.append({ name, dt });
        m_log(QStringLiteral("✔ %1 完成，耗时 %2").arg(name, formatDuration(dt)));
        return result;
    }

    void add(const QString& name, double seconds)
    {
        m_steps.append({ name, seconds });
        m_log(QStringLiteral("✔ %1 完成，耗时 %2").arg(name, formatDuration(seconds)));
    }

    void summary(const QString& title)
    {
        const double total = m_t0.elapsed() / 1000.0;
        m_log(QStringLiteral("—— %1 ——").arg(title));
        for (const auto& s : m_steps)
            m_log(QStringLiteral("  · %1: %2").arg(s.first, formatDuration(s.second)));
        m_log(QStringLiteral("  · 合计: %1").arg(formatDuration(total)));
    }

private:
    LabLogFn m_log;
    QElapsedTimer m_t0;
    QVector<QPair<QString, double>> m_steps;
};

std::optional<double> waitForMmsPort(const QString& host, int port, const QString& outDir, const LabLogFn& log)
{
    const double timeoutSec = 20 * 60;
    const double pollSec = 5.0;
    const QString model = formatMb(modelSizeBytes(outDir));
    const bool alreadyUp = tcpReachable(host, port, 2000);
    if (alreadyUp) {
        log(QStringLiteral(
            "注意: %1:%2 此刻已通，多半是旧 router 未退出；先等 102 关闭后再计 genconfig/装载（模型 %3）…")
                .arg(host)
                .arg(port)
                .arg(model));
        const qint64 dropDeadline = QDateTime::currentMSecsSinceEpoch() + 90000;
        while (QDateTime::currentMSecsSinceEpoch() < dropDeadline && tcpReachable(host, port, 1500))
            QThread::msleep(1000);
        if (tcpReachable(host, port, 1500)) {
            log(QStringLiteral(
                "102 一直未关闭，本次「装置耗时」会接近 0，不能代表 genconfig。"
                "请先在装置上删除 supervision.cfg/.stamp 并 kill iec61850router 后再测。"));
            return 0.0;
        }
        log(QStringLiteral("102 已关闭，开始计时等待重新生成/装载…"));
    }

    QElapsedTimer t;
    t.start();
    const qint64 deadline = t.elapsed() + qint64(timeoutSec * 1000);
    int lastReport = -1;
    log(QStringLiteral("等待装置 genconfig/装载完成（模型 %1，预期 %2）…").arg(model, QString::fromUtf8(GENCONFIG_HINT_TEXT)));
    while (t.elapsed() < qint64(timeoutSec * 1000)) {
        const double elapsed = t.elapsed() / 1000.0;
        if (tcpReachable(host, port, 2000)) {
            log(QStringLiteral("MMS %1:%2 已监听，装置侧耗时 %3").arg(host).arg(port).arg(formatDuration(elapsed)));
            return elapsed;
        }
        const int bucket = int(elapsed / 15);
        if (bucket != lastReport) {
            lastReport = bucket;
            log(QStringLiteral("  …仍在生成/装载模型，已等待 %1").arg(formatDuration(elapsed)));
        }
        QThread::msleep(int(pollSec * 1000));
        Q_UNUSED(deadline);
    }
    log(QStringLiteral("超时：%1 秒内 %2:%3 仍未监听").arg(int(timeoutSec)).arg(host).arg(port));
    return std::nullopt;
}

} // namespace

bool tcpReachable(const QString& host, int port, int timeoutMs)
{
    QTcpSocket sock;
    sock.connectToHost(host, static_cast<quint16>(port));
    return sock.waitForConnected(timeoutMs);
}

qint64 modelSizeBytes(const QString& outDir)
{
    const QStringList names = { QStringLiteral("station.SMCD"), QStringLiteral("station.zip") };
    for (const QString& name : names) {
        const QString path = QDir(outDir).absoluteFilePath(name);
        if (QFileInfo::exists(path))
            return QFileInfo(path).size();
    }
    return 0;
}

QString formatMb(qint64 numBytes)
{
    if (numBytes <= 0)
        return QStringLiteral("—");
    return QStringLiteral("%1 MB").arg(numBytes / (1024.0 * 1024.0), 0, 'f', 1);
}

QString formatDuration(double seconds)
{
    if (seconds < 0)
        seconds = 0;
    if (seconds < 10)
        return QStringLiteral("%1 秒").arg(seconds, 0, 'f', 1);
    const int secondsI = int(std::round(seconds));
    const int m = secondsI / 60;
    const int s = secondsI % 60;
    if (m <= 0)
        return QStringLiteral("%1 秒").arg(s);
    return QStringLiteral("%1 分 %2").arg(m).arg(s, 2, 10, QChar('0'));
}

int runGenerate(const QString& scdPath, const QString& outDir, const LabLogFn& log)
{
    LabLogFn lg = log ? log : noopLog;
    StepTimer timer(lg);
    lg(QStringLiteral("从 SCD 生成配置: %1").arg(scdPath));

    const auto devices = timer.run(QStringLiteral("生成本地配置(IEDList/SMCD/zip)"),
        [&]() { return ConfigGenerator::generate(scdPath, outDir); });

    const qint64 size = modelSizeBytes(outDir);
    lg(QStringLiteral("完成: %1 台装置 -> %2").arg(devices.size()).arg(outDir));
    lg(QStringLiteral("站级模型: %1（station.SMCD）；装置 genconfig.jar 预期 %2")
           .arg(formatMb(size), QString::fromUtf8(GENCONFIG_HINT_TEXT)));
    timer.summary(QStringLiteral("生成配置耗时"));
    return devices.size();
}

std::optional<double> runUpload(const QString& host, int maintainPort, const QString& outDir, bool waitGenconfig,
    int mmsPort, const LabLogFn& log)
{
    LabLogFn lg = log ? log : noopLog;
    const QString iedPath = QDir(outDir).absoluteFilePath(QStringLiteral("IEDList.xml"));
    const QString zipPath = QDir(outDir).absoluteFilePath(QStringLiteral("station.zip"));
    if (!QFileInfo::exists(iedPath) || !QFileInfo::exists(zipPath))
        throw std::runtime_error("缺少 IEDList 或 station.zip，请先生成配置");

    QFile iedFile(iedPath);
    iedFile.open(QIODevice::ReadOnly);
    const QByteArray ied = iedFile.readAll();
    iedFile.close();
    QFile zipFile(zipPath);
    zipFile.open(QIODevice::ReadOnly);
    const QByteArray zipp = zipFile.readAll();
    zipFile.close();

    const QString modelMb = formatMb(modelSizeBytes(outDir));
    StepTimer timer(lg);

    Maintain104Config cfg;
    cfg.host = host;
    cfg.port = maintainPort;

    Iec104MaintainClient* client = timer.run(QStringLiteral("连接运维口 2405"), [&]() {
        lg(QStringLiteral("连接运维口 %1:%2 ...").arg(host).arg(maintainPort));
        if (!tcpReachable(host, maintainPort))
            lg(QStringLiteral("警告: %1:%2 TCP 暂不可达，仍将尝试 104 握手").arg(host).arg(maintainPort));
        auto* c = new Iec104MaintainClient(cfg);
        c->connectToHost();
        return c;
    });

    try {
        timer.run(QStringLiteral("下装 IEDList.xml"),
            [&]() {
                client->downloadFile(QStringLiteral("/CONF/IEDList.xml"), ied, 1);
                return 0;
            });
        timer.run(QStringLiteral("下装 station.zip（模型 %1）").arg(modelMb),
            [&]() {
                client->downloadFile(QStringLiteral("/SMCD/station.zip"), zipp, 2);
                return 0;
            });
        timer.run(QStringLiteral("配置生效 ASDU0xCA INF=2"), [&]() {
            client->effect61850();
            return 0;
        });
        lg(QStringLiteral("下装与生效完成（COT=0x14）"));
        lg(QStringLiteral("装置将跑 genconfig.jar（模型约 %1），预期 %2 后 102 才监听")
               .arg(modelMb, QString::fromUtf8(GENCONFIG_HINT_TEXT)));
    } catch (...) {
        client->close();
        delete client;
        throw;
    }
    client->close();
    delete client;

    std::optional<double> elapsed;
    if (waitGenconfig) {
        QElapsedTimer tw;
        tw.start();
        elapsed = waitForMmsPort(host, mmsPort, outDir, lg);
        if (elapsed.has_value())
            timer.add(QStringLiteral("装置 genconfig.jar+装载（至 102，模型 %1）").arg(modelMb), *elapsed);
        else
            timer.add(QStringLiteral("装置 genconfig.jar+装载（超时未开 102）"), tw.elapsed() / 1000.0);
    }

    timer.summary(QStringLiteral("下装/生效/genconfig 耗时"));
    return elapsed;
}

int runMmsCheck(const QString& host, int mmsPort, MmsClientBridge* bridge, const LabLogFn& log)
{
    Q_UNUSED(bridge);
    LabLogFn lg = log ? log : noopLog;
    StepTimer timer(lg);

    const int rc = timer.run(QStringLiteral("检测 MMS(102)"), [&]() {
        if (!tcpReachable(host, mmsPort)) {
            lg(QStringLiteral("MMS 端口 %1:%2 不可达（router 可能仍在 genconfig）").arg(host).arg(mmsPort));
            return 1;
        }
        lg(QStringLiteral("MMS 端口 %1:%2 可达").arg(host).arg(mmsPort));

        QString cliPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("mms_master_cli.exe"));
        if (!QFileInfo::exists(cliPath)) {
            cliPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../bin/mms_master_cli.exe"));
        }
        if (!QFileInfo::exists(cliPath)) {
            lg(QStringLiteral("未找到 mms_master_cli，仅完成 TCP 探测"));
            return 0;
        }

        QProcess proc;
        proc.setProgram(cliPath);
        proc.setWorkingDirectory(QFileInfo(cliPath).absolutePath());
        proc.setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
        proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
            args->flags |= CREATE_NO_WINDOW;
            args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
            args->startupInfo->wShowWindow = SW_HIDE;
        });
#endif
        proc.start();
        if (!proc.waitForStarted(5000)) {
            lg(QStringLiteral("无法启动 mms_master_cli"));
            return 1;
        }

        const QJsonObject cmd{
            { QStringLiteral("cmd"), QStringLiteral("connect") },
            { QStringLiteral("host"), host },
            { QStringLiteral("port"), mmsPort },
        };
        proc.write(QJsonDocument(cmd).toJson(QJsonDocument::Compact) + '\n');

        bool connected = false;
        bool failed = false;
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 30000;
        while (QDateTime::currentMSecsSinceEpoch() < deadline && !connected && !failed) {
            if (!proc.waitForReadyRead(500))
                continue;
            const QByteArray chunk = proc.readAllStandardOutput();
            const QList<QByteArray> lines = chunk.split('\n');
            for (QByteArray line : lines) {
                line = line.trimmed();
                if (line.isEmpty())
                    continue;
                QJsonParseError err;
                const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
                if (err.error != QJsonParseError::NoError || !doc.isObject())
                    continue;
                const QJsonObject obj = doc.object();
                const QString evt = obj.value(QStringLiteral("evt")).toString();
                if (evt == QStringLiteral("connected")) {
                    connected = true;
                    lg(QStringLiteral("MMS 已连接 SubStation"));
                } else if (evt == QStringLiteral("error")) {
                    failed = true;
                    lg(QStringLiteral("MMS 握手失败: %1").arg(obj.value(QStringLiteral("msg")).toString()));
                }
            }
        }

        proc.write(QJsonDocument(QJsonObject{ { QStringLiteral("cmd"), QStringLiteral("quit") } })
                       .toJson(QJsonDocument::Compact)
                + '\n');
        proc.waitForFinished(3000);
        if (proc.state() != QProcess::NotRunning)
            proc.kill();

        if (failed)
            return 1;
        if (!connected) {
            lg(QStringLiteral("MMS 连接超时"));
            return 1;
        }
        return 0;
    });

    timer.summary(QStringLiteral("MMS 检测耗时"));
    return rc;
}

int runFullPipeline(const QString& host, int maintainPort, const QString& scdPath, const QString& outDir,
    bool mmsCheck, bool waitGenconfig, int mmsPort, MmsClientBridge* bridge, const LabLogFn& log)
{
    LabLogFn lg = log ? log : noopLog;
    QElapsedTimer total;
    total.start();
    runGenerate(scdPath, outDir, lg);
    runUpload(host, maintainPort, outDir, waitGenconfig, mmsPort, lg);
    int rc = 0;
    if (mmsCheck)
        rc = runMmsCheck(host, mmsPort, bridge, lg);
    lg(QStringLiteral("—— 一键全流程合计: %1 ——").arg(formatDuration(total.elapsed() / 1000.0)));
    return rc;
}

} // namespace LabOps
