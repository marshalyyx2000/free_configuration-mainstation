"""检查 libiec61850 是否把 RCB 暴露为可写对象（mms_write_service 前置分发）。"""
import re

PATH = "/home/admin/src/configuration-free/iec61850_router/third_party/libiec61850-1.5.3/src/mms/iso_mms/server/mms_write_service.c"

with open(PATH, errors="replace") as fh:
    text = fh.read()

# 找 domain 解析和目标
for pat in ("getVariableFromWriteRequest", "MmsDomain_getVariable", "handleWriteRequest", "proxyWrite"):
    print("=== pattern", pat)
    for m in list(re.finditer(re.escape(pat), text))[:3]:
        s = text.rfind("\n", 0, max(0, m.start() - 300))
        e = text.find("\n", m.end() + 400)
        print(text[s:e][:1200])
        print("-" * 40)
