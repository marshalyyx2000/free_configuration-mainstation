"""对照实验：同一时刻对 C_B5043X（已知可订阅）与 P_L5031A 做完全相同的订阅，
确认 router 侧对两者的处理差异。同时读 router 日志确认是否有转发。"""
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
    ("C_B5043XLD0 brcbAlarm", "C_B5043XLD0/LLN0$BR$brcbAlarm", "C_B5043XLD0/LLN0$dsAlarm"),
    ("P_L5031ALD0 brcbAlarm", "P_L5031ALD0/LLN0$BR$brcbAlarm", "P_L5031ALD0/LLN0$dsAlarm"),
    ("P_L5031ALD0 brcbRelayDin", "P_L5031ALD0/LLN0$BR$brcbRelayDin", "P_L5031ALD0/LLN0$dsRelayDin"),
]

for label, handler, ds in CASES:
    lines = run([conn,
                 {"cmd": "enable_rcb", "handlerRef": handler, "rcbGetRef": handler + "01",
                  "dsRef": ds, "timeoutMs": 30000},
                 {"cmd": "quit"}], 90)
    res = [ln for ln in lines if '"rcb_enabled"' in ln or '"error"' in ln]
    print("==", label, "->", res[0][:140] if res else "none")
