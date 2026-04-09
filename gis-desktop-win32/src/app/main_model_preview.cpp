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
#include <cwctype>
#include <new>

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

#include "app/model_preview_types.h"
#if AGIS_USE_BGFX
#include "app/model_preview_bgfx.h"
#include "imgui/imgui.h"
#endif
#include "app/ui_font.h"
#include "main_app.h"
#include "main_globals.h"

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
};

constexpr UINT kPreviewLoadedMsg = WM_APP + 201;
struct PreviewLoadCtx {
  HWND hwnd = nullptr;
  ModelPreviewState* st = nullptr;
};

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
  swprintf_s(line3, L"Renderer:%s | Solid:%s | Grid:%s | Texture:%s | Render:%s | Layer:%s", rendererText,
             st.solid ? L"on" : L"off", st.showGrid ? L"on" : L"off", st.useTexture ? L"on" : L"off", pbrViewText, texLayer);
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
  st->loadStage = 1;
  st->loadProgress = 8;
  st->loadedStats = ScanObjStats(st->path);
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
  st->loadProgress = 96;
  st->textureLayers = DetectTextureLayers(st->loadedModel);
  st->loadStage = 4;
  st->loadProgress = 100;
  PostMessageW(ctx->hwnd, kPreviewLoadedMsg, 1, 0);
  delete ctx;
  return 0;
}

LRESULT CALLBACK ModelPreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  constexpr int kPreviewModeComboId = 1;
  constexpr int kPreviewInfoEditId = 2;
  constexpr int kPreviewModeTextId = 3;
  constexpr int kPreviewSolidCheckId = 4;
  constexpr int kPreviewRuntimeTextId = 5;
  constexpr int kPreviewResetBtnId = 6;
  constexpr int kPreviewFitBtnId = 7;
  constexpr int kPreviewTexBtnId = 8;
  constexpr int kPreviewTexEnableCheckId = 9;
  constexpr int kPreviewGridCheckId = 10;
  constexpr int kPreviewTexLayerComboId = 11;
  switch (msg) {
    case WM_CREATE: {
      auto* st = new ModelPreviewState();
      st->path = g_pendingPreviewModelPath;
      st->lastFpsTick = GetTickCount();
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
      CreateWindowW(L"STATIC", L"渲染模式：", WS_CHILD | WS_VISIBLE, 12, 12, 72, 20, hwnd, nullptr,
                    GetModuleHandleW(nullptr), nullptr);
      HWND mode = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 88, 10, 180, 140,
                                hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewModeComboId)),
                                GetModuleHandleW(nullptr), nullptr);
#if AGIS_USE_BGFX
      SendMessageW(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"bgfx - Direct3D 11"));
      SendMessageW(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"bgfx - OpenGL"));
#else
      SendMessageW(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"内置渲染 - OpenGL"));
      SendMessageW(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"内置渲染 - DirectX11"));
#endif
      SendMessageW(mode, CB_SETCURSEL, 0, 0);
      CreateWindowW(L"BUTTON", L"实体填充", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 280, 10, 88, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewSolidCheckId)), GetModuleHandleW(nullptr), nullptr);
      SendMessageW(GetDlgItem(hwnd, kPreviewSolidCheckId), BM_SETCHECK, BST_CHECKED, 0);
      CreateWindowW(L"BUTTON", L"重置视角", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 372, 10, 76, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewResetBtnId)), GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"BUTTON", L"适配模型", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 372, 34, 76, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewFitBtnId)), GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"BUTTON", L"预览贴图", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 280, 34, 88, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewTexBtnId)), GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"BUTTON", L"贴图", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 456, 10, 56, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewTexEnableCheckId)), GetModuleHandleW(nullptr),
                    nullptr);
      SendMessageW(GetDlgItem(hwnd, kPreviewTexEnableCheckId), BM_SETCHECK, BST_CHECKED, 0);
      CreateWindowW(L"BUTTON", L"网格", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 516, 10, 56, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewGridCheckId)), GetModuleHandleW(nullptr), nullptr);
      SendMessageW(GetDlgItem(hwnd, kPreviewGridCheckId), BM_SETCHECK, BST_CHECKED, 0);
      HWND texLayer =
          CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 456, 34, 180, 140, hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewTexLayerComboId)),
                        GetModuleHandleW(nullptr), nullptr);
