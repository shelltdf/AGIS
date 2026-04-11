#include "ui_engine/platform_gui.h"

#include <memory>
#include <utility>

#if defined(_WIN32)
#include "platform/platform_windows.h"
#elif defined(__APPLE__)
#include "platform/platform_cocoa.h"
#elif defined(__linux__) || defined(__unix__)
#if defined(AGIS_UI_USE_XCB)
#include "platform/platform_xcb.h"
#else
#include "platform/platform_xlib.h"
#endif
#endif

namespace agis::ui {

std::unique_ptr<IGuiPlatform> CreateGuiPlatform(const AppLaunchParams& launch) {
#if defined(_WIN32)
  if (launch.native_app_instance != nullptr) {
    return std::make_unique<PlatformWindows>(launch);
  }
  return std::make_unique<PlatformWindows>();
#elif defined(__APPLE__)
  (void)launch;
  return std::make_unique<PlatformCocoa>();
#elif defined(__linux__) || defined(__unix__)
#if defined(AGIS_UI_USE_XCB)
  (void)launch;
  return std::make_unique<PlatformXcb>();
#else
  (void)launch;
  return std::make_unique<PlatformXlib>();
#endif
#else
  (void)launch;
  return nullptr;
#endif
}

}  // namespace agis::ui
