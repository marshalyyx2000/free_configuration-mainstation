"""验证子站：数据集读取 + RCB 订阅 + GI + 取消订阅（分步、长超时）。"""
import json
import subprocess
import sys
import time

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"
LD = "P_X5071APROT"


def run(cmds, timeout=180):
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    t0 = time.time()
    p = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                       timeout=timeout, encoding="utf-8", errors="replace")
    lines = (p.stdout or "").splitlines()
    print("  elapsed %.1fs rc=%d" % (time.time() - t0, p.returncode))
    for ln in lines:
        print("   ", ln[:300])


conn = {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 60000}

print("== read dsRelayDin1")
run([conn, {"cmd": "read_dataset", "ref": LD + "/LLN0$dsRelayDin1", "timeoutMs": 90000},
     {"cmd": "quit"}], 150)

print("== read dsWarning")
run([conn, {"cmd": "read_dataset", "ref": LD + "/LLN0$dsWarning", "timeoutMs": 90000},
     {"cmd": "quit"}], 150)

print("== enable_rcb brcbWarning01")
run([conn,
     {"cmd": "enable_rcb",
      "handlerRef": LD + "/LLN0$BR$brcbWarning",
      "rcbGetRef": LD + "/LLN0$BR$brcbWarning01",
      "dsRef": LD + "/LLN0$dsWarning",
      "timeoutMs": 90000},
     {"cmd": "gi", "handlerRef": LD + "/LLN0$BR$brcbWarning"},
     {"cmd": "disable_rcb", "handlerRef": LD + "/LLN0$BR$brcbWarning"},
     {"cmd": "quit"}], 200)
