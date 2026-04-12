#pragma once

#include "map_engine/geometry.h"
#include "map_engine/material.h"
#include "map_engine/shape.h"

/** 网格形体：同时携带材质与几何，可供 3D 渲染器或 2D 矢量网格化路径使用。 */
class Mesh : public Shape {
 public:
  ShapeKind kind() const override { return ShapeKind::kMesh; }

  Material& material() { return material_; }
  const Material& material() const { return material_; }
  Geometry& geometry() { return geometry_; }
  const Geometry& geometry() const { return geometry_; }

 private:
  Material material_;
  Geometry geometry_;
};
