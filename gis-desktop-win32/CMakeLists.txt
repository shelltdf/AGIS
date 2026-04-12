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

# IDE（clangd / Cursor C++）：生成 compile_commands.json；不需要时在 CMake GUI 关掉 CMAKE_EXPORT_COMPILE_COMMANDS。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json for IDE/LSP")

if(MSVC)
  # 单次 cl 调用内并行编译多个翻译单元，缩短大目标的纯编译阶段（与 cmake --build -j 互补；机器吃紧可去掉 /MP）
  add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/MP>)
  add_compile_options($<$<COMPILE_LANG_AND_ID:C,MSVC>:/MP>)
endif()

if(APPLE)
  enable_language(OBJCXX)
endif()

option(AGIS_USE_GDAL
  "Link GDAL for raster/vector layers. Default ON: repo includes ../3rdparty/gdal-* sources; use bundled build or gdal-install. Set OFF only for a minimal shell without GIS."
  ON)
option(AGIS_BUNDLE_GDAL
  "If GDAL is not found via find_package, build from ../3rdparty/gdal-3.12.3 (PROJ must be available in CMAKE_PREFIX_PATH)"
  ON)

# 开发期默认动态链接捆绑的 PROJ/GDAL/curl/tiff，显著缩短链接时间；正式发版单目录便携包请关闭（见 CMakePresets.json release-static）。
option(AGIS_BUNDLED_LIBS_SHARED
  "When building bundled PROJ/GDAL: build libcurl/libtiff/PROJ/GDAL as shared libs (DLL) and copy next to exes. OFF = 全静态 .lib（链接慢、适合无 DLL 的发布目录）。"
  ON)

option(AGIS_UI_ENGINE_SHARED "Build agis_ui_engine as shared library (DLL/SO); OFF builds static (links into every exe)" ON)
option(AGIS_COMMON_APP_SHARED
  "Build agis_common (仓库根 common/) as shared library（agis_common.dll）。ON 时 Windows 上须 AGIS_UI_ENGINE_SHARED=ON。OFF 为静态 .lib。" ON)
option(AGIS_MAP_ENGINE_SHARED
  "Build agis_map_engine as DLL/SO (ON) or static .lib (OFF). ON (WIN32) 须 AGIS_UI_ENGINE_SHARED=ON。" ON)
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
set(AGIS_UI_ENGINE_PUBLIC_INCLUDES
  ${AGIS_UI_ENGINE_DIR}/core/include
  ${AGIS_UI_ENGINE_DIR}/widgets/include
  ${AGIS_UI_ENGINE_DIR}/gdiplus/include
)
if(NOT EXISTS "${AGIS_UI_ENGINE_DIR}/core/src/app.cpp")
  message(
    FATAL_ERROR "ui_engine sources not found at ${AGIS_UI_ENGINE_DIR}/core/src (expected sibling of gis-desktop-win32).")
endif()

