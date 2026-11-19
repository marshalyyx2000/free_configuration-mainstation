#!/usr/bin/env python3
"""从保信 SubStation.icd 模板 + IEDList/SCD 台账生成 26579 的 SubStation.icd。

dsCommState 成员为各装置 prefix=IED 名的 SDDI.ComStatus（合/分子站通道状态），
不是各装置 LPHD.Proxy。
"""
from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

MASTER_SIM = Path(__file__).resolve().parents[2]
MIAN_PEI = MASTER_SIM.parent
DEFAULT_TEMPLATE = MIAN_PEI / "SubStation.icd"
DEFAULT_IEDLIST = MASTER_SIM / "out" / "IEDList.xml"
DEFAULT_OUT = MIAN_PEI / "SubStation_26579.icd"

_DEVICE_TAG = re.compile(r"<Device\b([^>]*)/?>", re.I)
_ATTR = re.compile(r'(\w+)="([^"]*)"')
_DS_COMM = re.compile(
    r'(<DataSet\b[^>]*\bname="dsCommState"[^>]*>)(.*?)(</DataSet>)',
    re.DOTALL | re.I,
)
_TYPE_BLOCK = re.compile(
    r"<(LNodeType|DOType|DAType|EnumType)\b([^>]*)>.*?</\1>",
    re.DOTALL,
)
_TYPE_ID = re.compile(r'\bid="([^"]+)"')


def xml_esc(text: str) -> str:
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def parse_iedlist(path: Path) -> list[dict]:
    text = path.read_text(encoding="utf-8")
    rows: list[dict] = []
    for m in _DEVICE_TAG.finditer(text):
        attrs = dict(_ATTR.findall(m.group(1)))
        ied = attrs.get("ied_name") or attrs.get("iedName") or ""
        if not ied:
            continue
        try:
            no = int(attrs.get("no") or "0")
        except ValueError:
            no = 0
        rows.append(
            {
                "no": no,
                "ied_name": ied,
                "name": attrs.get("name") or ied,
            }
        )
    rows.sort(key=lambda r: (r["no"] or 10**9, r["ied_name"]))
    for i, row in enumerate(rows, start=1):
        if not row["no"]:
            row["no"] = i
    return rows


def comm_status_du(desc: str) -> str:
    d = (desc or "").strip() or "装置"
    if "通信状态" in d:
        return d
    return d + "通信状态"


def sddi_ln_xml(prefix: str, inst: int, desc: str) -> str:
    du = xml_esc(comm_status_du(desc))
    p = xml_esc(prefix)
    return (
        f'          <LN desc="状态监视信息" prefix="{p}" inst="{inst}" '
        f'lnClass="SDDI" lnType="SUBSTATION_SDDI_V2.00">\n'
        "            <DOI name=\"Mod\" desc=\"Mode\">\n"
        "              <DAI name=\"stVal\">\n"
        "                <Val>on</Val>\n"
        "              </DAI>\n"
        "              <DAI name=\"ctlModel\">\n"
        "                <Val>status-only</Val>\n"
        "              </DAI>\n"
        "            </DOI>\n"
        "            <DOI name=\"Beh\" desc=\"Behaviour\">\n"
        "              <DAI name=\"stVal\">\n"
        "                <Val>on</Val>\n"
        "              </DAI>\n"
        "            </DOI>\n"
        "            <DOI name=\"Health\" desc=\"Health\">\n"
        "              <DAI name=\"stVal\">\n"
        "                <Val>Ok</Val>\n"
        "              </DAI>\n"
        "            </DOI>\n"
        "            <DOI name=\"NamPlt\" desc=\"Name Plate\">\n"
        "              <DAI name=\"vendor\">\n"
        "                <Val>NR</Val>\n"
        "              </DAI>\n"
        "              <DAI name=\"swRev\">\n"
        "                <Val>R1.00</Val>\n"
        "              </DAI>\n"
        "              <DAI name=\"d\">\n"
        "                <Val>Name Plate</Val>\n"
        "              </DAI>\n"
        "              <DAI name=\"dU\">\n"
        "                <Val>Name Plate</Val>\n"
        "              </DAI>\n"
        "              <DAI name=\"lnNs\">\n"
        "                <Val>NR PCS979X:2015</Val>\n"
        "              </DAI>\n"
        "            </DOI>\n"
        f'            <DOI desc="{du}" name="ComStatus">\n'
        "              <DAI name=\"stVal\"/>\n"
        "              <DAI name=\"dU\">\n"
        f"                <Val>{du}</Val>\n"
        "              </DAI>\n"
        "              <DAI name=\"dataNs\">\n"
        "                <Val>NR PCS979X:2015</Val>\n"
        "              </DAI>\n"
        "            </DOI>\n"
        "          </LN>\n"
    )


