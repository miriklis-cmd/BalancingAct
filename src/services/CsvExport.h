// CsvExport.h - CSV export of every report (services layer).
// v0.10.4 (main.cpp split stage 4a). The file dialog and message boxes that
// call this stay in main.cpp's DoExportCsv() (forms layer).
#pragma once

#include "../PlatformWin.h"
#include <string>
#include <vector>
#include "../core/FishBalanceCore.h" // Entry

// Writes every report for `entries` to `path` as an Excel-readable CSV. Returns false and fills *outError on failure.
// v0.23.2: the sheet is passed in (it used to read the Win32 global g_entries) so the WinUI edition can use it too.
bool ExportToCsv(const std::wstring& path, const std::vector<Entry>& entries, std::wstring* outError = nullptr);
