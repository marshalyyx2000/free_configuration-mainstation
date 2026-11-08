"""探测子站模型域：LD 列表里是否存在 SubStation* / LD0，以及 dsCommState 数据集。"""
import json
import subprocess
import sys

CLI = r"D:\研发\1U装置稳定性测试\免配置\master_sim\bin\mms_master_cli.exe"
HOST = "192.168.0.10"


def run(cmds, timeout=180):
    payload = "\n".join(json.dumps(c) for c in cmds) + "\n"
    p = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                       timeout=timeout, encoding="utf-8", errors="replace")
    return (p.stdout or "").splitlines()


conn = {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 60000}
lines = run([conn, {"cmd": "get_ld_list", "timeoutMs": 120000}, {"cmd": "quit"}], 200)
for ln in lines:
    if '"evt":"ld_list"' in ln:
        obj = json.loads(ln)
        devs = obj["devices"]
        print("total lds:", len(devs))
        print("substation-ish:", [d for d in devs if "Sub" in d or d in ("", "LD0")])
        print("first 12:", devs[:12])
        break
else:
    print("\n".join(lines[:6]))
