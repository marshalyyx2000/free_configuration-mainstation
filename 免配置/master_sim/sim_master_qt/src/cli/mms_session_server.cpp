#include "cli/mms_session_server.h"
#include "cli/cli_console.h"
#include "cli/cli_constants.h"
#include "mms/comm_state_support.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QThread>
#include <QTimer>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

QString jsonLine(const QJsonObject& obj)
{
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // namespace

MmsSessionServer::MmsSessionServer(const Options& opt, QObject* parent)
    : QObject(parent)
    , m_opt(opt)
{
    connect(&m_mms, &MmsClientBridge::eventReceived, this, &MmsSessionServer::onMmsEvent);
    connect(&m_mms, &MmsClientBridge::logMessage, this, &MmsSessionServer::onMmsLog);
    connect(&m_server, &QLocalServer::newConnection, this, &MmsSessionServer::onNewConnection);
}

int MmsSessionServer::execConnect()
{
    if (!QFileInfo::exists(m_opt.iedList)) {
        CliConsole::writeLine(jsonLine({{ QStringLiteral("ok"), false },
            { QStringLiteral("error"), QStringLiteral("missing IEDList: %1").arg(m_opt.iedList) }}));
        return 2;
    }
    m_model.loadIedList(m_opt.iedList);
    if (QFileInfo::exists(m_opt.smcd))
        m_model.loadSmcd(m_opt.smcd);

    QLocalSocket probe;
    probe.connectToServer(QString::fromUtf8(CliConstants::kPipeName));
    if (probe.waitForConnected(400)) {
        CliConsole::writeLine(jsonLine({{ QStringLiteral("ok"), false },
            { QStringLiteral("error"), QStringLiteral("MMS session already running") }}));
        return 3;
    }
    QLocalServer::removeServer(QString::fromUtf8(CliConstants::kPipeName));
    if (!m_server.listen(QString::fromUtf8(CliConstants::kPipeName))) {
        CliConsole::writeLine(jsonLine({{ QStringLiteral("ok"), false },
            { QStringLiteral("error"), QStringLiteral("listen failed: %1").arg(m_server.errorString()) }}));
        return 4;
    }

    m_mms.setAutoReadCommState(false);
    m_mms.setPreferSmcdFcda(m_opt.preferSmcdFcda);
    if (!m_mms.available()) {
        CliConsole::writeLine(jsonLine({{ QStringLiteral("ok"), false },
            { QStringLiteral("error"), QStringLiteral("mms_master_cli.exe not found") }}));
        return 5;
    }
    m_mms.connectHost(m_opt.host, m_opt.mmsPort);
    if (!waitConnected(120000)) {
        CliConsole::writeLine(jsonLine({{ QStringLiteral("ok"), false },
            { QStringLiteral("error"), QStringLiteral("connect timeout") }}));
        m_server.close();
        return 6;
    }

    if (m_opt.preferSmcdFcda) {
        QTimer::singleShot(CommStateSupport::kPostConnectDelayMs, this, [this]() {
            if (m_mms.connected())
                m_mms.readCommState(true);
        });
    }

    QJsonObject hello{
        { QStringLiteral("ok"), true },
        { QStringLiteral("evt"), QStringLiteral("connected") },
        { QStringLiteral("host"), m_opt.host },
        { QStringLiteral("port"), m_opt.mmsPort },
        { QStringLiteral("devices"), m_model.devices().size() },
        { QStringLiteral("rcbs"), m_model.allSubscribeRcbs().size() },
        { QStringLiteral("commEntries"), m_commState.size() },
        { QStringLiteral("pipe"), QString::fromUtf8(CliConstants::kPipeName) },
    };
    CliConsole::writeLine(jsonLine(hello));
    CliConsole::writeLine(QStringLiteral("# session ready; other terminals: subscribe / gi / status / disconnect"));

#ifdef Q_OS_WIN
    SetConsoleCtrlHandler([](DWORD) -> BOOL {
        QCoreApplication::quit();
        return TRUE;
    }, TRUE);
#endif

    const int rc = QCoreApplication::exec();
    m_mms.disconnectHost();
    m_mms.stopCli();
    m_server.close();
    QLocalServer::removeServer(QString::fromUtf8(CliConstants::kPipeName));
    return m_quitRequested ? 0 : rc;
}

void MmsSessionServer::onNewConnection()
{
    while (QLocalSocket* sock = m_server.nextPendingConnection()) {
        sock->setParent(this);
        connect(sock, &QLocalSocket::readyRead, this, [this, sock]() {
            while (sock->canReadLine()) {
                const QByteArray line = sock->readLine().trimmed();
                if (line.isEmpty())
                    continue;
                QJsonParseError err;
                const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
                QJsonObject replyObj;
                if (err.error != QJsonParseError::NoError || !doc.isObject())
                    replyObj = {{ QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("bad json") }};
                else
                    replyObj = handleCommand(doc.object());
                reply(sock, replyObj);
                if (m_quitRequested)
                    QTimer::singleShot(50, qApp, &QCoreApplication::quit);
            }
        });
        connect(sock, &QLocalSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void MmsSessionServer::reply(QLocalSocket* sock, const QJsonObject& obj)
{
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
    sock->write(payload);
    sock->flush();
    CliConsole::writeLine(QString::fromUtf8(payload.trimmed()));
}

QJsonObject MmsSessionServer::handleCommand(const QJsonObject& cmd)
{
    const QString c = cmd.value(QStringLiteral("cmd")).toString();
    if (c == QStringLiteral("status"))
        return cmdStatus();
    if (c == QStringLiteral("subscribe"))
        return cmdSubscribe(cmd);
    if (c == QStringLiteral("gi"))
        return cmdGi(cmd);
    if (c == QStringLiteral("disconnect"))
        return cmdDisconnect();
    return {{ QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("unknown cmd") }};
}

QJsonObject MmsSessionServer::cmdStatus() const
{
    int on = 0;
    for (auto it = m_commState.constBegin(); it != m_commState.constEnd(); ++it) {
        if (it.value())
            ++on;
    }
    return {
        { QStringLiteral("ok"), true },
        { QStringLiteral("connected"), m_mms.connected() },
        { QStringLiteral("host"), m_opt.host },
        { QStringLiteral("port"), m_opt.mmsPort },
        { QStringLiteral("devices"), m_model.devices().size() },
        { QStringLiteral("subscribed"), m_subscribed.size() },
        { QStringLiteral("commOn"), on },
        { QStringLiteral("commAligned"), m_commStateAligned },
    };
}

QVector<RcbDescriptor> MmsSessionServer::resolveTargets(const QString& scope, const QString& ied, const QString& rcb,
    QString* err) const
{
    const QVector<RcbDescriptor> all = m_model.allSubscribeRcbs();
    QVector<RcbDescriptor> out;
    if (scope == QStringLiteral("all")) {
        out = all;
    } else if (scope == QStringLiteral("ied")) {
        if (ied.isEmpty()) {
            if (err)
                *err = QStringLiteral("missing ied");
            return {};
        }
        for (const RcbDescriptor& d : all) {
            if (d.iedName.compare(ied, Qt::CaseInsensitive) == 0)
                out.append(d);
        }
        if (out.isEmpty() && err)
            *err = QStringLiteral("no RCB for ied %1").arg(ied);
    } else if (scope == QStringLiteral("rcb")) {
        if (rcb.isEmpty()) {
            if (err)
                *err = QStringLiteral("missing rcb");
            return {};
        }
        for (const RcbDescriptor& d : all) {
            if (d.handlerRef.compare(rcb, Qt::CaseInsensitive) == 0
                || d.getRef.compare(rcb, Qt::CaseInsensitive) == 0
                || d.rcbName.compare(rcb, Qt::CaseInsensitive) == 0)
                out.append(d);
        }
        if (out.isEmpty() && err)
            *err = QStringLiteral("no matching rcb %1").arg(rcb);
    } else if (err) {
        *err = QStringLiteral("scope must be all|ied|rcb");
    }
    return out;
}

QJsonObject MmsSessionServer::cmdSubscribe(const QJsonObject& cmd)
{
    if (!m_mms.connected())
        return {{ QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("not connected") }};
    QString err;
    const QVector<RcbDescriptor> list = resolveTargets(cmd.value(QStringLiteral("scope")).toString(),
        cmd.value(QStringLiteral("ied")).toString(), cmd.value(QStringLiteral("rcb")).toString(), &err);
    if (list.isEmpty())
        return {{ QStringLiteral("ok"), false }, { QStringLiteral("error"), err }};

    int enabled = 0;
    int failed = 0;
    int skipped = 0;
    QStringList errors;
    for (const RcbDescriptor& d : list) {
        if (shouldSkipOffline(d.iedName)) {
            ++skipped;
            continue;
        }
        if (m_subscribed.contains(d.handlerRef)) {
            ++enabled;
            continue;
        }
        QString oneErr;
        if (enableOne(d, &oneErr)) {
            ++enabled;
            m_subscribed.insert(d.handlerRef);
            m_subByHandler.insert(d.handlerRef, d);
        } else {
            ++failed;
            if (errors.size() < 8)
                errors.append(d.handlerRef + QLatin1Char(' ') + oneErr);
        }
        if (!m_mms.connected())
            break;
        if (list.size() > 200)
            QThread::msleep(20);
    }
    return {
        { QStringLiteral("ok"), enabled > 0 },
        { QStringLiteral("cmd"), QStringLiteral("subscribe") },
        { QStringLiteral("scope"), cmd.value(QStringLiteral("scope")).toString() },
        { QStringLiteral("requested"), list.size() },
        { QStringLiteral("enabled"), enabled },
        { QStringLiteral("failed"), failed },
        { QStringLiteral("skipped"), skipped },
        { QStringLiteral("errors"), QJsonArray::fromStringList(errors) },
        { QStringLiteral("connected"), m_mms.connected() },
    };
}

QJsonObject MmsSessionServer::cmdGi(const QJsonObject& cmd)
{
    if (!m_mms.connected())
        return {{ QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("not connected") }};
    QString err;
    const QVector<RcbDescriptor> list = resolveTargets(cmd.value(QStringLiteral("scope")).toString(),
        cmd.value(QStringLiteral("ied")).toString(), cmd.value(QStringLiteral("rcb")).toString(), &err);
    if (list.isEmpty())
        return {{ QStringLiteral("ok"), false }, { QStringLiteral("error"), err }};

    int sent = 0;
    int failed = 0;
    int skipped = 0;
    QStringList errors;
    for (const RcbDescriptor& d : list) {
        if (!m_subscribed.contains(d.handlerRef)) {
            ++skipped;
            continue;
        }
        QString oneErr;
        if (giOne(d.handlerRef, &oneErr)) {
            ++sent;
        } else {
            ++failed;
            if (errors.size() < 8)
                errors.append(d.handlerRef + QLatin1Char(' ') + oneErr);
        }
        if (!m_mms.connected())
            break;
        if (list.size() > 200)
            QThread::msleep(20);
    }
    const bool ok = sent > 0;
    QJsonObject out{
        { QStringLiteral("ok"), ok },
        { QStringLiteral("cmd"), QStringLiteral("gi") },
        { QStringLiteral("scope"), cmd.value(QStringLiteral("scope")).toString() },
        { QStringLiteral("sent"), sent },
        { QStringLiteral("failed"), failed },
        { QStringLiteral("skippedUnsubscribed"), skipped },
        { QStringLiteral("errors"), QJsonArray::fromStringList(errors) },
        { QStringLiteral("connected"), m_mms.connected() },
    };
    if (!ok && sent == 0 && skipped > 0)
        out.insert(QStringLiteral("error"), QStringLiteral("no subscribed RCB in scope; subscribe first"));
    return out;
}

QJsonObject MmsSessionServer::cmdDisconnect()
{
    m_quitRequested = true;
    m_mms.disconnectHost();
    return {
        { QStringLiteral("ok"), true },
        { QStringLiteral("cmd"), QStringLiteral("disconnect") },
    };
}

bool MmsSessionServer::waitConnected(int timeoutMs)
{
    if (m_mms.connected())
        return true;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    const auto conn = QObject::connect(&m_mms, &MmsClientBridge::eventReceived, &loop, [&](const QJsonObject& evt) {
        const QString t = evt.value(QStringLiteral("evt")).toString();
        if (t == QStringLiteral("connected") || t == QStringLiteral("error") || t == QStringLiteral("disconnected"))
            loop.quit();
    });
    timer.start(timeoutMs);
    loop.exec();
    QObject::disconnect(conn);
    return m_mms.connected();
}

bool MmsSessionServer::waitCommState(int timeoutMs)
{
    if (m_gotCommState)
        return true;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    const auto conn = QObject::connect(&m_mms, &MmsClientBridge::eventReceived, &loop, [&](const QJsonObject& evt) {
        if (evt.value(QStringLiteral("evt")).toString() == QStringLiteral("comm_state")
            || evt.value(QStringLiteral("evt")).toString() == QStringLiteral("disconnected"))
            loop.quit();
    });
    timer.start(timeoutMs);
    loop.exec();
    QObject::disconnect(conn);
    return m_gotCommState;
}

bool MmsSessionServer::enableOne(const RcbDescriptor& d, QString* err)
{
    m_waitKind = QStringLiteral("enable");
    m_waitHandler = d.handlerRef;
    m_waitOk = false;
    m_waitError.clear();
    QEventLoop loop;
    m_waitLoop = &loop;
    m_mms.enableRcb(QJsonObject{
        { QStringLiteral("handlerRef"), d.handlerRef },
        { QStringLiteral("rcbGetRef"), d.getRef },
        { QStringLiteral("dsRef"), d.dsRef },
    });
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        m_waitError = QStringLiteral("TIMEOUT");
        loop.quit();
    });
    timer.start(CliConstants::kSubscribeOneTimeoutMs);
    loop.exec();
    m_waitLoop = nullptr;
    m_waitKind.clear();
    m_waitHandler.clear();
    if (m_waitOk)
        return true;
    if (err)
        *err = m_waitError.isEmpty() ? QStringLiteral("failed") : m_waitError;
    return false;
}

bool MmsSessionServer::giOne(const QString& handlerRef, QString* err)
{
    m_waitKind = QStringLiteral("gi");
    m_waitHandler = handlerRef;
    m_waitOk = false;
    m_waitError.clear();
    QEventLoop loop;
    m_waitLoop = &loop;
    m_mms.triggerGi(handlerRef);
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        m_waitError = QStringLiteral("TIMEOUT");
        loop.quit();
    });
    timer.start(CliConstants::kGiOneTimeoutMs);
    loop.exec();
    m_waitLoop = nullptr;
    m_waitKind.clear();
    m_waitHandler.clear();
    if (m_waitOk)
        return true;
    if (err)
        *err = m_waitError.isEmpty() ? QStringLiteral("failed") : m_waitError;
    return false;
}

