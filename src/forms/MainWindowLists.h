// MainWindowLists.h - list views, combo boxes and balance totals of the main window
// (forms layer). v0.10.8 (main.cpp split stage 6a): these used to live in main.cpp.
#pragma once

#include "../PlatformWin.h"

void RefreshCombos();
void ComboAutoComplete(HWND combo, int& prevLen);
void RefreshEntriesList();
void SortEntriesBy(int col);
void RecalcTotals();
void RefreshOverviewList();
void RefreshBySpeciesList();
void RefreshBreakdownList();
