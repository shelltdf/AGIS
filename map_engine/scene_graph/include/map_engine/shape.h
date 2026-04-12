#pragma once

#include <cstdint>

enum class ShapeKind : std::uint8_t { kUnknown = 0, kMesh = 1 };

/** 场景节点上的可渲染形体抽象；具体种类由 ``Mesh`` 等子类实现。 */
class Shape {
 public:
  virtual ~Shape() = default;
  virtual ShapeKind kind() const = 0;
};
