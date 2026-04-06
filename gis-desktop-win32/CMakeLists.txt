cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
# 与子工程（PROJ/GDAL/curl/sqlite/libtiff）统一为 /MD，避免 LNK4098（LIBCMT vs MSVCRT）
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "MSVC CRT" FORCE)
endif()

project(agis_desktop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
  enable_language(OBJCXX)
endif()

option(AGIS_USE_GDAL
  "Link GDAL for raster/vector layers. Default ON: repo includes ../3rdparty/gdal-* sources; use bundled build or gdal-install. Set OFF only for a minimal shell without GIS."
  ON)
option(AGIS_BUNDLE_GDAL
  "If GDAL is not found via find_package, build from ../3rdparty/gdal-3.12.3 (PROJ must be available in CMAKE_PREFIX_PATH)"
  ON)

option(AGIS_UI_ENGINE_SHARED "Build agis_ui_engine as shared library (DLL/SO); OFF builds static" OFF)
option(AGIS_UI_USE_XCB "Linux: use XCB instead of Xlib for ui_engine IGuiPlatform" OFF)

# ui_engine 位于仓库根目录，与 gis-desktop-win32 并列（`AGIS/ui_engine/`）
set(AGIS_UI_ENGINE_DIR "${CMAKE_SOURCE_DIR}/../ui_engine")
if(NOT EXISTS "${AGIS_UI_ENGINE_DIR}/src/app.cpp")
  message(FATAL_ERROR "ui_engine sources not found at ${AGIS_UI_ENGINE_DIR}/src (expected sibling of gis-desktop-win32).")
endif()

# 从源码安装后的 GDAL 前缀（与捆绑源码二选一）
set(AGIS_GDAL_PREFIX "" CACHE PATH
  "GDAL install prefix from a source install (contains lib/cmake/gdal/). Ignored if bundled build is used.")

set(_agis_gdal_defs GIS_DESKTOP_HAVE_GDAL=0)
set(AGIS_GDAL_BUNDLED FALSE)

if(AGIS_USE_GDAL)
  if(AGIS_GDAL_PREFIX)
    list(PREPEND CMAKE_PREFIX_PATH "${AGIS_GDAL_PREFIX}")
  endif()
  if(DEFINED ENV{AGIS_GDAL_PREFIX} AND NOT "$ENV{AGIS_GDAL_PREFIX}" STREQUAL "")
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{AGIS_GDAL_PREFIX}")
  endif()
  if(EXISTS "${CMAKE_SOURCE_DIR}/../3rdparty/gdal-install")
    list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/../3rdparty/gdal-install")
  endif()
  if(EXISTS "${CMAKE_SOURCE_DIR}/../3rdparty/proj-install")
    list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/../3rdparty/proj-install")
  endif()
  if(DEFINED ENV{AGIS_PROJ_PREFIX} AND NOT "$ENV{AGIS_PROJ_PREFIX}" STREQUAL "")
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{AGIS_PROJ_PREFIX}")
  endif()

  find_package(GDAL CONFIG QUIET)
  if(NOT GDAL_FOUND)
    find_path(GDAL_INCLUDE_DIR NAMES gdal.h PATH_SUFFIXES gdal)
    find_library(GDAL_LIBRARY NAMES gdal gdal_i)
    if(GDAL_INCLUDE_DIR AND GDAL_LIBRARY)
      add_library(GDAL::GDAL UNKNOWN IMPORTED)
      set_target_properties(GDAL::GDAL PROPERTIES
        IMPORTED_LOCATION "${GDAL_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${GDAL_INCLUDE_DIR}"
      )
      set(GDAL_FOUND TRUE)
    endif()
  endif()

  if(NOT GDAL_FOUND AND AGIS_BUNDLE_GDAL)
    include(cmake/AGISBundledGDAL.cmake)
  endif()

  if(GDAL_FOUND)
    set(_agis_gdal_defs GIS_DESKTOP_HAVE_GDAL=1)
    if(DEFINED GDAL_VERSION)
      message(STATUS "AGIS: GDAL enabled (${GDAL_VERSION})")
    else()
      message(STATUS "AGIS: GDAL enabled")
    endif()
    if(AGIS_GDAL_BUNDLED)
      message(STATUS "AGIS: using bundled GDAL target from 3rdparty/gdal-3.12.3")
    endif()
  else()
    message(FATAL_ERROR
      "AGIS_USE_GDAL is ON but GDAL was not found.\n"
      "  Either install GDAL from source to ../3rdparty/gdal-install and/or set AGIS_GDAL_PREFIX,\n"
      "  or enable bundled deps: ../3rdparty/proj-9.8.0 + ../3rdparty/gdal-3.12.3 (needs SQLite3 + sqlite3.exe on PATH),\n"
      "  or disable: -DAGIS_USE_GDAL=OFF.\n"
      "See 3rdparty/README-GDAL-BUILD.md.")
  endif()
endif()

# --- ui_engine: 独立库（静态 / 动态由 AGIS_UI_ENGINE_SHARED 切换）---
set(_agis_ui_engine_sources
  ${AGIS_UI_ENGINE_DIR}/src/app.cpp
  ${AGIS_UI_ENGINE_DIR}/src/platform_factory.cpp
  ${AGIS_UI_ENGINE_DIR}/src/widget.cpp
  ${AGIS_UI_ENGINE_DIR}/src/widgets.cpp
  ${AGIS_UI_ENGINE_DIR}/src/widgets_mainframe.cpp
)
if(WIN32)
  list(APPEND _agis_ui_engine_sources ${AGIS_UI_ENGINE_DIR}/src/gdiplus_ui.cpp)
endif()
if(WIN32)
  list(APPEND _agis_ui_engine_sources ${AGIS_UI_ENGINE_DIR}/src/platform/platform_windows.cpp)
elseif(APPLE)
  list(APPEND _agis_ui_engine_sources ${AGIS_UI_ENGINE_DIR}/src/platform/platform_cocoa.mm)
elseif(UNIX)
  if(AGIS_UI_USE_XCB)
    find_path(AGIS_XCB_INCLUDE_DIR NAMES xcb/xcb.h)
    find_library(AGIS_XCB_LIBRARY NAMES xcb)
    if(NOT AGIS_XCB_INCLUDE_DIR OR NOT AGIS_XCB_LIBRARY)
      message(FATAL_ERROR "AGIS_UI_USE_XCB=ON but libxcb not found (set AGIS_XCB_INCLUDE_DIR / AGIS_XCB_LIBRARY)")
    endif()
    list(APPEND _agis_ui_engine_sources ${AGIS_UI_ENGINE_DIR}/src/platform/platform_xcb.cpp)
  else()
    find_package(X11 REQUIRED)
    list(APPEND _agis_ui_engine_sources ${AGIS_UI_ENGINE_DIR}/src/platform/platform_xlib.cpp)
  endif()
endif()

if(AGIS_UI_ENGINE_SHARED)
  add_library(agis_ui_engine SHARED ${_agis_ui_engine_sources})
else()
  add_library(agis_ui_engine STATIC ${_agis_ui_engine_sources})
endif()

set_target_properties(agis_ui_engine PROPERTIES OUTPUT_NAME "agis_ui_engine")

# `ui_engine/...`：`include/ui_engine/`；演示 `ui_engine_demo*` 在 `app/`；平台实现在 `src/platform/`
target_include_directories(agis_ui_engine PUBLIC
  ${AGIS_UI_ENGINE_DIR}/include
  ${AGIS_UI_ENGINE_DIR}/app
  ${AGIS_UI_ENGINE_DIR}/src/platform
)
# `widgets_mainframe.cpp` 依赖 `app/ui_private.h`（实现仍在 ui_engine 库内）
target_include_directories(agis_ui_engine PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(AGIS_UI_ENGINE_SHARED)
  target_compile_definitions(agis_ui_engine PRIVATE AGIS_UI_EXPORTS)
else()
  target_compile_definitions(agis_ui_engine PUBLIC AGIS_UI_STATIC=1)
endif()

if(WIN32)
  target_compile_definitions(agis_ui_engine PRIVATE UNICODE _UNICODE NOMINMAX)
endif()

if(WIN32)
  # PUBLIC：静态库时使用者需同链 Win32/GDI+；动态库时重复链接通常可接受。
  target_link_libraries(agis_ui_engine PUBLIC comctl32 user32 gdi32 gdiplus shell32)
elseif(APPLE)
  target_link_libraries(agis_ui_engine PUBLIC "-framework" Cocoa)
elseif(UNIX)
  if(AGIS_UI_USE_XCB)
    target_include_directories(agis_ui_engine PUBLIC ${AGIS_XCB_INCLUDE_DIR})
    target_link_libraries(agis_ui_engine PUBLIC ${AGIS_XCB_LIBRARY})
    target_compile_definitions(agis_ui_engine PRIVATE AGIS_UI_USE_XCB=1)
  else()
    target_link_libraries(agis_ui_engine PUBLIC X11::X11)
  endif()
endif()

if(MSVC)
  target_compile_options(agis_ui_engine PRIVATE /utf-8)
endif()

if(AGIS_UI_ENGINE_SHARED)
  message(STATUS "AGIS: agis_ui_engine = SHARED")
else()
  message(STATUS "AGIS: agis_ui_engine = STATIC")
endif()

add_executable(agis_desktop WIN32
  src/app/main.cpp
  src/map_engine/map_layer.cpp
  src/map_engine/map_layer_driver.cpp
  src/map_engine/map_layer_driver_gdal.cpp
  src/map_engine/map_document.cpp
  src/map_engine/map_engine.cpp
  src/map_engine/map_gpu.cpp
  src/map_engine/map_projection.cpp
  src/core/app_log.cpp
)

target_include_directories(agis_desktop PRIVATE ${CMAKE_SOURCE_DIR}/src ${AGIS_UI_ENGINE_DIR}/include)

set_target_properties(agis_desktop PROPERTIES
  OUTPUT_NAME "AGIS"
  WIN32_EXECUTABLE ON
)

target_compile_definitions(agis_desktop PRIVATE UNICODE _UNICODE NOMINMAX ${_agis_gdal_defs})

target_link_libraries(agis_desktop PRIVATE
  agis_ui_engine
  comctl32
  comdlg32
  user32
  gdi32
  gdiplus
  shell32
  d3d11
  dxgi
  d3dcompiler
  opengl32
)

if(AGIS_USE_GDAL AND GDAL_FOUND)
  target_link_libraries(agis_desktop PRIVATE GDAL::GDAL)
  if(TARGET PROJ::proj)
    target_link_libraries(agis_desktop PRIVATE PROJ::proj)
  endif()
endif()

if(WIN32 AND AGIS_USE_GDAL AND GDAL_FOUND AND AGIS_GDAL_BUNDLED)
  add_custom_command(TARGET agis_desktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:GDAL>
      $<TARGET_FILE_DIR:agis_desktop>)
  if(TARGET PROJ::proj)
    add_custom_command(TARGET agis_desktop POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:PROJ::proj>
        $<TARGET_FILE_DIR:agis_desktop>)
  endif()
endif()

if(WIN32 AND AGIS_UI_ENGINE_SHARED)
  add_custom_command(TARGET agis_desktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:agis_ui_engine>
      $<TARGET_FILE_DIR:agis_desktop>)
endif()

if(MSVC)
  target_sources(agis_desktop PRIVATE app.manifest)
  target_compile_options(agis_desktop PRIVATE /utf-8)
endif()

# 独立演示：链接 agis_ui_engine（GDI+ / 平台实现已在库内）。
if(WIN32)
  add_executable(ui_engine_demo WIN32
    ${AGIS_UI_ENGINE_DIR}/app/ui_engine_demo.cpp
    ${AGIS_UI_ENGINE_DIR}/app/ui_engine_demo_main.cpp
  )
else()
  add_executable(ui_engine_demo
    ${AGIS_UI_ENGINE_DIR}/app/ui_engine_demo.cpp
    ${AGIS_UI_ENGINE_DIR}/app/ui_engine_demo_main.cpp
  )
endif()

# `widgets_all.h` → `app/ui_private.h`（主程序侧 Widget 声明，实现仍在 agis_ui_engine）
target_include_directories(ui_engine_demo PRIVATE ${AGIS_UI_ENGINE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
if(WIN32)
  set_target_properties(ui_engine_demo PROPERTIES WIN32_EXECUTABLE ON)
endif()

if(WIN32)
  target_compile_definitions(ui_engine_demo PRIVATE UNICODE _UNICODE NOMINMAX AGIS_BUILD_UI_ENGINE_DEMO=1)
endif()

if(WIN32)
  target_link_libraries(ui_engine_demo PRIVATE
    agis_ui_engine
    comctl32
    user32
    gdi32
    gdiplus
    shell32
  )
else()
  target_link_libraries(ui_engine_demo PRIVATE agis_ui_engine)
endif()

if(WIN32 AND AGIS_UI_ENGINE_SHARED)
  add_custom_command(TARGET ui_engine_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:agis_ui_engine>
      $<TARGET_FILE_DIR:ui_engine_demo>)
endif()

if(MSVC)
  target_compile_options(ui_engine_demo PRIVATE /utf-8)
endif()
