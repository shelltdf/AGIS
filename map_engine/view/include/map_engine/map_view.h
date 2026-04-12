#pragma once

#include "map_engine/message_queue.h"
#include "map_engine/native_window.h"
#include "map_engine/renderer.h"

#include <memory>

class SceneNode;

/**
 * 视口视图：非拥有指针指向当前场景附着节点；拥有本地窗口包装与渲染器。
 * 系统事件由 ``NativeWindow::pumpMessages`` 写入本视图持有的 ``MessageQueue``，供跨平台逻辑统一处理；
 * 渲染经 ``Renderer::cull`` / ``draw`` 访问 ``SceneGraph`` 并下发到具体图形 API。
 */
class MapView {
 public:
  MapView();
  ~MapView();

  MapView(const MapView&) = delete;
  MapView& operator=(const MapView&) = delete;
  MapView(MapView&&) = default;
  MapView& operator=(MapView&&) = default;

  /** 非拥有：节点通常由 ``SceneGraph`` 持有，此处多为根或子树根。 */
  void setSceneRoot(SceneNode* root);
  SceneNode* sceneRoot() const { return sceneRoot_; }

  void setNativeWindow(std::unique_ptr<NativeWindow> window);
  NativeWindow* nativeWindow() { return nativeWindow_.get(); }
  const NativeWindow* nativeWindow() const { return nativeWindow_.get(); }

  void setRenderer(std::unique_ptr<Renderer> renderer);
  Renderer* renderer() { return renderer_.get(); }
  const Renderer* renderer() const { return renderer_.get(); }

  MessageQueue& messages() { return messageQueue_; }
  const MessageQueue& messages() const { return messageQueue_; }

  /** 若已设置 ``NativeWindow``，将当前平台消息泵入 ``messages()``。 */
  void pumpNativeMessages();

 private:
  SceneNode* sceneRoot_{nullptr};
  std::unique_ptr<NativeWindow> nativeWindow_;
  std::unique_ptr<Renderer> renderer_;
  MessageQueue messageQueue_;
};