def ds_fcda_xml(prefix: str, inst: int) -> str:
    return (
        f'              <FCDA doName="ComStatus" fc="ST" ldInst="LD0" '
        f'lnClass="SDDI" lnInst="{inst}" prefix="{xml_esc(prefix)}" />'
    )


def substation_ied_xml(rows: list[dict], lln0_ln_type: str) -> str:
    """注入 SMCD 的 SubStation IED：LD0 上 SDDI.ComStatus + dsCommState。"""
    fcda = "\n".join(ds_fcda_xml(r["ied_name"], r["no"]) for r in rows)
    lns = "".join(sddi_ln_xml(r["ied_name"], r["no"], r.get("name") or r["ied_name"]) for r in rows)
    return f"""  <IED configVersion="V1.00" desc="保信子站" manufacturer="master_sim" name="SubStation" type="virtual">
    <AccessPoint desc="站控层" name="S1">
      <Server timeout="30">
        <Authentication/>
        <LDevice desc="子站" inst="LD0">
          <LN0 desc="管理逻辑节点" inst="" lnClass="LLN0" lnType="{xml_esc(lln0_ln_type)}">
            <DataSet desc="通信工况" name="dsCommState">
{fcda}
            </DataSet>
            <ReportControl name="brcbCommState" datSet="dsCommState" intgPd="0" rptID="LD0/LLN0$BR$brcbCommState" confRev="1" buffered="true" bufTime="0">
              <TrgOps dchg="true" qchg="false" dupd="false" period="true"/>
              <OptFields seqNum="false" timeStamp="true" dataSet="true" reasonCode="true" dataRef="true" entryID="true" configRef="true"/>
              <RptEnabled max="1"/>
            </ReportControl>
          </LN0>
{lns}        </LDevice>
      </Server>
    </AccessPoint>
  </IED>
"""


def merge_icd_datatypes(smcd: str, icd: str) -> str:
    """把 ICD 中 SMCD 尚未声明的 DataTypeTemplates 类型合并进去。"""
    m = re.search(r"<DataTypeTemplates\b[^>]*>(.*?)</DataTypeTemplates>", icd, re.DOTALL)
    if not m:
        return smcd
    insert_parts: list[str] = []
    for block in _TYPE_BLOCK.finditer(m.group(1)):
        id_m = _TYPE_ID.search(block.group(0))
        if not id_m:
            continue
        type_id = id_m.group(1)
        if re.search(rf'\bid="{re.escape(type_id)}"', smcd):
            continue
        insert_parts.append(block.group(0))
    if not insert_parts:
        return smcd
    blob = "\n".join(insert_parts) + "\n"
    if "</DataTypeTemplates>" in smcd:
        return smcd.replace("</DataTypeTemplates>", blob + "  </DataTypeTemplates>", 1)
    return smcd.rstrip() + "\n  <DataTypeTemplates>\n" + blob + "  </DataTypeTemplates>\n"


_SDDI_LN_START = re.compile(r'<LN\b[^>]*\blnClass="SDDI"')