#if AGIS_USE_BGFX
      CreateWindowW(L"STATIC", L"当前渲染器: Direct3D 11（bgfx）", WS_CHILD | WS_VISIBLE, 280, 12, 170, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewModeTextId)), GetModuleHandleW(nullptr), nullptr);
#else
      CreateWindowW(L"STATIC", L"当前渲染器: OpenGL（内置）", WS_CHILD | WS_VISIBLE, 280, 12, 170, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewModeTextId)), GetModuleHandleW(nullptr), nullptr);
#endif
      HWND info = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL |
                                               WS_VSCROLL | ES_READONLY,
                                12, 270, 436, 220, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewInfoEditId)),
                                GetModuleHandleW(nullptr), nullptr);
      SendMessageW(mode, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
      SendMessageW(info, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
      SendMessageW(texLayer, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
      SetWindowTextW(info, L"模型正在后台加载，请稍候...");
      FitPreviewCamera(st);
      CreateWindowW(L"STATIC", L"FPS: -- | 帧时: -- ms | 瓶颈: -- | 曲线: --", WS_CHILD | WS_VISIBLE, 12, 494, 436, 20, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewRuntimeTextId)),
                    GetModuleHandleW(nullptr), nullptr);
      if (HWND rt = GetDlgItem(hwnd, kPreviewRuntimeTextId)) {
        SendMessageW(rt, WM_SETFONT, reinterpret_cast<WPARAM>(UiGetAppFont()), TRUE);
      }
      // 2D/3D 预览窗口统一走“渲染内 UI（类 ImGui）”，隐藏传统 Win32 控件。
      for (HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        ShowWindow(c, SW_HIDE);
      }
      SetFocus(hwnd);
      SetTimer(hwnd, 1, 33, nullptr);
      auto* loadCtx = new (std::nothrow) PreviewLoadCtx{hwnd, st};
      if (!loadCtx) {
        MessageBoxW(hwnd, L"内存不足：无法启动模型后台加载。", L"模型预览", MB_OK | MB_ICONERROR);
        return -1;
      }
      st->loadThread = CreateThread(nullptr, 0, PreviewLoadThreadProc, loadCtx, 0, nullptr);
      if (!st->loadThread) {
        delete loadCtx;
        MessageBoxW(hwnd, L"无法启动模型加载线程。", L"模型预览", MB_OK | MB_ICONERROR);
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
    case WM_COMMAND:
      if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        if (st->loading) return 0;
      }
      if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == kPreviewModeComboId) {
        HWND mode = GetDlgItem(hwnd, kPreviewModeComboId);
        const int sel = static_cast<int>(SendMessageW(mode, CB_GETCURSEL, 0, 0));
        if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
#if AGIS_USE_BGFX
          const AgisBgfxRendererKind kind = (sel == 1) ? AgisBgfxRendererKind::kOpenGL : AgisBgfxRendererKind::kD3D11;
          st->bgfxRenderer = kind;
          if (st->bgfxCtx) {
            if (st->imguiReady) {
              imguiDestroy();
              st->imguiReady = false;
            }
            agis_bgfx_preview_shutdown(hwnd, st->bgfxCtx);
            st->bgfxCtx = nullptr;
          }
          if (!agis_bgfx_preview_init(hwnd, &st->bgfxCtx, kind, st->model)) {
            MessageBoxW(hwnd, L"切换渲染器后初始化失败。", L"模型预览", MB_OK | MB_ICONWARNING);
          }
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
          st->backend = (sel == 1) ? PreviewRenderBackend::kDx11 : PreviewRenderBackend::kOpenGL;
          if (st->backend == PreviewRenderBackend::kOpenGL) {
            ReleasePreviewDx(st);
            InitPreviewGl(hwnd, st);
          } else {
            ReleasePreviewGl(hwnd, st);
            InitPreviewDx(hwnd, st);
          }
#endif
        }
#if AGIS_USE_BGFX
        SetWindowTextW(GetDlgItem(hwnd, kPreviewModeTextId),
                       sel == 1 ? L"当前渲染器: OpenGL（bgfx）" : L"当前渲染器: Direct3D 11（bgfx）");
#else
        SetWindowTextW(GetDlgItem(hwnd, kPreviewModeTextId),
                       sel == 1 ? L"当前渲染器: DirectX11（内置）" : L"当前渲染器: OpenGL（内置）");
