#!/usr/bin/env python3
"""配置并构建 `gis-desktop-win32` 的 `ui_engine_demo`，然后启动该可执行文件。

用法（在仓库根目录）::

    python run_demo.py

环境变量与 `gis-desktop-win32/build.py` 一致（如 ``AGIS_USE_GDAL``、``AGIS_FORCE_CONFIGURE``）；
本脚本在完整构建后仅增量编译 ``ui_engine_demo`` 目标以节省时间。
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys


def _repo_root() -> str:
    return os.path.dirname(os.path.abspath(__file__))


def main() -> int:
    root = _repo_root()
    proj = os.path.join(root, "gis-desktop-win32")
    build = os.path.join(proj, "build")
    if not os.path.isdir(proj):
        print("error: gis-desktop-win32 not found next to run_demo.py", file=sys.stderr)
        return 1

    # 先走与主工程相同的 configure + 全量构建，保证 CMake 缓存与依赖一致
    r = subprocess.run([sys.executable, os.path.join(proj, "build.py")], cwd=proj)
    if r.returncode != 0:
        return r.returncode

    cmake = shutil.which("cmake")
    if not cmake:
        print("cmake not found in PATH", file=sys.stderr)
        return 1

    # MSVC 多配置生成器通常不写 CMAKE_BUILD_TYPE；与 build.py 一致默认 Release
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
