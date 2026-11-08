"""按台账顺序解析 dsCommState，统计在线/离线装置并定位 P_X5071A。"""
import json
import re
import subprocess

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
IEDLIST = r"D:\研发\1U装置稳定性测试\免配置\子站scd\..\IEDList_local.xml"

import paramiko, os

# 本地没有 IEDList，改用子站顺序：从 cfg 已确认与台账一致，直接内联读取工况值即可
HOST = "192.168.0.10"


def run(cmds, timeout=120):
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    p = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                       timeout=timeout, encoding="utf-8", errors="replace")
    return (p.stdout or "").splitlines()


conn = {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 45000}
lines = run([conn, {"cmd": "read_comm_state", "timeoutMs": 60000}, {"cmd": "quit"}], 120)
for ln in lines:
    if '"comm_state"' in ln:
        obj = json.loads(ln)
        entries = obj["entries"]
        on = [e["idx"] for e in entries if e["stVal"]]
        off = [e["idx"] for e in entries if not e["stVal"]]
        print("total:", len(entries), "online:", len(on), "offline:", len(off))
        print("P_X5071A idx 156 ->", entries[155]["stVal"], "| idx 157 ->", entries[156]["stVal"])
        print("first 20 online idx:", on[:20])
        print("offline idx (first 30):", off[:30])
        break
