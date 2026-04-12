#pragma once

#include "map_engine/scene_node.h"

#include <memory>
#include <vector>

/**
 * 场景图容器：持有若干根 ``SceneNode``，供 ``Renderer::cull`` 遍历整树。
 */
class SceneGraph {
 public:
  SceneGraph();
  ~SceneGraph();

  SceneGraph(const SceneGraph&) = delete;
  SceneGraph& operator=(const SceneGraph&) = delete;
  SceneGraph(SceneGraph&&) = default;
  SceneGraph& operator=(SceneGraph&&) = default;

  void addRoot(std::unique_ptr<SceneNode> node);
  std::vector<std::unique_ptr<SceneNode>>& roots() { return roots_; }
  const std::vector<std::unique_ptr<SceneNode>>& roots() const { return roots_; }

 private:
  std::vector<std::unique_ptr<SceneNode>> roots_;
};
