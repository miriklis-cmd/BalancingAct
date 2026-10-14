// MainWindowApi.h - functions that main.cpp's main window provides to the other
// forms (popup windows). v0.10.6 (main.cpp split stage 5). These stay defined
// in main.cpp until the main window itself is split out (stages 6-7); the
// declarations (including RefreshAll's default argument) live ONLY here.
#pragma once

#include "../PlatformWin.h"
#include <string>

void RefreshAll(bool doAutosave = true);
void CancelEdit();
void ClearUndoState();
HWND MakeControl(LPCWSTR cls, LPCWSTR text, DWORD style, int id, HWND parent);
void GetDebtorCashText(std::wstring& debtor, std::wstring& cash);
void PrintBreakdownReport(HWND owner);
