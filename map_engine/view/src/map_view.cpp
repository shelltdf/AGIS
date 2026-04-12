#include "map_engine/map_view.h"

MapView::MapView() = default;
MapView::~MapView() = default;

void MapView::setSceneRoot(SceneNode* root) {
  sceneRoot_ = root;
}

void MapView::setNativeWindow(std::unique_ptr<NativeWindow> window) {
  nativeWindow_ = std::move(window);
}

void MapView::setRenderer(std::unique_ptr<Renderer> renderer) {
  renderer_ = std::move(renderer);
}

void MapView::pumpNativeMessages() {
  if (nativeWindow_) {
    nativeWindow_->pumpMessages(messageQueue_);
  }
}
