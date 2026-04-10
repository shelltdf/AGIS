cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
# 与子工程（PROJ/GDAL/curl/sqlite/libtiff）统一为 /MD，避免 LNK4098（LIBCMT vs MSVCRT）
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "MSVC CRT" FORCE)
endif()

# C + CXX: bundled libcurl / PROJ / GDAL use C; MSVC also needs RC (rc.exe) initialized when C is
# used — project(C CXX) sets CMAKE_RC_COMPILER early. CXX-only projects can leave RC unset so
# CMake 4.1 try_compile in add_subdirectory(curl) fails in CMakeRCInformation.cmake.
project(agis_desktop LANGUAGES C CXX)

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
option(AGIS_USE_LASZIP
  "Bundle LASzip from ../3rdparty/LASzip for LAZ in model preview (AGIS_HAVE_LASZIP=1 when sources are present). Default ON; set OFF to skip."
  ON)
option(AGIS_USE_BASISU
  "Bundle basis_universal from ../3rdparty/basis_universal for KTX2 tile textures (AGIS_HAVE_BASISU=1 when sources are present). Default ON; set OFF to skip."
  ON)
# 模型预览与其它 bgfx 路径依赖 bx/bimg；关闭 unsupported（勿传 -DAGIS_USE_BGFX=OFF）。
option(AGIS_USE_BGFX "强制：须为 ON。模型预览使用 bgfx/bx/bimg，关闭不受支持。" ON)
if(NOT AGIS_USE_BGFX)
  message(FATAL_ERROR
    "AGIS_USE_BGFX 必须为 ON（默认已开启）。本工程不支持禁用 bgfx（勿使用 -DAGIS_USE_BGFX=OFF）。"
    "若 CMakeCache.txt 中曾为 OFF，请删缓存重配或显式传入 -DAGIS_USE_BGFX=ON。")
endif()

# ui_engine 位于仓库根目录，与 gis-desktop-win32 并列（`AGIS/ui_engine/`）
set(AGIS_UI_ENGINE_DIR "${CMAKE_SOURCE_DIR}/../ui_engine")
if(NOT EXISTS "${AGIS_UI_ENGINE_DIR}/src/app.cpp")
  message(
    FATAL_ERROR "ui_engine sources not found at ${AGIS_UI_ENGINE_DIR}/src (expected sibling of gis-desktop-win32).")
endif()

set(AGIS_BGFX_DIR "${CMAKE_SOURCE_DIR}/../3rdparty/bgfx-master")
set(AGIS_BX_DIR "${CMAKE_SOURCE_DIR}/../3rdparty/bx-master")
set(AGIS_BIMG_DIR "${CMAKE_SOURCE_DIR}/../3rdparty/bimg-master")

if(NOT EXISTS "${AGIS_BGFX_DIR}/src/amalgamated.cpp")
  message(FATAL_ERROR "bgfx source is missing (required): ${AGIS_BGFX_DIR}/src/amalgamated.cpp")
endif()
if(NOT EXISTS "${AGIS_BX_DIR}/include/bx/bx.h")
  message(FATAL_ERROR "bx headers are missing (required): ${AGIS_BX_DIR}/include/bx/bx.h")
endif()
if(NOT EXISTS "${AGIS_BIMG_DIR}/src/image.cpp")
  message(FATAL_ERROR "bimg source is missing (required): ${AGIS_BIMG_DIR}/src/image.cpp")
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

# --- LASzip（源码包解压到 ../3rdparty/LASzip，用于 LAZ 预览解压；非 git clone）---
set(AGIS_HAVE_LASZIP FALSE)
if(AGIS_USE_LASZIP)
  include(cmake/AGISBundledLASzip.cmake)
else()
  message(STATUS "AGIS: AGIS_USE_LASZIP=OFF — bundled LASzip skipped (LAZ relies on GDAL if available)")
endif()

# --- basis_universal（../3rdparty/basis_universal，KTX2 + Basis + mip；勿 git clone，见 README-BASIS-KTX2.md）---
set(AGIS_HAVE_BASISU FALSE)
set(AGIS_BASISU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/basis_universal" CACHE PATH
  "basis_universal source root (extracted zip; see ../3rdparty/README-BASIS-KTX2.md)")
if(AGIS_USE_BASISU)
  include(cmake/AGISBundledBasisUniversal.cmake)
else()
  message(STATUS "AGIS: AGIS_USE_BASISU=OFF — KTX2 texture output skipped")
endif()

# --- Draco（../3rdparty/draco，3D Tiles / glTF KHR_draco_mesh_compression；须先于 agis_desktop link）---
set(AGIS_HAVE_DRACO FALSE)
include(cmake/AGISBundledDraco.cmake)

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

