# Bundle BinomialLLC/basis_universal for KTX2 + Basis Universal + mipmaps (tile/model RGB textures).
# 获取：GitHub 仓库页「Code → Download ZIP」或
#   https://github.com/BinomialLLC/basis_universal/archive/refs/heads/master.zip
# 解压后将顶层目录重命名为本仓库约定的 basis_universal/（见 ../3rdparty/README-BASIS-KTX2.md）。勿用 git clone。

if(NOT AGIS_USE_BASISU)
  return()
endif()

if(NOT EXISTS "${AGIS_BASISU_ROOT}/CMakeLists.txt")
  message(
    STATUS
    "AGIS: basis_universal not found at ${AGIS_BASISU_ROOT} (AGIS_USE_BASISU=ON: place sources per README-BASIS-KTX2.md; AGIS_HAVE_BASISU stays OFF until then)")
  return()
endif()

set(BASISU_EXAMPLES OFF CACHE BOOL "basis_universal: skip examples for AGIS" FORCE)
set(BASISU_BUILD_PYTHON OFF CACHE BOOL "basis_universal: skip Python for AGIS" FORCE)
set(BASISU_OPENCL OFF CACHE BOOL "basis_universal: skip OpenCL for AGIS" FORCE)

set(AGIS_BASISU_BINARY_DIR "${CMAKE_BINARY_DIR}/agis_bundled_basisu")
add_subdirectory("${AGIS_BASISU_ROOT}" "${AGIS_BASISU_BINARY_DIR}")

if(NOT TARGET basisu_encoder)
  message(FATAL_ERROR "AGIS: basis_universal target 'basisu_encoder' missing after add_subdirectory")
endif()

# basisu_comp / encoder 编译单元要求显式打开 KTX2；Zstd 与 bundled zstd.c 一致。
target_compile_definitions(basisu_encoder PRIVATE BASISD_SUPPORT_KTX2=1)
if(BASISU_ZSTD)
  target_compile_definitions(basisu_encoder PRIVATE BASISD_SUPPORT_KTX2_ZSTD=1)
else()
  target_compile_definitions(basisu_encoder PRIVATE BASISD_SUPPORT_KTX2_ZSTD=0)
endif()

if(MSVC)
  target_compile_options(basisu_encoder PRIVATE /utf-8)
endif()

set(AGIS_HAVE_BASISU TRUE)
message(STATUS "AGIS: bundled basis_universal (static) from ${AGIS_BASISU_ROOT}, target basisu_encoder")
