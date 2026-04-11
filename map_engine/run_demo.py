#!/usr/bin/env python3
"""配置并仅构建 ``map_engine_demo`` 目标（工程树在 ``gis-desktop-win32``）。

每次均执行 ``cmake -S … -B …`` 与 ``cmake --build … --target map_engine_demo``；增量由 CMake/MSBuild 处理。

环境变量：``CMAKE_CONFIG``（默认 ``Release``）、``AGIS_USE_GDAL``（与 ``gis-desktop-win32/build.py`` 语义一致：未设置则 ``ON``）、``CMAKE_PREFIX_PATH`` / ``AGIS_*_PREFIX`` 等同桌面构建。
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from typing import Optional


def _resolve_map_engine_demo_exe(build_dir: str, cfg: str) -> Optional[str]:
    """多配置生成器：``build/<cfg>/map_engine_demo.exe``；单配置（如 Ninja）：``build/map_engine_demo.exe``。"""
    candidates = [
        os.path.join(build_dir, cfg, "map_engine_demo.exe"),
        os.path.join(build_dir, "map_engine_demo.exe"),
    ]
    for p in candidates:
        if os.path.isfile(p):
            return os.path.normpath(os.path.abspath(p))
    return None


def _desktop_project_dir() -> str:
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "gis-desktop-win32"))


def _cmake_prefix_path(desktop: str) -> Optional[str]:
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


def main() -> int:
    desktop = _desktop_project_dir()
    build = os.path.join(desktop, "build")
    cmake = shutil.which("cmake")
    if not cmake:
        print("cmake not found in PATH", file=sys.stderr)
        return 1

    gdal_env = os.environ.get("AGIS_USE_GDAL", "").strip().lower()
    if not gdal_env:
        use_gdal = True
    else:
        use_gdal = gdal_env not in ("0", "off", "false", "no")

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
    subprocess.check_call(cmd)

    cfg = os.environ.get("CMAKE_CONFIG", "").strip() or "Release"
    subprocess.check_call(
        [cmake, "--build", build, "--config", cfg, "--parallel", "--target", "map_engine_demo"]
    )

    exe = _resolve_map_engine_demo_exe(build, cfg)
    if not exe:
        tried = os.path.join(build, cfg, "map_engine_demo.exe")
        alt = os.path.join(build, "map_engine_demo.exe")
        print(
            "error: map_engine_demo.exe not found (tried:",
            tried,
            "and",
            alt + ")",
            file=sys.stderr,
        )
        return 1

    exe_dir = os.path.dirname(exe)
    # 显式 cwd，避免依赖调用方当前工作目录；PATH 前置 exe 目录可兜底外部 GDAL 等依赖。
    env = os.environ.copy()
    env["PATH"] = exe_dir + os.pathsep + env.get("PATH", "")
    return subprocess.call([exe], cwd=exe_dir, env=env)


if __name__ == "__main__":
    raise SystemExit(main())