def _replace_sddi_lns(text: str, rows: list[dict]) -> str:
    starts = [m.start() for m in _SDDI_LN_START.finditer(text)]
    if not starts:
        raise RuntimeError("模板中没有 SDDI LN")
    ln_start = starts[0]
    ld_close = text.find("</LDevice>", starts[-1])
    if ld_close < 0:
        raise RuntimeError("找不到 </LDevice>")
    last_ln = text.rfind("</LN>", starts[-1], ld_close)
    if last_ln < 0:
        raise RuntimeError("找不到最后一个 SDDI </LN>")
    end = last_ln + len("</LN>")
    block = "".join(sddi_ln_xml(r["ied_name"], r["no"], r.get("name") or r["ied_name"]) for r in rows)
    return text[:ln_start] + block + text[end:]


def generate_icd(template: Path, rows: list[dict], dest: Path) -> Path:
    if not template.is_file():
        raise FileNotFoundError(f"模板不存在: {template}")
    if not rows:
        raise ValueError("台账为空，无法生成 dsCommState")
    text = template.read_text(encoding="utf-8")
    text = text.replace('iedName="TEMPLATE"', 'iedName="SubStation"')
    text = text.replace('IED name="TEMPLATE"', 'IED name="SubStation"')
    text = text.replace('id="template"', 'id="26579"')
    text = _replace_sddi_lns(text, rows)
    fcda = "\n".join(ds_fcda_xml(r["ied_name"], r["no"]) for r in rows)
    m = _DS_COMM.search(text)
    if not m:
        raise RuntimeError("模板中没有 dsCommState")
    text = text[: m.start()] + m.group(1) + "\n" + fcda + "\n            " + m.group(3) + text[m.end() :]
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(text, encoding="utf-8")
    return dest


_SDDI_TYPE_BLOCKS = (
    (
        "SUBSTATION_SDDI_V2.00",
        '<LNodeType id="SUBSTATION_SDDI_V2.00" lnClass="SDDI" desc="免配置保信子站状态监视信息">'
        '<DO name="Mod" type="SUBSTATION_INC_Mod" desc="Mode"/>'
        '<DO name="Beh" type="SUBSTATION_INS_Beh" desc="Behaviour"/>'
        '<DO name="Health" type="SUBSTATION_INS_Health" desc="Health"/>'
        '<DO name="NamPlt" type="SUBSTATION_LPL_EX" desc="Name Plate"/>'
        '<DO name="ComStatus" type="SUBSTATION_SPS_EX" desc="与对应IED的通信状态"/>'
        "</LNodeType>\n",
    ),
    (
        "SUBSTATION_INC_Mod",
        '<DOType id="SUBSTATION_INC_Mod" cdc="INC">'
        '<DA name="stVal" bType="Enum" type="Mod" dchg="true" fc="ST"/>'
        '<DA name="q" bType="Quality" qchg="true" fc="ST"/>'
        '<DA name="t" bType="Timestamp" fc="ST"/>'
        '<DA name="ctlModel" bType="Enum" type="ctlModel" fc="CF"><Val>0</Val></DA>'
        "</DOType>\n",
    ),
    (
        "SUBSTATION_INS_Beh",
        '<DOType id="SUBSTATION_INS_Beh" cdc="INS">'
        '<DA name="stVal" bType="Enum" type="Beh" dchg="true" fc="ST"/>'
        '<DA name="q" bType="Quality" qchg="true" fc="ST"/>'
        '<DA name="t" bType="Timestamp" fc="ST"/>'
        "</DOType>\n",
    ),
    (
        "SUBSTATION_INS_Health",
        '<DOType id="SUBSTATION_INS_Health" cdc="INS">'
        '<DA name="stVal" bType="Enum" type="Health" dchg="true" fc="ST"/>'
        '<DA name="q" bType="Quality" qchg="true" fc="ST"/>'
        '<DA name="t" bType="Timestamp" fc="ST"/>'
        "</DOType>\n",
    ),
    (
        "SUBSTATION_LPL_EX",
        '<DOType id="SUBSTATION_LPL_EX" cdc="LPL">'
        '<DA name="vendor" bType="VisString255" fc="DC"/>'
        '<DA name="swRev" bType="VisString255" fc="DC"/>'
        '<DA name="d" bType="VisString255" fc="DC"/>'
        '<DA name="dU" bType="Unicode255" fc="DC"/>'
        '<DA name="lnNs" bType="VisString255" fc="EX"><Val>NR PCS979X:2015</Val></DA>'
        "</DOType>\n",
    ),
    (
        "SUBSTATION_SPS_EX",
        '<DOType id="SUBSTATION_SPS_EX" cdc="SPS">'
        '<DA name="stVal" bType="BOOLEAN" dchg="true" fc="ST"/>'
        '<DA name="q" bType="Quality" qchg="true" fc="ST"/>'
        '<DA name="t" bType="Timestamp" fc="ST"/>'
        '<DA name="dU" bType="Unicode255" fc="DC"/>'
        '<DA name="dataNs" bType="VisString255" fc="EX"><Val>NR PCS979X:2015</Val></DA>'
        "</DOType>\n",
    ),
    (
        "Beh",
        '<EnumType id="Beh">'
        '<EnumVal ord="1">on</EnumVal><EnumVal ord="2">blocked</EnumVal>'
        '<EnumVal ord="3">test</EnumVal><EnumVal ord="4">test/blocked</EnumVal>'
        '<EnumVal ord="5">off</EnumVal></EnumType>\n',
    ),
    (
        "Mod",
        '<EnumType id="Mod">'
        '<EnumVal ord="1">on</EnumVal><EnumVal ord="2">blocked</EnumVal>'
        '<EnumVal ord="3">test</EnumVal><EnumVal ord="4">test/blocked</EnumVal>'
        '<EnumVal ord="5">off</EnumVal></EnumType>\n',
    ),
    (
        "Health",
        '<EnumType id="Health">'
        '<EnumVal ord="1">Ok</EnumVal><EnumVal ord="2">Warning</EnumVal>'
        '<EnumVal ord="3">Alarm</EnumVal></EnumType>\n',
    ),
    (
        "ctlModel",
        '<EnumType id="ctlModel">'
        '<EnumVal ord="0">status-only</EnumVal>'
        '<EnumVal ord="1">direct-with-normal-security</EnumVal>'
        '<EnumVal ord="2">sbo-with-normal-security</EnumVal>'
        '<EnumVal ord="3">direct-with-enhanced-security</EnumVal>'
        '<EnumVal ord="4">sbo-with-enhanced-security</EnumVal></EnumType>\n',
    ),
)


