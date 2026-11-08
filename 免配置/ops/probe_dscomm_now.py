"""定位 dsCommState 的 MMS 域名与数据集名。"""
import json
import subprocess

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"

cmds = [
    {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 45000},
    {"cmd": "get_ld_list"},
    {"cmd": "get_datasets", "ld": "SubStationLD0"},
    {"cmd": "get_datasets", "ld": "LD0"},
    {"cmd": "get_datasets", "ld": "C_B5043XLD0"},
    {"cmd": "read_dataset", "ref": "SubStationLD0/LLN0$dsCommState"},
    {"cmd": "read_dataset", "ref": "LD0/LLN0$dsCommState"},
    {"cmd": "quit"},
]
payload = "\n".join(json.dumps(c) for c in cmds) + "\n"
p = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                   timeout=120, encoding="utf-8", errors="replace")
for ln in p.stdout.splitlines():
    print(ln[:1200])
