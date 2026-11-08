"""修正子站合并模型域名为 SubStationLD0。

libiec61850 服务端代理分派规则：
`strncmp(domainId, deviceName, strlen(deviceName)) != 0` 时才转给下联装置。
本地合并 IED 名为 `SubStation`，其 LDevice inst 必须带 `SubStation` 前缀，
否则 `LD0/LLN0$dsCommState` 会被当成代理请求，匹配不到任何装置 → 静默丢弃 →
主站读通信工况永远 TIMEOUT。
"""
import os
import re
import sys

CFG = "/home/admin/router/config/supervision.cfg"
BACKUP = CFG + ".bak_pre_ldrename"


def main() -> int:
    with open(CFG, "rb") as fh:
        raw = fh.read()
    text = raw.decode("utf-8", "replace")

    if "LD(SubStationLD0)" in text:
        print("already renamed")
        return 0

    # 只替换合并模型里的第一个 LD(LD0)（SubStation 自己的 LD），
    # 其余形如 P_xxxLD0 的装置域不动。
    idx = text.find("LD(LD0){")
    if idx < 0:
        print("LD(LD0) not found", file=sys.stderr)
        return 2

    text = text[:idx] + "LD(SubStationLD0){" + text[idx + len("LD(LD0){"):]

    if not os.path.exists(BACKUP):
        os.rename(CFG, BACKUP)
    else:
        os.remove(CFG)
    with open(CFG, "wb") as fh:
        fh.write(text.encode("utf-8"))
    print("renamed LD(LD0) -> LD(SubStationLD0), size", os.path.getsize(CFG))
    print("dsCommState count:", text.count("DS(dsCommState){"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
