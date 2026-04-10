#pragma once

#include <cstdint>
#include <vector>
#include <windows.h>

#include "app/preview/model_preview_types.h"

struct AgisBgfxPreviewContext;
struct AgisBgfxRuntimeStats {
  float cpuFrameMs = 0.0f;
  float gpuFrameMs = 0.0f;
  float waitSubmitMs = 0.0f;
  float waitRenderMs = 0.0f;
  uint32_t drawCalls = 0;
};

enum class AgisBgfxRendererKind : int { kD3D11 = 0, kOpenGL = 1 };

struct AgisBgfxPbrTexturePaths {
  std::wstring baseColorPath;
  std::wstring normalPath;
  std::wstring roughnessPath;
  std::wstring metallicPath;
  std::wstring aoPath;
};

enum class AgisBgfxPbrViewMode : int {
  kPbrLit = 0,
  kAlbedo = 1,
  kNormal = 2,
  kRoughness = 3,
  kMetallic = 4,
  kAo = 5,
};

/// 与 `model_preview_bgfx.cpp` 内 `PosTexColorVertex` 布局一致，供 CPU 侧线程构建后交给主线程 `bgfx::copy`。
struct AgisBgfxInterleavedVertex {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
  float u = 0.f;
  float v = 0.f;
  uint32_t abgr = 0xff000000;
};

struct AgisBgfxCpuBuiltMesh {
  std::vector<AgisBgfxInterleavedVertex> vertices;
  std::vector<uint32_t> triIndices;
  std::vector<uint32_t> lineIndices;
};

/// 工作线程解码的 2D 贴图像素（无 bgfx）；主线程据此 `bgfx::createTexture2D`。
struct AgisBgfxCpuImage2D {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t bpp = 0;
  bool bgra = false;
  std::vector<uint8_t> pixels;
};

/// 工作线程产出：mesh + 贴图 CPU 数据，一次性交给主线程上传 GPU。
struct AgisBgfxPreviewWorkerPackage {
  AgisBgfxCpuBuiltMesh mesh;
  AgisBgfxCpuImage2D baseColor;
  AgisBgfxCpuImage2D normalMap;
  AgisBgfxCpuImage2D roughnessMap;
  AgisBgfxCpuImage2D metallicMap;
  AgisBgfxCpuImage2D aoMap;
};

/// 工作线程（无 bgfx）：① OBJ/瓦片等已在加载线程解析进 `model`；② 解码 PBR 贴图到 CPU（顶点烘焙依赖）；
/// ③ 展开 BuildMesh；④ 解码 baseColor(map_Kd)。`progressPct` 可选 0–100。
bool agis_bgfx_preview_prepare_on_worker(const ObjPreviewModel& model, bool pseudoPbr, AgisBgfxPbrViewMode mode,
                                         const AgisBgfxPbrTexturePaths& pbrPaths, int* progressPct,
                                         AgisBgfxPreviewWorkerPackage* out);

/// 大块 GPU / ImGui 初始化期间泵送 Win32 消息。`repaintHwnd` 非空且有效时在本轮末尾 `InvalidateRect`，便于加载条与视口 GDI 重绘。
void agis_bgfx_preview_pump_win32_messages(HWND repaintHwnd);

/// 主线程仅：bgfx init、用 `bgfx::makeRef` 把包内像素/索引交给 GPU（避免巨型 `bgfx::copy` 卡死 UI）、渲染。
/// 若传入 `workerPackage` 非空，init 会 **std::move 吞掉** 其内容；调用后 `*workerPackage` 被清空。传 `nullptr` 时从磁盘重建（如切换 Renderer）。
/// `meshSourceForRebuild` 非空时，内部 **不再深拷贝** `model` 到缓存（大 OBJ 时拷贝可达数 GB，会卡死「未响应」）；PBR/线框切换从该指针读网格。生命周期须覆盖 ctx 直至 `shutdown`。
bool agis_bgfx_preview_init(HWND hwnd, AgisBgfxPreviewContext** ctx, AgisBgfxRendererKind renderer, const ObjPreviewModel& model,
                            AgisBgfxPreviewWorkerPackage* workerPackage = nullptr, bool pseudoPbr = true,
                            AgisBgfxPbrViewMode pbrViewMode = AgisBgfxPbrViewMode::kPbrLit, ObjPreviewModel* meshSourceForRebuild = nullptr);
void agis_bgfx_preview_shutdown(HWND hwnd, AgisBgfxPreviewContext* ctx);
void agis_bgfx_preview_draw(AgisBgfxPreviewContext* ctx, HWND hwnd, const RECT& viewportPx, float rotX, float rotY, float zoom,
                            bool solid, bool showGrid, bool backfaceCulling);
bool agis_bgfx_preview_get_runtime_stats(AgisBgfxPreviewContext* ctx, AgisBgfxRuntimeStats* out);
bool agis_bgfx_preview_set_texture(AgisBgfxPreviewContext* ctx, const std::wstring& texturePath);
bool agis_bgfx_preview_set_pbr_textures(AgisBgfxPreviewContext* ctx, const AgisBgfxPbrTexturePaths& paths);
bool agis_bgfx_preview_set_pseudo_pbr(AgisBgfxPreviewContext* ctx, bool enabled);
bool agis_bgfx_preview_set_pbr_view_mode(AgisBgfxPreviewContext* ctx, AgisBgfxPbrViewMode mode);
/// 仅替换网格缓存并重建 VB/IB（纹理与 Program 不变）。用于 LAS 点大小等参数变更。
bool agis_bgfx_preview_reload_model(AgisBgfxPreviewContext* ctx, const ObjPreviewModel& model);
