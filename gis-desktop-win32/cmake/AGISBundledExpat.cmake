# Bundle Expat from ../3rdparty/libexpat-R_2_7_5/expat for GDAL (OGR OSM XML, GPX, GML, etc.).
# Build the `expat` target first (build.py prebuilds it), then GDAL's FindEXPAT picks up the static .lib.

set(_agis_expat_root "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/libexpat-R_2_7_5/expat")
if(NOT EXISTS "${_agis_expat_root}/CMakeLists.txt")
  message(FATAL_ERROR
    "AGIS: bundled Expat not found.\n"
    "  Expected: ${_agis_expat_root}/CMakeLists.txt\n"
    "  Download Expat sources (e.g. expat R_2_7_5) and place under:\n"
    "    <repo>/3rdparty/libexpat-R_2_7_5/expat/\n")
endif()

message(STATUS "AGIS: bundling Expat from ${_agis_expat_root}")

set(EXPAT_SHARED_LIBS OFF CACHE BOOL "Expat: static lib for GDAL (AGIS)" FORCE)
set(EXPAT_BUILD_TOOLS OFF CACHE BOOL "Expat: skip xmlwf (AGIS)" FORCE)
set(EXPAT_BUILD_EXAMPLES OFF CACHE BOOL "Expat: skip examples (AGIS)" FORCE)
set(EXPAT_BUILD_TESTS OFF CACHE BOOL "Expat: skip tests (AGIS)" FORCE)
set(EXPAT_BUILD_DOCS OFF CACHE BOOL "Expat: skip docs (AGIS)" FORCE)
set(EXPAT_BUILD_PKGCONFIG OFF CACHE BOOL "Expat: skip pkg-config (AGIS)" FORCE)
set(EXPAT_ENABLE_INSTALL OFF CACHE BOOL "Expat: skip install rules (AGIS)" FORCE)

if(MSVC)
  set(EXPAT_MSVC_STATIC_CRT OFF CACHE BOOL "Expat: use /MD like AGIS (AGIS)" FORCE)
endif()

add_subdirectory("${_agis_expat_root}" "${CMAKE_BINARY_DIR}/agis_bundled_expat")

set(EXPAT_USE_STATIC_LIBS ON CACHE BOOL "FindEXPAT: XML_STATIC for static Expat (AGIS)" FORCE)

set(EXPAT_INCLUDE_DIR "${_agis_expat_root}/lib" CACHE PATH "Expat headers (AGIS bundled)" FORCE)

# GDAL uses FindEXPAT (Expat's build-tree expat-config.cmake includes a missing expat.cmake; CONFIG mode is unreliable here).
set(_agis_expat_lib "")
if(MSVC)
  foreach(_c IN ITEMS
      "${CMAKE_BINARY_DIR}/agis_bundled_expat/Release/libexpatMD.lib"
      "${CMAKE_BINARY_DIR}/agis_bundled_expat/RelWithDebInfo/libexpatMD.lib"
      "${CMAKE_BINARY_DIR}/agis_bundled_expat/MinSizeRel/libexpatMD.lib"
      "${CMAKE_BINARY_DIR}/agis_bundled_expat/Debug/libexpatdMD.lib"
      "${CMAKE_BINARY_DIR}/agis_bundled_expat/libexpatMD.lib")
    if(EXISTS "${_c}")
      set(_agis_expat_lib "${_c}")
      break()
    endif()
  endforeach()
  if(_agis_expat_lib STREQUAL "")
    file(GLOB_RECURSE _agis_expat_glob "${CMAKE_BINARY_DIR}/agis_bundled_expat/**/libexpat*.lib")
    if(_agis_expat_glob)
      list(SORT _agis_expat_glob)
      list(GET _agis_expat_glob -1 _agis_expat_lib)
    endif()
  endif()
else()
  # Ninja/Make single-config: static archive next to the expat target
  foreach(_c IN ITEMS
      "${CMAKE_BINARY_DIR}/agis_bundled_expat/libexpat.a"
      "${CMAKE_BINARY_DIR}/agis_bundled_expat/liblibexpat.a")
    if(EXISTS "${_c}")
      set(_agis_expat_lib "${_c}")
      break()
    endif()
  endforeach()
  if(_agis_expat_lib STREQUAL "")
    file(GLOB_RECURSE _agis_expat_glob "${CMAKE_BINARY_DIR}/agis_bundled_expat/**/libexpat*.a")
    if(_agis_expat_glob)
      list(SORT _agis_expat_glob)
      list(GET _agis_expat_glob -1 _agis_expat_lib)
    endif()
  endif()
endif()

if(NOT _agis_expat_lib STREQUAL "" AND EXISTS "${_agis_expat_lib}")
  set(EXPAT_LIBRARY "${_agis_expat_lib}" CACHE FILEPATH "Expat library (AGIS bundled)" FORCE)
  message(STATUS "AGIS: EXPAT_LIBRARY for GDAL (bundled) = ${_agis_expat_lib}")
  set(GDAL_USE_EXPAT ON CACHE BOOL "GDAL: use Expat (AGIS bundled)" FORCE)
else()
  message(STATUS
    "AGIS: bundled Expat static library not found yet (first configure or not built). "
    "GDAL_USE_EXPAT may stay OFF until `expat` is built and CMake is re-run. "
    "Run: python build.py (prebuilds expat then reconfigures), or build target `expat` then `cmake -B build` again.")
endif()