set(AGIS_MAP_ENGINE_DIR "${CMAKE_SOURCE_DIR}/../map_engine")
if(NOT EXISTS "${AGIS_MAP_ENGINE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "map_engine not found at ${AGIS_MAP_ENGINE_DIR} (expected sibling of gis-desktop-win32).")
endif()

set(AGIS_COMMON_DIR "${CMAKE_SOURCE_DIR}/../common")
if(NOT EXISTS "${AGIS_COMMON_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "AGIS common module not found at ${AGIS_COMMON_DIR} (expected sibling of gis-desktop-win32).")
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

# 捆绑 GDAL/PROJ 等为 DLL 时，将运行时依赖复制到 exe 同目录（仅 TYPE=SHARED_LIBRARY 的目标）。
function(agis_copy_bundled_gdal_deps_to_exe _exe)
  if(NOT WIN32)
    return()
  endif()
  if(NOT AGIS_USE_GDAL OR NOT GDAL_FOUND OR NOT AGIS_GDAL_BUNDLED OR NOT AGIS_BUNDLED_LIBS_SHARED)
    return()
  endif()
  if(NOT TARGET "${_exe}")
    return()
  endif()
  # curl 8.x 共享库目标名为 libcurl_shared（见 3rdparty/curl lib/CMakeLists.txt）
  foreach(_dep IN ITEMS GDAL::GDAL PROJ::proj libcurl_shared tiff)
    if(TARGET "${_dep}")
      get_target_property(_t "${_dep}" TYPE)
      if(_t STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET "${_exe}" POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${_dep}>
            $<TARGET_FILE_DIR:${_exe}>)
      endif()
    endif()
  endforeach()
endfunction()

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
# 按模块：<模块>/include/… + <模块>/src/*.cpp（core/widgets/gdiplus 为 include/ui_engine/；platform 为 include/platform/，PRIVATE）
set(_agis_ui_engine_sources
  ${AGIS_UI_ENGINE_DIR}/core/src/app.cpp
  ${AGIS_UI_ENGINE_DIR}/core/src/widget.cpp
  ${AGIS_UI_ENGINE_DIR}/platform/src/platform_factory.cpp
  ${AGIS_UI_ENGINE_DIR}/widgets/src/widgets.cpp
  ${AGIS_UI_ENGINE_DIR}/widgets/src/widgets_mainframe.cpp
)
if(WIN32)
  list(APPEND _agis_ui_engine_sources ${AGIS_UI_ENGINE_DIR}/gdiplus/src/gdiplus_ui.cpp)
endif()
if(WIN32)
  list(APPEND _agis_ui_engine_sources ${AGIS_UI_ENGINE_DIR}/platform/src/platform_windows.cpp)
elseif(APPLE)
  list(APPEND _agis_ui_engine_sources ${AGIS_UI_ENGINE_DIR}/platform/src/platform_cocoa.mm)
elseif(UNIX)
  if(AGIS_UI_USE_XCB)
    find_path(AGIS_XCB_INCLUDE_DIR NAMES xcb/xcb.h)
    find_library(AGIS_XCB_LIBRARY NAMES xcb)
    if(NOT AGIS_XCB_INCLUDE_DIR OR NOT AGIS_XCB_LIBRARY)
      message(FATAL_ERROR "AGIS_UI_USE_XCB=ON but libxcb not found (set AGIS_XCB_INCLUDE_DIR / AGIS_XCB_LIBRARY)")
    endif()
    list(APPEND _agis_ui_engine_sources ${AGIS_UI_ENGINE_DIR}/platform/src/platform_xcb.cpp)
  else()
    find_package(X11 REQUIRED)
    list(APPEND _agis_ui_engine_sources ${AGIS_UI_ENGINE_DIR}/platform/src/platform_xlib.cpp)
  endif()
endif()

if(AGIS_UI_ENGINE_SHARED)
  add_library(agis_ui_engine SHARED ${_agis_ui_engine_sources})
else()
  add_library(agis_ui_engine STATIC ${_agis_ui_engine_sources})
endif()

set_target_properties(agis_ui_engine PROPERTIES OUTPUT_NAME "agis_ui_engine")

# 公开 API：`core` / `widgets` / `gdiplus` 的 `include/ui_engine/`；平台私有头在 `platform/include/platform/`（PRIVATE）
target_include_directories(agis_ui_engine PUBLIC ${AGIS_UI_ENGINE_PUBLIC_INCLUDES})
target_include_directories(agis_ui_engine PRIVATE ${AGIS_UI_ENGINE_DIR}/platform/include)

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

add_subdirectory("${AGIS_COMMON_DIR}" agis_common_build)

# 与 agis_ui_engine.dll、主程序 exe 同目录（build/<Config>/），避免 DLL 仅出现在 agis_common_build 子目录。
if(TARGET agis_common)
  set_target_properties(agis_common PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
endif()

add_subdirectory("${AGIS_MAP_ENGINE_DIR}" agis_map_engine_build)

set(AGIS_THIRDPARTY_ROOT "${CMAKE_SOURCE_DIR}/../3rdparty")

set(_agis_workbench_imgui_sources
  ${AGIS_BGFX_DIR}/examples/common/imgui/imgui.cpp
  ${AGIS_BGFX_DIR}/3rdparty/dear-imgui/imgui.cpp
  ${AGIS_BGFX_DIR}/3rdparty/dear-imgui/imgui_draw.cpp
  ${AGIS_BGFX_DIR}/3rdparty/dear-imgui/imgui_tables.cpp
  ${AGIS_BGFX_DIR}/3rdparty/dear-imgui/imgui_widgets.cpp
  ${AGIS_BGFX_DIR}/3rdparty/dear-imgui/imgui_demo.cpp
)
add_library(agis_workbench_app STATIC
  src/app/common/gis_document/main_gis_xml.cpp
  src/app/common/gis_document/main_gis_document.cpp
  src/app/workbench/main_chrome.cpp
  src/app/workbench/main_debug.cpp
  src/app/workbench/main_panels.cpp
  src/app/workbench/workbench_tool_launch.cpp
  src/app/workbench/help_data_drivers.cpp
)
if(NOT AGIS_MAP_ENGINE_SHARED)
  target_sources(agis_workbench_app PRIVATE ${_agis_workbench_imgui_sources})
endif()
target_include_directories(agis_workbench_app PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${AGIS_UI_ENGINE_PUBLIC_INCLUDES}
  ${AGIS_BX_DIR}/include
  $<$<AND:$<PLATFORM_ID:Windows>,$<CXX_COMPILER_ID:MSVC>>:${AGIS_BX_DIR}/include/compat/msvc>
  ${AGIS_BX_DIR}/3rdparty
  ${AGIS_BGFX_DIR}/include
  ${AGIS_BGFX_DIR}/3rdparty
  ${AGIS_BGFX_DIR}/3rdparty/khronos
  ${AGIS_BGFX_DIR}/examples/17-drawstress
  ${AGIS_BGFX_DIR}/examples/common
  ${AGIS_BGFX_DIR}/examples/common/imgui
  ${AGIS_BGFX_DIR}/examples/common/debugdraw
  ${AGIS_THIRDPARTY_ROOT}
)
target_link_libraries(agis_workbench_app PRIVATE agis_common agis_ui_engine agis_bgfx_vendor)
target_link_libraries(agis_workbench_app PUBLIC agis_map_engine)
target_compile_definitions(agis_workbench_app PRIVATE UNICODE _UNICODE NOMINMAX ${_agis_gdal_defs})
target_compile_features(agis_workbench_app PRIVATE cxx_std_20)

# 模型 / 瓦片预览：仅编入独立 exe，不进入 AGIS 主程序。
add_library(agis_model_preview_lib STATIC
  src/app/preview/model_preview/main_model_preview.cpp
  src/app/preview/model_preview/model_preview_bgfx.cpp
  src/app/preview/model_preview/tiles_gltf_loader.cpp
)
# 须始终编入 bgfx 的 imgui 封装（含 imguiCreate），与 model_preview_bgfx 内 bgfx::init 同属 exe 内静态链。
# 若改从 agis_map_engine.dll 导入 ImGui，则会调用 DLL 内另一份未 init 的 bgfx，加载完成弹 ImGui 时崩溃。
target_sources(agis_model_preview_lib PRIVATE ${_agis_workbench_imgui_sources})
target_include_directories(agis_model_preview_lib PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${AGIS_UI_ENGINE_PUBLIC_INCLUDES}
  ${AGIS_BX_DIR}/include
  $<$<AND:$<PLATFORM_ID:Windows>,$<CXX_COMPILER_ID:MSVC>>:${AGIS_BX_DIR}/include/compat/msvc>
  ${AGIS_BX_DIR}/3rdparty
  ${AGIS_BGFX_DIR}/include
  ${AGIS_BGFX_DIR}/3rdparty
  ${AGIS_BGFX_DIR}/3rdparty/khronos
  ${AGIS_BGFX_DIR}/examples/17-drawstress
  ${AGIS_BGFX_DIR}/examples/common
  ${AGIS_BGFX_DIR}/examples/common/imgui
  ${AGIS_BGFX_DIR}/examples/common/debugdraw
  ${AGIS_THIRDPARTY_ROOT}
  ${AGIS_THIRDPARTY_ROOT}/tinygltf
)
target_link_libraries(agis_model_preview_lib PRIVATE agis_common agis_ui_engine agis_bgfx_vendor)
target_compile_definitions(agis_model_preview_lib PRIVATE
  UNICODE _UNICODE NOMINMAX ${_agis_gdal_defs}
  AGIS_USE_BGFX=1 BX_CONFIG_DEBUG=0
)
target_compile_features(agis_model_preview_lib PRIVATE cxx_std_20)
if(AGIS_USE_GDAL AND GDAL_FOUND)
  target_link_libraries(agis_model_preview_lib PRIVATE GDAL::GDAL)
  if(TARGET PROJ::proj)
    target_link_libraries(agis_model_preview_lib PRIVATE PROJ::proj)
  endif()
endif()
if(AGIS_HAVE_DRACO)
  target_compile_definitions(agis_model_preview_lib PRIVATE TINYGLTF_ENABLE_DRACO=1)
  target_link_libraries(agis_model_preview_lib PRIVATE draco::draco)
  target_include_directories(agis_model_preview_lib PRIVATE
    "${AGIS_THIRDPARTY_ROOT}/draco/src"
    "${CMAKE_BINARY_DIR}")
endif()
if(AGIS_HAVE_LASZIP)
  target_compile_definitions(agis_model_preview_lib PRIVATE AGIS_HAVE_LASZIP=1)
  target_include_directories(agis_model_preview_lib PRIVATE
    "${AGIS_LASZIP_ROOT}/dll"
    "${CMAKE_BINARY_DIR}/agis_bundled_laszip/include")
  target_link_libraries(agis_model_preview_lib PRIVATE "${AGIS_LASZIP_TARGET}")
endif()
if(AGIS_HAVE_BASISU)
  target_compile_definitions(agis_model_preview_lib PRIVATE AGIS_HAVE_BASISU=1)
else()
  target_compile_definitions(agis_model_preview_lib PRIVATE AGIS_HAVE_BASISU=0)
endif()
if(MSVC)
  target_compile_options(agis_model_preview_lib PRIVATE /utf-8 /Zc:__cplusplus /Zc:preprocessor)
endif()

add_library(agis_tile_preview_lib STATIC
  src/app/preview/tile_preview/tile_raster_preview.cpp
  src/app/preview/tile_preview/tile_preview_protocol_picker.cpp
)
target_include_directories(agis_tile_preview_lib PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${AGIS_UI_ENGINE_PUBLIC_INCLUDES}
)
target_link_libraries(agis_tile_preview_lib PRIVATE agis_common agis_ui_engine)
target_compile_definitions(agis_tile_preview_lib PRIVATE UNICODE _UNICODE NOMINMAX ${_agis_gdal_defs})
target_compile_features(agis_tile_preview_lib PRIVATE cxx_std_20)
if(AGIS_USE_GDAL AND GDAL_FOUND)
  target_link_libraries(agis_tile_preview_lib PRIVATE GDAL::GDAL)
  if(TARGET PROJ::proj)
    target_link_libraries(agis_tile_preview_lib PRIVATE PROJ::proj)
  endif()
endif()
if(AGIS_HAVE_BASISU)
  target_compile_definitions(agis_tile_preview_lib PRIVATE AGIS_HAVE_BASISU=1)
else()
  target_compile_definitions(agis_tile_preview_lib PRIVATE AGIS_HAVE_BASISU=0)
endif()
if(MSVC)
  target_compile_options(agis_tile_preview_lib PRIVATE /utf-8 /Zc:__cplusplus /Zc:preprocessor)
endif()

add_library(agis_converter_app STATIC
  src/app/common/gis_document/main_gis_xml.cpp
  src/app/converter/main_convert.cpp
)
target_include_directories(agis_converter_app PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${AGIS_UI_ENGINE_PUBLIC_INCLUDES}
)
target_link_libraries(agis_converter_app PRIVATE agis_common agis_map_engine agis_bgfx_vendor)
target_compile_definitions(agis_converter_app PRIVATE UNICODE _UNICODE NOMINMAX ${_agis_gdal_defs})
target_compile_features(agis_converter_app PRIVATE cxx_std_20)

add_executable(agis_desktop WIN32
  src/app/workbench/main.cpp
)

target_include_directories(agis_desktop PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${AGIS_UI_ENGINE_PUBLIC_INCLUDES}
)
target_link_libraries(agis_desktop PRIVATE agis_workbench_app agis_common)

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
  d2d1
  opengl32
)

target_compile_definitions(agis_desktop PRIVATE AGIS_USE_BGFX=1 BX_CONFIG_DEBUG=0)
target_compile_definitions(agis_workbench_app PRIVATE AGIS_USE_BGFX=1 BX_CONFIG_DEBUG=0)
target_link_libraries(agis_desktop PRIVATE agis_bgfx_vendor)
target_include_directories(agis_desktop PRIVATE
  ${AGIS_BGFX_DIR}/examples/17-drawstress
  ${AGIS_BGFX_DIR}/examples/common
  ${AGIS_BGFX_DIR}/examples/common/imgui
  ${AGIS_BGFX_DIR}/examples/common/debugdraw
  # 须置于 bgfx「examples/common」之后，否则会优先命中仓库根 3rdparty/imgui（无 imguiCreate 封装）
  ${AGIS_THIRDPARTY_ROOT}
)
# bx/bgfx 头文件要求 C++20（与 agis_bgfx_vendor 一致）
target_compile_features(agis_desktop PRIVATE cxx_std_20)

if(AGIS_USE_GDAL AND GDAL_FOUND)
  target_link_libraries(agis_workbench_app PRIVATE GDAL::GDAL)
  target_link_libraries(agis_desktop PRIVATE GDAL::GDAL)
  if(TARGET PROJ::proj)
    target_link_libraries(agis_workbench_app PRIVATE PROJ::proj)
    target_link_libraries(agis_desktop PRIVATE PROJ::proj)
  endif()
endif()

if(AGIS_HAVE_LASZIP)
  target_compile_definitions(agis_converter_app PRIVATE AGIS_HAVE_LASZIP=1)
  target_include_directories(agis_converter_app PRIVATE
    "${AGIS_LASZIP_ROOT}/dll"
    "${CMAKE_BINARY_DIR}/agis_bundled_laszip/include")
  target_link_libraries(agis_converter_app PRIVATE "${AGIS_LASZIP_TARGET}")
endif()
if(AGIS_HAVE_BASISU)
  target_compile_definitions(agis_converter_app PRIVATE AGIS_HAVE_BASISU=1)
else()
  target_compile_definitions(agis_converter_app PRIVATE AGIS_HAVE_BASISU=0)
endif()

add_executable(agis_convert_gui WIN32
  src/app/converter/main_convert_standalone.cpp
)
target_include_directories(agis_convert_gui PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${AGIS_UI_ENGINE_PUBLIC_INCLUDES}
)
target_link_libraries(agis_convert_gui PRIVATE agis_converter_app agis_common)
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
  target_link_libraries(agis_converter_app PRIVATE GDAL::GDAL)
  target_link_libraries(agis_convert_gui PRIVATE GDAL::GDAL)
  if(TARGET PROJ::proj)
    target_link_libraries(agis_converter_app PRIVATE PROJ::proj)
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
if(MSVC)
  target_compile_options(agis_workbench_app PRIVATE /utf-8 /Zc:__cplusplus /Zc:preprocessor)
  target_compile_options(agis_converter_app PRIVATE /utf-8 /Zc:__cplusplus /Zc:preprocessor)
endif()

add_executable(agis_model_preview_gui WIN32
  src/app/preview/model_preview/main.cpp
)
target_include_directories(agis_model_preview_gui PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${AGIS_UI_ENGINE_PUBLIC_INCLUDES}
)
set_target_properties(agis_model_preview_gui PROPERTIES
  OUTPUT_NAME "AGIS-ModelPreview"
  WIN32_EXECUTABLE ON
)
target_compile_definitions(agis_model_preview_gui PRIVATE UNICODE _UNICODE NOMINMAX ${_agis_gdal_defs})
target_link_libraries(agis_model_preview_gui PRIVATE
  agis_model_preview_lib
  agis_common
  agis_bgfx_vendor
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
  target_link_libraries(agis_model_preview_gui PRIVATE GDAL::GDAL)
  if(TARGET PROJ::proj)
    target_link_libraries(agis_model_preview_gui PRIVATE PROJ::proj)
  endif()
endif()
if(AGIS_HAVE_BASISU)
  target_compile_definitions(agis_model_preview_gui PRIVATE AGIS_HAVE_BASISU=1)
else()
  target_compile_definitions(agis_model_preview_gui PRIVATE AGIS_HAVE_BASISU=0)
endif()
if(MSVC)
  target_compile_options(agis_model_preview_gui PRIVATE /utf-8 /Zc:__cplusplus /Zc:preprocessor)
endif()

add_executable(agis_tile_preview_gui WIN32
  src/app/preview/tile_preview/main.cpp
)
target_include_directories(agis_tile_preview_gui PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app
  ${AGIS_UI_ENGINE_PUBLIC_INCLUDES}
)
set_target_properties(agis_tile_preview_gui PROPERTIES
  OUTPUT_NAME "AGIS-TilePreview"
  WIN32_EXECUTABLE ON
)
target_compile_definitions(agis_tile_preview_gui PRIVATE UNICODE _UNICODE NOMINMAX ${_agis_gdal_defs})
target_link_libraries(agis_tile_preview_gui PRIVATE
  agis_tile_preview_lib
  agis_common
  agis_ui_engine
  comctl32
  comdlg32
  user32
  gdi32
  gdiplus
  shell32
)
if(AGIS_USE_GDAL AND GDAL_FOUND)
  target_link_libraries(agis_tile_preview_gui PRIVATE GDAL::GDAL)
  if(TARGET PROJ::proj)
    target_link_libraries(agis_tile_preview_gui PRIVATE PROJ::proj)
  endif()
endif()
if(AGIS_HAVE_BASISU)
  target_compile_definitions(agis_tile_preview_gui PRIVATE AGIS_HAVE_BASISU=1)
else()
  target_compile_definitions(agis_tile_preview_gui PRIVATE AGIS_HAVE_BASISU=0)
endif()
if(MSVC)
  target_compile_options(agis_tile_preview_gui PRIVATE /utf-8 /Zc:__cplusplus /Zc:preprocessor)
endif()

if(WIN32 AND AGIS_USE_GDAL AND GDAL_FOUND AND AGIS_GDAL_BUNDLED)
  agis_copy_bundled_gdal_deps_to_exe(agis_desktop)
  agis_copy_bundled_gdal_deps_to_exe(agis_convert_gui)
  agis_copy_bundled_gdal_deps_to_exe(agis_model_preview_gui)
  agis_copy_bundled_gdal_deps_to_exe(agis_tile_preview_gui)
endif()

if(WIN32 AND AGIS_UI_ENGINE_SHARED)
  add_custom_command(TARGET agis_desktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:agis_ui_engine>
      $<TARGET_FILE_DIR:agis_desktop>)
  add_custom_command(TARGET agis_convert_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:agis_ui_engine>
      $<TARGET_FILE_DIR:agis_convert_gui>)
  add_custom_command(TARGET agis_model_preview_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:agis_ui_engine>
      $<TARGET_FILE_DIR:agis_model_preview_gui>)
  add_custom_command(TARGET agis_tile_preview_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:agis_ui_engine>
      $<TARGET_FILE_DIR:agis_tile_preview_gui>)
endif()

if(WIN32 AND AGIS_MAP_ENGINE_SHARED)
  foreach(_agis_me_exe IN ITEMS agis_desktop agis_convert_gui)
    add_custom_command(TARGET ${_agis_me_exe} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:agis_map_engine>
        $<TARGET_FILE_DIR:${_agis_me_exe}>)
    add_custom_command(TARGET ${_agis_me_exe} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:agis_common>
        $<TARGET_FILE_DIR:${_agis_me_exe}>)
  endforeach()
  foreach(_agis_copy_common IN ITEMS agis_model_preview_gui agis_tile_preview_gui)
    add_custom_command(TARGET ${_agis_copy_common} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:agis_common>
        $<TARGET_FILE_DIR:${_agis_copy_common}>)
  endforeach()
endif()

if(MSVC)
  target_sources(agis_desktop PRIVATE app.manifest)
  target_sources(agis_model_preview_gui PRIVATE app.manifest)
  target_sources(agis_tile_preview_gui PRIVATE app.manifest)
  target_compile_options(agis_desktop PRIVATE /utf-8)
  # bx/bgfx 头文件要求（与 agis_bgfx_vendor 一致）
  target_compile_options(agis_desktop PRIVATE /Zc:__cplusplus /Zc:preprocessor)
endif()

# 独立演示：链接 agis_ui_engine（GDI+ / 平台实现已在库内）。
if(WIN32)
  add_executable(ui_engine_demo WIN32
    ${AGIS_UI_ENGINE_DIR}/demo/src/ui_engine_demo.cpp
    ${AGIS_UI_ENGINE_DIR}/demo/src/ui_engine_demo_main.cpp
  )
else()
  add_executable(ui_engine_demo
    ${AGIS_UI_ENGINE_DIR}/demo/src/ui_engine_demo.cpp
    ${AGIS_UI_ENGINE_DIR}/demo/src/ui_engine_demo_main.cpp
  )
endif()

# `widgets_all.h` → `ui_engine/ui_private.h`（声明在 ui_engine/include，实现仍在 widgets_mainframe.cpp）
target_include_directories(ui_engine_demo PRIVATE
  ${AGIS_UI_ENGINE_PUBLIC_INCLUDES}
  ${AGIS_UI_ENGINE_DIR}/demo/src
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/app)
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

# map_engine：独立演示（顶层 Map 宿主 + GDI+ / agis_common）。
if(WIN32)
  set(_map_engine_demo_sources ${AGIS_MAP_ENGINE_DIR}/demo/src/map_engine_demo_main.cpp)
  if(NOT AGIS_MAP_ENGINE_SHARED)
    list(APPEND _map_engine_demo_sources ${_agis_workbench_imgui_sources})
  endif()
  add_executable(map_engine_demo WIN32 ${_map_engine_demo_sources})
  target_include_directories(map_engine_demo PRIVATE
    ${AGIS_UI_ENGINE_PUBLIC_INCLUDES}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
  )
  if(NOT AGIS_MAP_ENGINE_SHARED)
    target_include_directories(map_engine_demo PRIVATE
      ${AGIS_BX_DIR}/include
      $<$<AND:$<PLATFORM_ID:Windows>,$<CXX_COMPILER_ID:MSVC>>:${AGIS_BX_DIR}/include/compat/msvc>
      ${AGIS_BX_DIR}/3rdparty
      ${AGIS_BGFX_DIR}/include
      ${AGIS_BGFX_DIR}/3rdparty
      ${AGIS_BGFX_DIR}/3rdparty/khronos
      ${AGIS_BGFX_DIR}/examples/17-drawstress
      ${AGIS_BGFX_DIR}/examples/common
      ${AGIS_BGFX_DIR}/examples/common/imgui
      ${AGIS_THIRDPARTY_ROOT}
    )
  endif()
  set_target_properties(map_engine_demo PROPERTIES WIN32_EXECUTABLE ON)
  target_compile_definitions(map_engine_demo PRIVATE
    UNICODE _UNICODE NOMINMAX ${_agis_gdal_defs}
    AGIS_USE_BGFX=1 BX_CONFIG_DEBUG=0
    AGIS_BUILD_MAP_ENGINE_DEMO=1
  )
  if(NOT AGIS_UI_ENGINE_SHARED)
    target_compile_definitions(map_engine_demo PRIVATE AGIS_UI_STATIC=1)
  endif()
  target_link_libraries(map_engine_demo PRIVATE
    agis_map_engine
    agis_common
    agis_ui_engine
    agis_bgfx_vendor
    comctl32
    comdlg32
    user32
    gdi32
    gdiplus
    shell32
    d2d1
  )
  target_compile_features(map_engine_demo PRIVATE cxx_std_20)
  if(AGIS_USE_GDAL AND GDAL_FOUND)
    target_link_libraries(map_engine_demo PRIVATE GDAL::GDAL)
    if(TARGET PROJ::proj)
      target_link_libraries(map_engine_demo PRIVATE PROJ::proj)
    endif()
  endif()
  if(WIN32 AND AGIS_UI_ENGINE_SHARED)
    add_custom_command(TARGET map_engine_demo POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:agis_ui_engine>
        $<TARGET_FILE_DIR:map_engine_demo>)
  endif()
  if(WIN32 AND AGIS_MAP_ENGINE_SHARED)
    add_custom_command(TARGET map_engine_demo POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:agis_map_engine>
        $<TARGET_FILE_DIR:map_engine_demo>)
    add_custom_command(TARGET map_engine_demo POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:agis_common>
        $<TARGET_FILE_DIR:map_engine_demo>)
  endif()
  # 须在 add_executable(map_engine_demo) 之后：此前 if(TARGET map_engine_demo) 恒为假，导致未复制 gdal/proj/curl/tiff。
  if(WIN32 AND AGIS_USE_GDAL AND GDAL_FOUND AND AGIS_GDAL_BUNDLED)
    agis_copy_bundled_gdal_deps_to_exe(map_engine_demo)
  endif()
  if(MSVC)
    target_sources(map_engine_demo PRIVATE app.manifest)
    target_compile_options(map_engine_demo PRIVATE /utf-8 /Zc:__cplusplus /Zc:preprocessor)
  endif()
endif()

# 7 个数据转换后端命令行工具（3 类型互转 6 过程 + 模型↔模型 1 过程）
set(_agis_convert_backend_sources
  src/app/tools/convert_backend_common.cpp
)
if(AGIS_HAVE_BASISU)
  list(APPEND _agis_convert_backend_sources src/app/tools/ktx2_basis_encode.cpp)
endif()
add_library(agis_convert_backend_common STATIC ${_agis_convert_backend_sources})
target_include_directories(agis_convert_backend_common PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/app)
target_link_libraries(agis_convert_backend_common PUBLIC agis_common)
target_compile_features(agis_convert_backend_common PRIVATE cxx_std_20)
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
  add_executable(${_tool} src/app/tools/${_tool}.cpp)
  target_include_directories(${_tool} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/app)
  target_link_libraries(${_tool} PRIVATE agis_convert_backend_common)
  if(MSVC)
    target_compile_options(${_tool} PRIVATE /utf-8)
  endif()
endforeach()

if(WIN32 AND AGIS_USE_GDAL AND GDAL_FOUND AND AGIS_GDAL_BUNDLED)
  foreach(_tool IN LISTS _agis_convert_tools)
    agis_copy_bundled_gdal_deps_to_exe(${_tool})
  endforeach()
endif()

# 捆绑 GDAL 运行时所需 gcore/data（如 tms_*.json）；复制到 build/gdal_data，并由 AgisEnsureGdalDataPath 探测。
set(_AGIS_GDAL_GCORE_DATA "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/gdal-3.12.3/gcore/data")
if(AGIS_USE_GDAL AND GDAL_FOUND AND EXISTS "${_AGIS_GDAL_GCORE_DATA}/tms_NZTM2000.json")
  set(AGIS_GDAL_DATA_STAGE "${CMAKE_BINARY_DIR}/gdal_data")
  add_custom_target(agis_gdal_runtime_data ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${AGIS_GDAL_DATA_STAGE}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_AGIS_GDAL_GCORE_DATA}" "${AGIS_GDAL_DATA_STAGE}"
    COMMENT "Stage GDAL_DATA (gcore/data) for AGIS")
  add_dependencies(agis_desktop agis_gdal_runtime_data)
  if(TARGET map_engine_demo)
    add_dependencies(map_engine_demo agis_gdal_runtime_data)
  endif()
  # 仅挂在 agis_desktop 上复制一份：各转换工具与主程序默认在同一 $(OutDir)/Configuration 目录下，
  # 若对每个 exe 各自 POST_BUILD copy_directory，并行编译时会多进程同时写同一 gdal_data，易触发 MSB3073。
  add_custom_command(
    TARGET agis_desktop
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${AGIS_GDAL_DATA_STAGE}"
            "$<TARGET_FILE_DIR:agis_desktop>/gdal_data"
    COMMENT "Copy GDAL_DATA next to AGIS (shared by convert tools in same output dir)")
  if(TARGET map_engine_demo)
    add_custom_command(
      TARGET map_engine_demo
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${AGIS_GDAL_DATA_STAGE}"
              "$<TARGET_FILE_DIR:map_engine_demo>/gdal_data"
      COMMENT "Copy GDAL_DATA next to map_engine_demo")
  endif()
  foreach(_t IN LISTS _agis_convert_tools)
    add_dependencies(${_t} agis_gdal_runtime_data)
  endforeach()
endif()

# 捆绑 PROJ 数据库（proj.db）：动态链接 PROJ 时默认不嵌入 DB，独立 proj_context 依赖 PROJ_DATA。
# 先暂存到 ${CMAKE_BINARY_DIR}/proj_data；与 gdal_data 相同，仅挂在 agis_desktop 上 POST_BUILD 复制到 $(OutDir)/proj_data，
# 避免对每个 exe 各自复制时 VS 多目标并行同时写同一目录导致失败。同目录下的转换工具与预览 exe 共用该份 proj_data。
# 运行时亦可自 exe 向上探测 build/proj_data。
# copy_directory 来自 PROJ data 的构建树，会带入 CMakeFiles/、cmake_install.cmake、x64/ 等；复制后脚本剔除，仅保留运行所需文件。
if(WIN32 AND AGIS_USE_GDAL AND GDAL_FOUND AND AGIS_GDAL_BUNDLED AND TARGET generate_proj_db)
  set(_agis_proj_built_data "${CMAKE_BINARY_DIR}/agis_bundled_proj/data")
  set(AGIS_PROJ_DATA_STAGE "${CMAKE_BINARY_DIR}/proj_data")
  add_custom_target(agis_proj_runtime_data ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${AGIS_PROJ_DATA_STAGE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${AGIS_PROJ_DATA_STAGE}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_agis_proj_built_data}" "${AGIS_PROJ_DATA_STAGE}"
    COMMAND ${CMAKE_COMMAND} "-DAGIS_PROJ_STAGE_DST=${AGIS_PROJ_DATA_STAGE}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/agis_clean_proj_data_stage.cmake"
    COMMENT "Stage PROJ_DATA (proj.db etc.) under build/proj_data for AGIS")
  add_dependencies(agis_proj_runtime_data generate_proj_db)
  set(_agis_proj_runtime_consumers
    agis_desktop
    agis_tile_preview_gui
    agis_model_preview_gui
    agis_convert_gui
    map_engine_demo)
  foreach(_c IN LISTS _agis_proj_runtime_consumers)
    if(TARGET "${_c}")
      add_dependencies("${_c}" agis_proj_runtime_data)
    endif()
  endforeach()
  foreach(_t IN LISTS _agis_convert_tools)
    add_dependencies("${_t}" agis_proj_runtime_data)
  endforeach()
  add_custom_command(
    TARGET agis_desktop
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${AGIS_PROJ_DATA_STAGE}"
            "$<TARGET_FILE_DIR:agis_desktop>/proj_data"
    COMMENT "Copy PROJ_DATA next to AGIS (shared OutDir)")
  if(TARGET map_engine_demo)
    add_custom_command(
      TARGET map_engine_demo
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${AGIS_PROJ_DATA_STAGE}"
              "$<TARGET_FILE_DIR:map_engine_demo>/proj_data"
      COMMENT "Copy PROJ_DATA next to map_engine_demo")
  endif()
endif()

add_dependencies(agis_desktop ${_agis_convert_tools} agis_convert_gui agis_model_preview_gui agis_tile_preview_gui)
if(WIN32)
  foreach(_tool IN LISTS _agis_convert_tools)
    add_custom_command(TARGET agis_desktop POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:${_tool}>
        $<TARGET_FILE_DIR:agis_desktop>)
  endforeach()
endif()
