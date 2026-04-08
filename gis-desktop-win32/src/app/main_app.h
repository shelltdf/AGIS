#pragma once

#include <windows.h>

#include <string>

#include "main_globals.h"

bool ForwardWheelToMapIfOver(WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ToolbarWheelSubclass(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR);

void SyncViewMenu(HWND hwnd);
void SyncWindowMenu(HWND hwnd);
void GetInnerClient(HWND hwnd, RECT* out);
void UpdateStatusParts();
void LayoutChildren();
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

std::wstring CurrentWindowTitle();
void SyncMainTitle();
void RefreshUiAfterDocumentReload();
bool SaveGisXmlTo(const std::wstring& path);
bool LoadGisXmlFrom(const std::wstring& path, std::wstring* err);
std::wstring PromptOpenGisPath(HWND owner);
std::wstring PromptSaveGisPath(HWND owner, const std::wstring& seed);
void GisNew(HWND owner);
void GisOpen(HWND owner);
void GisSaveAs(HWND owner);
void GisSave(HWND owner);

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
std::wstring BuildConvertCommandLine(HWND hwnd);
void UpdateConvertCmdlinePreview(HWND hwnd);
bool RunConvertBackend(HWND hwnd);
void PreviewPath(HWND hwnd, bool inputSide);
void ShowDataConvertWindow(HWND owner);
LRESULT CALLBACK ConvertWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void OpenModelPreviewWindow(HWND owner, const std::wstring& path);
LRESULT CALLBACK ModelPreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
HMENU BuildMenu();
LRESULT CALLBACK StatusSubclass(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR);
bool RegisterClasses(HINSTANCE inst);
