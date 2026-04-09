#pragma once

/// 在首次 GDALAllRegister 之前调用：若环境未设置 GDAL_DATA，则探测捆绑/构建输出的
/// `gdal_data` 目录并 `CPLSetConfigOption`。可安全重复调用。
void AgisEnsureGdalDataPath();
