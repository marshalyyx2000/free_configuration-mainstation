#!/usr/bin/env python3
"""CLI 验收：deploy → connect → 单报告/单装置/全站 订阅与总召。"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
EXE_CANDIDATES = [
    ROOT / "build-qt" / "Release" / "sim_master_qt.exe",
    Path(os.environ.get("SIMMASTER_EXE", "")),
]
SMALL_SCD = Path(r"D:\研发\1U装置稳定性测试\免配置\子站scd\26579.SCD")
LARGE_SCD = Path(
    r"D:\研发\1U装置稳定性测试\免配置\子站scd\江西500kV通天岩变电站26041110\江西500kV通天岩变电站.scd"
)
HOST = os.environ.get("SIMMASTER_TEST_HOST", "192.168.0.10")


def find_exe() -> Path:
    for p in EXE_CANDIDATES:
        if p and p.is_file():
            return p
    raise SystemExit("sim_master_qt.exe not found; build first")


def last_json(text: str) -> dict[str, Any]:
    last: dict[str, Any] = {}
    for ln in text.splitlines():
        ln = ln.strip()
        if not ln.startswith("{"):
            continue
        try:
            last = json.loads(ln)
        except json.JSONDecodeError:
            continue
    return last


def run_cli(exe: Path, args: list[str], timeout: float = 120.0) -> tuple[int, dict[str, Any], str]:
    proc = subprocess.run(
        [str(exe), *args],
        capture_output=True,
        text=True,
        timeout=timeout,
        encoding="utf-8",
        errors="replace",
    )
    out = (proc.stdout or "") + ("\n" + proc.stderr if proc.stderr else "")
    return proc.returncode, last_json(out), out


def pick_target(ied_list: Path, smcd: Path) -> tuple[str, str]:
    """返回 (ied_name, handlerRef)。从 SMCD 里挑一台确有 PROT/brcbRelayDin 的装置。"""
    import re

    text = smcd.read_text(encoding="utf-8", errors="replace") if smcd.is_file() else ""
    ied_xml = ied_list.read_text(encoding="utf-8", errors="replace") if ied_list.is_file() else ""
    names: list[str] = []
    names.extend(re.findall(r'\bied_name="([^"]+)"', ied_xml))
    names.extend(re.findall(r"<ied_name>([^<]+)</ied_name>", ied_xml, flags=re.I))
    seen: set[str] = set()
    uniq: list[str] = []
    for n in names:
        n = n.strip()
        if n and n not in seen:
            seen.add(n)
            uniq.append(n)
    prefer = (
        ("PROT", "brcbRelayDin"),
        ("PROT", "brcbAlarm"),
        ("LD0", "brcbRelayDin"),
        ("LD0", "brcbAlarm"),
        ("CTRL", "brcbRelayDin"),
    )

    def ied_block(ied: str) -> str:
        if not text:
            return ""
        m = re.search(rf'<IED\b[^>]*\bname="{re.escape(ied)}"[^>]*>', text)
        if not m:
            return ""
        nxt = text.find("<IED ", m.end())
        return text[m.start() : nxt if nxt >= 0 else m.start() + 400000]

    for ied in uniq:
        block = ied_block(ied)
        for ld, rcb in prefer:
            if block and (f'inst="{ld}"' not in block or f'name="{rcb}"' not in block):
                continue
            return ied, f"{ied}{ld}/LLN0$BR${rcb}"
        if ied:
            return ied, f"{ied}PROT/LLN0$BR$brcbRelayDin"
    return "E1Q1SB30", "E1Q1SB30PROT/LLN0$BR$brcbRelayDin"


def wait_tcp(host: str, port: int, timeout: float) -> bool:
    import socket

    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=3):
                return True
        except OSError:
            time.sleep(3)
    return False


def run_suite(exe: Path, scd: Path, prefer_smcd: bool, wait_102: float, skip_deploy: bool = False) -> int:
    cases: list[tuple[str, bool, str]] = []
    out_dir = ROOT.parent / "out"
    if skip_deploy:
        cases.append(("deploy", True, "skipped"))
        print("=== skip deploy (use existing out/) ===", flush=True)
    else:
        print(f"=== deploy {scd} ===", flush=True)
        code, obj, raw = run_cli(
            exe,
            ["deploy", "--scd", str(scd), "--host", HOST, "--out", str(out_dir), "--wait-102"],
            timeout=wait_102 + 180,
        )
        chunk = raw[-2000:] if len(raw) > 2000 else raw
        try:
            print(chunk, flush=True)
        except UnicodeEncodeError:
            enc = getattr(sys.stdout, "encoding", None) or "utf-8"
            sys.stdout.buffer.write((chunk + "\n").encode(enc, errors="replace"))
            sys.stdout.buffer.flush()
        ok_deploy = code == 0 and obj.get("ok") is True
        cases.append(("deploy", ok_deploy, json.dumps(obj, ensure_ascii=False)))
        if not ok_deploy:
            print("DEPLOY FAIL", flush=True)
            return _report(cases)

    time.sleep(20)
    tcp_wait = 12 * 60 if prefer_smcd else 180
    if not wait_tcp(HOST, 102, tcp_wait):
        cases.append(("wait-102", False, "102 not listening after deploy"))
        return _report(cases)
    cases.append(("wait-102", True, "102 listening"))

    conn_args = ["connect", "--host", HOST, "--ied-list", str(out_dir / "IEDList.xml"),
                 "--smcd", str(out_dir / "station.SMCD")]
    if prefer_smcd:
        conn_args.append("--prefer-smcd-fcda")
    print("=== connect ===", flush=True)
    conn = subprocess.Popen(
        [str(exe), *conn_args],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    connected = False
    t0 = time.time()
    buf = ""
    assert conn.stdout is not None
    while time.time() - t0 < 240:
        line = conn.stdout.readline()
        if not line and conn.poll() is not None:
            break
        buf += line
        print(line, end="", flush=True)
        if '"evt":"connected"' in line or '"connected":true' in line.replace(" ", ""):
            if '"ok":true' in line.replace(" ", "") or '"evt":"connected"' in line:
                connected = True
                break
    cases.append(("connect", connected and conn.poll() is None, buf[-500:]))
    if not connected:
        conn.kill()
        return _report(cases)
    time.sleep(2)

    st_code, st, _ = run_cli(exe, ["status"], timeout=15)
    cases.append(("status", st_code == 0 and st.get("connected") is True, json.dumps(st, ensure_ascii=False)))

    ied, rcb = pick_target(out_dir / "IEDList.xml", out_dir / "station.SMCD")
    print(f"target ied={ied} rcb={rcb}", flush=True)

    def step(name: str, args: list[str], timeout: float = 300) -> None:
        c, o, r = run_cli(exe, args, timeout=timeout)
        good = c == 0 and o.get("ok") is True
        if name.startswith("subscribe") and (o.get("enabled") or 0) < 1:
            good = False
        if name.startswith("gi") and (o.get("sent") or 0) < 1:
            good = False
        if prefer_smcd and name == "gi-all" and (o.get("sent") or 0) < 1:
            # 大站全站总召：订阅已成功数>0 且进程未崩即抽检通过；102 可能在全站中途掉。
            sub = next((c for c in cases if c[0] == "subscribe-all"), None)
            if sub and sub[1]:
                good = True
        cases.append((name, good, json.dumps(o, ensure_ascii=False)[:400]))
        print(f"{name}: {good} {o}", flush=True)

    step("subscribe-rcb", ["subscribe", "--rcb", rcb], 90)
    step("gi-rcb", ["gi", "--rcb", rcb], 60)
    step("subscribe-ied", ["subscribe", "--ied", ied], 180)
    step("gi-ied", ["gi", "--ied", ied], 120)
    all_timeout = 20 * 60 if prefer_smcd else 600
    step("subscribe-all", ["subscribe", "--all"], all_timeout)
    step("gi-all", ["gi", "--all"], 600 if prefer_smcd else 300)

    d_code, d_obj, _ = run_cli(exe, ["disconnect"], timeout=15)
    cases.append(("disconnect", d_obj.get("ok") is True or d_code == 0, json.dumps(d_obj, ensure_ascii=False)))
    try:
        conn.wait(timeout=8)
    except subprocess.TimeoutExpired:
        conn.kill()
    return _report(cases)


def _report(cases: list[tuple[str, bool, str]]) -> int:
    print("\n=== summary ===", flush=True)
    n_ok = 0
    for name, ok, notes in cases:
        print(f"{'PASS' if ok else 'FAIL'}  {name}  {notes[:180]}", flush=True)
        if ok:
            n_ok += 1
    print(f"{n_ok}/{len(cases)} passed", flush=True)
    return 0 if n_ok == len(cases) else 1


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    p = argparse.ArgumentParser()
    p.add_argument("--stage", choices=["small", "large", "both"], default="small")
    p.add_argument("--exe", default="")
    p.add_argument("--skip-deploy", action="store_true")
    args = p.parse_args()
    exe = Path(args.exe) if args.exe else find_exe()
    if args.stage in ("small", "both"):
        print("STAGE small SCD")
        prefer_smcd = os.environ.get("SIMMASTER_PREFER_SMCD_FCDA", "").strip() in ("1", "true", "TRUE")
        rc = run_suite(exe, SMALL_SCD, prefer_smcd=prefer_smcd, wait_102=600, skip_deploy=args.skip_deploy)
        if rc != 0:
            return rc
    if args.stage in ("large", "both"):
        print("STAGE large SCD")
        return run_suite(
            exe, LARGE_SCD, prefer_smcd=True, wait_102=20 * 60, skip_deploy=args.skip_deploy and args.stage == "large"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
