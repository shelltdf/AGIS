#pragma once

#include <windows.h>

namespace UiLayout {

struct Box {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

Box FromRect(const RECT& rc);
RECT ToRect(const Box& b);
Box Inset(const Box& b, int all);
Box Inset(const Box& b, int l, int t, int r, int bt);

// Dock helpers: take space from the current box and shrink it.
Box DockTop(Box& b, int h);
Box DockBottom(Box& b, int h);
Box DockLeft(Box& b, int w);
Box DockRight(Box& b, int w);

// Split one row into 3 columns with equal gaps.
void Split3Cols(const Box& row, int gap, Box* left, Box* mid, Box* right);

}  // namespace UiLayout

