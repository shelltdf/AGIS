#pragma once

#include <windows.h>

#include "app/model_preview_types.h"

struct AgisBgfxPreviewContext;

enum class AgisBgfxRendererKind : int { kD3D11 = 0, kOpenGL = 1 };

bool agis_bgfx_preview_init(HWND hwnd, AgisBgfxPreviewContext** ctx, AgisBgfxRendererKind renderer, const ObjPreviewModel& model);
void agis_bgfx_preview_shutdown(HWND hwnd, AgisBgfxPreviewContext* ctx);
void agis_bgfx_preview_draw(AgisBgfxPreviewContext* ctx, HWND hwnd, const RECT& viewportPx, float rotX, float rotY, float zoom,
                            bool solid);
