#pragma once

/**
 * 场景图（功能组占位）：后续可承载节点树、局部/世界变换、可见性裁剪与拾取遍历，
 * 与 ``layer`` / ``render`` 等模块对接。
 */
class SceneGraph {
 public:
  SceneGraph();
  ~SceneGraph();

  SceneGraph(const SceneGraph&) = delete;
  SceneGraph& operator=(const SceneGraph&) = delete;
  SceneGraph(SceneGraph&&) = default;
  SceneGraph& operator=(SceneGraph&&) = default;
};
