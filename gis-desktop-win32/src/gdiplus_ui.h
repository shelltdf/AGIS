#pragma once

#include <cstdint>

#include <windows.h>

void UiGdiplusInit();
void UiGdiplusShutdown();

/** 左侧「图层」Dock 顶区渐变、标题与副标题（GDI+）。 */
void UiPaintLayerPanel(HDC hdc, const RECT& rc);

/** 右侧「图层属性」Dock：卡片式信息区（GDI+）。 */
void UiPaintLayerPropsPanel(HDC hdc, const RECT& rc, const wchar_t* nameLine, const wchar_t* body);
/** 仅绘制 Dock 装饰与空卡片区；详细文本由子控件 EDIT 承载。 */
void UiPaintLayerPropsDockFrame(HDC hdc, const RECT& rc);

/** 地图区右下角半透明提示条（GDI+，在 GDI BitBlt 之后绘制到同一 DC）。 */
void UiPaintMapHintOverlay(HDC hdc, const RECT& client, const wchar_t* hint);

/** 地图区中央提示卡片（如无 GDAL 构建等），绘制在离屏缓冲上。 */
void UiPaintMapCenterHint(HDC hdc, const RECT& client, const wchar_t* text);

/** 将 DIB 位图保存为 PNG（GDI+）。失败返回 false。 */
bool UiSaveHbitmapToPngFile(HBITMAP hbmp, const wchar_t* path);

/** 将自上而下 BGRA8 像素保存为 PNG（与 CreateDIBSection BI_RGB 32 位、首行为顶行一致）。 */
bool UiSaveBgraTopDownToPngFile(const uint8_t* pixels, int width, int height, const wchar_t* path);
