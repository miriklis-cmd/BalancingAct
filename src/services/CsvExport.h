// CsvExport.h - CSV export of every report (services layer).
// v0.10.4 (main.cpp split stage 4a). The file dialog and message boxes that
// call this stay in main.cpp's DoExportCsv() (forms layer).
#pragma once

#include "../PlatformWin.h"
#include <string>

// Writes every report for the current sheet (g_entries) to `path` as an
// Excel-readable CSV. Returns false and fills *outError on failure.
bool ExportToCsv(const std::wstring& path, std::wstring* outError = nullptr);
