"""按 cfg 实际 RCB 名实测 P_L5031A 订阅（LD0 与 PROT 两个 LD）。"""
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
    ("LD0 brcbAlarm01", "P_L5031ALD0/LLN0$BR$brcbAlarm",
     "P_L5031ALD0/LLN0$dsAlarm"),
    ("LD0 brcbRelayDin01", "P_L5031ALD0/LLN0$BR$brcbRelayDin",
     "P_L5031ALD0/LLN0$dsRelayDin"),
    ("PROT brcbWarning01", "P_L5031APROT/LLN0$BR$brcbWarning",
     "P_L5031APROT/LLN0$dsWarning"),
    ("PROT brcbTripInfo01", "P_L5031APROT/LLN0$BR$brcbTripInfo",
     "P_L5031APROT/LLN0$dsTripInfo"),
]

for label, handler, ds in CASES:
    print("==", label)
    lines = run([conn,
                 {"cmd": "enable_rcb", "handlerRef": handler, "rcbGetRef": handler + "01",
                  "dsRef": ds, "timeoutMs": 30000},
                 {"cmd": "gi", "handlerRef": handler},
                 {"cmd": "disable_rcb", "handlerRef": handler},
                 {"cmd": "quit"}], 100)
    for ln in lines:
        if any(k in ln for k in ('"rcb_enabled"', '"error"', '"ok"', '"report"')):
            print("   ", ln[:220])
