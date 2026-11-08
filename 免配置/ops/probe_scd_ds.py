"""查看 SCD 中 dsWarning 的原始 FCDA 文本（确认成员 LD 归属）。"""
import re
import sys
from pathlib import Path

SCD = Path(r"D:\研发\1U装置稳定性测试\免配置\子站scd\江西500kV通天岩变电站26041110\江西500kV通天岩变电站.scd")
IED = sys.argv[1] if len(sys.argv) > 1 else "P_X5071A"
DS = sys.argv[2] if len(sys.argv) > 2 else "dsWarning"

text = SCD.read_text(encoding="utf-8", errors="replace")
m = re.search(r'<IED[^>]*name="%s"[^>]*>' % re.escape(IED), text)
start = m.start()
nxt = re.search(r"<IED[^>]*name=", text[start + 10:])
seg = text[start:start + 10 + (nxt.start() if nxt else len(text))]

for dm in re.finditer(r'<DataSet[^>]*name="%s"[^>]*>' % re.escape(DS), seg):
    body_start = dm.end()
    body_end = seg.find("</DataSet>", body_start)
    print("---- DataSet", DS, "at", dm.start())
    print(seg[body_start:body_start + 700])
