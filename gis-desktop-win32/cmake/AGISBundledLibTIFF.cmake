# Build libtiff for PROJ ENABLE_TIFF (grid TIFF). 源码目录：3rdparty/tiff-4.7.1（不自动下载，缺则 FATAL_ERROR）。
# Subproject defaults to tiff-install OFF — ON so export() runs (optional; FindTIFF override uses target `tiff`).

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

set(_agis_tiff_local "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/tiff-4.7.1")
if(NOT AGIS_BUNDLED_LIBS_SHARED)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "libtiff: static (AGIS)" FORCE)
endif()
set(tiff-install ON CACHE BOOL "libtiff: export TiffConfig (PROJ find_package)" FORCE)
set(tiff-tools OFF CACHE BOOL "libtiff: skip tools" FORCE)
set(tiff-tests OFF CACHE BOOL "libtiff: skip tests" FORCE)
set(tiff-contrib OFF CACHE BOOL "libtiff: skip contrib" FORCE)
set(tiff-docs OFF CACHE BOOL "libtiff: skip docs" FORCE)

if(EXISTS "${_agis_tiff_local}/CMakeLists.txt")
  message(STATUS "AGIS: building libtiff from ${_agis_tiff_local}")
  set(_agis_tiff_bin "${CMAKE_BINARY_DIR}/agis_bundled_libtiff")
  add_subdirectory("${_agis_tiff_local}" "${_agis_tiff_bin}")
else()
  message(FATAL_ERROR
    "AGIS: 未找到 libtiff 源码树：${_agis_tiff_local}/CMakeLists.txt\n"
    "请自行下载官方包并解压到该路径（本工程不在 CMake 中自动下载）。\n"
    "例如：https://download.osgeo.org/libtiff/tiff-4.7.1.tar.gz\n"
    "解压后目录名应为 tiff-4.7.1，且与仓库中 3rdparty/tiff-4.7.1 对齐。")
endif()

unset(Tiff_DIR CACHE)
set(AGIS_LIBTIFF_AVAILABLE TRUE CACHE INTERNAL "")
