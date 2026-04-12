#!/usr/bin/env python3
"""Configure and build AGIS (CMake).

调用顺序固定：**先** ``cmake -B`` **再** ``cmake --build``；增量编译由 **MSBuild / Ninja** 等后端决定。**并行度**：默认 ``cmake --build --parallel <逻辑核数>``；可用 ``AGIS_BUILD_JOBS`` 或 ``CMAKE_BUILD_PARALLEL_LEVEL`` 覆盖。

- **快速迭代**：``--quick`` 仅构建 ``agis_desktop``（依赖仍会增量编译），明显缩短改代码后的等待；发版/全量仍用默认（不传 ``--quick``）。也可用环境变量 ``AGIS_BUILD_TARGETS=agis_desktop,map_engine_demo``（逗号分隔）覆盖目标列表。
- **Ninja**：``--ninja`` 使用生成器 Ninja 与 ``build-ninja`` 目录（需 ``ninja`` 在 PATH）；常与更快增量搭配。也可使用预设 ``cmake --preset win-ninja`` 后 ``cmake --build build-ninja``。
- **IDE**：配置时开启 ``CMAKE_EXPORT_COMPILE_COMMANDS``，并把 ``compile_commands.json`` 复制到仓库根，便于 clangd / Cursor C++ 索引。
- **MSVC**：顶层 CMake 为 C/CXX 开启 ``/MP``（单 cl 内多文件并行），缩短大目标的编译阶段。

- **仓库根目录**：``REPO_ROOT`` 为 ``gis-desktop-win32`` 的父目录。
- **GDAL**：未设置 ``AGIS_USE_GDAL`` 时传入 ``AGIS_USE_GDAL=ON``；仅无 GIS 壳时设 ``AGIS_USE_GDAL=off``。
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from typing import Optional

ROOT = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.normpath(os.path.join(ROOT, ".."))
BUILD_VS = os.path.join(ROOT, "build")
BUILD_NINJA = os.path.join(ROOT, "build-ninja")

# 与 CMakeLists.txt 中 add_executable 对齐；新增 exe 时请同步更新。
_AGIS_CONVERT_CLI = (
    "agis_convert_gis_to_model",
    "agis_convert_gis_to_tile",
    "agis_convert_model_to_gis",
    "agis_convert_model_to_model",
    "agis_convert_model_to_tile",
    "agis_convert_tile_to_gis",
    "agis_convert_tile_to_model",
)


def all_program_targets() -> list[str]:
    """本工程内全部可执行 CMake 目标（用于 ``cmake --build ... --target``）。"""
    out: list[str] = [
        "agis_desktop",
        "agis_convert_gui",
        "agis_model_preview_gui",
        "agis_tile_preview_gui",
        "ui_engine_demo",
        *_AGIS_CONVERT_CLI,
    ]
    if sys.platform == "win32":
        out.insert(5, "map_engine_demo")
    return out


def resolve_build_targets(args: argparse.Namespace) -> list[str]:
    raw = (args.targets or "").strip()
    if raw:
        return [x.strip() for x in raw.split(",") if x.strip()]
    env = os.environ.get("AGIS_BUILD_TARGETS", "").strip()
    if env:
        return [x.strip() for x in env.split(",") if x.strip()]
    if args.quick:
        return ["agis_desktop"]
    return all_program_targets()


def _norm(p: str) -> str:
    return os.path.normpath(p)


def build_parallel_jobs() -> int:
    """供 ``cmake --build --parallel N`` 使用：优先读环境变量，否则为逻辑 CPU 数（至少 1）。"""
    for key in ("AGIS_BUILD_JOBS", "CMAKE_BUILD_PARALLEL_LEVEL"):
        raw = os.environ.get(key, "").strip()
        if not raw:
            continue
        try:
            return max(1, int(raw))
        except ValueError:
            print(f"warning: ignore invalid {key}={raw!r}, fall back to cpu_count()", file=sys.stderr)
    n = os.cpu_count()
    return max(1, n if n is not None else 1)


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


def _copy_compile_commands(build_dir: str) -> None:
    src = os.path.join(build_dir, "compile_commands.json")
    if not os.path.isfile(src):
        return
    dst = os.path.join(REPO_ROOT, "compile_commands.json")
    try:
        shutil.copy2(src, dst)
        print(f"IDE: compile_commands.json -> {dst}", file=sys.stderr)
    except OSError as e:
        print(f"warning: copy compile_commands.json failed: {e}", file=sys.stderr)


def _reconfigure_gdal_after_bundled_deps(cmake: str, cfg: list[str], jobs: int, build_dir: str, use_msvc_config: bool) -> None:
    """PROJ builds ``agis_sqlite3``; bundled Expat builds ``expat``. GDAL needs those ``.lib`` at configure time."""
    pre: list[str] = [
        cmake,
        "--build",
        build_dir,
        "--parallel",
        str(jobs),
        "--target",
        "agis_sqlite3",
        "--target",
        "expat",
    ]
    if use_msvc_config:
        pre.extend(["--config", "Release"])
    r = subprocess.run(pre, cwd=ROOT)
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
    parser = argparse.ArgumentParser(description="Configure and build AGIS (gis-desktop-win32)")
    parser.add_argument(
        "--quick",
        action="store_true",
        help="仅构建 agis_desktop，缩短日常改代码后的构建时间（默认构建全部 exe）",
    )
    parser.add_argument(
        "--targets",
        type=str,
        default="",
        metavar="T1,T2",
        help="逗号分隔的 CMake 目标，覆盖 --quick / 默认全量",
    )
    parser.add_argument(
        "--ninja",
        action="store_true",
        help="使用 Ninja + gis-desktop-win32/build-ninja（需 ninja 在 PATH；与 Visual Studio 的 build/ 互不兼容）",
    )
    args = parser.parse_args()

    if args.ninja and not shutil.which("ninja"):
        print("error: --ninja requires `ninja` in PATH", file=sys.stderr)
        return 1

    cmake = shutil.which("cmake")
    if not cmake:
        print("cmake not found in PATH", file=sys.stderr)
        return 1

    build_dir = BUILD_NINJA if args.ninja else BUILD_VS
    use_msvc_config = not args.ninja
    targets = resolve_build_targets(args)

    gdal_env = os.environ.get("AGIS_USE_GDAL", "").strip().lower()
    if not gdal_env:
        use_gdal = True
    else:
        use_gdal = gdal_env not in ("0", "off", "false", "no")

    cfg = [cmake, "-B", build_dir, "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    if args.ninja:
        cfg.extend(["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"])
    else:
        cfg.append("-DCMAKE_BUILD_TYPE=Release")
    cfg.append("-DAGIS_USE_GDAL=" + ("ON" if use_gdal else "OFF"))
    merged = cmake_prefix_path()
    if merged:
        cfg.append("-DCMAKE_PREFIX_PATH=" + merged)
        print("CMAKE_PREFIX_PATH:", merged.replace(";", "\n  "), file=sys.stderr)
    if use_gdal:
        print(
            "AGIS_USE_GDAL=ON: ensure PROJ is built from source (see 3rdparty/README-GDAL-BUILD.md).",
            file=sys.stderr,
        )

    subprocess.check_call(cfg)
    _copy_compile_commands(build_dir)

    jobs = build_parallel_jobs()
    print(f"Build parallel jobs: {jobs} (override with AGIS_BUILD_JOBS or CMAKE_BUILD_PARALLEL_LEVEL)", file=sys.stderr)
    print("Build targets:", " ".join(targets), file=sys.stderr)

    if use_gdal:
        _reconfigure_gdal_after_bundled_deps(cmake, cfg, jobs, build_dir, use_msvc_config)
        _copy_compile_commands(build_dir)

    build_cmd = [cmake, "--build", build_dir, "--parallel", str(jobs)]
    if use_msvc_config:
        build_cmd.extend(["--config", "Release"])
    for t in targets:
        build_cmd.extend(["--target", t])
    subprocess.check_call(build_cmd)
    print("Build OK:", build_dir, "| REPO_ROOT:", REPO_ROOT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
