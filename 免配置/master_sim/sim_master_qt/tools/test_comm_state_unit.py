#!/usr/bin/env python3
"""工况对象引用 / 跳过订阅 单元测试（不依赖 102）。可选 CLI 实读。"""
from __future__ import annotations

import json
import os
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT.parent / "native"
MIAN_PEI = ROOT.parent.parent
ICD_26579 = MIAN_PEI / "SubStation_26579.icd"
IEDLIST = ROOT.parent / "out" / "IEDList.xml"
CLI_CANDIDATES = [
    NATIVE / "build" / "Release" / "mms_master_cli.exe",
    ROOT / "build-qt" / "Release" / "mms_master_cli.exe",
    ROOT.parent / "bin" / "mms_master_cli.exe",
]
TEST_EXE_CANDIDATES = [
    NATIVE / "build" / "Release" / "test_comm_state_refs.exe",
    NATIVE / "build" / "Debug" / "test_comm_state_refs.exe",
]
HOST = os.environ.get("SIMMASTER_TEST_HOST", "192.168.0.10")
PORT = int(os.environ.get("SIMMASTER_TEST_PORT", "102"))
SMCD = Path(os.environ.get("SIM_MASTER_SMCD") or "")
if not SMCD.is_file():
    SMCD = ROOT.parent / "out" / "station.SMCD"
    if not SMCD.is_file():
        SMCD = Path(os.environ.get("LOCALAPPDATA", "")) / "SimMasterQt" / "out" / "station.SMCD"

g_fail = 0


def expect(cond: bool, name: str, detail: str = "") -> None:
    global g_fail
    if cond:
        print(f"PASS  {name}")
        return
    g_fail += 1
    extra = f"  {detail}" if detail else ""
    print(f"FAIL  {name}{extra}")


def ied_from_ld(ld: str) -> str:
    for suf in ("PIGO", "RPIT", "PROT", "MEAS", "CTRL", "RCD", "LD0"):
        if len(ld) > len(suf) and ld.endswith(suf):
            return ld[: -len(suf)]
    return ld


def should_skip_offline(comm: dict[str, bool], aligned: bool, ied: str) -> bool:
    if not aligned or not ied or ied not in comm:
        return False
    if not any(comm.values()):
        return False
    return not comm[ied]


def dollar_unsafe(obj: str) -> bool:
    if "$" not in obj:
        return False
    slash = obj.find("/")
    item = obj[slash + 1 :] if slash >= 0 else obj
    parts = item.split("$")
    if len(parts) >= 2 and len(parts[1]) == 2 and parts[1].isalpha():
        return False
    return True


def test_mapping() -> None:
    expect(ied_from_ld("E1Q1SB31LD0") == "E1Q1SB31", "py ied LD0")
    expect(ied_from_ld("E1Q1SB31PROT") == "E1Q1SB31", "py ied PROT")
    dotted = "E1Q1SB31LD0/LPHD1.Proxy.stVal"
    bad = "E1Q1SB20LD0/LPHD1$Proxy$stVal"
    good_mms = "E1Q1SB31LD0/LPHD1$ST$Proxy$stVal"
    expect("$" not in dotted, "dotted has no dollar")
    expect(dollar_unsafe(bad), "wireshark form is unsafe with FC_ST")
    expect(not dollar_unsafe(dotted), "dotted is safe")
    expect(not dollar_unsafe(good_mms), "embedded $ST$ is safe with FC_NONE")
    expect("stVal$ST" in "LPHD1$Proxy$stVal$ST", "document GetDataValue Failure form")
    com = "SubStationLD0/E1Q1SB31SDDI12.ComStatus.stVal"
    expect("ComStatus" in com and "LPHD" not in com, "ComStatus object ref")
    expect("$" not in com, "ComStatus dotted has no dollar")


