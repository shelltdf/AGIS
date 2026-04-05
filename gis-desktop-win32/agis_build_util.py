#!/usr/bin/env python3
"""Shared helpers for test/run/publish: optional fast skip + incremental build via build.py."""

import glob
import os
import subprocess
import sys
from typing import Optional

_ROOT = os.path.dirname(os.path.abspath(__file__))


def project_root() -> str:
    return _ROOT


def _find_exe() -> Optional[str]:
    patterns = [
        os.path.join(_ROOT, "build", "Release", "AGIS.exe"),
        os.path.join(_ROOT, "build", "Debug", "AGIS.exe"),
        os.path.join(_ROOT, "build", "AGIS.exe"),
    ]
    for p in patterns:
        if os.path.isfile(p):
            return p
    for path in glob.glob(os.path.join(_ROOT, "build", "**", "AGIS.exe"), recursive=True):
        return path
    return None


def _any_path_newer_than(t_ref: float, paths: list[str]) -> bool:
    for p in paths:
        if os.path.isfile(p) and os.path.getmtime(p) > t_ref:
            return True
        if os.path.isdir(p):
            for dp, _, fns in os.walk(p):
                for fn in fns:
                    fp = os.path.join(dp, fn)
                    if os.path.isfile(fp) and os.path.getmtime(fp) > t_ref:
                        return True
    return False


def _cmake_related_files_newer_than_exe(exe_mtime: float) -> bool:
    """CMake configure inputs only (not .cpp), for consistency with build.py skip-configure."""
    extra: list[str] = []
    cm = os.path.join(_ROOT, "CMakeLists.txt")
    if os.path.isfile(cm):
        extra.append(cm)
    mf = os.path.join(_ROOT, "app.manifest")
    if os.path.isfile(mf):
        extra.append(mf)
    cmake_dir = os.path.join(_ROOT, "cmake")
    if os.path.isdir(cmake_dir):
        for dp, _, fns in os.walk(cmake_dir):
            for fn in fns:
                if fn.endswith((".cmake", ".txt")):
                    extra.append(os.path.join(dp, fn))
    return _any_path_newer_than(exe_mtime, extra)


def _sources_newer_than_exe(exe_mtime: float) -> bool:
    """True if any source under src/ is newer than exe."""
    src = os.path.join(_ROOT, "src")
    if not os.path.isdir(src):
        return True
    suffixes = (".cpp", ".h", ".hpp", ".c", ".hxx", ".rc", ".def")
    for dp, _, fns in os.walk(src):
        for fn in fns:
            if fn.endswith(suffixes):
                fp = os.path.join(dp, fn)
                if os.path.isfile(fp) and os.path.getmtime(fp) > exe_mtime:
                    return True
    return False


def should_run_build() -> bool:
    """
    Return False if we can skip invoking build.py (exe exists and nothing relevant changed).

    Override with AGIS_ALWAYS_BUILD=1 / true to always build.
    """
    force = os.environ.get("AGIS_ALWAYS_BUILD", "").strip().lower()
    if force in ("1", "true", "yes", "on"):
        return True
    exe = _find_exe()
    if not exe:
        return True
    try:
        t = os.path.getmtime(exe)
    except OSError:
        return True
    if _sources_newer_than_exe(t):
        return True
    if _cmake_related_files_newer_than_exe(t):
        return True
    return False


def ensure_project_built() -> int:
    """
    Run build.py when sources or CMake inputs are newer than AGIS.exe (or exe missing).

    - Set AGIS_SKIP_BUILD=1 to skip (e.g. CI already built).
    - Set AGIS_ALWAYS_BUILD=1 to always run build.py (ignore freshness).
    - When skipped, prints a short message to stderr.
    """
    skip = os.environ.get("AGIS_SKIP_BUILD", "").strip().lower()
    if skip in ("1", "true", "yes", "on"):
        return 0
    build_py = os.path.join(_ROOT, "build.py")
    if not should_run_build():
        print("Build skipped: AGIS.exe is up to date (use AGIS_ALWAYS_BUILD=1 to force).")
        return 0
    return subprocess.call([sys.executable, build_py], cwd=_ROOT)
