// AppData.cpp - definitions for the globals declared in AppData.h.
#include "AppData.h"

std::vector<Entry> g_entries;
bool g_autosaveFailWarned = false;
bool g_backupFailWarned = false;
std::wstring g_currentFile;
std::wstring g_finalizedDate;
std::wstring g_lastLoadSourceFile;
bool g_lastLoadHasSourceFile = false;
bool g_dirty = false;
ULONGLONG g_lastBackupTick = 0;
std::string g_lastBackupContent;
bool g_hasUndo = false;
bool g_allowMixedDatesThisSession = false;
Entry g_undoEntry;
int g_undoIndex = -1;
std::vector<std::wstring> g_recentFiles;
std::map<std::wstring, std::wstring> g_supplierEmails;
AppSettings g_settings;
