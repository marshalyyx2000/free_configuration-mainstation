"""在子站本地执行：定位需要修改的代理分发代码片段。"""
import re

FILES = [
    "/home/admin/src/configuration-free/iec61850_router/third_party/libiec61850-1.5.3/src/mms/iso_mms/server/mms_read_service.c",
    "/home/admin/src/configuration-free/iec61850_router/third_party/libiec61850-1.5.3/src/mms/iso_mms/server/mms_write_service.c",
]

for path in FILES:
    print("=" * 20, path)
    with open(path) as fh:
        text = fh.read()
    for pat in ("proxyReadNamedVariableHandler != NULL", "proxyReadVariableHandler != NULL", "proxyWriteVariableHandler != NULL"):
        for m in re.finditer(re.escape(pat), text):
            s = text.rfind("\n", 0, max(0, m.start() - 400))
            e = text.find("\n", m.end() + 500)
            line_no = text[: m.start()].count("\n") + 1
            print("--- line", line_no, pat)
            print(text[s:e])
