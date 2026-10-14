// MainWindowLayout.h - control helpers, layout and tab switching of the main window
// (forms layer). v0.10.9 (main.cpp split stage 6b): these used to live in main.cpp.
// MakeControl is declared in MainWindowApi.h (the popups use it too).
#pragma once

#include "../PlatformWin.h"

void AddColumn(HWND lv, int idx, LPCWSTR text, int width);
void InitFonts();
void AddButtonGlyphIcon(HWND btn, wchar_t glyph, COLORREF color);
void SetListRowHeight(HWND lv, int heightPx);
void LayoutAll(HWND hwnd);
void ShowTab(int idx);
void FixComboBoxFirstPaint();
