#include "mms/mms_client_bridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

MmsClientBridge::MmsClientBridge(QObject* parent)
    : QObject(parent)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    m_cliPath = appDir.absoluteFilePath(QStringLiteral("mms_master_cli.exe"));
    if (!QFileInfo::exists(m_cliPath)) {
        QDir d(appDir);
        for (int i = 0; i < 8; ++i) {
            const QString cand = d.absoluteFilePath(QStringLiteral("bin/mms_master_cli.exe"));
            if (QFileInfo::exists(cand)) {
                m_cliPath = cand;
                break;
            }
            if (!d.cdUp())
                break;
        }
    }
}

bool MmsClientBridge::available() const
{
    return QFileInfo::exists(m_cliPath);
}

void MmsClientBridge::startCli()
{
    if (m_proc.state() != QProcess::NotRunning)
        return;
    if (!available()) {
        emit logMessage(tr("未找到 mms_master_cli.exe: %1").arg(m_cliPath));
        return;
    }
    m_cliReady = false;
    m_connected = false;
    m_readBuffer.clear();
    m_proc.setProgram(m_cliPath);
    m_proc.setWorkingDirectory(QFileInfo(m_cliPath).absolutePath());
    m_proc.setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
    m_proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
        args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        args->startupInfo->wShowWindow = SW_HIDE;
    });
#endif
    if (!m_signalsHooked) {
        m_signalsHooked = true;
        connect(&m_proc, &QProcess::readyReadStandardOutput, this, &MmsClientBridge::onReadyRead);
        connect(&m_proc, &QProcess::readyReadStandardError, this, &MmsClientBridge::onReadyReadStderr);
        connect(&m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &MmsClientBridge::onProcessFinished);
    }
    m_proc.start();
    if (!m_proc.waitForStarted(5000)) {
        emit logMessage(tr("启动 CLI 失败: %1").arg(m_proc.errorString()));
        return;
    }
    emit logMessage(tr("已启动 MMS CLI: %1").arg(m_cliPath));
}

void MmsClientBridge::stopCli()
{
    m_pending.clear();
    m_cliReady = false;
    if (m_proc.state() == QProcess::NotRunning)
        return;
    sendCommand(QJsonObject{ { QStringLiteral("cmd"), QStringLiteral("quit") } });
    m_proc.waitForFinished(3000);
    if (m_proc.state() != QProcess::NotRunning)
        m_proc.kill();
    m_connected = false;
}

void MmsClientBridge::sendCommand(const QJsonObject& cmd)
{
    if (m_proc.state() == QProcess::NotRunning) {
        emit logMessage(tr("CLI 未运行，无法发送 %1").arg(cmd.value(QStringLiteral("cmd")).toString()));
        return;
    }
    if (!m_cliReady) {
        m_pending.append(cmd);
        return;
    }
    const QByteArray line = QJsonDocument(cmd).toJson(QJsonDocument::Compact) + '\n';
    if (m_proc.write(line) < 0) {
        emit logMessage(tr("向 CLI 写入失败: %1").arg(m_proc.errorString()));
        return;
    }
    m_proc.waitForBytesWritten(3000);
}

void MmsClientBridge::flushPending()
{
    if (!m_cliReady || m_proc.state() == QProcess::NotRunning)
        return;
    const QVector<QJsonObject> queued = m_pending;
    m_pending.clear();
    for (const QJsonObject& cmd : queued)
        sendCommand(cmd);
}

void MmsClientBridge::connectHost(const QString& host, int port)
{
    startCli();
    emit logMessage(tr("正在连接 SubStation %1:%2 …").arg(host).arg(port));
    sendCommand(QJsonObject{
        { QStringLiteral("cmd"), QStringLiteral("connect") },
        { QStringLiteral("host"), host },
        { QStringLiteral("port"), port },
    });
    flushPending();
}

void MmsClientBridge::disconnectHost()
{
    sendCommand(QJsonObject{ { QStringLiteral("cmd"), QStringLiteral("disconnect") } });
    m_connected = false;
}

