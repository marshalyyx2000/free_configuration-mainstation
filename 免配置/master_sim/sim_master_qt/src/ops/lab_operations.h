#ifndef LAB_OPERATIONS_H
#define LAB_OPERATIONS_H

#include <functional>
#include <optional>

#include <QString>

class MmsClientBridge;

using LabLogFn = std::function<void(const QString&)>;

namespace LabOps {

inline constexpr const char* GENCONFIG_HINT_TEXT =
    "约 1 分钟内（加速 genconfig.jar + 装载 supervision.cfg）";

bool tcpReachable(const QString& host, int port, int timeoutMs = 3000);
qint64 modelSizeBytes(const QString& outDir);
QString formatMb(qint64 numBytes);
QString formatDuration(double seconds);

int runGenerate(const QString& scdPath, const QString& outDir, const LabLogFn& log = {});
std::optional<double> runUpload(const QString& host, int maintainPort, const QString& outDir,
    bool waitGenconfig, int mmsPort, const LabLogFn& log = {});
int runMmsCheck(const QString& host, int mmsPort, MmsClientBridge* bridge, const LabLogFn& log = {});
int runFullPipeline(const QString& host, int maintainPort, const QString& scdPath, const QString& outDir,
    bool mmsCheck, bool waitGenconfig, int mmsPort, MmsClientBridge* bridge, const LabLogFn& log = {});

} // namespace LabOps

#endif