def ensure_sddi_types(smcd: str) -> str:
    """补上 SubStation SDDI 引用的 LNodeType 及其 DO/枚举。已有同 id 的不重复插入。"""
    insert = []
    for type_id, xml in _SDDI_TYPE_BLOCKS:
        if f'id="{type_id}"' in smcd or any(f'id="{type_id}"' in part for part in insert):
            continue
        insert.append(xml)
    if not insert:
        return smcd
    blob = "".join(insert)
    if "</DataTypeTemplates>" in smcd:
        return smcd.replace("</DataTypeTemplates>", blob + "</DataTypeTemplates>", 1)
    return smcd.rstrip() + "\n  <DataTypeTemplates>\n" + blob + "  </DataTypeTemplates>\n"


def copy_icd_to_out(src: Path, out_dir: Path) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    dst = out_dir / src.name
    if dst.resolve() != src.resolve():
        shutil.copy2(src, dst)
    return dst


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="生成 26579 SubStation.icd（dsCommState/ComStatus）")
    p.add_argument("--template", type=Path, default=DEFAULT_TEMPLATE)
    p.add_argument("--iedlist", type=Path, default=DEFAULT_IEDLIST)
    p.add_argument("--out", type=Path, default=DEFAULT_OUT)
    p.add_argument("--copy-out-dir", type=Path, default=MASTER_SIM / "out")
    args = p.parse_args(argv)
    rows = parse_iedlist(args.iedlist)
    dest = generate_icd(args.template, rows, args.out)
    copied = copy_icd_to_out(dest, args.copy_out_dir)
    print(f"装置数: {len(rows)}")
    print(f"写入: {dest}")
    print(f"复制: {copied}")
    prefixes = [r["ied_name"] for r in rows]
    print("prefix: " + ",".join(prefixes[:3]) + "..." + prefixes[-1] if prefixes else "prefix: (empty)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
