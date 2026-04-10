#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>
#include <list>
#include <cstring>
#include <cstdlib>
#include <cwctype>
#include <memory>
#include <new>
#include <optional>

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#if !AGIS_USE_BGFX
#include <GL/gl.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <DirectXMath.h>
#endif

#include "app/preview/model_preview_types.h"
#if AGIS_USE_BGFX
#include "app/preview/model_preview_bgfx.h"
#include "imgui/imgui.h"
#endif
#include "app/preview/tiles_gltf_loader.h"
#include <gdiplus.h>
#include "common/ui/ui_font.h"
#include "common/app_core/main_app.h"
#include "common/app_core/main_globals.h"

#ifndef GIS_DESKTOP_HAVE_GDAL
#define GIS_DESKTOP_HAVE_GDAL 0
#endif
#if GIS_DESKTOP_HAVE_GDAL
#include "common/runtime/agis_gdal_runtime_env.h"
#include <cpl_error.h>
#include <gdal.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#endif

struct ObjPreviewStats {
  uint64_t vertices = 0;
  uint64_t texcoords = 0;
  uint64_t normals = 0;
  uint64_t faces = 0;
  uint64_t materials = 0;
  uint64_t textures = 0;
  uint64_t fileBytes = 0;
};

#if !AGIS_USE_BGFX
enum class PreviewRenderBackend { kOpenGL, kDx11 };
#endif

struct LasPointFltRaw {
  double x = 0;
  double y = 0;
  double z = 0;
  std::uint8_t r = 200;
  std::uint8_t g = 200;
  std::uint8_t b = 200;
};

struct ModelPreviewState {
  ObjPreviewModel model;
  ObjPreviewStats stats;
  std::wstring path;
  float rotX = 0.5f;
  float rotY = -0.8f;
  float zoom = 1.0f;
  bool dragging = false;
  POINT lastPt{};
  bool solid = true;
  bool backfaceCulling = false;
  bool showGrid = true;
  bool useTexture = true;
  std::vector<std::pair<std::wstring, std::wstring>> textureLayers;
  int currentTextureLayer = 0;
#if AGIS_USE_BGFX
  AgisBgfxRendererKind bgfxRenderer = AgisBgfxRendererKind::kD3D11;
  AgisBgfxPreviewContext* bgfxCtx = nullptr;
  bool imguiReady = false;
  int imguiScroll = 0;
#else
  PreviewRenderBackend backend = PreviewRenderBackend::kOpenGL;
  HDC glHdc = nullptr;
  HGLRC glRc = nullptr;
  ID3D11Device* d3dDev = nullptr;
  ID3D11DeviceContext* d3dCtx = nullptr;
  IDXGISwapChain* d3dSwap = nullptr;
  ID3D11RenderTargetView* d3dRtv = nullptr;
  ID3D11VertexShader* d3dVs = nullptr;
  ID3D11PixelShader* d3dPs = nullptr;
  ID3D11InputLayout* d3dLayout = nullptr;
  ID3D11Buffer* d3dVb = nullptr;
  ID3D11Buffer* d3dCbMvp = nullptr;
  ID3D11RasterizerState* d3dRsSolid = nullptr;
  ID3D11RasterizerState* d3dRsWire = nullptr;
  ID3D11DepthStencilState* d3dDsState = nullptr;
  ID3D11Texture2D* d3dDsTex = nullptr;
  ID3D11DepthStencilView* d3dDsv = nullptr;
  UINT d3dVbCap = 0;
#endif
  DWORD lastFpsTick = 0;
  int frameCounter = 0;
  float fps = 0.0f;
  float lastFrameMs = 0.0f;
  std::array<float, 64> frameMsHistory{};
  int frameMsHistoryCount = 0;
  int frameMsHistoryPos = 0;
  std::wstring runtimeBottleneck = L"--";
  std::wstring runtimeHudText;
  std::wstring infoPanelText;
  bool pseudoPbrMode = true;
  AgisBgfxPbrViewMode pbrViewMode = AgisBgfxPbrViewMode::kPbrLit;
  bool inPaint = false;
  bool loading = true;
  bool loadFailed = false;
  int loadProgress = 0;
  int loadStage = 0;
  HANDLE loadThread = nullptr;
  ObjPreviewModel loadedModel;
  ObjPreviewStats loadedStats;
  /// >0 表示上次成功加载来自 LAS（用于信息面板文案）。
  uint64_t lasSourcePointCount = 0;
  /// LAS/LAZ 源点缓存（含下采样前完整读入集），用于在 UI 中调整点大小后快速重建网格。
  std::vector<LasPointFltRaw> lasPointCache;
  /// 点云「屏幕像素」近似大小（双三角点精灵），默认 5。
  float lasPointScreenPx = 5.f;
  /// LAZ 经 GDAL 打开失败时由 CPL/实现侧填写的说明（LAS 路径通常为空）。
  std::wstring lazPreviewDiag;
  bool loadAs3DTiles = false;
  bool sourceIs3DTiles = false;
  std::wstring tilesLoadDiag;
};

constexpr UINT kPreviewLoadedMsg = WM_APP + 201;
struct PreviewLoadCtx {
  HWND hwnd = nullptr;
  ModelPreviewState* st = nullptr;
};

