#include "map_engine/scene_node.h"

SceneNode::SceneNode() = default;
SceneNode::~SceneNode() = default;

void SceneNode::addChild(std::unique_ptr<SceneNode> child) {
  if (child) {
    children_.push_back(std::move(child));
  }
}

void SceneNode::setShape(std::unique_ptr<Shape> shape) {
  shape_ = std::move(shape);
}
