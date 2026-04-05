#pragma once

#include <windows.h>

#include <string>

void AppLogLine(const wchar_t* line);
void AppLogLine(const std::wstring& line);
const std::wstring& AppLogGetText();

/** 日志窗口中的只读编辑框；打开/关闭时由主窗口设置。 */
void AppLogSetEdit(HWND edit);

/** 追加一行后，若日志窗口已打开则刷新编辑框内容。 */
void AppLogFlushToEdit();