namespace {
constexpr uint64_t kPreviewObjFaceHardLimit = 5000000ull;
constexpr uint32_t kLasPreviewMaxSourcePoints = 25'000'000u;
/// 每个 LAS 点用 2 个三角面近似方形点斑，故面数上限需覆盖点数×2。
constexpr size_t kLasPreviewMaxOutputTriangles = 900'000u;

std::string PreviewWideToUtf8(const std::wstring& ws);

static int PeekLasRecordCount(const std::wstring& pathW, std::uint32_t* nOut) {
  if (!nOut) {
    return 7;
  }
  std::ifstream ifs(std::filesystem::path(pathW), std::ios::binary);
  if (!ifs) {
    return 3;
  }
  char sig[4]{};
  ifs.read(sig, 4);
  if (!ifs || std::memcmp(sig, "LASF", 4) != 0) {
    return 7;
  }
  ifs.seekg(96);
  std::uint32_t offData = 0;
  ifs.read(reinterpret_cast<char*>(&offData), 4);
  ifs.seekg(104);
  unsigned char pdrf = 0;
  ifs.read(reinterpret_cast<char*>(&pdrf), 1);
  std::uint16_t pdlen = 0;
  ifs.read(reinterpret_cast<char*>(&pdlen), 2);
  std::uint32_t nrec = 0;
  ifs.read(reinterpret_cast<char*>(&nrec), 4);
  if (!ifs || offData < 227 || pdlen < 20 || nrec == 0) {
    return 7;
  }
  *nOut = nrec;
  return 0;
}

static bool ReadLasPointsPreview(const std::wstring& pathW, std::vector<LasPointFltRaw>* outPts, int* progressPct) {
  if (!outPts) {
    return false;
  }
  outPts->clear();
  std::ifstream ifs(std::filesystem::path(pathW), std::ios::binary);
  if (!ifs) {
    return false;
  }
  char sig[4]{};
  ifs.read(sig, 4);
  if (!ifs || std::memcmp(sig, "LASF", 4) != 0) {
    return false;
  }
  ifs.seekg(96);
  std::uint32_t offData = 0;
  ifs.read(reinterpret_cast<char*>(&offData), 4);
  ifs.seekg(104);
  unsigned char pdrf = 0;
  ifs.read(reinterpret_cast<char*>(&pdrf), 1);
  std::uint16_t pdlen = 0;
  ifs.read(reinterpret_cast<char*>(&pdlen), 2);
  std::uint32_t nrec = 0;
  ifs.read(reinterpret_cast<char*>(&nrec), 4);
  if (!ifs || offData < 227 || pdlen < 20 || nrec == 0) {
    return false;
  }
  double xs = 0.001, ys = 0.001, zs = 0.001, xo = 0, yo = 0, zo = 0;
  ifs.seekg(131);
  ifs.read(reinterpret_cast<char*>(&xs), 8);
  ifs.read(reinterpret_cast<char*>(&ys), 8);
  ifs.read(reinterpret_cast<char*>(&zs), 8);
  ifs.read(reinterpret_cast<char*>(&xo), 8);
  ifs.read(reinterpret_cast<char*>(&yo), 8);
  ifs.read(reinterpret_cast<char*>(&zo), 8);
  if (!ifs) {
    return false;
  }
  ifs.seekg(static_cast<std::streamoff>(offData));
  outPts->reserve(nrec);
  for (std::uint32_t i = 0; i < nrec; ++i) {
    if (progressPct && (i & 0x3FFFu) == 0) {
      *progressPct = 5 + static_cast<int>((static_cast<uint64_t>(i) * 85u) / (std::max)(1u, nrec));
    }
    std::vector<unsigned char> buf(pdlen);
    ifs.read(reinterpret_cast<char*>(buf.data()), pdlen);
    if (!ifs) {
      break;
    }
    if (buf.size() < 12) {
      continue;
    }
    std::int32_t xi = 0, yi = 0, zi = 0;
    std::memcpy(&xi, buf.data() + 0, 4);
    std::memcpy(&yi, buf.data() + 4, 4);
    std::memcpy(&zi, buf.data() + 8, 4);
    LasPointFltRaw p;
    p.x = static_cast<double>(xi) * xs + xo;
    p.y = static_cast<double>(yi) * ys + yo;
    p.z = static_cast<double>(zi) * zs + zo;
    if (pdrf == 2 && buf.size() >= 26) {
      std::uint16_t R = 0, G = 0, B = 0;
      std::memcpy(&R, buf.data() + 20, 2);
      std::memcpy(&G, buf.data() + 22, 2);
      std::memcpy(&B, buf.data() + 24, 2);
      p.r = static_cast<std::uint8_t>((std::min)(255, static_cast<int>(R / 256)));
      p.g = static_cast<std::uint8_t>((std::min)(255, static_cast<int>(G / 256)));
      p.b = static_cast<std::uint8_t>((std::min)(255, static_cast<int>(B / 256)));
    } else if (pdrf == 3 && buf.size() >= 34) {
      std::uint16_t R = 0, G = 0, B = 0;
      std::memcpy(&R, buf.data() + 28, 2);
      std::memcpy(&G, buf.data() + 30, 2);
      std::memcpy(&B, buf.data() + 32, 2);
      p.r = static_cast<std::uint8_t>((std::min)(255, static_cast<int>(R / 256)));
      p.g = static_cast<std::uint8_t>((std::min)(255, static_cast<int>(G / 256)));
      p.b = static_cast<std::uint8_t>((std::min)(255, static_cast<int>(B / 256)));
    }
    outPts->push_back(p);
  }
  if (progressPct) {
    *progressPct = 95;
  }
  return !outPts->empty();
}

#if GIS_DESKTOP_HAVE_GDAL
static std::wstring GdalUtf8ErrToWide(const char* utf8) {
  if (!utf8 || !utf8[0]) {
    return {};
  }
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
  if (n <= 0) {
    return {};
  }
  std::wstring w(static_cast<size_t>(n - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w.data(), n);
  return w;
}

static void AppendLazGdalDriverHint(std::wstring* d) {
  if (!d) {
    return;
  }
  *d += L"\n\n";
  *d += L"【依赖】LAZ 为 LASzip 压缩。若 bundled LASzip 未随工程编入，可放置源码到 ../3rdparty/LASzip（见 README-LASZIP.md）；"
        L"否则仅能通过 GDAL 矢量路径打开 LAZ，依赖 GDAL 对 LAS/LAZ 的驱动与解压配置。\n";
  *d += L"【可选】将 LAZ 转为 .las；并确认 CMake 已对 bundled LASzip / GDAL 正确检测。\n";
  *d += L"【排查】若上方 bundled LASzip 已报 “wrong file_signature” / “not a LAS/LAZ file”，说明文件头不是 LASF，多为文件本体或扩展名问题，"
        L"与「未装 LASzip」无关；此时 GDAL 回退通常同样失败，请先用外部工具或十六进制查看器核对文件格式。";
}

static bool ReadLazPointsPreviewGdal(const std::wstring& pathW, std::vector<LasPointFltRaw>* outPts, int* progressPct,
                                     std::wstring* diagOut) {
  if (!outPts) {
    return false;
  }
  outPts->clear();
  auto setDiag = [&](std::wstring head) {
    if (!diagOut) {
      return;
    }
    diagOut->swap(head);
    const char* cpl = CPLGetLastErrorMsg();
    if (cpl && cpl[0]) {
      diagOut->append(L"\n\n[CPL] ");
      diagOut->append(GdalUtf8ErrToWide(cpl));
    }
    AppendLazGdalDriverHint(diagOut);
  };
  AgisEnsureGdalDataPath();
  CPLErrorReset();
  GDALAllRegister();
  std::string utf8;
  {
    const int n = WideCharToMultiByte(CP_UTF8, 0, pathW.c_str(), static_cast<int>(pathW.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
      setDiag(L"内部错误：无法将路径转为 UTF-8 供 GDALOpenEx 使用。");
      return false;
    }
    utf8.assign(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, pathW.c_str(), static_cast<int>(pathW.size()), utf8.data(), n, nullptr, nullptr);
  }
  CPLErrorReset();
  GDALDataset* ds = static_cast<GDALDataset*>(
      GDALOpenEx(utf8.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
  if (!ds) {
    setDiag(L"GDAL（矢量 Open）未能打开该 LAZ：驱动/格式未识别、缺少 LASzip 支持，或 CPL 详情如下。");
    return false;
  }
  OGRLayer* layer = ds->GetLayer(0);
  if (!layer) {
    setDiag(L"GDAL 已打开数据集，但不存在第 0 个矢量图层。");
    GDALClose(ds);
    return false;
  }
  layer->ResetReading();
  OGRFeatureDefn* defn = layer->GetLayerDefn();
  auto findField = [defn](const char* a, const char* b) -> int {
    int i = defn->GetFieldIndex(a);
    if (i < 0) {
      i = defn->GetFieldIndex(b);
    }
    return i;
  };
  const int iRed = findField("Color Red", "Red");
  const int iGreen = findField("Color Green", "Green");
  const int iBlue = findField("Color Blue", "Blue");

  const GIntBig fc = layer->GetFeatureCount(FALSE);
  if (fc > 0) {
    const size_t cap = (std::min)(static_cast<size_t>(fc), static_cast<size_t>(kLasPreviewMaxSourcePoints));
    outPts->reserve(cap);
  }

  OGRFeature* f = nullptr;
  GIntBig idx = 0;
  while ((f = layer->GetNextFeature()) != nullptr) {
    if (outPts->size() >= kLasPreviewMaxSourcePoints) {
      OGRFeature::DestroyFeature(f);
      break;
    }
    if (progressPct && (idx & 0x3FFF) == 0) {
      if (fc > 0) {
        *progressPct =
            5 + static_cast<int>((static_cast<double>(idx) * 85.0) / (std::max)(static_cast<GIntBig>(1), fc));
      } else {
        *progressPct = 5 + static_cast<int>((idx & 0xFF) % 85);
      }
    }
    OGRGeometry* geom = f->GetGeometryRef();
    if (!geom || geom->IsEmpty()) {
      OGRFeature::DestroyFeature(f);
      ++idx;
      continue;
    }
    if (wkbFlatten(geom->getGeometryType()) != wkbPoint) {
      OGRFeature::DestroyFeature(f);
      ++idx;
      continue;
    }
    const OGRPoint* const pt = geom->toPoint();
    if (!pt) {
      OGRFeature::DestroyFeature(f);
      ++idx;
      continue;
    }
    LasPointFltRaw p{};
    p.x = pt->getX();
    p.y = pt->getY();
    p.z = pt->getZ();
    auto clampU8 = [](int v) -> std::uint8_t {
      return static_cast<std::uint8_t>((std::clamp)(v, 0, 255));
    };
    if (iRed >= 0 && iGreen >= 0 && iBlue >= 0) {
      const int R = f->GetFieldAsInteger(iRed);
      const int G = f->GetFieldAsInteger(iGreen);
      const int B = f->GetFieldAsInteger(iBlue);
      p.r = clampU8(R / 256);
      p.g = clampU8(G / 256);
      p.b = clampU8(B / 256);
    }
    outPts->push_back(p);
    OGRFeature::DestroyFeature(f);
    ++idx;
  }
  GDALClose(ds);
  if (progressPct) {
    *progressPct = 95;
  }
  if (outPts->empty()) {
    setDiag(L"图层可读但未得到任何 Point 要素（几何过滤后为空）。");
    return false;
  }
  if (diagOut) {
    diagOut->clear();
  }
  return true;
}
#endif

#if defined(AGIS_HAVE_LASZIP) && AGIS_HAVE_LASZIP
#include <laszip_api.h>

static std::wstring LaszipUtf8ToWide(const char* utf8) {
  if (!utf8 || !utf8[0]) {
    return {};
  }
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
  if (n <= 0) {
    return {};
  }
  std::wstring w(static_cast<size_t>(n - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w.data(), n);
  return w;
}

static void AppendLaszipLastError(std::wstring* d, laszip_POINTER laszip) {
  if (!d || !laszip) {
    return;
  }
  laszip_CHAR* err = nullptr;
  if (laszip_get_error(laszip, &err) != 0 || !err || !err[0]) {
    return;
  }
  d->append(L"\n[LASzip] ");
  d->append(LaszipUtf8ToWide(err));
}

/// LASzip 报非 LASF 魔数时，避免用户误认为是「未装 LASzip / 仅 GDAL 问题」。
static void AppendLazWrongSignatureHintIfMatching(std::wstring* d) {
  if (!d || d->empty()) {
    return;
  }
  const auto lowerContains = [d](const wchar_t* asciiSub) -> bool {
    const size_t m = std::wcslen(asciiSub);
    if (m == 0 || d->size() < m) {
      return false;
    }
    for (size_t i = 0; i + m <= d->size(); ++i) {
      bool ok = true;
      for (size_t j = 0; j < m; ++j) {
        const wchar_t c = (*d)[i + j];
        const wchar_t s = asciiSub[j];
        if (c != s && std::towlower(static_cast<std::wint_t>(c)) != std::towlower(static_cast<std::wint_t>(s))) {
          ok = false;
          break;
        }
      }
      if (ok) {
        return true;
      }
    }
    return false;
  };
  if (lowerContains(L"file_signature") || lowerContains(L"not a las")) {
    d->append(
        L"\n\n【文件头】正规 LAS/（LAZ 外层亦为）LAS 1.x 应以 ASCII “LASF” 开头（十六进制 4C 41 53 "
        L"46）。出现本错误多表示：文件并非 LAS/LAZ、下载/拷贝损坏、扩展名与内容不符，或其它点云格式被误命名为 .laz。请先在外部工具或用十六进制查看器核对文件头，"
        L"而非仅重装 GDAL/LASzip。");
  }
}

/// `laszip_get_point_count` 反映的是读写游标 `p_count`，reader 刚打开时为 0；文件总点数须从公共头读取（与 LASzip `open_reader` 内对 `npoints` 的算法一致）。
static laszip_I64 LazFileTotalPointCount(const laszip_header_struct* header) {
  if (!header) {
    return 0;
  }
  if (header->number_of_point_records != 0) {
    return static_cast<laszip_I64>(header->number_of_point_records);
  }
  return static_cast<laszip_I64>(header->extended_number_of_point_records);
}

/// @return 0 成功；3 路径/创建失败；7 非有效 LAS/LAZ 或读头失败
static int PeekLazPointCountLaszip(const std::wstring& pathW, std::uint64_t* nOut, std::wstring* diagOut) {
  if (!nOut) {
    return 7;
  }
  *nOut = 0;
  laszip_POINTER laszip = nullptr;
  if (laszip_create(&laszip) != 0 || !laszip) {
    if (diagOut) {
      diagOut->assign(L"LASzip：laszip_create 失败。");
    }
    return 3;
  }
  const std::string utf8 = PreviewWideToUtf8(pathW);
  if (utf8.empty() && !pathW.empty()) {
    if (diagOut) {
      diagOut->assign(L"内部错误：无法将路径转为 UTF-8。");
    }
    laszip_destroy(laszip);
    return 3;
  }
  laszip_BOOL compressed = 0;
  if (laszip_open_reader(laszip, utf8.c_str(), &compressed) != 0) {
    if (diagOut) {
      diagOut->assign(L"LASzip：无法打开读端（可能不是 LAZ/LAS 或文件损坏）。");
      AppendLaszipLastError(diagOut, laszip);
      AppendLazWrongSignatureHintIfMatching(diagOut);
    }
    laszip_destroy(laszip);
    return 7;
  }
  laszip_header_struct* header = nullptr;
  if (laszip_get_header_pointer(laszip, &header) != 0 || !header) {
    if (diagOut) {
      diagOut->assign(L"LASzip：无法取得文件头指针。");
      AppendLaszipLastError(diagOut, laszip);
    }
    laszip_close_reader(laszip);
    laszip_destroy(laszip);
    return 7;
  }
  const laszip_I64 cnt = LazFileTotalPointCount(header);
  if (cnt <= 0) {
    if (diagOut) {
      diagOut->assign(L"LASzip：无法读取有效点数。");
      AppendLaszipLastError(diagOut, laszip);
    }
    laszip_close_reader(laszip);
    laszip_destroy(laszip);
    return 7;
  }
  laszip_close_reader(laszip);
  laszip_destroy(laszip);
  *nOut = static_cast<std::uint64_t>(cnt);
  return 0;
}

static bool ReadLazPointsLaszip(const std::wstring& pathW, std::vector<LasPointFltRaw>* outPts, int* progressPct,
                                std::wstring* diagOut) {
  if (!outPts) {
    return false;
  }
  outPts->clear();
  laszip_POINTER laszip = nullptr;
  if (laszip_create(&laszip) != 0 || !laszip) {
    if (diagOut) {
      diagOut->assign(L"LASzip：laszip_create 失败。");
    }
    return false;
  }
  const std::string utf8 = PreviewWideToUtf8(pathW);
  if (utf8.empty() && !pathW.empty()) {
    if (diagOut) {
      diagOut->assign(L"内部错误：无法将路径转为 UTF-8。");
    }
    laszip_destroy(laszip);
    return false;
  }
  laszip_BOOL compressed = 0;
  if (laszip_open_reader(laszip, utf8.c_str(), &compressed) != 0) {
    if (diagOut) {
      diagOut->assign(L"LASzip：无法打开读端。");
      AppendLaszipLastError(diagOut, laszip);
      AppendLazWrongSignatureHintIfMatching(diagOut);
    }
    laszip_destroy(laszip);
    return false;
  }
  laszip_header_struct* header = nullptr;
  if (laszip_get_header_pointer(laszip, &header) != 0 || !header) {
    if (diagOut) {
      diagOut->assign(L"LASzip：无法读取文件头。");
      AppendLaszipLastError(diagOut, laszip);
    }
    laszip_close_reader(laszip);
    laszip_destroy(laszip);
    return false;
  }
  const laszip_I64 npoints = LazFileTotalPointCount(header);
  if (npoints <= 0) {
    if (diagOut) {
      diagOut->assign(L"LASzip：点数无效。");
      AppendLaszipLastError(diagOut, laszip);
    }
    laszip_close_reader(laszip);
    laszip_destroy(laszip);
    return false;
  }
  laszip_point_struct* point = nullptr;
  if (laszip_get_point_pointer(laszip, &point) != 0 || !point) {
    if (diagOut) {
      diagOut->assign(L"LASzip：无法取得点缓冲指针。");
      AppendLaszipLastError(diagOut, laszip);
    }
    laszip_close_reader(laszip);
    laszip_destroy(laszip);
    return false;
  }

  const size_t maxRead =
      (std::min)(static_cast<size_t>(npoints), static_cast<size_t>(kLasPreviewMaxSourcePoints));
  outPts->reserve(maxRead);
  const unsigned pdrf = header->point_data_format;
  auto clampU8 = [](int v) -> std::uint8_t { return static_cast<std::uint8_t>((std::clamp)(v, 0, 255)); };

  for (laszip_I64 i = 0; i < npoints && outPts->size() < kLasPreviewMaxSourcePoints; ++i) {
    if (laszip_read_point(laszip) != 0) {
      if (diagOut) {
        diagOut->assign(L"LASzip：读取第 ")
            .append(std::to_wstring(static_cast<unsigned long long>(i)))
            .append(L" 个点失败。");
        AppendLaszipLastError(diagOut, laszip);
      }
      laszip_close_reader(laszip);
      laszip_destroy(laszip);
      outPts->clear();
      return false;
    }
    laszip_F64 coords[3]{};
    if (laszip_get_coordinates(laszip, coords) != 0) {
      if (diagOut) {
        diagOut->assign(L"LASzip：坐标解码失败。");
        AppendLaszipLastError(diagOut, laszip);
      }
      laszip_close_reader(laszip);
      laszip_destroy(laszip);
      outPts->clear();
      return false;
    }
    LasPointFltRaw p{};
    p.x = coords[0];
    p.y = coords[1];
    p.z = coords[2];
    if (pdrf == 2 || pdrf == 3 || pdrf == 5 || pdrf == 7) {
      p.r = clampU8(static_cast<int>(point->rgb[0] / 256));
      p.g = clampU8(static_cast<int>(point->rgb[1] / 256));
      p.b = clampU8(static_cast<int>(point->rgb[2] / 256));
    }
    outPts->push_back(p);
    if (progressPct && (i & 0x3FFF) == 0) {
      *progressPct =
          5 + static_cast<int>((static_cast<std::uint64_t>(i) * 85u) /
                               (std::max)(static_cast<std::uint64_t>(1), static_cast<std::uint64_t>(npoints)));
    }
  }
  laszip_close_reader(laszip);
  laszip_destroy(laszip);
  if (progressPct) {
    *progressPct = 95;
  }
  if (outPts->empty()) {
    if (diagOut) {
      diagOut->assign(L"LASzip：未得到任何点。");
    }
    return false;
  }
  if (diagOut) {
    diagOut->clear();
  }
  return true;
}
#endif  // AGIS_HAVE_LASZIP

static bool BuildObjPreviewFromLasPoints(const std::vector<LasPointFltRaw>& pts, float pointScreenSizePx,
                                          ObjPreviewModel* out) {
  if (!out || pts.empty()) {
    return false;
  }
  PreviewVec3 vmin{1e9f, 1e9f, 1e9f};
  PreviewVec3 vmax{-1e9f, -1e9f, -1e9f};
  for (const auto& p : pts) {
    const float x = static_cast<float>(p.x);
    const float y = static_cast<float>(p.y);
    const float z = static_cast<float>(p.z);
    vmin.x = (std::min)(vmin.x, x);
    vmin.y = (std::min)(vmin.y, y);
    vmin.z = (std::min)(vmin.z, z);
    vmax.x = (std::max)(vmax.x, x);
    vmax.y = (std::max)(vmax.y, y);
    vmax.z = (std::max)(vmax.z, z);
  }
  out->vertices.clear();
  out->texcoords.clear();
  out->faces.clear();
  out->faceTexcoord.clear();
  out->faceMaterial.clear();
  out->materials.clear();
  out->hasVertexTexcoords = false;
  out->primaryMapKdPath.clear();
  out->center = {(vmin.x + vmax.x) * 0.5f, (vmin.y + vmax.y) * 0.5f, (vmin.z + vmax.z) * 0.5f};
  const float ex = (std::max)(1e-6f, vmax.x - vmin.x);
  const float ey = (std::max)(1e-6f, vmax.y - vmin.y);
  const float ez = (std::max)(1e-6f, vmax.z - vmin.z);
  out->extentHoriz = (std::max)((std::max)(ex, ey), 1e-6f);
  out->extent = (std::max)(out->extentHoriz, ez);
  out->kdR = 0.55f;
  out->kdG = 0.55f;
  out->kdB = 0.6f;

  const float pxScale = (std::clamp)(pointScreenSizePx, 0.5f, 64.f) / 5.f;
  const size_t stride = 1;
  /// XY 平面上的「半边长」，按包围范围与目标像素尺寸缩放，近似屏幕常量大小点斑（GPU 用三角面而非 PT_POINTS，兼容可调的屏幕像素感）。
  const float hx =
      (std::max)(out->extent * 1e-6f, out->extent * 0.0020f * pxScale);
  const float hy = hx;

  const size_t approxPt = (pts.size() + stride - 1) / stride;
  out->materials.reserve(approxPt);
  out->vertices.reserve(approxPt * 4);
  out->faces.reserve(approxPt * 2);
  out->faceTexcoord.reserve(approxPt * 2);
  out->faceMaterial.reserve(approxPt * 2);

  for (size_t i = 0; i < pts.size(); i += stride) {
    const auto& p = pts[i];
    ObjPreviewModel::MaterialInfo m{};
    m.name = L"las";
    m.kdR = static_cast<float>(p.r) / 255.0f;
    m.kdG = static_cast<float>(p.g) / 255.0f;
    m.kdB = static_cast<float>(p.b) / 255.0f;
    const int mid = static_cast<int>(out->materials.size());
    out->materials.push_back(std::move(m));

    const float px = static_cast<float>(p.x);
    const float py = static_cast<float>(p.y);
    const float pz = static_cast<float>(p.z);
    const int b0 = static_cast<int>(out->vertices.size());
    out->vertices.push_back({px - hx, py - hy, pz});
    out->vertices.push_back({px + hx, py - hy, pz});
    out->vertices.push_back({px + hx, py + hy, pz});
    out->vertices.push_back({px - hx, py + hy, pz});
    out->faces.push_back({b0, b0 + 1, b0 + 2});
    out->faces.push_back({b0, b0 + 2, b0 + 3});
    out->faceTexcoord.push_back({-1, -1, -1});
    out->faceTexcoord.push_back({-1, -1, -1});
    out->faceMaterial.push_back(mid);
    out->faceMaterial.push_back(mid);
  }
  return !out->faces.empty();
}

static bool PreviewPathIsLasFile(const std::wstring& path) {
  return _wcsicmp(std::filesystem::path(path).extension().c_str(), L".las") == 0;
}

static bool PreviewPathIsLazFile(const std::wstring& path) {
  return _wcsicmp(std::filesystem::path(path).extension().c_str(), L".laz") == 0;
}

static bool PreviewPathIsPointCloudFile(const std::wstring& path) {
  return PreviewPathIsLasFile(path) || PreviewPathIsLazFile(path);
}

std::string PreviewWideToUtf8(const std::wstring& ws) {
  if (ws.empty()) {
    return {};
  }
  const int n =
      WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
  if (n <= 0) {
    return {};
  }
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), out.data(), n, nullptr, nullptr);
  return out;
}
}  // namespace

std::wstring BuildFrameMsSparkline(const ModelPreviewState& st);

std::vector<std::pair<std::wstring, std::wstring>> DetectTextureLayers(const ObjPreviewModel& model) {
  std::vector<std::pair<std::wstring, std::wstring>> layers;
  if (!model.primaryMapKdPath.empty()) {
    layers.push_back({L"baseColor(map_Kd)", model.primaryMapKdPath});
    std::filesystem::path p(model.primaryMapKdPath);
    std::wstring stem = p.stem().wstring();
    if (stem.size() > 7 && stem.substr(stem.size() - 7) == L"_albedo") {
      stem = stem.substr(0, stem.size() - 7);
    }
    const std::wstring ext = p.extension().wstring();
    const std::filesystem::path dir = p.parent_path();
    auto addIf = [&](const wchar_t* name, const wchar_t* suffix) {
      const std::filesystem::path cand = dir / (stem + suffix + ext);
      if (std::filesystem::exists(cand)) layers.push_back({name, cand.wstring()});
    };
    addIf(L"normal", L"_normal");
    addIf(L"roughness", L"_roughness");
    addIf(L"metallic", L"_metallic");
    addIf(L"ao", L"_ao");
  }
  return layers;
}

AgisBgfxPbrTexturePaths BuildPbrTexturePaths(const ModelPreviewState& st) {
  AgisBgfxPbrTexturePaths out{};
  for (const auto& kv : st.textureLayers) {
    if (kv.first == L"baseColor(map_Kd)") {
      out.baseColorPath = kv.second;
    } else if (kv.first == L"normal") {
      out.normalPath = kv.second;
    } else if (kv.first == L"roughness") {
      out.roughnessPath = kv.second;
    } else if (kv.first == L"metallic") {
      out.metallicPath = kv.second;
    } else if (kv.first == L"ao") {
      out.aoPath = kv.second;
    }
  }
  return out;
}

struct PreviewHudButton {
  RECT rc{};
  const wchar_t* label = L"";
  WPARAM key = 0;
};

std::vector<PreviewHudButton> BuildPreviewHudButtons(const RECT& vrc) {
  std::vector<PreviewHudButton> out;
  const int x0 = vrc.left + 10;
  const int y0 = vrc.top + 112;
  const int h = 24;
  const int gap = 6;
  int x = x0;
  auto push = [&](const wchar_t* label, WPARAM key, int w) {
    PreviewHudButton b{};
    b.rc = RECT{x, y0, x + w, y0 + h};
    b.label = label;
    b.key = key;
    out.push_back(b);
    x += w + gap;
  };
  push(L"M", 'M', 26);
  push(L"Cull", 'B', 46);
  push(L"Grid", 'G', 44);
  push(L"Tex", 'T', 40);
  push(L"PBR", 'P', 42);
  push(L"[", VK_OEM_4, 24);
  push(L"]", VK_OEM_6, 24);
  push(L"Fit", 'F', 36);
  push(L"Reset", 'R', 48);
  return out;
}

bool DispatchPreviewHudClick(HWND hwnd, const RECT& vrc, const POINT& pt) {
  const auto buttons = BuildPreviewHudButtons(vrc);
  for (const auto& b : buttons) {
    if (PtInRect(&b.rc, pt)) {
      PostMessageW(hwnd, WM_KEYDOWN, b.key, 0);
      return true;
    }
  }
  return false;
}

void DrawAxisOverlay(HDC hdc, const RECT& vrc, const ModelPreviewState& st) {
  const int ox = vrc.left + 28;
  const int oy = vrc.bottom - 28;
  const float len = 20.0f;
  const float cx = std::cos(st.rotX), sx = std::sin(st.rotX);
  const float cy = std::cos(st.rotY), sy = std::sin(st.rotY);
  auto rot = [&](float x, float y, float z) {
    float x1 = x;
    float y1 = y * cx - z * sx;
    float z1 = y * sx + z * cx;
    float x2 = x1 * cy + z1 * sy;
    float y2 = y1;
    return POINT{ox + static_cast<int>(x2 * len), oy - static_cast<int>(y2 * len)};
  };
  const POINT px = rot(1, 0, 0);
  const POINT py = rot(0, 1, 0);
  const POINT pz = rot(0, 0, 1);
  SetBkMode(hdc, TRANSPARENT);
  HPEN rx = CreatePen(PS_SOLID, 2, RGB(220, 60, 60));
  HPEN gx = CreatePen(PS_SOLID, 2, RGB(60, 180, 70));
  HPEN bx = CreatePen(PS_SOLID, 2, RGB(70, 110, 220));
  HPEN old = reinterpret_cast<HPEN>(SelectObject(hdc, rx));
  MoveToEx(hdc, ox, oy, nullptr); LineTo(hdc, px.x, px.y); TextOutW(hdc, px.x + 2, px.y - 8, L"X", 1);
  SelectObject(hdc, gx);
  MoveToEx(hdc, ox, oy, nullptr); LineTo(hdc, py.x, py.y); TextOutW(hdc, py.x + 2, py.y - 8, L"Y", 1);
  SelectObject(hdc, bx);
  MoveToEx(hdc, ox, oy, nullptr); LineTo(hdc, pz.x, pz.y); TextOutW(hdc, pz.x + 2, pz.y - 8, L"Z", 1);
  SelectObject(hdc, old);
  DeleteObject(rx);
  DeleteObject(gx);
  DeleteObject(bx);
}

#if AGIS_USE_BGFX
namespace {
void DrawAxisImGuiOverlay(const RECT& vrc, float rotX, float rotY) {
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const float ox = static_cast<float>(vrc.left + 28);
  const float oy = static_cast<float>(vrc.bottom - 28);
  const float len = 20.0f;
  const float cx = std::cos(rotX), sx = std::sin(rotX);
  const float cy = std::cos(rotY), sy = std::sin(rotY);
  auto rot = [&](float x, float y, float z) {
    float x1 = x;
    float y1 = y * cx - z * sx;
    float z1 = y * sx + z * cx;
    float x2 = x1 * cy + z1 * sy;
    float y2 = y1;
    return ImVec2(ox + x2 * len, oy - y2 * len);
  };
  const ImVec2 px = rot(1, 0, 0);
  const ImVec2 py = rot(0, 1, 0);
  const ImVec2 pz = rot(0, 0, 1);
  dl->AddLine(ImVec2(ox, oy), px, IM_COL32(220, 60, 60, 255), 2.0f);
  dl->AddLine(ImVec2(ox, oy), py, IM_COL32(60, 180, 70, 255), 2.0f);
  dl->AddLine(ImVec2(ox, oy), pz, IM_COL32(70, 110, 220, 255), 2.0f);
  ImFont* font = ImGui::GetFont();
  if (font) {
    dl->AddText(font, 13.0f, ImVec2(px.x + 3.0f, px.y - 10.0f), IM_COL32(220, 60, 60, 255), "X");
    dl->AddText(font, 13.0f, ImVec2(py.x + 3.0f, py.y - 10.0f), IM_COL32(60, 180, 70, 255), "Y");
    dl->AddText(font, 13.0f, ImVec2(pz.x + 3.0f, pz.y - 10.0f), IM_COL32(70, 110, 220, 255), "Z");
  }
}
}  // namespace
#endif

void DrawRuntimeHud(HDC hdc, const RECT& vrc, const ModelPreviewState& st) {
  const RECT panel{vrc.left + 10, vrc.top + 10, vrc.left + 560, vrc.top + 108};
  HBRUSH bg = CreateSolidBrush(RGB(250, 250, 250));
  FillRect(hdc, &panel, bg);
  DeleteObject(bg);
  FrameRect(hdc, &panel, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(32, 42, 64));
  const std::wstring spark = BuildFrameMsSparkline(st);
  wchar_t line1[320]{};
  swprintf_s(line1, L"FPS %.1f | 帧时 %.2f ms | 瓶颈 %s", st.fps, st.lastFrameMs, st.runtimeBottleneck.c_str());
  TextOutW(hdc, panel.left + 8, panel.top + 6, line1, static_cast<int>(wcslen(line1)));
  wchar_t line2[320]{};
  swprintf_s(line2, L"曲线 %s", spark.c_str());
  TextOutW(hdc, panel.left + 8, panel.top + 30, line2, static_cast<int>(wcslen(line2)));
  const wchar_t* rendererText = L"D3D11";
#if AGIS_USE_BGFX
  rendererText = st.bgfxRenderer == AgisBgfxRendererKind::kOpenGL ? L"OpenGL" : L"D3D11";
#else
  rendererText = st.backend == PreviewRenderBackend::kOpenGL ? L"OpenGL" : L"D3D11";
#endif
  const wchar_t* texLayer = L"(none)";
  if (st.currentTextureLayer >= 0 && st.currentTextureLayer < static_cast<int>(st.textureLayers.size())) {
    texLayer = st.textureLayers[st.currentTextureLayer].first.c_str();
  }
  const wchar_t* pbrViewText = L"PBRLit";
  switch (st.pbrViewMode) {
    case AgisBgfxPbrViewMode::kAlbedo: pbrViewText = L"Albedo"; break;
    case AgisBgfxPbrViewMode::kNormal: pbrViewText = L"Normal"; break;
    case AgisBgfxPbrViewMode::kRoughness: pbrViewText = L"Roughness"; break;
    case AgisBgfxPbrViewMode::kMetallic: pbrViewText = L"Metallic"; break;
    case AgisBgfxPbrViewMode::kAo: pbrViewText = L"AO"; break;
    case AgisBgfxPbrViewMode::kPbrLit:
    default: break;
  }
  wchar_t line3[640]{};
  swprintf_s(line3, L"Renderer:%s | Solid:%s | Cull:%s | Grid:%s | Texture:%s | Render:%s | Layer:%s", rendererText,
             st.solid ? L"on" : L"off", st.backfaceCulling ? L"on" : L"off", st.showGrid ? L"on" : L"off",
             st.useTexture ? L"on" : L"off", pbrViewText, texLayer);
  TextOutW(hdc, panel.left + 8, panel.top + 54, line3, static_cast<int>(wcslen(line3)));
  const int vh = (std::max)(1L, vrc.bottom - vrc.top);
  const float unitsPerPx = (2.0f / (std::max)(0.05f, st.zoom)) / static_cast<float>((std::max)(1, vh));
  const float scale100 = unitsPerPx * 100.0f;
  wchar_t line4[320]{};
  swprintf_s(line4, L"缩放倍数: x%.2f | 比例尺: 100px≈%.4f 模型单位 | 网格总长: 2.0 模型单位", st.zoom, scale100);
  TextOutW(hdc, panel.left + 8, panel.top + 78, line4, static_cast<int>(wcslen(line4)));
  const auto buttons = BuildPreviewHudButtons(vrc);
  HBRUSH bbg = CreateSolidBrush(RGB(236, 242, 250));
  HBRUSH bOn = CreateSolidBrush(RGB(208, 228, 255));
  HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, bbg));
  HPEN border = CreatePen(PS_SOLID, 1, RGB(120, 138, 168));
  HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(hdc, border));
  SetTextColor(hdc, RGB(28, 40, 62));
  SetBkMode(hdc, TRANSPARENT);
  for (const auto& b : buttons) {
    bool on = false;
    if (b.key == 'G') on = st.showGrid;
    if (b.key == 'T') on = st.useTexture;
    if (b.key == 'P') on = st.pseudoPbrMode;
    if (b.key == 'B') on = st.backfaceCulling;
    FillRect(hdc, &b.rc, on ? bOn : bbg);
    Rectangle(hdc, b.rc.left, b.rc.top, b.rc.right, b.rc.bottom);
    TextOutW(hdc, b.rc.left + 6, b.rc.top + 5, b.label, static_cast<int>(wcslen(b.label)));
  }
  SelectObject(hdc, oldPen);
  DeleteObject(border);
  SelectObject(hdc, oldBrush);
  DeleteObject(bOn);
  DeleteObject(bbg);
}

