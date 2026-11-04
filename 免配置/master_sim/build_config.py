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


def _dedupe_by_ied(devices: list[ConnectedDevice]) -> list[ConnectedDevice]:
    """同一 IED 在 SCD 里常有 A/B 双网 ConnectedAP。

    子站的 ch_para 只能填一个 IP，重复条目会在 IEDList 和 dsCommState 里
    各出现两次（同名 IED 被当成两台，路由侧反复重连 B 网）。这里每个
    ied_name 只保留一个，优先 A 网（10.x）。
    """
    picked: dict[str, ConnectedDevice] = {}
    for dev in devices:
        old = picked.get(dev.ied_name)
        if old is None:
            picked[dev.ied_name] = dev
            continue
        if not old.ip.startswith("10.") and dev.ip.startswith("10."):
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


def _substation_ied_xml(devices: list[ConnectedDevice]) -> str:
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
          <LN0 desc="管理逻辑节点" inst="" lnClass="LLN0" lnType="XJ_PAC852GG_LD0_V01_LLN0">
            <DataSet desc="通信工况" name="dsCommState">
{fcda_block}
            </DataSet>
          </LN0>
        </LDevice>
      </Server>
    </AccessPoint>
  </IED>
"""


def inject_substation(scd_path: Path, dest_smcd: Path, devices: list[ConnectedDevice]) -> None:
    shutil.copy2(scd_path, dest_smcd)
    text = dest_smcd.read_text(encoding="utf-8")
    if 'name="SubStation"' in text:
        return
    insert = _substation_ied_xml(devices)
    marker = re.search(r"<IED\s", text)
    if not marker:
        raise RuntimeError("SCD 中未找到 <IED>，无法插入 SubStation")
    pos = marker.start()
    new_text = text[:pos] + insert + text[pos:]
    dest_smcd.write_text(new_text, encoding="utf-8")


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


def main() -> None:
    parser = argparse.ArgumentParser(description="生成免配置下装包")
    parser.add_argument("--scd", type=Path, default=DEFAULT_SCD)
    parser.add_argument("--out", type=Path, default=OUT_DIR)
    args = parser.parse_args()
    generate(args.scd, args.out)


if __name__ == "__main__":
    main()
