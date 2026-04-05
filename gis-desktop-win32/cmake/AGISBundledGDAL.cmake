# Build GDAL from ../3rdparty/gdal-3.12.3 when no preinstalled GDAL is found.
# PROJ: prefer existing install (proj-install / AGIS_PROJ_PREFIX); else build ../3rdparty/proj-9.8.0.

set(_agis_gdal_src "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/gdal-3.12.3")
if(NOT EXISTS "${_agis_gdal_src}/CMakeLists.txt")
  return()
endif()

find_package(PROJ CONFIG QUIET)
if(NOT PROJ_FOUND)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AGISBundledPROJ.cmake")
endif()

find_package(PROJ CONFIG REQUIRED)

message(STATUS "AGIS: building bundled GDAL from ${_agis_gdal_src}")

set(BUILD_PYTHON_BINDINGS OFF CACHE BOOL "GDAL: skip Python bindings" FORCE)
set(BUILD_APPS OFF CACHE BOOL "GDAL: skip utilities" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "GDAL: skip tests" FORCE)

# PROJ and GDAL both use CACHE variable EMBED_RESOURCE_FILES. PROJ sets ON for proj.db embed;
# GDAL would reuse it and then require C23 #embed (fatal on MSVC). Turn OFF for GDAL configure only.
set(_agis_embed_saved "${EMBED_RESOURCE_FILES}")
set(EMBED_RESOURCE_FILES OFF CACHE BOOL "Whether resource files should be embedded (GDAL)" FORCE)

add_subdirectory("${_agis_gdal_src}" "${CMAKE_BINARY_DIR}/agis_bundled_gdal")

set(EMBED_RESOURCE_FILES "${_agis_embed_saved}" CACHE BOOL "Whether resource files should be embedded (PROJ)" FORCE)

if(NOT TARGET GDAL::GDAL)
  message(FATAL_ERROR "Bundled GDAL did not provide target GDAL::GDAL")
endif()

set(GDAL_FOUND TRUE)
set(AGIS_GDAL_BUNDLED TRUE)
