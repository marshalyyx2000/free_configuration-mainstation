#ifndef LAB_WORKER_H
#define LAB_WORKER_H

#include <QObject>

class SharedConnectionSettings;
class MmsClientBridge;

enum class LabJobKind {
    Generate,
    Upload,
    MmsCheck,
    FullPipeline,
};
Q_DECLARE_METATYPE(LabJobKind)

class LabWorker : public QObject {
    Q_OBJECT
public:
    explicit LabWorker(SharedConnectionSettings* settings, QObject* parent = nullptr);

    void setScdPath(const QString& path) { m_scdPath = path; }
    void setWaitGenconfig(bool v) { m_waitGenconfig = v; }
    void setMmsAfterFull(bool v) { m_mmsAfterFull = v; }

public slots:
    void runJob(LabJobKind kind);

signals:
    void logLine(const QString& line);
    void finished(bool ok, double genconfigSec, bool hasGenconfigSec);
    void modelOutputsChanged();

private:
    SharedConnectionSettings* m_settings = nullptr;
    QString m_scdPath;
    bool m_waitGenconfig = true;
    bool m_mmsAfterFull = true;
};

#endif