#if !AGIS_USE_BGFX
template <typename T>
void SafeRelease(T*& p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

bool InitPreviewGl(HWND hwnd, ModelPreviewState* st) {
  if (!st || st->glRc) {
    return st && st->glRc;
  }
  st->glHdc = GetDC(hwnd);
  if (!st->glHdc) {
    return false;
  }
  PIXELFORMATDESCRIPTOR pfd{};
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cDepthBits = 24;
  const int pf = ChoosePixelFormat(st->glHdc, &pfd);
  if (!pf || !SetPixelFormat(st->glHdc, pf, &pfd)) {
    ReleaseDC(hwnd, st->glHdc);
    st->glHdc = nullptr;
    return false;
  }
  st->glRc = wglCreateContext(st->glHdc);
  if (!st->glRc) {
    ReleaseDC(hwnd, st->glHdc);
    st->glHdc = nullptr;
    return false;
  }
  return true;
}

void ReleasePreviewGl(HWND hwnd, ModelPreviewState* st) {
  if (!st) {
    return;
  }
  if (st->glRc) {
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(st->glRc);
    st->glRc = nullptr;
  }
  if (st->glHdc) {
    ReleaseDC(hwnd, st->glHdc);
    st->glHdc = nullptr;
  }
}

bool RecreatePreviewDxRtv(ModelPreviewState* st) {
  if (!st || !st->d3dSwap || !st->d3dDev) {
    return false;
  }
  SafeRelease(st->d3dRtv);
  ID3D11Texture2D* bb = nullptr;
  HRESULT hr = st->d3dSwap->GetBuffer(0, IID_PPV_ARGS(&bb));
  if (FAILED(hr) || !bb) {
    return false;
  }
  hr = st->d3dDev->CreateRenderTargetView(bb, nullptr, &st->d3dRtv);
  bb->Release();
  return SUCCEEDED(hr) && st->d3dRtv;
}

void ReleasePreviewDx(ModelPreviewState* st) {
  if (!st) return;
  SafeRelease(st->d3dDsv);
  SafeRelease(st->d3dDsTex);
  SafeRelease(st->d3dDsState);
  SafeRelease(st->d3dRsWire);
  SafeRelease(st->d3dRsSolid);
  SafeRelease(st->d3dVb);
  SafeRelease(st->d3dCbMvp);
  st->d3dVbCap = 0;
  SafeRelease(st->d3dLayout);
  SafeRelease(st->d3dVs);
  SafeRelease(st->d3dPs);
  SafeRelease(st->d3dRtv);
  SafeRelease(st->d3dSwap);
  SafeRelease(st->d3dCtx);
  SafeRelease(st->d3dDev);
}

bool InitPreviewDx(HWND hwnd, ModelPreviewState* st) {
  if (!st) return false;
  if (st->d3dDev && st->d3dCtx && st->d3dSwap && st->d3dRtv) return true;
  ReleasePreviewDx(st);
  RECT rc{};
  GetClientRect(hwnd, &rc);
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 1;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.Width = static_cast<UINT>((std::max)(1L, rc.right - rc.left));
  sd.BufferDesc.Height = static_cast<UINT>((std::max)(1L, rc.bottom - rc.top));
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  D3D_FEATURE_LEVEL fls[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL outFl{};
  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fls, ARRAYSIZE(fls),
                                             D3D11_SDK_VERSION, &sd, &st->d3dSwap, &st->d3dDev, &outFl, &st->d3dCtx);
  if (FAILED(hr)) return false;
  if (!RecreatePreviewDxRtv(st)) return false;
  D3D11_TEXTURE2D_DESC dtd{};
  dtd.Width = sd.BufferDesc.Width;
  dtd.Height = sd.BufferDesc.Height;
  dtd.MipLevels = 1;
  dtd.ArraySize = 1;
  dtd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  dtd.SampleDesc.Count = 1;
  dtd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  if (FAILED(st->d3dDev->CreateTexture2D(&dtd, nullptr, &st->d3dDsTex))) return false;
  if (FAILED(st->d3dDev->CreateDepthStencilView(st->d3dDsTex, nullptr, &st->d3dDsv))) return false;
  D3D11_DEPTH_STENCIL_DESC dsd{};
  dsd.DepthEnable = TRUE;
  dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
  if (FAILED(st->d3dDev->CreateDepthStencilState(&dsd, &st->d3dDsState))) return false;
  D3D11_RASTERIZER_DESC rsSolid{};
  rsSolid.FillMode = D3D11_FILL_SOLID;
  rsSolid.CullMode = D3D11_CULL_BACK;
  rsSolid.FrontCounterClockwise = FALSE;
  rsSolid.DepthClipEnable = TRUE;
  if (FAILED(st->d3dDev->CreateRasterizerState(&rsSolid, &st->d3dRsSolid))) return false;
  D3D11_RASTERIZER_DESC rsWire = rsSolid;
  rsWire.FillMode = D3D11_FILL_WIREFRAME;
  rsWire.CullMode = D3D11_CULL_NONE;
  if (FAILED(st->d3dDev->CreateRasterizerState(&rsWire, &st->d3dRsWire))) return false;
  static const char* kVs = R"(
cbuffer CbMvp : register(b0) { float4x4 mvp; };
struct VS_IN { float3 pos : POSITION; float4 col : COLOR; };
struct VS_OUT { float4 pos : SV_POSITION; float4 col : COLOR; };
VS_OUT main(VS_IN i){
  VS_OUT o;
  o.pos = mul(float4(i.pos, 1.0f), mvp);
  o.col = i.col;
  return o;
})";
  static const char* kPs = R"(
float4 main(float4 pos:SV_POSITION, float4 col:COLOR) : SV_Target { return col; })";
  ID3DBlob* vsBlob = nullptr;
  ID3DBlob* psBlob = nullptr;
  ID3DBlob* errBlob = nullptr;
  hr = D3DCompile(kVs, strlen(kVs), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vsBlob, &errBlob);
  if (FAILED(hr) || !vsBlob) {
    SafeRelease(errBlob);
    return false;
  }
  hr = D3DCompile(kPs, strlen(kPs), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &psBlob, &errBlob);
  if (FAILED(hr) || !psBlob) {
    SafeRelease(vsBlob);
    SafeRelease(errBlob);
    return false;
  }
  D3D11_INPUT_ELEMENT_DESC ied[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  hr = st->d3dDev->CreateInputLayout(ied, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &st->d3dLayout);
  if (FAILED(hr)) {
    SafeRelease(vsBlob);
    SafeRelease(psBlob);
    return false;
  }
  hr = st->d3dDev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &st->d3dVs);
  if (FAILED(hr)) {
    SafeRelease(vsBlob);
    SafeRelease(psBlob);
    return false;
  }
  hr = st->d3dDev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &st->d3dPs);
  SafeRelease(vsBlob);
  SafeRelease(psBlob);
  if (FAILED(hr)) return false;
  D3D11_BUFFER_DESC cbd{};
  cbd.ByteWidth = sizeof(float) * 16;
  cbd.Usage = D3D11_USAGE_DYNAMIC;
  cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = st->d3dDev->CreateBuffer(&cbd, nullptr, &st->d3dCbMvp);
  return SUCCEEDED(hr);
}

#endif  // !AGIS_USE_BGFX

std::wstring TrimLeft(const std::wstring& s) {
  size_t i = 0;
  while (i < s.size() && iswspace(s[i])) ++i;
  return s.substr(i);
}

bool ParseMtlKdColor(const std::filesystem::path& mtlPath, float* r, float* g, float* b) {
  if (!r || !g || !b) return false;
  std::wifstream ifs(mtlPath);
  if (!ifs.is_open()) return false;
  std::wstring line;
  while (std::getline(ifs, line)) {
    line = TrimLeft(line);
    if (line.rfind(L"Kd ", 0) == 0) {
      std::wistringstream ss(line.substr(3));
      float rr = 0.0f, gg = 0.0f, bb = 0.0f;
      if (ss >> rr >> gg >> bb) {
        *r = std::clamp(rr, 0.0f, 1.0f);
        *g = std::clamp(gg, 0.0f, 1.0f);
        *b = std::clamp(bb, 0.0f, 1.0f);
        return true;
      }
    }
  }
  return false;
}

bool ParseMtlMaterials(const std::filesystem::path& mtlPath, std::vector<ObjPreviewModel::MaterialInfo>* out) {
  if (!out) return false;
  out->clear();
  std::wifstream ifs(mtlPath);
  if (!ifs.is_open()) return false;
  std::wstring line;
  int cur = -1;
  while (std::getline(ifs, line)) {
    line = TrimLeft(line);
    if (line.rfind(L"newmtl ", 0) == 0) {
      ObjPreviewModel::MaterialInfo m{};
      m.name = line.substr(7);
      out->push_back(m);
      cur = static_cast<int>(out->size()) - 1;
    } else if (cur >= 0 && line.rfind(L"Kd ", 0) == 0) {
      std::wistringstream ss(line.substr(3));
      float r = 0, g = 0, b = 0;
      if (ss >> r >> g >> b) {
        (*out)[cur].kdR = std::clamp(r, 0.0f, 1.0f);
        (*out)[cur].kdG = std::clamp(g, 0.0f, 1.0f);
        (*out)[cur].kdB = std::clamp(b, 0.0f, 1.0f);
      }
    } else if (cur >= 0 && line.rfind(L"map_Kd ", 0) == 0) {
      std::filesystem::path p = mtlPath.parent_path() / line.substr(7);
      (*out)[cur].mapKdPath = p.wstring();
    }
  }
  return !out->empty();
}

std::wstring ToHumanBytes(uint64_t bytes) {
  const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB"};
  double v = static_cast<double>(bytes);
  int idx = 0;
  while (v >= 1024.0 && idx < 3) {
    v /= 1024.0;
    ++idx;
  }
  wchar_t buf[64]{};
  swprintf_s(buf, L"%.2f %s", v, units[idx]);
  return buf;
}

double NowPerfMs() {
  static LARGE_INTEGER freq{};
  static bool init = false;
  if (!init) {
    QueryPerformanceFrequency(&freq);
    init = true;
  }
  LARGE_INTEGER t{};
  QueryPerformanceCounter(&t);
  return (static_cast<double>(t.QuadPart) * 1000.0) / static_cast<double>(freq.QuadPart);
}

std::wstring BuildFrameMsSparkline(const ModelPreviewState& st) {
  static constexpr wchar_t kLevel[] = L" .:-=+*#%@";
  if (st.frameMsHistoryCount <= 0) {
    return L"--";
  }
  float minV = st.frameMsHistory[0];
  float maxV = st.frameMsHistory[0];
  for (int i = 1; i < st.frameMsHistoryCount; ++i) {
    minV = (std::min)(minV, st.frameMsHistory[i]);
    maxV = (std::max)(maxV, st.frameMsHistory[i]);
  }
  const float span = (std::max)(0.001f, maxV - minV);
  std::wstring s;
  s.reserve(static_cast<size_t>(st.frameMsHistoryCount));
  const int start = st.frameMsHistoryCount < static_cast<int>(st.frameMsHistory.size()) ? 0 : st.frameMsHistoryPos;
  for (int i = 0; i < st.frameMsHistoryCount; ++i) {
    const int idx = (start + i) % static_cast<int>(st.frameMsHistory.size());
    const float v = st.frameMsHistory[idx];
    const float n = (v - minV) / span;
    const int lv = static_cast<int>(n * 9.0f);
    s.push_back(kLevel[(std::clamp)(lv, 0, 9)]);
  }
  return s;
}

std::wstring EvaluatePreviewBottleneck(const ModelPreviewState& st, float cpuFrameMs, float gpuFrameMs, uint32_t drawCalls,
                                       float waitSubmitMs, float waitRenderMs) {
  if (gpuFrameMs > cpuFrameMs + 2.0f && gpuFrameMs > 12.0f) {
    if (drawCalls > 50000) return L"GPU: DrawCall偏高";
    if (st.stats.faces > 1000000) return L"GPU: 三角面过多";
    return L"GPU负载偏高";
  }
  if (cpuFrameMs > gpuFrameMs + 2.0f && cpuFrameMs > 12.0f) {
    if (st.stats.faces > 1000000) return L"CPU: 几何准备压力大";
    return L"CPU负载偏高";
  }
  if (waitSubmitMs > 1.5f || waitRenderMs > 1.5f) {
    return L"同步等待/VSync";
  }
  if (st.lastFrameMs > 20.0f) {
    return L"渲染负载偏高";
  }
  return L"稳定";
}

ObjPreviewStats ScanObjStats(const std::wstring& path) {
  ObjPreviewStats s{};
  std::error_code ec;
  s.fileBytes = std::filesystem::file_size(path, ec);
  std::wifstream ifs(path);
  if (!ifs.is_open()) {
    return s;
  }
  std::set<std::wstring> mats;
  std::set<std::wstring> texs;
  std::wstring line;
  while (std::getline(ifs, line)) {
    line = TrimLeft(line);
    if (line.rfind(L"v ", 0) == 0) ++s.vertices;
    else if (line.rfind(L"vt ", 0) == 0) ++s.texcoords;
    else if (line.rfind(L"vn ", 0) == 0) ++s.normals;
    else if (line.rfind(L"f ", 0) == 0) ++s.faces;
    else if (line.rfind(L"usemtl ", 0) == 0) mats.insert(line.substr(7));
    else if (line.rfind(L"map_Kd ", 0) == 0) texs.insert(line.substr(7));
  }
  s.materials = mats.size();
  s.textures = texs.size();
  return s;
}

static void SplitObjFaceToken(const std::wstring& tok, std::wstring* vOut, std::wstring* vtOut, std::wstring* vnOut) {
  *vOut = *vtOut = *vnOut = L"";
  const size_t p1 = tok.find(L'/');
  if (p1 == std::wstring::npos) {
    *vOut = tok;
    return;
  }
  *vOut = tok.substr(0, p1);
  const size_t p2 = tok.find(L'/', p1 + 1);
  if (p2 == std::wstring::npos) {
    *vtOut = tok.substr(p1 + 1);
    return;
  }
  *vtOut = tok.substr(p1 + 1, p2 - p1 - 1);
  *vnOut = tok.substr(p2 + 1);
}

/// OBJ 1-based 索引；负索引相对当前列表末尾；返回 0-based，非法为 -1。
static int ResolveObjIndex(const std::wstring& s, int count) {
  if (s.empty()) {
    return -1;
  }
  int i = _wtoi(s.c_str());
  if (i == 0) {
    return -1;
  }
  if (i < 0) {
    i = count + i + 1;
  }
  if (i <= 0 || i > count) {
    return -1;
  }
  return i - 1;
}

bool ParseObjModel(const std::wstring& path, ObjPreviewModel* out, int* progressPct) {
  if (!out) {
    return false;
  }
  out->vertices.clear();
  out->texcoords.clear();
  out->faces.clear();
  out->faceTexcoord.clear();
  out->faceMaterial.clear();
  out->materials.clear();
  out->hasVertexTexcoords = false;
  out->center = {};
  out->extentHoriz = 1.0f;
  out->extent = 1.0f;
  out->kdR = 0.30f;
  out->kdG = 0.62f;
  out->kdB = 0.92f;
  out->primaryMapKdPath.clear();
  std::wifstream ifs(path);
  if (!ifs.is_open()) {
    return false;
  }
  uintmax_t fileBytes = 0;
  std::error_code fec;
  fileBytes = std::filesystem::file_size(path, fec);
  if (fec) fileBytes = 0;
  if (progressPct) *progressPct = 0;
  std::wstring line;
  std::filesystem::path objPath(path);
  int curMtlIndex = -1;
  PreviewVec3 vmin{1e9f, 1e9f, 1e9f};
  PreviewVec3 vmax{-1e9f, -1e9f, -1e9f};
  size_t lineCounter = 0;
  while (std::getline(ifs, line)) {
    if (progressPct) {
      ++lineCounter;
      if ((lineCounter & 0x1FF) == 0) {
        const std::streampos pos = ifs.tellg();
        if (fileBytes > 0 && pos != std::streampos(-1)) {
          const auto readNow = static_cast<uintmax_t>(pos);
          const int pct = static_cast<int>((std::min<uintmax_t>)(100, (readNow * 100) / fileBytes));
          *progressPct = (std::clamp)(pct, 0, 100);
        }
      }
    }
    line = TrimLeft(line);
    if (line.rfind(L"v ", 0) == 0) {
      std::wistringstream ss(line.substr(2));
      PreviewVec3 v{};
      ss >> v.x >> v.y >> v.z;
      out->vertices.push_back(v);
      vmin.x = (std::min)(vmin.x, v.x);
      vmin.y = (std::min)(vmin.y, v.y);
      vmin.z = (std::min)(vmin.z, v.z);
      vmax.x = (std::max)(vmax.x, v.x);
      vmax.y = (std::max)(vmax.y, v.y);
      vmax.z = (std::max)(vmax.z, v.z);
    } else if (line.rfind(L"vt ", 0) == 0) {
      std::wistringstream ss(line.substr(3));
      PreviewVec2 t{};
      ss >> t.u >> t.v;
      out->texcoords.push_back(t);
    } else if (line.rfind(L"mtllib ", 0) == 0) {
      std::filesystem::path mtl = objPath.parent_path() / line.substr(7);
      ParseMtlKdColor(mtl, &out->kdR, &out->kdG, &out->kdB);
      ParseMtlMaterials(mtl, &out->materials);
      if (!out->materials.empty() && !out->materials[0].mapKdPath.empty()) {
        out->primaryMapKdPath = out->materials[0].mapKdPath;
      }
    } else if (line.rfind(L"usemtl ", 0) == 0) {
      const std::wstring name = line.substr(7);
      curMtlIndex = -1;
      for (size_t i = 0; i < out->materials.size(); ++i) {
        if (out->materials[i].name == name) {
          curMtlIndex = static_cast<int>(i);
          if (!out->materials[i].mapKdPath.empty()) {
            out->primaryMapKdPath = out->materials[i].mapKdPath;
          }
          break;
        }
      }
    } else if (line.rfind(L"f ", 0) == 0) {
      std::wistringstream ss(line.substr(2));
      std::wstring tok;
      struct Corner {
        int vi;
        int ti;
      };
      std::vector<Corner> poly;
      while (ss >> tok) {
        std::wstring vs, vts, vns;
        SplitObjFaceToken(tok, &vs, &vts, &vns);
        (void)vns;
        if (vs.empty()) {
          continue;
        }
        const int vi = ResolveObjIndex(vs, static_cast<int>(out->vertices.size()));
        if (vi < 0) {
          continue;
        }
        int ti = -1;
        if (!vts.empty()) {
          ti = ResolveObjIndex(vts, static_cast<int>(out->texcoords.size()));
          if (ti >= 0) {
            out->hasVertexTexcoords = true;
          }
        }
        poly.push_back({vi, ti});
      }
      if (poly.size() >= 3) {
        for (size_t i = 1; i + 1 < poly.size(); ++i) {
          out->faces.push_back({poly[0].vi, poly[i].vi, poly[i + 1].vi});
          out->faceTexcoord.push_back({poly[0].ti, poly[i].ti, poly[i + 1].ti});
          out->faceMaterial.push_back(curMtlIndex);
        }
      }
    }
  }
  if (!out->vertices.empty()) {
    out->center = {(vmin.x + vmax.x) * 0.5f, (vmin.y + vmax.y) * 0.5f, (vmin.z + vmax.z) * 0.5f};
    const float ex = (std::max)(1e-6f, vmax.x - vmin.x);
    const float ey = (std::max)(1e-6f, vmax.y - vmin.y);
    const float ez = (std::max)(1e-6f, vmax.z - vmin.z);
    out->extentHoriz = (std::max)((std::max)(ex, ey), 1e-6f);
    out->extent = (std::max)(out->extentHoriz, ez);
  }
  if (progressPct) *progressPct = 100;
  return !out->vertices.empty() && !out->faces.empty();
}

#if !AGIS_USE_BGFX
POINT ProjectPoint(const PreviewVec3& v, float rotX, float rotY, float zoom, const RECT& rc) {
  const float cx = std::cos(rotX), sx = std::sin(rotX);
  const float cy = std::cos(rotY), sy = std::sin(rotY);
  float x1 = v.x;
  float y1 = v.y * cx - v.z * sx;
  float z1 = v.y * sx + v.z * cx;
  float x2 = x1 * cy + z1 * sy;
  float z2 = -x1 * sy + z1 * cy;
  const float halfW = static_cast<float>((std::max)(1, static_cast<int>((rc.right - rc.left) / 2)));
  const float halfH = static_cast<float>((std::max)(1, static_cast<int>((rc.bottom - rc.top) / 2)));
  const float persp = 1.8f / (z2 + 3.5f);
  POINT p{};
  p.x = rc.left + static_cast<int>(halfW + x2 * halfW * zoom * persp);
  p.y = rc.top + static_cast<int>(halfH - y1 * halfH * zoom * persp);
  return p;
}

