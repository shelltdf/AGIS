# Shim for find_package(PROJ CONFIG) when PROJ is built via add_subdirectory(3rdparty/proj-9.8.0).
# Early configure passes may load this before add_subdirectory(proj); do not fatal — let AGISBundledPROJ run.
if(NOT TARGET PROJ::proj)
  set(PROJ_FOUND FALSE)
  return()
endif()
set(PROJ_FOUND TRUE)
set(PROJ_VERSION "9.8.0")
set(PROJ_VERSION_STRING "9.8.0")
