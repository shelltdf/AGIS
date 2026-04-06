#!/usr/bin/env python3
"""先运行 ``build.py``，再将 ``AGIS.exe`` 复制到 ``dist/``。"""
import glob
import os
import shutil
import sys
from typing import Optional

from agis_build_util import ensure_project_built

ROOT = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.join(ROOT, "dist")


def find_exe() -> Optional[str]:
    patterns = [
        os.path.join(ROOT, "build", "Release", "AGIS.exe"),
        os.path.join(ROOT, "build", "Debug", "AGIS.exe"),
        os.path.join(ROOT, "build", "AGIS.exe"),
    ]
    for p in patterns:
        if os.path.isfile(p):
            return p
    for path in glob.glob(os.path.join(ROOT, "build", "**", "AGIS.exe"), recursive=True):
        return path
    return None


def main() -> int:
    code = ensure_project_built()
    if code != 0:
        return code
    exe = find_exe()
    if not exe:
        print("AGIS.exe not found after build", file=sys.stderr)
        return 1
    os.makedirs(DIST, exist_ok=True)
    dst = os.path.join(DIST, "AGIS.exe")
    shutil.copy2(exe, dst)
    print("Published:", dst)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
