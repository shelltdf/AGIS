#pragma once

#include <windows.h>

#include <string>

#include "core/main_globals.h"

bool ForwardWheelToMapIfOver(WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ToolbarWheelSubclass(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR);

void SyncViewMenu(HWND hwnd);
void SyncWindowMenu(HWND hwnd);
void GetInnerClient(HWND hwnd, RECT* out);
void UpdateStatusParts();
void LayoutChildren();

void SyncMainFrameMapUiMenu(HMENU mapUiPopup);
void LayoutMapShellClient(HWND mapShell);
LRESULT CALLBACK MapShellProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool HitLeftSplitter(int x, int y, int innerTop, int innerBottom);
bool HitRightSplitter(int x, int y, int innerTop, int innerBottom);
HIMAGELIST BuildToolbarImageList();
HWND CreateMainToolbar(HWND parent, HINSTANCE inst);

void CopyTextToClipboard(HWND owner, const std::wstring& text);
LRESULT CALLBACK LayerPaneProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void RefreshPropsPanel(HWND hwndProps);
LRESULT CALLBACK PropsPaneProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LogWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowLogDialog(HWND owner);
void ShowAbout(HWND owner);
/** 语言切换后刷新 Dock / 日志窗口等控件文案（须已创建子窗口）。 */
void ApplyWorkbenchPanelsL10n();

std::wstring CurrentWindowTitle();
void SyncMainTitle();
void RefreshUiAfterDocumentReload();
bool SaveGisXmlTo(const std::wstring& path);
bool LoadGisXmlFrom(const std::wstring& path, std::wstring* err);
std::wstring PromptOpenGisPath(HWND owner);
std::wstring PromptSaveGisPath(HWND owner, const std::wstring& seed);
void GisNew(HWND owner);
void GisOpen(HWND owner);
/// 从路径打开 .gis（或含 `<agis-gis>` 的 .xml）；失败时弹窗。供命令行 /「打开方式」等复用。
void GisOpenFromPath(HWND owner, const std::wstring& path);
void GisSaveAs(HWND owner);
void GisSave(HWND owner);

/// 启动与当前进程同目录下的工具 exe（如 ``AGIS-Convert.exe``）；``params`` 可为空。
bool AgisLaunchSiblingToolExe(HWND owner, const wchar_t* exeName, const wchar_t* params = nullptr);

void WriteConvertLog(HWND hwnd, const wchar_t* line);
void LayoutConvertWindow(HWND hwnd);
void FillConvertTypeCombo(HWND combo);
void FillConvertSubtypeCombo(HWND combo, int majorType);
const wchar_t* ConvertTypeTooltipByMajor(int major);
const wchar_t* ConvertSubtypeTooltipByMajorSubtype(int major, int sub);
const wchar_t* GetConvertTooltipText(HWND dlg, UINT_PTR ctrlId);
void AttachConvertTooltip(HWND dlg, HWND tip, int ctrlId);
void ShowConvertHelpDialog(HWND hwnd, bool inputSide, bool typeHelp);
void SyncConvertInfoByType(HWND hwnd, bool inputSide);
std::wstring GetComboSelectedText(HWND combo);
std::wstring GetModelCoordArg(HWND hwnd);
std::wstring GetVectorModeArg(HWND hwnd);
double GetElevHorizRatioArg(HWND hwnd);
std::wstring GetTargetCrsArg(HWND hwnd);
std::wstring GetOutputUnitArg(HWND hwnd);
int GetMeshSpacingArg(HWND hwnd);
std::wstring GetTextureFormatArg(HWND hwnd);
void FillConvertTextureFormatCombo(HWND combo);
int GetRasterReadMaxDimArg(HWND hwnd);
std::wstring PromptOpenInputPath(HWND owner);
std::wstring PromptSaveOutputPath(HWND owner);
std::wstring PromptSelectOutputFolder(HWND owner);
const wchar_t* ConvertToolExeName(int inMajor, int outMajor);
std::wstring QuoteArg(const std::wstring& s);
void LayoutConvertMidColumn(HWND hwnd, int midColX, int colW, int m, int topH);
std::wstring AssembleConvertProcessCommandLine(HWND hwnd);
std::wstring BuildConvertCommandLine(HWND hwnd);
void UpdateConvertCmdlinePreview(HWND hwnd);
bool RunConvertBackend(HWND hwnd);
void PreviewPath(HWND hwnd, bool inputSide);
void ShowDataConvertWindow(HWND owner);
/// 若数据转换窗口已打开，重新应用语言、组合框与中间列参数标签。
void RefreshConvertWindowL10nIfOpen();
LRESULT CALLBACK ConvertWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void OpenModelPreviewWindow(HWND owner, const std::wstring& path);
void OpenModelPreviewWindow3DTiles(HWND owner, const std::wstring& tilesetRootOrFile);
LRESULT CALLBACK ModelPreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
/// 模型预览：每帧在消息泵空闲时调用（`bgfx::frame` 不在 WM_PAINT 内）。加载中仅 Invalidate 以刷新 GDI 进度条。
void ModelPreviewFrameStep(HWND hwnd);
/// 在主循环中先于通用 PeekMessage 调用：优先派发加载完成消息，避免被海量输入/WM_PAINT 插队导致长时间停在「100%」后无响应。
void ModelPreviewPumpPriorityLoadMessages(HWND hwnd);

void OpenTileRasterPreviewWindow(HWND owner, const std::wstring& path);
LRESULT CALLBACK TilePreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
HMENU BuildMenu();
void AgisReapplyWorkbenchMenu(HWND hwnd);

bool AgisCopyMainWindowScreenshotToClipboard(HWND mainHwnd);
void AgisCopyWorkbenchUiStateJsonToClipboard(HWND mainHwnd);
LRESULT CALLBACK StatusSubclass(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR);
bool RegisterClasses(HINSTANCE inst);
