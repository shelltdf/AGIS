#include "ui_engine/platform_xcb.h"

#include "ui_engine/app.h"

#include <xcb/xcb.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

namespace agis::ui {

class PlatformXcb::Impl {
 public:
  xcb_connection_t* conn{nullptr};
  std::atomic<bool> quit{false};
};

PlatformXcb::PlatformXcb() : impl_(std::make_unique<Impl>()) {
  impl_->conn = xcb_connect(nullptr, nullptr);
  if (xcb_connection_has_error(impl_->conn)) {
    xcb_disconnect(impl_->conn);
    impl_->conn = nullptr;
  }
}

PlatformXcb::~PlatformXcb() {
  if (impl_ && impl_->conn) {
    xcb_disconnect(impl_->conn);
    impl_->conn = nullptr;
  }
}

int PlatformXcb::runEventLoop(App& app) {
  (void)app;
  if (!impl_ || !impl_->conn) {
    return 1;
  }
  while (!impl_->quit.load(std::memory_order_acquire)) {
    xcb_generic_event_t* ev = xcb_poll_for_event(impl_->conn);
    if (ev) {
      std::free(ev);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  return 0;
}

void PlatformXcb::requestExit() {
  if (impl_) {
    impl_->quit.store(true, std::memory_order_release);
  }
}

const char* PlatformXcb::backendId() const { return "xcb"; }

}  // namespace agis::ui
