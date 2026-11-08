from pathlib import Path
import difflib

root = Path(r"C:\工程项目开发\1U装置稳定性测试\免配置")
old_path = root / "configuration-free/iec61850_router/third_party/libiec61850-1.5.3/src/iec61850/client/client_report.c"
new_path = root / "master_sim/third_party/libiec61850/src/iec61850/client/client_report.c"
out_path = root / "master_sim/native/patches/libiec61850-report-dataset-size.patch"

old = old_path.read_text(encoding="utf-8", errors="replace").splitlines(keepends=True)
new = new_path.read_text(encoding="utf-8", errors="replace").splitlines(keepends=True)

diff = difflib.unified_diff(
    old,
    new,
    fromfile="a/src/iec61850/client/client_report.c",
    tofile="b/src/iec61850/client/client_report.c",
    n=3,
)
text = "".join(diff)
out_path.parent.mkdir(parents=True, exist_ok=True)
out_path.write_text(text, encoding="utf-8", newline="\n")
print(f"wrote {out_path} ({len(text.splitlines())} lines)")
