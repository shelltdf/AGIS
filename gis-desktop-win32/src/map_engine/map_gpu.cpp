#include "map_engine/map_gpu.h"

#include <GL/gl.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <algorithm>
#include <cstring>
#include <memory>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "opengl32.lib")

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

namespace {

MapRenderBackend g_active = MapRenderBackend::kGdi;
HWND g_hwnd = nullptr;
int g_bufW = 0;
int g_bufH = 0;

struct D3D11State {
  ID3D11Device* dev = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  IDXGISwapChain* sc = nullptr;
  ID3D11RenderTargetView* rtv = nullptr;
  ID3D11Texture2D* tex = nullptr;
  ID3D11ShaderResourceView* srv = nullptr;
  ID3D11SamplerState* samp = nullptr;
  ID3D11VertexShader* vs = nullptr;
  ID3D11PixelShader* ps = nullptr;
  ID3D11InputLayout* il = nullptr;
  ID3D11Buffer* vb = nullptr;
  ID3D11RasterizerState* rs = nullptr;
  UINT tw = 0;
  UINT th = 0;
};

std::unique_ptr<D3D11State> g_d3d;

struct GLState {
  HDC hdc = nullptr;
  HGLRC rc = nullptr;
  GLuint tex = 0;
  int tw = 0;
  int th = 0;
};

std::unique_ptr<GLState> g_gl;

void ReleaseD3D() {
  if (!g_d3d) {
    return;
  }
  if (g_d3d->ctx) {
    g_d3d->ctx->ClearState();
    g_d3d->ctx->Flush();
  }
  if (g_d3d->rs) {
    g_d3d->rs->Release();
    g_d3d->rs = nullptr;
  }
  if (g_d3d->vb) {
    g_d3d->vb->Release();
    g_d3d->vb = nullptr;
  }
  if (g_d3d->il) {
    g_d3d->il->Release();
    g_d3d->il = nullptr;
  }
  if (g_d3d->vs) {
    g_d3d->vs->Release();
    g_d3d->vs = nullptr;
  }
  if (g_d3d->ps) {
    g_d3d->ps->Release();
    g_d3d->ps = nullptr;
  }
  if (g_d3d->samp) {
    g_d3d->samp->Release();
    g_d3d->samp = nullptr;
  }
  if (g_d3d->srv) {
    g_d3d->srv->Release();
    g_d3d->srv = nullptr;
  }
  if (g_d3d->tex) {
    g_d3d->tex->Release();
    g_d3d->tex = nullptr;
  }
  if (g_d3d->rtv) {
    g_d3d->rtv->Release();
    g_d3d->rtv = nullptr;
  }
  if (g_d3d->sc) {
    g_d3d->sc->Release();
    g_d3d->sc = nullptr;
  }
  if (g_d3d->ctx) {
    g_d3d->ctx->Release();
    g_d3d->ctx = nullptr;
  }
  if (g_d3d->dev) {
    g_d3d->dev->Release();
    g_d3d->dev = nullptr;
  }
  g_d3d.reset();
}

bool CreateD3D11Rtv() {
  if (!g_d3d || !g_d3d->sc || !g_d3d->dev) {
    return false;
  }
  ID3D11Texture2D* bb = nullptr;
  HRESULT hr = g_d3d->sc->GetBuffer(0, IID_PPV_ARGS(&bb));
  if (FAILED(hr) || !bb) {
    return false;
  }
  hr = g_d3d->dev->CreateRenderTargetView(bb, nullptr, &g_d3d->rtv);
  bb->Release();
  return SUCCEEDED(hr) && g_d3d->rtv;
}

bool EnsureD3D11Texture(UINT w, UINT h) {
  if (!g_d3d || !g_d3d->dev || !g_d3d->ctx) {
    return false;
  }
  if (g_d3d->tex && g_d3d->tw == w && g_d3d->th == h) {
    return true;
  }
  if (g_d3d->srv) {
    g_d3d->srv->Release();
    g_d3d->srv = nullptr;
  }
  if (g_d3d->tex) {
    g_d3d->tex->Release();
    g_d3d->tex = nullptr;
  }
  g_d3d->tw = w;
  g_d3d->th = h;
  D3D11_TEXTURE2D_DESC td{};
  td.Width = w;
  td.Height = h;
  td.MipLevels = 1;
  td.ArraySize = 1;
  td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DYNAMIC;
  td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  HRESULT hr = g_d3d->dev->CreateTexture2D(&td, nullptr, &g_d3d->tex);
  if (FAILED(hr) || !g_d3d->tex) {
    return false;
  }
  D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
  sv.Format = td.Format;
  sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  sv.Texture2D.MipLevels = 1;
  hr = g_d3d->dev->CreateShaderResourceView(g_d3d->tex, &sv, &g_d3d->srv);
  return SUCCEEDED(hr) && g_d3d->srv;
}

bool InitD3D11(HWND hwnd) {
  g_d3d = std::make_unique<D3D11State>();
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 1;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  RECT r{};
  GetClientRect(hwnd, &r);
  sd.BufferDesc.Width = static_cast<UINT>(std::max(1L, r.right - r.left));
  sd.BufferDesc.Height = static_cast<UINT>(std::max(1L, r.bottom - r.top));

  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  D3D_FEATURE_LEVEL fls[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL flOut{};
  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fls,
                                           ARRAYSIZE(fls), D3D11_SDK_VERSION, &sd, &g_d3d->sc, &g_d3d->dev, &flOut,
                                           &g_d3d->ctx);
  if (FAILED(hr)) {
    ReleaseD3D();
    return false;
  }

  if (!CreateD3D11Rtv()) {
    ReleaseD3D();
    return false;
  }

  static const char kVs[] = R"(
struct VS_IN { float2 pos : POSITION; float2 uv : TEXCOORD0; };
struct VS_OUT { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VS_OUT main(VS_IN vin) {
  VS_OUT o;
  o.pos = float4(vin.pos, 0.0f, 1.0f);
  o.uv = vin.uv;
  return o;
}
)";
  static const char kPs[] = R"(
Texture2D tex : register(t0);
SamplerState samp : register(s0);
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target {
  return tex.Sample(samp, uv);
}
)";

