#include "ui_engine/widget.h"

#include <utility>

namespace agis::ui {

Widget::~Widget() = default;

void Widget::setGeometry(const Rect& r) {
  geometry_ = r;
}

void Widget::addChild(std::unique_ptr<Widget> child) {
  if (!child) {
    return;
  }
  Widget* raw = child.get();
  raw->parent_ = this;
  children_.push_back(std::move(child));
}

void Widget::setVisible(bool on) {
  visible_ = on;
}

}  // namespace agis::ui
