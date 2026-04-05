#pragma once

#include "ui/ui_types.h"

#include <memory>
#include <string>
#include <vector>

namespace agis::ui {

class ScrollArea;

/**
 * 最小 UI 单元：轴对齐矩形区域 + 父子树（类似 QWidget 几何与层次，不含信号槽）。
 * 绘制与输入由各平台后端在 realize 后对接原生控件或自绘表面。
 * 层次关系：仅通过 parent 的 addChild(std::unique_ptr<Widget>) 建立所有权与 parent 指针。
 */
class Widget {
 public:
  Widget() = default;
  virtual ~Widget();

  Widget(const Widget&) = delete;
  Widget& operator=(const Widget&) = delete;

  Widget* parent() const { return parent_; }

  Rect geometry() const { return geometry_; }
  void setGeometry(const Rect& r);

  /** 子控件顺序即 z-order 初值（具体叠放以后端为准）。 */
  const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }

  void addChild(std::unique_ptr<Widget> child);

  bool visible() const { return visible_; }
  void setVisible(bool on);

  /** 纯绘制：由平台在合适时机调用（或合成到缓冲）。 */
  virtual void paintEvent(PaintContext& ctx) = 0;

 protected:
  friend class ScrollArea;

  Widget* parent_{nullptr};
  Rect geometry_{};
  bool visible_{true};
  std::vector<std::unique_ptr<Widget>> children_;
};

}  // namespace agis::ui