#endif
        RECT vrc = GetPreviewViewportRect(hwnd);
        InvalidateRect(hwnd, &vrc, FALSE);
        return 0;
      }
      if (LOWORD(wParam) == kPreviewSolidCheckId && HIWORD(wParam) == BN_CLICKED) {
        if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
          st->solid = (SendMessageW(GetDlgItem(hwnd, kPreviewSolidCheckId), BM_GETCHECK, 0, 0) == BST_CHECKED);
          RECT vrc = GetPreviewViewportRect(hwnd);
          InvalidateRect(hwnd, &vrc, FALSE);
        }
        return 0;
      }
      if (LOWORD(wParam) == kPreviewGridCheckId && HIWORD(wParam) == BN_CLICKED) {
        if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
          st->showGrid = (SendMessageW(GetDlgItem(hwnd, kPreviewGridCheckId), BM_GETCHECK, 0, 0) == BST_CHECKED);
          RECT vrc = GetPreviewViewportRect(hwnd);
          InvalidateRect(hwnd, &vrc, FALSE);
        }
        return 0;
      }
      if (LOWORD(wParam) == kPreviewTexEnableCheckId && HIWORD(wParam) == BN_CLICKED) {
        if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
          st->useTexture = (SendMessageW(GetDlgItem(hwnd, kPreviewTexEnableCheckId), BM_GETCHECK, 0, 0) == BST_CHECKED);
#if AGIS_USE_BGFX
          if (st->bgfxCtx) {
            if (st->useTexture && st->currentTextureLayer < static_cast<int>(st->textureLayers.size())) {
              agis_bgfx_preview_set_texture(st->bgfxCtx, st->textureLayers[st->currentTextureLayer].second);
            } else {
              agis_bgfx_preview_set_texture(st->bgfxCtx, L"");
            }
          }
#endif
          RECT vrc = GetPreviewViewportRect(hwnd);
          InvalidateRect(hwnd, &vrc, FALSE);
        }
        return 0;
      }
      if (LOWORD(wParam) == kPreviewTexLayerComboId && HIWORD(wParam) == CBN_SELCHANGE) {
        if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
          st->currentTextureLayer = static_cast<int>(SendMessageW(GetDlgItem(hwnd, kPreviewTexLayerComboId), CB_GETCURSEL, 0, 0));
          if (st->currentTextureLayer < 0) st->currentTextureLayer = 0;
#if AGIS_USE_BGFX
          if (st->bgfxCtx && st->useTexture && st->currentTextureLayer < static_cast<int>(st->textureLayers.size())) {
            agis_bgfx_preview_set_texture(st->bgfxCtx, st->textureLayers[st->currentTextureLayer].second);
          }
#endif
          RECT vrc = GetPreviewViewportRect(hwnd);
          InvalidateRect(hwnd, &vrc, FALSE);
        }
        return 0;
      }
      if (LOWORD(wParam) == kPreviewResetBtnId && HIWORD(wParam) == BN_CLICKED) {
        if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
          st->rotX = 0.0f;
          st->rotY = 0.0f;
          st->zoom = 2.2f;
          RECT vrc = GetPreviewViewportRect(hwnd);
          InvalidateRect(hwnd, &vrc, FALSE);
        }
        return 0;
      }
      if (LOWORD(wParam) == kPreviewFitBtnId && HIWORD(wParam) == BN_CLICKED) {
        if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
          FitPreviewCamera(st);
          RECT vrc = GetPreviewViewportRect(hwnd);
          InvalidateRect(hwnd, &vrc, FALSE);
        }
        return 0;
      }
      if (LOWORD(wParam) == kPreviewTexBtnId && HIWORD(wParam) == BN_CLICKED) {
        if (auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
          if (!st->model.primaryMapKdPath.empty()) {
            ShellExecuteW(hwnd, L"open", st->model.primaryMapKdPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          } else {
            MessageBoxW(hwnd, L"当前模型未检测到 map_Kd 贴图路径。", L"预览贴图", MB_OK | MB_ICONINFORMATION);
          }
        }
        return 0;
      }
      break;
    case kPreviewLoadedMsg: {
      auto* st = reinterpret_cast<ModelPreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      if (!st) return 0;
      if (st->loadThread) {
        CloseHandle(st->loadThread);
        st->loadThread = nullptr;
      }
      st->loading = false;
      if (wParam == 0 || st->loadFailed) {
        MessageBoxW(hwnd, L"模型解析失败：OBJ 文件损坏或缺少有效顶点/面。", L"模型预览", MB_OK | MB_ICONWARNING);
        DestroyWindow(hwnd);
        return 0;
      }
      st->model = std::move(st->loadedModel);
      st->stats = st->loadedStats;
      st->loadStage = 4;
      if (HWND info = GetDlgItem(hwnd, kPreviewInfoEditId)) {
        SetWindowTextW(info, BuildObjInfoText(st->path).c_str());
      }
      if (HWND texLayer = GetDlgItem(hwnd, kPreviewTexLayerComboId)) {
        SendMessageW(texLayer, CB_RESETCONTENT, 0, 0);
        for (const auto& kv : st->textureLayers) {
          SendMessageW(texLayer, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kv.first.c_str()));
        }
        if (!st->textureLayers.empty()) {
          SendMessageW(texLayer, CB_SETCURSEL, 0, 0);
        }
      }
#if AGIS_USE_BGFX
      if (!agis_bgfx_preview_init(hwnd, &st->bgfxCtx, AgisBgfxRendererKind::kD3D11, st->model)) {
        MessageBoxW(hwnd, L"3D 预览初始化失败：bgfx 或网格数据无效（请确认 OBJ 含顶点与面）。", L"模型预览",
                    MB_OK | MB_ICONWARNING);
        DestroyWindow(hwnd);
        return 0;
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
      RECT rc{};
      GetClientRect(hwnd, &rc);
      const int clientRight = static_cast<int>(rc.right);
      const int clientBottom = static_cast<int>(rc.bottom);
      const int margin = 12;
      const int runtimeH = 20;
      const int infoH = (std::clamp<int>)((clientBottom - 74) / 3, 120, 240);
      const int runtimeTop = (std::max)(140, clientBottom - margin - runtimeH);
      const int infoTop = (std::max)(120, runtimeTop - 8 - infoH);
      const int infoW = (std::max)(120, clientRight - margin * 2);
      if (HWND info = GetDlgItem(hwnd, kPreviewInfoEditId)) {
        MoveWindow(info, margin, infoTop, infoW, infoH, TRUE);
      }
      if (HWND rt = GetDlgItem(hwnd, kPreviewRuntimeTextId)) {
        MoveWindow(rt, margin, runtimeTop, infoW, runtimeH, TRUE);
      }
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
          const std::wstring line = L"模型加载中... " + std::to_wstring((std::clamp)(st->loadProgress, 0, 100)) + L"%";
          const wchar_t* stage = L"准备中...";
          switch (st->loadStage) {
            case 1: stage = L"正在统计模型信息..."; break;
            case 2: stage = L"正在解析 OBJ 网格..."; break;
            case 3: stage = L"正在分析贴图层..."; break;
            case 4: stage = L"正在初始化渲染资源..."; break;
            case 9: stage = L"模型解析失败"; break;
            default: break;
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
          ImGui::End();
          imguiEndFrame();
        }
        agis_bgfx_preview_draw(st->bgfxCtx, hwnd, vrc, st->rotX, st->rotY, st->zoom, st->solid, st->showGrid);
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
          SetWindowTextW(GetDlgItem(hwnd, kPreviewRuntimeTextId), line);
        }
        DrawAxisOverlay(hdc, vrc, *st);
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
  const ObjPreviewStats stat = ScanObjStats(path);
  constexpr uint64_t kPreviewFaceHardLimit = 5000000ull;
  if (stat.faces > kPreviewFaceHardLimit) {
    wchar_t msg[512]{};
    swprintf_s(msg,
               L"当前 OBJ 面片过大（%llu），直接打开预览会导致卡死风险。\n请提高 mesh-spacing 或使用更粗网格后再预览（建议 <= %llu 面）。",
               static_cast<unsigned long long>(stat.faces), static_cast<unsigned long long>(kPreviewFaceHardLimit));
    MessageBoxW(owner, msg, L"模型预览", MB_OK | MB_ICONWARNING);
    return;
  }
  g_pendingPreviewModelPath = path;
  CreateWindowExW(WS_EX_TOOLWINDOW, kModelPreviewClass, L"模型数据预览",
                  WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
                  CW_USEDEFAULT, CW_USEDEFAULT, 960, 720, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
}
