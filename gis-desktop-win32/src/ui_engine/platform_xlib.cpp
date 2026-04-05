#include "ui_engine/platform_xlib.h"

#include "ui_engine/app.h"

#include <X11/Xlib.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace agis::ui {

class PlatformXlib::Impl {
 public:
  Display* dpy{nullptr};
  std::atomic<bool> quit{false};
};

PlatformXlib::PlatformXlib() : impl_(std::make_unique<Impl>()) {
  impl_->dpy = XOpenDisplay(nullptr);
}

PlatformXlib::~PlatformXlib() {
  if (impl_ && impl_->dpy) {
    XCloseDisplay(impl_->dpy);
    impl_->dpy = nullptr;
  }
}

int PlatformXlib::runEventLoop(App& app) {
  (void)app;
  if (!impl_ || !impl_->dpy) {
    return 1;
  }
  while (!impl_->quit.load(std::memory_order_acquire)) {
    while (XPending(impl_->dpy) > 0) {
      XEvent ev{};
      XNextEvent(impl_->dpy, &ev);
      (void)ev;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return 0;
}

void PlatformXlib::requestExit() {
  if (impl_) {
    impl_->quit.store(true, std::memory_order_release);
  }
}

const char* PlatformXlib::backendId() const { return "xlib"; }

}  // namespace agis::ui
