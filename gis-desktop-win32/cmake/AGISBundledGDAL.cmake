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

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AGISBundledExpat.cmake")

# OGR OSM / GPKG need SQLite3. PROJ already builds `agis_sqlite3` from ../3rdparty/sqlite-amalgamation-*.
# On the *first* CMake configure the .lib may not exist yet; build.py builds agis_sqlite3 then re-runs CMake.
set(_agis_sql_amalg "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/sqlite-amalgamation-3510300")
if(EXISTS "${_agis_sql_amalg}/sqlite3.h")
  set(SQLite3_INCLUDE_DIR "${_agis_sql_amalg}" CACHE PATH "SQLite3 headers (AGIS amalgamation)" FORCE)
endif()
set(_agis_sqlite_lib "")
foreach(_c IN ITEMS
    "${CMAKE_BINARY_DIR}/agis_bundled_sqlite/Release/agis_sqlite3.lib"
    "${CMAKE_BINARY_DIR}/agis_bundled_sqlite/RelWithDebInfo/agis_sqlite3.lib"
    "${CMAKE_BINARY_DIR}/agis_bundled_sqlite/MinSizeRel/agis_sqlite3.lib"
    "${CMAKE_BINARY_DIR}/agis_bundled_sqlite/Debug/agis_sqlite3.lib"
    "${CMAKE_BINARY_DIR}/agis_bundled_sqlite/agis_sqlite3.lib")
  if(EXISTS "${_c}")
    set(_agis_sqlite_lib "${_c}")
    break()
  endif()
endforeach()
if(_agis_sqlite_lib STREQUAL "")
  file(GLOB_RECURSE _agis_sql_glob "${CMAKE_BINARY_DIR}/agis_bundled_sqlite/**/agis_sqlite3.lib")
  if(_agis_sql_glob)
    list(SORT _agis_sql_glob)
    list(GET _agis_sql_glob -1 _agis_sqlite_lib)
  endif()
endif()
if(NOT _agis_sqlite_lib STREQUAL "" AND EXISTS "${_agis_sqlite_lib}")
  set(SQLite3_LIBRARY "${_agis_sqlite_lib}" CACHE FILEPATH "SQLite3 library (bundled agis_sqlite3)" FORCE)
  message(STATUS "AGIS: SQLite3_LIBRARY for GDAL (bundled agis_sqlite3) = ${_agis_sqlite_lib}")
else()
  message(STATUS "AGIS: bundled agis_sqlite3.lib not found yet; OGR OSM/GPKG may be off until you build target agis_sqlite3 and reconfigure (build.py does this).")
endif()

# Without SQLite3, OGR OSM / SQLite / GPKG drivers are OFF; first configure often has no .lib yet
# (build.py builds agis_sqlite3 then re-runs CMake). Stale CACHE can keep GDAL_USE_SQLITE3=OFF.
if(SQLite3_INCLUDE_DIR AND SQLite3_LIBRARY AND EXISTS "${SQLite3_LIBRARY}")
  set(GDAL_USE_SQLITE3 ON CACHE BOOL "SQLite3 for GDAL (AGIS bundled amalgamation)" FORCE)
  set(OGR_ENABLE_DRIVER_SQLITE ON CACHE BOOL "OGR SQLite driver — OSM dependency (AGIS)" FORCE)
  set(OGR_ENABLE_DRIVER_OSM ON CACHE BOOL "OGR OpenStreetMap driver (AGIS)" FORCE)
  # MSVC + static amalgamation: GDAL's try_compile for sqlite3_rtree_query_callback sometimes fails on RTREE
  # detection even when SQLITE_ENABLE_RTREE=1; allow configure (OSM/PBF still works; GPKG spatial index may be limited).
  if(SQLite3_LIBRARY MATCHES "agis_sqlite3")
    set(ACCEPT_MISSING_SQLITE3_RTREE ON CACHE BOOL "AGIS: bundled sqlite RTREE check" FORCE)
  endif()
  message(STATUS "AGIS: GDAL_USE_SQLITE3 + OGR SQLite/OSM drivers forced ON (SQLite3 library present)")
endif()

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
