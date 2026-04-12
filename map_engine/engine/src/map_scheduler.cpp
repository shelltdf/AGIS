#include "map_engine/map_scheduler.h"

#include "map_engine/map.h"
#include "map_engine/scene_graph.h"
#include "map_engine/scene_node.h"

SceneNode* MapScheduler::rebuildFromMap(Map& map, SceneGraph& graph) {
  graph.roots().clear();
  auto root = std::make_unique<SceneNode>();
  SceneNode* const rootPtr = root.get();
  for (const auto& layerPtr : map.layers) {
    (void)layerPtr;
    rootPtr->addChild(std::make_unique<SceneNode>());
  }
  graph.addRoot(std::move(root));
  if (graph.roots().empty()) {
    return nullptr;
  }
  return graph.roots().front().get();
}
