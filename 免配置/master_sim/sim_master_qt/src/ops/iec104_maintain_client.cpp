#include "ops/iec104_maintain_client.h"

#include <QDateTime>
#include <QHostAddress>
#include <QThread>

#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

namespace {

constexpr quint8 P104F_STARTDT_ACT = 0x07;
constexpr quint8 P104F_STARTDT_CON = 0x0B;
constexpr quint8 P104F_TESTFR_ACT = 0x43;
constexpr quint8 P104F_TESTFR_CON = 0x83;

quint8 toU8(char c)
{
    return static_cast<quint8>(static_cast<unsigned char>(c));
}

class SocketInit {
public:
    SocketInit()
    {
#ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            throw std::runtime_error("WSAStartup failed");
#endif
    }
    ~SocketInit()
    {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

SocketInit& socketInitOnce()
{
    static SocketInit init;
    return init;
}

bool waitReadable(NativeSocket sock, int timeoutMs)
{
    if (timeoutMs < 0)
        timeoutMs = 0;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
#ifdef _WIN32
    const int rc = select(0, &fds, nullptr, nullptr, &tv);
#else
    const int rc = select(sock + 1, &fds, nullptr, nullptr, &tv);
#endif
    return rc > 0;
}

void setTcpNoDelay(NativeSocket sock)
{
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
}

void closeNative(NativeSocket sock)
{
    if (sock == kInvalidSocket)
        return;
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

NativeSocket connectTcp(const QString& host, int port, int timeoutMs)
{
    Q_UNUSED(timeoutMs);
    socketInitOnce();

    QHostAddress addr;
    if (!addr.setAddress(host)) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        const QByteArray hostUtf8 = host.toUtf8();
        const std::string portStr = std::to_string(port);
        if (getaddrinfo(hostUtf8.constData(), portStr.c_str(), &hints, &res) != 0 || !res)
            throw std::runtime_error("resolve host failed");
        NativeSocket sock = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock == kInvalidSocket) {
            freeaddrinfo(res);
            throw std::runtime_error("socket create failed");
        }
        setTcpNoDelay(sock);
        const int rc = ::connect(sock, res->ai_addr, static_cast<int>(res->ai_addrlen));
        freeaddrinfo(res);
        if (rc != 0)
            throw std::runtime_error("connect failed");
        return sock;
    }

    if (addr.protocol() != QAbstractSocket::IPv4Protocol)
        throw std::runtime_error("only IPv4 supported");

    NativeSocket sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == kInvalidSocket)
        throw std::runtime_error("socket create failed");
    setTcpNoDelay(sock);

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<u_short>(port));
    sa.sin_addr.s_addr = htonl(addr.toIPv4Address());

    if (::connect(sock, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
        closeNative(sock);
        throw std::runtime_error("connect failed");
    }
    return sock;
}

} // namespace

Iec104MaintainClient::Iec104MaintainClient(const Maintain104Config& cfg)
    : m_cfg(cfg)
{
}

Iec104MaintainClient::~Iec104MaintainClient()
{
    close();
}

void Iec104MaintainClient::connectToHost(int retries)
{
    std::exception_ptr lastErr;
    for (int attempt = 0; attempt < retries; ++attempt) {
        try {
            close();
            const NativeSocket sock = connectTcp(m_cfg.host, m_cfg.port, m_cfg.timeoutMs);
            m_sock = static_cast<intptr_t>(sock);
            m_sendSeq = 0;
            m_recvSeq = 0;
            sendU(P104F_STARTDT_ACT);
            waitU(P104F_STARTDT_CON);
            return;
        } catch (...) {
            lastErr = std::current_exception();
            close();
            QThread::msleep(1000);
        }
    }
    if (lastErr)
        std::rethrow_exception(lastErr);
    throw std::runtime_error("104 connect failed");
}

void Iec104MaintainClient::close()
{
    if (m_sock >= 0) {
        closeNative(static_cast<NativeSocket>(m_sock));
        m_sock = -1;
    }
}

