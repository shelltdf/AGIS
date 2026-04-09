#pragma once

#include <string>

struct ObjPreviewModel;

/// 自 3D Tiles（tileset.json + b3dm/i3dm/glb/cmpt/pnts，不含 vcpkg）解析网格并填入 ObjPreviewModel，供 bgfx 预览。
/// KHR_draco_mesh_compression：构建启用 `TINYGLTF_ENABLE_DRACO` 并链接 `../3rdparty/draco` 时由 tinygltf 解压。
/// pnts：FLOAT POSITION 或 POSITION_QUANTIZED（含 RGB/RGBA 时取首点着色）；点展开为小四边形。
/// 不支持仅远程 http(s) 内容（跳过外链）。
/// @param progressPct 可选 0–100，加载过程中更新
bool AgisLoad3DTilesForPreview(const std::wstring& tilesetRootOrFile, ObjPreviewModel* outModel, std::wstring* errOut,
                               int* progressPct = nullptr);
