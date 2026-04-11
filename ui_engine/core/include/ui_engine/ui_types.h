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

/**
 * 绘制上下文：由**当前平台**在 `WM_PAINT` / 等价时机构造并传入 `Widget::paintEvent`。
 * - `nativeDevice`：平台原生绘制目标（如 Win32 `HDC`）。
 * - `clip`：本控件在**宿主顶层窗口客户区**中的轴对齐矩形（像素，与 `Widget::geometry()` 链叠加后的绝对位置一致）。
 */
struct PaintContext {
  void* nativeDevice = nullptr;
  Rect clip{};
};

}  // namespace agis::ui