void Iec104MaintainClient::sendAll(const QByteArray& data)
{
    if (m_sock < 0)
        throw std::runtime_error("not connected");
    const NativeSocket sock = static_cast<NativeSocket>(m_sock);
    int sent = 0;
    while (sent < data.size()) {
        const int chunk = data.size() - sent;
#ifdef _WIN32
        const int n = ::send(sock, data.constData() + sent, chunk, 0);
#else
        const int n = static_cast<int>(::send(sock, data.constData() + sent, static_cast<size_t>(chunk), 0));
#endif
        if (n <= 0)
            throw std::runtime_error("send failed");
        sent += n;
    }
}

void Iec104MaintainClient::sendU(quint8 ctrl1)
{
    const char frame[] = { char(0x68), char(0x04), char(0x00), char(ctrl1), char(0x00), char(0x00), char(0x00) };
    sendAll(QByteArray(frame, 7));
}

void Iec104MaintainClient::waitU(quint8 expectedCtrl1)
{
    const QByteArray data = recvExact(7, m_cfg.timeoutMs);
    if (data.size() != 7 || toU8(data[0]) != 0x68 || toU8(data[1]) != 0x04 || toU8(data[2]) != 0x00
        || toU8(data[3]) != expectedCtrl1)
        throw std::runtime_error(QStringLiteral("unexpected U-frame: %1").arg(QString(data.toHex())).toStdString());
}

QByteArray Iec104MaintainClient::recvExact(int n, int timeoutMs)
{
    if (m_sock < 0)
        throw std::runtime_error("not connected");
    const NativeSocket sock = static_cast<NativeSocket>(m_sock);
    QByteArray buf;
    buf.reserve(n);
    while (buf.size() < n) {
        if (!waitReadable(sock, timeoutMs))
            throw std::runtime_error("timeout");
        char tmp[8192];
        const int want = n - buf.size();
        const int cap = qMin(want, static_cast<int>(sizeof(tmp)));
#ifdef _WIN32
        const int r = ::recv(sock, tmp, cap, 0);
#else
        const int r = static_cast<int>(::recv(sock, tmp, static_cast<size_t>(cap), 0));
#endif
        if (r <= 0)
            throw std::runtime_error("connection closed");
        buf.append(tmp, r);
    }
    return buf;
}

QByteArray Iec104MaintainClient::buildIFrame(const QByteArray& asdu)
{
    const int apduLen = 4 + asdu.size();
    quint16 sendCtrl = static_cast<quint16>(m_sendSeq << 1);
    quint16 recvCtrl = static_cast<quint16>(m_recvSeq << 1);
    m_sendSeq = (m_sendSeq + 1) % 32768;
    QByteArray frame;
    frame.append(char(0x68));
    frame.append(char(apduLen & 0xFF));
    frame.append(char((apduLen >> 8) & 0xFF));
    frame.append(char(sendCtrl & 0xFF));
    frame.append(char((sendCtrl >> 8) & 0xFF));
    frame.append(char(recvCtrl & 0xFF));
    frame.append(char((recvCtrl >> 8) & 0xFF));
    frame.append(asdu);
    return frame;
}

std::pair<int, QByteArray> Iec104MaintainClient::readApdu(int ioTimeoutMs)
{
    QByteArray start = recvExact(1, ioTimeoutMs);
    if (toU8(start[0]) != 0x68)
        throw std::runtime_error("bad APCI start");
    QByteArray lenBytes = recvExact(2, ioTimeoutMs);
    const quint16 length = toU8(lenBytes[0]) | (quint16(toU8(lenBytes[1])) << 8);
    if (length == 4) {
        QByteArray rest = recvExact(4, ioTimeoutMs);
        const quint8 c0 = toU8(rest[0]);
        if ((c0 & 0x03) == 0x03) {
            if (c0 == P104F_TESTFR_ACT)
                sendU(P104F_TESTFR_CON);
            return readApdu(ioTimeoutMs);
        }
        return { 0, QByteArray() };
    }
    QByteArray body = recvExact(length, ioTimeoutMs);
    if (body.size() < 4)
        return { 0, body };
    const quint16 peerNs = toU8(body[0]) | (quint16(toU8(body[1])) << 8);
    m_recvSeq = ((peerNs >> 1) + 1) % 32768;
    return { 1, body.mid(4) };
}

