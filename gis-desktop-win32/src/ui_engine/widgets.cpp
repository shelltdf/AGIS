#include "ui_engine/widgets.h"

#include <utility>

namespace agis::ui {

void Window::setTitle(std::wstring title) {
  title_ = std::move(title);
}

void Window::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void Frame::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void Label::setText(std::wstring t) {
  text_ = std::move(t);
}

void Label::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void PushButton::setText(std::wstring t) {
  text_ = std::move(t);
}

void PushButton::click() {
  if (onClicked_) {
    onClicked_();
  }
}

void PushButton::setOnClicked(std::function<void()> fn) {
  onClicked_ = std::move(fn);
}

void PushButton::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void LineEdit::setText(std::wstring t) {
  text_ = std::move(t);
}

void LineEdit::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void ScrollArea::setContentWidget(std::unique_ptr<Widget> w) {
  if (content_) {
    content_->parent_ = nullptr;
  }
  content_ = std::move(w);
  if (content_) {
    content_->parent_ = this;
  }
}

void ScrollArea::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void MainFrame::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void MenuBarWidget::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void ToolBarWidget::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void StatusBarWidget::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void DockArea::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void DockButtonStrip::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void DockView::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void DockPanel::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void LayerDockPanel::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void PropsDockPanel::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void Splitter::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void Canvas2D::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void MapCanvas2D::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void ShortcutHelpPanel::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void LayerVisibilityPanel::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void MapZoomBar::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void MapHintOverlay::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void MapCenterHintOverlay::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void DialogWindow::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void LayerDriverDialog::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void LogWindow::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void PopupMenu::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void LayerListContextMenu::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void MapContextMenu::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

void LogPanel::paintEvent(PaintContext& ctx) {
  (void)ctx;
}

}  // namespace agis::ui
