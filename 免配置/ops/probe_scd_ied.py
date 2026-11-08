"""从本地 SCD 提取指定 IED 的 LDevice/LN0/DataSet 结构，核对与装置实际模型是否一致。"""
import re
import sys
from pathlib import Path

SCD = Path(r"D:\研发\1U装置稳定性测试\免配置\子站scd\江西500kV通天岩变电站26041110\江西500kV通天岩变电站.scd")
IED = sys.argv[1] if len(sys.argv) > 1 else "P_X5071A"

print("scd exists:", SCD.is_file(), SCD.stat().st_size if SCD.is_file() else 0)
text = SCD.read_text(encoding="utf-8", errors="replace")

m = re.search(r'<IED[^>]*name="%s"[^>]*>' % re.escape(IED), text)
if not m:
    print("IED not found")
    raise SystemExit(2)
start = m.start()
nxt = re.search(r"<IED[^>]*name=", text[start + 10:])
end = start + 10 + nxt.start() if nxt else len(text)
seg = text[start:end]
print("IED segment size:", len(seg))
for ld in re.finditer(r'<LDevice[^>]*inst="([^"]*)"', seg):
    print("  LDevice inst:", ld.group(1))
print("DataSet names:", re.findall(r'<DataSet[^>]*name="([^"]*)"', seg)[:20])
print("ReportControl names:", re.findall(r'<ReportControl[^>]*name="([^"]*)"', seg)[:20])
print("RptEnabled max:", re.findall(r'RptEnabled max="([^"]*)"', seg)[:5])