void MmsClientBridge::readDataSet(const QString& dsRef, const QString& requestId, const QStringList& items)
{
    QJsonObject cmd{
        { QStringLiteral("cmd"), QStringLiteral("read_dataset") },
        { QStringLiteral("ref"), dsRef },
        { QStringLiteral("req_id"), requestId },
    };
    if (!items.isEmpty()) {
        QJsonArray arr;
        for (const QString& item : items)
            arr.append(item);
        cmd.insert(QStringLiteral("items"), arr);
    }
    sendCommand(cmd);
}

void MmsClientBridge::enableRcb(const QJsonObject& params)
{
    QJsonObject cmd = params;
    cmd.insert(QStringLiteral("cmd"), QStringLiteral("enable_rcb"));
    sendCommand(cmd);
}

void MmsClientBridge::disableRcb(const QString& handlerRef)
{
    sendCommand(QJsonObject{
        { QStringLiteral("cmd"), QStringLiteral("disable_rcb") },
        { QStringLiteral("handlerRef"), handlerRef },
    });
}

void MmsClientBridge::triggerGi(const QString& handlerRef)
{
    sendCommand(QJsonObject{
        { QStringLiteral("cmd"), QStringLiteral("gi") },
        { QStringLiteral("handlerRef"), handlerRef },
    });
}

void MmsClientBridge::readCommState()
{
    sendCommand(QJsonObject{ { QStringLiteral("cmd"), QStringLiteral("read_comm_state") } });
}

void MmsClientBridge::listFiles(const QString& path, const QString& requestId)
{
    sendCommand(QJsonObject{
        { QStringLiteral("cmd"), QStringLiteral("list_files") },
        { QStringLiteral("path"), path },
        { QStringLiteral("req_id"), requestId },
    });
}

void MmsClientBridge::ping()
{
    sendCommand(QJsonObject{ { QStringLiteral("cmd"), QStringLiteral("ping") } });
}

void MmsClientBridge::onReadyRead()
{
    m_readBuffer.append(m_proc.readAllStandardOutput());
    int idx = 0;
    while ((idx = m_readBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_readBuffer.left(idx).trimmed();
        m_readBuffer.remove(0, idx + 1);
        if (!line.isEmpty())
            handleLine(line);
    }
}

void MmsClientBridge::onReadyReadStderr()
{
    const QByteArray err = m_proc.readAllStandardError().trimmed();
    if (!err.isEmpty())
        emit logMessage(QString::fromLocal8Bit(err));
}

void MmsClientBridge::handleLine(const QByteArray& line)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit logMessage(tr("CLI 非 JSON: %1").arg(QString::fromUtf8(line)));
        return;
    }
    const QJsonObject obj = doc.object();
    const QString evt = obj.value(QStringLiteral("evt")).toString();
    if (evt == QStringLiteral("ready")) {
        m_cliReady = true;
        flushPending();
        emit eventReceived(obj);
        return;
    }
    if (evt == QStringLiteral("connected"))
        m_connected = true;
    else if (evt == QStringLiteral("disconnected"))
        m_connected = false;
    else if (evt == QStringLiteral("error")) {
        const QString msg = obj.value(QStringLiteral("msg")).toString();
        if (msg == QStringLiteral("CONNECTION_LOST") || msg == QStringLiteral("NOT_CONNECTED")
            || msg == QStringLiteral("not connected")) {
            m_connected = false;
        }
    }
    emit eventReceived(obj);
    if (evt == QStringLiteral("connected"))
        readCommState();
}

void MmsClientBridge::onProcessFinished(int code, QProcess::ExitStatus status)
{
    Q_UNUSED(status);
    m_connected = false;
    m_cliReady = false;
    emit logMessage(tr("CLI 已退出，code=%1").arg(code));
    emit eventReceived(QJsonObject{
        { QStringLiteral("evt"), QStringLiteral("disconnected") },
        { QStringLiteral("reason"), QStringLiteral("CLI_EXIT") },
    });
}
