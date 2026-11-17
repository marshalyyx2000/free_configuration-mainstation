#include "cli/mms_session_client.h"
#include "cli/cli_console.h"
#include "cli/cli_constants.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

bool sessionServerAlive()
{
    QLocalSocket sock;
    sock.connectToServer(QString::fromUtf8(CliConstants::kPipeName));
    return sock.waitForConnected(CliConstants::kClientConnectTimeoutMs);
}

int runSessionClient(const QJsonObject& cmd, int timeoutMs)
{
    if (timeoutMs < 0)
        timeoutMs = CliConstants::kClientReplyTimeoutMs;
    QLocalSocket sock;
    sock.connectToServer(QString::fromUtf8(CliConstants::kPipeName));
    if (!sock.waitForConnected(CliConstants::kClientConnectTimeoutMs)) {
        CliConsole::writeLine(QString::fromUtf8(
            QJsonDocument(QJsonObject{
                { QStringLiteral("ok"), false },
                { QStringLiteral("error"), QStringLiteral("no MMS session; run: sim_master_qt.exe connect") },
            })
                .toJson(QJsonDocument::Compact)));
        return 2;
    }
    sock.write(QJsonDocument(cmd).toJson(QJsonDocument::Compact) + '\n');
    sock.flush();
    if (!sock.waitForReadyRead(timeoutMs)) {
        CliConsole::writeLine(QString::fromUtf8(
            QJsonDocument(QJsonObject{
                { QStringLiteral("ok"), false },
                { QStringLiteral("error"), QStringLiteral("session reply timeout") },
            })
                .toJson(QJsonDocument::Compact)));
        return 3;
    }
    QByteArray buf = sock.readAll();
    while (!buf.contains('\n') && sock.waitForReadyRead(2000))
        buf += sock.readAll();
    const QList<QByteArray> lines = buf.split('\n');
    QJsonObject last;
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            last = doc.object();
        CliConsole::writeLine(QString::fromUtf8(line));
    }
    if (last.isEmpty())
        return 4;
    return last.value(QStringLiteral("ok")).toBool() ? 0 : 1;
}
