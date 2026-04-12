#pragma once

#include "map_engine/export.h"

#include <cstdint>
#include <memory>
#include <windows.h>

// ---------------------------------------------------------------------------
// 渲染设备环境实现基类：2D / 3D 具体后端均派生自此（与宿主侧 ``RenderDeviceContext`` 区分）
// ---------------------------------------------------------------------------

/**
 * 具体图形 API（GDI+ / D2D / bgfx 等）实现的公共接口。
 * ``RenderDeviceContext2D`` / ``RenderDeviceContext3D`` 为其派生类型，便于按维度区分工厂与文档。
 */
class RenderDeviceContextBase {
 public:
  virtual ~RenderDeviceContextBase() = default;

  virtual bool initGraphics(HWND hwnd) {
    (void)hwnd;
    return true;
  }
  virtual void shutdownGraphics() {}
  virtual void resizeGraphics(int w, int h) {
    (void)w;
    (void)h;
  }
  /** 自上而下 BGRA8；GDI 路径在 WM_PAINT 中不走此处，可返回 false。 */
  virtual bool presentBgraTopDown(HWND hwnd, const uint8_t* bgraTopDown, int w, int h) {
    (void)hwnd;
    (void)bgraTopDown;
    (void)w;
    (void)h;
    return false;
  }
  /**
   * 客户区坐标是否落在 bgfx 路径下 ImGui「Map」工具栏区域内（由 3D 实现覆盖）。
   */
  virtual bool imguiMapToolbarHitClient(int clientX, int clientY) const {
    (void)clientX;
    (void)clientY;
    return false;
  }
};

// ---------------------------------------------------------------------------
// 2D：客户区呈现（GDI / GDI+ / D2D）
// ---------------------------------------------------------------------------

/**
 * 2D 地图客户区绘制后端：CPU 光栅化结果在窗口客户区呈现。
 * - kGdi / kGdiPlus：WM_PAINT 中直接用 GDI 位图或 GDI+ Bitmap 呈现。
 * - kD2d：自上而下 BGRA8 帧提交到 Direct2D HwndRenderTarget。
 */
enum class MapRenderBackend2D {
  kGdi,
  kGdiPlus,
  kD2d,
};

/** 2D 实现基类：派生自 ``RenderDeviceContextBase``。 */
class RenderDeviceContext2D : public RenderDeviceContextBase {};

/**
 * CPU/GDI、GDI+ 路径：无独立 GPU 呈现对象，帧在 ``WM_PAINT`` 中由宿主直接 BitBlt / GDI+ 绘制。
 */
class CpuRenderDeviceContext2D final : public RenderDeviceContext2D {};

/**
 * 按 2D 后端枚举创建具体实现（GDI/GDI+ → CPU 桩；D2D → Win32 下 Direct2D）。
 */
AGIS_MAP_ENGINE_API std::unique_ptr<RenderDeviceContext2D> CreateRenderDeviceContext2D(MapRenderBackend2D backend);

// ---------------------------------------------------------------------------
// 3D：bgfx（D3D11 / OpenGL / Auto）
// ---------------------------------------------------------------------------

/**
 * 3D/GPU 呈现后端：自上而下 BGRA8 CPU 帧上传为纹理，经 bgfx 全屏四边形绘制。
 * 具体图形 API 由 bgfx 的 RendererType 决定：
 * - kBgfxD3d11：Direct3D 11
 * - kBgfxOpenGL：OpenGL
 * - kBgfxAuto：由 bgfx 自动选择可用后端
 */
enum class MapRenderBackend3D {
  kBgfxD3d11,
  kBgfxOpenGL,
  kBgfxAuto,
};

/** 3D 实现基类：派生自 ``RenderDeviceContextBase``。 */
class RenderDeviceContext3D : public RenderDeviceContextBase {};

/** 按 3D 后端枚举创建 bgfx 实现（非 Win32 返回 nullptr）。 */
AGIS_MAP_ENGINE_API std::unique_ptr<RenderDeviceContext3D> CreateRenderDeviceContext3D(MapRenderBackend3D backend);

