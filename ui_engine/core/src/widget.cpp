#include "ui_engine/widget.h"

#include <algorithm>
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

void Widget::removeChild(Widget* child) {
  if (!child || child->parent_ != this) {
    return;
  }
  const auto it =
      std::find_if(children_.begin(), children_.end(), [child](const std::unique_ptr<Widget>& w) { return w.get() == child; });
  if (it == children_.end()) {
    return;
  }
  (*it)->parent_ = nullptr;
  children_.erase(it);
}

void Widget::setVisible(bool on) {
  visible_ = on;
}

}  // namespace agis::ui
