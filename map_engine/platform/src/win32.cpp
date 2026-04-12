#include "map_engine_demo/platform.h"

#if defined(_WIN32)

#  include "map_engine/map_engine.h"
#  include "map_engine/renderer.h"
#  include "map_host_win32.h"

#  include "debug/ui_debug_pick.h"
#  include "ui_engine/gdiplus_ui.h"

#  include <windows.h>

namespace map_engine_demo {
namespace {

const wchar_t kMapEngineDemoClass[] = L"AGISMapEngineDemoHost";

LRESULT CALLBACK MapEngineDemoHostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  const LRESULT r = MapHostProc(hwnd, msg, wParam, lParam);
  if (msg == WM_DESTROY) {
    MapEngine::Instance().Shutdown();
    PostQuitMessage(0);
    return 0;
  }
  return r;
}

bool RegisterDemoMapClass(HINSTANCE inst) {
  WNDCLASSW wc{};
  wc.lpfnWndProc = MapEngineDemoHostProc;
  wc.hInstance = inst;
  wc.lpszClassName = kMapEngineDemoClass;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.style = 0;
  return RegisterClassW(&wc) != 0;
}

void CenterWindowOnPrimaryWorkArea(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd)) {
    return;
  }
  RECT wr{};
  if (!GetWindowRect(hwnd, &wr)) {
    return;
  }
  const int ww = wr.right - wr.left;
  const int wh = wr.bottom - wr.top;
  const HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(mon, &mi)) {
    return;
  }
  const RECT& wa = mi.rcWork;
  const int x = wa.left + (wa.right - wa.left - ww) / 2;
  const int y = wa.top + (wa.bottom - wa.top - wh) / 2;
  SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

class Win32Platform final : public Platform {
 public:
  Win32Platform(HINSTANCE inst, int nCmdShow) : inst_(inst), nCmdShow_(nCmdShow) {}

  bool Init(MapEngine& engine) override {
    UiGdiplusInit();
    gdiplusInited_ = true;
    if (!RegisterDemoMapClass(inst_)) {
      UiGdiplusShutdown();
      gdiplusInited_ = false;
      return false;
    }
    hwnd_ = CreateWindowExW(0, kMapEngineDemoClass, L"AGIS — map_engine 演示", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                            CW_USEDEFAULT, 960, 640, nullptr, nullptr, inst_, nullptr);
    if (!hwnd_) {
      UiGdiplusShutdown();
      gdiplusInited_ = false;
      return false;
    }
    engine.InitDefaultMapViewStack(hwnd_);
    CenterWindowOnPrimaryWorkArea(hwnd_);
    ShowWindow(hwnd_, nCmdShow_);
    UpdateWindow(hwnd_);
    AgisUiDebugPickInit(inst_);
    debugPickInited_ = true;
    return true;
  }

  int Step(MapEngine& engine) override {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        return static_cast<int>(msg.wParam);
      }
      if (AgisUiDebugPickHandleMessage(&msg)) {
        continue;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (Renderer* r = engine.DefaultMapView().renderer()) {
      RenderStructure rs{};
      r->cull(engine.GetSceneGraph(), rs);
      r->draw(rs);
    }
    WaitMessage();
    return 0;
  }

  void Shutdown(MapEngine&) override {
    if (debugPickInited_) {
      AgisUiDebugPickShutdown();
      debugPickInited_ = false;
    }
    if (gdiplusInited_) {
      UiGdiplusShutdown();
      gdiplusInited_ = false;
    }
  }

 private:
  HINSTANCE inst_{};
  int nCmdShow_{SW_SHOW};
  HWND hwnd_{};
  bool debugPickInited_{false};
  bool gdiplusInited_{false};
};

}  // namespace

std::unique_ptr<Platform> CreatePlatform(void* instance, int nCmdShow) {
  return std::make_unique<Win32Platform>(reinterpret_cast<HINSTANCE>(instance), nCmdShow);
}

}  // namespace map_engine_demo

#endif  // _WIN32
