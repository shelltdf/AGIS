#include "map_engine/renderer.h"

#include "map_engine/scene_graph.h"

void Renderer2D::cull(SceneGraph& graph, RenderStructure& out) {
  (void)graph;
  out.batchCount = 0;
  out.itemIds.clear();
}

void Renderer2D::draw(const RenderStructure& rs) {
  (void)rs;
}

void Renderer3D::cull(SceneGraph& graph, RenderStructure& out) {
  (void)graph;
  out.batchCount = 0;
  out.itemIds.clear();
}

void Renderer3D::draw(const RenderStructure& rs) {
  (void)rs;
}
