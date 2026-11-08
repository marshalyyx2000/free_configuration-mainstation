"""对 3 台在线装置各启用 1 个 LD0 事件 RCB 并总召，确认不是单台偶然。"""
import json
import os
import re
import subprocess

import paramiko

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"
DEVICES = ["C_B5031X", "C_L5013X", "P_L5033A"]

c = paramiko.SSHClient()
c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect(HOST, username="admin", password=os.environ["SUBST_PASS"], timeout=15,
          allow_agent=False, look_for_keys=False)
sftp = c.open_sftp()
with sftp.open("/home/admin/router/config/supervision.cfg", "rb") as fh:
    text = fh.read().decode("utf-8", "replace")
sftp.close(); c.close()

rct = re.compile(r"^RC\((\w+)\s+(\S+)\s+(\d)\s+(\S+)\s", re.M)
cases = []
for dev in DEVICES:
    for m in rct.finditer(text):
        name, ref, buffered, ds = m.group(1), m.group(2), m.group(3), m.group(4)
        if not ref.startswith(dev + "LD0/") or "$BR$" not in ref:
            continue
        inst = name[-2:] if name[-2:].isdigit() else "01"
        ld = ref.split("/", 1)[0]
        cases.append((ref, ref + inst, ld + "/LLN0$" + ds))
        break
print("cases", cases)

cmds = [{"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 45000}]
for handler, rcb, ds in cases:
    cmds.append({"cmd": "enable_rcb", "handlerRef": handler, "rcbGetRef": rcb,
                 "dsRef": ds, "timeoutMs": 20000})
for handler, _, _ in cases:
    cmds.append({"cmd": "gi", "handlerRef": handler})
cmds.append({"cmd": "quit"})
p = subprocess.run([CLI], input="\n".join(json.dumps(c) for c in cmds) + "\n",
                   capture_output=True, text=True, timeout=120, encoding="utf-8", errors="replace")
for ln in p.stdout.splitlines():
    print(ln[:500])
