#!/usr/bin/env python3
"""供 ``test.py`` / ``run.py`` / ``publish.py`` 调用：始终执行 ``build.py``，由 CMake/MSBuild 决定增量编译。"""

import os
import subprocess
import sys

_ROOT = os.path.dirname(os.path.abspath(__file__))


def project_root() -> str:
    return _ROOT


def ensure_project_built() -> int:
    build_py = os.path.join(_ROOT, "build.py")
    return subprocess.call([sys.executable, build_py], cwd=_ROOT)