def test_icd_26579() -> None:
    expect(ICD_26579.is_file(), f"SubStation_26579.icd 存在 {ICD_26579}")
    expect(IEDLIST.is_file(), f"IEDList.xml 存在 {IEDLIST}")
    if not ICD_26579.is_file() or not IEDLIST.is_file():
        return
    icd = ICD_26579.read_text(encoding="utf-8")
    expect('name="dsCommState"' in icd, "ICD 含 dsCommState")
    expect("SUBSTATION_SDDI_V2.00" in icd, "ICD 含 SDDI 类型")
    expect('IED name="SubStation"' in icd, "ICD IED 名为 SubStation")
    expect("PL1001" not in icd, "ICD 不含云南站 prefix")
    expect("dsParameter" in icd and "dsSelfChk" in icd, "保留 dsParameter/dsSelfChk")
    ds = re.search(r'<DataSet\b[^>]*name="dsCommState"[^>]*>(.*?)</DataSet>', icd, re.S)
    expect(ds is not None, "能切开 dsCommState")
    prefixes: list[tuple[str, int]] = []
    if ds:
        for attrs in re.findall(r"<FCDA\b([^>]*)/?>", ds.group(1)):
            d = dict(re.findall(r'(\w+)="([^"]*)"', attrs))
            if d.get("doName") == "ComStatus" and d.get("prefix"):
                prefixes.append((d["prefix"], int(d.get("lnInst") or "0")))
    expect(len(prefixes) == 30, f"dsCommState 30 条 ComStatus，got {len(prefixes)}")
    from gen_substation_icd import parse_iedlist

    rows = parse_iedlist(IEDLIST)
    expect(len(rows) == 30, f"IEDList 30 台 got {len(rows)}")
    if prefixes and rows:
        expect(prefixes[0][0] == rows[0]["ied_name"], "首条 prefix=IEDList no=1")
        expect(prefixes[-1][0] == rows[-1]["ied_name"], "末条 prefix=IEDList 最后一台")
        mapped = {p: n for p, n in prefixes}
        for row in rows:
            expect(row["ied_name"] in mapped, f"IED {row['ied_name']} 在 dsCommState")
            if row["ied_name"] in mapped:
                expect(mapped[row["ied_name"]] == row["no"], f"{row['ied_name']} lnInst={row['no']}")
        expect(mapped.get("E1Q1SB31") == 12, "E1Q1SB31 lnInst=12")
        expect('prefix="E1Q1SB31" inst="12"' in icd, "SDDI LN E1Q1SB31 inst=12")


def test_skip() -> None:
    all_off = {f"E1Q1SB{i}": False for i in range(20, 50)}
    expect(not should_skip_offline(all_off, True, "E1Q1SB31"), "合=0 不跳过保护装置")
    expect(not should_skip_offline(all_off, True, "E1Q1SB20"), "合=0 不跳过测控")
    mixed = dict(all_off)
    mixed["E1Q1SB20"] = True
    expect(should_skip_offline(mixed, True, "E1Q1SB31"), "有合时跳过明确为分的保护")
    expect(not should_skip_offline(mixed, True, "E1Q1SB20"), "有合时不跳过在线测控")
    expect(not should_skip_offline(mixed, False, "E1Q1SB31"), "未对齐不跳过")
    expect(not should_skip_offline({}, True, "E1Q1SB31"), "无快照不跳过")
    invalid_missing = {"E1Q1SB20": True}
    expect(not should_skip_offline(invalid_missing, True, "E1Q1SB31"), "读失败未入表不跳过")


def find_exe(cands: list[Path]) -> Path | None:
    for p in cands:
        if p.is_file():
            return p
    return None


def test_c_exe() -> None:
    exe = find_exe(TEST_EXE_CANDIDATES)
    expect(exe is not None, "test_comm_state_refs.exe 已编译", str(TEST_EXE_CANDIDATES[0]))
    if not exe:
        return
    proc = subprocess.run([str(exe)], capture_output=True, text=True, encoding="utf-8", errors="replace")
    print(proc.stdout)
    if proc.stderr:
        print(proc.stderr)
    expect(proc.returncode == 0, "C 工况引用单测 exit=0")