int Iec104MaintainClient::nextRii()
{
    const int v = m_rii;
    m_rii = (m_rii + 1) & 0xFF;
    if (m_rii == 0)
        m_rii = 1;
    return v;
}

QByteArray Iec104MaintainClient::buildAsdu(int typ, int inf, const QByteArray& payload, int cot, int fun, int rii)
{
    if (rii < 0)
        rii = nextRii();
    // 装置 BufID 10421 = CMH_104_Len2_Cpu：Type VSQ COT Addr CPU FUN INF RII。
    // 旧实现把 0x00 追加在 RII 后，装置把 RII 当成 INF，INF=8 实际变成 INF=2。
    QByteArray head;
    head.append(char(typ));
    head.append(char(0x81));
    head.append(char(cot));
    head.append(char(m_cfg.asduAddr & 0xFF));
    head.append(char(0x00));
    head.append(char(fun));
    head.append(char(inf));
    head.append(char(rii));
    return head + payload;
}

void Iec104MaintainClient::sendSAck()
{
    const int nr = m_recvSeq;
    const quint8 c2 = quint8(((nr & 0x7F) << 1) & 0xFF);
    const quint8 c3 = quint8(((nr & 0x3F80) >> 7) & 0xFF);
    const char frame[] = { char(0x68), char(0x04), char(0x00), char(0x01), char(0x00), char(c2), char(c3) };
    sendAll(QByteArray(frame, 7));
}

QByteArray Iec104MaintainClient::recvUntilAsdu(int expectTyp, int timeoutMs)
{
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        const int remain = int(deadline - QDateTime::currentMSecsSinceEpoch());
        const int ioTimeout = qMax(200, remain);
        try {
            auto [kind, payload] = readApdu(ioTimeout);
            if (kind == 1 && !payload.isEmpty()) {
                if (toU8(payload[0]) == expectTyp) {
                    sendSAck();
                    return payload;
                }
                sendSAck();
            }
        } catch (const std::runtime_error& e) {
            const char* msg = e.what();
            if (msg && std::string_view(msg).find("timeout") != std::string_view::npos)
                continue;
            throw;
        }
    }
    throw std::runtime_error(QStringLiteral("等待 ASDU 0x%1 超时").arg(expectTyp, 2, 16, QChar('0')).toStdString());
}

void Iec104MaintainClient::drainLink(int timeoutMs)
{
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        const int remain = int(deadline - QDateTime::currentMSecsSinceEpoch());
        const int ioTimeout = qMax(100, remain);
        try {
            auto [kind, payload] = readApdu(ioTimeout);
            if (kind == 1 && !payload.isEmpty())
                sendSAck();
        } catch (const std::runtime_error& e) {
            const char* msg = e.what();
            if (msg && std::string_view(msg).find("timeout") != std::string_view::npos)
                break;
            throw;
        }
    }
}

QByteArray Iec104MaintainClient::sendIAndRecvAsdu(const QByteArray& asdu, int expectTyp)
{
    sendAll(buildIFrame(asdu));
    return recvUntilAsdu(expectTyp, m_cfg.timeoutMs);
}

void Iec104MaintainClient::sendINoReply(const QByteArray& asdu)
{
    sendAll(buildIFrame(asdu));
    drainLink(1500);
}

QByteArray Iec104MaintainClient::fileInfoPayload(const QString& path, int mesCount)
{
    QByteArray block(112, char(0));
    block[0] = char(mesCount & 0xFF);
    const QByteArray name = path.toLatin1();
    const int copyLen = qMin(name.size(), 100);
    if (copyLen > 0)
        block.replace(1, copyLen, name.constData(), copyLen);
    return block;
}

