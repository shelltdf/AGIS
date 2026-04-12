#pragma once

#include <cstdint>
#include <vector>

class SceneGraph;

/**
 * Cull 阶段输出的中间结构（占位）：后续可扩展为按材质/深度排序的绘制批次、实例缓冲等。
 */
struct RenderStructure {
  std::uint32_t batchCount{0};
  /** 占位：与场景节点或 GPU 资源的弱引用索引。 */
  std::vector<std::uint32_t> itemIds{};
};

/**
 * 渲染器：对场景图做可见性裁剪（cull），生成 ``RenderStructure``；在 draw 中翻译为具体图形 API。
 * - ``Renderer2D``：面向 GDI、GDI+、D2D 或纯 CPU 光栅化等路径。
 * - ``Renderer3D``：面向三角网格与 GPU 管线（与现有 bgfx/OpenGL/D3D 等对接）。
 */
class Renderer {
 public:
  virtual ~Renderer() = default;
  virtual void cull(SceneGraph& graph, RenderStructure& out) = 0;
  virtual void draw(const RenderStructure& rs) = 0;
};

class Renderer2D : public Renderer {
 public:
  ~Renderer2D() override = default;
  void cull(SceneGraph& graph, RenderStructure& out) override;
  void draw(const RenderStructure& rs) override;
};

class Renderer3D : public Renderer {
 public:
  ~Renderer3D() override = default;
  void cull(SceneGraph& graph, RenderStructure& out) override;
  void draw(const RenderStructure& rs) override;
};
