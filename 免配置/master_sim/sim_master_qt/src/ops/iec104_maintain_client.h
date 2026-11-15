#ifndef IEC104_MAINTAIN_CLIENT_H
#define IEC104_MAINTAIN_CLIENT_H

#include <QByteArray>
#include <QString>

struct Maintain104Config {
    QString host = QStringLiteral("192.168.0.10");
    int port = 2405;
    int asduAddr = 1;
    int timeoutMs = 30000;
};

class Iec104MaintainClient {
public:
    explicit Iec104MaintainClient(const Maintain104Config& cfg);
    ~Iec104MaintainClient();

    void connectToHost(int retries = 3);
    void close();

    void downloadFile(const QString& remotePath, const QByteArray& data, int fileInf = 0,
        int chunkSize = 1200, int mesCount = 1);
    void effectConfigure(int inf);
    void stopIec61850Router();
    void effect61850();
    void uploadConfig(const QByteArray& iedList, const QByteArray& stationZip);

private:
    void sendU(quint8 ctrl1);
    void waitU(quint8 expectedCtrl1);
    void sendAll(const QByteArray& data);
    QByteArray recvExact(int n, int timeoutMs);
    QByteArray buildIFrame(const QByteArray& asdu);
    std::pair<int, QByteArray> readApdu(int ioTimeoutMs);
    int nextRii();
    QByteArray buildAsdu(int typ, int inf, const QByteArray& payload, int cot = 0x14, int fun = 0xFF,
        int rii = -1);
    void sendSAck();
    QByteArray recvUntilAsdu(int expectTyp, int timeoutMs);
    void drainLink(int timeoutMs = 1000);
    QByteArray sendIAndRecvAsdu(const QByteArray& asdu, int expectTyp);
    void sendINoReply(const QByteArray& asdu);
    QByteArray fileInfoPayload(const QString& path, int mesCount);

    Maintain104Config m_cfg;
    intptr_t m_sock = -1;
    int m_sendSeq = 0;
    int m_recvSeq = 0;
    int m_rii = 1;
};

#endif
