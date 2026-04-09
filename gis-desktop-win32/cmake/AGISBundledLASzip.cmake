# Bundle LASzip from ../3rdparty/LASzip for LAZ decompression (model preview).
# Sources: GitHub Release tarball `laszip-src-3.4.4.tar.gz`（勿用 git clone）。该包解压后顶层目录名可能为
# `laszip-src-3.4.4.tar.gz`（与压缩包同名），请重命名为本仓库约定的 `LASzip/`（见 ../3rdparty/README-LASZIP.md）。
# Link only the main library target (laszip[3]): it contains laszip_dll API implementations.
# Do not link laszip_api[3] (laszip_api.c); that translation unit loads LASzip DLL at runtime.

set(AGIS_LASZIP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/LASzip" CACHE PATH
  "LASzip source root (extracted zip; see 3rdparty/README-LASZIP.md)")

if(NOT EXISTS "${AGIS_LASZIP_ROOT}/CMakeLists.txt")
  set(AGIS_HAVE_LASZIP FALSE)
  message(STATUS "AGIS: LASzip not found at ${AGIS_LASZIP_ROOT} (LAZ preview via LASzip disabled)")
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

set(AGIS_HAVE_LASZIP TRUE)
message(STATUS "AGIS: bundled LASzip (static) from ${AGIS_LASZIP_ROOT}, target ${AGIS_LASZIP_TARGET}")
