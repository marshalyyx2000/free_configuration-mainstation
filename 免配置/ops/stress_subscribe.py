"""批量订阅压力测试：从子站 supervision.cfg 取 N 个 LD0 事件 RCB，逐个 enable+GI，
再读通信工况，确认 router 不会再次 100% CPU/无响应。"""
import json
import os
import re
import subprocess
import time

import paramiko

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"
CFG = "/home/admin/router/config/supervision.cfg"
N = 50

c = paramiko.SSHClient()
c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect(HOST, username="admin", password=os.environ["SUBST_PASS"], timeout=15,
          allow_agent=False, look_for_keys=False)
sftp = c.open_sftp()
with sftp.open(CFG, "rb") as fh:
    text = fh.read().decode("utf-8", "replace")
sftp.close()
c.close()

rct = re.compile(r"^RC\((\w+)\s+(\S+)\s+(\d)\s+(\S+)\s", re.M)
cases = []
seen = set()
for m in rct.finditer(text):
    name, ref, buffered, ds = m.group(1), m.group(2), m.group(3), m.group(4)
    if "LD0" not in ref or "$BR$" not in ref:
        continue
    if "Ain" in ds or ref in seen:
        continue
    inst = name[-2:] if name[-2:].isdigit() else "01"
    handler = ref
    rcb = ref + inst
    ld = ref.split("/", 1)[0]
    dsref = ld + "/LLN0$" + ds
    seen.add(ref)
    cases.append((handler, rcb, dsref))
    if len(cases) >= N:
        break

print("cases", len(cases), "first", cases[:3])
cmds = [{"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 60000}]
for handler, rcb, dsref in cases:
    cmds.append({"cmd": "enable_rcb", "handlerRef": handler, "rcbGetRef": rcb,
                 "dsRef": dsref, "timeoutMs": 20000})
for handler, _, _ in cases:
    cmds.append({"cmd": "gi", "handlerRef": handler})
cmds.append({"cmd": "read_comm_state"})
cmds.append({"cmd": "quit"})

payload = "\n".join(json.dumps(x) for x in cmds) + "\n"
t0 = time.time()
p = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                   timeout=600, encoding="utf-8", errors="replace")
print("rc", p.returncode, "wall", round(time.time() - t0, 1))
lines = p.stdout.splitlines()
ok = sum(1 for ln in lines if '"rcb_enabled"' in ln)
err = [ln for ln in lines if '"error"' in ln]
print("rcb_enabled", ok, "errors", len(err))
for ln in err[:5]:
    print("  ", ln[:200])
for ln in lines:
    if '"evt":"comm_state"' in ln:
        print("comm_state ok, len", len(ln))
