#pragma once

#include <cstdint>
#include <string>

/** 材质：着色参数、纹理槽位等（占位，后续对接实际着色器与资源表）。 */
struct Material {
  std::string name;
  std::uint32_t flags{0};
};
