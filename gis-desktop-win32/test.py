#!/usr/bin/env python3
"""Smoke test: build if needed (same freshness rules as run.py), then verify AGIS.exe exists."""
import glob
import os
import sys

from agis_build_util import ensure_project_built

ROOT = os.path.dirname(os.path.abspath(__file__))


def main() -> int:
    code = ensure_project_built()
    if code != 0:
        return code
    patterns = [
        os.path.join(ROOT, "build", "Release", "AGIS.exe"),
        os.path.join(ROOT, "build", "Debug", "AGIS.exe"),
        os.path.join(ROOT, "build", "AGIS.exe"),
    ]
    for p in patterns:
        if os.path.isfile(p):
            print("Found:", p)
            return 0
    for path in glob.glob(os.path.join(ROOT, "build", "**", "AGIS.exe"), recursive=True):
        print("Found:", path)
        return 0
    print("AGIS.exe not found after build", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
