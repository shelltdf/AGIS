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

add_subdirectory("${_agis_curl_src}" "${CMAKE_BINARY_DIR}/agis_bundled_curl")
