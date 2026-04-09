# Google Draco（网格 Draco 解压；与 tinygltf TINYGLTF_ENABLE_DRACO 配合）
# 源码目录：见 ../3rdparty/README-DRACO.md（Release 归档解压为 3rdparty/draco，勿 git clone）
set(AGIS_DRACO_ROOT "${CMAKE_SOURCE_DIR}/../3rdparty/draco")
if(NOT EXISTS "${AGIS_DRACO_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR
    "Draco sources missing at ${AGIS_DRACO_ROOT}.\n"
    "  Download a release archive (e.g. 1.5.7 zip from google/draco releases), extract to 3rdparty/, "
    "rename the top folder to \"draco\". See 3rdparty/README-DRACO.md.")
endif()

set(_agis_draco_saved_shared "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
set(DRACO_JS_GLUE OFF CACHE BOOL "Draco: disable JS glue for native embed" FORCE)

add_subdirectory("${AGIS_DRACO_ROOT}" "${CMAKE_BINARY_DIR}/agis_bundled_draco" EXCLUDE_FROM_ALL)

set(BUILD_SHARED_LIBS "${_agis_draco_saved_shared}")
unset(_agis_draco_saved_shared)

set(AGIS_HAVE_DRACO TRUE)
message(STATUS "AGIS: Draco bundled from ${AGIS_DRACO_ROOT} (KHR_draco_mesh_compression)")
