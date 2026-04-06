#!/usr/bin/env python3
"""Shared helpers for test/run/publish: optional fast skip + incremental build via build.py.

Freshness (vs ``AGIS.exe`` mtime) — if any item below is newer, ``ensure_project_built()`` runs
``build.py`` (then CMake performs its own incremental compile/link):

- This directory: ``build.py``, ``run.py``, ``test.py``, ``publish.py``, ``agis_build_util.py``
- ``src/**`` (desktop / map_engine sources)
- ``cmake/**``, ``CMakeLists.txt``, ``app.manifest``
- Sibling ``../ui_engine/**`` (same source extensions as ``src/``; linked by CMake)

Override: ``AGIS_ALWAYS_BUILD=1`` to always build; ``AGIS_SKIP_BUILD=1`` to never invoke build.
"""

import glob
import os
import subprocess
import sys
from typing import Optional

_ROOT = os.path.dirname(os.path.abspath(__file__))

# Match typical compile inputs (CMakeLists also lists .mm for Apple).
_CODE_SUFFIXES = (".cpp", ".h", ".hpp", ".c", ".hxx", ".rc", ".def", ".mm", ".inl")


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


def _tree_code_newer_than(root: str, t_ref: float) -> bool:
    """True if any file under ``root`` with a code-like suffix is newer than ``t_ref``."""
    if not os.path.isdir(root):
        return False
    for dp, _, fns in os.walk(root):
        for fn in fns:
            if fn.endswith(_CODE_SUFFIXES):
                fp = os.path.join(dp, fn)
                if os.path.isfile(fp) and os.path.getmtime(fp) > t_ref:
                    return True
    return False


def _build_scripts_newer_than_exe(exe_mtime: float) -> bool:
    """Project-local tooling that invokes or affects the build pipeline."""
    for name in ("build.py", "run.py", "test.py", "publish.py", "agis_build_util.py"):
        p = os.path.join(_ROOT, name)
        if os.path.isfile(p) and os.path.getmtime(p) > exe_mtime:
            return True
    return False


def _ui_engine_newer_than_exe(exe_mtime: float) -> bool:
    """Sibling ``ui_engine`` (see ``CMakeLists.txt`` ``AGIS_UI_ENGINE_DIR``)."""
    ui = os.path.normpath(os.path.join(_ROOT, "..", "ui_engine"))
    return _tree_code_newer_than(ui, exe_mtime)


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
    return _tree_code_newer_than(src, exe_mtime)


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
    if _build_scripts_newer_than_exe(t):
        return True
    if _cmake_related_files_newer_than_exe(t):
        return True
    if _sources_newer_than_exe(t):
        return True
    if _ui_engine_newer_than_exe(t):
        return True
    return False


def ensure_project_built() -> int:
    """
    Run build.py when tooling scripts, CMake inputs, ``src/``, or ``../ui_engine`` are newer than
    AGIS.exe (or exe missing). See module docstring for the file set.

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
