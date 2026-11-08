"""对比 P_L5031A 各实例的 get RCB 结果，判断装置实际有多少 RCB 实例。"""
import re

PATH = "/home/admin/router/logs/iec61850ReportStatus.log"

ok_inst = {}
fail_inst = {}
with open(PATH, errors="replace") as fh:
    for ln in fh:
        m = re.search(r"(?:get RCB values for|subscribe for) (\S+?)(\d\d) (success|failed)", ln)
        if not m:
            continue
        ref, inst, res = m.group(1), m.group(2), m.group(3)
        if "P_L5031A" not in ref:
            continue
        d = ok_inst if res == "success" else fail_inst
        d.setdefault(inst, 0)
        d[inst] += 1

print("P_L5031A ok instances:", sorted(ok_inst))
print("P_L5031A failed instances:", sorted(fail_inst))
print("ok counts:", {k: ok_inst[k] for k in sorted(ok_inst)})
print("fail counts:", {k: fail_inst[k] for k in sorted(fail_inst)})
