# Build libcurl from ../3rdparty/curl-8.19.0 (Windows: Schannel TLS, no OpenSSL).
# Custom cmake/FindCURL.cmake resolves find_package(CURL) via target CURL::libcurl (install config is invalid in sub-builds).

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(_agis_curl_src "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/curl-8.19.0")
if(NOT EXISTS "${_agis_curl_src}/CMakeLists.txt")
  message(FATAL_ERROR "AGIS: missing ${_agis_curl_src}/CMakeLists.txt")
endif()

set(AGIS_BUNDLED_CURL_SOURCE_DIR "${_agis_curl_src}" CACHE INTERNAL "Bundled curl source (for FindCURL)")
unset(CURL_DIR CACHE)

message(STATUS "AGIS: building bundled libcurl from ${_agis_curl_src}")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "curl: static only (AGIS)" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "curl: build static lib" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "curl: skip curl.exe (AGIS)" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "curl: skip tests" FORCE)
set(PICKY_COMPILER OFF CACHE BOOL "curl: do not treat warnings as errors (AGIS)" FORCE)

set(CURL_USE_SCHANNEL ON CACHE BOOL "curl: Windows Schannel TLS" FORCE)
set(CURL_WINDOWS_SSPI ON CACHE BOOL "curl: SSPI" FORCE)
set(CURL_ZLIB "OFF" CACHE STRING "curl: no zlib dep unless added to AGIS" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "curl: no libpsl (AGIS)" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "curl: no libssh2 (AGIS)" FORCE)

# MSVC + CMake 4.1: empty or invalid CMAKE_RC_COMPILER makes CMakeRCInformation.cmake:14 call
# get_filename_component(..., NAME_WE) with no path and fail inside curl's try_compile.
if(MSVC)
  set(_agis_rc_ok FALSE)
  if(CMAKE_RC_COMPILER AND NOT CMAKE_RC_COMPILER STREQUAL "")
    if(EXISTS "${CMAKE_RC_COMPILER}")
      set(_agis_rc_ok TRUE)
    endif()
  endif()
  if(NOT _agis_rc_ok)
    unset(CMAKE_RC_COMPILER CACHE)
    unset(_agis_rc CACHE)
    find_program(_agis_rc NAMES rc.exe)
    if(NOT _agis_rc AND DEFINED ENV{WindowsSdkDir})
      foreach(_sub IN ITEMS "x64" "x86")
        foreach(_ver IN ITEMS "$ENV{WindowsSDKVersion}" "10.0.0.0")
          find_program(_agis_rc NAMES rc.exe
            PATHS
              "$ENV{WindowsSdkDir}/bin/${_ver}/${_sub}"
              "$ENV{WindowsSdkDir}/bin/${_sub}"
            NO_DEFAULT_PATH
          )
          if(_agis_rc)
            break()
          endif()
        endforeach()
        if(_agis_rc)
          break()
        endif()
      endforeach()
    endif()
    if(NOT _agis_rc)
      file(GLOB _agis_rc_glob
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/rc.exe"
        "C:/Program Files/Windows Kits/10/bin/*/x64/rc.exe"
      )
      if(_agis_rc_glob)
        list(SORT _agis_rc_glob COMPARE NATURAL ORDER DESCENDING)
        list(GET _agis_rc_glob 0 _agis_rc)
      endif()
    endif()
    if(_agis_rc)
      set(CMAKE_RC_COMPILER "${_agis_rc}" CACHE FILEPATH "Windows Resource Compiler (AGIS bundled curl)" FORCE)
    else()
      message(FATAL_ERROR
        "AGIS: rc.exe not found (needed for MSVC resource tooling). "
        "Install the Windows SDK (Visual Studio Installer → Individual components) or run CMake from "
        "\"x64 Native Tools Command Prompt for VS\" so rc.exe is on PATH.")
    endif()
  endif()
endif()

add_subdirectory("${_agis_curl_src}" "${CMAKE_BINARY_DIR}/agis_bundled_curl")
