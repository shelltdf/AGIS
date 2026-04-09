#!/usr/bin/env python3
"""Configure and build AGIS (CMake).

调用顺序固定：**先** ``cmake -B`` **再** ``cmake --build``；是否增量编译/链接由 **MSBuild / Ninja** 等后端自行决定，本脚本不对「是否需要配置」或「并行度」做启发式判断。

- **GDAL**：未设置 ``AGIS_USE_GDAL`` 时向 CMake 传入 ``AGIS_USE_GDAL=ON``（与 ``CMakeLists.txt`` 一致）。仅需无 GIS 壳程序时设 ``AGIS_USE_GDAL=off``。
- 启用 GDAL 时若需 SQLite/Expat 预构建，仍由 ``_reconfigure_gdal_after_bundled_deps`` 在配置后尝试（与增量判断无关；详见 ``3rdparty/README-GDAL-BUILD.md``）。
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
    seen: set[str] = set()
    out: list[str] = []
    for p in parts:
        key = p.lower()
        if key not in seen:
            seen.add(key)
            out.append(p)
    return ";".join(x.replace("\\", "/") for x in out)


def _reconfigure_gdal_after_bundled_deps(cmake: str, cfg: list[str]) -> None:
    """PROJ builds ``agis_sqlite3``; bundled Expat builds ``expat``. GDAL needs those ``.lib`` at configure time."""
    r = subprocess.run(
        [cmake, "--build", BUILD, "--config", "Release", "--target", "agis_sqlite3", "--target", "expat"],
        cwd=ROOT,
    )
    if r.returncode != 0:
        print(
            "warning: prebuild of agis_sqlite3/expat failed; OGR OSM/GPKG or Expat-based drivers may remain disabled. "
            "Build those targets manually then run `cmake -B build ...` again.",
            file=sys.stderr,
        )
        return
    cfg2 = list(cfg)
    for uvar in (
        "GDAL_USE_SQLITE3",
        "SQLite3_LIBRARY",
        "OGR_ENABLE_DRIVER_SQLITE",
        "OGR_ENABLE_DRIVER_OSM",
        "OGR_ENABLE_DRIVER_GPKG",
        "OGR_ENABLE_DRIVER_MVT",
        "GDAL_ENABLE_DRIVER_MBTILES",
        "GDAL_USE_EXPAT",
        "EXPAT_INCLUDE_DIR",
        "EXPAT_LIBRARY",
        "EXPAT_USE_STATIC_LIBS",
    ):
        cfg2.extend(["-U", uvar])
    print("Re-running CMake so bundled GDAL picks up SQLite3 + Expat...", file=sys.stderr)
    subprocess.check_call(cfg2)


def main() -> int:
    os.chdir(ROOT)
    cmake = shutil.which("cmake")
    if not cmake:
        print("cmake not found in PATH", file=sys.stderr)
        return 1
    gdal_env = os.environ.get("AGIS_USE_GDAL", "").strip().lower()
    if not gdal_env:
        use_gdal = True
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
    subprocess.check_call(cfg)
    if use_gdal:
        _reconfigure_gdal_after_bundled_deps(cmake, cfg)
    subprocess.check_call([cmake, "--build", BUILD, "--config", "Release", "--parallel"])
    print("Build OK:", BUILD)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
