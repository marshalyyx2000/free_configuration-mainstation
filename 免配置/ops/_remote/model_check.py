"""在子站本地执行：检查 SMCD/SubStation 与 cfg 的 LD 命名、dsCommState 归属。"""
import re

SMCD = "/home/ipofas/stdown/maintain/SMCD/station.SMCD"
CFG = "/home/admin/router/config/supervision.cfg"


def scan_smcd():
    pat = re.compile(b'<IED[^>]*name="SubStation"[^>]*>')
    buf = b""
    pos = 0
    with open(SMCD, "rb") as fh:
        while True:
            chunk = fh.read(1 << 20)
            if not chunk:
                break
            buf += chunk
            m = pat.search(buf)
            if m:
                print("SubStation IED at", pos - len(buf) + m.start())
                seg = buf[m.start():m.start() + 3000].decode("utf-8", "replace")
                print("LDevice:", re.findall(r"<LDevice[^>]*>", seg)[:4])
                print("LN0:", re.findall(r"<LN0[^>]*>", seg)[:2])
                return
            pos += len(chunk)
            buf = buf[-64:]
    print("SubStation not found in SMCD")


def scan_cfg():
    with open(CFG) as fh:
        cfg = fh.read()
    print("MODEL:", re.findall(r"^MODEL\((\w+)\)", cfg, re.M)[:3])
    lds = re.findall(r"^LD\((\w+)\)", cfg, re.M)
    print("LD count:", len(lds), "first:", lds[:5])
    print("has LD(SubStationLD0):", "LD(SubStationLD0)" in cfg)
    print("has LD(LD0):", "LD(LD0)" in cfg)
    for m in list(re.finditer(r"^DS\(dsCommState\)\{", cfg, re.M))[:6]:
        head = cfg[: m.start()]
        ld = re.findall(r"^LD\((\w+)\)", head, re.M)
        print("  dsCommState -> LD=", ld[-1] if ld else None)
    i = cfg.find("LD(LD0){")
    if i >= 0:
        seg = cfg[i:i + 1500]
        print("LD0 LN list:", re.findall(r"^LN\((\w+)\)", seg, re.M))
        print("LD0 DS list:", re.findall(r"^DS\((\w+)\)", seg, re.M))


if __name__ == "__main__":
    scan_cfg()
    scan_smcd()
