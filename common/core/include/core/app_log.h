#pragma once

#include <windows.h>

#include <string>

#include "core/export.h"

AGIS_COMMON_API void AppLogLine(const wchar_t* line);
AGIS_COMMON_API void AppLogLine(const std::wstring& line);
AGIS_COMMON_API const std::wstring& AppLogGetText();

/** 日志窗口中的只读编辑框；打开/关闭时由主窗口设置。 */
AGIS_COMMON_API void AppLogSetEdit(HWND edit);

/** 追加一行后，若日志窗口已打开则刷新编辑框内容。 */
AGIS_COMMON_API void AppLogFlushToEdit();
