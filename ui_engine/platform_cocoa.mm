#include "ui_engine/platform_cocoa.h"

#include "ui_engine/app.h"

#import <Cocoa/Cocoa.h>

namespace agis::ui {

PlatformCocoa::PlatformCocoa() = default;

PlatformCocoa::~PlatformCocoa() = default;

int PlatformCocoa::runEventLoop(App& app) {
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
