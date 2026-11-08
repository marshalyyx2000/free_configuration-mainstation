"""修复后验证：连接、LD 列表、通信工况、单 RCB 订阅+总召。"""
import json
import subprocess
import time

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"

cmds = [
    {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 45000},
    {"cmd": "get_ld_list"},
    {"cmd": "read_comm_state"},
    {"cmd": "enable_rcb",
     "handlerRef": "C_B5043XLD0/LLN0$BR$brcbAlarm",
     "rcbGetRef": "C_B5043XLD0/LLN0$BR$brcbAlarm01",
     "dsRef": "C_B5043XLD0/LLN0$dsAlarm", "timeoutMs": 30000},
    {"cmd": "gi", "handlerRef": "C_B5043XLD0/LLN0$BR$brcbAlarm"},
    {"cmd": "quit"},
]
payload = "\n".join(json.dumps(c) for c in cmds) + "\n"
t0 = time.time()
p = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                   timeout=180, encoding="utf-8", errors="replace")
print("rc", p.returncode, "wall", round(time.time() - t0, 1))
for ln in p.stdout.splitlines():
    print(ln[:600])
if p.stderr.strip():
    print("STDERR", p.stderr[:500])
