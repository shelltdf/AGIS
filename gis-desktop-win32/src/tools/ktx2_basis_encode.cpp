#include "tools/ktx2_basis_encode.h"

#if AGIS_HAVE_BASISU

#include "encoder/basisu_comp.h"
#include "encoder/basisu_enc.h"

#include <fstream>
#include <mutex>
#include <vector>

namespace {

std::mutex g_basis_mu;
bool g_basis_inited = false;

bool EnsureBasisEncoder() {
  std::lock_guard<std::mutex> lock(g_basis_mu);
  if (g_basis_inited) {
    return true;
  }
  if (!basisu::basisu_encoder_init(false, false)) {
    return false;
  }
  g_basis_inited = true;
  return true;
}

}  // namespace

int AgisWriteRgbToKtx2Basis(const std::filesystem::path& path, int w, int h,
                             const std::vector<std::uint8_t>& rgb, bool etc1s_mode) {
  if (w < 1 || h < 1) {
    return 7;
  }
  const size_t need = static_cast<size_t>(w) * static_cast<size_t>(h) * 3u;
  if (rgb.size() < need) {
    return 7;
  }
  if (!EnsureBasisEncoder()) {
    return 8;
  }

  std::vector<std::uint8_t> rgba(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
  for (int i = 0; i < w * h; ++i) {
    rgba[static_cast<size_t>(i) * 4u + 0u] = rgb[static_cast<size_t>(i) * 3u + 0u];
    rgba[static_cast<size_t>(i) * 4u + 1u] = rgb[static_cast<size_t>(i) * 3u + 1u];
    rgba[static_cast<size_t>(i) * 4u + 2u] = rgb[static_cast<size_t>(i) * 3u + 2u];
    rgba[static_cast<size_t>(i) * 4u + 3u] = 255;
  }

  const basist::basis_tex_format mode =
      etc1s_mode ? basist::basis_tex_format::cETC1S : basist::basis_tex_format::cUASTC_LDR_4x4;

  uint32_t flags = basisu::cFlagKTX2 | basisu::cFlagGenMipsClamp | basisu::cFlagSRGB | basisu::cFlagThreaded;
#if BASISD_SUPPORT_KTX2_ZSTD
  flags |= basisu::cFlagKTX2UASTCSuperCompression;
#endif

  const int quality_level = etc1s_mode ? 90 : 85;
  const int effort_level = etc1s_mode ? 2 : 2;

  size_t out_size = 0;
  void* p =
      basisu::basis_compress2(mode, rgba.data(), static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                               static_cast<uint32_t>(w), flags, quality_level, effort_level, &out_size, nullptr);
  if (!p || out_size == 0) {
    return 9;
  }

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    basisu::basis_free_data(p);
    return 10;
  }
  out.write(static_cast<const char*>(p), static_cast<std::streamsize>(out_size));
  basisu::basis_free_data(p);
  return out.good() ? 0 : 11;
}

#endif  // AGIS_HAVE_BASISU
