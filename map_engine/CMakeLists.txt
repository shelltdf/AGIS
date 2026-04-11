# agis_map_engine — 地图引擎静态库
#
# 设计为与 gis-desktop-win32 并列，由后者以 add_subdirectory(../map_engine) 引入。
# 依赖父工程中已定义的：AGIS_UI_ENGINE_DIR、AGIS_BGFX_DIR、AGIS_BX_DIR、AGIS_BIMG_DIR、
# agis_bgfx_vendor、_agis_gdal_defs、GDAL/PROJ 等。

if(NOT DEFINED AGIS_UI_ENGINE_DIR)
  message(FATAL_ERROR "agis_map_engine: 请从 gis-desktop-win32 工程内 add_subdirectory(../map_engine)。")
endif()
if(NOT TARGET agis_bgfx_vendor)
  message(FATAL_ERROR "agis_map_engine: 须先于本目录定义目标 agis_bgfx_vendor。")
endif()

set(_ME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(_ME_DESKTOP_SRC "${_ME_ROOT}/../gis-desktop-win32/src")
set(_ME_THIRDPARTY "${_ME_ROOT}/../3rdparty")

# 按功能组划分：每组 <组>/include/map_engine/*.h + <组>/src/*.cpp
# PUBLIC 聚合各组 include，对外仍为 #include "map_engine/map_*.h"
add_library(agis_map_engine STATIC
  layer/src/map_layer.cpp
  layer/src/map_layer_driver.cpp
  layer/src/map_layer_driver_gdal.cpp
  document/src/map_document.cpp
  projection/src/map_projection.cpp
  gpu/src/map_gpu.cpp
  gpu/src/map_gpu_d2d.cpp
  gpu/src/map_gpu_bgfx.cpp
  shell/src/map_engine.cpp
)

target_include_directories(agis_map_engine
  PUBLIC
    "${_ME_ROOT}/layer/include"
    "${_ME_ROOT}/document/include"
    "${_ME_ROOT}/projection/include"
    "${_ME_ROOT}/gpu/include"
    "${_ME_ROOT}/shell/include"
  PRIVATE
    "${_ME_DESKTOP_SRC}"
    "${_ME_DESKTOP_SRC}/app"
    "${AGIS_UI_ENGINE_DIR}/include"
    "${AGIS_BX_DIR}/include"
    $<$<AND:$<PLATFORM_ID:Windows>,$<CXX_COMPILER_ID:MSVC>>:${AGIS_BX_DIR}/include/compat/msvc>
    "${AGIS_BX_DIR}/3rdparty"
    "${AGIS_BGFX_DIR}/include"
    "${AGIS_BGFX_DIR}/3rdparty"
    "${AGIS_BGFX_DIR}/3rdparty/khronos"
    "${AGIS_BGFX_DIR}/examples/17-drawstress"
    "${AGIS_BGFX_DIR}/examples/common"
    "${AGIS_BGFX_DIR}/examples/common/imgui"
    "${AGIS_BGFX_DIR}/examples/common/debugdraw"
    "${_ME_THIRDPARTY}"
    "${_ME_THIRDPARTY}/tinygltf"
)

target_link_libraries(agis_map_engine PRIVATE agis_bgfx_vendor)

target_compile_definitions(agis_map_engine PRIVATE
  UNICODE
  _UNICODE
  NOMINMAX
  ${_agis_gdal_defs}
  AGIS_USE_BGFX=1
  BX_CONFIG_DEBUG=0
)
target_compile_features(agis_map_engine PRIVATE cxx_std_20)

if(WIN32)
  target_link_libraries(agis_map_engine PRIVATE
    gdiplus
    d2d1
    comdlg32
    user32
    gdi32
    shell32
  )
endif()

if(AGIS_USE_GDAL AND GDAL_FOUND)
  target_link_libraries(agis_map_engine PRIVATE GDAL::GDAL)
endif()
if(TARGET PROJ::proj)
  target_link_libraries(agis_map_engine PRIVATE PROJ::proj)
endif()

if(MSVC)
  target_compile_options(agis_map_engine PRIVATE /utf-8 /Zc:__cplusplus /Zc:preprocessor)
endif()
