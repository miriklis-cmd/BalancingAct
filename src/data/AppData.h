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
#include "../services/PrintJob.h" // PrintDefaults (v0.23.0)
#include "../core/NameMaster.h"

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
const ULONGLONG kBackupIntervalMsDefault = 3ULL * 60ULL * 1000ULL; // 3 minutes (was 10 - tightened 2026-09-24 after a real data-loss incident; cap stayed at 50 by choice, so this trades full-day rolling coverage for ~2.5hr of more frequent snapshots)
const int kMaxBackupsDefault = 50; // rolling cap - oldest snapshots pruned beyond this (v0.22.0: both now come from Settings)

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
extern NameMasterData g_nameMaster; // v0.13.0: master Supplier / Species lists (names.txt)

// Window size/position and last-file association, persisted between runs.
struct AppSettings {
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 1050, h = 720;
    bool maximized = false;
    std::wstring lastFile;
    bool dirtyAtExit = false; // v0.17.3: the app was closed with unsaved changes (they live on in autosave.fbd)
    std::wstring gmailAccount; // v0.21.0: settings.txt GMAILACCOUNT=<address> - pre-selects this Google account at sign-in (optional)
    EmailMode emailMode = EmailMode::Mailto; // v0.20.0: settings.txt EMAILMODE=mailto|eml|gmail (Tools > Settings > Email)
    // v0.22.0 (Tools > Settings). Blank wording = default; see EmailWording in FishBalanceCore.h.
    std::wstring emailSubject, emailIntro, emailSignoff;   // EMAILSUBJECT= / EMAILINTRO= / EMAILSIGNOFF=
    int backupMinutes = 3;                                  // BACKUPMINUTES= (1-60)
    int backupKeep = 50;                                    // BACKUPKEEP= (10-500)
    int outlierSensitivity = 1;                             // OUTLIERSENS= 0 relaxed, 1 normal, 2 strict
    // v0.23.0
    int dateStyle = 0;                                      // DATESTYLE= 0 long, 1 short month, 2 dd/mm/yyyy (emails)
    int textPercent = 100;                                  // TEXTSIZE= 100 / 125 / 150 (list text; applied at start-up)
    std::wstring printer;                                   // PRINTER= printer name; blank = the Windows default printer
    int printOrientation = 0;                               // PRINTORIENT= 0 printer default, 1 portrait, 2 landscape
    int printPaper = 0;                                     // PRINTPAPER= 0 printer default, 1 A4, 2 A3
};
extern AppSettings g_settings;
inline ULONGLONG BackupIntervalMs() { return (ULONGLONG)ClampBackupMinutes(g_settings.backupMinutes) * 60ULL * 1000ULL; }
inline int MaxBackups() { return ClampBackupKeep(g_settings.backupKeep); }
inline EmailWording CurrentEmailWording() {
    EmailWording w = ResolveEmailWording(g_settings.emailSubject, g_settings.emailIntro, g_settings.emailSignoff);
    SYSTEMTIME st; GetLocalTime(&st);
    w.dateText = EmailDateText(g_entries, g_settings.dateStyle, SimpleDate{ st.wYear, st.wMonth, st.wDay }); // v0.23.0: sheet date, not today
    return w;
}
inline PrintDefaults CurrentPrintDefaults() {
    PrintDefaults d;
    d.printer = g_settings.printer;
    d.orientation = ClampPrintOrientation(g_settings.printOrientation);
    d.paper = ClampPrintPaper(g_settings.printPaper);
    return d;
}
inline void ApplyOutlierSensitivitySetting() { g_outlierFence = OutlierFenceForSensitivity(ClampOutlierSensitivity(g_settings.outlierSensitivity)); }
