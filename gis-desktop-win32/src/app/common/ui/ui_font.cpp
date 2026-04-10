#include "ui_font.h"

#include <windows.h>

static HFONT g_appUiFont = nullptr;
static HFONT g_logMonoFont = nullptr;
static bool g_appUiFontOwned = false;
static bool g_logMonoFontOwned = false;

static HFONT CreateAppUiFontInternal() {
  return CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static HFONT CreateYaHeiFallback() {
  HFONT yh = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  return yh ? yh : CreateAppUiFontInternal();
}

static HFONT CreateLogMonoFontInternal() {
  HFONT f = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono");
  if (!f) {
    f = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
  }
  return f;
}

HFONT UiGetAppFont() {
  return g_appUiFont ? g_appUiFont : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

HFONT UiGetLogFont() {
  return g_logMonoFont ? g_logMonoFont : UiGetAppFont();
}

void UiFontInit() {
  if (!g_appUiFont) {
    g_appUiFont = CreateYaHeiFallback();
    if (!g_appUiFont) {
      g_appUiFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
      g_appUiFontOwned = false;
    } else {
      g_appUiFontOwned = true;
    }
  }
  if (!g_logMonoFont) {
    g_logMonoFont = CreateLogMonoFontInternal();
    if (!g_logMonoFont) {
      g_logMonoFont = g_appUiFont;
      g_logMonoFontOwned = false;
    } else {
      g_logMonoFontOwned = true;
    }
  }
}

void UiFontShutdown() {
  if (g_logMonoFontOwned && g_logMonoFont && g_logMonoFont != g_appUiFont) {
    DeleteObject(g_logMonoFont);
  }
  g_logMonoFont = nullptr;
  g_logMonoFontOwned = false;
  if (g_appUiFontOwned && g_appUiFont) {
    DeleteObject(g_appUiFont);
  }
  g_appUiFont = nullptr;
  g_appUiFontOwned = false;
}
