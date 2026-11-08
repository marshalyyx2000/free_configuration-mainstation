"""严格验证订阅：同一装置重复多次，检查 rcb_enabled 与报告值是否真实。"""
import json
import subprocess
import time

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"


def run(cmds, timeout=120):
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    p = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                       timeout=timeout, encoding="utf-8", errors="replace")
    return (p.stdout or "").splitlines()


conn = {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 45000}
CASE = ("C_B5043XLD0/LLN0$BR$brcbAlarm", "C_B5043XLD0/LLN0$dsAlarm")

for attempt in range(3):
    handler, ds = CASE
    lines = run([conn,
                 {"cmd": "enable_rcb", "handlerRef": handler, "rcbGetRef": handler + "01",
                  "dsRef": ds, "timeoutMs": 30000},
                 {"cmd": "gi", "handlerRef": handler},
                 {"cmd": "quit"}], 120)
    print("=== attempt", attempt + 1)
    for ln in lines:
        if any(k in ln for k in ('"rcb_enabled"', '"error"', '"ok"', '"report"')):
            print("   ", ln[:400])
    time.sleep(2)
