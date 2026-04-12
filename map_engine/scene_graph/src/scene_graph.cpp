#include "map_engine/scene_graph.h"

SceneGraph::SceneGraph() = default;
SceneGraph::~SceneGraph() = default;

void SceneGraph::addRoot(std::unique_ptr<SceneNode> node) {
  if (node) {
    roots_.push_back(std::move(node));
  }
}
