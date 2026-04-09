#pragma once

#include <windows.h>

#include "app/model_preview_types.h"

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

bool agis_bgfx_preview_init(HWND hwnd, AgisBgfxPreviewContext** ctx, AgisBgfxRendererKind renderer, const ObjPreviewModel& model);
void agis_bgfx_preview_shutdown(HWND hwnd, AgisBgfxPreviewContext* ctx);
void agis_bgfx_preview_draw(AgisBgfxPreviewContext* ctx, HWND hwnd, const RECT& viewportPx, float rotX, float rotY, float zoom,
                            bool solid, bool showGrid, bool backfaceCulling);
bool agis_bgfx_preview_get_runtime_stats(AgisBgfxPreviewContext* ctx, AgisBgfxRuntimeStats* out);
bool agis_bgfx_preview_set_texture(AgisBgfxPreviewContext* ctx, const std::wstring& texturePath);
bool agis_bgfx_preview_set_pbr_textures(AgisBgfxPreviewContext* ctx, const AgisBgfxPbrTexturePaths& paths);
bool agis_bgfx_preview_set_pseudo_pbr(AgisBgfxPreviewContext* ctx, bool enabled);
bool agis_bgfx_preview_set_pbr_view_mode(AgisBgfxPreviewContext* ctx, AgisBgfxPbrViewMode mode);
