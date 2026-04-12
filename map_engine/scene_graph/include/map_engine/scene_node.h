#pragma once

#include "map_engine/shape.h"

#include <memory>
#include <vector>

/**
 * 场景图节点：子节点构成树；可选挂接一个 ``Shape``（如 ``Mesh``）。
 */
class SceneNode {
 public:
  SceneNode();
  ~SceneNode();

  SceneNode(const SceneNode&) = delete;
  SceneNode& operator=(const SceneNode&) = delete;
  SceneNode(SceneNode&&) = default;
  SceneNode& operator=(SceneNode&&) = default;

  void addChild(std::unique_ptr<SceneNode> child);
  std::vector<std::unique_ptr<SceneNode>>& children() { return children_; }
  const std::vector<std::unique_ptr<SceneNode>>& children() const { return children_; }

  void setShape(std::unique_ptr<Shape> shape);
  Shape* shape() { return shape_.get(); }
  const Shape* shape() const { return shape_.get(); }

 private:
  std::vector<std::unique_ptr<SceneNode>> children_;
  std::unique_ptr<Shape> shape_;
};
