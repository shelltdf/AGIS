#!/usr/bin/env python3
"""配置并构建 ``gis-desktop-win32`` 中的 ``ui_engine_demo``，然后启动该可执行文件。

用法（在 ``ui_engine`` 目录或任意目录）::

    python run_ui_demo.py

或自仓库根目录::

    python ui_engine/run_ui_demo.py

环境变量与 ``gis-desktop-win32/build.py`` 一致（如 ``AGIS_USE_GDAL``、``AGIS_FORCE_CONFIGURE``）；
完整构建后仅增量编译 ``ui_engine_demo`` 目标以节省时间。
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys


def _desktop_project_dir() -> str:
    """``gis-desktop-win32`` 目录（与本脚本所在 ``ui_engine`` 为兄弟目录）。"""
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "gis-desktop-win32"))


def main() -> int:
    proj = _desktop_project_dir()
    build = os.path.join(proj, "build")

    r = subprocess.run([sys.executable, os.path.join(proj, "build.py")], cwd=proj)
    if r.returncode != 0:
        return r.returncode

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
