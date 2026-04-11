#include "utils/ui_layout.h"

#include <algorithm>

namespace UiLayout {

Box FromRect(const RECT& rc) {
  const int w = static_cast<int>(rc.right - rc.left);
  const int h = static_cast<int>(rc.bottom - rc.top);
  return Box{static_cast<int>(rc.left), static_cast<int>(rc.top), (std::max)(0, w), (std::max)(0, h)};
}

RECT ToRect(const Box& b) {
  RECT rc{};
  rc.left = b.x;
  rc.top = b.y;
  rc.right = b.x + (std::max)(0, b.w);
  rc.bottom = b.y + (std::max)(0, b.h);
  return rc;
}

Box Inset(const Box& b, int all) {
  return Inset(b, all, all, all, all);
}

Box Inset(const Box& b, int l, int t, int r, int bt) {
  Box out{};
  out.x = b.x + l;
  out.y = b.y + t;
  out.w = (std::max)(0, b.w - l - r);
  out.h = (std::max)(0, b.h - t - bt);
  return out;
}

Box DockTop(Box& b, int h) {
  const int hh = (std::clamp)(h, 0, b.h);
  Box out{b.x, b.y, b.w, hh};
  b.y += hh;
  b.h -= hh;
  return out;
}

Box DockBottom(Box& b, int h) {
  const int hh = (std::clamp)(h, 0, b.h);
  Box out{b.x, b.y + b.h - hh, b.w, hh};
  b.h -= hh;
  return out;
}

Box DockLeft(Box& b, int w) {
  const int ww = (std::clamp)(w, 0, b.w);
  Box out{b.x, b.y, ww, b.h};
  b.x += ww;
  b.w -= ww;
  return out;
}

Box DockRight(Box& b, int w) {
  const int ww = (std::clamp)(w, 0, b.w);
  Box out{b.x + b.w - ww, b.y, ww, b.h};
  b.w -= ww;
  return out;
}

void Split3Cols(const Box& row, int gap, Box* left, Box* mid, Box* right) {
  if (!left || !mid || !right) return;
  const int g = (std::max)(0, gap);
  const int colW = (std::max)(0, (row.w - g * 2) / 3);
  *left = Box{row.x, row.y, colW, row.h};
  *mid = Box{row.x + colW + g, row.y, colW, row.h};
  *right = Box{row.x + (colW + g) * 2, row.y, (std::max)(0, row.w - (colW + g) * 2), row.h};
}

}  // namespace UiLayout
