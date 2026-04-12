#pragma once

#include <string>

std::wstring XmlEscape(const std::wstring& s);
std::wstring XmlUnescape(const std::wstring& s);
std::wstring GetXmlAttr(const std::wstring& line, const wchar_t* key);
bool ParseBoolAttr(const std::wstring& line, const wchar_t* key, bool def);
int ParseIntAttr(const std::wstring& line, const wchar_t* key, int def);
double ParseDoubleAttr(const std::wstring& line, const wchar_t* key, double def);
