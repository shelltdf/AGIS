#include "ui_engine/gdiplus_ui.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

namespace {

ULONG_PTR g_gdiplusToken = 0;
bool g_panelThemeDark = false;

}  // namespace

void UiSetPanelThemeDark(bool dark) {
  g_panelThemeDark = dark;
}

bool UiGetPanelThemeDark() {
  return g_panelThemeDark;
}

void UiGdiplusInit() {
  if (g_gdiplusToken != 0) {
    return;
  }
  Gdiplus::GdiplusStartupInput in;
  Gdiplus::GdiplusStartup(&g_gdiplusToken, &in, nullptr);
}

void UiGdiplusShutdown() {
  if (g_gdiplusToken != 0) {
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    g_gdiplusToken = 0;
  }
}

void UiPaintLayerPanel(HDC hdc, const RECT& rc) {
  Gdiplus::Graphics g(hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;
  if (w <= 0 || h <= 0) {
    return;
  }
  const float x0 = static_cast<float>(rc.left);
  const float y0 = static_cast<float>(rc.top);
  Gdiplus::RectF bounds(x0, y0, static_cast<Gdiplus::REAL>(w), static_cast<Gdiplus::REAL>(h));
  if (g_panelThemeDark) {
    Gdiplus::LinearGradientBrush brush(Gdiplus::PointF(x0, y0), Gdiplus::PointF(x0, y0 + static_cast<float>(h)),
                                       Gdiplus::Color(255, 52, 56, 64), Gdiplus::Color(255, 36, 39, 46));
    g.FillRectangle(&brush, bounds);
  } else {
    Gdiplus::LinearGradientBrush brush(Gdiplus::PointF(x0, y0), Gdiplus::PointF(x0, y0 + static_cast<float>(h)),
                                       Gdiplus::Color(255, 250, 252, 255), Gdiplus::Color(255, 232, 240, 252));
    g.FillRectangle(&brush, bounds);
  }

  Gdiplus::SolidBrush hairline(g_panelThemeDark ? Gdiplus::Color(200, 88, 96, 110) : Gdiplus::Color(200, 180, 195, 220));
  Gdiplus::Pen edge(&hairline, 1.0f);
  g.DrawRectangle(&edge, bounds.X, bounds.Y, bounds.Width - 1.0f, bounds.Height - 1.0f);

  Gdiplus::FontFamily fam(L"Microsoft YaHei UI");
  Gdiplus::Font titleFont(&fam, 13.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font subFont(&fam, 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::SolidBrush titleBrush(
      g_panelThemeDark ? Gdiplus::Color(255, 230, 234, 242) : Gdiplus::Color(255, 30, 55, 95));
  Gdiplus::SolidBrush subBrush(g_panelThemeDark ? Gdiplus::Color(220, 168, 176, 190) : Gdiplus::Color(220, 90, 105, 130));
  Gdiplus::StringFormat fmt{};
  fmt.SetAlignment(Gdiplus::StringAlignmentNear);
  fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);

  const float textW = std::max(40.0f, static_cast<float>(w) - 24.0f - 12.0f);
  g.DrawString(L"图层", -1, &titleFont, Gdiplus::RectF(x0 + 12.0f, y0 + 10.0f, textW, 22.0f), &fmt, &titleBrush);
  g.DrawString(L"Dock · 数据列表", -1, &subFont, Gdiplus::RectF(x0 + 12.0f, y0 + 30.0f, textW, 18.0f), &fmt, &subBrush);
}

void UiPaintLayerPropsPanel(HDC hdc, const RECT& rc, const wchar_t* nameLine, const wchar_t* body) {
  Gdiplus::Graphics g(hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;
  if (w <= 0 || h <= 0) {
    return;
  }
  const float x0 = static_cast<float>(rc.left);
  const float y0 = static_cast<float>(rc.top);
  Gdiplus::RectF bounds(x0, y0, static_cast<Gdiplus::REAL>(w), static_cast<Gdiplus::REAL>(h));
  if (g_panelThemeDark) {
    Gdiplus::LinearGradientBrush bgBrush(Gdiplus::PointF(x0, y0), Gdiplus::PointF(x0, y0 + static_cast<float>(h)),
                                         Gdiplus::Color(255, 50, 54, 62), Gdiplus::Color(255, 38, 41, 49));
    g.FillRectangle(&bgBrush, bounds);
  } else {
    Gdiplus::LinearGradientBrush bgBrush(Gdiplus::PointF(x0, y0), Gdiplus::PointF(x0, y0 + static_cast<float>(h)),
                                         Gdiplus::Color(255, 248, 251, 255), Gdiplus::Color(255, 236, 252, 255));
    g.FillRectangle(&bgBrush, bounds);
  }

  Gdiplus::SolidBrush edgeCol(g_panelThemeDark ? Gdiplus::Color(200, 90, 98, 112) : Gdiplus::Color(200, 190, 205, 225));
  Gdiplus::Pen edge(&edgeCol, 1.0f);
  g.DrawRectangle(&edge, bounds.X, bounds.Y, bounds.Width - 1.0f, bounds.Height - 1.0f);

  Gdiplus::FontFamily fam(L"Microsoft YaHei UI");
  Gdiplus::Font titleFont(&fam, 13.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font subFont(&fam, 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::Font bodyFont(&fam, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::SolidBrush titleBr(
      g_panelThemeDark ? Gdiplus::Color(255, 228, 232, 240) : Gdiplus::Color(255, 35, 60, 100));
  Gdiplus::SolidBrush subBr(g_panelThemeDark ? Gdiplus::Color(210, 165, 172, 188) : Gdiplus::Color(210, 95, 110, 135));
  Gdiplus::SolidBrush bodyBr(g_panelThemeDark ? Gdiplus::Color(255, 210, 214, 222) : Gdiplus::Color(255, 45, 55, 75));
  Gdiplus::StringFormat fmt{};
  fmt.SetAlignment(Gdiplus::StringAlignmentNear);
  fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);

  const float chipW = 52.0f;
  const float chipH = 18.0f;
  const bool showChip = static_cast<float>(w) >= chipW + 100.0f;
  const float reserveRight = showChip ? (chipW + 16.0f) : 12.0f;
  const float textW = std::max(40.0f, static_cast<float>(w) - 24.0f - reserveRight);
  g.DrawString(L"图层属性", -1, &titleFont, Gdiplus::RectF(x0 + 12.0f, y0 + 10.0f, textW, 22.0f), &fmt, &titleBr);
  g.DrawString(L"Dock · 选中项", -1, &subFont, Gdiplus::RectF(x0 + 12.0f, y0 + 30.0f, textW, 18.0f), &fmt, &subBr);

  if (showChip) {
    const float chipX = x0 + static_cast<float>(w) - chipW - 12.0f;
    const float chipY = y0 + 10.0f;
    Gdiplus::GraphicsPath chip;
    const float rr = 4.0f;
    chip.AddArc(chipX + chipW - 2.0f * rr, chipY, 2.0f * rr, 2.0f * rr, 270, 90);
    chip.AddLine(chipX + chipW, chipY + rr, chipX + chipW, chipY + chipH - rr);
    chip.AddArc(chipX + chipW - 2.0f * rr, chipY + chipH - 2.0f * rr, 2.0f * rr, 2.0f * rr, 0, 90);
    chip.AddLine(chipX + chipW - rr, chipY + chipH, chipX + rr, chipY + chipH);
    chip.AddArc(chipX, chipY + chipH - 2.0f * rr, 2.0f * rr, 2.0f * rr, 90, 90);
    chip.AddLine(chipX, chipY + chipH - rr, chipX, chipY + rr);
    chip.AddArc(chipX, chipY, 2.0f * rr, 2.0f * rr, 180, 90);
    chip.CloseFigure();
    Gdiplus::SolidBrush chipBg(g_panelThemeDark ? Gdiplus::Color(200, 70, 78, 92) : Gdiplus::Color(200, 255, 245, 250));
    Gdiplus::SolidBrush chipFg(
        g_panelThemeDark ? Gdiplus::Color(255, 120, 220, 255) : Gdiplus::Color(255, 0, 120, 110));
    g.FillPath(&chipBg, &chip);
    Gdiplus::StringFormat cfmt{};
    cfmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    cfmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    g.DrawString(L"右", -1, &subFont, Gdiplus::RectF(chipX, chipY, chipW, chipH), &cfmt, &chipFg);
  }

  const float cardX = x0 + 12.0f;
  const float cardY = y0 + 54.0f;
  const float cardW = static_cast<float>(w) - 24.0f;
  const float cardH = static_cast<float>(h) - 66.0f;
  if (cardW > 20.0f && cardH > 40.0f) {
    Gdiplus::GraphicsPath card;
    const float cr = 10.0f;
    card.AddArc(cardX + cardW - 2.0f * cr, cardY, 2.0f * cr, 2.0f * cr, 270, 90);
    card.AddLine(cardX + cardW, cardY + cr, cardX + cardW, cardY + cardH - cr);
    card.AddArc(cardX + cardW - 2.0f * cr, cardY + cardH - 2.0f * cr, 2.0f * cr, 2.0f * cr, 0, 90);
    card.AddLine(cardX + cardW - cr, cardY + cardH, cardX + cr, cardY + cardH);
    card.AddArc(cardX, cardY + cardH - 2.0f * cr, 2.0f * cr, 2.0f * cr, 90, 90);
    card.AddLine(cardX, cardY + cardH - cr, cardX, cardY + cr);
    card.AddArc(cardX, cardY, 2.0f * cr, 2.0f * cr, 180, 90);
    card.CloseFigure();
    Gdiplus::SolidBrush cardFill(
        g_panelThemeDark ? Gdiplus::Color(245, 48, 52, 60) : Gdiplus::Color(245, 255, 255, 255));
    Gdiplus::SolidBrush cardSh(g_panelThemeDark ? Gdiplus::Color(100, 100, 140, 175) : Gdiplus::Color(80, 200, 210, 230));
    Gdiplus::Pen cardPen(&cardSh, 1.0f);
    g.FillPath(&cardFill, &card);
    g.DrawPath(&cardPen, &card);

    Gdiplus::Font nameFont(&fam, 12.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush nameBr(g_panelThemeDark ? Gdiplus::Color(255, 225, 230, 240) : Gdiplus::Color(255, 25, 50, 85));
    const wchar_t* nm = nameLine && nameLine[0] ? nameLine : L"—";
    g.DrawString(nm, -1, &nameFont, Gdiplus::RectF(cardX + 14.0f, cardY + 12.0f, cardW - 28.0f, 24.0f), &fmt,
                 &nameBr);

    Gdiplus::StringFormat bodyFmt;
    bodyFmt.SetAlignment(Gdiplus::StringAlignmentNear);
    bodyFmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
    bodyFmt.SetTrimming(Gdiplus::StringTrimmingEllipsisWord);
    bodyFmt.SetFormatFlags(Gdiplus::StringFormatFlagsLineLimit);
    const wchar_t* bd = body && body[0] ? body : L"";
    g.DrawString(bd, -1, &bodyFont, Gdiplus::RectF(cardX + 14.0f, cardY + 42.0f, cardW - 28.0f, cardH - 56.0f),
                 &bodyFmt, &bodyBr);
  }
}

namespace {

void FillRoundRectPath(Gdiplus::GraphicsPath* path, float x, float y, float rw, float rh, float cornerR) {
  const float cr = std::min(cornerR, std::min(rw, rh) * 0.5f);
  path->AddArc(x + rw - 2.0f * cr, y, 2.0f * cr, 2.0f * cr, 270, 90);
  path->AddLine(x + rw, y + cr, x + rw, y + rh - cr);
  path->AddArc(x + rw - 2.0f * cr, y + rh - 2.0f * cr, 2.0f * cr, 2.0f * cr, 0, 90);
  path->AddLine(x + rw - cr, y + rh, x + cr, y + rh);
  path->AddArc(x, y + rh - 2.0f * cr, 2.0f * cr, 2.0f * cr, 90, 90);
  path->AddLine(x, y + rh - cr, x, y + cr);
  path->AddArc(x, y, 2.0f * cr, 2.0f * cr, 180, 90);
  path->CloseFigure();
}

void PaintPropsSectionCard(Gdiplus::Graphics& g, const RECT& rc, const Gdiplus::Color& fill, const Gdiplus::Color& stroke) {
  const float x = static_cast<float>(rc.left);
  const float y = static_cast<float>(rc.top);
  const float rw = static_cast<float>(rc.right - rc.left);
  const float rh = static_cast<float>(rc.bottom - rc.top);
  if (rw < 12.0f || rh < 12.0f) {
    return;
  }
  Gdiplus::GraphicsPath card;
  FillRoundRectPath(&card, x, y, rw, rh, 9.0f);
  Gdiplus::SolidBrush bFill(fill);
  Gdiplus::Pen pen(stroke, 1.0f);
  g.FillPath(&bFill, &card);
  g.DrawPath(&pen, &card);
}

/** 卡片顶部：左侧强调条 + 主标题 + 灰色副标题（与 main LayoutPropsPane 预留区对齐）。 */
void DrawPropsCardHeader(Gdiplus::Graphics& g, const RECT& cardRc, const wchar_t* title, const wchar_t* subtitle,
                         const Gdiplus::Color& accent) {
  if (!title || !title[0]) {
    return;
  }
  const float x = static_cast<float>(cardRc.left);
  const float y = static_cast<float>(cardRc.top);
  const float w = static_cast<float>(cardRc.right - cardRc.left);
  const float headerH = 28.0f;
  if (w < 24.0f) {
    return;
  }
  Gdiplus::SolidBrush bar(accent);
  g.FillRectangle(&bar, x + 3.0f, y + 5.0f, 4.0f, headerH - 10.0f);

  Gdiplus::FontFamily fam(L"Microsoft YaHei UI");
  Gdiplus::Font titleF(&fam, 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font subF(&fam, 9.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::SolidBrush titleBr(
      g_panelThemeDark ? Gdiplus::Color(255, 220, 225, 235) : Gdiplus::Color(255, 32, 52, 92));
  Gdiplus::SolidBrush subBr(
      g_panelThemeDark ? Gdiplus::Color(230, 165, 172, 188) : Gdiplus::Color(230, 105, 115, 135));
  Gdiplus::StringFormat fmt{};
  fmt.SetAlignment(Gdiplus::StringAlignmentNear);
  fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
  fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

  g.DrawString(title, -1, &titleF, Gdiplus::RectF(x + 14.0f, y + 5.0f, w - 20.0f, 16.0f), &fmt, &titleBr);
  if (subtitle && subtitle[0]) {
    g.DrawString(subtitle, -1, &subF, Gdiplus::RectF(x + 14.0f, y + 17.0f, w - 20.0f, 14.0f), &fmt, &subBr);
  }
}

}  // namespace

void UiPaintLayerPropsDockFrame(HDC hdc, const RECT& rc, const RECT* driverCard, const RECT* sourceCard,
                                const wchar_t* layerSubtitleLine) {
  Gdiplus::Graphics g(hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;
  if (w <= 0 || h <= 0) {
    return;
  }
  const float x0 = static_cast<float>(rc.left);
  const float y0 = static_cast<float>(rc.top);
  Gdiplus::RectF bounds(x0, y0, static_cast<Gdiplus::REAL>(w), static_cast<Gdiplus::REAL>(h));
  if (g_panelThemeDark) {
    Gdiplus::LinearGradientBrush bgBrush(Gdiplus::PointF(x0, y0), Gdiplus::PointF(x0, y0 + static_cast<float>(h)),
                                         Gdiplus::Color(255, 50, 54, 62), Gdiplus::Color(255, 38, 41, 49));
    g.FillRectangle(&bgBrush, bounds);
  } else {
    Gdiplus::LinearGradientBrush bgBrush(Gdiplus::PointF(x0, y0), Gdiplus::PointF(x0, y0 + static_cast<float>(h)),
                                         Gdiplus::Color(255, 248, 251, 255), Gdiplus::Color(255, 236, 252, 255));
    g.FillRectangle(&bgBrush, bounds);
  }

  Gdiplus::SolidBrush edgeCol(g_panelThemeDark ? Gdiplus::Color(200, 88, 96, 110) : Gdiplus::Color(200, 190, 205, 225));
  Gdiplus::Pen edge(&edgeCol, 1.0f);
  g.DrawRectangle(&edge, bounds.X, bounds.Y, bounds.Width - 1.0f, bounds.Height - 1.0f);

  Gdiplus::FontFamily fam(L"Microsoft YaHei UI");
  Gdiplus::Font titleFont(&fam, 13.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font subFont(&fam, 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::SolidBrush titleBr(
      g_panelThemeDark ? Gdiplus::Color(255, 228, 232, 240) : Gdiplus::Color(255, 35, 60, 100));
  Gdiplus::SolidBrush subBr(g_panelThemeDark ? Gdiplus::Color(210, 165, 172, 188) : Gdiplus::Color(210, 95, 110, 135));
  Gdiplus::StringFormat fmt{};
  fmt.SetAlignment(Gdiplus::StringAlignmentNear);
  fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);

  const float chipW = 52.0f;
  const float chipH = 18.0f;
  const bool showChip = static_cast<float>(w) >= chipW + 100.0f;
  const float reserveRight = showChip ? (chipW + 16.0f) : 12.0f;
  const float textW = std::max(40.0f, static_cast<float>(w) - 24.0f - reserveRight);
  g.DrawString(L"图层属性", -1, &titleFont, Gdiplus::RectF(x0 + 12.0f, y0 + 10.0f, textW, 22.0f), &fmt, &titleBr);
  const wchar_t* sub =
      (layerSubtitleLine && layerSubtitleLine[0]) ? layerSubtitleLine : L"Dock · 选中项";
  g.DrawString(sub, -1, &subFont, Gdiplus::RectF(x0 + 12.0f, y0 + 30.0f, textW, 36.0f), &fmt, &subBr);

  if (showChip) {
    const float chipX = x0 + static_cast<float>(w) - chipW - 12.0f;
    const float chipY = y0 + 10.0f;
    Gdiplus::GraphicsPath chip;
    const float rr = 4.0f;
    chip.AddArc(chipX + chipW - 2.0f * rr, chipY, 2.0f * rr, 2.0f * rr, 270, 90);
    chip.AddLine(chipX + chipW, chipY + rr, chipX + chipW, chipY + chipH - rr);
    chip.AddArc(chipX + chipW - 2.0f * rr, chipY + chipH - 2.0f * rr, 2.0f * rr, 2.0f * rr, 0, 90);
    chip.AddLine(chipX + chipW - rr, chipY + chipH, chipX + rr, chipY + chipH);
    chip.AddArc(chipX, chipY + chipH - 2.0f * rr, 2.0f * rr, 2.0f * rr, 90, 90);
    chip.AddLine(chipX, chipY + chipH - rr, chipX, chipY + rr);
    chip.AddArc(chipX, chipY, 2.0f * rr, 2.0f * rr, 180, 90);
    chip.CloseFigure();
    Gdiplus::SolidBrush chipBg(g_panelThemeDark ? Gdiplus::Color(200, 70, 78, 92) : Gdiplus::Color(200, 255, 245, 250));
    Gdiplus::SolidBrush chipFg(
        g_panelThemeDark ? Gdiplus::Color(255, 120, 220, 255) : Gdiplus::Color(255, 0, 120, 110));
    g.FillPath(&chipBg, &chip);
    Gdiplus::StringFormat cfmt{};
    cfmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    cfmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    g.DrawString(L"右", -1, &subFont, Gdiplus::RectF(chipX, chipY, chipW, chipH), &cfmt, &chipFg);
  }

  const bool dual = driverCard && sourceCard && driverCard->right > driverCard->left + 8 &&
                    sourceCard->right > sourceCard->left + 8;
  if (dual) {
    if (g_panelThemeDark) {
      PaintPropsSectionCard(g, *driverCard, Gdiplus::Color(255, 44, 48, 56), Gdiplus::Color(110, 100, 150, 190));
      DrawPropsCardHeader(g, *driverCard, L"驱动属性", L"驱动类型、金字塔与数据集访问",
                          Gdiplus::Color(255, 80, 200, 255));
      PaintPropsSectionCard(g, *sourceCard, Gdiplus::Color(255, 46, 50, 58), Gdiplus::Color(110, 110, 160, 185));
      DrawPropsCardHeader(g, *sourceCard, L"数据源属性", L"路径、连接串与 GDAL 元数据",
                          Gdiplus::Color(255, 100, 220, 200));
    } else {
      PaintPropsSectionCard(g, *driverCard, Gdiplus::Color(255, 252, 254, 255), Gdiplus::Color(100, 175, 205, 235));
      DrawPropsCardHeader(g, *driverCard, L"驱动属性", L"驱动类型、金字塔与数据集访问",
                          Gdiplus::Color(255, 0, 130, 175));
      PaintPropsSectionCard(g, *sourceCard, Gdiplus::Color(255, 254, 255, 252), Gdiplus::Color(100, 195, 210, 220));
      DrawPropsCardHeader(g, *sourceCard, L"数据源属性", L"路径、连接串与 GDAL 元数据",
                          Gdiplus::Color(255, 0, 150, 130));
    }
  } else {
    const float cardX = x0 + 12.0f;
    const float cardY = y0 + 54.0f;
    const float cardW = static_cast<float>(w) - 24.0f;
    const float cardH = static_cast<float>(h) - 66.0f;
    if (cardW > 20.0f && cardH > 40.0f) {
      RECT fallback{};
      fallback.left = static_cast<LONG>(cardX);
      fallback.top = static_cast<LONG>(cardY);
      fallback.right = static_cast<LONG>(cardX + cardW);
      fallback.bottom = static_cast<LONG>(cardY + cardH);
      if (g_panelThemeDark) {
        PaintPropsSectionCard(g, fallback, Gdiplus::Color(245, 46, 50, 58), Gdiplus::Color(90, 105, 155, 185));
      } else {
        PaintPropsSectionCard(g, fallback, Gdiplus::Color(245, 255, 255, 255), Gdiplus::Color(80, 200, 210, 230));
      }
    }
  }
}

void UiPaintMapHintOverlay(HDC hdc, const RECT& client, const wchar_t* hint) {
  if (!hint || !*hint) {
    return;
  }
  Gdiplus::Graphics g(hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
  Gdiplus::FontFamily fam(L"Microsoft YaHei UI");
  Gdiplus::Font font(&fam, 11.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

  Gdiplus::StringFormat fmt;
  fmt.SetAlignment(Gdiplus::StringAlignmentNear);
  Gdiplus::RectF measure(0, 0, 1200, 400);
  Gdiplus::RectF bbox;
  g.MeasureString(hint, -1, &font, measure, &fmt, &bbox);

  const float pad = 10.0f;
  const float margin = 12.0f;
  const float boxW = bbox.Width + pad * 2.0f;
  const float boxH = bbox.Height + pad * 2.0f;
  const float x = static_cast<float>(client.right) - margin - boxW;
  const float y = static_cast<float>(client.bottom) - margin - boxH;

  Gdiplus::SolidBrush bg(Gdiplus::Color(215, 252, 253, 255));
  Gdiplus::SolidBrush edge(Gdiplus::Color(110, 100, 140, 190));
  Gdiplus::Pen pen(&edge, 1.0f);
  g.FillRectangle(&bg, x, y, boxW, boxH);
  g.DrawRectangle(&pen, x, y, boxW, boxH);

  Gdiplus::SolidBrush fg(Gdiplus::Color(255, 40, 55, 75));
  Gdiplus::RectF textRect(x + pad, y + pad, bbox.Width + 4.0f, bbox.Height + 4.0f);
  g.DrawString(hint, -1, &font, textRect, &fmt, &fg);
}

namespace {

int GetPngEncoderClsid(CLSID* clsid) {
  UINT n = 0;
  UINT size = 0;
  Gdiplus::GetImageEncodersSize(&n, &size);
  if (n == 0 || size == 0) {
    return -1;
  }
  std::vector<Gdiplus::ImageCodecInfo> buf(n);
  if (Gdiplus::GetImageEncoders(n, size, buf.data()) != Gdiplus::Ok) {
    return -1;
  }  // size 为 GetImageEncodersSize 返回的字节数，与 buf 容量一致
  for (UINT i = 0; i < n; ++i) {
    if (wcscmp(buf[i].MimeType, L"image/png") == 0) {
      *clsid = buf[i].Clsid;
      return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace

bool UiSaveHbitmapToPngFile(HBITMAP hbmp, const wchar_t* path) {
  if (!hbmp || !path || !path[0]) {
    return false;
  }
  Gdiplus::Bitmap bmp(hbmp, nullptr);
  CLSID png{};
  if (GetPngEncoderClsid(&png) < 0) {
    return false;
  }
  return bmp.Save(path, &png, nullptr) == Gdiplus::Ok;
}

bool UiSaveBgraTopDownToPngFile(const uint8_t* pixels, int width, int height, const wchar_t* path) {
  if (!pixels || width <= 0 || height <= 0 || !path || !path[0]) {
    return false;
  }
  const INT stride = width * 4;
  Gdiplus::Bitmap bmp(width, height, stride, PixelFormat32bppARGB, const_cast<BYTE*>(pixels));
  CLSID png{};
  if (GetPngEncoderClsid(&png) < 0) {
    return false;
  }
  return bmp.Save(path, &png, nullptr) == Gdiplus::Ok;
}

void UiPaintMapCenterHint(HDC hdc, const RECT& client, const wchar_t* text) {
  if (!text || !*text) {
    return;
  }
  Gdiplus::Graphics g(hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
  Gdiplus::FontFamily fam(L"Microsoft YaHei UI");
  Gdiplus::Font font(&fam, 12.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

  Gdiplus::StringFormat fmt;
  fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
  fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  const float cw = static_cast<float>(client.right - client.left);
  const float ch = static_cast<float>(client.bottom - client.top);
  if (cw <= 1.0f || ch <= 1.0f) {
    return;
  }
  Gdiplus::RectF measure(0, 0, cw - 80.0f, 800.0f);
  Gdiplus::RectF bbox;
  g.MeasureString(text, -1, &font, measure, &fmt, &bbox);

  const float pad = 18.0f;
  const float boxW = std::min(bbox.Width + pad * 2.0f, cw - 48.0f);
  const float boxH = bbox.Height + pad * 2.0f;
  const float x = (cw - boxW) * 0.5f;
  const float y = (ch - boxH) * 0.5f;

  Gdiplus::GraphicsPath path;
  const float r = 8.0f;
  path.AddLine(x + r, y, x + boxW - r, y);
  path.AddArc(x + boxW - 2.0f * r, y, 2.0f * r, 2.0f * r, 270, 90);
  path.AddLine(x + boxW, y + r, x + boxW, y + boxH - r);
  path.AddArc(x + boxW - 2.0f * r, y + boxH - 2.0f * r, 2.0f * r, 2.0f * r, 0, 90);
  path.AddLine(x + boxW - r, y + boxH, x + r, y + boxH);
  path.AddArc(x, y + boxH - 2.0f * r, 2.0f * r, 2.0f * r, 90, 90);
  path.AddLine(x, y + boxH - r, x, y + r);
  path.AddArc(x, y, 2.0f * r, 2.0f * r, 180, 90);
  path.CloseFigure();

  Gdiplus::PathGradientBrush pbrush(&path);
  pbrush.SetCenterColor(Gdiplus::Color(245, 255, 255, 255));
  const Gdiplus::Color edgeCol(255, 230, 236, 248);
  Gdiplus::Color surround[] = {edgeCol};
  int scount = 1;
  pbrush.SetSurroundColors(surround, &scount);

  Gdiplus::SolidBrush border(Gdiplus::Color(140, 110, 140, 200));
  Gdiplus::Pen pen(&border, 1.25f);
  g.FillPath(&pbrush, &path);
  g.DrawPath(&pen, &path);

  Gdiplus::SolidBrush fg(Gdiplus::Color(255, 45, 60, 90));
  Gdiplus::RectF textRect(x + pad, y + pad, boxW - pad * 2.0f, boxH - pad * 2.0f);
  g.DrawString(text, -1, &font, textRect, &fmt, &fg);
}
