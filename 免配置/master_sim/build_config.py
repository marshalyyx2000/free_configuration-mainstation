#!/usr/bin/env python3
"""从 26579.SCD 生成 IEDList.xml、station.SMCD（插入 SubStation）、station.zip。"""
from __future__ import annotations

import argparse
import re
import shutil
import zipfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

DEFAULT_SCD = Path(__file__).resolve().parents[1] / "子站scd" / "26579.SCD"
OUT_DIR = Path(__file__).resolve().parent / "out"

DEVICE_DEFAULTS = {
    "rptno": "9",
    "BRCB_TrgOps": "44",
    "URCB_TrgOps": "4C",
    "BRCB_OptFlds": "3900",
    "URCB_OptFlds": "3800",
    "wave_dir": "/COMTRADE/",
    "wave_type": "7",
    "file_suff_capital": "0",
    "wave_path": "/COMTRADE/",
    "n61850_version": "1",
}


@dataclass
class ConnectedDevice:
    ied_name: str
    ip: str
    desc: str = ""


def _local(tag: str) -> str:
    return tag.split("}")[-1] if "}" in tag else tag


def parse_connected_aps(scd_path: Path) -> list[ConnectedDevice]:
    devices: list[ConnectedDevice] = []
    ied_desc: dict[str, str] = {}

    for event, elem in ET.iterparse(scd_path, events=("end",)):
        tag = _local(elem.tag)
        if tag == "IED":
            name = elem.attrib.get("name", "")
            if name:
                ied_desc[name] = elem.attrib.get("desc", name)
            elem.clear()
        elif tag == "ConnectedAP":
            ied = elem.attrib.get("iedName", "")
            ip = ""
            for child in elem:
                if _local(child.tag) == "Address":
                    for p in child:
                        if _local(p.tag) == "P" and p.attrib.get("type") == "IP":
                            ip = (p.text or "").strip()
            if ied and ip:
                devices.append(
                    ConnectedDevice(ied_name=ied, ip=ip, desc=ied_desc.get(ied, ied))
                )
            elem.clear()
    devices.sort(key=lambda d: d.ied_name)
    return _dedupe_by_ied(devices)


def _connected_ap_rank(ip: str) -> int:
    """数值越大越优先写入 ch_para（生产站 A 网 172.16 > B 网 172.17 > 仿真 10.100.100）。"""
    if ip.startswith("10.100.100."):
        return 0
    if ip.startswith("10."):
        return 1
    if ip.startswith("172.17."):
        return 2
    if ip.startswith("172.16."):
        return 3
    return 2


def _dedupe_by_ied(devices: list[ConnectedDevice]) -> list[ConnectedDevice]:
    """同一 IED 在 SCD 里常有 A/B 双网 ConnectedAP。

    子站的 ch_para 只能填一个 IP，重复条目会在 IEDList 和 dsCommState 里
    各出现两次（同名 IED 被当成两台，路由侧反复重连 B 网）。这里每个
    ied_name 只保留一个，双网时优先 172.16 A 网，不用 10.100.100 仿真网段。
    """
    picked: dict[str, ConnectedDevice] = {}
    for dev in devices:
        old = picked.get(dev.ied_name)
        if old is None:
            picked[dev.ied_name] = dev
            continue
        if _connected_ap_rank(dev.ip) > _connected_ap_rank(old.ip):
            picked[dev.ied_name] = dev
    result = sorted(picked.values(), key=lambda d: d.ied_name)
    dropped = len(devices) - len(result)
    if dropped:
        print(f"去重: {len(devices)} 个 ConnectedAP -> {len(result)} 台 IED（丢弃 {dropped} 个备用网）")
    return result


def build_ied_list_xml(devices: list[ConnectedDevice]) -> str:
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        "<CONFIG>",
        f'  <Header desc="仿真主站生成" version="1.0" CRC="0" date="{now}">',
        '    <History>',
        f'      <Hitem version="1.0" revision="0" when="{now}" who="master_sim" what="自动生成" />',
        "    </History>",
        "  </Header>",
        "  <IEDList>",
    ]
    for no, dev in enumerate(devices, start=1):
        esc_name = dev.desc.replace('"', "'")
        ch = f"{dev.ip}:102"
        attrs = " ".join(f'{k}="{v}"' for k, v in DEVICE_DEFAULTS.items())
        lines.append(
            f'    <Device no="{no}" name="{esc_name}" ied_name="{dev.ied_name}" '
            f'addr="0" {attrs} ch_para="{ch}" p_type="1" />'
        )
    lines.append("  </IEDList>")
    lines.append("</CONFIG>")
    return "\n".join(lines) + "\n"


