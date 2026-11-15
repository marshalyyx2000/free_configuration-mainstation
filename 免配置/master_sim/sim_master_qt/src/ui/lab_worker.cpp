#include "ui/lab_worker.h"
#include "ops/lab_operations.h"
#include "settings/shared_connection_settings.h"
#include "util/app_paths.h"

#include <optional>

LabWorker::LabWorker(SharedConnectionSettings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
}

void LabWorker::runJob(LabJobKind kind)
{
    const LabLogFn log = [this](const QString& msg) { emit logLine(msg); };
    std::optional<double> genSec;
    bool ok = true;
    try {
        const QString out = AppPaths::outDir();
        const QString host = m_settings->host();
        const int mp = m_settings->maintainPort();
        const int mms = m_settings->mmsPort();
        switch (kind) {
        case LabJobKind::Generate:
            LabOps::runGenerate(m_scdPath, out, log);
            emit modelOutputsChanged();
            break;
        case LabJobKind::Upload:
            genSec = LabOps::runUpload(host, mp, out, m_waitGenconfig, mms, log);
            emit modelOutputsChanged();
            break;
        case LabJobKind::MmsCheck:
            LabOps::runMmsCheck(host, mms, nullptr, log);
            break;
        case LabJobKind::FullPipeline:
            LabOps::runGenerate(m_scdPath, out, log);
            emit modelOutputsChanged();
            genSec = LabOps::runUpload(host, mp, out, m_waitGenconfig, mms, log);
            emit modelOutputsChanged();
            if (m_mmsAfterFull)
                LabOps::runMmsCheck(host, mms, nullptr, log);
            break;
        }
    } catch (const std::exception& ex) {
        emit logLine(QStringLiteral("错误: %1").arg(QString::fromUtf8(ex.what())));
        ok = false;
    } catch (...) {
        emit logLine(QStringLiteral("错误: 未知异常"));
        ok = false;
    }
    emit finished(ok, genSec.value_or(0.0), genSec.has_value());
}