void Iec104MaintainClient::downloadFile(const QString& remotePath, const QByteArray& data, int fileInf, int chunkSize,
    int mesCount)
{
    const int rii = nextRii();
    QByteArray infoAsdu = buildAsdu(0x69, fileInf, fileInfoPayload(remotePath, mesCount), 0x14, 0xFF, rii);
    QByteArray resp = sendIAndRecvAsdu(infoAsdu, 0x6A);
    if (resp.isEmpty() || toU8(resp[0]) != 0x6A || toU8(resp[2]) != 0x14)
        throw std::runtime_error("file info rejected");

    int pos = 0;
    int framesSinceAck = 0;
    const int total = data.size();
    while (pos < total) {
        const int end = qMin(pos + chunkSize, total);
        const QByteArray chunk = data.mid(pos, end - pos);
        const int more = end >= total ? 0 : 1;
        QByteArray body;
        body.append(char(more));
        body.append(char(pos & 0xFF));
        body.append(char((pos >> 8) & 0xFF));
        body.append(char((pos >> 16) & 0xFF));
        body.append(char((pos >> 24) & 0xFF));
        body.append(chunk);
        QByteArray contentAsdu = buildAsdu(0x6B, fileInf, body, 0x14, 0xFF, rii);
        const bool isLast = end >= total;
        const bool needAck = isLast || (framesSinceAck + 1) >= mesCount;
        if (needAck) {
            resp = sendIAndRecvAsdu(contentAsdu, 0x6C);
            if (resp.isEmpty() || toU8(resp[0]) != 0x6C)
                throw std::runtime_error("file chunk failed");
            if (toU8(resp[2]) == 0x15)
                throw std::runtime_error("file chunk denied");
            framesSinceAck = 0;
        } else {
            sendINoReply(contentAsdu);
            framesSinceAck++;
        }
        pos = end;
    }
}

void Iec104MaintainClient::effectConfigure(int inf)
{
    const int rii = nextRii();
    QByteArray payload;
    payload.append(char(rii));
    QByteArray asdu = buildAsdu(0xCA, inf, payload, 0x14, 0xFF, rii);
    QByteArray resp = sendIAndRecvAsdu(asdu, 0xCB);
    if (resp.isEmpty() || toU8(resp[0]) != 0xCB || toU8(resp[2]) != 0x14) {
        throw std::runtime_error(
            QStringLiteral("配置命令 INF=%1 失败").arg(inf).toStdString());
    }
}

void Iec104MaintainClient::stopIec61850Router()
{
    // 装置 HandleTakeffectConf INF=8：chmod +x 后 system() 执行 path/SYS/*.sh。
    // 0xCA INF=2 的 StartProgram 只 kill 插件进程表里的 PID，杀不掉
    // MaintainServer 看门狗拉起的 iec61850router，现场也没有 SSH。
    static const char kScript[] =
        "#!/bin/sh\n"
        "killall -9 /home/admin/router/bin/iec61850router >/dev/null 2>&1 || true\n"
        "killall -9 iec61850router >/dev/null 2>&1 || true\n"
        "pkill -9 -f /home/admin/router/bin/iec61850router >/dev/null 2>&1 || true\n"
        "rm -f /home/admin/router/router/supervision.cfg.stamp "
        "/home/admin/router/router/.stamp "
        "/home/admin/router/config/supervision.cfg.stamp\n"
        "exit 0\n";
    downloadFile(QStringLiteral("/SYS/stop_iec61850router.sh"), QByteArray(kScript), 0);
    QThread::msleep(1000);
    effectConfigure(8);
}

void Iec104MaintainClient::effect61850()
{
    effectConfigure(2);
}

void Iec104MaintainClient::uploadConfig(const QByteArray& iedList, const QByteArray& stationZip)
{
    downloadFile(QStringLiteral("/CONF/IEDList.xml"), iedList, 1);
    QThread::msleep(200);
    downloadFile(QStringLiteral("/SMCD/station.zip"), stationZip, 2);
    QThread::msleep(200);
    stopIec61850Router();
    QThread::msleep(200);
    effect61850();
}