void MmsSessionServer::onMmsEvent(const QJsonObject& evt)
{
    const QString type = evt.value(QStringLiteral("evt")).toString();
    if (type == QStringLiteral("comm_state"))
        applyCommState(evt);
    if (type == QStringLiteral("disconnected"))
        m_subscribed.clear();

    bool done = false;
    if (m_waitKind == QStringLiteral("enable") && type == QStringLiteral("rcb_enabled")) {
        const QString handler = evt.value(QStringLiteral("handlerRef")).toString();
        if (handler == m_waitHandler) {
            const bool verified =
                evt.value(QStringLiteral("verified")).isUndefined() || evt.value(QStringLiteral("verified")).toBool();
            m_waitOk = verified;
            if (!verified)
                m_waitError = QStringLiteral("RPTENA_VERIFY_FAILED");
            done = true;
        }
    }
    if (m_waitKind == QStringLiteral("enable") && type == QStringLiteral("error")) {
        m_waitOk = false;
        m_waitError = evt.value(QStringLiteral("msg")).toString();
        done = true;
    }
    if (m_waitKind == QStringLiteral("gi") && type == QStringLiteral("ok")
        && evt.value(QStringLiteral("cmd")).toString() == QStringLiteral("gi")) {
        const QString handler = evt.value(QStringLiteral("handlerRef")).toString();
        if (handler.isEmpty() || handler == m_waitHandler) {
            m_waitOk = true;
            done = true;
        }
    }
    if (m_waitKind == QStringLiteral("gi") && type == QStringLiteral("error")) {
        m_waitOk = false;
        m_waitError = evt.value(QStringLiteral("msg")).toString();
        done = true;
    }
    if (type == QStringLiteral("disconnected") && m_waitLoop) {
        m_waitOk = false;
        m_waitError = QStringLiteral("CONNECTION_LOST");
        done = true;
    }
    if (done && m_waitLoop)
        m_waitLoop->quit();
}

