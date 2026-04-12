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
 * - ``Renderer2D``：与 ``render_device_context.h`` 中 ``RenderDeviceContext2D``（``RenderDeviceContextBase`` 派生，GDI / GDI+ / D2D）一致。
 * - ``Renderer3D``：与 ``render_device_context.h`` 中 ``RenderDeviceContext3D``（``RenderDeviceContextBase`` 派生，bgfx：D3D11 / OpenGL / Auto）一致。
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
