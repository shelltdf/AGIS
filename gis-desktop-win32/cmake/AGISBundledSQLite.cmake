# Build SQLite from ../3rdparty/sqlite-amalgamation-3510300 (sqlite3.c / sqlite3.h / shell.c).

set(_agis_sql_src "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/sqlite-amalgamation-3510300")
if(NOT EXISTS "${_agis_sql_src}/CMakeLists.txt")
  message(FATAL_ERROR "AGIS: missing ${_agis_sql_src}/CMakeLists.txt")
endif()

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(AGIS_SQLITE_EXE_DIR "${CMAKE_BINARY_DIR}/agis_sqlite_bin")
# FORCE: upgrading from sqlite-src path requires cache refresh on reconfigure
set(AGIS_SQLITE_INCLUDE_DIR "${_agis_sql_src}" CACHE INTERNAL "Bundled SQLite headers" FORCE)

add_subdirectory("${_agis_sql_src}" "${CMAKE_BINARY_DIR}/agis_bundled_sqlite")

set(EXE_SQLITE3 "${AGIS_SQLITE_EXE_DIR}/sqlite3.exe" CACHE FILEPATH "sqlite3 CLI for PROJ proj.db" FORCE)
