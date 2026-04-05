# Build libtiff for PROJ ENABLE_TIFF (grid TIFF). Source: official tarball or 3rdparty/libtiff-4.6.0.
# Subproject defaults to tiff-install OFF — ON so export() runs (optional; FindTIFF override uses target `tiff`).

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

set(_agis_tiff_local "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/libtiff-4.6.0")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "libtiff: static" FORCE)
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
  include(FetchContent)
  message(STATUS "AGIS: fetching libtiff 4.6.0 (extract to ${_agis_tiff_local}/ to avoid download)")
  FetchContent_Declare(
    agis_libtiff_upstream
    URL https://download.osgeo.org/libtiff/tiff-4.6.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(agis_libtiff_upstream)
endif()

unset(Tiff_DIR CACHE)
set(AGIS_LIBTIFF_AVAILABLE TRUE CACHE INTERNAL "")
