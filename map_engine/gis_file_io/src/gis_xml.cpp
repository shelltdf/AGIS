#include "map_engine/gis_xml.h"

#include <cwchar>
#include <string>

std::wstring XmlEscape(const std::wstring& s) {
  std::wstring out;
  out.reserve(s.size() + 16);
  for (wchar_t ch : s) {
    switch (ch) {
      case L'&':
        out += L"&amp;";
        break;
      case L'<':
        out += L"&lt;";
        break;
      case L'>':
        out += L"&gt;";
        break;
      case L'"':
        out += L"&quot;";
        break;
      case L'\'':
        out += L"&apos;";
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  return out;
}

std::wstring XmlUnescape(const std::wstring& s) {
  std::wstring out = s;
  auto rep = [&out](const wchar_t* from, const wchar_t* to) {
    size_t pos = 0;
    const size_t fromLen = wcslen(from);
    const size_t toLen = wcslen(to);
    while ((pos = out.find(from, pos)) != std::wstring::npos) {
      out.replace(pos, fromLen, to);
      pos += toLen;
    }
  };
  rep(L"&lt;", L"<");
  rep(L"&gt;", L">");
  rep(L"&quot;", L"\"");
  rep(L"&apos;", L"'");
  rep(L"&amp;", L"&");
  return out;
}

std::wstring GetXmlAttr(const std::wstring& line, const wchar_t* key) {
  const std::wstring k = std::wstring(key) + L"=\"";
  const size_t p0 = line.find(k);
  if (p0 == std::wstring::npos) {
    return L"";
  }
  const size_t p1 = p0 + k.size();
  const size_t p2 = line.find(L"\"", p1);
  if (p2 == std::wstring::npos || p2 <= p1) {
    return L"";
  }
  return XmlUnescape(line.substr(p1, p2 - p1));
}

bool ParseBoolAttr(const std::wstring& line, const wchar_t* key, bool def) {
  const std::wstring s = GetXmlAttr(line, key);
  if (s.empty()) {
    return def;
  }
  return !(s == L"0" || s == L"false" || s == L"False" || s == L"FALSE");
}

int ParseIntAttr(const std::wstring& line, const wchar_t* key, int def) {
  const std::wstring s = GetXmlAttr(line, key);
  if (s.empty()) {
    return def;
  }
  return _wtoi(s.c_str());
}

double ParseDoubleAttr(const std::wstring& line, const wchar_t* key, double def) {
  const std::wstring s = GetXmlAttr(line, key);
  if (s.empty()) {
    return def;
  }
  return _wtof(s.c_str());
}
