#!/usr/bin/env python3
"""仅配置（若需要）并**增量编译** ``ui_engine_demo``，不编译 ``agis_desktop`` / 地图引擎等本目录外源码。

``ui_engine_demo`` 目标只依赖 ``agis_ui_engine``（``ui_engine/`` 下库源码与 ``app/`` 演示），适合在 ``ui_engine`` 内迭代界面。

用法（在 ``ui_engine`` 目录）::

    python run_ui_demo.py

或自仓库根目录::

    python ui_engine/run_ui_demo.py

- **不调用** ``gis-desktop-win32/build.py``（该脚本会默认构建整个桌面工程）。
- 首次或 ``AGIS_FORCE_CONFIGURE=1`` 时在 ``gis-desktop-win32/build`` 执行 ``cmake -S .. -B build``；
  默认 ``-DAGIS_USE_GDAL=OFF`` 以加快配置（仅演示无需 GDAL；若需与桌面一致可设 ``AGIS_USE_GDAL=ON``）。
- 随后 ``cmake --build … --target ui_engine_demo`` 仅此目标。

其它环境变量：``CMAKE_CONFIG``（如 ``Debug``）、``AGIS_FORCE_CONFIGURE``、``AGIS_USE_GDAL``。
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from typing import Optional


def _desktop_project_dir() -> str:
    """``gis-desktop-win32`` 目录（与本脚本所在 ``ui_engine`` 为兄弟目录）。"""
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "gis-desktop-win32"))


def _cmake_prefix_path(desktop: str) -> Optional[str]:
    """与 ``gis-desktop-win32/build.py`` 类似，供 ``AGIS_USE_GDAL=ON`` 时查找依赖。"""
    parts: list[str] = []
    for key in ("AGIS_GDAL_PREFIX", "AGIS_PROJ_PREFIX"):
        v = os.environ.get(key, "").strip()
        if v:
            parts.append(os.path.normpath(v))
    for rel in (
        os.path.join(desktop, "..", "3rdparty", "proj-install"),
        os.path.join(desktop, "..", "3rdparty", "gdal-install"),
    ):
        p = os.path.normpath(rel)
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
        k = p.lower()
        if k not in seen:
            seen.add(k)
            out.append(p)
    return ";".join(x.replace("\\", "/") for x in out)


def _ensure_configure(desktop: str, build: str) -> int:
    cmake = shutil.which("cmake")
    if not cmake:
        print("cmake not found in PATH", file=sys.stderr)
        return 1

    cache = os.path.join(build, "CMakeCache.txt")
    force = os.environ.get("AGIS_FORCE_CONFIGURE", "").strip().lower() in ("1", "true", "yes", "on")
    if os.path.isfile(cache) and not force:
        print("CMake: using existing", cache, "(set AGIS_FORCE_CONFIGURE=1 to reconfigure)")
        return 0

    gdal_env = os.environ.get("AGIS_USE_GDAL", "").strip().lower()
    if gdal_env:
        use_gdal = gdal_env not in ("0", "off", "false", "no")
    else:
        use_gdal = False

    cmd = [
        cmake,
        "-S",
        desktop,
        "-B",
        build,
        "-DCMAKE_BUILD_TYPE=Release",
        "-DAGIS_USE_GDAL=" + ("ON" if use_gdal else "OFF"),
    ]
    merged = _cmake_prefix_path(desktop)
    if merged:
        cmd.append("-DCMAKE_PREFIX_PATH=" + merged)
    print("CMake configure:", " ".join(cmd))
    return subprocess.run(cmd).returncode


def main() -> int:
    desktop = _desktop_project_dir()
    build = os.path.join(desktop, "build")

    r = _ensure_configure(desktop, build)
    if r != 0:
        return r

    cmake = shutil.which("cmake")
    if not cmake:
        print("cmake not found in PATH", file=sys.stderr)
        return 1

    cfg = os.environ.get("CMAKE_CONFIG", "").strip() or "Release"
    try:
        cache = os.path.join(build, "CMakeCache.txt")
        if os.path.isfile(cache):
            with open(cache, encoding="utf-8", errors="replace") as f:
                for line in f:
                    if line.startswith("CMAKE_BUILD_TYPE:"):
                        t = line.split("=", 1)
                        if len(t) == 2:
                            v = t[1].strip()
                            if v:
                                cfg = v
                        break
    except OSError:
        pass

    b = [cmake, "--build", build, "--config", cfg, "--target", "ui_engine_demo"]
    subprocess.check_call(b)

    exe = os.path.join(build, cfg, "ui_engine_demo.exe")
    if not os.path.isfile(exe):
        print("error: expected exe not found:", exe, file=sys.stderr)
        return 1

    os.chdir(os.path.dirname(exe))
    return subprocess.call([exe])


if __name__ == "__main__":
    raise SystemExit(main())
