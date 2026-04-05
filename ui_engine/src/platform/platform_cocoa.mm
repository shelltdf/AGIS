#include "platform_cocoa.h"

#include "ui_engine/app.h"

#import <Cocoa/Cocoa.h>

namespace agis::ui {

PlatformCocoa::PlatformCocoa() = default;

PlatformCocoa::~PlatformCocoa() = default;

int PlatformCocoa::runEventLoop(App& app) {
  // 约定：每个 app.rootWidgets() 根对应一个 NSWindow，全部关闭后结束；当前占位未实现。
  (void)app;
  @autoreleasepool {
    NSApplication* nsapp = [NSApplication sharedApplication];
    [nsapp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [nsapp run];
  }
  return 0;
}

void PlatformCocoa::requestExit() {
  dispatch_async(dispatch_get_main_queue(), ^{
    [NSApp terminate:nil];
  });
}

const char* PlatformCocoa::backendId() const { return "cocoa"; }

}  // namespace agis::ui