def _resolve_lln0_ln_type(scd_path: Path) -> str:
    """DataTypeTemplates 常在 SCD 末尾，须先扫全文件 LNodeType 再统计 LN0。"""
    template_ids: set[str] = set()
    for ev, el in ET.iterparse(scd_path, events=("start", "end")):
        if ev == "start" and el.tag.split("}")[-1] == "LNodeType":
            tid = el.attrib.get("id", "")
            if tid:
                template_ids.add(tid)
        if ev == "end":
            el.clear()
    stats: dict[str, dict[str, int | bool]] = {}
    for ev, el in ET.iterparse(scd_path, events=("end",)):
        if el.tag.split("}")[-1] != "LN0":
            el.clear()
            continue
        ln_type = el.attrib.get("lnType", "")
        if not ln_type or ln_type not in template_ids:
            el.clear()
            continue
        st = stats.setdefault(ln_type, {"n": 0, "bad_rcb": False, "max_rcb": 0})
        st["n"] = int(st["n"]) + 1
        rcbs = [c for c in el if c.tag.split("}")[-1] == "ReportControl"]
        st["max_rcb"] = max(int(st["max_rcb"]), len(rcbs))
        if any(not c.attrib.get("confRev") for c in rcbs):
            st["bad_rcb"] = True
        el.clear()
    if not stats:
        raise RuntimeError("SCD 中找不到可用于 SubStation 的 LLN0 lnType")
    safe = {lt: st for lt, st in stats.items() if not st["bad_rcb"]}
    if not safe:
        raise RuntimeError("SCD 中所有 LLN0 都含 genconfig 无法解析的空 ReportControl")
    zero_rcb = {lt: st for lt, st in safe.items() if int(st["max_rcb"]) == 0}
    pool = zero_rcb if zero_rcb else safe
    return max(pool.items(), key=lambda kv: int(kv[1]["n"]))[0]


def _substation_ied_xml(devices: list[ConnectedDevice], lln0_ln_type: str) -> str:
    """dsCommState 的 FCDA 必须是所有装置都有的对象。

    SCD 里各型号装置的 LN 不同：`GOAlmGGIO1` 只有 13 台有，其余 17 台没有，
    引用它会让 genconfig 产生不存在的条目（读出来是 error 10）。
    30 台装置都有的公共点是 `LPHD1.Proxy.stVal`（BOOLEAN，fc=ST）——
    保信子站用它表示装置通信状态，正好用来做通信工况。
    """
    fcda_lines = []
    for dev in devices:
        fcda_lines.append(
            f'            <FCDA doName="Proxy" fc="ST" ldInst="{dev.ied_name}LD0" '
            f'lnClass="LPHD" lnInst="1"/>'
        )
    fcda_block = "\n".join(fcda_lines)
    return f"""  <IED configVersion="V1.00" desc="保信子站" manufacturer="master_sim" name="SubStation" type="virtual">
    <AccessPoint desc="站控层" name="S1">
      <Server timeout="30">
        <Authentication/>
        <LDevice desc="子站" inst="LD0">
          <LN0 desc="管理逻辑节点" inst="" lnClass="LLN0" lnType="{lln0_ln_type}">
            <DataSet desc="通信工况" name="dsCommState">
{fcda_block}
            </DataSet>
          </LN0>
        </LDevice>
      </Server>
    </AccessPoint>
  </IED>
"""


def _sanitize_scl_for_genconfig(text: str) -> str:
    """江西等大 SCD 中常见 intgPd=\"\"，会导致 genconfig.jar NumberFormatException。

    同时把每个 ReportControl 的 RptEnabled max 收敛为 1。原 SCD 常为 16/12/18，
    convert2CID 后 genconfig 会为每个 RCB 生成 max 个实例（本机曾达 31312 个
    RC），代理子站只需要一个实例；实例过多会让 router 启动时 LinkedList 插入
    退化为 O(N^2)，并对每个主站连接每 10ms 扫描全部 RCB，导致 100% CPU、
    主站订阅/总召全部超时。
    """
    text = text.replace('intgPd=""', 'intgPd="0"')
    text = text.replace('bufTime=""', 'bufTime="0"')
    text = text.replace('confRev=""', 'confRev="1"')
    text = re.sub(r'(<RptEnabled\b[^>]*\bmax=")[^"]*(")', r"\g<1>1\g<2>", text)
    return text


