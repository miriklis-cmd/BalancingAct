// Storage.h - paths and simple file persistence (data layer).
//
// v0.10.3 (main.cpp split stage 3). Two deliberate signature changes so this
// layer never calls up into the UI layer:
//   - LoadSettings(int dpi): was LoadSettings() calling the UI's S(); the
//     first-run default window size is now scaled with MulDiv(px, dpi, 96)
//     (identical result) using the dpi the caller passes (g_dpi).
//   - SaveSettingsFile(...): the file-writing half of the old SaveSettings();
//     main.cpp's SaveSettings() still reads the live window placement and
//     then calls this.
#pragma once

#include "../PlatformWin.h"
#include <string>
#include "AppData.h"

// Folder the exe lives in (or the test override); every data file is built from it.
void SetExeDirOverrideForTests(const std::wstring& dir);
std::wstring GetExeDir();

std::wstring AutosavePath();
std::wstring SettingsPath();
std::wstring RecentFilesPath();
std::wstring EmailsPath();
std::wstring BackupDir();
std::wstring HistoryDir();

void LoadSettings(int dpi);
void SaveSettingsFile(int x, int y, int w, int h, bool maximized, const std::wstring& lastFile);

void SaveRecentFiles();
void LoadRecentFiles();

void LoadSupplierEmails();
bool SaveSupplierEmails();

void PruneOldBackups();
std::wstring CurrentFileLabelForBackup();
