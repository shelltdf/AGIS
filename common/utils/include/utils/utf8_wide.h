#pragma once

/** Windows 下 UTF-8（窄）与 UTF-16（宽）互转；供 GDAL/CPL 等 UTF-8 接口与 UI 宽字符串衔接。 */

#include <windows.h>

#include <string>

inline std::string Utf8FromWide(const std::wstring& w) {
  if (w.empty()) {
    return {};
  }
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (n <= 0) {
    return {};
  }
  std::string out(static_cast<size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
  return out;
}

inline std::wstring WideFromUtf8(const char* s) {
  if (!s || !s[0]) {
    return {};
  }
  const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
  if (n <= 0) {
    return {};
  }
  std::wstring w(static_cast<size_t>(n - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
  return w;
}
