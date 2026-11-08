#!/usr/bin/env python3
"""流式裁剪 SCL/CID（头 + SubStation + DataTypeTemplates）。"""

import argparse
import re
import sys
from pathlib import Path

IED_OPEN = b"<IED"
IED_CLOSE = b"</IED>"
DTT_OPEN = b"<DataTypeTemplates>"
SUBSTATION = b'manufacturer="master_sim" name="SubStation"'
IO = 1 << 20
TAIL = 8 * 1024 * 1024
BACKSCAN = 65536
BLOATED_IED = 4 * 1024 * 1024
DS_COMM_RE = re.compile(
    r'<DataSet[^>]*name="dsCommState"[^>]*>[\s\S]*?</DataSet>'
)


def slice_bloated_substation(f, out, first_ied: int, sub_start: int, dtt: int, length: int) -> None:
    cap = min(8 * 1024 * 1024, dtt - sub_start)
    f.seek(sub_start)
    text = f.read(cap).decode("utf-8", errors="replace")
    ied_tag = text.find("<IED")
    ied_end = text.find(">", ied_tag)
    if ied_tag < 0 or ied_end < 0:
        raise RuntimeError("无法解析 SubStation IED")
    ied_open = text[ied_tag : ied_end + 1]
    dm = DS_COMM_RE.search(text)
    if not dm:
        raise RuntimeError("未找到 dsCommState DataSet")
    data_set = dm.group(0)
    ln_hits = re.findall(r'lnType="([^"]+)"', text[: dm.start()])
    if not ln_hits:
        raise RuntimeError("未找到 lnType")
    ln_type = ln_hits[-1]
    rebuilt = (
        ied_open
        + '\n\t\t<AccessPoint desc="站控层" name="S1">\n'
        + '\t\t\t<Server timeout="30">\n'
        + "\t\t\t\t<Authentication/>\n"
        + '\t\t\t\t<LDevice desc="子站" inst="LD0">\n'
        + '\t\t\t\t\t<LN0 desc="管理逻辑节点" inst="" lnClass="LLN0" lnType="'
        + ln_type
        + '">\n\t\t\t\t\t\t'
        + data_set
        + "\n\t\t\t\t\t</LN0>\n"
        + "\t\t\t\t</LDevice>\n"
        + "\t\t\t</Server>\n"
        + "\t\t</AccessPoint>\n"
        + "\t</IED>\n"
    )
    copy_range(f, out, 0, first_ied)
    out.write(rebuilt.encode("utf-8"))
    copy_range(f, out, dtt, length)


def find_pattern(f, pat: bytes, start: int, limit: int) -> int:
    carry = b""
    pos = start
    pat_len = len(pat)
    while pos < limit:
        f.seek(pos)
        chunk = f.read(min(IO, limit - pos))
        if not chunk:
            break
        hay = carry + chunk
        idx = hay.find(pat)
        if idx >= 0:
            return pos - len(carry) + idx
        carry = hay[-(pat_len - 1) :] if pat_len > 1 else b""
        pos += len(chunk)
    return -1


def find_last_pattern(f, pat: bytes, start: int, limit: int) -> int:
    last = -1
    pos = start
    while pos < limit:
        hit = find_pattern(f, pat, pos, limit)
        if hit < 0:
            break
        last = hit
        pos = hit + 1
    return last


def find_ied_close(f, ied_start: int, limit: int) -> int:
    pos = ied_start
    while pos < limit:
        hit = find_pattern(f, IED_CLOSE, pos, limit)
        if hit < 0:
            return -1
        after = hit + len(IED_CLOSE)
        ok = False
        while after < limit:
            f.seek(after)
            ch = f.read(1)
            if not ch:
                break
            b = ch[0]
            if b == ord(b"<"):
                ok = True
                break
            if b in (9, 10, 13, 32):
                after += 1
                continue
            break
        if ok:
            return hit
        pos = hit + 1
    return -1


def find_ied_open_before(f, name_pos: int, min_pos: int) -> int:
    start = max(min_pos, name_pos - BACKSCAN)
    f.seek(start)
    buf = f.read(name_pos - start)
    idx = buf.rfind(IED_OPEN)
    if idx >= 0:
        return start + idx
    return find_pattern(f, IED_OPEN, min_pos, name_pos + 1)


def copy_range(f, out, start: int, end: int) -> None:
    f.seek(start)
    remain = end - start
    while remain > 0:
        data = f.read(min(IO, remain))
        if not data:
            break
        out.write(data)
        remain -= len(data)


def slice_scl(src: Path, dst: Path) -> None:
    with src.open("rb") as f:
        length = f.seek(0, 2)
        dtt = find_last_pattern(f, DTT_OPEN, max(0, length - TAIL), length)
        if dtt < 0:
            raise RuntimeError("未找到 DataTypeTemplates")
        first_ied = find_pattern(f, IED_OPEN, 0, length)
        if first_ied < 0:
            raise RuntimeError("未找到 <IED")
        sub_name = find_pattern(f, SUBSTATION, first_ied, dtt)
        if sub_name < 0:
            raise RuntimeError("未找到 SubStation IED")
        sub_start = find_ied_open_before(f, sub_name, first_ied)
        sub_end = find_ied_close(f, sub_start, dtt)
        if sub_end < 0:
            nxt = find_pattern(f, IED_OPEN, sub_start + 5, dtt)
            sub_end = nxt if nxt >= 0 else dtt
        else:
            sub_end += len(IED_CLOSE)
        with dst.open("wb") as out:
            if sub_end - sub_start > BLOATED_IED:
                slice_bloated_substation(f, out, first_ied, sub_start, dtt, length)
            else:
                copy_range(f, out, 0, first_ied)
                copy_range(f, out, sub_start, sub_end)
                copy_range(f, out, dtt, length)
    if dst.stat().st_size < 1000:
        raise RuntimeError("裁剪输出过小")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("src", type=Path)
    p.add_argument("dst", type=Path)
    args = p.parse_args()
    slice_scl(args.src, args.dst)
    print(
        "OK {} ({} KiB)".format(args.dst, args.dst.stat().st_size // 1024),
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