  ID3DBlob *vsBlob = nullptr, *psBlob = nullptr, *errBlob = nullptr;
  hr = D3DCompile(kVs, std::strlen(kVs), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vsBlob, &errBlob);
  if (FAILED(hr) || !vsBlob) {
    if (errBlob) {
      errBlob->Release();
    }
    ReleaseD3D();
    return false;
  }
  errBlob = nullptr;
  hr = D3DCompile(kPs, std::strlen(kPs), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &psBlob, &errBlob);
  if (FAILED(hr) || !psBlob) {
    vsBlob->Release();
    if (errBlob) {
      errBlob->Release();
    }
    ReleaseD3D();
    return false;
  }

  D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  hr = g_d3d->dev->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_d3d->il);
  if (FAILED(hr)) {
    vsBlob->Release();
    psBlob->Release();
    ReleaseD3D();
    return false;
  }

  hr = g_d3d->dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_d3d->vs);
  vsBlob->Release();
  vsBlob = nullptr;
  if (FAILED(hr)) {
    psBlob->Release();
    ReleaseD3D();
    return false;
  }

  hr = g_d3d->dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_d3d->ps);
  psBlob->Release();
  psBlob = nullptr;
  if (FAILED(hr)) {
    ReleaseD3D();
    return false;
  }

  struct Vertex {
    float x, y, u, v;
  };
  Vertex quad[] = {
      {-1.0f, -1.0f, 0.0f, 1.0f},
      {1.0f, -1.0f, 1.0f, 1.0f},
      {-1.0f, 1.0f, 0.0f, 0.0f},
      {1.0f, 1.0f, 1.0f, 0.0f},
  };
  D3D11_BUFFER_DESC bd{};
  bd.ByteWidth = sizeof(quad);
  bd.Usage = D3D11_USAGE_IMMUTABLE;
  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_SUBRESOURCE_DATA init{};
  init.pSysMem = quad;
  hr = g_d3d->dev->CreateBuffer(&bd, &init, &g_d3d->vb);
  if (FAILED(hr)) {
    ReleaseD3D();
    return false;
  }

  D3D11_SAMPLER_DESC sampd{};
  sampd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  hr = g_d3d->dev->CreateSamplerState(&sampd, &g_d3d->samp);
  if (FAILED(hr)) {
    ReleaseD3D();
    return false;
  }

  D3D11_RASTERIZER_DESC rd{};
  rd.FillMode = D3D11_FILL_SOLID;
  rd.CullMode = D3D11_CULL_NONE;
  hr = g_d3d->dev->CreateRasterizerState(&rd, &g_d3d->rs);
  if (FAILED(hr)) {
    ReleaseD3D();
    return false;
  }

  return true;
}

