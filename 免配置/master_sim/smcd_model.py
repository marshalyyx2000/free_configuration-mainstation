"""从 station.SMCD 解析各 IED 的数据集与报告控制块（UI 导航回退）。"""
from __future__ import annotations

import xml.etree.ElementTree as ET
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

from build_config import OUT_DIR

DEFAULT_SMCD = OUT_DIR / "station.SMCD"


@dataclass
class DataSetInfo:
    name: str
    desc: str


@dataclass
class ReportInfo:
    name: str
    desc: str
    dat_set: str
    buffered: bool


@dataclass
class IedModelInfo:
    ied_name: str
    desc: str
    datasets: list[DataSetInfo]
    reports: list[ReportInfo]


def _local(tag: str) -> str:
    return tag.split("}")[-1] if "}" in tag else tag


@lru_cache(maxsize=1)
def load_smcd_models(smcd_path: str) -> dict[str, IedModelInfo]:
    path = Path(smcd_path)
    if not path.is_file():
        return {}
    root = ET.parse(path).getroot()
    out: dict[str, IedModelInfo] = {}
    for ied in root.iter():
        if _local(ied.tag) != "IED":
            continue
        name = ied.attrib.get("name", "")
        if not name or name == "SubStation":
            continue
        datasets: list[DataSetInfo] = []
        reports: list[ReportInfo] = []
        for ln0 in ied.iter():
            if _local(ln0.tag) != "LN0":
                continue
            for ds in ln0:
                if _local(ds.tag) == "DataSet":
                    datasets.append(
                        DataSetInfo(
                            name=ds.attrib.get("name", ""),
                            desc=ds.attrib.get("desc", ds.attrib.get("name", "")),
                        )
                    )
                elif _local(ds.tag) == "ReportControl":
                    reports.append(
                        ReportInfo(
                            name=ds.attrib.get("name", ""),
                            desc=ds.attrib.get("desc", ds.attrib.get("name", "")),
                            dat_set=ds.attrib.get("datSet", ""),
                            buffered=ds.attrib.get("buffered", "false").lower() == "true",
                        )
                    )
        if datasets or reports:
            out[name] = IedModelInfo(
                ied_name=name,
                desc=ied.attrib.get("desc", name),
                datasets=datasets,
                reports=reports,
            )
    return out


def get_ied_model(ied_name: str, smcd_path: Path | None = None) -> IedModelInfo | None:
    path = smcd_path or DEFAULT_SMCD
    return load_smcd_models(str(path.resolve())).get(ied_name)