// ---------------------------------------------------------------------------
// 宿主侧聚合：后端枚举、init/present 调度（MapEngine 持有）
// ---------------------------------------------------------------------------

/**
 * 地图宿主统一渲染后端选择（设置项 / 菜单 / 持久化）：为 2D 与 3D 枚举值的并集，数值保持稳定。
 * ``RenderDeviceContext``（本类）由 ``MapEngine`` 持有，经 ``NativeWindow::attachRenderDeviceContext`` 绑定到窗口；
 * 内部按 ``MapRenderBackendType`` 持有 ``RenderDeviceContextBase`` 的 2D 或 3D 派生实现。
 */
enum class MapRenderBackendType : std::uint8_t {
  kGdi = 0,
  kGdiPlus = 1,
  kD2d = 2,
  kBgfxD3d11 = 3,
  kBgfxOpenGL = 4,
  kBgfxAuto = 5,
};

constexpr bool MapGpu_Is2DBackend(MapRenderBackendType b) {
  return b == MapRenderBackendType::kGdi || b == MapRenderBackendType::kGdiPlus || b == MapRenderBackendType::kD2d;
}

constexpr bool MapGpu_Is3DBackend(MapRenderBackendType b) {
  return b == MapRenderBackendType::kBgfxD3d11 || b == MapRenderBackendType::kBgfxOpenGL ||
         b == MapRenderBackendType::kBgfxAuto;
}

constexpr MapRenderBackend2D MapGpu_To2DBackend(MapRenderBackendType b) {
  return (b == MapRenderBackendType::kGdiPlus)   ? MapRenderBackend2D::kGdiPlus
         : (b == MapRenderBackendType::kD2d)       ? MapRenderBackend2D::kD2d
                                                   : MapRenderBackend2D::kGdi;
}

constexpr MapRenderBackend3D MapGpu_To3DBackend(MapRenderBackendType b) {
  return (b == MapRenderBackendType::kBgfxOpenGL) ? MapRenderBackend3D::kBgfxOpenGL
         : (b == MapRenderBackendType::kBgfxAuto) ? MapRenderBackend3D::kBgfxAuto
                                                  : MapRenderBackend3D::kBgfxD3d11;
}

constexpr MapRenderBackendType MapRenderBackendFrom2D(MapRenderBackend2D b) {
  return (b == MapRenderBackend2D::kGdiPlus) ? MapRenderBackendType::kGdiPlus
         : (b == MapRenderBackend2D::kD2d)   ? MapRenderBackendType::kD2d
                                             : MapRenderBackendType::kGdi;
}

constexpr MapRenderBackendType MapRenderBackendFrom3D(MapRenderBackend3D b) {
  return (b == MapRenderBackend3D::kBgfxOpenGL) ? MapRenderBackendType::kBgfxOpenGL
         : (b == MapRenderBackend3D::kBgfxAuto) ? MapRenderBackendType::kBgfxAuto
                                                : MapRenderBackendType::kBgfxD3d11;
}

class RenderDeviceContext {
 public:
  RenderDeviceContext() = default;

  /** 仅初始化 2D 后端（GDI / GDI+ / D2D）。 */
  bool init2D(HWND hwnd, MapRenderBackend2D backend);
  /** 仅初始化 3D 后端（bgfx + D3D11 / OpenGL / Auto）。 */
  bool init3D(HWND hwnd, MapRenderBackend3D backend);
  /** 按统一枚举初始化（内部转发 init2D / init3D）。 */
  bool init(HWND hwnd, MapRenderBackendType backend);

  void shutdown(HWND hwnd);
  void onResize(int w, int h);
  /** 将自上而下 BGRA8 帧送到当前后端（与 BitBlt 同向：首行为顶行）。 */
  bool presentFrame(HWND hwnd, const uint8_t* bgraTopDown, int w, int h);

  MapRenderBackendType activeBackend() const { return active_; }

  bool imguiMapToolbarHitClient(int clientX, int clientY) const;

 private:
  void beginHostInit(HWND hwnd);

  MapRenderBackendType active_{MapRenderBackendType::kGdi};
  int bufW_{0};
  int bufH_{0};
  std::unique_ptr<RenderDeviceContextBase> impl_;
};
