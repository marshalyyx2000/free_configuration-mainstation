"""对比三种订阅场景，判断失败原因：
1) 本地合并模型 RCB（SubStationLD0）
2) 在线装置 RCB
3) 离线装置 RCB
"""
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
    ("local SubStation", "SubStationLD0/LLN0$BR$brcbCommState",
     "SubStationLD0/LLN0$BR$brcbCommState01", "SubStationLD0/LLN0$dsCommState"),
    ("online C_B5043X", "C_B5043XLD0/LLN0$BR$brcbAlarm",
     "C_B5043XLD0/LLN0$BR$brcbAlarm01", "C_B5043XLD0/LLN0$dsAlarm"),
    ("offline CMCK3502", "CMCK3502LD0/LLN0$BR$brcbAlarm",
     "CMCK3502LD0/LLN0$BR$brcbAlarm01", "CMCK3502LD0/LLN0$dsAlarm"),
]

for label, handler, getref, dsref in CASES:
    print("=== ", label, handler)
    lines = run([conn,
                 {"cmd": "enable_rcb", "handlerRef": handler, "rcbGetRef": getref,
                  "dsRef": dsref, "timeoutMs": 30000},
                 {"cmd": "quit"}], 90)
    for ln in lines:
        if any(k in ln for k in ('"rcb_enabled"', '"error"', '"report"')):
            print("   ", ln[:200])
