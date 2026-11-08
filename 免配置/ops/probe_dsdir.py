"""读取子站 DataSet 目录，确认域/数据集名是否可访问。"""
import json
import subprocess
import sys

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"

CASES = [
    ("P_X5071APROT", "P_X5071APROT/LLN0$dsWarning"),
    ("P_X5071APROT", "P_X5071APROT/LLN0$dsRelayDin1"),
    ("P_B5013APROT", "P_B5013APROT/LLN0$dsWarning"),
    ("P_B5013ALD0", "P_B5013ALD0/LLN0$dsAlarm"),
]

conn = {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 60000}
for ld, ref in CASES:
    print("==", ref)
    payload = "\n".join(json.dumps(c) for c in [
        conn,
        {"cmd": "get_datasets", "ld": ld, "timeoutMs": 60000},
        {"cmd": "get_dataset_directory", "ref": ref, "timeoutMs": 60000},
        {"cmd": "quit"},
    ]) + "\n"
    p = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                       timeout=200, encoding="utf-8", errors="replace")
    for ln in (p.stdout or "").splitlines():
        print("   ", ln[:260])
