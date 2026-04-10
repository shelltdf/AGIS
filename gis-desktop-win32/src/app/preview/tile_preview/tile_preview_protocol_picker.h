#pragma once

#include <windows.h>

#include <string>

/// 与瓦片预览支持的打开方式一一对应（顺序固定，供列表框索引使用）。
enum class AgisTilePreviewProtocol : int {
  kXyzTmsFolder = 0,
  kThreeDTilesJson,
  kSingleRasterImage,
  kMBTilesFile,
  kGeoPackageFile,
  kCount
};

/// 模态：先选协议，再弹出「选择文件夹」或「打开文件」。
/// 返回 true 且 *pathOut 非空表示用户完成选择；取消返回 false。
/// *pickerErrorOut 仅在返回 false 且非空时填充（例如 COM/ Shell 失败），用于 MessageBox。
bool TilePreviewShowProtocolAndPickPath(HWND owner, std::wstring* pathOut, AgisTilePreviewProtocol* protocolOut,
                                         std::wstring* pickerErrorOut);

/// 校验用户所选路径形态是否与协议一致（在调用加载逻辑之前调用）。
bool TilePreviewValidatePathMatchesProtocol(AgisTilePreviewProtocol protocol, const std::wstring& path,
                                            std::wstring* mismatchMessageOut);
