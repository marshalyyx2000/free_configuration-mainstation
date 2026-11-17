#ifndef LAB_CONSTANTS_H
#define LAB_CONSTANTS_H

namespace LabOps {

inline constexpr const char* GENCONFIG_HINT_TEXT =
    "约 1 分钟内（加速 genconfig.jar + 装载 supervision.cfg）";

/** 下装生效后等待 102 监听的上限（秒）。大 SMCD 冷启动约 8–15 分钟。 */
inline constexpr int kWaitMmsPortTimeoutSec = 20 * 60;
inline constexpr int kWaitMmsPortPollSec = 5;

/** 验收脚本在 102 已监听后继续等待的上限（秒），可用 SIMMASTER_WAIT_102 覆盖。 */
inline constexpr int kDefaultWait102Sec = 720;

} // namespace LabOps

#endif
