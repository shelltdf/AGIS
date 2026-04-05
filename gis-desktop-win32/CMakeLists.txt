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

option(AGIS_USE_GDAL "Link GDAL for raster/vector layers (needs PROJ + GDAL install or bundled GDAL source)" ON)
option(AGIS_BUNDLE_GDAL "If no GDAL is found, build from ../3rdparty/gdal-3.12.3 (requires PROJ in CMAKE_PREFIX_PATH)" ON)

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
  src/ui_engine/app.cpp
  src/ui_engine/widget.cpp
  src/ui_engine/widgets.cpp
  src/ui_engine/widgets_mainframe.cpp
  src/ui_engine/gdiplus_ui.cpp
)

if(WIN32)
  target_sources(agis_desktop PRIVATE src/ui_engine/platform_windows.cpp)
elseif(APPLE)
  target_sources(agis_desktop PRIVATE src/ui_engine/platform_cocoa.mm)
  target_link_libraries(agis_desktop PRIVATE "-framework" "Cocoa")
elseif(UNIX)
  option(AGIS_UI_USE_XCB "Linux: use XCB instead of Xlib for ui_engine IGuiPlatform" OFF)
  if(AGIS_UI_USE_XCB)
    find_path(AGIS_XCB_INCLUDE_DIR NAMES xcb/xcb.h)
    find_library(AGIS_XCB_LIBRARY NAMES xcb)
    if(NOT AGIS_XCB_INCLUDE_DIR OR NOT AGIS_XCB_LIBRARY)
      message(FATAL_ERROR "AGIS_UI_USE_XCB=ON but libxcb not found (set AGIS_XCB_INCLUDE_DIR / AGIS_XCB_LIBRARY)")
    endif()
    target_sources(agis_desktop PRIVATE src/ui_engine/platform_xcb.cpp)
    target_include_directories(agis_desktop PRIVATE ${AGIS_XCB_INCLUDE_DIR})
    target_link_libraries(agis_desktop PRIVATE ${AGIS_XCB_LIBRARY})
  else()
    find_package(X11 REQUIRED)
    target_sources(agis_desktop PRIVATE src/ui_engine/platform_xlib.cpp)
    target_link_libraries(agis_desktop PRIVATE X11::X11)
  endif()
endif()

target_include_directories(agis_desktop PRIVATE ${CMAKE_SOURCE_DIR}/src)

set_target_properties(agis_desktop PROPERTIES
  OUTPUT_NAME "AGIS"
  WIN32_EXECUTABLE ON
)

target_compile_definitions(agis_desktop PRIVATE UNICODE _UNICODE NOMINMAX ${_agis_gdal_defs})

target_link_libraries(agis_desktop PRIVATE
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

if(MSVC)
  target_sources(agis_desktop PRIVATE app.manifest)
  target_compile_options(agis_desktop PRIVATE /utf-8)
endif()

# 独立演示：仅链接 ui_engine + GDI+，不依赖 map_engine / GDAL。
add_executable(ui_engine_demo WIN32
  src/app/ui_engine_demo.cpp
  src/app/ui_engine_demo_win32.cpp
  src/ui_engine/app.cpp
  src/ui_engine/widget.cpp
  src/ui_engine/widgets.cpp
  src/ui_engine/widgets_mainframe.cpp
  src/ui_engine/gdiplus_ui.cpp
)

if(WIN32)
  target_sources(ui_engine_demo PRIVATE src/ui_engine/platform_windows.cpp)
elseif(APPLE)
  target_sources(ui_engine_demo PRIVATE src/ui_engine/platform_cocoa.mm)
  target_link_libraries(ui_engine_demo PRIVATE "-framework" "Cocoa")
elseif(UNIX)
  if(AGIS_UI_USE_XCB)
    target_sources(ui_engine_demo PRIVATE src/ui_engine/platform_xcb.cpp)
    target_include_directories(ui_engine_demo PRIVATE ${AGIS_XCB_INCLUDE_DIR})
    target_link_libraries(ui_engine_demo PRIVATE ${AGIS_XCB_LIBRARY})
  else()
    target_sources(ui_engine_demo PRIVATE src/ui_engine/platform_xlib.cpp)
    target_link_libraries(ui_engine_demo PRIVATE X11::X11)
  endif()
endif()

target_include_directories(ui_engine_demo PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(ui_engine_demo PROPERTIES WIN32_EXECUTABLE ON)
target_compile_definitions(ui_engine_demo PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(ui_engine_demo PRIVATE
  comctl32
  user32
  gdi32
  gdiplus
  shell32
)
if(MSVC)
  target_compile_options(ui_engine_demo PRIVATE /utf-8)
endif()
