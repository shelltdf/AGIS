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

/// 在**无 bgfx 调用**的工作线程中构建展开网格；`progressPct` 可选，写入 0–100。
bool agis_bgfx_preview_build_mesh_on_cpu(const ObjPreviewModel& model, bool pseudoPbr, AgisBgfxPbrViewMode mode,
                                         const AgisBgfxPbrTexturePaths& pbrPaths, int* progressPct,
                                         AgisBgfxCpuBuiltMesh* out);

/// `prebuiltMesh` 非空且含顶点时跳过主线程上的 `BuildMesh`，仅上传 GPU（大幅减轻 UI 卡顿）。
bool agis_bgfx_preview_init(HWND hwnd, AgisBgfxPreviewContext** ctx, AgisBgfxRendererKind renderer, const ObjPreviewModel& model,
                            const AgisBgfxCpuBuiltMesh* prebuiltMesh = nullptr);
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
