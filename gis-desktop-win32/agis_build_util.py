#!/usr/bin/env python3
"""Shared helpers: run build.py so test/run/publish always get a fresh incremental build."""

import os
import subprocess
import sys

_ROOT = os.path.dirname(os.path.abspath(__file__))


def project_root() -> str:
    return _ROOT


def ensure_project_built() -> int:
    """
    Run build.py (configure + cmake --build). Incremental when sources unchanged.
    Set AGIS_SKIP_BUILD=1 to skip (e.g. CI already built).
    """
    skip = os.environ.get("AGIS_SKIP_BUILD", "").strip().lower()
    if skip in ("1", "true", "yes", "on"):
        return 0
    build_py = os.path.join(_ROOT, "build.py")
    return subprocess.call([sys.executable, build_py], cwd=_ROOT)