void MmsSessionServer::onMmsLog(const QString& msg)
{
    CliConsole::writeLine(QStringLiteral("# ") + msg);
}

void MmsSessionServer::applyCommState(const QJsonObject& evt)
{
    m_gotCommState = true;
    QHash<QString, bool> st;
    const QJsonArray arr = evt.value(QStringLiteral("entries")).toArray();
    int named = 0;
    for (const QJsonValue& v : arr) {
        const QJsonObject e = v.toObject();
        QString ied = e.value(QStringLiteral("ied")).toString();
        if (ied.isEmpty())
            ied = iedNameFromLd(e.value(QStringLiteral("ld")).toString());
        if (ied.isEmpty())
            continue;
        const bool entryValid = !e.contains(QStringLiteral("valid")) || e.value(QStringLiteral("valid")).toBool();
        if (!entryValid)
            continue;
        ++named;
        st.insert(ied, e.value(QStringLiteral("stVal")).toBool());
    }
    m_commState = st;
    m_commStateAligned = named == arr.size() && named > 0;
}

bool MmsSessionServer::shouldSkipOffline(const QString& iedName) const
{
    return CommStateSupport::shouldSkipOfflineIed(m_commState, m_commStateAligned, iedName);
}

QString MmsSessionServer::iedNameFromLd(const QString& ld)
{
    return CommStateSupport::iedNameFromLd(ld);
}
