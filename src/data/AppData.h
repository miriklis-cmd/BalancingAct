// AppData.h - the application's document/persistence state (data layer).
//
// v0.10.2 (main.cpp split stage 2): these globals used to be `static` in
// main.cpp. They are declared `extern` here and defined once in AppData.cpp
// so code moved out of main.cpp in later stages can reach them. Nothing
// about their meaning or initial values changed.
//
// Layering: this header may depend on core/ only - no window handles here
// (those live in forms/UiState.h).
#pragma once

#include "../PlatformWin.h"
#include <string>
#include <vector>
#include <map>
#include "../core/FishBalanceCore.h"

extern std::vector<Entry> g_entries;
extern bool g_autosaveFailWarned; // avoid repeating the same warning on every single autosave attempt while a problem persists (e.g. disk full) - reset once a save succeeds again
extern bool g_backupFailWarned; // v0.9.31: same warn-once/reset-on-recovery pattern as g_autosaveFailWarned, but for rolling backup snapshots (see WriteBackupSnapshot)
extern std::wstring g_currentFile; // empty = unsaved / using autosave only

// Finalize Day (ROADMAP.md item 3). Empty = an ordinary, editable working
// file. Non-empty = the ISO date this file was locked in as, written into
// history\<date>.fbd - the entry form, Debtor/Cash, and everything that
// would change the numbers is disabled (see ApplyFinalizedLockState) until
// Un-finalize clears this again. Set from FbdLoadResult::finalizedDate on
// load, or directly by DoFinalizeDay() on success.
extern std::wstring g_finalizedDate;

// Phase 1 / F1 audit remediation: set by LoadFromFile() from the just-loaded
// document's SOURCE_FILE= marker (FbdLoadResult::sourceFile/hasSourceFile).
// wWinMain's startup sequence is the only reader - after silently reloading
// autosave.fbd, it compares g_lastLoadSourceFile against g_settings.lastFile
// before deciding whether it's safe to associate the recovered content with
// that named file (see the startup comment for the full reasoning).
extern std::wstring g_lastLoadSourceFile;
extern bool g_lastLoadHasSourceFile;

// v0.9.30: tracks whether anything has actually changed since the last
// explicit Save/Save As or the last successful load (New/Open/Recent
// Files/Restore from Backup/startup). ConfirmDiscardCurrentData() uses
// this instead of "is there any data at all" - Jack: "should be kinda
// dirty flagged - if something changes, dialog comes up. Once saved,
// then no dialog until something changes again." Deliberately does NOT
// get cleared by autosave.fbd writes or rolling backup snapshots - those
// are safety nets, not the deliberate "I'm done with this" save the
// prompt exists to protect.
extern bool g_dirty;

// Rolling timestamped backups (ROADMAP.md item 4). Autosave fires on every
// focus-loss during normal data entry (see ARCHITECTURE.md's data flow
// notes) - far too often to snapshot every time without flooding the
// backups folder - so snapshots are throttled to at most one every
// kBackupIntervalMs, plus one unconditional snapshot on every explicit
// named-file Save/Save As (a deliberate user action, not just a keystroke).
extern ULONGLONG g_lastBackupTick;
extern std::string g_lastBackupContent; // content of the most recent backup snapshot - see MaybeBackupOnTimer's no-change skip (v0.9.26)
const ULONGLONG kBackupIntervalMs = 3ULL * 60ULL * 1000ULL; // 3 minutes (was 10 - tightened 2026-09-24 after a real data-loss incident; cap stayed at 50 by choice, so this trades full-day rolling coverage for ~2.5hr of more frequent snapshots)
const int kMaxBackups = 50; // rolling cap - oldest snapshots pruned beyond this

// Single-level undo for the last deleted row.
extern bool g_hasUndo;
// v0.9.63: set when the user answers Yes to the "this entry's date differs
// from the rest of the sheet" prompt; suppresses further prompts until the
// sheet changes (File > New, opening/loading a file, Finalize Day's clear).
extern bool g_allowMixedDatesThisSession;
extern Entry g_undoEntry;
extern int g_undoIndex;

// Recent Files (most-recent-first, capped at 8).
extern std::vector<std::wstring> g_recentFiles;
static const size_t kMaxRecentFiles = 8;

// Supplier -> email address, persisted to emails.txt, used by Email Suppliers.
extern std::map<std::wstring, std::wstring> g_supplierEmails;

// Window size/position and last-file association, persisted between runs.
struct AppSettings {
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 1050, h = 720;
    bool maximized = false;
    std::wstring lastFile;
};
extern AppSettings g_settings;
