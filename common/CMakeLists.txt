# AGIS 根目录共享模块：按组（core / utils / debug）各含 include + src；由 gis-desktop-win32 以 add_subdirectory(../common) 引入。
# CMake 目标名：agis_common（Windows 上动态库为 agis_common.dll）。
#
# 期望父工程已定义：agis_ui_engine、_agis_gdal_defs、AGIS_USE_GDAL、GDAL_FOUND 等。
# AGIS_COMMON_APP_SHARED：未定义时默认 ON（生成 DLL/SO；OFF 为静态 .lib）。

if(NOT DEFINED AGIS_COMMON_APP_SHARED)
  option(AGIS_COMMON_APP_SHARED
    "Build agis_common as SHARED (agis_common.dll / .so). OFF = static .lib. WIN32+ON 须 AGIS_UI_ENGINE_SHARED=ON。" ON)
endif()

if(NOT DEFINED _agis_gdal_defs)
  set(_agis_gdal_defs GIS_DESKTOP_HAVE_GDAL=0)
endif()
if(NOT DEFINED AGIS_UI_ENGINE_PUBLIC_INCLUDES)
  message(FATAL_ERROR "common: AGIS_UI_ENGINE_PUBLIC_INCLUDES must be set by parent (ui_engine include dirs).")
endif()

set(_AGIS_COMMON_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(_agis_common_sources
  ${_AGIS_COMMON_ROOT}/utils/src/agis_gdal_runtime_env.cpp
  ${_AGIS_COMMON_ROOT}/utils/src/ui_layout.cpp
  ${_AGIS_COMMON_ROOT}/debug/src/ui_debug_pick.cpp
  ${_AGIS_COMMON_ROOT}/utils/src/ui_font.cpp
  ${_AGIS_COMMON_ROOT}/utils/src/ui_theme.cpp
  ${_AGIS_COMMON_ROOT}/utils/src/agis_ui_l10n.cpp
  ${_AGIS_COMMON_ROOT}/core/src/main_globals.cpp
  ${_AGIS_COMMON_ROOT}/core/src/app_log.cpp
)

if(AGIS_COMMON_APP_SHARED)
  if(WIN32 AND NOT AGIS_UI_ENGINE_SHARED)
    message(FATAL_ERROR
      "AGIS_COMMON_APP_SHARED=ON on WIN32 requires AGIS_UI_ENGINE_SHARED=ON (ui_theme → gdiplus_ui; 避免静态 ui_engine 重复链入)。")
  endif()
  add_library(agis_common SHARED ${_agis_common_sources})
  if(WIN32)
    set_target_properties(agis_common PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  endif()
  target_compile_definitions(agis_common PRIVATE AGIS_COMMON_APP_BUILDING=1)
  target_compile_definitions(agis_common INTERFACE AGIS_COMMON_APP_SHARED=1)
else()
  add_library(agis_common STATIC ${_agis_common_sources})
endif()

if(AGIS_COMMON_APP_SHARED)
  message(STATUS "AGIS: agis_common = SHARED (dynamic library)")
else()
  message(STATUS "AGIS: agis_common = STATIC")
endif()

target_include_directories(agis_common PUBLIC
  ${_AGIS_COMMON_ROOT}/core/include
  ${_AGIS_COMMON_ROOT}/utils/include
  ${_AGIS_COMMON_ROOT}/debug/include
)
target_include_directories(agis_common PRIVATE ${AGIS_UI_ENGINE_PUBLIC_INCLUDES})
target_compile_definitions(agis_common PRIVATE UNICODE _UNICODE NOMINMAX ${_agis_gdal_defs})
target_compile_features(agis_common PRIVATE cxx_std_20)

if(NOT AGIS_UI_ENGINE_SHARED)
  target_compile_definitions(agis_common PRIVATE AGIS_UI_STATIC=1)
endif()

if(AGIS_COMMON_APP_SHARED)
  target_link_libraries(agis_common PRIVATE agis_ui_engine)
endif()

if(AGIS_USE_GDAL AND GDAL_FOUND)
  target_link_libraries(agis_common PRIVATE GDAL::GDAL)
  if(TARGET PROJ::proj)
    target_link_libraries(agis_common PRIVATE PROJ::proj)
  endif()
endif()

if(MSVC)
  target_compile_options(agis_common PRIVATE /utf-8 /Zc:__cplusplus /Zc:preprocessor)
endif()