void DrawModelPreview(HDC hdc, const RECT& rc, const ModelPreviewState& st) {
  // OpenGL 风格：偏清亮底色 + 蓝色实体；DX11 风格：偏深底色 + 青蓝线框。
  const bool glStyle = st.backend == PreviewRenderBackend::kOpenGL;
  const COLORREF bgColor = glStyle ? RGB(245, 248, 252) : RGB(31, 35, 42);
  const COLORREF edgeColor = glStyle ? RGB(36, 82, 156) : RGB(112, 196, 255);
  const COLORREF fillColor =
      RGB(static_cast<int>(st.model.kdR * 255.0f), static_cast<int>(st.model.kdG * 255.0f), static_cast<int>(st.model.kdB * 255.0f));
  HBRUSH bg = CreateSolidBrush(RGB(245, 248, 252));
  DeleteObject(bg);
  bg = CreateSolidBrush(bgColor);
  FillRect(hdc, &rc, bg);
  DeleteObject(bg);
  HPEN pen = CreatePen(PS_SOLID, glStyle ? 1 : 2, edgeColor);
  HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(hdc, pen));
  HBRUSH solidBrush = CreateSolidBrush(fillColor);
  const bool doFill = st.solid && glStyle;
  HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(hdc, doFill ? solidBrush : GetStockObject(NULL_BRUSH)));
  SetBkMode(hdc, TRANSPARENT);
  if (!glStyle) {
    SetPolyFillMode(hdc, WINDING);
  }
  for (const auto& f : st.model.faces) {
    if (f[0] < 0 || f[1] < 0 || f[2] < 0 || f[0] >= static_cast<int>(st.model.vertices.size()) ||
        f[1] >= static_cast<int>(st.model.vertices.size()) || f[2] >= static_cast<int>(st.model.vertices.size())) {
      continue;
    }
    POINT pts[3] = {ProjectPoint({st.model.vertices[f[0]].x - st.model.center.x, st.model.vertices[f[0]].y - st.model.center.y,
                                  st.model.vertices[f[0]].z - st.model.center.z},
                                 st.rotX, st.rotY, st.zoom, rc),
                    ProjectPoint({st.model.vertices[f[1]].x - st.model.center.x, st.model.vertices[f[1]].y - st.model.center.y,
                                  st.model.vertices[f[1]].z - st.model.center.z},
                                 st.rotX, st.rotY, st.zoom, rc),
                    ProjectPoint({st.model.vertices[f[2]].x - st.model.center.x, st.model.vertices[f[2]].y - st.model.center.y,
                                  st.model.vertices[f[2]].z - st.model.center.z},
                                 st.rotX, st.rotY, st.zoom, rc)};
    if (glStyle) {
      Polygon(hdc, pts, 3);
    } else {
      MoveToEx(hdc, pts[0].x, pts[0].y, nullptr);
      LineTo(hdc, pts[1].x, pts[1].y);
      LineTo(hdc, pts[2].x, pts[2].y);
      LineTo(hdc, pts[0].x, pts[0].y);
    }
  }
  SelectObject(hdc, oldBrush);
  SelectObject(hdc, oldPen);
  DeleteObject(solidBrush);
  DeleteObject(pen);
}

void DrawModelPreviewOpenGL(HWND hwnd, const RECT& rc, const ModelPreviewState& st) {
  if (!st.glHdc || !st.glRc) {
    return;
  }
  if (!wglMakeCurrent(st.glHdc, st.glRc)) {
    return;
  }
  RECT cr{};
  GetClientRect(hwnd, &cr);
  int vpW = static_cast<int>(cr.right - cr.left);
  int vpH = static_cast<int>(cr.bottom - cr.top);
  if (vpW < 1) vpW = 1;
  if (vpH < 1) vpH = 1;
  glViewport(0, 0, vpW, vpH);
  glClearColor(0.10f, 0.12f, 0.16f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  int rw = static_cast<int>(rc.right - rc.left);
  int rh = static_cast<int>(rc.bottom - rc.top);
  if (rw < 1) rw = 1;
  if (rh < 1) rh = 1;
  const double w = static_cast<double>(rw);
  const double h = static_cast<double>(rh);
  const double ar = w / h;
  glFrustum(-ar * 0.3, ar * 0.3, -0.3, 0.3, 0.7, 100.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glTranslated(0.0, 0.0, -3.5);
  glRotated(st.rotX * 57.29578, 1.0, 0.0, 0.0);
  glRotated(st.rotY * 57.29578, 0.0, 1.0, 0.0);
  const float normScale = 1.0f / (std::max)(0.001f, st.model.extent);
  glScaled(st.zoom * normScale, st.zoom * normScale, st.zoom * normScale);
  glTranslated(-st.model.center.x, -st.model.center.y, -st.model.center.z);

  const size_t faceStride = ModelPreviewFaceStride(st.model.faces.size());
  if (st.solid) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glBegin(GL_TRIANGLES);
    for (size_t fi = 0; fi < st.model.faces.size(); fi += faceStride) {
      const auto& f = st.model.faces[fi];
      if (f[0] < 0 || f[1] < 0 || f[2] < 0 || f[0] >= static_cast<int>(st.model.vertices.size()) ||
          f[1] >= static_cast<int>(st.model.vertices.size()) || f[2] >= static_cast<int>(st.model.vertices.size())) {
        continue;
      }
      float cr = st.model.kdR, cg = st.model.kdG, cb = st.model.kdB;
      if (fi < st.model.faceMaterial.size()) {
        const int mi = st.model.faceMaterial[fi];
        if (mi >= 0 && mi < static_cast<int>(st.model.materials.size())) {
          cr = st.model.materials[mi].kdR;
          cg = st.model.materials[mi].kdG;
          cb = st.model.materials[mi].kdB;
        }
      }
      glColor3f(cr, cg, cb);
      const auto& va = st.model.vertices[f[0]];
      const auto& vb = st.model.vertices[f[1]];
      const auto& vc = st.model.vertices[f[2]];
      glVertex3f(va.x, va.y, va.z);
      glVertex3f(vb.x, vb.y, vb.z);
      glVertex3f(vc.x, vc.y, vc.z);
    }
    glEnd();
  }
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  glColor3f((std::min)(1.0f, st.model.kdR + 0.20f), (std::min)(1.0f, st.model.kdG + 0.20f),
            (std::min)(1.0f, st.model.kdB + 0.20f));
  glBegin(GL_TRIANGLES);
  for (size_t fi = 0; fi < st.model.faces.size(); fi += faceStride) {
    const auto& f = st.model.faces[fi];
    if (f[0] < 0 || f[1] < 0 || f[2] < 0 || f[0] >= static_cast<int>(st.model.vertices.size()) ||
        f[1] >= static_cast<int>(st.model.vertices.size()) || f[2] >= static_cast<int>(st.model.vertices.size())) {
      continue;
    }
    const auto& a = st.model.vertices[f[0]];
    const auto& b = st.model.vertices[f[1]];
    const auto& c = st.model.vertices[f[2]];
    glVertex3f(a.x, a.y, a.z);
    glVertex3f(b.x, b.y, b.z);
    glVertex3f(c.x, c.y, c.z);
  }
  glEnd();
  SwapBuffers(st.glHdc);
  wglMakeCurrent(nullptr, nullptr);
}

struct DxLineVertex {
  float x, y, z;
  float r, g, b, a;
};

void DrawModelPreviewDx11(HWND hwnd, const RECT& rc, ModelPreviewState* st) {
  if (!st || !InitPreviewDx(hwnd, st) || !st->d3dCtx || !st->d3dSwap || !st->d3dRtv) return;
  RECT cr{};
  GetClientRect(hwnd, &cr);
  UINT w = static_cast<UINT>((std::max)(1L, cr.right - cr.left));
  UINT h = static_cast<UINT>((std::max)(1L, cr.bottom - cr.top));
  DXGI_SWAP_CHAIN_DESC sd{};
  st->d3dSwap->GetDesc(&sd);
  if (sd.BufferDesc.Width != w || sd.BufferDesc.Height != h) {
    st->d3dCtx->OMSetRenderTargets(0, nullptr, nullptr);
    SafeRelease(st->d3dRtv);
    SafeRelease(st->d3dDsv);
    SafeRelease(st->d3dDsTex);
    if (SUCCEEDED(st->d3dSwap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) {
      RecreatePreviewDxRtv(st);
      D3D11_TEXTURE2D_DESC dtd{};
      dtd.Width = w;
      dtd.Height = h;
      dtd.MipLevels = 1;
      dtd.ArraySize = 1;
      dtd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
      dtd.SampleDesc.Count = 1;
      dtd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
      st->d3dDev->CreateTexture2D(&dtd, nullptr, &st->d3dDsTex);
      if (st->d3dDsTex) st->d3dDev->CreateDepthStencilView(st->d3dDsTex, nullptr, &st->d3dDsv);
    }
  }
  using namespace DirectX;
  std::vector<DxLineVertex> triVerts;
  std::vector<DxLineVertex> lineVerts;
  if (st->solid) {
    triVerts.reserve(st->model.faces.size() * 3);
  }
  lineVerts.reserve(st->model.faces.size() * 6);
  const float normScale = 1.0f / (std::max)(0.001f, st->model.extent);
  const float cx = std::cos(st->rotX), sx = std::sin(st->rotX);
  const float cy = std::cos(st->rotY), sy = std::sin(st->rotY);
  auto rotate = [&](const PreviewVec3& v) {
    PreviewVec3 o{};
    float x1 = v.x;
    float y1 = v.y * cx - v.z * sx;
    float z1 = v.y * sx + v.z * cx;
    o.x = x1 * cy + z1 * sy;
    o.y = y1;
    o.z = -x1 * sy + z1 * cy;
    return o;
  };
  const size_t faceStride = ModelPreviewFaceStride(st->model.faces.size());
  for (size_t fidx = 0; fidx < st->model.faces.size(); fidx += faceStride) {
    const auto& f = st->model.faces[fidx];
    if (f[0] < 0 || f[1] < 0 || f[2] < 0 || f[0] >= static_cast<int>(st->model.vertices.size()) ||
        f[1] >= static_cast<int>(st->model.vertices.size()) || f[2] >= static_cast<int>(st->model.vertices.size())) {
      continue;
    }
    PreviewVec3 va = st->model.vertices[f[0]];
    PreviewVec3 vb = st->model.vertices[f[1]];
    PreviewVec3 vc = st->model.vertices[f[2]];
    va.x = (va.x - st->model.center.x) * normScale;
    va.y = (va.y - st->model.center.y) * normScale;
    va.z = (va.z - st->model.center.z) * normScale;
    vb.x = (vb.x - st->model.center.x) * normScale;
    vb.y = (vb.y - st->model.center.y) * normScale;
    vb.z = (vb.z - st->model.center.z) * normScale;
    vc.x = (vc.x - st->model.center.x) * normScale;
    vc.y = (vc.y - st->model.center.y) * normScale;
    vc.z = (vc.z - st->model.center.z) * normScale;
    const float lr = 0.45f, lg = 0.82f, lb = 1.0f, la = 1.0f;
    if (st->solid) {
      const PreviewVec3 a3 = rotate(va);
      const PreviewVec3 b3 = rotate(vb);
      const PreviewVec3 c3 = rotate(vc);
      const float ux = b3.x - a3.x, uy = b3.y - a3.y, uz = b3.z - a3.z;
      const float vx = c3.x - a3.x, vy = c3.y - a3.y, vz = c3.z - a3.z;
      float nx = uy * vz - uz * vy;
      float ny = uz * vx - ux * vz;
      float nz = ux * vy - uy * vx;
      float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
      if (nl < 1e-6f) nl = 1.0f;
      nx /= nl; ny /= nl; nz /= nl;
      const float lx = -0.35f, ly = 0.5f, lz = 0.78f;
      const float diffuse = (std::max)(0.0f, nx * lx + ny * ly + nz * lz);
      const float shade = 0.20f + 0.80f * diffuse;
      float mr = st->model.kdR, mg = st->model.kdG, mb = st->model.kdB;
      if (fidx < st->model.faceMaterial.size()) {
        const int mi = st->model.faceMaterial[fidx];
        if (mi >= 0 && mi < static_cast<int>(st->model.materials.size())) {
          mr = st->model.materials[mi].kdR;
          mg = st->model.materials[mi].kdG;
          mb = st->model.materials[mi].kdB;
        }
      }
      const float fr = mr * shade, fg = mg * shade, fb = mb * shade, fa = 1.0f;
      triVerts.push_back({va.x, va.y, va.z, fr, fg, fb, fa});
      triVerts.push_back({vb.x, vb.y, vb.z, fr, fg, fb, fa});
      triVerts.push_back({vc.x, vc.y, vc.z, fr, fg, fb, fa});
    }
    lineVerts.push_back({va.x, va.y, va.z, lr, lg, lb, la});
    lineVerts.push_back({vb.x, vb.y, vb.z, lr, lg, lb, la});
    lineVerts.push_back({vb.x, vb.y, vb.z, lr, lg, lb, la});
    lineVerts.push_back({vc.x, vc.y, vc.z, lr, lg, lb, la});
    lineVerts.push_back({vc.x, vc.y, vc.z, lr, lg, lb, la});
    lineVerts.push_back({va.x, va.y, va.z, lr, lg, lb, la});
  }
  std::vector<DxLineVertex> allVerts;
  allVerts.reserve(triVerts.size() + lineVerts.size());
  allVerts.insert(allVerts.end(), triVerts.begin(), triVerts.end());
  const UINT triCount = static_cast<UINT>(triVerts.size());
  allVerts.insert(allVerts.end(), lineVerts.begin(), lineVerts.end());
  const UINT needBytes = static_cast<UINT>(allVerts.size() * sizeof(DxLineVertex));
  if (needBytes > 0 && needBytes > st->d3dVbCap) {
    SafeRelease(st->d3dVb);
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.ByteWidth = needBytes + 4096;
    if (SUCCEEDED(st->d3dDev->CreateBuffer(&bd, nullptr, &st->d3dVb))) {
      st->d3dVbCap = bd.ByteWidth;
    }
  }
  if (needBytes > 0 && st->d3dVb) {
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (SUCCEEDED(st->d3dCtx->Map(st->d3dVb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
      memcpy(ms.pData, allVerts.data(), needBytes);
      st->d3dCtx->Unmap(st->d3dVb, 0);
    }
  }
  FLOAT clear[4] = {0.10f, 0.12f, 0.16f, 1.0f};
  st->d3dCtx->OMSetRenderTargets(1, &st->d3dRtv, st->d3dDsv);
  if (st->d3dDsState) st->d3dCtx->OMSetDepthStencilState(st->d3dDsState, 0);
  D3D11_VIEWPORT vp{};
  vp.Width = static_cast<float>(w);
  vp.Height = static_cast<float>(h);
  vp.MinDepth = 0;
  vp.MaxDepth = 1;
  st->d3dCtx->RSSetViewports(1, &vp);
  st->d3dCtx->ClearRenderTargetView(st->d3dRtv, clear);
  if (st->d3dDsv) st->d3dCtx->ClearDepthStencilView(st->d3dDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
  const float aspect = (h > 0) ? (static_cast<float>(w) / static_cast<float>(h)) : 1.0f;
  XMMATRIX world = XMMatrixRotationX(st->rotX) * XMMatrixRotationY(st->rotY) * XMMatrixScaling(st->zoom, st->zoom, st->zoom);
  XMMATRIX view = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
  XMMATRIX proj = XMMatrixPerspectiveFovLH(0.75f, aspect, 0.1f, 40.0f);
  XMMATRIX mvp = XMMatrixTranspose(world * view * proj);
  if (st->d3dCbMvp) {
    D3D11_MAPPED_SUBRESOURCE msCb{};
    if (SUCCEEDED(st->d3dCtx->Map(st->d3dCbMvp, 0, D3D11_MAP_WRITE_DISCARD, 0, &msCb))) {
      memcpy(msCb.pData, &mvp, sizeof(mvp));
      st->d3dCtx->Unmap(st->d3dCbMvp, 0);
    }
  }
  if (st->d3dVb && !allVerts.empty()) {
    UINT stride = sizeof(DxLineVertex), off = 0;
    st->d3dCtx->IASetInputLayout(st->d3dLayout);
    st->d3dCtx->IASetVertexBuffers(0, 1, &st->d3dVb, &stride, &off);
    st->d3dCtx->VSSetShader(st->d3dVs, nullptr, 0);
    st->d3dCtx->PSSetShader(st->d3dPs, nullptr, 0);
    if (st->d3dCbMvp) {
      st->d3dCtx->VSSetConstantBuffers(0, 1, &st->d3dCbMvp);
    }
    if (triCount > 0) {
      if (st->d3dRsSolid) st->d3dCtx->RSSetState(st->d3dRsSolid);
      st->d3dCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      st->d3dCtx->Draw(triCount, 0);
    }
    if (st->d3dRsWire) st->d3dCtx->RSSetState(st->d3dRsWire);
    st->d3dCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    st->d3dCtx->Draw(static_cast<UINT>(lineVerts.size()), triCount);
  }
  st->d3dSwap->Present(1, 0);
}

#endif  // !AGIS_USE_BGFX

RECT GetPreviewViewportRect(HWND hwnd) {
  RECT rc{};
  GetClientRect(hwnd, &rc);
  // 预览窗口采用渲染内 UI 时，3D 视口尽量占满客户区。
  const int margin = 6;
  rc.left = margin;
  rc.top = margin;
  rc.right = (std::max)(rc.left + 40L, rc.right - margin);
  rc.bottom = (std::max)(rc.top + 40L, rc.bottom - margin);
  return rc;
}

std::wstring BuildObjInfoText(const std::wstring& path) {
  const ObjPreviewStats st = ScanObjStats(path);
  const uint64_t estMem = st.vertices * 32 + st.texcoords * 8 + st.normals * 12 + st.faces * 16;
  const uint64_t estVram = estMem * 3 / 2;
  std::wstringstream ss;
  ss << L"文件: " << path << L"\r\n";
  ss << L"文件体积: " << ToHumanBytes(st.fileBytes) << L"\r\n\r\n";
  ss << L"[Mesh 信息]\r\n";
  ss << L"顶点数: " << st.vertices << L"\r\n";
  ss << L"纹理坐标数: " << st.texcoords << L"\r\n";
  ss << L"法线数: " << st.normals << L"\r\n";
  ss << L"面片数: " << st.faces << L"\r\n\r\n";
  ss << L"[材质/贴图信息]\r\n";
  ss << L"材质数量(usemtl): " << st.materials << L"\r\n";
  ss << L"贴图数量(map_Kd): " << st.textures << L"\r\n\r\n";
  ss << L"[资源占用估算]\r\n";
  ss << L"内存占用(估算): " << ToHumanBytes(estMem) << L"\r\n";
  ss << L"显存占用(估算): " << ToHumanBytes(estVram) << L"\r\n";
  return ss.str();
}

static std::wstring BuildModelPreviewInfoText(const ModelPreviewState& st) {
  if (st.sourceIs3DTiles) {
    std::wstringstream ss;
    ss << L"3D Tiles（内建 glTF 解析，无 vcpkg）\r\n";
    ss << L"路径: " << st.path << L"\r\n\r\n";
    ss << L"顶点: " << st.stats.vertices << L"\r\n面片: " << st.stats.faces << L"\r\n";
    ss << L"材质槽: " << st.stats.materials << L"\r\n";
    ss << L"\r\n说明: 合并遍历 tileset 中本地 b3dm/i3dm/glb/cmpt；Draco、仅 http 外链、pnts 等跳过。\r\n";
    return ss.str();
  }
  if (st.lasSourcePointCount > 0) {
    std::wstringstream ss;
    ss << L"文件: " << st.path << L"\r\n";
    std::error_code ec;
    const uint64_t fb = std::filesystem::file_size(st.path, ec);
    if (!ec) {
      ss << L"文件体积: " << ToHumanBytes(fb) << L"\r\n\r\n";
    } else {
      ss << L"\r\n";
    }
    ss << L"[点云 LAS/LAZ]\r\n";
    ss << L"源点数: " << st.lasSourcePointCount << L"\r\n";
    ss << L"点斑近似像素: " << st.lasPointScreenPx << L" px（每点 2 三角/XY 点精灵；非 GPU PT_POINTS，便于稳定控制大小）\r\n";
    ss << L"预览三角面: " << st.model.faces.size()
       << L"（已按上限下采样；颜色：LAS/经 LASzip 读的 LAZ 取 PDRF 2/3/5/7 的 RGB；经 GDAL 的 LAZ 依赖属性字段）\r\n";
    return ss.str();
  }
  return BuildObjInfoText(st.path);
}

void FitPreviewCamera(ModelPreviewState* st) {
  if (!st) return;
  st->rotX = 0.0f;
  st->rotY = 0.0f;
  st->zoom = 2.8f;
}

DWORD WINAPI PreviewLoadThreadProc(LPVOID param) {
  auto* ctx = reinterpret_cast<PreviewLoadCtx*>(param);
  if (!ctx || !ctx->st) return 1;
  ModelPreviewState* st = ctx->st;
  st->lasSourcePointCount = 0;
  st->lasPointCache.clear();
  if (st->path.empty()) {
    // 无参数启动：直接进入空场景待机，不报错不退出。
    st->loadedModel = ObjPreviewModel{};
    st->loadedStats = ObjPreviewStats{};
    st->textureLayers.clear();
    st->sourceIs3DTiles = false;
    st->loadFailed = false;
    st->loadStage = 4;
    st->loadProgress = 100;
    PostMessageW(ctx->hwnd, kPreviewLoadedMsg, 1, 0);
    delete ctx;
    return 0;
  }

  if (st->loadAs3DTiles) {
    st->tilesLoadDiag.clear();
    st->loadStage = 1;
    st->loadProgress = 2;
    std::wstring err;
    if (!AgisLoad3DTilesForPreview(st->path, &st->loadedModel, &err, &st->loadProgress)) {
      st->loadFailed = true;
      st->tilesLoadDiag = std::move(err);
      st->loadStage = 9;
      st->loadProgress = 100;
      PostMessageW(ctx->hwnd, kPreviewLoadedMsg, 0, 0);
      delete ctx;
      return 2;
    }
    if (st->loadedModel.faces.size() > kPreviewObjFaceHardLimit) {
      st->loadedStats.faces = st->loadedModel.faces.size();
      st->loadStage = 9;
      st->loadProgress = 100;
      PostMessageW(ctx->hwnd, kPreviewLoadedMsg, 3, 0);
      delete ctx;
      return 2;
    }
    st->loadedStats = {};
    st->loadedStats.vertices = st->loadedModel.vertices.size();
    st->loadedStats.faces = st->loadedModel.faces.size();
    st->loadedStats.materials = st->loadedModel.materials.size();
    st->loadedStats.texcoords = st->loadedModel.texcoords.size();
    st->textureLayers = DetectTextureLayers(st->loadedModel);
    st->sourceIs3DTiles = true;
    st->loadStage = 4;
    st->loadProgress = 100;
    PostMessageW(ctx->hwnd, kPreviewLoadedMsg, 1, 0);
    delete ctx;
    return 0;
  }

  if (PreviewPathIsPointCloudFile(st->path)) {
    st->lazPreviewDiag.clear();
    st->loadStage = 1;
    st->loadProgress = 3;
    std::vector<LasPointFltRaw> pts;
    if (PreviewPathIsLasFile(st->path)) {
      std::uint32_t nrec = 0;
      if (PeekLasRecordCount(st->path, &nrec) != 0) {
        st->loadFailed = true;
        st->loadStage = 9;
        st->loadProgress = 100;
        PostMessageW(ctx->hwnd, kPreviewLoadedMsg, 0, 0);
        delete ctx;
        return 2;
      }
      if (nrec > kLasPreviewMaxSourcePoints) {
        st->loadStage = 9;
        st->loadProgress = 100;
        PostMessageW(ctx->hwnd, kPreviewLoadedMsg, 4, 0);
        delete ctx;
        return 2;
      }
      if (!ReadLasPointsPreview(st->path, &pts, &st->loadProgress)) {
        st->loadFailed = true;
        st->loadStage = 9;
        st->loadProgress = 100;
        PostMessageW(ctx->hwnd, kPreviewLoadedMsg, 0, 0);
        delete ctx;
        return 2;
      }
    } else {
      bool lazOk = false;
#if defined(AGIS_HAVE_LASZIP) && AGIS_HAVE_LASZIP
      std::uint64_t nLaz = 0;
      if (PeekLazPointCountLaszip(st->path, &nLaz, &st->lazPreviewDiag) == 0) {
        if (nLaz > kLasPreviewMaxSourcePoints) {
          st->loadStage = 9;
          st->loadProgress = 100;
          PostMessageW(ctx->hwnd, kPreviewLoadedMsg, 4, 0);
          delete ctx;
          return 2;
        }
        lazOk = ReadLazPointsLaszip(st->path, &pts, &st->loadProgress, &st->lazPreviewDiag);
      }
#endif
#if GIS_DESKTOP_HAVE_GDAL
      if (!lazOk) {
        // 勿丢弃 LASzip 阶段的报错：回退 GDAL 后若仍失败，应一并展示，避免只见 GDAL 笼统提示。
        const std::wstring laszipDiagBeforeGdal = st->lazPreviewDiag;
        st->lazPreviewDiag.clear();
        lazOk = ReadLazPointsPreviewGdal(st->path, &pts, &st->loadProgress, &st->lazPreviewDiag);
        if (!lazOk && !laszipDiagBeforeGdal.empty()) {
          st->lazPreviewDiag = L"【bundled LASzip】\n" + laszipDiagBeforeGdal + L"\n\n【GDAL 回退】\n" + st->lazPreviewDiag;
        }
      }
#endif
      if (!lazOk) {
        st->loadFailed = true;
        st->loadStage = 9;
        st->loadProgress = 100;
#if (defined(AGIS_HAVE_LASZIP) && AGIS_HAVE_LASZIP) || GIS_DESKTOP_HAVE_GDAL
        PostMessageW(ctx->hwnd, kPreviewLoadedMsg, 0, 0);
#else
        PostMessageW(ctx->hwnd, kPreviewLoadedMsg, 5, 0);
#endif
        delete ctx;
        return 2;
      }
    }
    st->loadStage = 2;
    st->loadProgress = 92;
    if (!BuildObjPreviewFromLasPoints(pts, st->lasPointScreenPx, &st->loadedModel)) {
      st->loadFailed = true;
      st->loadStage = 9;
      st->loadProgress = 100;
      PostMessageW(ctx->hwnd, kPreviewLoadedMsg, 0, 0);
      delete ctx;
      return 2;
    }
    std::error_code ec;
    st->loadedStats = {};
    st->loadedStats.fileBytes = std::filesystem::file_size(st->path, ec);
    st->loadedStats.vertices = pts.size();
    st->loadedStats.faces = st->loadedModel.faces.size();
    st->loadedStats.materials = st->loadedModel.materials.size();
    st->lasSourcePointCount = pts.size();
    st->lasPointCache = std::move(pts);
    st->textureLayers.clear();
    st->loadStage = 4;
    st->loadProgress = 100;
    PostMessageW(ctx->hwnd, kPreviewLoadedMsg, 1, 0);
    delete ctx;
    return 0;
  }

  st->loadStage = 1;
  st->loadProgress = 5;
  st->loadStage = 2;
  st->loadProgress = 5;
  int parsePct = 0;
  const bool ok = ParseObjModel(st->path, &st->loadedModel, &parsePct);
  st->loadProgress = (std::clamp)(parsePct, 0, 100);
  if (!ok) {
    st->loadFailed = true;
    st->loadStage = 9;
    st->loadProgress = 100;
    PostMessageW(ctx->hwnd, kPreviewLoadedMsg, 0, 0);
    delete ctx;
    return 2;
  }
  st->loadStage = 3;
  st->loadProgress = 92;
  std::error_code ec;
  st->loadedStats.fileBytes = std::filesystem::file_size(st->path, ec);
  st->loadedStats.vertices = st->loadedModel.vertices.size();
  st->loadedStats.texcoords = st->loadedModel.texcoords.size();
  // ObjPreviewModel 当前不保留独立法线数组（仅保留面索引与UV），此处先按 0 统计避免二次扫描。
  st->loadedStats.normals = 0;
  st->loadedStats.faces = st->loadedModel.faces.size();
  st->loadedStats.materials = st->loadedModel.materials.size();
  st->textureLayers = DetectTextureLayers(st->loadedModel);
  st->loadedStats.textures = st->textureLayers.size();
  if (st->loadedStats.faces > kPreviewObjFaceHardLimit) {
    std::wstring dbg = L"[PREVIEW] OBJ faces " + std::to_wstring(st->loadedStats.faces) + L" exceed soft limit " +
                       std::to_wstring(kPreviewObjFaceHardLimit) + L", continue loading full geometry by request.\n";
    OutputDebugStringW(dbg.c_str());
  }
  st->loadStage = 4;
  st->loadProgress = 100;
  PostMessageW(ctx->hwnd, kPreviewLoadedMsg, 1, 0);
  delete ctx;
  return 0;
}

struct AgisCopyableMsgParams {
  HWND owner;
  const wchar_t* title;
  const wchar_t* body;
};

enum {
  kAgisCopyMsgEditId = 100,
  kAgisCopyMsgOkId = 101,
};

static LRESULT CALLBACK AgisCopyableMsgWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      auto* p = reinterpret_cast<AgisCopyableMsgParams*>(cs->lpCreateParams);
      SetWindowTextW(w, p->title);
      RECT rc{};
      GetClientRect(w, &rc);
      constexpr int kMargin = 10;
      constexpr int kBtnH = 28;
      constexpr int kBtnW = 88;
      constexpr int kGap = 10;
      const int editH = (std::max)(0, static_cast<int>(rc.bottom) - kMargin * 2 - kBtnH - kGap);
      HWND ed = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", p->body,
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
          kMargin, kMargin, (std::max)(40, static_cast<int>(rc.right) - kMargin * 2), editH, w,
          reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kAgisCopyMsgEditId)), GetModuleHandleW(nullptr), nullptr);
      if (HFONT f = UiGetAppFont()) {
        SendMessageW(ed, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
      }
      HWND ok = CreateWindowExW(
          0, L"BUTTON", L"确定",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
          (std::max)(kMargin, static_cast<int>(rc.right) - kMargin - kBtnW),
          static_cast<int>(rc.bottom) - kMargin - kBtnH, kBtnW, kBtnH, w,
          reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kAgisCopyMsgOkId)), GetModuleHandleW(nullptr), nullptr);
      if (HFONT f = UiGetAppFont()) {
        SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
      }
      return 0;
    }
    case WM_SIZE: {
      const int cw = LOWORD(lp);
      const int ch = HIWORD(lp);
      constexpr int kMargin = 10;
      constexpr int kBtnH = 28;
      constexpr int kBtnW = 88;
      constexpr int kGap = 10;
      HWND ed = GetDlgItem(w, kAgisCopyMsgEditId);
      HWND ok = GetDlgItem(w, kAgisCopyMsgOkId);
      if (ed) {
        MoveWindow(ed, kMargin, kMargin, (std::max)(40, cw - kMargin * 2),
                   (std::max)(0, ch - kMargin * 2 - kBtnH - kGap), TRUE);
      }
      if (ok) {
        MoveWindow(ok, (std::max)(kMargin, cw - kMargin - kBtnW), ch - kMargin - kBtnH, kBtnW, kBtnH, TRUE);
      }
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(wp) == kAgisCopyMsgOkId && HIWORD(wp) == BN_CLICKED) {
        DestroyWindow(w);
        return 0;
      }
      break;
    case WM_CLOSE:
      DestroyWindow(w);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(w, msg, wp, lp);
}

