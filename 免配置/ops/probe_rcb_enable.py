"""RCB 订阅实测：对在线装置 C_B5043XLD0 启用 brcbAlarm01 + GI + 取消。"""
import json
import subprocess

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"

HANDLER = "C_B5043XLD0/LLN0$BR$brcbAlarm"
GETREF = "C_B5043XLD0/LLN0$BR$brcbAlarm01"
DSREF = "C_B5043XLD0/LLN0$dsAlarm"


def run(cmds, timeout=150):
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    t = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                       timeout=timeout, encoding="utf-8", errors="replace")
    return (t.stdout or "").splitlines()


conn = {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 45000}
lines = run([
    conn,
    {"cmd": "enable_rcb", "handlerRef": HANDLER, "rcbGetRef": GETREF,
     "dsRef": DSREF, "timeoutMs": 60000},
    {"cmd": "gi", "handlerRef": HANDLER},
    {"cmd": "disable_rcb", "handlerRef": HANDLER},
    {"cmd": "quit"},
], 180)

for ln in lines:
    print(ln[:260])
