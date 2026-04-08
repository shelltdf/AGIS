#pragma once

#include <array>
#include <string>
#include <vector>

struct PreviewVec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct PreviewVec2 {
  float u = 0.0f;
  float v = 0.0f;
};

struct ObjPreviewModel {
  struct MaterialInfo {
    std::wstring name;
    float kdR = 0.30f;
    float kdG = 0.62f;
    float kdB = 0.92f;
    std::wstring mapKdPath;
  };
  std::vector<PreviewVec3> vertices;
  /// 来自 OBJ `vt`，按索引引用；可能为空。
  std::vector<PreviewVec2> texcoords;
  std::vector<std::array<int, 3>> faces;
  /// 与 `faces` 对齐；角点无 UV 时为 -1。
  std::vector<std::array<int, 3>> faceTexcoord;
  std::vector<int> faceMaterial;
  std::vector<MaterialInfo> materials;
  bool hasVertexTexcoords = false;
  PreviewVec3 center{};
  /// 水平面包围 max(ΔX,ΔY)（统计/布局用）；网格归一化以 `extent` 等比缩放。
  float extentHoriz = 1.0f;
  /// 包围盒最大边长 max(ΔX,ΔY,ΔZ)，预览网格按此 **等比** 缩放，与 OBJ 一致（高程/水平比例由转换阶段设置）。
  float extent = 1.0f;
  float kdR = 0.30f;
  float kdG = 0.62f;
  float kdB = 0.92f;
  std::wstring primaryMapKdPath;
};

constexpr size_t kModelPreviewMaxFaces = 120000;

inline size_t ModelPreviewFaceStride(size_t faceCount) {
  if (faceCount <= kModelPreviewMaxFaces) return 1;
  return (faceCount + kModelPreviewMaxFaces - 1) / kModelPreviewMaxFaces;
}