def _strip_master_sim_substation(text: str) -> str:
    """删除此前主站插入的 SubStation IED，便于更正 lnType 后重新生成。"""
    return re.sub(
        r'  <IED configVersion="V1.00" desc="保信子站" manufacturer="master_sim" name="SubStation" type="virtual">.*?</IED>\s*',
        "",
        text,
        count=1,
        flags=re.DOTALL,
    )


def inject_substation(scd_path: Path, dest_smcd: Path, devices: list[ConnectedDevice]) -> None:
    shutil.copy2(scd_path, dest_smcd)
    text = dest_smcd.read_text(encoding="utf-8")
    text = _strip_master_sim_substation(text)
    lln0_ln_type = _resolve_lln0_ln_type(scd_path)
    insert = _substation_ied_xml(devices, lln0_ln_type)
    marker = re.search(r"<IED\s", text)
    if not marker:
        raise RuntimeError("SCD 中未找到 <IED>，无法插入 SubStation")
    pos = marker.start()
    new_text = text[:pos] + insert + text[pos:]
    dest_smcd.write_text(_sanitize_scl_for_genconfig(new_text), encoding="utf-8")


def build_station_zip(smcd_path: Path, zip_path: Path) -> None:
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        zf.write(smcd_path, arcname="station.SMCD")


def generate(scd_path: Path = DEFAULT_SCD, out_dir: Path = OUT_DIR) -> list[ConnectedDevice]:
    if not scd_path.is_file():
        raise FileNotFoundError(f"SCD 不存在: {scd_path}")

    out_dir.mkdir(parents=True, exist_ok=True)
    devices = parse_connected_aps(scd_path)
    if not devices:
        raise ValueError("未从 SCD 解析到任何 ConnectedAP")

    ied_path = out_dir / "IEDList.xml"
    ied_path.write_text(build_ied_list_xml(devices), encoding="utf-8")

    smcd_path = out_dir / "station.SMCD"
    inject_substation(scd_path, smcd_path, devices)

    zip_path = out_dir / "station.zip"
    build_station_zip(smcd_path, zip_path)

    print(f"装置数: {len(devices)}")
    print(f"写入: {ied_path}")
    print(f"写入: {smcd_path} ({smcd_path.stat().st_size // 1024} KiB)")
    print(f"写入: {zip_path} ({zip_path.stat().st_size // 1024} KiB)")
    return devices


def _selfcheck_genconfig(smcd: Path, timeout: float = 90.0) -> None:
    """PC 上 slice + 加速 genconfig 自检（大站应 <60s）。"""
    import subprocess
    import sys
    import tempfile
    import time

    root = Path(__file__).resolve().parent
    sys.path.insert(0, str(root))
    from tools.scl_genconfig_slice import slice_scl  # noqa: WPS433

    fast_jar = (
        root / "third_party/libiec61850/tools/model_generator/genconfig-fast.jar"
    )
    if not fast_jar.is_file():
        print("自检跳过: 未构建 genconfig-fast.jar", file=sys.stderr)
        return
    with tempfile.TemporaryDirectory() as td:
        td_path = Path(td)
        mini = td_path / "mini.scl"
        cfg = td_path / "supervision.cfg"
        t0 = time.perf_counter()
        slice_scl(smcd, mini)
        r = subprocess.run(
            ["java", "-jar", str(fast_jar), str(mini), str(cfg)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
        )
        wall = time.perf_counter() - t0
        ok = r.returncode == 0 and cfg.is_file() and cfg.stat().st_size > 0
        msg = (
            f"genconfig 自检: ok={ok} wall={wall:.1f}s "
            f"mini={mini.stat().st_size // 1024}KiB "
            f"cfg={cfg.stat().st_size // 1024 if cfg.is_file() else 0}KiB"
        )
        print(msg, file=sys.stderr)
        if not ok or wall > 60:
            print(
                "警告: 大站请部署 ops/deploy_genconfig_fast.py；"
                "装置 CID 须为完整 SMCD 方能裁剪加速",
                file=sys.stderr,
            )
        if r.stderr:
            print(r.stderr[-400:], file=sys.stderr)


def main() -> None:
    parser = argparse.ArgumentParser(description="生成免配置下装包")
    parser.add_argument("--scd", type=Path, default=DEFAULT_SCD)
    parser.add_argument("--out", type=Path, default=OUT_DIR)
    parser.add_argument(
        "--selfcheck-genconfig",
        action="store_true",
        help="生成后跑 slice+genconfig-fast 自检",
    )
    args = parser.parse_args()
    generate(args.scd, args.out)
    if args.selfcheck_genconfig:
        _selfcheck_genconfig(args.out / "station.SMCD")


if __name__ == "__main__":
    main()
