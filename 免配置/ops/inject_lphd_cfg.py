#!/usr/bin/env python3
"""在装置 supervision.cfg 中为每个 dsCommState 注入 LPHD1.Proxy。"""
from __future__ import annotations

import re
import sys
from pathlib import Path

CFG = "/home/admin/router/config/supervision.cfg"
LPHD = (
    "LN(LPHD1){\n"
    "DO(Proxy 0){\n"
    "DA(stVal 0 12 0 1 0);\n"
    "DA(q 0 23 0 2 0);\n"
    "DA(t 0 22 0 0 0);\n"
    "}\n"
    "}\n"
)


def inject(text: str) -> tuple[str, int]:
    ld_names = sorted(set(re.findall(r"DE\(([^/]+)/LPHD1\$ST\$Proxy\);", text)))
    count = 0
    for ld in ld_names:
        marker = f"LD({ld}){{"
        idx = text.find(marker)
        if idx < 0:
            continue
        at = idx + len(marker)
        if "LN(LPHD1)" in text[at : at + 400]:
            continue
        text = text[:at] + LPHD + text[at:]
        count += 1
    return text, count


def main() -> int:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    if len(sys.argv) > 1 and sys.argv[1] == "--local":
        p = Path(sys.argv[2])
        new_text, n = inject(p.read_text(encoding="utf-8", errors="replace"))
        p.write_text(new_text, encoding="utf-8")
        print(f"injected {n} lds into {p}")
        return 0

    from remote import run

    script = f"""
import re, os
cfg='{CFG}'
text=open(cfg,encoding='utf-8',errors='replace').read()
lds=sorted(set(re.findall(r'DE\\(([^/]+)/LPHD1\\\\$ST\\\\$Proxy\\);', text)))
lphd={LPHD!r}
n=0
for ld in lds:
    m='LD('+ld+')'+'{{'
    i=text.find(m)
    if i<0:
        continue
    at=i+len(m)
    if 'LN(LPHD1)' in text[at:at+400]:
        continue
    text=text[:at]+lphd+text[at:]
    n+=1
open(cfg,'w',encoding='utf-8').write(text)
print('injected',n,'size',os.path.getsize(cfg))
"""
    print(run(f"python3 <<'PY'\n{script}\nPY"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
