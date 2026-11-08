"""检查 P_L5031A 在线状态与订阅实际错误。"""
import json
import re
import subprocess

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"
IED = "P_L5031A"


def run(cmds, timeout=120):
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    p = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                       timeout=timeout, encoding="utf-8", errors="replace")
    return (p.stdout or "").splitlines()


conn = {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 45000}

# 台账顺序：从子站 cfg 已知 dsCommState 顺序 = IEDList 顺序 + LD0
lines = run([conn, {"cmd": "read_comm_state", "timeoutMs": 60000}, {"cmd": "quit"}], 120)
for ln in lines:
    if '"comm_state"' in ln:
        entries = json.loads(ln)["entries"]
        print("entries:", len(entries))
        break

# 读取台账顺序
import os
from pathlib import Path

# 用子站顺序不可直接得，这里用已知 idx：先打印前 5 与 P_L5031A 相关
print("hint: need IEDList order; reading via ssh-less approach skipped")
