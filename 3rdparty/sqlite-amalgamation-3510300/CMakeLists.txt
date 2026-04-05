# Bundled by AGIS — SQLite official amalgamation (sqlite3.c + shell.c). No nmake required.
cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

if(NOT DEFINED AGIS_SQLITE_EXE_DIR)
  message(FATAL_ERROR "Internal: AGIS_SQLITE_EXE_DIR must be set by gis-desktop-win32/cmake/AGISBundledSQLite.cmake")
endif()

set(AGIS_SQLITE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT EXISTS "${AGIS_SQLITE_ROOT}/sqlite3.c" OR NOT EXISTS "${AGIS_SQLITE_ROOT}/sqlite3.h")
  message(FATAL_ERROR "缺少 ${AGIS_SQLITE_ROOT}/sqlite3.c 或 sqlite3.h。请使用官方 sqlite-amalgamation 压缩包内容。")
endif()

add_library(agis_sqlite3 STATIC "${AGIS_SQLITE_ROOT}/sqlite3.c")
target_include_directories(agis_sqlite3 PUBLIC "${AGIS_SQLITE_ROOT}")
target_compile_definitions(agis_sqlite3 PRIVATE SQLITE_ENABLE_COLUMN_METADATA=1)
if(MSVC)
  # 与全工程 /MD 对齐；子目录内仅 CMP0091+MSVC_RUNTIME 在部分 CMake/生成器下仍生成空 RuntimeLibrary
  target_compile_options(agis_sqlite3 PRIVATE
    /wd4244 /wd4267 /wd4018 /wd4312 /wd4047 /wd4146
    $<$<NOT:$<CONFIG:Debug>>:/MD>
    $<$<CONFIG:Debug>:/MDd>)
endif()

if(NOT EXISTS "${AGIS_SQLITE_ROOT}/shell.c")
  message(FATAL_ERROR "缺少 shell.c（应与 sqlite3.c 同目录的 amalgamation 发行包）。")
endif()

add_executable(agis_sqlite3_cli "${AGIS_SQLITE_ROOT}/shell.c")
target_link_libraries(agis_sqlite3_cli PRIVATE agis_sqlite3)
if(MSVC)
  target_compile_options(agis_sqlite3_cli PRIVATE
    /wd4244 /wd4267 /wd4028
    $<$<NOT:$<CONFIG:Debug>>:/MD>
    $<$<CONFIG:Debug>:/MDd>)
endif()

if(MSVC)
  set_property(TARGET agis_sqlite3 agis_sqlite3_cli PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

set_target_properties(agis_sqlite3_cli PROPERTIES OUTPUT_NAME sqlite3)
set_target_properties(agis_sqlite3_cli PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${AGIS_SQLITE_EXE_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${AGIS_SQLITE_EXE_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${AGIS_SQLITE_EXE_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${AGIS_SQLITE_EXE_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${AGIS_SQLITE_EXE_DIR}"
)