def port_open(host: str, port: int, timeout: float = 2.0) -> bool:
    s = socket.socket()
    s.settimeout(timeout)
    try:
        s.connect((host, port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def test_cli_live() -> None:
    if os.environ.get("SIMMASTER_SKIP_LIVE", "").strip() in ("1", "true", "TRUE"):
        print("SKIP  CLI live (SIMMASTER_SKIP_LIVE)")
        return
    cli = find_exe(CLI_CANDIDATES)
    if not cli:
        print("SKIP  CLI live (no mms_master_cli.exe)")
        return
    if not port_open(HOST, PORT):
        print(f"SKIP  CLI live ({HOST}:{PORT} 未监听)")
        return
    cmds = [
        {"cmd": "connect", "host": HOST, "port": PORT, "timeoutMs": 30000},
        {
            "cmd": "read_comm_state",
            "ref": "SubStationLD0/LLN0$dsCommState",
            "preferSmcdFcda": True,
            "timeoutMs": 120000,
        },
        {"cmd": "quit"},
    ]
    if SMCD.is_file():
        cmds[1]["smcd"] = str(SMCD)
    stdin = "\n".join(json.dumps(c, ensure_ascii=False) for c in cmds) + "\n"
    proc = subprocess.run(
        [str(cli)],
        input=stdin,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=180,
    )
    events = []
    for ln in (proc.stdout or "").splitlines():
        ln = ln.strip()
        if not ln.startswith("{"):
            continue
        try:
            events.append(json.loads(ln))
        except json.JSONDecodeError:
            continue
    cs = next((e for e in events if e.get("evt") == "comm_state"), None)
    expect(cs is not None, "CLI 读到 comm_state 事件")
    if not cs:
        print((proc.stdout or "")[-800:])
        return
    entries = cs.get("entries") or []
    expect(len(entries) >= 1, f"工况条目数 {len(entries)}")
    valid = [e for e in entries if e.get("valid") is True]
    prot = [
        e
        for e in entries
        if str(e.get("ied") or "").startswith("E1Q1SB")
        and str(e.get("ied"))[6:].isdigit()
        and int(str(e.get("ied"))[6:] or 0) >= 29
    ]
    prot_valid = [e for e in prot if e.get("valid") is True]
    objs = [str(e.get("obj") or "") for e in entries]
    expect(not any("stVal$ST" in o or o.endswith("$ST") for o in objs if o), "obj 不含 $stVal$ST")
    expect(not any(dollar_unsafe(o) for o in objs if o), "obj 不是 LPHD1$Proxy$stVal 形式")
    expect(any("ComStatus" in o for o in objs), "obj 含 ComStatus")
    expect(not any("LPHD" in o and "Proxy" in o for o in objs), "obj 不再走 LPHD.Proxy")
    expect(len(valid) > 0, f"至少一条 valid=true（got {len(valid)}/{len(entries)}）")
    if prot:
        expect(len(prot_valid) > 0, f"保护装置工况可读 valid={len(prot_valid)}/{len(prot)}")
        expect(all("ComStatus" in str(e.get("obj") or "") for e in prot), "保护装置 obj 为 ComStatus")
    on = sum(1 for e in valid if e.get("stVal") is True)
    print(
        f"  source={cs.get('source')} entries={len(entries)} valid={len(valid)} "
        f"合={on} 保护={len(prot_valid)}/{len(prot)}"
    )


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    test_mapping()
    test_icd_26579()
    test_skip()
    test_c_exe()
    test_cli_live()
    print(f"\n{'ALL PASSED' if g_fail == 0 else 'FAILED'}  failures={g_fail}")
    return 0 if g_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
