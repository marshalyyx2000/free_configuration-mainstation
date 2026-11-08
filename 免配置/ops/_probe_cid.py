#!/usr/bin/env python3
"""SSH: 快速探测 supervision.CID 结构。"""
from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from remote import run  # noqa: E402

CID = "/home/admin/router/config/supervision.CID"
cmd = rf"""
python3 <<'PY'
import re
p = "{CID}"
with open(p, encoding="utf-8", errors="ignore") as f:
    head = f.read(2000000)
import os
print("size_mb", os.path.getsize(p) / (1024*1024))
print("LDevice", head.count("<LDevice"))
ieds = re.findall(r'<IED[^>]+name="([^"]+)"', head)
print("IED_in_first_2mb", ieds[:15], "count", len(ieds))
print("SubStation", "name=\"SubStation\"" in head)
print("P_X5071A", "P_X5071A" in head)
for pat in ["ReportControl", "dsRelayDin", "LPHD"]:
    print(pat, head.count(pat))
PY
"""
print(run(cmd))
