"""解析 master_sim 生成的 IEDList.xml。"""
from __future__ import annotations

import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

from build_config import OUT_DIR

DEFAULT_IED_LIST = OUT_DIR / "IEDList.xml"

# 经子站 SubStation 对上订阅时，代理模型 RptEnabled max=1，实例固定为 01（与 IEDList.rptno 下联无关）
UPSTREAM_SUBST_RPT_INST = "1"


@dataclass
class IedDevice:
    no: int
    ied_name: str
    desc: str
    ch_para: str
    rptno: str
    wave_path: str


def _local(tag: str) -> str:
    return tag.split("}")[-1] if "}" in tag else tag


def load_ied_list(path: Path | None = None) -> list[IedDevice]:
    path = path or DEFAULT_IED_LIST
    if not path.is_file():
        return []
    root = ET.parse(path).getroot()
    devices: list[IedDevice] = []
    for node in root.iter():
        if _local(node.tag) != "Device":
            continue
        if node.attrib.get("p_type") != "1":
            continue
        devices.append(
            IedDevice(
                no=int(node.attrib.get("no", "0")),
                ied_name=node.attrib.get("ied_name", ""),
                desc=node.attrib.get("name", ""),
                ch_para=node.attrib.get("ch_para", ""),
                rptno=node.attrib.get("rptno", "9"),
                wave_path=node.attrib.get("wave_path", "/COMTRADE/"),
            )
        )
    devices.sort(key=lambda d: d.no)
    return devices


def logical_device_name(ied_name: str) -> str:
    return f"{ied_name}LD0"


def rcb_get_reference(ied_name: str, rcb_name: str, rptno: str, buffered: bool = True) -> str:
    """对上连 SubStation 时请用 rcb_get_reference_upstream。"""
    ld = logical_device_name(ied_name)
    try:
        inst = int(rptno)
    except ValueError:
        inst = 9
    br = "$BR$" if buffered else "$RP$"
    return f"{ld}/LLN0{br}{rcb_name}{inst:02d}"


def rcb_get_reference_upstream(ied_name: str, rcb_name: str, buffered: bool = True) -> str:
    return rcb_get_reference(ied_name, rcb_name, UPSTREAM_SUBST_RPT_INST, buffered)


def rcb_handler_reference(ied_name: str, rcb_name: str, buffered: bool = True) -> str:
    ld = logical_device_name(ied_name)
    br = "$BR$" if buffered else "$RP$"
    return f"{ld}/LLN0{br}{rcb_name}"



def dataset_reference(ied_name: str, dataset_name: str) -> str:
    ld = logical_device_name(ied_name)
    return f"{ld}/LLN0${dataset_name}"
