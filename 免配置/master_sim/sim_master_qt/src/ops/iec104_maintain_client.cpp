#include "ops/iec104_maintain_client.h"

#include <QDateTime>
#include <QTcpSocket>
#include <QThread>

#include <stdexcept>

namespace {
constexpr quint8 P104F_STARTDT_ACT = 0x07;
constexpr quint8 P104F_STARTDT_CON = 0x0B;
constexpr quint8 P104F_TESTFR_ACT = 0x43;
constexpr quint8 P104F_TESTFR_CON = 0x83;
} // namespace

Iec104MaintainClient::Iec104MaintainClient(const Maintain104Config& cfg)
    : m_cfg(cfg)
{
}

void Iec104MaintainClient::connectToHost(int retries)
{
    std::exception_ptr lastErr;
    for (int attempt = 0; attempt < retries; ++attempt) {
        try {
            close();
            m_sock = new QTcpSocket;
            m_sock->connectToHost(m_cfg.host, static_cast<quint16>(m_cfg.port));
            if (!m_sock->waitForConnected(m_cfg.timeoutMs))
                throw std::runtime_error("connect timeout");
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
    if (m_sock) {
        m_sock->close();
        delete m_sock;
        m_sock = nullptr;
    }
}

void Iec104MaintainClient::sendU(quint8 ctrl1)
{
    const char frame[] = { char(0x68), char(0x04), char(0x00), char(ctrl1), char(0x00), char(0x00), char(0x00) };
    m_sock->write(frame, 7);
    m_sock->waitForBytesWritten(m_cfg.timeoutMs);
}

void Iec104MaintainClient::waitU(quint8 expectedCtrl1)
{
    const QByteArray data = recvExact(7);
    if (data.size() != 7 || uchar(data[0]) != 0x68 || uchar(data[1]) != 0x04 || uchar(data[2]) != 0x00
        || uchar(data[3]) != expectedCtrl1)
        throw std::runtime_error(QStringLiteral("unexpected U-frame: %1").arg(QString(data.toHex())).toStdString());
}

QByteArray Iec104MaintainClient::recvExact(int n)
{
    QByteArray buf;
    while (buf.size() < n) {
        if (!m_sock->waitForReadyRead(m_cfg.timeoutMs))
            throw std::runtime_error("connection closed");
        buf.append(m_sock->read(n - buf.size()));
        if (m_sock->state() != QAbstractSocket::ConnectedState && buf.size() < n)
            throw std::runtime_error("connection closed");
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

std::pair<int, QByteArray> Iec104MaintainClient::readApdu()
{
    QByteArray start = recvExact(1);
    if (uchar(start[0]) != 0x68)
        throw std::runtime_error("bad APCI start");
    QByteArray lenBytes = recvExact(2);
    const quint16 length = quint8(lenBytes[0]) | (quint16(quint8(lenBytes[1])) << 8);
    if (length == 4) {
        QByteArray rest = recvExact(4);
        const quint8 c0 = quint8(rest[0]);
        if ((c0 & 0x03) == 0x03) {
            if (c0 == P104F_TESTFR_ACT)
                sendU(P104F_TESTFR_CON);
            return readApdu();
        }
        return { 0, QByteArray() };
    }
    QByteArray body = recvExact(length);
    if (body.size() < 4)
        return { 0, body };
    const quint16 peerNs = quint8(body[0]) | (quint16(quint8(body[1])) << 8);
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
    QByteArray head;
    head.append(char(typ));
    head.append(char(0x81));
    head.append(char(cot));
    head.append(char(m_cfg.asduAddr & 0xFF));
    head.append(char(fun));
    head.append(char(inf));
    head.append(char(rii));
    if (typ == 0x69 || typ == 0x6B || typ == 0xCA)
        head.append(char(0x00));
    return head + payload;
}

void Iec104MaintainClient::sendSAck()
{
    const int nr = m_recvSeq;
    const quint8 c2 = quint8(((nr & 0x7F) << 1) & 0xFF);
    const quint8 c3 = quint8(((nr & 0x3F80) >> 7) & 0xFF);
    const char frame[] = { char(0x68), char(0x04), char(0x00), char(0x01), char(0x00), char(c2), char(c3) };
    m_sock->write(frame, 7);
    m_sock->waitForBytesWritten(3000);
}

QByteArray Iec104MaintainClient::recvUntilAsdu(int expectTyp, int timeoutMs)
{
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        const int remain = int(deadline - QDateTime::currentMSecsSinceEpoch());
        m_sock->setReadBufferSize(0);
        if (!m_sock->waitForReadyRead(qMax(200, remain)))
            continue;
        try {
            auto [kind, payload] = readApdu();
            if (kind == 1 && !payload.isEmpty() && uchar(payload[0]) == expectTyp) {
                sendSAck();
                return payload;
            }
            if (kind == 1 && !payload.isEmpty())
                sendSAck();
        } catch (...) {
            continue;
        }
    }
    throw std::runtime_error(QStringLiteral("等待 ASDU 0x%1 超时").arg(expectTyp, 2, 16, QChar('0')).toStdString());
}

void Iec104MaintainClient::drainLink(int timeoutMs)
{
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        const int remain = int(deadline - QDateTime::currentMSecsSinceEpoch());
        if (!m_sock->waitForReadyRead(qMax(100, remain)))
            break;
        try {
            auto [kind, payload] = readApdu();
            if (kind == 1 && !payload.isEmpty())
                sendSAck();
        } catch (...) {
            break;
        }
    }
}

QByteArray Iec104MaintainClient::sendIAndRecvAsdu(const QByteArray& asdu, int expectTyp)
{
    m_sock->write(buildIFrame(asdu));
    m_sock->waitForBytesWritten(m_cfg.timeoutMs);
    return recvUntilAsdu(expectTyp, m_cfg.timeoutMs);
}

void Iec104MaintainClient::sendINoReply(const QByteArray& asdu)
{
    m_sock->write(buildIFrame(asdu));
    m_sock->waitForBytesWritten(m_cfg.timeoutMs);
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
    if (resp.isEmpty() || uchar(resp[0]) != 0x6A || uchar(resp[2]) != 0x14)
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
            if (resp.isEmpty() || uchar(resp[0]) != 0x6C)
                throw std::runtime_error("file chunk failed");
            if (uchar(resp[2]) == 0x15)
                throw std::runtime_error("file chunk denied");
            framesSinceAck = 0;
        } else {
            sendINoReply(contentAsdu);
            framesSinceAck++;
        }
        pos = end;
    }
}

void Iec104MaintainClient::effect61850()
{
    const int rii = nextRii();
    QByteArray payload;
    payload.append(char(rii));
    QByteArray asdu = buildAsdu(0xCA, 2, payload, 0x14, 0xFF, rii);
    QByteArray resp = sendIAndRecvAsdu(asdu, 0xCB);
    if (resp.isEmpty() || uchar(resp[0]) != 0xCB || uchar(resp[2]) != 0x14)
        throw std::runtime_error("61850 effect failed");
}

void Iec104MaintainClient::uploadConfig(const QByteArray& iedList, const QByteArray& stationZip)
{
    downloadFile(QStringLiteral("/CONF/IEDList.xml"), iedList, 1);
    QThread::msleep(200);
    downloadFile(QStringLiteral("/SMCD/station.zip"), stationZip, 2);
    QThread::msleep(200);
    effect61850();
}