bool PresentD3D11(const uint8_t* bgraTopDown, int w, int h) {
  if (!g_d3d || !g_d3d->ctx || !g_d3d->sc || !g_d3d->rtv || w <= 0 || h <= 0) {
    return false;
  }
  if (!EnsureD3D11Texture(static_cast<UINT>(w), static_cast<UINT>(h))) {
    return false;
  }

  D3D11_MAPPED_SUBRESOURCE mapped{};
  HRESULT hr = g_d3d->ctx->Map(g_d3d->tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr)) {
    return false;
  }
  auto* dst = static_cast<uint8_t*>(mapped.pData);
  const UINT stride = mapped.RowPitch;
  const size_t rowBytes = static_cast<size_t>(w) * 4u;
  for (int y = 0; y < h; ++y) {
    std::memcpy(dst + static_cast<size_t>(y) * stride, bgraTopDown + static_cast<size_t>(y) * rowBytes, rowBytes);
  }
  g_d3d->ctx->Unmap(g_d3d->tex, 0);

  FLOAT clear[4] = {0.15f, 0.15f, 0.18f, 1.0f};
  g_d3d->ctx->OMSetRenderTargets(1, &g_d3d->rtv, nullptr);
  D3D11_VIEWPORT vp{};
  vp.Width = static_cast<FLOAT>(g_bufW > 0 ? g_bufW : w);
  vp.Height = static_cast<FLOAT>(g_bufH > 0 ? g_bufH : h);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  g_d3d->ctx->RSSetViewports(1, &vp);
  if (g_d3d->rs) {
    g_d3d->ctx->RSSetState(g_d3d->rs);
  }
  g_d3d->ctx->ClearRenderTargetView(g_d3d->rtv, clear);

  UINT strideV = sizeof(float) * 4u;
  UINT off = 0;
  g_d3d->ctx->IASetInputLayout(g_d3d->il);
  g_d3d->ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  g_d3d->ctx->IASetVertexBuffers(0, 1, &g_d3d->vb, &strideV, &off);
  g_d3d->ctx->VSSetShader(g_d3d->vs, nullptr, 0);
  g_d3d->ctx->PSSetShader(g_d3d->ps, nullptr, 0);
  ID3D11ShaderResourceView* srv = g_d3d->srv;
  g_d3d->ctx->PSSetShaderResources(0, 1, &srv);
  g_d3d->ctx->PSSetSamplers(0, 1, &g_d3d->samp);
  g_d3d->ctx->Draw(4, 0);

  hr = g_d3d->sc->Present(0, 0);
  return SUCCEEDED(hr);
}

void ReleaseGL(HWND hwnd) {
  if (!g_gl) {
    return;
  }
  if (g_gl->hdc && g_gl->rc) {
    wglMakeCurrent(g_gl->hdc, g_gl->rc);
    if (g_gl->tex != 0) {
      glDeleteTextures(1, &g_gl->tex);
      g_gl->tex = 0;
    }
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(g_gl->rc);
    g_gl->rc = nullptr;
  }
  if (g_gl->hdc && hwnd) {
    ReleaseDC(hwnd, g_gl->hdc);
    g_gl->hdc = nullptr;
  }
  g_gl.reset();
}

bool InitGL(HWND hwnd) {
  g_gl = std::make_unique<GLState>();
  g_gl->hdc = GetDC(hwnd);
  if (!g_gl->hdc) {
    g_gl.reset();
    return false;
  }
  PIXELFORMATDESCRIPTOR pfd{};
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | 0x00000001;  // PFD_DOUBLE_BUFFER
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cDepthBits = 24;
  const int pf = ChoosePixelFormat(g_gl->hdc, &pfd);
  if (!pf || !SetPixelFormat(g_gl->hdc, pf, &pfd)) {
    ReleaseDC(hwnd, g_gl->hdc);
    g_gl.reset();
    return false;
  }
  g_gl->rc = wglCreateContext(g_gl->hdc);
  if (!g_gl->rc) {
    ReleaseDC(hwnd, g_gl->hdc);
    g_gl.reset();
    return false;
  }
  if (!wglMakeCurrent(g_gl->hdc, g_gl->rc)) {
    wglDeleteContext(g_gl->rc);
    ReleaseDC(hwnd, g_gl->hdc);
    g_gl.reset();
    return false;
  }
  glEnable(GL_TEXTURE_2D);
  glGenTextures(1, &g_gl->tex);
  wglMakeCurrent(nullptr, nullptr);
  return true;
}

