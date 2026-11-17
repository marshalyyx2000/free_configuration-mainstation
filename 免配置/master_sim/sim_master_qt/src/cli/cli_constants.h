#ifndef CLI_CONSTANTS_H
#define CLI_CONSTANTS_H

namespace CliConstants {

inline constexpr const char* kPipeName = "SimMasterQt.mms";
inline constexpr int kClientConnectTimeoutMs = 3000;
inline constexpr int kSubscribeOneTimeoutMs = 45000;
inline constexpr int kGiOneTimeoutMs = 15000;
inline constexpr int kClientReplyTimeoutMs = 30 * 60 * 1000;

} // namespace CliConstants

#endif
