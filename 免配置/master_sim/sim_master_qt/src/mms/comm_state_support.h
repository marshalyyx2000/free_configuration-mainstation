#ifndef COMM_STATE_SUPPORT_H
#define COMM_STATE_SUPPORT_H

#include <QHash>
#include <QJsonObject>
#include <QString>

namespace CommStateSupport {

inline constexpr const char* kDsCommStateRef = "SubStationLD0/LLN0$dsCommState";
inline constexpr int kDefaultReadTimeoutMs = 120000;
inline constexpr int kDeployReadTimeoutMs = 300000;
/** MMS 关联后延后首读工况，避免与 ping / 全站订阅冲击子站。 */
inline constexpr int kPostConnectDelayMs = 15000;
inline constexpr const char* kTempSmcdFileName = "sim_master_station.SMCD";

QString resolveStationSmcdForCli();
QJsonObject buildReadCommStateCommand(int timeoutMs = kDefaultReadTimeoutMs,
    bool iedDataSetOnly = false, bool preferSmcdFcda = false);

/** 仅在工况已对齐、且至少有一台「合」时，才把明确为「分」的装置从全站订阅中跳过。
 *  全分为分或读失败时视为快照不可信，避免 449 条 RCB 全部被静默跳过。 */
inline bool shouldSkipOfflineIed(const QHash<QString, bool>& comm, bool aligned, const QString& iedName)
{
    if (!aligned || iedName.isEmpty() || !comm.contains(iedName))
        return false;
    bool anyOn = false;
    for (auto it = comm.constBegin(); it != comm.constEnd(); ++it) {
        if (it.value()) {
            anyOn = true;
            break;
        }
    }
    if (!anyOn)
        return false;
    return !comm.value(iedName);
}

} // namespace CommStateSupport

#endif
