#!/usr/bin/env python3
"""Configure and build AGIS (CMake).

- **GDAL default ON** when `../3rdparty/gdal-3.12.3/CMakeLists.txt` exists; set **`AGIS_USE_GDAL=off`**
  to build a shell without GIS (no PROJ/SQLite needed).
- With GDAL on, CMake uses bundled **proj-9.8.0** / **gdal-3.12.3** or existing `*-install` prefixes; see
  `3rdparty/README-GDAL-BUILD.md`.

`test.py` / `run.py` / `publish.py` use `agis_build_util.ensure_project_built()` which **skips**
invoking this script when ``AGIS.exe`` is newer than ``src/**`` and CMake inputs (set
``AGIS_ALWAYS_BUILD=1`` to force).

This script **only re-runs ``cmake -B`` (configure)** when ``CMakeCache.txt`` is missing or
CMake-related files are newer than the cache; otherwise only ``cmake --build`` runs
(incremental link/compile). Use ``AGIS_FORCE_CONFIGURE=1`` to always configure.
"""
import os
import shutil
import subprocess
import sys
from typing import Optional


ROOT = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(ROOT, "build")


def _norm(p: str) -> str:
    return os.path.normpath(p)


def cmake_prefix_path() -> Optional[str]:
    """Merge AGIS / 3rdparty install prefixes for find_package(PROJ/GDAL)."""
    parts: list[str] = []
    env = os.environ.get("AGIS_GDAL_PREFIX", "").strip()
    if env:
        parts.append(_norm(env))
    envp = os.environ.get("AGIS_PROJ_PREFIX", "").strip()
    if envp:
        parts.append(_norm(envp))
    for rel in (
        os.path.join(ROOT, "..", "3rdparty", "proj-install"),
        os.path.join(ROOT, "..", "3rdparty", "gdal-install"),
    ):
        p = _norm(rel)
        if os.path.isdir(p):
            parts.append(p)
    existing = os.environ.get("CMAKE_PREFIX_PATH", "").strip()
    if existing:
        parts.extend(x.strip() for x in existing.split(os.pathsep) if x.strip())
    if not parts:
        return None
    # stable unique order
    seen: set[str] = set()
    out: list[str] = []
    for p in parts:
        key = p.lower()
        if key not in seen:
            seen.add(key)
            out.append(p)
    return ";".join(x.replace("\\", "/") for x in out)


def needs_cmake_configure() -> bool:
    """True if we must run ``cmake -B`` (first time or CMake inputs changed vs cache)."""
    env = os.environ.get("AGIS_FORCE_CONFIGURE", "").strip().lower()
    if env in ("1", "true", "yes", "on"):
        return True
    cache = os.path.join(BUILD, "CMakeCache.txt")
    if not os.path.isfile(cache):
        return True
    try:
        t_cache = os.path.getmtime(cache)
    except OSError:
        return True

    def cmake_input_paths() -> list[str]:
        out: list[str] = []
        cm = os.path.join(ROOT, "CMakeLists.txt")
        if os.path.isfile(cm):
            out.append(cm)
        mf = os.path.join(ROOT, "app.manifest")
        if os.path.isfile(mf):
            out.append(mf)
        cmake_dir = os.path.join(ROOT, "cmake")
        if os.path.isdir(cmake_dir):
            for dp, _, fns in os.walk(cmake_dir):
                for fn in fns:
                    if fn.endswith((".cmake", ".txt")):
                        out.append(os.path.join(dp, fn))
        return out

    for p in cmake_input_paths():
        try:
            if os.path.isfile(p) and os.path.getmtime(p) > t_cache:
                return True
        except OSError:
            return True
    return False


def cmake_build_args() -> list[str]:
    """Args after ``cmake`` for ``--build`` (parallelism).

    On Windows, unbounded parallel MSVC builds of large trees (e.g. bundled GDAL) can hit
    MSB6003 / C1083 Permission denied on ``.obj`` / ``.tlog`` when many ``cl.exe`` / MSBuild
    tasks contend, or when two builds target the same ``build/`` dir at once.

    - **AGIS_BUILD_PARALLEL**: ``1`` | ``4`` | ``max`` | empty (default below).
    - Default on **win32**: ``--parallel N`` with ``N = max(1, min(8, cpu//2))``.
    - Default elsewhere: ``--parallel`` (tool default, usually all cores).
    """
    cmd = ["--build", BUILD, "--config", "Release"]
    env = os.environ.get("AGIS_BUILD_PARALLEL", "").strip()
    if env:
        el = env.lower()
        if el in ("1", "single", "one"):
            cmd.extend(["--parallel", "1"])
        elif el in ("max", "all"):
            cmd.append("--parallel")
        elif env.isdigit() and int(env) >= 1:
            cmd.extend(["--parallel", env])
        else:
            print(f"warning: ignoring invalid AGIS_BUILD_PARALLEL={env!r}, using Windows default", file=sys.stderr)
            if sys.platform == "win32":
                cpu = os.cpu_count() or 4
                cmd.extend(["--parallel", str(max(1, min(8, cpu // 2)))])
            else:
                cmd.append("--parallel")
        return cmd
    if sys.platform == "win32":
        cpu = os.cpu_count() or 4
        n = max(1, min(8, cpu // 2))
        cmd.extend(["--parallel", str(n)])
    else:
        cmd.append("--parallel")
    return cmd


def main() -> int:
    os.chdir(ROOT)
    cmake = shutil.which("cmake")
    if not cmake:
        print("cmake not found in PATH", file=sys.stderr)
        return 1
    gdal_env = os.environ.get("AGIS_USE_GDAL", "").strip().lower()
    bundled_gdal = os.path.isfile(
        os.path.join(ROOT, "..", "3rdparty", "gdal-3.12.3", "CMakeLists.txt")
    )
    if not gdal_env:
        use_gdal = bundled_gdal
    else:
        use_gdal = gdal_env not in ("0", "off", "false", "no")
    cfg = [cmake, "-B", BUILD, "-DCMAKE_BUILD_TYPE=Release"]
    cfg.append("-DAGIS_USE_GDAL=" + ("ON" if use_gdal else "OFF"))
    merged = cmake_prefix_path()
    if merged:
        cfg.append("-DCMAKE_PREFIX_PATH=" + merged)
        print("CMAKE_PREFIX_PATH:", merged.replace(";", "\n  "))
    if use_gdal:
        print(
            "AGIS_USE_GDAL=ON: ensure PROJ is built from source (see 3rdparty/README-GDAL-BUILD.md).",
            file=sys.stderr,
        )
    if needs_cmake_configure():
        subprocess.check_call(cfg)
    else:
        print("CMake configure skipped (unchanged); use AGIS_FORCE_CONFIGURE=1 to re-run.")
    bargs = [cmake] + cmake_build_args()
    if sys.platform == "win32" and not os.environ.get("AGIS_BUILD_PARALLEL", "").strip():
        print(
            "Windows build: using limited parallelism (see AGIS_BUILD_PARALLEL in build.py). "
            "Do not run two builds on the same build/ folder.",
            file=sys.stderr,
        )
    subprocess.check_call(bargs)
    print("Build OK:", BUILD)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