file(GLOB _agis_bimg_astc CONFIGURE_DEPENDS
  ${AGIS_BIMG_DIR}/3rdparty/astc-encoder/source/*.cpp)
add_library(agis_bgfx_vendor STATIC
  ${AGIS_BX_DIR}/src/amalgamated.cpp
  ${AGIS_BIMG_DIR}/src/image.cpp
  ${AGIS_BIMG_DIR}/src/image_decode.cpp
  ${AGIS_BIMG_DIR}/src/image_encode.cpp
  ${AGIS_BIMG_DIR}/src/image_cubemap_filter.cpp
  ${AGIS_BIMG_DIR}/src/image_gnf.cpp
  ${_agis_bimg_astc}
  ${AGIS_BGFX_DIR}/src/amalgamated.cpp
)
target_include_directories(agis_bgfx_vendor PUBLIC
  ${AGIS_BX_DIR}/include
  $<$<AND:$<PLATFORM_ID:Windows>,$<CXX_COMPILER_ID:MSVC>>:${AGIS_BX_DIR}/include/compat/msvc>
  ${AGIS_BIMG_DIR}/include
  ${AGIS_BGFX_DIR}/include
  ${AGIS_BGFX_DIR}/3rdparty
  ${AGIS_BGFX_DIR}/3rdparty/khronos
  ${AGIS_BIMG_DIR}/3rdparty
  ${AGIS_BIMG_DIR}/3rdparty/astc-encoder/include
  ${AGIS_BIMG_DIR}/3rdparty/iqa/include
  ${AGIS_BIMG_DIR}/3rdparty/tinyexr/deps
  ${AGIS_BX_DIR}/3rdparty
)
target_compile_definitions(agis_bgfx_vendor PRIVATE
  BX_CONFIG_DEBUG=0
)
target_compile_features(agis_bgfx_vendor PRIVATE cxx_std_20)
if(WIN32)
  target_link_libraries(agis_bgfx_vendor PUBLIC
    user32
    gdi32
    shell32
    advapi32
    ws2_32
    psapi
    winmm
    imm32
    ole32
    version
    dxgi
    d3d11
    d3dcompiler
  )
endif()
if(MSVC)
  target_compile_options(agis_bgfx_vendor PRIVATE /utf-8 /Zc:__cplusplus /Zc:preprocessor)
endif()

add_executable(agis_desktop WIN32
  src/app/main.cpp
  src/app/main_globals.cpp
  src/app/gis_document/main_gis_xml.cpp
  src/app/gis_document/main_gis_document.cpp
  src/app/main_chrome.cpp
  src/app/main_panels.cpp
  src/app/main_convert.cpp
  src/app/ui_debug_pick.cpp
  src/app/preview/main_model_preview.cpp
  src/app/preview/tiles_gltf_loader.cpp
  src/app/ui_font.cpp
  src/app/ui_theme.cpp
  src/app/help_data_drivers.cpp
  src/map_engine/map_layer.cpp
  src/map_engine/map_layer_driver.cpp
  src/map_engine/map_layer_driver_gdal.cpp
  src/map_engine/map_document.cpp
  src/map_engine/map_engine.cpp
  src/map_engine/map_gpu.cpp
  src/map_engine/map_projection.cpp
  src/core/app_log.cpp
)

set(AGIS_THIRDPARTY_ROOT "${CMAKE_SOURCE_DIR}/../3rdparty")
target_include_directories(agis_desktop PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${AGIS_UI_ENGINE_DIR}/include
)

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

target_compile_definitions(agis_desktop PRIVATE AGIS_USE_BGFX=1 BX_CONFIG_DEBUG=0)
target_link_libraries(agis_desktop PRIVATE agis_bgfx_vendor)
target_sources(agis_desktop PRIVATE
  src/app/preview/model_preview_bgfx.cpp
  ${AGIS_BGFX_DIR}/examples/common/imgui/imgui.cpp
  ${AGIS_BGFX_DIR}/3rdparty/dear-imgui/imgui.cpp
  ${AGIS_BGFX_DIR}/3rdparty/dear-imgui/imgui_draw.cpp
  ${AGIS_BGFX_DIR}/3rdparty/dear-imgui/imgui_tables.cpp
  ${AGIS_BGFX_DIR}/3rdparty/dear-imgui/imgui_widgets.cpp
  ${AGIS_BGFX_DIR}/3rdparty/dear-imgui/imgui_demo.cpp
)
target_include_directories(agis_desktop PRIVATE
  ${AGIS_BGFX_DIR}/examples/17-drawstress
  ${AGIS_BGFX_DIR}/examples/common
  ${AGIS_BGFX_DIR}/examples/common/imgui
  ${AGIS_BGFX_DIR}/examples/common/debugdraw
  # 须置于 bgfx「examples/common」之后，否则会优先命中仓库根 3rdparty/imgui（无 imguiCreate 封装）
  ${AGIS_THIRDPARTY_ROOT}
  ${AGIS_THIRDPARTY_ROOT}/tinygltf
)
if(AGIS_HAVE_DRACO)
  target_compile_definitions(agis_desktop PRIVATE TINYGLTF_ENABLE_DRACO=1)
  target_link_libraries(agis_desktop PRIVATE draco::draco)
  # Draco 源码 `#include "draco/..."`：`src/` + 配置时生成的 `build/draco/draco_features.h`（Draco 用顶层 CMAKE_BINARY_DIR）
  target_include_directories(agis_desktop PRIVATE
    "${AGIS_THIRDPARTY_ROOT}/draco/src"
    "${CMAKE_BINARY_DIR}")
endif()
# bx/bgfx 头文件要求 C++20（与 agis_bgfx_vendor 一致）
target_compile_features(agis_desktop PRIVATE cxx_std_20)

if(AGIS_USE_GDAL AND GDAL_FOUND)
  target_link_libraries(agis_desktop PRIVATE GDAL::GDAL)
  target_sources(agis_desktop PRIVATE src/app/agis_gdal_runtime_env.cpp)
  if(TARGET PROJ::proj)
    target_link_libraries(agis_desktop PRIVATE PROJ::proj)
  endif()
endif()

if(AGIS_HAVE_LASZIP)
  target_compile_definitions(agis_desktop PRIVATE AGIS_HAVE_LASZIP=1)
  target_include_directories(agis_desktop PRIVATE
    "${AGIS_LASZIP_ROOT}/dll"
    "${CMAKE_BINARY_DIR}/agis_bundled_laszip/include")
  target_link_libraries(agis_desktop PRIVATE "${AGIS_LASZIP_TARGET}")
endif()
if(AGIS_HAVE_BASISU)
  target_compile_definitions(agis_desktop PRIVATE AGIS_HAVE_BASISU=1)
else()
  target_compile_definitions(agis_desktop PRIVATE AGIS_HAVE_BASISU=0)
endif()

add_executable(agis_convert_gui WIN32
  src/app/main_convert_standalone.cpp
  src/app/main_convert.cpp
  src/app/main_globals.cpp
  src/app/ui_debug_pick.cpp
  src/app/gis_document/main_gis_xml.cpp
  src/app/ui_font.cpp
  src/app/ui_theme.cpp
  src/map_engine/map_projection.cpp
  src/core/app_log.cpp
)
target_include_directories(agis_convert_gui PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${AGIS_UI_ENGINE_DIR}/include
)
set_target_properties(agis_convert_gui PROPERTIES
  OUTPUT_NAME "AGIS-Convert"
  WIN32_EXECUTABLE ON
)
target_compile_definitions(agis_convert_gui PRIVATE UNICODE _UNICODE NOMINMAX ${_agis_gdal_defs})
target_link_libraries(agis_convert_gui PRIVATE
  agis_ui_engine
  comctl32
  comdlg32
  user32
  gdi32
  gdiplus
  shell32
)
if(AGIS_USE_GDAL AND GDAL_FOUND)
  target_link_libraries(agis_convert_gui PRIVATE GDAL::GDAL)
  target_sources(agis_convert_gui PRIVATE src/app/agis_gdal_runtime_env.cpp)
  if(TARGET PROJ::proj)
    target_link_libraries(agis_convert_gui PRIVATE PROJ::proj)
  endif()
endif()
if(AGIS_HAVE_BASISU)
  target_compile_definitions(agis_convert_gui PRIVATE AGIS_HAVE_BASISU=1)
else()
  target_compile_definitions(agis_convert_gui PRIVATE AGIS_HAVE_BASISU=0)
endif()
if(MSVC)
  target_compile_options(agis_convert_gui PRIVATE /utf-8)
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
  # bx/bgfx 头文件要求（与 agis_bgfx_vendor 一致）
  target_compile_options(agis_desktop PRIVATE /Zc:__cplusplus /Zc:preprocessor)
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
  # 与主程序相同：嵌入 Common Controls 6，否则 `TaskDialog` 等符号在旧 comctl32 上会导致
  # 进程启动失败（退出码 0xC0000138 STATUS_ORDINAL_NOT_FOUND）。
  target_sources(ui_engine_demo PRIVATE app.manifest)
  target_compile_options(ui_engine_demo PRIVATE /utf-8)
endif()

# 7 个数据转换后端命令行工具（3 类型互转 6 过程 + 模型↔模型 1 过程）
set(_agis_convert_backend_sources
  src/tools/convert_backend_common.cpp
  src/app/agis_gdal_runtime_env.cpp
)
if(AGIS_HAVE_BASISU)
  list(APPEND _agis_convert_backend_sources src/tools/ktx2_basis_encode.cpp)
endif()
add_library(agis_convert_backend_common STATIC ${_agis_convert_backend_sources})
target_include_directories(agis_convert_backend_common PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(AGIS_USE_GDAL AND GDAL_FOUND)
  target_link_libraries(agis_convert_backend_common PRIVATE GDAL::GDAL)
  target_compile_definitions(agis_convert_backend_common PRIVATE GIS_DESKTOP_HAVE_GDAL=1)
else()
  target_compile_definitions(agis_convert_backend_common PRIVATE GIS_DESKTOP_HAVE_GDAL=0)
endif()
if(MSVC)
  target_compile_options(agis_convert_backend_common PRIVATE /utf-8)
endif()
if(AGIS_HAVE_LASZIP)
  target_compile_definitions(agis_convert_backend_common PRIVATE AGIS_HAVE_LASZIP=1)
  target_include_directories(agis_convert_backend_common PRIVATE
    "${AGIS_LASZIP_ROOT}/dll"
    "${CMAKE_BINARY_DIR}/agis_bundled_laszip/include")
  target_link_libraries(agis_convert_backend_common PRIVATE "${AGIS_LASZIP_TARGET}")
endif()
if(AGIS_HAVE_BASISU)
  target_compile_definitions(agis_convert_backend_common PRIVATE
    AGIS_HAVE_BASISU=1
    BASISD_SUPPORT_KTX2=1
  )
  if(BASISU_ZSTD)
    target_compile_definitions(agis_convert_backend_common PRIVATE BASISD_SUPPORT_KTX2_ZSTD=1)
  else()
    target_compile_definitions(agis_convert_backend_common PRIVATE BASISD_SUPPORT_KTX2_ZSTD=0)
  endif()
  target_include_directories(agis_convert_backend_common PRIVATE "${AGIS_BASISU_ROOT}")
  target_link_libraries(agis_convert_backend_common PRIVATE basisu_encoder)
else()
  target_compile_definitions(agis_convert_backend_common PRIVATE AGIS_HAVE_BASISU=0)
endif()

set(_agis_convert_tools
  agis_convert_gis_to_model
  agis_convert_gis_to_tile
  agis_convert_model_to_gis
  agis_convert_model_to_model
  agis_convert_model_to_tile
  agis_convert_tile_to_gis
  agis_convert_tile_to_model
)

foreach(_tool IN LISTS _agis_convert_tools)
  add_executable(${_tool} src/tools/${_tool}.cpp)
  target_include_directories(${_tool} PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(${_tool} PRIVATE agis_convert_backend_common)
  if(MSVC)
    target_compile_options(${_tool} PRIVATE /utf-8)
  endif()
endforeach()

# 捆绑 GDAL 运行时所需 gcore/data（如 tms_*.json）；复制到 build/gdal_data，并由 AgisEnsureGdalDataPath 探测。
set(_AGIS_GDAL_GCORE_DATA "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/gdal-3.12.3/gcore/data")
if(AGIS_USE_GDAL AND GDAL_FOUND AND EXISTS "${_AGIS_GDAL_GCORE_DATA}/tms_NZTM2000.json")
  set(AGIS_GDAL_DATA_STAGE "${CMAKE_BINARY_DIR}/gdal_data")
  add_custom_target(agis_gdal_runtime_data ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${AGIS_GDAL_DATA_STAGE}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_AGIS_GDAL_GCORE_DATA}" "${AGIS_GDAL_DATA_STAGE}"
    COMMENT "Stage GDAL_DATA (gcore/data) for AGIS")
  add_dependencies(agis_desktop agis_gdal_runtime_data)
  # 仅挂在 agis_desktop 上复制一份：各转换工具与主程序默认在同一 $(OutDir)/Configuration 目录下，
  # 若对每个 exe 各自 POST_BUILD copy_directory，并行编译时会多进程同时写同一 gdal_data，易触发 MSB3073。
  add_custom_command(
    TARGET agis_desktop
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${AGIS_GDAL_DATA_STAGE}"
            "$<TARGET_FILE_DIR:agis_desktop>/gdal_data"
    COMMENT "Copy GDAL_DATA next to AGIS (shared by convert tools in same output dir)")
  foreach(_t IN LISTS _agis_convert_tools)
    add_dependencies(${_t} agis_gdal_runtime_data)
  endforeach()
endif()

add_dependencies(agis_desktop ${_agis_convert_tools})
if(WIN32)
  foreach(_tool IN LISTS _agis_convert_tools)
    add_custom_command(TARGET agis_desktop POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:${_tool}>
        $<TARGET_FILE_DIR:agis_desktop>)
  endforeach()
endif()
