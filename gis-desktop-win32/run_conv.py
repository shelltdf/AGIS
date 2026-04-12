#!/usr/bin/env python3
"""先运行 ``build.py``，再启动 ``AGIS-Convert.exe``。"""
import glob
import os
import subprocess
import sys
from typing import Optional

from agis_build_util import ensure_project_built

ROOT = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.normpath(os.path.join(ROOT, ".."))
BUILD_ROOT = os.path.join(REPO_ROOT, "build")


def find_exe() -> Optional[str]:
    patterns = [
        os.path.join(BUILD_ROOT, "Release", "AGIS-Convert.exe"),
        os.path.join(BUILD_ROOT, "Debug", "AGIS-Convert.exe"),
        os.path.join(BUILD_ROOT, "AGIS-Convert.exe"),
    ]
    for p in patterns:
        if os.path.isfile(p):
            return p
    for path in glob.glob(os.path.join(BUILD_ROOT, "**", "AGIS-Convert.exe"), recursive=True):
        return path
    return None


def main() -> int:
    code = ensure_project_built()
    if code != 0:
        return code
    exe = find_exe()
    if not exe:
        print("AGIS-Convert.exe not found after build", file=sys.stderr)
        return 1
    return subprocess.call([exe])


if __name__ == "__main__":
    raise SystemExit(main())

