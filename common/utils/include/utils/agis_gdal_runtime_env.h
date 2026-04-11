#pragma once

#include "core/export.h"

/// 在首次 GDALAllRegister / `proj_create_crs_to_crs` 之前调用：若环境未设置 GDAL_DATA，则探测
/// `gdal_data`；若未设置 PROJ_DATA，则探测 `proj_data`（或 `share/proj`，内含 `proj.db`）。
/// 共享库版 PROJ 通常不嵌入 proj.db，独立 `proj_context_create` 依赖 PROJ_DATA。可安全重复调用。
AGIS_COMMON_API void AgisEnsureGdalDataPath();
