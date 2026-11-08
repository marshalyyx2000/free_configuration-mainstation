"""核对 SCD 中指定 IED 的 LDevice -> LN/DataSet 归属，验证 FCDA 的 ldInst 是否被改写。"""
import re
import sys
from pathlib import Path

SCD = Path(r"D:\研发\1U装置稳定性测试\免配置\子站scd\江西500kV通天岩变电站26041110\江西500kV通天岩变电站.scd")
IED = sys.argv[1] if len(sys.argv) > 1 else "P_X5071A"

text = SCD.read_text(encoding="utf-8", errors="replace")
m = re.search(r'<IED[^>]*name="%s"[^>]*>' % re.escape(IED), text)
start = m.start()
nxt = re.search(r"<IED[^>]*name=", text[start + 10:])
end = start + 10 + nxt.start() if nxt else len(text)
seg = text[start:end]

# 按 LDevice 切段
lds = list(re.finditer(r'<LDevice[^>]*inst="([^"]*)"[^>]*>', seg))
for i, ld in enumerate(lds):
    e = lds[i + 1].start() if i + 1 < len(lds) else len(seg)
    body = seg[ld.start():e]
    inst = ld.group(1)
    lns = re.findall(r'<LN[^>]*lnClass="([^"]*)"[^>]*inst="([^"]*)"', body)
    dss = re.findall(r'<DataSet[^>]*name="([^"]*)"', body)
    print("LDevice", inst, "| LN:", lns[:10], "| DS:", dss)

# 检查 dsWarning 的 FCDA
for ld in lds:
    e = seg.find("</LDevice>", ld.start())
    body = seg[ld.start():e]
    dm = re.search(r'<DataSet[^>]*name="dsWarning"[^>]*>(.*?)</DataSet>', body, re.S)
    if dm:
        fcdas = re.findall(r'<FCDA[^>]*ldInst="([^"]*)"[^>]*prefix="([^"]*)"[^>]*lnClass="([^"]*)"[^>]*lnInst="([^"]*)"[^>]*doName="([^"]*)"', dm.group(1))
        print("dsWarning in LDevice", ld.group(1), "first FCDAs:", fcdas[:4])
