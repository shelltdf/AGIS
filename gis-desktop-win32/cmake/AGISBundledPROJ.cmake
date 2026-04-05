# Build PROJ from ../3rdparty/proj-9.8.0 — SQLite from ../3rdparty/sqlite-amalgamation-3510300;
# libcurl from ../3rdparty/curl-8.19.0 (see README-GDAL-BUILD.md).

set(_agis_proj_src "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/proj-9.8.0")
if(NOT EXISTS "${_agis_proj_src}/CMakeLists.txt")
  message(FATAL_ERROR "AGIS: missing ${_agis_proj_src}/CMakeLists.txt")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AGISBundledSQLite.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AGISBundledCurl.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AGISBundledLibTIFF.cmake")

message(STATUS "AGIS: building bundled PROJ 9.8.0 from ${_agis_proj_src}")

set(ENABLE_TIFF ON CACHE BOOL "PROJ: TIFF grids (bundled libtiff)" FORCE)
set(ENABLE_CURL ON CACHE BOOL "PROJ: bundled libcurl (Schannel)" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "PROJ: skip tests" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "PROJ: skip examples" FORCE)

add_subdirectory("${_agis_proj_src}" "${CMAKE_BINARY_DIR}/agis_bundled_proj")

if(NOT TARGET PROJ::proj)
  message(FATAL_ERROR "AGIS: bundled PROJ did not define target PROJ::proj")
endif()

if(TARGET generate_proj_db AND TARGET agis_sqlite3_cli)
  add_dependencies(generate_proj_db agis_sqlite3_cli)
endif()

list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bundled_proj")

set(AGIS_PROJ_BUNDLED TRUE)
