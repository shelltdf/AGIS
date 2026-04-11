#include "map_engine/map_gpu.h"

#if defined(_WIN32)

#include <algorithm>
#include <cstring>

#include <windows.h>
#include <d2d1.h>

#pragma comment(lib, "d2d1.lib")

namespace map_gpu_d2d {

namespace {

ID2D1Factory* g_factory = nullptr;
ID2D1HwndRenderTarget* g_rt = nullptr;
ID2D1Bitmap* g_staging = nullptr;
HWND g_hwnd = nullptr;
int g_clientW = 0;
int g_clientH = 0;

void ReleaseStaging() {
  if (g_staging) {
    g_staging->Release();
    g_staging = nullptr;
  }
}

}  // namespace

void Shutdown() {
  ReleaseStaging();
  if (g_rt) {
    g_rt->Release();
    g_rt = nullptr;
  }
  if (g_factory) {
    g_factory->Release();
    g_factory = nullptr;
  }
  g_hwnd = nullptr;
  g_clientW = 0;
  g_clientH = 0;
}

bool Init(HWND hwnd) {
  Shutdown();
  if (!hwnd) {
    return false;
  }
  HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&g_factory));
  if (FAILED(hr) || !g_factory) {
    return false;
  }

  RECT cr{};
  GetClientRect(hwnd, &cr);
  g_clientW = (std::max)(1, static_cast<int>(cr.right - cr.left));
  g_clientH = (std::max)(1, static_cast<int>(cr.bottom - cr.top));

  const D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 0.f, 0.f,
      D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
  const D2D1_HWND_RENDER_TARGET_PROPERTIES htp = D2D1::HwndRenderTargetProperties(
      hwnd, D2D1::SizeU(static_cast<UINT32>(g_clientW), static_cast<UINT32>(g_clientH)));
  hr = g_factory->CreateHwndRenderTarget(&rtp, &htp, &g_rt);
  if (FAILED(hr) || !g_rt) {
    Shutdown();
    return false;
  }
  g_hwnd = hwnd;
  return true;
}

void OnResize(int w, int h) {
  if (!g_rt || w <= 0 || h <= 0) {
    return;
  }
  ReleaseStaging();
  g_clientW = w;
  g_clientH = h;
  D2D1_SIZE_U sz = D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h));
  g_rt->Resize(&sz);
}

bool Present(HWND hwnd, const uint8_t* bgraTopDown, int w, int h) {
  if (!g_rt || !bgraTopDown || w <= 0 || h <= 0 || hwnd != g_hwnd) {
    return false;
  }

  ReleaseStaging();
  const D2D1_BITMAP_PROPERTIES bp =
      D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
  const UINT32 uw = static_cast<UINT32>(w);
  const UINT32 uh = static_cast<UINT32>(h);
  HRESULT hr = g_rt->CreateBitmap(D2D1::SizeU(uw, uh), bgraTopDown, static_cast<UINT32>(w) * 4u, &bp, &g_staging);
  if (FAILED(hr) || !g_staging) {
    return false;
  }

  g_rt->BeginDraw();
  g_rt->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 1.f));
  const float vw = static_cast<float>(g_clientW);
  const float vh = static_cast<float>(g_clientH);
  const D2D1_RECT_F dest = D2D1::RectF(0.f, 0.f, vw, vh);
  g_rt->DrawBitmap(g_staging, dest, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
  hr = g_rt->EndDraw();

  ReleaseStaging();

  if (hr == D2DERR_RECREATE_TARGET) {
    HWND saved = g_hwnd;
    const int ph = h;
    Shutdown();
    return Init(saved) && Present(hwnd, bgraTopDown, w, ph);
  }
  return SUCCEEDED(hr);
}

}  // namespace map_gpu_d2d

#else  // !_WIN32

namespace map_gpu_d2d {

void Shutdown() {}
bool Init(HWND) { return false; }
void OnResize(int, int) {}
bool Present(HWND, const uint8_t*, int, int) { return false; }

}  // namespace map_gpu_d2d

#endif
