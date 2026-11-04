#!/usr/bin/env python3
"""串联：生成配置 -> 2405 下装 -> 可选 MMS 探测。"""
from __future__ import annotations

import argparse

from lab_ops import run_full_pipeline, run_generate


def main() -> int:
    parser = argparse.ArgumentParser(description="仿真主站实验室一键脚本")
    parser.add_argument("--host", default="192.168.0.10")
    parser.add_argument("--port", type=int, default=2405)
    parser.add_argument("--skip-upload", action="store_true", help="仅生成 out/ 不下装")
    parser.add_argument("--mms-check", action="store_true", help="下装后探测 102 端口")
    args = parser.parse_args()

    if args.skip_upload:
        run_generate(log=print)
        print("已跳过下装（--skip-upload）")
        return 0

    return run_full_pipeline(
        host=args.host,
        port=args.port,
        mms_check=args.mms_check,
        log=print,
    )


if __name__ == "__main__":
    raise SystemExit(main())
