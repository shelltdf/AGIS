#pragma once

#include <cstdint>
#include <vector>

/** 几何：顶点布局、缓冲范围等（占位，后续对接 VBO/IBO 或 GPU 资源句柄）。 */
struct Geometry {
  std::uint32_t vertexCount{0};
  std::uint32_t indexCount{0};
  std::vector<std::uint8_t> vertexBlob{};
};
