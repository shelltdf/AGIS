#include "map_engine/render_device_context.h"

#include <windows.h>

std::unique_ptr<RenderDeviceContext2D> CreateD2dRenderDeviceContext2D();
std::unique_ptr<RenderDeviceContext3D> CreateBgfxRenderDeviceContext3D(MapRenderBackend3D backend);

AGIS_MAP_ENGINE_API std::unique_ptr<RenderDeviceContext2D> CreateRenderDeviceContext2D(MapRenderBackend2D backend) {
  switch (backend) {
    case MapRenderBackend2D::kGdi:
    case MapRenderBackend2D::kGdiPlus:
      return std::make_unique<CpuRenderDeviceContext2D>();
    case MapRenderBackend2D::kD2d:
      break;
  }
#if defined(_WIN32)
  return CreateD2dRenderDeviceContext2D();
#else
  return nullptr;
#endif
}

AGIS_MAP_ENGINE_API std::unique_ptr<RenderDeviceContext3D> CreateRenderDeviceContext3D(MapRenderBackend3D backend) {
#if defined(_WIN32)
  return CreateBgfxRenderDeviceContext3D(backend);
#else
  (void)backend;
  return nullptr;
#endif
}

void RenderDeviceContext::beginHostInit(HWND hwnd) {
  shutdown(hwnd);
  active_ = MapRenderBackendType::kGdi;
  bufW_ = 0;
  bufH_ = 0;
  if (!hwnd) {
    return;
  }
  RECT r{};
  GetClientRect(hwnd, &r);
  bufW_ = r.right - r.left;
  bufH_ = r.bottom - r.top;
}

bool RenderDeviceContext::init2D(HWND hwnd, MapRenderBackend2D backend) {
  beginHostInit(hwnd);
  if (!hwnd) {
    return true;
  }
  auto created = CreateRenderDeviceContext2D(backend);
  if (!created || !created->initGraphics(hwnd)) {
    impl_.reset();
    return false;
  }
  impl_.reset(created.release());
  active_ = MapRenderBackendFrom2D(backend);
  return true;
}

bool RenderDeviceContext::init3D(HWND hwnd, MapRenderBackend3D backend) {
  beginHostInit(hwnd);
  if (!hwnd) {
    return true;
  }
#if defined(_WIN32)
  auto created = CreateRenderDeviceContext3D(backend);
  if (!created || !created->initGraphics(hwnd)) {
    impl_.reset();
    return false;
  }
  impl_.reset(created.release());
  active_ = MapRenderBackendFrom3D(backend);
  return true;
#else
  (void)backend;
  return false;
#endif
}

bool RenderDeviceContext::init(HWND hwnd, MapRenderBackendType backend) {
  if (!hwnd) {
    beginHostInit(hwnd);
    return true;
  }
  if (MapGpu_Is2DBackend(backend)) {
    return init2D(hwnd, MapGpu_To2DBackend(backend));
  }
  if (MapGpu_Is3DBackend(backend)) {
    return init3D(hwnd, MapGpu_To3DBackend(backend));
  }
  beginHostInit(hwnd);
  return true;
}

void RenderDeviceContext::shutdown(HWND hwnd) {
  (void)hwnd;
  if (impl_) {
    impl_->shutdownGraphics();
    impl_.reset();
  }
  active_ = MapRenderBackendType::kGdi;
  bufW_ = 0;
  bufH_ = 0;
}

void RenderDeviceContext::onResize(int w, int h) {
  bufW_ = w;
  bufH_ = h;
  if (impl_) {
    impl_->resizeGraphics(w, h);
  }
}

bool RenderDeviceContext::presentFrame(HWND hwnd, const uint8_t* bgraTopDown, int w, int h) {
  if (!bgraTopDown || w <= 0 || h <= 0 || !impl_) {
    return false;
  }
  return impl_->presentBgraTopDown(hwnd, bgraTopDown, w, h);
}

bool RenderDeviceContext::imguiMapToolbarHitClient(int clientX, int clientY) const {
  return impl_ && impl_->imguiMapToolbarHitClient(clientX, clientY);
}