static void ShowPreviewCopyableMessage(HWND owner, const wchar_t* title, const wchar_t* body) {
  static const wchar_t kClassName[] = L"AgisPreviewCopyableMsgWnd";
  static bool classRegistered = false;
  if (!classRegistered) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = AgisCopyableMsgWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.style = CS_DBLCLKS;
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      MessageBoxW(owner, body, title, MB_OK | MB_ICONWARNING);
      return;
    }
    classRegistered = true;
  }
  RECT anchor{};
  if (owner && IsWindow(owner)) {
    GetWindowRect(owner, &anchor);
  } else {
    anchor.left = 0;
    anchor.top = 0;
    anchor.right = GetSystemMetrics(SM_CXSCREEN);
    anchor.bottom = GetSystemMetrics(SM_CYSCREEN);
  }
  constexpr int ww = 580;
  constexpr int wh = 400;
  const int x = anchor.left + ((anchor.right - anchor.left) - ww) / 2;
  const int y = anchor.top + ((anchor.bottom - anchor.top) - wh) / 2;

  AgisCopyableMsgParams params{owner, title, body};
  HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE, kClassName, title,
                             WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VISIBLE | WS_CLIPCHILDREN, x, y, ww,
                             wh, owner, nullptr, GetModuleHandleW(nullptr), &params);
  if (!dlg) {
    MessageBoxW(owner, body, title, MB_OK | MB_ICONWARNING);
    return;
  }
  ShowWindow(dlg, SW_SHOW);
  UpdateWindow(dlg);
  if (HWND edFocus = GetDlgItem(dlg, kAgisCopyMsgEditId)) {
    SetFocus(edFocus);
  }
  bool ownerWasDisabled = false;
  if (owner && IsWindow(owner)) {
    EnableWindow(owner, FALSE);
    ownerWasDisabled = true;
  }
  MSG qmsg{};
  while (IsWindow(dlg) && GetMessageW(&qmsg, nullptr, 0, 0) > 0) {
    if (!IsWindow(dlg)) {
      TranslateMessage(&qmsg);
      DispatchMessageW(&qmsg);
      continue;
    }
    if (qmsg.hwnd == dlg || IsChild(dlg, qmsg.hwnd)) {
      if (!IsDialogMessageW(dlg, &qmsg)) {
        TranslateMessage(&qmsg);
        DispatchMessageW(&qmsg);
      }
    } else {
      TranslateMessage(&qmsg);
      DispatchMessageW(&qmsg);
    }
  }
  if (ownerWasDisabled && owner && IsWindow(owner)) {
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
  }
}

LRESULT CALLBACK ModelPreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE: {
      auto* st = new ModelPreviewState();
      st->path = g_pendingPreviewModelPath;
      st->loadAs3DTiles = g_pendingPreviewLoadAs3DTiles;
      g_pendingPreviewLoadAs3DTiles = false;
      st->lastFpsTick = GetTickCount();
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
      st->infoPanelText = L"模型正在后台加载，请稍候...";
      st->runtimeHudText = L"FPS: -- | 帧时: -- ms | CPU: -- ms | GPU: -- ms | Draw: -- | 瓶颈: -- | 曲线: --";
      FitPreviewCamera(st);
      SetFocus(hwnd);
      SetTimer(hwnd, 1, 33, nullptr);
      auto* loadCtx = new (std::nothrow) PreviewLoadCtx{hwnd, st};
      if (!loadCtx) {
        ShowPreviewCopyableMessage(hwnd, L"模型预览", L"内存不足：无法启动模型后台加载。");
        return -1;
      }
      st->loadThread = CreateThread(nullptr, 0, PreviewLoadThreadProc, loadCtx, 0, nullptr);
      if (!st->loadThread) {
        delete loadCtx;
        ShowPreviewCopyableMessage(hwnd, L"模型预览", L"无法启动模型加载线程。");
        return -1;
      }
      return 0;
    }
    case WM_KEYDOWN: {
      auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      if (!st) return 0;
      if (st->loading) return 0;
      bool needRedraw = true;
      switch (wParam) {
        case 'M':
#if AGIS_USE_BGFX
          st->bgfxRenderer = (st->bgfxRenderer == AgisBgfxRendererKind::kD3D11) ? AgisBgfxRendererKind::kOpenGL
                                                                                : AgisBgfxRendererKind::kD3D11;
          if (st->imguiReady) {
            imguiDestroy();
            st->imguiReady = false;
          }
          if (st->bgfxCtx) {
            agis_bgfx_preview_shutdown(hwnd, st->bgfxCtx);
            st->bgfxCtx = nullptr;
          }
          agis_bgfx_preview_init(hwnd, &st->bgfxCtx, st->bgfxRenderer, st->model);
          if (st->bgfxCtx && st->useTexture && st->currentTextureLayer < static_cast<int>(st->textureLayers.size())) {
            agis_bgfx_preview_set_texture(st->bgfxCtx, st->textureLayers[st->currentTextureLayer].second);
          }
          if (st->bgfxCtx) {
            agis_bgfx_preview_set_pbr_textures(st->bgfxCtx, BuildPbrTexturePaths(*st));
          }
          if (st->bgfxCtx) {
            agis_bgfx_preview_set_pseudo_pbr(st->bgfxCtx, st->pseudoPbrMode);
            agis_bgfx_preview_set_pbr_view_mode(st->bgfxCtx, st->pbrViewMode);
            imguiCreate(16.0f, nullptr);
            st->imguiReady = true;
          }
#else
          st->backend = (st->backend == PreviewRenderBackend::kOpenGL) ? PreviewRenderBackend::kDx11
                                                                        : PreviewRenderBackend::kOpenGL;
#endif
          break;
        case VK_SPACE:
          st->solid = !st->solid;
          break;
        case 'B':
          st->backfaceCulling = !st->backfaceCulling;
          break;
        case 'G':
          st->showGrid = !st->showGrid;
          break;
        case 'T':
          st->useTexture = !st->useTexture;
#if AGIS_USE_BGFX
          if (st->bgfxCtx) {
            if (st->useTexture && st->currentTextureLayer < static_cast<int>(st->textureLayers.size())) {
              agis_bgfx_preview_set_texture(st->bgfxCtx, st->textureLayers[st->currentTextureLayer].second);
            } else {
              agis_bgfx_preview_set_texture(st->bgfxCtx, L"");
            }
          }
#endif
          break;
        case 'P':
          st->pseudoPbrMode = !st->pseudoPbrMode;
#if AGIS_USE_BGFX
          if (st->bgfxCtx) {
            agis_bgfx_preview_set_pseudo_pbr(st->bgfxCtx, st->pseudoPbrMode);
          }
#endif
          break;
        case '1':
          st->pbrViewMode = AgisBgfxPbrViewMode::kPbrLit;
          st->pseudoPbrMode = true;
          if (st->bgfxCtx) agis_bgfx_preview_set_pbr_view_mode(st->bgfxCtx, st->pbrViewMode);
          if (st->bgfxCtx) agis_bgfx_preview_set_pseudo_pbr(st->bgfxCtx, st->pseudoPbrMode);
          break;
        case '2':
          st->pbrViewMode = AgisBgfxPbrViewMode::kAlbedo;
          st->pseudoPbrMode = false;
          if (st->bgfxCtx) agis_bgfx_preview_set_pbr_view_mode(st->bgfxCtx, st->pbrViewMode);
          if (st->bgfxCtx) agis_bgfx_preview_set_pseudo_pbr(st->bgfxCtx, st->pseudoPbrMode);
          break;
        case '3':
          st->pbrViewMode = AgisBgfxPbrViewMode::kNormal;
          st->pseudoPbrMode = false;
          if (st->bgfxCtx) agis_bgfx_preview_set_pbr_view_mode(st->bgfxCtx, st->pbrViewMode);
          if (st->bgfxCtx) agis_bgfx_preview_set_pseudo_pbr(st->bgfxCtx, st->pseudoPbrMode);
          break;
        case '4':
          st->pbrViewMode = AgisBgfxPbrViewMode::kRoughness;
          st->pseudoPbrMode = false;
          if (st->bgfxCtx) agis_bgfx_preview_set_pbr_view_mode(st->bgfxCtx, st->pbrViewMode);
          if (st->bgfxCtx) agis_bgfx_preview_set_pseudo_pbr(st->bgfxCtx, st->pseudoPbrMode);
          break;
        case '5':
          st->pbrViewMode = AgisBgfxPbrViewMode::kMetallic;
          st->pseudoPbrMode = false;
          if (st->bgfxCtx) agis_bgfx_preview_set_pbr_view_mode(st->bgfxCtx, st->pbrViewMode);
          if (st->bgfxCtx) agis_bgfx_preview_set_pseudo_pbr(st->bgfxCtx, st->pseudoPbrMode);
          break;
        case '6':
          st->pbrViewMode = AgisBgfxPbrViewMode::kAo;
          st->pseudoPbrMode = false;
          if (st->bgfxCtx) agis_bgfx_preview_set_pbr_view_mode(st->bgfxCtx, st->pbrViewMode);
          if (st->bgfxCtx) agis_bgfx_preview_set_pseudo_pbr(st->bgfxCtx, st->pseudoPbrMode);
          break;
        case 'F':
          FitPreviewCamera(st);
          break;
        case 'R':
          st->rotX = 0.0f;
          st->rotY = 0.0f;
          st->zoom = 2.2f;
          break;
        case VK_OEM_4:  // [
        case VK_OEM_6:  // ]
          if (!st->textureLayers.empty()) {
            const int n = static_cast<int>(st->textureLayers.size());
            if (wParam == VK_OEM_4) {
              st->currentTextureLayer = (st->currentTextureLayer + n - 1) % n;
            } else {
              st->currentTextureLayer = (st->currentTextureLayer + 1) % n;
            }
#if AGIS_USE_BGFX
            if (st->bgfxCtx && st->useTexture) {
              agis_bgfx_preview_set_texture(st->bgfxCtx, st->textureLayers[st->currentTextureLayer].second);
            }
#endif
          }
          break;
        default:
          needRedraw = false;
          break;
      }
      if (needRedraw) {
        RECT vrc = GetPreviewViewportRect(hwnd);
        InvalidateRect(hwnd, &vrc, FALSE);
      }
      return 0;
    }
    case kPreviewLoadedMsg: {
      auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      if (!st) return 0;
      if (wParam == 5) {
        if (st->loadThread) {
          CloseHandle(st->loadThread);
          st->loadThread = nullptr;
        }
        st->loading = false;
        ShowPreviewCopyableMessage(
            hwnd, L"模型预览",
            L"LAZ 预览需要下列之一：\n"
            L"（1）在 ../3rdparty/LASzip 放置 LASzip 源码包并重新 CMake 配置编译（推荐，见 3rdparty/README-LASZIP.md）；\n"
            L"（2）使用 AGIS_USE_GDAL=on 且 GDAL 能读 LAZ 的构建；\n"
            L"或先将 LAZ 解压/转为 .las。");
        DestroyWindow(hwnd);
        return 0;
      }
      if (wParam == 4) {
        if (st->loadThread) {
          CloseHandle(st->loadThread);
          st->loadThread = nullptr;
        }
        st->loading = false;
        ShowPreviewCopyableMessage(
            hwnd, L"模型预览",
            L"LAS 点数超过内置预览上限（约 2500 万点），为防内存过高已中止。\n请先用外部工具抽稀或切分后再预览。");
        DestroyWindow(hwnd);
        return 0;
      }
      if (wParam == 3) {
        if (st->loadThread) {
          CloseHandle(st->loadThread);
          st->loadThread = nullptr;
        }
        st->loading = false;
        wchar_t msg[512]{};
        swprintf_s(msg,
                   L"当前 OBJ 面片过大（%llu），直接预览可能导致卡死。\n请提高 mesh-spacing 或使用更粗网格后再试（建议 <= %llu 面）。",
                   static_cast<unsigned long long>(st->loadedStats.faces),
                   static_cast<unsigned long long>(kPreviewObjFaceHardLimit));
        ShowPreviewCopyableMessage(hwnd, L"模型预览", msg);
        DestroyWindow(hwnd);
        return 0;
      }
      if (st->loadThread) {
        CloseHandle(st->loadThread);
        st->loadThread = nullptr;
      }
      st->loading = false;
      if (wParam == 0 || st->loadFailed) {
        const wchar_t* failMsg = nullptr;
        if (!st->tilesLoadDiag.empty()) {
          failMsg = L"3D Tiles 预览加载失败。";
        } else if (PreviewPathIsLazFile(st->path)) {
          failMsg = L"LAZ 点云预览失败（详见下方 LASzip/GDAL 说明）。";
        } else if (PreviewPathIsLasFile(st->path)) {
          failMsg = L"LAS 点云预览失败：文件非 LAS、头无效、无点记录或已截断读取出错。";
        } else {
          failMsg = L"模型解析失败：OBJ 文件损坏或缺少有效顶点/面。";
        }
        std::wstring box = failMsg;
        if (!st->tilesLoadDiag.empty()) {
          box += L"\n\n";
          box += st->tilesLoadDiag;
        }
        if (PreviewPathIsLazFile(st->path) && !st->lazPreviewDiag.empty()) {
          box += L"\n\n—— 详情 ——\n";
          box += st->lazPreviewDiag;
        }
        ShowPreviewCopyableMessage(hwnd, L"模型预览", box.c_str());
        DestroyWindow(hwnd);
        return 0;
      }
      st->model = std::move(st->loadedModel);
      st->stats = st->loadedStats;
      st->loadStage = 4;
      st->infoPanelText = BuildModelPreviewInfoText(*st);
#if AGIS_USE_BGFX
      const bool hasRenderableMesh = !st->model.vertices.empty() && !st->model.faces.empty();
      if (hasRenderableMesh) {
        if (!agis_bgfx_preview_init(hwnd, &st->bgfxCtx, AgisBgfxRendererKind::kD3D11, st->model)) {
          ShowPreviewCopyableMessage(hwnd, L"模型预览",
                                     L"3D 预览初始化失败：bgfx 或网格数据无效（请确认 OBJ/点云网格有效）。");
          DestroyWindow(hwnd);
          return 0;
        }
      } else {
        st->bgfxCtx = nullptr;
      }
      if (st->bgfxCtx && !st->textureLayers.empty()) {
        agis_bgfx_preview_set_texture(st->bgfxCtx, st->textureLayers[0].second);
      }
      if (st->bgfxCtx) {
        agis_bgfx_preview_set_pbr_textures(st->bgfxCtx, BuildPbrTexturePaths(*st));
        agis_bgfx_preview_set_pseudo_pbr(st->bgfxCtx, st->pseudoPbrMode);
        agis_bgfx_preview_set_pbr_view_mode(st->bgfxCtx, st->pbrViewMode);
        imguiCreate(16.0f, nullptr);
        st->imguiReady = true;
      }
#endif
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_TIMER:
      if (wParam == 1) {
        if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
          if (!st->inPaint) {
            RECT vrc = GetPreviewViewportRect(hwnd);
            InvalidateRect(hwnd, &vrc, FALSE);
          }
        }
        return 0;
      }
      break;
    case WM_LBUTTONDOWN: {
      if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        if (st->loading) return 0;
      }
      RECT vrc = GetPreviewViewportRect(hwnd);
      const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      if (PtInRect(&vrc, pt)) {
        if (DispatchPreviewHudClick(hwnd, vrc, pt)) {
          return 0;
        }
        if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
          st->dragging = true;
          st->lastPt = pt;
          SetCapture(hwnd);
          SetFocus(hwnd);
          return 0;
        }
      }
      break;
    }
    case WM_MOUSEMOVE: {
      auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      if (st && st->loading) return 0;
      if (st && st->dragging) {
        const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int dx = pt.x - st->lastPt.x;
        const int dy = pt.y - st->lastPt.y;
        st->rotY += dx * 0.02f;
        st->rotX += dy * 0.02f;
        st->lastPt = pt;
        RECT vrc = GetPreviewViewportRect(hwnd);
        InvalidateRect(hwnd, &vrc, FALSE);
      }
      return 0;
    }
    case WM_LBUTTONUP:
      if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        st->dragging = false;
      }
      ReleaseCapture();
      return 0;
    case WM_MOUSEWHEEL:
      if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        if (st->loading) return 0;
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        st->zoom *= (delta > 0) ? 1.18f : 0.85f;
        st->zoom = std::clamp(st->zoom, 0.03f, 48.0f);
        st->imguiScroll += (delta > 0) ? 1 : -1;
        st->imguiScroll = (std::clamp)(st->imguiScroll, -8, 8);
        RECT vrc = GetPreviewViewportRect(hwnd);
        InvalidateRect(hwnd, &vrc, FALSE);
      }
      return 0;
    case WM_ERASEBKGND:
