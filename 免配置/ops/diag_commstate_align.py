#!/usr/bin/env python3
"""对照 IEDList 顺序、supervision.cfg dsCommState DE() 顺序，并读 MMS stVal。"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys

CLI = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "master_sim", "bin", "mms_master_cli.exe")
HOST = os.environ.get("SUBST_HOST", "192.168.0.10")


def run_cli(cmds, timeout=60):
    payload = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    p = subprocess.run([CLI], input=payload, capture_output=True, text=True,
                       timeout=timeout, encoding="utf-8", errors="replace")
    return (p.stdout or "").splitlines()


def main() -> int:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from remote import run as ssh_run

    print("=== IEDList vs cfg dsCommState ===")
    out = ssh_run(
        "python3 - <<'PY'\n"
        "import re\n"
        "xml=open('/home/ipofas/stdown/maintain/CONF/IEDList.xml',encoding='utf-8',errors='replace').read()\n"
        "devs=re.findall(r'ied_name=\"([^\"]+)\"', xml)\n"
        "cfg=open('/home/admin/router/config/supervision.cfg',encoding='utf-8',errors='replace').read()\n"
        "m=re.search(r'LD\\(SubStationLD0\\)\\{.*?DS\\(dsCommState\\)\\{(.*?)\\}', cfg, re.S)\n"
        "fcdas=re.findall(r'DE\\(([^/]+)/LPHD1\\$ST\\$Proxy\\);', m.group(1)) if m else []\n"
        "print('IEDList', len(devs), 'dsCommState', len(fcdas))\n"
        "n=min(len(devs), len(fcdas))\n"
        "mismatch=0\n"
        "for i in range(n):\n"
        "    expect=devs[i]+'LD0'\n"
        "    ok=fcdas[i]==expect\n"
        "    if not ok:\n"
        "        mismatch+=1\n"
        "        if mismatch<=12:\n"
        "            print('MISMATCH', i+1, 'ied', devs[i], 'fcda', fcdas[i])\n"
        "print('order_mismatch', mismatch, '/', n)\n"
        "print('first5_ied', ','.join(devs[:5]))\n"
        "print('first5_fcda', ','.join(fcdas[:5]))\n"
        "PY"
    )
    print(out)

    print("=== MMS read_comm_state ===")
    true_n = 0
    named = 0
    for ln in run_cli([
        {"cmd": "connect", "host": HOST, "port": 102, "timeoutMs": 45000},
        {"cmd": "read_comm_state", "timeoutMs": 30000},
        {"cmd": "quit"},
    ], 90):
        if '"comm_state"' not in ln:
            continue
        evt = json.loads(ln)
        entries = evt.get("entries") or []
        for e in entries:
            if e.get("stVal"):
                true_n += 1
            if e.get("ied"):
                named += 1
        print("entries", len(entries), "stVal_true", true_n, "with_ied", named)
        trues = [e for e in entries if e.get("stVal")]
        print("true sample:", [(e.get("idx"), e.get("ied") or e.get("ld")) for e in trues[:15]])
        false_ieds = [e.get("ied") for e in entries if not e.get("stVal") and e.get("ied")]
        true_ieds = [e.get("ied") for e in entries if e.get("stVal") and e.get("ied")]
        print("=== TCP / 连接日志 vs stVal ===")
        tcp_out = ssh_run(
            "python3 - <<'PY'\n"
            "import re, os, subprocess\n"
            "xml=open('/home/ipofas/stdown/maintain/CONF/IEDList.xml',encoding='utf-8',errors='replace').read()\n"
            "names=re.findall(r'ied_name=\"([^\"]+)\"', xml)\n"
            "ips=re.findall(r'ch_para=\"([^\"]+)\"', xml)\n"
            "devs=list(zip(names, ips))\n"
            "ss=subprocess.check_output('ss -tn 2>/dev/null || netstat -tn', shell=True).decode('utf-8','replace')\n"
            "tcp=set()\n"
            "for n,ip in devs:\n"
            "    if ip and ip in ss:\n"
            "        tcp.add(n)\n"
            "print('tcp_ied', len(tcp))\n"
            "print('tcp_C', sum(1 for n in tcp if n.startswith('C_')))\n"
            "print('tcp_P', sum(1 for n in tcp if n.startswith('P_')))\n"
            "print('tcp_sample', ','.join(sorted(tcp)[:15]))\n"
            "log='/home/admin/router/logs/iec61850Connection.log'\n"
            "size=os.path.getsize(log)\n"
            "f=open(log, errors='replace'); f.seek(max(0,size-3000000)); text=f.read(); f.close()\n"
            "print('connected_to_lines', text.lower().count('connected to'))\n"
            "print('updated_common_state', text.count('Updated common state'))\n"
            "print('could_not_find_common', text.count('Could not find common state'))\n"
            "PY"
        )
        print(tcp_out)
        print("stVal_true", len(true_ieds), "stVal_false", len(false_ieds))
        break
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
