#pragma once

#include <cstdint>

namespace agis::ui {

/** 整数像素坐标下的轴对齐矩形（逻辑客户区，左上为原点、向右向下为正）。 */
struct Point {
  int x = 0;
  int y = 0;
};

struct Size {
  int w = 0;
  int h = 0;
};

struct Rect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

/** 绘制上下文：由各平台后端填入原生绘制句柄（Win32: HDC；XCB/Cairo；macOS: CGContextRef 等）。 */
struct PaintContext {
  void* nativeDevice = nullptr;
  Rect clip{};
};

}  // namespace agis::ui
