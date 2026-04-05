#include "gdiplus_ui.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

namespace {

ULONG_PTR g_gdiplusToken = 0;

}  // namespace

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
  Gdiplus::LinearGradientBrush brush(Gdiplus::PointF(x0, y0), Gdiplus::PointF(x0, y0 + static_cast<float>(h)),
                                      Gdiplus::Color(255, 250, 252, 255), Gdiplus::Color(255, 232, 240, 252));
  g.FillRectangle(&brush, bounds);

  Gdiplus::SolidBrush hairline(Gdiplus::Color(200, 180, 195, 220));
  Gdiplus::Pen edge(&hairline, 1.0f);
  g.DrawRectangle(&edge, bounds.X, bounds.Y, bounds.Width - 1.0f, bounds.Height - 1.0f);

  Gdiplus::FontFamily fam(L"Segoe UI");
  Gdiplus::Font titleFont(&fam, 14.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font subFont(&fam, 10.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 30, 55, 95));
  Gdiplus::SolidBrush subBrush(Gdiplus::Color(220, 90, 105, 130));
  Gdiplus::SolidBrush chipBg(Gdiplus::Color(180, 230, 240, 255));
  Gdiplus::SolidBrush chipFg(Gdiplus::Color(255, 55, 95, 140));
  Gdiplus::StringFormat fmt{};
  fmt.SetAlignment(Gdiplus::StringAlignmentNear);
  fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);

  const float chipW = 52.0f;
  const float chipH = 18.0f;
  const bool showChip = static_cast<float>(w) >= chipW + 100.0f;
  const float reserveRight = showChip ? (chipW + 16.0f) : 12.0f;
  const float textW = std::max(40.0f, static_cast<float>(w) - 24.0f - reserveRight);
  g.DrawString(L"图层", -1, &titleFont, Gdiplus::RectF(x0 + 12.0f, y0 + 10.0f, textW, 22.0f), &fmt, &titleBrush);
  g.DrawString(L"Dock · 数据列表", -1, &subFont, Gdiplus::RectF(x0 + 12.0f, y0 + 30.0f, textW, 18.0f), &fmt, &subBrush);

  if (showChip) {
    const float chipX = x0 + static_cast<float>(w) - chipW - 12.0f;
    const float chipY = y0 + 10.0f;
    Gdiplus::GraphicsPath chip;
    const float r = 4.0f;
    chip.AddArc(chipX + chipW - 2.0f * r, chipY, 2.0f * r, 2.0f * r, 270, 90);
    chip.AddLine(chipX + chipW, chipY + r, chipX + chipW, chipY + chipH - r);
    chip.AddArc(chipX + chipW - 2.0f * r, chipY + chipH - 2.0f * r, 2.0f * r, 2.0f * r, 0, 90);
    chip.AddLine(chipX + chipW - r, chipY + chipH, chipX + r, chipY + chipH);
    chip.AddArc(chipX, chipY + chipH - 2.0f * r, 2.0f * r, 2.0f * r, 90, 90);
    chip.AddLine(chipX, chipY + chipH - r, chipX, chipY + r);
    chip.AddArc(chipX, chipY, 2.0f * r, 2.0f * r, 180, 90);
    chip.CloseFigure();
    g.FillPath(&chipBg, &chip);
    Gdiplus::StringFormat cfmt{};
    cfmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    cfmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    g.DrawString(L"左", -1, &subFont, Gdiplus::RectF(chipX, chipY, chipW, chipH), &cfmt, &chipFg);
  }
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
  Gdiplus::LinearGradientBrush bgBrush(Gdiplus::PointF(x0, y0), Gdiplus::PointF(x0, y0 + static_cast<float>(h)),
                                       Gdiplus::Color(255, 248, 251, 255), Gdiplus::Color(255, 236, 252, 255));
  g.FillRectangle(&bgBrush, bounds);

  Gdiplus::SolidBrush edgeCol(Gdiplus::Color(200, 190, 205, 225));
  Gdiplus::Pen edge(&edgeCol, 1.0f);
  g.DrawRectangle(&edge, bounds.X, bounds.Y, bounds.Width - 1.0f, bounds.Height - 1.0f);

  Gdiplus::FontFamily fam(L"Segoe UI");
  Gdiplus::Font titleFont(&fam, 14.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font subFont(&fam, 10.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::Font bodyFont(&fam, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::SolidBrush titleBr(Gdiplus::Color(255, 35, 60, 100));
  Gdiplus::SolidBrush subBr(Gdiplus::Color(210, 95, 110, 135));
  Gdiplus::SolidBrush bodyBr(Gdiplus::Color(255, 45, 55, 75));
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
    Gdiplus::SolidBrush chipBg(Gdiplus::Color(200, 255, 245, 250));
    Gdiplus::SolidBrush chipFg(Gdiplus::Color(255, 0, 120, 110));
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
    Gdiplus::SolidBrush cardFill(Gdiplus::Color(245, 255, 255, 255));
    Gdiplus::SolidBrush cardSh(Gdiplus::Color(80, 200, 210, 230));
    Gdiplus::Pen cardPen(&cardSh, 1.0f);
    g.FillPath(&cardFill, &card);
    g.DrawPath(&cardPen, &card);

    Gdiplus::Font nameFont(&fam, 12.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush nameBr(Gdiplus::Color(255, 25, 50, 85));
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

void UiPaintLayerPropsDockFrame(HDC hdc, const RECT& rc) {
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
  Gdiplus::LinearGradientBrush bgBrush(Gdiplus::PointF(x0, y0), Gdiplus::PointF(x0, y0 + static_cast<float>(h)),
                                       Gdiplus::Color(255, 248, 251, 255), Gdiplus::Color(255, 236, 252, 255));
  g.FillRectangle(&bgBrush, bounds);

  Gdiplus::SolidBrush edgeCol(Gdiplus::Color(200, 190, 205, 225));
  Gdiplus::Pen edge(&edgeCol, 1.0f);
  g.DrawRectangle(&edge, bounds.X, bounds.Y, bounds.Width - 1.0f, bounds.Height - 1.0f);

  Gdiplus::FontFamily fam(L"Segoe UI");
  Gdiplus::Font titleFont(&fam, 14.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font subFont(&fam, 10.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::SolidBrush titleBr(Gdiplus::Color(255, 35, 60, 100));
  Gdiplus::SolidBrush subBr(Gdiplus::Color(210, 95, 110, 135));
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
    Gdiplus::SolidBrush chipBg(Gdiplus::Color(200, 255, 245, 250));
    Gdiplus::SolidBrush chipFg(Gdiplus::Color(255, 0, 120, 110));
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
    Gdiplus::SolidBrush cardFill(Gdiplus::Color(245, 255, 255, 255));
    Gdiplus::SolidBrush cardSh(Gdiplus::Color(80, 200, 210, 230));
    Gdiplus::Pen cardPen(&cardSh, 1.0f);
    g.FillPath(&cardFill, &card);
    g.DrawPath(&cardPen, &card);
  }
}

void UiPaintMapHintOverlay(HDC hdc, const RECT& client, const wchar_t* hint) {
  if (!hint || !*hint) {
    return;
  }
  Gdiplus::Graphics g(hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
  Gdiplus::FontFamily fam(L"Segoe UI");
  Gdiplus::Font font(&fam, 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

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
  Gdiplus::FontFamily fam(L"Segoe UI");
  Gdiplus::Font font(&fam, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

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
