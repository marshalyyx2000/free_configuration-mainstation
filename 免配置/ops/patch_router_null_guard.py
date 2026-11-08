#!/usr/bin/env python3
"""修补子站 iec61850router 源码：ReadNamed/Read 失败时空值导致段错误。

现象：主站 read_dataset 触发 `ProxyReadNamedVariableResponse::cacheDataSet`，
装置读取失败时 dataSet/values 为 NULL，直接 MmsValue_getArraySize → SIGSEGV，
MaintainServer 重新拉起 router（102 只活约 20 秒），主站表现为 TIMEOUT/CONNECTION_LOST。
"""
from __future__ import annotations

import os
import re
import sys

import paramiko

HOST = os.environ.get("SUBST_HOST", "192.168.0.10")
USER = os.environ.get("SUBST_USER", "admin")
PASS = os.environ.get("SUBST_PASS", "")
SRC = "/home/admin/src/configuration-free/iec61850_router/src/common/proxy_entry.cpp"

NAMED_PAT = re.compile(
    r"void ProxyReadNamedVariableResponse::cacheDataSet\(ClientDataSet dataSet\)\{(?P<body>.*?)\n\}",
    re.S,
)
READ_PAT = re.compile(
    r"void ProxyReadVariableResponse::cacheValue\(MmsValue\* value\)\{(?P<body>.*?)\n\}",
    re.S,
)


def patch_named(match: re.Match) -> str:
    body = match.group("body")
    if "== nullptr" in body:
        return match.group(0)
    body = body.replace(
        "\tm_dataSet = dataSet;",
        "\tm_dataSet = dataSet;\n\tif(m_dataSet == nullptr){\n\t\treturn;\n\t}",
        1,
    )
    body = body.replace(
        "ClientDataSet_getValues(dataSet)",
        "ClientDataSet_getValues(m_dataSet)",
    )
    body = body.replace(
        "\tint datasetSize = MmsValue_getArraySize(values);",
        "\tif(values == nullptr){\n\t\treturn;\n\t}\n\tint datasetSize = MmsValue_getArraySize(values);",
        1,
    )
    return "void ProxyReadNamedVariableResponse::cacheDataSet(ClientDataSet dataSet){" + body + "\n}"


def patch_read(match: re.Match) -> str:
    body = match.group("body")
    if "== nullptr" in body:
        return match.group(0)
    body = body.replace(
        "\tm_value = value;",
        "\tm_value = value;\n\tif(m_value == nullptr){\n\t\treturn;\n\t}",
        1,
    )
    body = body.replace(
        "\tint datasetSize = MmsValue_getArraySize(value);",
        "\tint datasetSize = MmsValue_getArraySize(m_value);\n\tif(datasetSize <= 0){\n\t\treturn;\n\t}",
        1,
    )
    return "void ProxyReadVariableResponse::cacheValue(MmsValue* value){" + body + "\n}"


def main() -> int:
    if not PASS:
        print("请先设置 SUBST_PASS", file=sys.stderr)
        return 1
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username=USER, password=PASS, timeout=15,
                   allow_agent=False, look_for_keys=False)
    sftp = client.open_sftp()
    with sftp.open(SRC, "rb") as fh:
        raw = fh.read()
    text = raw.decode("utf-8", "replace")
    crlf = "\r\n" in text
    text = text.replace("\r\n", "\n")

    new_text, n_named = NAMED_PAT.subn(patch_named, text, count=1)
    new_text, n_read = READ_PAT.subn(patch_read, new_text, count=1)
    if n_named != 1:
        print("named block not found; abort", file=sys.stderr)
        sftp.close()
        client.close()
        return 2

    if new_text == text:
        print("already patched")
        sftp.close()
        client.close()
        return 0

    out = new_text.replace("\n", "\r\n") if crlf else new_text
    backup = SRC + ".bak_pre_guard"
    try:
        sftp.stat(backup)
    except OSError:
        sftp.rename(SRC, backup)
    with sftp.open(SRC, "wb") as fh:
        fh.write(out.encode("utf-8"))
    print(f"patched {SRC}: named={n_named} read={n_read} (backup {backup})")
    sftp.close()
    client.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