#if AGIS_USE_BGFX
      // bgfx 在子矩形视口清屏；抑制父窗口对该区默认擦除可减少闪烁。
#else
      // 预览视口由 OpenGL / DX11 自行清屏与交换，不做默认底色擦除可减少闪烁。
#endif
      return 1;
    case WM_SIZE: {
      RECT vrc = GetPreviewViewportRect(hwnd);
      InvalidateRect(hwnd, &vrc, FALSE);
      return 0;
    }
    case WM_PAINT: {
      auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      if (st && st->inPaint) {
        ValidateRect(hwnd, nullptr);
        return 0;
      }
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT vrc = GetPreviewViewportRect(hwnd);
      if (st) {
        st->inPaint = true;
        if (st->loading) {
          HBRUSH bg = CreateSolidBrush(RGB(236, 236, 236));
          FillRect(hdc, &vrc, bg);
          DeleteObject(bg);
          const int vw = (std::max)(1L, vrc.right - vrc.left);
          const int vh = (std::max)(1L, vrc.bottom - vrc.top);
          const RECT panel{vrc.left + vw / 2 - 220, vrc.top + vh / 2 - 60, vrc.left + vw / 2 + 220, vrc.top + vh / 2 + 60};
          HBRUSH panelBrush = CreateSolidBrush(RGB(248, 248, 248));
          FillRect(hdc, &panel, panelBrush);
          DeleteObject(panelBrush);
          FrameRect(hdc, &panel, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
          SetBkMode(hdc, TRANSPARENT);
          SetTextColor(hdc, RGB(32, 32, 32));
          const bool lasWait = PreviewPathIsPointCloudFile(st->path);
          const std::wstring line =
              (lasWait ? L"点云加载中... " : L"模型加载中... ") + std::to_wstring((std::clamp)(st->loadProgress, 0, 100)) + L"%";
          const wchar_t* stage = L"准备中...";
          if (lasWait) {
            switch (st->loadStage) {
              case 1:
                stage = PreviewPathIsLazFile(st->path) ? L"正在读取 LAZ 点云（优先 LASzip）..." : L"正在读取 LAS 点云...";
                break;
              case 2: stage = L"正在生成预览点斑..."; break;
              case 4: stage = L"即将完成..."; break;
              case 9: stage = L"点云读取失败"; break;
              default: break;
            }
          } else {
            switch (st->loadStage) {
              case 1: stage = L"正在统计模型信息..."; break;
              case 2: stage = L"正在解析 OBJ 网格..."; break;
              case 3: stage = L"正在分析贴图层..."; break;
              case 4: stage = L"正在初始化渲染资源..."; break;
              case 9: stage = L"模型解析失败"; break;
              default: break;
            }
          }
          TextOutW(hdc, panel.left + 14, panel.top + 12, line.c_str(), static_cast<int>(line.size()));
          TextOutW(hdc, panel.left + 14, panel.top + 34, stage, static_cast<int>(wcslen(stage)));
          RECT bar{panel.left + 14, panel.top + 70, panel.right - 14, panel.top + 86};
          FrameRect(hdc, &bar, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
          RECT fill = bar;
          const int w = (std::max)(0L, bar.right - bar.left - 2);
          fill.left += 1;
          fill.top += 1;
          fill.bottom -= 1;
          fill.right = fill.left + w * (std::clamp)(st->loadProgress, 0, 100) / 100;
          HBRUSH fillBrush = CreateSolidBrush(RGB(90, 150, 255));
          FillRect(hdc, &fill, fillBrush);
          DeleteObject(fillBrush);
          st->inPaint = false;
          FrameRect(hdc, &vrc, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
          EndPaint(hwnd, &ps);
          return 0;
        }
        const double t0 = NowPerfMs();
#if AGIS_USE_BGFX
        if (st->imguiReady) {
          POINT pt{};
          GetCursorPos(&pt);
          ScreenToClient(hwnd, &pt);
          uint8_t mbut = 0;
          if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) mbut |= IMGUI_MBUT_LEFT;
          if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) mbut |= IMGUI_MBUT_RIGHT;
          if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) mbut |= IMGUI_MBUT_MIDDLE;
          const int vw = (std::max)(1L, vrc.right - vrc.left);
          const int vh = (std::max)(1L, vrc.bottom - vrc.top);
          imguiBeginFrame(pt.x, pt.y, mbut, st->imguiScroll, static_cast<uint16_t>(vw), static_cast<uint16_t>(vh), -1, 254);
          st->imguiScroll = 0;
          ImGui::SetNextWindowBgAlpha(0.86f);
          ImGui::SetNextWindowPos(ImVec2(static_cast<float>(vrc.left + 10), static_cast<float>(vrc.top + 10)), ImGuiCond_Always);
          ImGui::Begin("Preview Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
          int rendererSel = (st->bgfxRenderer == AgisBgfxRendererKind::kOpenGL) ? 1 : 0;
          const char* rendererItems[] = {"D3D11", "OpenGL"};
          if (ImGui::Combo("Renderer", &rendererSel, rendererItems, 2)) {
            PostMessageW(hwnd, WM_KEYDOWN, 'M', 0);
          }
          if (ImGui::Checkbox("Solid", &st->solid)) {}
          if (ImGui::Checkbox("Backface Culling", &st->backfaceCulling)) {}
          if (ImGui::Checkbox("Grid", &st->showGrid)) {}
          if (ImGui::Checkbox("Texture", &st->useTexture)) {
            if (st->bgfxCtx) {
              if (st->useTexture && st->currentTextureLayer < static_cast<int>(st->textureLayers.size())) {
                agis_bgfx_preview_set_texture(st->bgfxCtx, st->textureLayers[st->currentTextureLayer].second);
              } else {
                agis_bgfx_preview_set_texture(st->bgfxCtx, L"");
              }
            }
          }
          int renderMode = 0;
          if (!st->pseudoPbrMode) {
            renderMode = static_cast<int>(st->pbrViewMode);
            if (renderMode <= 0) renderMode = 2;
          }
          const char* renderItems[] = {"Full PBR", "Albedo", "Normal", "Roughness", "Metallic", "AO"};
          if (ImGui::Combo("Render Output", &renderMode, renderItems, 6)) {
            if (renderMode == 0) {
              st->pseudoPbrMode = true;
              st->pbrViewMode = AgisBgfxPbrViewMode::kPbrLit;
            } else {
              st->pseudoPbrMode = false;
              st->pbrViewMode = static_cast<AgisBgfxPbrViewMode>(renderMode);
            }
            if (st->bgfxCtx) {
              agis_bgfx_preview_set_pseudo_pbr(st->bgfxCtx, st->pseudoPbrMode);
              agis_bgfx_preview_set_pbr_view_mode(st->bgfxCtx, st->pbrViewMode);
            }
          }
          if (st->lasSourcePointCount > 0 && !st->lasPointCache.empty()) {
            float pxSz = st->lasPointScreenPx;
            if (ImGui::SliderFloat("点大小(像素)", &pxSz, 1.f, 32.f, "%.0f")) {
              st->lasPointScreenPx = pxSz;
              BuildObjPreviewFromLasPoints(st->lasPointCache, st->lasPointScreenPx, &st->model);
              if (st->bgfxCtx) {
                agis_bgfx_preview_reload_model(st->bgfxCtx, st->model);
              }
              st->stats.faces = st->model.faces.size();
              st->infoPanelText = BuildModelPreviewInfoText(*st);
            }
          }
          ImGui::Separator();
          ImGui::Text("Info");
          std::string infoUtf8 = PreviewWideToUtf8(st->infoPanelText);
          if (infoUtf8.empty()) {
            infoUtf8 = "No model info.";
          }
          ImGui::BeginChild("model-info", ImVec2(420.0f, 170.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
          ImGui::TextUnformatted(infoUtf8.c_str());
          ImGui::EndChild();
          if (!st->textureLayers.empty()) {
            std::string layerPreview;
            for (wchar_t ch : st->textureLayers[st->currentTextureLayer].first) {
              layerPreview.push_back((ch >= 32 && ch < 127) ? static_cast<char>(ch) : '?');
            }
            if (ImGui::BeginCombo("Layer", layerPreview.c_str())) {
              for (int i = 0; i < static_cast<int>(st->textureLayers.size()); ++i) {
                bool sel = (i == st->currentTextureLayer);
                std::string label;
                for (wchar_t ch : st->textureLayers[i].first) {
                  label.push_back((ch >= 32 && ch < 127) ? static_cast<char>(ch) : '?');
                }
                if (ImGui::Selectable(label.c_str(), sel)) {
                  st->currentTextureLayer = i;
                  if (st->bgfxCtx && st->useTexture) {
                    agis_bgfx_preview_set_texture(st->bgfxCtx, st->textureLayers[i].second);
                  }
                }
                if (sel) ImGui::SetItemDefaultFocus();
              }
              ImGui::EndCombo();
            }
          }
          if (ImGui::Button("Fit")) {
            FitPreviewCamera(st);
          }
          ImGui::SameLine();
          if (ImGui::Button("Reset")) {
            st->rotX = 0.0f;
            st->rotY = 0.0f;
            st->zoom = 2.2f;
          }
          const int vhud = (std::max)(1, vh);
          const float unitsPerPxHud = (2.0f / (std::max)(0.05f, st->zoom)) / static_cast<float>(vhud);
          ImGui::Text("FPS %.1f | Frame %.2f ms", st->fps, st->lastFrameMs);
          ImGui::Text("Scale: 100px ~= %.4f units | Grid length: 2.0 units", unitsPerPxHud * 100.0f);
          const std::wstring sparkNow = BuildFrameMsSparkline(*st);
          float cpuMsHud = st->lastFrameMs;
          float gpuMsHud = 0.0f;
          float waitSubmitMsHud = 0.0f;
          float waitRenderMsHud = 0.0f;
          uint32_t drawCallsHud = static_cast<uint32_t>(st->stats.faces);
          AgisBgfxRuntimeStats bgfxHud{};
          if (agis_bgfx_preview_get_runtime_stats(st->bgfxCtx, &bgfxHud)) {
            cpuMsHud = (std::max)(cpuMsHud, bgfxHud.cpuFrameMs);
            gpuMsHud = bgfxHud.gpuFrameMs;
            waitSubmitMsHud = bgfxHud.waitSubmitMs;
            waitRenderMsHud = bgfxHud.waitRenderMs;
            drawCallsHud = bgfxHud.drawCalls;
          }
          const std::wstring bottleneckNow =
              EvaluatePreviewBottleneck(*st, cpuMsHud, gpuMsHud, drawCallsHud, waitSubmitMsHud, waitRenderMsHud);
          ImGui::Separator();
          ImGui::Text("CPU %.2f ms | GPU %.2f ms | Draw %u", cpuMsHud, gpuMsHud, drawCallsHud);
          ImGui::Text("瓶颈: %s", PreviewWideToUtf8(bottleneckNow).c_str());
          ImGui::Text("曲线: %s", PreviewWideToUtf8(sparkNow).c_str());
          ImGui::End();
          DrawAxisImGuiOverlay(vrc, st->rotX, st->rotY);
          imguiEndFrame();
        }
        if (st->bgfxCtx) {
          agis_bgfx_preview_draw(st->bgfxCtx, hwnd, vrc, st->rotX, st->rotY, st->zoom, st->solid, st->showGrid,
                                 st->backfaceCulling);
        } else {
          HBRUSH bg = CreateSolidBrush(RGB(236, 236, 236));
          FillRect(hdc, &vrc, bg);
          DeleteObject(bg);
          SetBkMode(hdc, TRANSPARENT);
          SetTextColor(hdc, RGB(84, 84, 84));
          const wchar_t* tip = L"空场景（未传入模型路径）";
          TextOutW(hdc, vrc.left + 12, vrc.top + 12, tip, static_cast<int>(wcslen(tip)));
        }
#else
        if (st->backend == PreviewRenderBackend::kOpenGL) {
          InitPreviewGl(hwnd, st);
          DrawModelPreviewOpenGL(hwnd, vrc, *st);
        } else {
          DrawModelPreviewDx11(hwnd, vrc, st);
        }
#endif
        const double t1 = NowPerfMs();
        st->lastFrameMs = static_cast<float>((std::max)(0.0, t1 - t0));
        const int cap = static_cast<int>(st->frameMsHistory.size());
        st->frameMsHistory[st->frameMsHistoryPos] = st->lastFrameMs;
        st->frameMsHistoryPos = (st->frameMsHistoryPos + 1) % cap;
        if (st->frameMsHistoryCount < cap) {
          st->frameMsHistoryCount += 1;
        }
        st->frameCounter += 1;
        const DWORD now = GetTickCount();
        const DWORD dt = now - st->lastFpsTick;
        if (dt >= 500) {
          st->fps = (st->frameCounter * 1000.0f) / static_cast<float>(dt);
          st->frameCounter = 0;
          st->lastFpsTick = now;
          float cpuFrameMs = st->lastFrameMs;
          float gpuFrameMs = 0.0f;
          float waitSubmitMs = 0.0f;
          float waitRenderMs = 0.0f;
          uint32_t drawCalls = static_cast<uint32_t>(st->stats.faces);
#if AGIS_USE_BGFX
          AgisBgfxRuntimeStats bgfxStats{};
          if (agis_bgfx_preview_get_runtime_stats(st->bgfxCtx, &bgfxStats)) {
            cpuFrameMs = (std::max)(cpuFrameMs, bgfxStats.cpuFrameMs);
            gpuFrameMs = bgfxStats.gpuFrameMs;
            waitSubmitMs = bgfxStats.waitSubmitMs;
            waitRenderMs = bgfxStats.waitRenderMs;
            drawCalls = bgfxStats.drawCalls;
          }
#endif
          const std::wstring bottleneck =
              EvaluatePreviewBottleneck(*st, cpuFrameMs, gpuFrameMs, drawCalls, waitSubmitMs, waitRenderMs);
          st->runtimeBottleneck = bottleneck;
          const std::wstring spark = BuildFrameMsSparkline(*st);
          wchar_t line[384]{};
          swprintf_s(line,
                     L"FPS: %.1f | 帧时: %.2f ms | CPU: %.2f ms | GPU: %.2f ms | Draw: %u | 瓶颈: %s | 曲线:%s",
                     st->fps, st->lastFrameMs, cpuFrameMs, gpuFrameMs, drawCalls, bottleneck.c_str(), spark.c_str());
          st->runtimeHudText = line;
        }
#if AGIS_USE_BGFX
        if (!st->imguiReady) {
          DrawAxisOverlay(hdc, vrc, *st);
        }
#else
        DrawAxisOverlay(hdc, vrc, *st);
#endif
        if (!st->imguiReady) {
          DrawRuntimeHud(hdc, vrc, *st);
        }
        st->inPaint = false;
      }
      FrameRect(hdc, &vrc, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        KillTimer(hwnd, 1);
        if (st->loadThread) {
          WaitForSingleObject(st->loadThread, 5000);
          CloseHandle(st->loadThread);
          st->loadThread = nullptr;
        }
#if AGIS_USE_BGFX
        if (st->imguiReady) {
          imguiDestroy();
          st->imguiReady = false;
        }
        if (st->bgfxCtx) {
          agis_bgfx_preview_shutdown(hwnd, st->bgfxCtx);
          st->bgfxCtx = nullptr;
        }
#else
        ReleasePreviewGl(hwnd, st);
        ReleasePreviewDx(st);
#endif
        delete st;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void OpenModelPreviewWindow(HWND owner, const std::wstring& path) {
  g_pendingPreviewLoadAs3DTiles = false;
  g_pendingPreviewModelPath = path;
  const DWORD exStyle = owner ? WS_EX_TOOLWINDOW : 0;
  HWND pw = CreateWindowExW(exStyle, kModelPreviewClass, L"模型数据预览",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 960, 720, owner,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
  if (pw) {
    AgisCenterWindowInMonitorWorkArea(pw, owner ? owner : g_hwndMain);
    ShowWindow(pw, SW_SHOW);
  }
}

void OpenModelPreviewWindow3DTiles(HWND owner, const std::wstring& tilesetRootOrFile) {
  g_pendingPreviewLoadAs3DTiles = true;
  g_pendingPreviewModelPath = tilesetRootOrFile;
  const DWORD exStyle = owner ? WS_EX_TOOLWINDOW : 0;
  HWND pw = CreateWindowExW(exStyle, kModelPreviewClass, L"3D Tiles 预览",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 960, 720, owner,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
  if (pw) {
    AgisCenterWindowInMonitorWorkArea(pw, owner ? owner : g_hwndMain);
    ShowWindow(pw, SW_SHOW);
  }
}

// --- 瓦片：平面四叉树 (slippy z/x/y) + 3D Tiles BVH/体积元数据（tileset.json 根 region）---

enum class TileSampleResult { kOk, kNoRaster, kContainerUnsupported };

struct TileFindResult {
  TileSampleResult code = TileSampleResult::kNoRaster;
  std::wstring path;
};

static bool IsRasterTileExtension(const std::wstring& ext) {
  return _wcsicmp(ext.c_str(), L".png") == 0 || _wcsicmp(ext.c_str(), L".jpg") == 0 ||
         _wcsicmp(ext.c_str(), L".jpeg") == 0 || _wcsicmp(ext.c_str(), L".webp") == 0 ||
         _wcsicmp(ext.c_str(), L".bmp") == 0;
}

static bool TryParseNonNegIntW(const std::wstring& s, int* out) {
  if (!out || s.empty()) {
    return false;
  }
  wchar_t* end = nullptr;
  const long v = std::wcstol(s.c_str(), &end, 10);
  if (end == s.c_str() || v < 0 || v > 0x0fffffff) {
    return false;
  }
  *out = static_cast<int>(v);
  return true;
}

static uint64_t PackTileKey(int z, int x, int y) {
  return (static_cast<uint64_t>(static_cast<unsigned char>(z)) << 56) | (static_cast<uint64_t>(x) << 28) |
         static_cast<uint64_t>(y);
}

static TileFindResult FindSampleTileRaster(const std::wstring& pathW) {
  std::error_code ec;
  const std::filesystem::path p(pathW);
  if (std::filesystem::is_regular_file(p, ec)) {
    const std::wstring ext = p.extension().wstring();
    if (IsRasterTileExtension(ext)) {
      return {TileSampleResult::kOk, p.wstring()};
    }
    if (_wcsicmp(ext.c_str(), L".mbtiles") == 0 || _wcsicmp(ext.c_str(), L".gpkg") == 0) {
      return {TileSampleResult::kContainerUnsupported, L""};
    }
    return {TileSampleResult::kNoRaster, L""};
  }
  if (!std::filesystem::is_directory(p, ec)) {
    return {TileSampleResult::kNoRaster, L""};
  }
  int scanned = 0;
  constexpr int kMaxScan = 6000;
  for (std::filesystem::recursive_directory_iterator it(
           p, std::filesystem::directory_options::skip_permission_denied, ec);
       it != std::filesystem::recursive_directory_iterator{} && scanned < kMaxScan; ++it, ++scanned) {
    if (!it->is_regular_file(ec)) {
      continue;
    }
    const std::wstring ext = it->path().extension().wstring();
    if (IsRasterTileExtension(ext)) {
      return {TileSampleResult::kOk, it->path().wstring()};
    }
  }
  return {TileSampleResult::kNoRaster, L""};
}

static bool ReadWholeFileAscii(const std::wstring& pathW, std::string* out) {
  if (!out) {
    return false;
  }
  std::ifstream ifs(std::filesystem::path(pathW), std::ios::binary);
  if (!ifs) {
    return false;
  }
  std::string buf((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  out->swap(buf);
  return !out->empty();
}

/// 磁盘为 TMS 行号文件名（AGIS 写出为 `y_tms`）时，预览栅格需按 XYZ 映射：y_xyz = (2^z-1-y_disk)。
static bool DetectTmsTileLayoutOnDisk(const std::filesystem::path& root) {
  std::error_code ec;
  if (std::filesystem::is_regular_file(root / L"tms.xml", ec)) {
    return true;
  }
  std::string raw;
  if (ReadWholeFileAscii((root / L"README.txt").wstring(), &raw)) {
    if (raw.find("protocol=tms") != std::string::npos) {
      return true;
    }
  }
  return false;
}

#if GIS_DESKTOP_HAVE_GDAL
static std::unique_ptr<Gdiplus::Bitmap> TryLoadGdalRasterTileContainerPreview(const std::wstring& pathW,
                                                                              std::wstring* diagOut) {
  if (diagOut) {
    diagOut->clear();
  }
  AgisEnsureGdalDataPath();
  CPLErrorReset();
  GDALAllRegister();
  std::string utf8;
  {
    const int n =
        WideCharToMultiByte(CP_UTF8, 0, pathW.c_str(), static_cast<int>(pathW.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
      if (diagOut) {
        *diagOut = L"路径无法转为 UTF-8。";
      }
      return nullptr;
    }
    utf8.assign(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, pathW.c_str(), static_cast<int>(pathW.size()), utf8.data(), n, nullptr, nullptr);
  }
  GDALDatasetH ds = GDALOpenEx(utf8.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr);
  if (!ds) {
    if (diagOut) {
      *diagOut = L"GDAL 无法以栅格方式打开该文件（驱动缺失、需 PROJ/GDAL_DATA，或不是平铺栅格内容）。";
      const char* cpl = CPLGetLastErrorMsg();
      if (cpl && cpl[0]) {
        const int wn = MultiByteToWideChar(CP_UTF8, 0, cpl, -1, nullptr, 0);
        if (wn > 1) {
          std::wstring wc(static_cast<size_t>(wn - 1), L'\0');
          MultiByteToWideChar(CP_UTF8, 0, cpl, -1, wc.data(), wn);
          diagOut->append(L"\n");
          diagOut->append(wc);
        }
      }
    }
    return nullptr;
  }
  const int w = GDALGetRasterXSize(ds);
  const int h = GDALGetRasterYSize(ds);
  const int bands = GDALGetRasterCount(ds);
  if (w < 1 || h < 1 || bands < 1) {
    GDALClose(ds);
    if (diagOut) {
      *diagOut = L"数据集无有效栅格尺寸。";
    }
    return nullptr;
  }
  constexpr int kMaxDim = 2048;
  int outW = w;
  int outH = h;
  if (w >= h) {
    if (outW > kMaxDim) {
      outW = kMaxDim;
      outH = (std::max)(1, static_cast<int>(std::lround(static_cast<double>(h) * static_cast<double>(kMaxDim) /
                                                         static_cast<double>(w))));
    }
  } else {
    if (outH > kMaxDim) {
      outH = kMaxDim;
      outW = (std::max)(1, static_cast<int>(std::lround(static_cast<double>(w) * static_cast<double>(kMaxDim) /
                                                         static_cast<double>(h))));
    }
  }
  std::vector<uint8_t> planeR(static_cast<size_t>(outW) * static_cast<size_t>(outH));
  std::vector<uint8_t> planeG(planeR.size());
  std::vector<uint8_t> planeB(planeR.size());
  auto readPlane = [&](int bandIdx, uint8_t* dst) -> bool {
    GDALRasterBandH bh = GDALGetRasterBand(ds, bandIdx);
    if (!bh) {
      return false;
    }
    return GDALRasterIO(bh, GF_Read, 0, 0, w, h, dst, outW, outH, GDT_Byte, 1, outW) == CE_None;
  };
  bool ok = false;
  if (bands >= 3) {
    ok = readPlane(1, planeR.data()) && readPlane(2, planeG.data()) && readPlane(3, planeB.data());
  } else {
    ok = readPlane(1, planeR.data());
    if (ok) {
      planeG = planeR;
      planeB = planeR;
    }
  }
  GDALClose(ds);
  if (!ok) {
    if (diagOut) {
      *diagOut = L"RasterIO 读缩略图失败（波段类型可能非 Byte，或仅为矢量 GeoPackage）。";
    }
    return nullptr;
  }
  auto bmp = std::make_unique<Gdiplus::Bitmap>(outW, outH, PixelFormat24bppRGB);
  if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
    return nullptr;
  }
  Gdiplus::BitmapData bd{};
  Gdiplus::Rect r(0, 0, outW, outH);
  if (bmp->LockBits(&r, Gdiplus::ImageLockModeWrite, PixelFormat24bppRGB, &bd) != Gdiplus::Ok) {
    return nullptr;
  }
  auto* dstBase = static_cast<uint8_t*>(bd.Scan0);
  for (int yy = 0; yy < outH; ++yy) {
    uint8_t* row = dstBase + yy * bd.Stride;
    for (int xx = 0; xx < outW; ++xx) {
      const size_t i = static_cast<size_t>(yy) * static_cast<size_t>(outW) + static_cast<size_t>(xx);
      row[xx * 3 + 0] = planeB[i];
      row[xx * 3 + 1] = planeG[i];
      row[xx * 3 + 2] = planeR[i];
    }
  }
  bmp->UnlockBits(&bd);
  return bmp;
}
#endif  // GIS_DESKTOP_HAVE_GDAL

static std::optional<std::wstring> FindTilesetJsonPath(const std::wstring& rootW) {
  std::error_code ec;
  const std::filesystem::path p(rootW);
  if (std::filesystem::is_regular_file(p, ec)) {
    if (_wcsicmp(p.filename().c_str(), L"tileset.json") == 0) {
      return p.wstring();
    }
    return std::nullopt;
  }
  if (!std::filesystem::is_directory(p, ec)) {
    return std::nullopt;
  }
  const auto tj = p / L"tileset.json";
  if (std::filesystem::is_regular_file(tj, ec)) {
    return tj.wstring();
  }
  return std::nullopt;
}

static std::filesystem::path ThreeDTilesContentDirectory(const std::wstring& rootW, const std::wstring& tilesetJsonW) {
  const std::filesystem::path ts(tilesetJsonW);
  std::error_code ec;
  if (std::filesystem::is_regular_file(ts, ec)) {
    return ts.parent_path();
  }
  return std::filesystem::path(rootW);
}

static std::wstring Utf8JsonToWide(const std::string& u8) {
  if (u8.empty()) {
    return {};
  }
  const int n = MultiByteToWideChar(CP_UTF8, 0, u8.data(), static_cast<int>(u8.size()), nullptr, 0);
  if (n <= 0) {
    return std::wstring(u8.begin(), u8.end());
  }
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, u8.data(), static_cast<int>(u8.size()), w.data(), n);
  return w;
}

static bool TryParseFirstDoubleAfterKey(const std::string& raw, const char* key, double* out) {
  if (!out || !key) {
    return false;
  }
  const size_t pos = raw.find(key);
  if (pos == std::string::npos) {
    return false;
  }
  const size_t colon = raw.find(':', pos);
  if (colon == std::string::npos) {
    return false;
  }
  const char* p = raw.c_str() + colon + 1;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
    ++p;
  }
  char* endp = nullptr;
  const double v = std::strtod(p, &endp);
  if (endp == p) {
    return false;
  }
  *out = v;
  return true;
}

static bool TryExtractTilesetAssetVersion(const std::string& raw, std::string* ver) {
  if (!ver) {
    return false;
  }
  const size_t apos = raw.find("\"asset\"");
  if (apos == std::string::npos) {
    return false;
  }
  const size_t searchEnd = (std::min)(apos + 900, raw.size());
  const size_t vpos = raw.find("\"version\"", apos);
  if (vpos == std::string::npos || vpos > searchEnd) {
    return false;
  }
  const size_t colon = raw.find(':', vpos);
  if (colon == std::string::npos) {
    return false;
  }
  size_t i = colon + 1;
  while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t' || raw[i] == '\n' || raw[i] == '\r')) {
    ++i;
  }
  if (i >= raw.size() || raw[i] != '"') {
    return false;
  }
  ++i;
  size_t j = i;
  while (j < raw.size() && raw[j] != '"') {
    if (raw[j] == '\\' && j + 1 < raw.size()) {
      j += 2;
    } else {
      ++j;
    }
  }
  if (j <= i) {
    return false;
  }
  *ver = raw.substr(i, j - i);
  return !ver->empty();
}

static void CollectSampleUris(const std::string& raw, int maxN, std::vector<std::string>* out) {
  if (!out || maxN <= 0) {
    return;
  }
  std::set<std::string> seen;
  size_t pos = 0;
  while (static_cast<int>(out->size()) < maxN && pos < raw.size()) {
    pos = raw.find("\"uri\"", pos);
    if (pos == std::string::npos) {
      break;
    }
    const size_t colon = raw.find(':', pos);
    if (colon == std::string::npos) {
      pos += 5;
      continue;
    }
    size_t i = colon + 1;
    while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t' || raw[i] == '\n' || raw[i] == '\r')) {
      ++i;
    }
    if (i >= raw.size() || raw[i] != '"') {
      pos = colon + 1;
      continue;
    }
    ++i;
    size_t j = i;
    while (j < raw.size() && raw[j] != '"') {
      if (raw[j] == '\\' && j + 1 < raw.size()) {
        j += 2;
      } else {
        ++j;
      }
    }
    if (j > i) {
      const std::string uri = raw.substr(i, j - i);
      if (!uri.empty() && seen.insert(uri).second) {
        out->push_back(uri);
      }
    }
    pos = j + 1;
  }
}

struct ThreeDTilesContentStats {
  size_t b3dm = 0;
  size_t i3dm = 0;
  size_t pnts = 0;
  size_t cmpt = 0;
  size_t glb = 0;
  size_t gltf = 0;
};

static void ScanThreeDTilesPayloadFiles(const std::filesystem::path& contentRoot, ThreeDTilesContentStats* s) {
  if (!s) {
    return;
  }
  std::error_code ec;
  if (!std::filesystem::is_directory(contentRoot, ec)) {
    return;
  }
  size_t scanned = 0;
  constexpr size_t kMaxScan = 12000;
  for (std::filesystem::recursive_directory_iterator it(
           contentRoot, std::filesystem::directory_options::skip_permission_denied, ec);
       it != std::filesystem::recursive_directory_iterator{} && scanned < kMaxScan; ++it, ++scanned) {
    if (!it->is_regular_file(ec)) {
      continue;
    }
    const std::wstring ext = it->path().extension().wstring();
    if (_wcsicmp(ext.c_str(), L".b3dm") == 0) {
      ++s->b3dm;
    } else if (_wcsicmp(ext.c_str(), L".i3dm") == 0) {
      ++s->i3dm;
    } else if (_wcsicmp(ext.c_str(), L".pnts") == 0) {
      ++s->pnts;
    } else if (_wcsicmp(ext.c_str(), L".cmpt") == 0) {
      ++s->cmpt;
    } else if (_wcsicmp(ext.c_str(), L".glb") == 0) {
      ++s->glb;
    } else if (_wcsicmp(ext.c_str(), L".gltf") == 0) {
      ++s->gltf;
    }
  }
}

static std::wstring BuildThreeDTilesDashboard(const std::wstring& rootW, const std::wstring& tilesetJsonW,
                                              const std::wstring& bvHintLines) {
  std::string raw;
  if (!ReadWholeFileAscii(tilesetJsonW, &raw)) {
    std::wstring w = L"【3D Tiles】无法读取 tileset.json。\n路径：\n" + tilesetJsonW;
    return w;
  }
  std::wstring dash = L"【3D Tiles · 元数据预览】\n";
  dash += L"AGIS 内建说明与目录扫描（不加载 glTF/b3dm 网格）。完整浏览请用 Cesium 或「系统默认打开」。\n";
  dash += L"对接 C++ 运行时请参考仓库 3rdparty/README-CESIUM-NATIVE.md（cesium-native 源码已在 3rdparty/cesium-native-*）。\n\n";
  if (!bvHintLines.empty()) {
    dash += bvHintLines;
    dash += L"\n\n";
  }
  std::string aver;
  if (TryExtractTilesetAssetVersion(raw, &aver)) {
    dash += L"asset.version（粗解析）: ";
    dash += Utf8JsonToWide(aver);
    dash += L"\n";
  }
  double ge = 0;
  if (TryParseFirstDoubleAfterKey(raw, "\"geometricError\"", &ge)) {
    dash += L"首个 geometricError（粗解析，多为根节点）: ";
    dash += std::to_wstring(ge);
    dash += L"\n";
  }
  const auto contentDir = ThreeDTilesContentDirectory(rootW, tilesetJsonW);
  ThreeDTilesContentStats st{};
  ScanThreeDTilesPayloadFiles(contentDir, &st);
  const size_t tileFiles = st.b3dm + st.i3dm + st.pnts + st.cmpt;
  dash += L"\n内容瓦片文件（子目录扫描≤12000，按扩展名计数）：\n";
  dash += L"  b3dm=" + std::to_wstring(st.b3dm) + L" i3dm=" + std::to_wstring(st.i3dm) + L" pnts=" +
          std::to_wstring(st.pnts) + L" cmpt=" + std::to_wstring(st.cmpt) + L"\n";
  dash += L"  glb=" + std::to_wstring(st.glb) + L" gltf=" + std::to_wstring(st.gltf) + L"\n";
  if (tileFiles == 0 && st.glb == 0 && st.gltf == 0) {
    dash += L"（未见常见载荷扩展名：可能仅外链 URL、或路径不在当前目录树下。）\n";
  }
  std::vector<std::string> uris;
  CollectSampleUris(raw, 8, &uris);
  if (!uris.empty()) {
    dash += L"\ntileset 内 uri 抽样（至多 8 条，去重）：\n";
    for (const auto& u : uris) {
      dash += L"  · ";
      dash += Utf8JsonToWide(u);
      dash += L"\n";
    }
  }
  dash += L"\n根目录/内容根：\n  ";
  dash += contentDir.wstring();
  dash += L"\n";
  return dash;
}

/// 自 tileset.json 粗提取首个 region（弧度）并换算为度 + 粗计 children 出现次数（BVH 层级提示）。
static std::wstring RoughTilesetBvHintForFile(const std::wstring& tilesetJsonPathW) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(std::filesystem::path(tilesetJsonPathW), ec)) {
    return {};
  }
  std::string raw;
  if (!ReadWholeFileAscii(tilesetJsonPathW, &raw)) {
    return L"(tileset.json 无法读取)";
  }
  size_t rpos = raw.find("\"region\"");
  double west = 0, south = 0, east = 0, north = 0, zminM = 0, zmaxM = 0;
  bool haveReg = false;
  if (rpos != std::string::npos) {
    size_t lb = raw.find('[', rpos);
    if (lb != std::string::npos) {
      const char* p = raw.c_str() + lb + 1;
      double vals[6]{};
      int got = 0;
      for (; got < 6 && p && *p; ++got) {
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) {
          ++p;
        }
        if (!*p) {
          break;
        }
        char* endp = nullptr;
        vals[got] = std::strtod(p, &endp);
        if (endp == p) {
          break;
        }
        p = endp;
      }
      if (got >= 4) {
        constexpr double kRad2Deg = 180.0 / 3.14159265358979323846;
        west = vals[0] * kRad2Deg;
        south = vals[1] * kRad2Deg;
        east = vals[2] * kRad2Deg;
        north = vals[3] * kRad2Deg;
        if (got >= 6) {
          zminM = vals[4];
          zmaxM = vals[5];
        }
        haveReg = true;
      }
    }
  }
  int childHits = 0;
  for (size_t i = 0; i + 10 < raw.size(); ++i) {
    if (raw.compare(i, 10, "\"children\"") == 0) {
      ++childHits;
    }
  }
  std::wostringstream wo;
  wo << L"【BVH / 3D Tiles】Cesium 瓦片树为层次包围体；根节点常用 region/box。\n";
  if (haveReg) {
    wo << L"根 region→经纬度(°): W=" << west << L" S=" << south << L" E=" << east << L" N=" << north;
    wo << L" ；高程约(m) zmin=" << zminM << L" zmax=" << zmaxM << L"\n";
  } else {
    wo << L"未解析到标准数字 region 数组（可能被压缩或格式非预期）。\n";
  }
  wo << L"子树提示: \"children\" 出现 " << childHits
     << L" 次。\n【八叉树】对 3D box 体积的八分细分常见于嵌套子 tile；本预览不解析子网格、不渲染 glTF/b3dm，完整三维请用 "
        L"Cesium/系统打开。";
  return wo.str();
}

static size_t IndexSlippyQuadtree(const std::wstring& rootW, bool tmsYFilenamesOnDisk,
                                  std::unordered_map<uint64_t, std::wstring>* paths, int* maxZOut) {
  paths->clear();
  if (maxZOut) {
    *maxZOut = 0;
  }
  std::error_code ec;
  const std::filesystem::path root(rootW);
  if (!std::filesystem::is_directory(root, ec)) {
    return 0;
  }
  size_t scanned = 0;
  constexpr size_t kMaxScan = 12000;
  int localMaxZ = 0;
  for (std::filesystem::recursive_directory_iterator it(
           root, std::filesystem::directory_options::skip_permission_denied, ec);
       it != std::filesystem::recursive_directory_iterator{} && scanned < kMaxScan; ++it, ++scanned) {
    if (!it->is_regular_file(ec)) {
      continue;
    }
    const std::wstring ext = it->path().extension().wstring();
    if (!IsRasterTileExtension(ext)) {
      continue;
    }
    std::filesystem::path rel = std::filesystem::relative(it->path(), root, ec);
    if (ec || rel.empty()) {
      continue;
    }
    std::vector<std::wstring> comp;
    for (auto& part : rel) {
      comp.push_back(part.wstring());
    }
    if (comp.size() < 3) {
      continue;
    }
    int z = 0, x = 0, yDisk = 0;
    if (!TryParseNonNegIntW(comp[comp.size() - 3], &z) || !TryParseNonNegIntW(comp[comp.size() - 2], &x)) {
      continue;
    }
    const std::wstring& fname = comp.back();
    const size_t dot = fname.find_last_of(L'.');
    const std::wstring ystem = dot == std::wstring::npos ? fname : fname.substr(0, dot);
    if (!TryParseNonNegIntW(ystem, &yDisk)) {
      continue;
    }
    if (z > 29) {
      continue;
    }
    const int dim = 1 << z;
    if (x >= dim || yDisk >= dim) {
      continue;
    }
    const int yKey = tmsYFilenamesOnDisk ? (dim - 1 - yDisk) : yDisk;
    (*paths)[PackTileKey(z, x, yKey)] = it->path().wstring();
    localMaxZ = (std::max)(localMaxZ, z);
  }
  if (maxZOut) {
    *maxZOut = localMaxZ;
  }
  return paths->size();
}

struct TilePreviewState {
  enum class Mode { kSingleRaster, kSlippyQuadtree, kThreeDTilesMeta };
  std::wstring rootPath;
  std::wstring samplePath;
  std::wstring hint;
  std::wstring bvHint;
  HWND hintEdit = nullptr;
  std::unique_ptr<Gdiplus::Bitmap> bmp;
  Mode mode = Mode::kSingleRaster;
  std::unordered_map<uint64_t, std::wstring> slippyPaths;
  /// 解码后的瓦片面 LRU：front 最久未用；换 Z 时剔除非当前层键，控制常驻内存。
  std::list<uint64_t> tileTexLru;
  std::unordered_map<uint64_t, std::pair<std::list<uint64_t>::iterator, std::unique_ptr<Gdiplus::Bitmap>>> tileTexCache;
  int indexMaxZ = 0;
  size_t tileCount = 0;
  int viewZ = 0;
  double centerTx = 0.5;
  double centerTy = 0.5;
  /** 屏幕上每逻辑瓦片边长（像素）；接近 256 时与常见地图瓦片尺度一致。 */
  float pixelsPerTile = 256.f;
  bool dragging = false;
  POINT lastDragPt{};
};

static constexpr size_t kTilePreviewBitmapCacheMax = 160;

static std::wstring g_pendingTilePreviewRoot;

static RECT TileSlippyImageArea(RECT cr) {
  constexpr int kTopBar = 100;
  const int margin = 10;
  return {cr.left + margin, cr.top + kTopBar, cr.right - margin, cr.bottom - margin};
}

static void TilePruneBitmapCacheNotAtZ(TilePreviewState* st, int z) {
  if (!st) {
    return;
  }
  for (auto it = st->tileTexCache.begin(); it != st->tileTexCache.end();) {
    const uint64_t k = it->first;
    const int kz = static_cast<int>(static_cast<unsigned char>(k >> 56));
    if (kz != z) {
      st->tileTexLru.erase(it->second.first);
      it = st->tileTexCache.erase(it);
    } else {
      ++it;
    }
  }
}

static Gdiplus::Bitmap* TileBitmapCacheGet(TilePreviewState* st, uint64_t key, const std::wstring& tilePath) {
  if (!st) {
    return nullptr;
  }
  auto it = st->tileTexCache.find(key);
  if (it != st->tileTexCache.end()) {
    st->tileTexLru.erase(it->second.first);
    st->tileTexLru.push_back(key);
    it->second.first = std::prev(st->tileTexLru.end());
    Gdiplus::Bitmap* bm = it->second.second.get();
    if (bm && bm->GetLastStatus() == Gdiplus::Ok) {
      return bm;
    }
    st->tileTexLru.pop_back();
    st->tileTexCache.erase(it);
  }
  auto loaded = std::make_unique<Gdiplus::Bitmap>(tilePath.c_str());
  if (!loaded || loaded->GetLastStatus() != Gdiplus::Ok) {
    return nullptr;
  }
  Gdiplus::Bitmap* raw = loaded.get();
  st->tileTexLru.push_back(key);
  auto lit = std::prev(st->tileTexLru.end());
  st->tileTexCache[key] = std::make_pair(lit, std::move(loaded));
  while (st->tileTexCache.size() > kTilePreviewBitmapCacheMax) {
    const uint64_t old = st->tileTexLru.front();
    st->tileTexLru.pop_front();
    st->tileTexCache.erase(old);
  }
  return raw;
}

/// 以焦点（地图坐标归一化 nu,nv 保持不变）对齐切换缩放级，类似在线地图滚轮缩放。
static void TileZoomSlippy(TilePreviewState* st, RECT clientCr, const POINT* cursorClient, int dz) {
  if (!st || dz == 0) {
    return;
  }
  int newZ = st->viewZ + dz;
  newZ = (std::clamp)(newZ, 0, (std::max)(0, st->indexMaxZ));
  if (newZ == st->viewZ) {
    return;
  }
  const RECT img = TileSlippyImageArea(clientCr);
  const int aw = (std::max)(1, static_cast<int>(img.right - img.left));
  const int ah = (std::max)(1, static_cast<int>(img.bottom - img.top));
  POINT focusPt{};
  if (cursorClient && PtInRect(&img, *cursorClient)) {
    focusPt = *cursorClient;
  } else {
    focusPt.x = img.left + aw / 2;
    focusPt.y = img.top + ah / 2;
  }
  const float ppt = st->pixelsPerTile;
  const double worldW = static_cast<double>(aw) / static_cast<double>(ppt);
  const double worldH = static_cast<double>(ah) / static_cast<double>(ppt);
  const double worldLeft = st->centerTx - worldW * 0.5;
  const double worldTop = st->centerTy - worldH * 0.5;
  const int dim0 = 1 << st->viewZ;
  const double focX = static_cast<double>(focusPt.x - img.left);
  const double focY = static_cast<double>(focusPt.y - img.top);
  double nu = (worldLeft + focX / static_cast<double>(ppt)) / static_cast<double>((std::max)(1, dim0));
  double nv = (worldTop + focY / static_cast<double>(ppt)) / static_cast<double>((std::max)(1, dim0));
  nu = (std::clamp)(nu, 0.0, 1.0);
  nv = (std::clamp)(nv, 0.0, 1.0);
  st->viewZ = newZ;
  const int dim1 = 1 << newZ;
  st->centerTx = nu * static_cast<double>(dim1) + worldW * 0.5 - focX / static_cast<double>(ppt);
  st->centerTy = nv * static_cast<double>(dim1) + worldH * 0.5 - focY / static_cast<double>(ppt);
  const double lim = static_cast<double>(1 << newZ) + 4.0;
  st->centerTx = (std::clamp)(st->centerTx, -2.0, lim);
  st->centerTy = (std::clamp)(st->centerTy, -2.0, lim);
  TilePruneBitmapCacheNotAtZ(st, newZ);
}

static void TilePreviewLayoutHintEdit(HWND hwnd, TilePreviewState* st) {
  if (!st || !st->hintEdit || !IsWindow(st->hintEdit)) {
    return;
  }
  RECT cr{};
  GetClientRect(hwnd, &cr);
  const int hintH = (st->mode == TilePreviewState::Mode::kThreeDTilesMeta) ? 240 : 100;
  MoveWindow(st->hintEdit, 12, 10, (std::max)(40, static_cast<int>(cr.right) - 24), hintH, TRUE);
}

static void TilePreviewCreateHintEdit(HWND hwnd, TilePreviewState* st) {
  if (!st || st->hintEdit) {
    return;
  }
  st->hintEdit = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", st->hint.c_str(),
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL, 0, 0, 10,
      10, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (st->hintEdit && UiGetAppFont()) {
    SendMessageW(st->hintEdit, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
  }
  TilePreviewLayoutHintEdit(hwnd, st);
}

static void TilePaintSlippy(HDC hdc, RECT cr, TilePreviewState* st) {
  if (!st || st->slippyPaths.empty()) {
    return;
  }
  const RECT imgArea = TileSlippyImageArea(cr);
  const int aw = (std::max)(1, static_cast<int>(imgArea.right - imgArea.left));
  const int ah = (std::max)(1, static_cast<int>(imgArea.bottom - imgArea.top));
  const double worldW = static_cast<double>(aw) / static_cast<double>(st->pixelsPerTile);
  const double worldH = static_cast<double>(ah) / static_cast<double>(st->pixelsPerTile);
  const double worldLeft = st->centerTx - worldW * 0.5;
  const double worldTop = st->centerTy - worldH * 0.5;
  const int dim = 1 << st->viewZ;
  int tx0 = static_cast<int>(std::floor(worldLeft));
  int ty0 = static_cast<int>(std::floor(worldTop));
  int tx1 = static_cast<int>(std::ceil(worldLeft + worldW));
  int ty1 = static_cast<int>(std::ceil(worldTop + worldH));
  tx0 = (std::max)(0, tx0);
  ty0 = (std::max)(0, ty0);
  tx1 = (std::min)(dim - 1, tx1);
  ty1 = (std::min)(dim - 1, ty1);
  const int spanX = tx1 - tx0 + 1;
  const int spanY = ty1 - ty0 + 1;
  bool tooMany = spanX * spanY > 220;

  {
    Gdiplus::Graphics g(hdc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeLowQuality);
    Gdiplus::SolidBrush miss(Gdiplus::Color(255, 238, 240, 245));
    if (!tooMany) {
      for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
          const uint64_t key = PackTileKey(st->viewZ, tx, ty);
          auto pit = st->slippyPaths.find(key);
          const int sx =
              imgArea.left + static_cast<int>(std::lround((static_cast<double>(tx) - worldLeft) * st->pixelsPerTile));
          const int sy =
              imgArea.top + static_cast<int>(std::lround((static_cast<double>(ty) - worldTop) * st->pixelsPerTile));
          const int sw = (std::max)(1, static_cast<int>(std::ceil(st->pixelsPerTile)) + 1);
          if (pit == st->slippyPaths.end()) {
            g.FillRectangle(&miss, sx, sy, sw, sw);
            continue;
          }
          Gdiplus::Bitmap* bm = TileBitmapCacheGet(st, key, pit->second);
          if (bm) {
            g.DrawImage(bm, sx, sy, sw, sw);
          } else {
            g.FillRectangle(&miss, sx, sy, sw, sw);
          }
        }
      }
    }
  }
  if (HFONT f = UiGetAppFont()) {
    SelectObject(hdc, f);
  }
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(90, 60, 30));
  wchar_t status[320]{};
  if (tooMany) {
    swprintf_s(status, L"可视块过多(%d×%d)；请滚轮缩小（降低 Z）或 Ctrl+滚轮缩小片元。", spanX, spanY);
  } else {
    swprintf_s(status,
               L"Z=%d 可视 [%d..%d]×[%d..%d] 已索引 %zu | 拖拽平移 | 滚轮=换级(光标中心) | Shift+滚轮=视口中心换级 | Ctrl+滚轮=片元尺度",
               st->viewZ, tx0, tx1, ty0, ty1, st->tileCount);
  }
  RECT sr{imgArea.left, imgArea.top - 26, imgArea.right, imgArea.top - 4};
  DrawTextW(hdc, status, -1, &sr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
}

LRESULT CALLBACK TilePreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;
    case WM_CREATE: {
      auto* st = new TilePreviewState();
      st->rootPath = g_pendingTilePreviewRoot;
      g_pendingTilePreviewRoot.clear();
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
      std::error_code ecPath;
      const std::filesystem::path rootFs(st->rootPath);
#if GIS_DESKTOP_HAVE_GDAL
      if (std::filesystem::is_regular_file(rootFs, ecPath)) {
        const std::wstring ext = rootFs.extension().wstring();
        if (_wcsicmp(ext.c_str(), L".mbtiles") == 0 || _wcsicmp(ext.c_str(), L".gpkg") == 0) {
          std::wstring gdalDiag;
          if (auto bm = TryLoadGdalRasterTileContainerPreview(st->rootPath, &gdalDiag)) {
            st->bmp = std::move(bm);
            st->mode = TilePreviewState::Mode::kSingleRaster;
            std::wostringstream hs;
            hs << L"【MBTiles / GeoPackage】GDAL 栅格缩略预览（全球拼图下采样至 ≤2048px，非 z/x/y 交互瓦片）。\n路径：\n"
               << st->rootPath << L"\n\n纯矢量 GPKG 或无栅格层时会打开失败。";
            if (!gdalDiag.empty()) {
              hs << L"\n" << gdalDiag;
            }
            st->hint = hs.str();
            TilePreviewCreateHintEdit(hwnd, st);
            return 0;
          }
          st->hint = L"【MBTiles / GeoPackage】GDAL 预览失败：\n" + gdalDiag +
                     L"\n\n请检查：构建已启用 GDAL、MBTiles/GPKG 驱动、PROJ 与 gdal_data；或「系统默认打开」/ 导出 XYZ 目录再预览。";
          TilePreviewCreateHintEdit(hwnd, st);
          return 0;
        }
      }
#endif
      const std::optional<std::wstring> tilesetPathOpt = FindTilesetJsonPath(st->rootPath);
      std::wstring bvForTileset;
      if (tilesetPathOpt.has_value()) {
        bvForTileset = RoughTilesetBvHintForFile(*tilesetPathOpt);
      }
      st->bvHint = bvForTileset;
      bool tmsFlip = false;
      if (std::filesystem::is_directory(rootFs, ecPath)) {
        tmsFlip = DetectTmsTileLayoutOnDisk(rootFs);
      }
      st->tileCount = IndexSlippyQuadtree(st->rootPath, tmsFlip, &st->slippyPaths, &st->indexMaxZ);
      if (st->tileCount >= 1) {
        st->mode = TilePreviewState::Mode::kSlippyQuadtree;
        st->viewZ = (std::min)(st->indexMaxZ, 6);
        if (st->viewZ < st->indexMaxZ && st->tileCount < 4) {
          st->viewZ = st->indexMaxZ;
        }
        const double sz = double(1u << st->viewZ);
        st->centerTx = sz * 0.5;
        st->centerTy = sz * 0.5;
        std::wostringstream hs;
        hs << L"【平面四叉树 / XYZ 显示】已索引 " << st->tileCount << L" 个 z/x/y 图块（最多扫描 12000 文件）。\n";
        if (tmsFlip) {
          hs << L"已识别 TMS（存在 tms.xml 或 README 中 protocol=tms）：磁盘行号文件已按 XYZ（北在上）映射。\n";
        } else {
          hs << L"坐标系：与常见 XYZ 目录一致（行号 y 向南递增）。若仍颠倒，可能是非标准导出，可对照外部地图。\n";
        }
        if (!st->bvHint.empty()) {
          hs << st->bvHint << L"\n";
        }
        st->hint = hs.str();
      } else if (tilesetPathOpt.has_value()) {
        st->mode = TilePreviewState::Mode::kThreeDTilesMeta;
        st->hint = BuildThreeDTilesDashboard(st->rootPath, *tilesetPathOpt, bvForTileset);
      } else {
        const TileFindResult found = FindSampleTileRaster(st->rootPath);
        if (found.code == TileSampleResult::kContainerUnsupported) {
          st->hint = L"单文件 MBTiles / GeoPackage 无法用此窗口直接解码栅格图块。\n请使用「系统默认打开」，或先导出为含 "
                     L"PNG/JPG 的 XYZ/TMS 目录后再预览。";
        } else if (found.code == TileSampleResult::kOk && !found.path.empty()) {
          st->samplePath = found.path;
          auto loaded = std::make_unique<Gdiplus::Bitmap>(st->samplePath.c_str());
          if (loaded && loaded->GetLastStatus() == Gdiplus::Ok) {
            st->bmp = std::move(loaded);
            std::wostringstream hs;
            hs << L"单图采样（目录未识别 z/x/y 四叉结构）：\n" << st->samplePath;
            if (!st->bvHint.empty()) {
              hs << L"\n\n" << st->bvHint;
            }
            st->hint = hs.str();
          } else {
            st->hint = L"无法加载栅格文件：\n" + st->samplePath;
          }
        } else {
          std::wostringstream hs;
          hs << L"未在目录内找到 PNG/JPG 栅格（或未识别 z/x/y 路径）。\n";
          if (!st->bvHint.empty()) {
            hs << L"\n" << st->bvHint;
          } else {
            hs << L"\n3DTiles 请用 Cesium 或「系统默认打开」。";
          }
          st->hint = hs.str();
        }
      }
      TilePreviewCreateHintEdit(hwnd, st);
      return 0;
    }
    case WM_SIZE: {
      if (auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        TilePreviewLayoutHintEdit(hwnd, st);
      }
      return 0;
    }
    case WM_LBUTTONDOWN: {
      if (auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        if (st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
          st->dragging = true;
          st->lastDragPt.x = GET_X_LPARAM(lParam);
          st->lastDragPt.y = GET_Y_LPARAM(lParam);
          SetCapture(hwnd);
        }
      }
      return 0;
    }
    case WM_LBUTTONUP: {
      if (auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        if (st->dragging) {
          st->dragging = false;
          ReleaseCapture();
          InvalidateRect(hwnd, nullptr, FALSE);
        }
      }
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        if (st->mode == TilePreviewState::Mode::kSlippyQuadtree && st->dragging) {
          const int x = GET_X_LPARAM(lParam);
          const int y = GET_Y_LPARAM(lParam);
          const double dx = static_cast<double>(x - st->lastDragPt.x);
          const double dy = static_cast<double>(y - st->lastDragPt.y);
          st->centerTx -= dx / static_cast<double>(st->pixelsPerTile);
          st->centerTy -= dy / static_cast<double>(st->pixelsPerTile);
          st->lastDragPt.x = x;
          st->lastDragPt.y = y;
          const double lim = double(1 << st->viewZ) + 2.0;
          st->centerTx = (std::clamp)(st->centerTx, -1.0, lim);
          st->centerTy = (std::clamp)(st->centerTy, -1.0, lim);
          InvalidateRect(hwnd, nullptr, FALSE);
        }
      }
      return 0;
    }
    case WM_MOUSEWHEEL: {
      if (auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        if (st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
          const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
          const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
          const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
          POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
          ScreenToClient(hwnd, &pt);
          RECT cr{};
          GetClientRect(hwnd, &cr);
          if (ctrl) {
            const float factor = delta > 0 ? 1.08f : 0.92f;
            st->pixelsPerTile = (std::clamp)(st->pixelsPerTile * factor, 48.f, 520.f);
          } else {
            const int dz = delta > 0 ? 1 : -1;
            TileZoomSlippy(st, cr, shift ? nullptr : &pt, dz);
          }
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
      }
      break;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdcWnd = BeginPaint(hwnd, &ps);
      RECT cr{};
      GetClientRect(hwnd, &cr);
      const int cw = (std::max)(1, static_cast<int>(cr.right - cr.left));
      const int ch = (std::max)(1, static_cast<int>(cr.bottom - cr.top));
      HDC hdc = CreateCompatibleDC(hdcWnd);
      HBITMAP memBmp = CreateCompatibleBitmap(hdcWnd, cw, ch);
      HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(hdc, memBmp));
      HBRUSH bg = CreateSolidBrush(RGB(252, 252, 252));
      FillRect(hdc, &cr, bg);
      DeleteObject(bg);
      if (auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        if (HFONT f = UiGetAppFont()) {
          SelectObject(hdc, f);
        }
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(32, 42, 64));
        if (st->mode == TilePreviewState::Mode::kSlippyQuadtree) {
          TilePaintSlippy(hdc, cr, st);
        } else if (st->bmp && st->bmp->GetLastStatus() == Gdiplus::Ok) {
          const int margin = 12;
          RECT imgArea{cr.left + margin, cr.top + 110, cr.right - margin, cr.bottom - margin};
          const int aw = (std::max)(1, static_cast<int>(imgArea.right - imgArea.left));
          const int ah = (std::max)(1, static_cast<int>(imgArea.bottom - imgArea.top));
          const int iw = st->bmp->GetWidth();
          const int ih = st->bmp->GetHeight();
          if (iw > 0 && ih > 0) {
            Gdiplus::Graphics g(hdc);
            const float scale = (std::min)(static_cast<float>(aw) / static_cast<float>(iw),
                                           static_cast<float>(ah) / static_cast<float>(ih));
            const int dw = static_cast<int>(static_cast<float>(iw) * scale);
            const int dh = static_cast<int>(static_cast<float>(ih) * scale);
            const int dx = imgArea.left + (aw - dw) / 2;
            const int dy = imgArea.top + (ah - dh) / 2;
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.DrawImage(st->bmp.get(), dx, dy, dw, dh);
          }
        }
      }
      BitBlt(hdcWnd, 0, 0, cw, ch, hdc, 0, 0, SRCCOPY);
      SelectObject(hdc, oldBmp);
      DeleteObject(memBmp);
      DeleteDC(hdc);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      if (auto* st = reinterpret_cast<TilePreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        delete st;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void OpenTileRasterPreviewWindow(HWND owner, const std::wstring& path) {
  g_pendingTilePreviewRoot = path;
  const DWORD exStyle = owner ? WS_EX_TOOLWINDOW : 0;
  HWND tw = CreateWindowExW(exStyle, kTilePreviewClass, L"瓦片预览 · 四叉树 / BVH 元数据",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 960, 720, owner,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
  if (tw) {
    AgisCenterWindowInMonitorWorkArea(tw, owner ? owner : g_hwndMain);
    ShowWindow(tw, SW_SHOW);
  }
}

