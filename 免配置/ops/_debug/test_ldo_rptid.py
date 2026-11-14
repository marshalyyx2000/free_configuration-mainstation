# -*- coding: utf-8 -*-
"""校验 LD0/LDO 纠错：SMCD 消毒、Python sanitize、genconfig 输出。"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "master_sim"))
from build_config import _sanitize_scl_for_genconfig  # noqa: E402

SMCD = ROOT / "master_sim" / "out" / "station.SMCD"
JAR = ROOT / "master_sim/third_party/libiec61850/tools/model_generator/genconfig-fast.jar"
MINI = Path(__file__).with_name("ldo_rptid_mini.cid")
BUILD = ROOT / "master_sim/third_party/libiec61850/tools/model_generator/build"


def count_pat(path: Path, pat: str) -> int:
    n = 0
    rgx = re.compile(pat)
    with path.open("r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            n += len(rgx.findall(line))
    return n


def test_python_sanitize() -> None:
    raw = (
        'rptID="LDO/LLN0$BR$brcbDeviceState" '
        'rptID="P_M5001ALDO/LLN0$BR$brcbDeviceState" '
        'rptID="LD0/LLN0$BR$brcbAlarm"'
    )
    got = _sanitize_scl_for_genconfig(raw)
    assert "LDO/LLN0$" not in got, got
    assert 'rptID="LD0/LLN0$BR$brcbDeviceState"' in got
    assert 'rptID="P_M5001ALD0/LLN0$BR$brcbDeviceState"' in got
    assert 'rptID="LD0/LLN0$BR$brcbAlarm"' in got
    print("PASS python sanitize")


def test_smcd_sanitize() -> None:
    if not SMCD.is_file():
        print("SKIP smcd (missing)")
        return
    before = count_pat(SMCD, r'rptID="[^"]*LDO/')
    sample = (
        'rptID="LDO/LLN0$BR$brcbDeviceState"\n'
        'rptID="P_M5001ALDO/LLN0$BR$brcbDeviceState"\n'
    )
    # 只抽含 LDO 的行做消毒，避免把整份 SMCD 读进内存再写回
    n_fixed = 0
    with SMCD.open("r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if "LDO/" in line and "rptID=" in line:
                fixed = _sanitize_scl_for_genconfig(line)
                if "LDO/LLN0$" in fixed:
                    raise AssertionError("sanitize missed: " + line[:180])
                n_fixed += 1
    assert n_fixed == before, (n_fixed, before)
    assert _sanitize_scl_for_genconfig(sample).count("LDO/LLN0$") == 0
    print(f"PASS smcd sanitize LDO rptID lines={before}")


def test_java_helper() -> None:
    cp = str(BUILD)
    r = subprocess.run(
        ["java", "-cp", cp, "com.libiec61850.tools.SclTypoFix"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    print(r.stdout.strip())
    if r.returncode != 0:
        print(r.stderr)
        raise SystemExit("FAIL SclTypoFix")
    print("PASS java SclTypoFix")


def test_genconfig_mini() -> None:
    if not JAR.is_file():
        raise SystemExit(f"missing jar {JAR}")
    out = Path(__file__).with_name("ldo_rptid_mini.cfg")
    if out.exists():
        out.unlink()
    r = subprocess.run(
        [
            "java",
            "-cp",
            str(JAR),
            "com.libiec61850.tools.DynamicModelGenerator",
            str(MINI),
            "-ied",
            "SampleIED",
            "-ap",
            "accessPoint1",
            str(out),
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if r.returncode != 0 or not out.is_file():
        print(r.stdout)
        print(r.stderr)
        raise SystemExit("FAIL genconfig mini")
    text = out.read_text(encoding="utf-8", errors="replace")
    if "LDO/" in text or "P_M5001ALDO/" in text:
        raise SystemExit("FAIL cfg still has LDO:\n" + text)
    if "P_M5001ALD0/LLN0$BR$brcbDeviceState" not in text:
        raise SystemExit("FAIL missing fixed DeviceState rptID:\n" + text)
    if "LD0/LLN0$BR$brcbRelayDin" not in text:
        raise SystemExit("FAIL missing fixed RelayDin rptID:\n" + text)
    print("PASS genconfig mini")
    for line in text.splitlines():
        if line.startswith("RC("):
            print(line)


def main() -> int:
    test_python_sanitize()
    test_smcd_sanitize()
    test_java_helper()
    test_genconfig_mini()
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
