"""用 rptno=9 推出的实例 09 实测 P_L5031A 订阅（对照 01）。"""
import json
import subprocess

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"


def run(cmds, timeout=120):
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    p = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                       timeout=timeout, encoding="utf-8", errors="replace")
    return (p.stdout or "").splitlines()


conn = {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 45000}
CASES = [
    ("LD0 brcbAlarm inst01", "P_L5031ALD0/LLN0$BR$brcbAlarm", "01", "P_L5031ALD0/LLN0$dsAlarm"),
    ("LD0 brcbAlarm inst09", "P_L5031ALD0/LLN0$BR$brcbAlarm", "09", "P_L5031ALD0/LLN0$dsAlarm"),
    ("PROT brcbWarning inst09", "P_L5031APROT/LLN0$BR$brcbWarning", "09", "P_L5031APROT/LLN0$dsWarning"),
    ("PROT brcbTripInfo inst09", "P_L5031APROT/LLN0$BR$brcbTripInfo", "09", "P_L5031APROT/LLN0$dsTripInfo"),
]

for label, handler, inst, ds in CASES:
    lines = run([conn,
                 {"cmd": "enable_rcb", "handlerRef": handler, "rcbGetRef": handler + inst,
                  "dsRef": ds, "timeoutMs": 30000},
                 {"cmd": "gi", "handlerRef": handler},
                 {"cmd": "disable_rcb", "handlerRef": handler},
                 {"cmd": "quit"}], 100)
    res = [ln for ln in lines if any(k in ln for k in ('"rcb_enabled"', '"error"', '"ok"', '"report"'))]
    print("==", label)
    for ln in res[:4]:
        print("   ", ln[:200])