bool PresentGL(const uint8_t* bgraTopDown, int w, int h) {
  if (!g_gl || !g_gl->hdc || !g_gl->rc || w <= 0 || h <= 0) {
    return false;
  }
  if (!wglMakeCurrent(g_gl->hdc, g_gl->rc)) {
    return false;
  }
  glViewport(0, 0, g_bufW > 0 ? g_bufW : w, g_bufH > 0 ? g_bufH : h);
  glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glBindTexture(GL_TEXTURE_2D, g_gl->tex);
  if (g_gl->tw != w || g_gl->th != h) {
    glTexImage2D(GL_TEXTURE_2D, 0, 4, w, h, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, bgraTopDown);
    g_gl->tw = w;
    g_gl->th = h;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA_EXT, GL_UNSIGNED_BYTE, bgraTopDown);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glBegin(GL_TRIANGLE_STRIP);
  glTexCoord2f(0.0f, 1.0f);
  glVertex2f(-1.0f, -1.0f);
  glTexCoord2f(1.0f, 1.0f);
  glVertex2f(1.0f, -1.0f);
  glTexCoord2f(0.0f, 0.0f);
  glVertex2f(-1.0f, 1.0f);
  glTexCoord2f(1.0f, 0.0f);
  glVertex2f(1.0f, 1.0f);
  glEnd();

  SwapBuffers(g_gl->hdc);
  wglMakeCurrent(nullptr, nullptr);
  return true;
}

}  // namespace

bool MapGpu_Init(HWND hwnd, MapRenderBackend backend) {
  MapGpu_Shutdown(hwnd);
  g_hwnd = hwnd;
  g_active = MapRenderBackend::kGdi;
  g_bufW = 0;
  g_bufH = 0;
  if (!hwnd || backend == MapRenderBackend::kGdi) {
    return true;
  }
  RECT r{};
  GetClientRect(hwnd, &r);
  g_bufW = r.right - r.left;
  g_bufH = r.bottom - r.top;

  if (backend == MapRenderBackend::kD3d11) {
    if (InitD3D11(hwnd)) {
      g_active = MapRenderBackend::kD3d11;
      return true;
    }
    return false;
  }
  if (backend == MapRenderBackend::kOpenGL) {
    if (InitGL(hwnd)) {
      g_active = MapRenderBackend::kOpenGL;
      return true;
    }
    return false;
  }
  return true;
}

void MapGpu_Shutdown(HWND hwnd) {
  HWND hRel = hwnd ? hwnd : g_hwnd;
  ReleaseD3D();
  if (g_gl && hRel) {
    ReleaseGL(hRel);
  }
  g_hwnd = nullptr;
  g_active = MapRenderBackend::kGdi;
  g_bufW = 0;
  g_bufH = 0;
}

void MapGpu_OnResize(int w, int h) {
  g_bufW = w;
  g_bufH = h;
  if (g_active != MapRenderBackend::kD3d11 || !g_d3d || !g_d3d->sc || !g_d3d->ctx) {
    return;
  }
  if (w <= 0 || h <= 0) {
    return;
  }
  g_d3d->ctx->OMSetRenderTargets(0, nullptr, nullptr);
  if (g_d3d->rtv) {
    g_d3d->rtv->Release();
    g_d3d->rtv = nullptr;
  }
  HRESULT hr = g_d3d->sc->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_UNKNOWN, 0);
  if (FAILED(hr)) {
    return;
  }
  CreateD3D11Rtv();
}

bool MapGpu_PresentFrame(HWND hwnd, const uint8_t* bgraTopDown, int w, int h) {
  (void)hwnd;
  if (!bgraTopDown || w <= 0 || h <= 0) {
    return false;
  }
  if (g_active == MapRenderBackend::kGdi) {
    return false;
  }
  if (g_active == MapRenderBackend::kD3d11) {
    return PresentD3D11(bgraTopDown, w, h);
  }
  if (g_active == MapRenderBackend::kOpenGL) {
    return PresentGL(bgraTopDown, w, h);
  }
  return false;
}

MapRenderBackend MapGpu_GetActiveBackend() { return g_active; }
