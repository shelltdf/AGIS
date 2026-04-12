# agis_map_engine — 地图引擎库（静/动由父工程 AGIS_MAP_ENGINE_SHARED 决定）
#
# 由 gis-desktop-win32 以 add_subdirectory(../map_engine) 引入。
# 依赖父工程已定义的：AGIS_UI_ENGINE_DIR、仓库根 common/（目标 agis_common）、AGIS_BGFX_DIR、
# AGIS_BX_DIR、AGIS_BIMG_DIR、agis_bgfx_vendor、_agis_gdal_defs、GDAL/PROJ 等。

if(NOT DEFINED AGIS_UI_ENGINE_DIR)
  message(FATAL_ERROR "agis_map_engine: 请从 gis-desktop-win32 工程内 add_subdirectory(../map_engine)。")
endif()
if(NOT TARGET agis_bgfx_vendor)
  message(FATAL_ERROR "agis_map_engine: 须先于本目录定义目标 agis_bgfx_vendor。")
endif()

if(NOT DEFINED AGIS_MAP_ENGINE_SHARED)
  option(AGIS_MAP_ENGINE_SHARED "Build agis_map_engine as SHARED (standalone configure fallback)" OFF)
endif()

set(_ME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(_ME_THIRDPARTY "${_ME_ROOT}/../3rdparty")

# 按功能组划分：每组 <组>/include/map_engine/*.h + <组>/src/*.cpp
# PUBLIC 聚合各组 include，对外仍为 #include "map_engine/map_*.h" 等
set(_agis_map_engine_sources
  layer/src/map_layer.cpp
  layer/src/map_layer_driver.cpp
  layer/src/map_layer_driver_gdal.cpp
  document/src/map.cpp
  projection/src/map_projection.cpp
  scene_graph/src/scene_graph.cpp
  view/src/map_viewport.cpp
  render/src/map_gpu.cpp
  render/src/map_gpu_d2d.cpp
  render/src/map_gpu_bgfx.cpp
  gis_file_io/src/gis_xml.cpp
  gis_file_io/src/gis_project_xml.cpp
  engine/src/map_engine.cpp
)
if(AGIS_MAP_ENGINE_SHARED)
  add_library(agis_map_engine SHARED ${_agis_map_engine_sources})
  message(STATUS "AGIS: agis_map_engine = SHARED (DLL/SO)")
else()
  add_library(agis_map_engine STATIC ${_agis_map_engine_sources})
  message(STATUS "AGIS: agis_map_engine = STATIC (.lib/.a)")
endif()

target_include_directories(agis_map_engine
  PUBLIC
    "${_ME_ROOT}/layer/include"
    "${_ME_ROOT}/document/include"
    "${_ME_ROOT}/projection/include"
    "${_ME_ROOT}/scene_graph/include"
    "${_ME_ROOT}/view/include"
    "${_ME_ROOT}/render/include"
    "${_ME_ROOT}/gis_file_io/include"
    "${_ME_ROOT}/engine/include"
  PRIVATE
    "${_ME_ROOT}/layer/src"
    "${AGIS_UI_ENGINE_DIR}/core/include"
    "${AGIS_UI_ENGINE_DIR}/widgets/include"
    "${AGIS_UI_ENGINE_DIR}/gdiplus/include"
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

if(NOT TARGET agis_common OR NOT TARGET agis_ui_engine)
  message(FATAL_ERROR
    "agis_map_engine: 须先于本目录定义 agis_common（仓库根 common/）与 agis_ui_engine。")
endif()
# 静/动均需：继承 common 的 PUBLIC include（resource.h、utils/agis_gdal_runtime_env.h 等），并解析 engine 组中对 ui_engine 的引用。
target_link_libraries(agis_map_engine PRIVATE agis_common agis_ui_engine)

# SHARED：ImGui 只编进 map_engine DLL，避免与 workbench 重复。
if(AGIS_MAP_ENGINE_SHARED)
  set(_me_imgui_sources
    ${AGIS_BGFX_DIR}/examples/common/imgui/imgui.cpp
    ${AGIS_BGFX_DIR}/3rdparty/dear-imgui/imgui.cpp
    ${AGIS_BGFX_DIR}/3rdparty/dear-imgui/imgui_draw.cpp
    ${AGIS_BGFX_DIR}/3rdparty/dear-imgui/imgui_tables.cpp
    ${AGIS_BGFX_DIR}/3rdparty/dear-imgui/imgui_widgets.cpp
    ${AGIS_BGFX_DIR}/3rdparty/dear-imgui/imgui_demo.cpp
  )
  target_sources(agis_map_engine PRIVATE ${_me_imgui_sources})
endif()

if(AGIS_MAP_ENGINE_SHARED)
  target_compile_definitions(agis_map_engine PRIVATE AGIS_MAP_ENGINE_EXPORTS=1)
else()
  target_compile_definitions(agis_map_engine PUBLIC AGIS_MAP_ENGINE_STATIC=1)
endif()
target_compile_definitions(agis_map_engine
  PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    ${_agis_gdal_defs}
    AGIS_USE_BGFX=1
    BX_CONFIG_DEBUG=0
)
target_compile_features(agis_map_engine PRIVATE cxx_std_20)

# MSVC：显式 decorate 未覆盖的符号较多；共享库导出全部符号（与许多第三方 C++ DLL 做法一致）
if(AGIS_MAP_ENGINE_SHARED AND WIN32)
  set_target_properties(agis_map_engine PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

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
