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
// v0.13.1: folder for the small lists shared by the Win32 and WinUI apps (emails.txt, names.txt):
// C:\BalancingAct\data, created on first use. If it cannot be created it falls back to the exe folder so
// the app still works. With a test override set, that override is used (tests never touch real data).
std::wstring GetSharedDataDir();

std::wstring AutosavePath();
std::wstring SettingsPath();
std::wstring RecentFilesPath();
std::wstring EmailsPath();
std::wstring NamesPath();
std::wstring BackupDir();
std::wstring HistoryDir();

void LoadSettings(int dpi);
void SaveSettingsFile(int x, int y, int w, int h, bool maximized, const std::wstring& lastFile);

void SaveRecentFiles();
void LoadRecentFiles();

void LoadSupplierEmails();
bool SaveSupplierEmails();

// v0.13.0 master name lists (names.txt). Load sets g_nameMaster.fileExisted from whether the file was there.
void LoadNameMaster();
bool SaveNameMaster();
// Adds every distinct Supplier / Species in g_entries that is not already in the lists (dateISO "" = unknown).
// Returns true if anything was added.
bool AddSheetNamesToMaster(const std::wstring& dateISO);

void PruneOldBackups();
std::wstring CurrentFileLabelForBackup();
