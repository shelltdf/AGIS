# After copy_directory from PROJ's CMAKE_CURRENT_BINARY_DIR (data/), remove CMake / compiler
# debris so runtime PROJ_DATA only keeps proj.db, grids, schemas, etc.
#
# cmake -DAGIS_PROJ_STAGE_DST=... -P agis_clean_proj_data_stage.cmake

if(NOT DEFINED AGIS_PROJ_STAGE_DST)
  message(FATAL_ERROR "agis_clean_proj_data_stage.cmake: set AGIS_PROJ_STAGE_DST")
endif()

set(_root "${AGIS_PROJ_STAGE_DST}")
if(IS_DIRECTORY "${_root}")
  if(EXISTS "${_root}/CMakeFiles")
    file(REMOVE_RECURSE "${_root}/CMakeFiles")
  endif()

  # MSVC sometimes places per-config folders under the target binary dir.
  foreach(_cfg x64 Win32 ARM64)
    if(EXISTS "${_root}/${_cfg}")
      file(REMOVE_RECURSE "${_root}/${_cfg}")
    endif()
  endforeach()

  foreach(_f
      cmake_install.cmake
      CTestTestfile.cmake
      Makefile
      cmake.check_cache
      install_manifest.txt
      all.sql.in
    )
    if(EXISTS "${_root}/${_f}")
      file(REMOVE "${_root}/${_f}")
    endif()
  endforeach()

  file(GLOB _objs "${_root}/*.obj")
  foreach(_o IN LISTS _objs)
    file(REMOVE "${_o}")
  endforeach()

  file(GLOB _pdb "${_root}/*.pdb")
  foreach(_p IN LISTS _pdb)
    file(REMOVE "${_p}")
  endforeach()

  file(GLOB _ilk "${_root}/*.ilk")
  foreach(_i IN LISTS _ilk)
    file(REMOVE "${_i}")
  endforeach()
endif()
