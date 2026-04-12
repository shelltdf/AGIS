#pragma once

#include "map_engine/export.h"

class Map;
class SceneGraph;
class SceneNode;

/**
 * 地图调度器：将 ``Map`` 文档（图层列表、顺序等）解释为 ``SceneGraph`` 中的节点树，
 * 供 ``Renderer::cull`` / ``draw`` 与后续按图层绑定 ``Shape`` / 网格等资源。
 *
 * 当前策略：单根节点下按 ``map.layers`` 顺序挂一层一子节点（占位，尚无 ``Shape``）。
 */
class AGIS_MAP_ENGINE_API MapScheduler {
 public:
  /**
   * 清空 ``graph`` 的根列表并按 ``map`` 重建：返回新的场景根指针（由 ``graph`` 拥有）；
   * 若 ``map`` 无图层则仍保留空子节点列表的单根。
   */
  SceneNode* rebuildFromMap(Map& map, SceneGraph& graph);
};
