# FindCURL for AGIS: when libcurl is built via add_subdirectory(3rdparty/curl-*),
# CURLConfig.cmake uses install prefixes and breaks. Prefer the real CURL::libcurl target.

include(FindPackageHandleStandardArgs)

if(TARGET CURL::libcurl AND DEFINED AGIS_BUNDLED_CURL_SOURCE_DIR)
  set(CURL_INCLUDE_DIRS "${AGIS_BUNDLED_CURL_SOURCE_DIR}/include")
  if(EXISTS "${CURL_INCLUDE_DIRS}/curl/curlver.h")
    file(STRINGS "${CURL_INCLUDE_DIRS}/curl/curlver.h" _curl_ver_line REGEX "^#define[\t ]+LIBCURL_VERSION[\t ]+\".*\"")
    string(REGEX REPLACE "^#define[\t ]+LIBCURL_VERSION[\t ]+\"([^\"]*)\".*" "\\1" CURL_VERSION "${_curl_ver_line}")
  else()
    set(CURL_VERSION "0.0.0")
  endif()
  set(CURL_VERSION_STRING "${CURL_VERSION}")
  set(CURL_LIBRARIES CURL::libcurl)

  if(CURL_FIND_COMPONENTS)
    foreach(_c IN LISTS CURL_FIND_COMPONENTS)
      set(CURL_${_c}_FOUND TRUE)
    endforeach()
  endif()

  find_package_handle_standard_args(CURL
    REQUIRED_VARS CURL_INCLUDE_DIRS CURL_LIBRARIES
    VERSION_VAR CURL_VERSION
    HANDLE_COMPONENTS)
  return()
endif()

set(CURL_NO_CURL_CMAKE ON)
include(${CMAKE_ROOT}/Modules/FindCURL.cmake)
