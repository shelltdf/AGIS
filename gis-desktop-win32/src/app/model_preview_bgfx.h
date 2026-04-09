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

bool agis_bgfx_preview_init(HWND hwnd, AgisBgfxPreviewContext** ctx, AgisBgfxRendererKind renderer, const ObjPreviewModel& model);
void agis_bgfx_preview_shutdown(HWND hwnd, AgisBgfxPreviewContext* ctx);
void agis_bgfx_preview_draw(AgisBgfxPreviewContext* ctx, HWND hwnd, const RECT& viewportPx, float rotX, float rotY, float zoom,
                            bool solid, bool showGrid);
bool agis_bgfx_preview_get_runtime_stats(AgisBgfxPreviewContext* ctx, AgisBgfxRuntimeStats* out);
bool agis_bgfx_preview_set_texture(AgisBgfxPreviewContext* ctx, const std::wstring& texturePath);
