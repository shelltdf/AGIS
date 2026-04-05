# AGIS: Stock FindTIFF CONFIG path reads LOCATION from subdirectory target `tiff` (CMake >= 3.19 forbids).
# When bundled libtiff added `tiff`, expose TIFF::TIFF and skip CONFIG/module disk search.

if(TARGET tiff)
  if(NOT TARGET TIFF::TIFF)
    add_library(TIFF::TIFF ALIAS tiff)
  endif()
  get_target_property(TIFF_INCLUDE_DIRS tiff INTERFACE_INCLUDE_DIRECTORIES)
  if(NOT TIFF_INCLUDE_DIRS)
    set(TIFF_INCLUDE_DIRS "")
  endif()
  set(TIFF_LIBRARIES TIFF::TIFF)
  set(TIFF_VERSION_STRING "4.6.0")
  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(TIFF
    FOUND_VAR TIFF_FOUND
    REQUIRED_VARS TIFF_INCLUDE_DIRS
    VERSION_VAR TIFF_VERSION_STRING)
  return()
endif()

include(${CMAKE_ROOT}/Modules/FindTIFF.cmake)
