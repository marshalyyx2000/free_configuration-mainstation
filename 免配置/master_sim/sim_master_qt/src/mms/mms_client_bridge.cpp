#include "mms/mms_client_bridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

MmsClientBridge::MmsClientBridge(QObject* parent)
    : QObject(parent)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    m_cliPath = appDir.absoluteFilePath(QStringLiteral("mms_master_cli.exe"));
    if (!QFileInfo::exists(m_cliPath))
        m_cliPath = appDir.absoluteFilePath(QStringLiteral("../bin/mms_master_cli.exe"));
    if (!QFileInfo::exists(m_cliPath)) {
        QDir root(appDir);
        root.cdUp();
        m_cliPath = root.absoluteFilePath(QStringLiteral("bin/mms_master_cli.exe"));
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
    m_proc.setProgram(m_cliPath);
    m_proc.setWorkingDirectory(QFileInfo(m_cliPath).absolutePath());
    m_proc.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_proc, &QProcess::readyReadStandardOutput, this, &MmsClientBridge::onReadyRead);
    connect(&m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        &MmsClientBridge::onProcessFinished);
    m_proc.start();
    if (!m_proc.waitForStarted(5000))
        emit logMessage(tr("启动 CLI 失败"));
}

void MmsClientBridge::stopCli()
{
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
        emit logMessage(tr("CLI 未运行"));
        return;
    }
    const QByteArray line = QJsonDocument(cmd).toJson(QJsonDocument::Compact) + '\n';
    m_proc.write(line);
}

void MmsClientBridge::connectHost(const QString& host, int port)
{
    startCli();
    sendCommand(QJsonObject{
        { QStringLiteral("cmd"), QStringLiteral("connect") },
        { QStringLiteral("host"), host },
        { QStringLiteral("port"), port },
    });
}

void MmsClientBridge::disconnectHost()
{
    sendCommand(QJsonObject{ { QStringLiteral("cmd"), QStringLiteral("disconnect") } });
    m_connected = false;
}

void MmsClientBridge::readDataSet(const QString& dsRef, const QString& requestId)
{
    sendCommand(QJsonObject{
        { QStringLiteral("cmd"), QStringLiteral("read_dataset") },
        { QStringLiteral("ref"), dsRef },
        { QStringLiteral("req_id"), requestId },
    });
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

void MmsClientBridge::handleLine(const QByteArray& line)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject obj = doc.object();
    const QString evt = obj.value(QStringLiteral("evt")).toString();
    if (evt == QStringLiteral("connected"))
        m_connected = true;
    else if (evt == QStringLiteral("disconnected"))
        m_connected = false;
    emit eventReceived(obj);
    if (evt == QStringLiteral("connected"))
        readCommState();
}

void MmsClientBridge::onProcessFinished(int code, QProcess::ExitStatus status)
{
    Q_UNUSED(code);
    Q_UNUSED(status);
    m_connected = false;
    emit logMessage(tr("CLI 已退出"));
}
