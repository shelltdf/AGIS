#pragma once

/** agis_common_app 为 DLL 时：BUILDING 仅在该目标的 .cpp 中通过 target 的 compile definition 开启。 */
#if defined(_WIN32)
#  if defined(AGIS_COMMON_APP_BUILDING)
#    define AGIS_COMMON_API __declspec(dllexport)
#  elif defined(AGIS_COMMON_APP_SHARED)
#    define AGIS_COMMON_API __declspec(dllimport)
#  else
#    define AGIS_COMMON_API
#  endif
#else
#  define AGIS_COMMON_API
#endif
