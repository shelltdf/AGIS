# Bundle LASzip from ../3rdparty/LASzip for LAZ decompression (model preview).
# Sources: GitHub Release tarball `laszip-src-3.4.4.tar.gz`（勿用 git clone）。该包解压后顶层目录名可能为
# `laszip-src-3.4.4.tar.gz`（与压缩包同名），请重命名为本仓库约定的 `LASzip/`（见 ../3rdparty/README-LASZIP.md）。
# Link only the main library target (laszip[3]): it contains laszip_dll API implementations.
# Do not link laszip_api[3] (laszip_api.c); that translation unit loads LASzip DLL at runtime.

set(AGIS_LASZIP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/LASzip" CACHE PATH
  "LASzip source root (extracted zip; see 3rdparty/README-LASZIP.md)")

if(NOT EXISTS "${AGIS_LASZIP_ROOT}/CMakeLists.txt")
  set(AGIS_HAVE_LASZIP FALSE)
  message(STATUS
          "AGIS: LASzip not found at ${AGIS_LASZIP_ROOT} (AGIS_USE_LASZIP=ON: place sources per README-LASZIP.md; AGIS_HAVE_LASZIP stays OFF until then)")
  return()
endif()

set(AGIS_LASZIP_BINARY_DIR "${CMAKE_BINARY_DIR}/agis_bundled_laszip")
set(LASZIP_BUILD_STATIC ON CACHE BOOL "LASzip: static library for AGIS" FORCE)

add_subdirectory("${AGIS_LASZIP_ROOT}" "${AGIS_LASZIP_BINARY_DIR}")

if(WIN32)
  set(AGIS_LASZIP_TARGET laszip3)
else()
  set(AGIS_LASZIP_TARGET laszip)
endif()
if(NOT TARGET "${AGIS_LASZIP_TARGET}")
  message(FATAL_ERROR "AGIS: LASzip target '${AGIS_LASZIP_TARGET}' missing after add_subdirectory")
endif()

# 与根 CMakeLists 中 CMAKE_MSVC_RUNTIME_LIBRARY 一致（/MD 与 /MDd），避免 LASzip 子工程在 CMP0091
# 下与主目标 CRT 不一致，导致 Release 链接时出现 __imp__calloc_dbg / __imp__CrtDbgReport（Debug 堆）。
if(MSVC)
  set_target_properties("${AGIS_LASZIP_TARGET}" PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

set(AGIS_HAVE_LASZIP TRUE)
message(STATUS "AGIS: bundled LASzip (static) from ${AGIS_LASZIP_ROOT}, target ${AGIS_LASZIP_TARGET}")
