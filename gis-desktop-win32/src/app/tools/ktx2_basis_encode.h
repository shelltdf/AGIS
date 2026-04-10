#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#if !defined(AGIS_HAVE_BASISU)
#define AGIS_HAVE_BASISU 0
#endif

#if AGIS_HAVE_BASISU
// 写出 KTX2（Basis Universal），RGB24 → RGBA；含 mip、sRGB、可选 Zstd 超压（取决于 basis 构建）。
// etc1s_mode：true → ETC1S；false → UASTC LDR 4x4。
int AgisWriteRgbToKtx2Basis(const std::filesystem::path& path, int w, int h,
                             const std::vector<std::uint8_t>& rgb, bool etc1s_mode);
#endif
