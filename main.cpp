// Fish Balance Manager
// A native Windows (Win32 API) replacement for the BALANCE_PivotTable.xlsx workbook.
//
// Tab 1 "Data Entry"     - enter Supplier / Species / Kgs / Price rows, and enter the
//                           Debtor / Cash figures from the books; the app checks that
//                           Debtor + Cash equals the sum of entered rows. Also has the
//                           Finalize Day button (ROADMAP.md item 3), which locks the
//                           sheet once it balances and writes a permanent snapshot to
//                           history\<date>.fbd.
// Tab 2 "Total Overview" - a $ total for every supplier, plus the grand total and the
//                           same book-balance check.
// Tab 3 "Breakdown"      - entries grouped by Supplier -> Species -> Price, with a
//                           weight (Kgs) and $ total for each, and subtotals.
// Tab 4 "By Species"     - Kgs/$ totals grouped by species across every supplier, plus
//                           the average, highest, and lowest price seen for each.
//
// Data is kept in a simple pipe-delimited UTF-8 text file (*.fbd) and is auto-saved
// next to the .exe after every change, so nothing is lost between sessions. Use
// File > Save As to keep named files (e.g. per month).
//
// Build instructions are in README.md (CMake, MinGW batch, or MSVC batch).

#define WINVER 0x0601
#define _WIN32_WINNT 0x0601
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
// <windows.h> defines min/max as macros unless this is set first, which
// silently mangles any std::min/std::max call (or any other code with an
// identifier literally named min/max) into a broken expression before the
// compiler ever sees it as a function call - the standard, permanent fix
// for an entire class of confusing "illegal token" errors, not just a
// patch for wherever it happens to bite first.
#define NOMINMAX

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <winspool.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <cmath>
#include <new>       // std::bad_alloc - Phase 1 completeness follow-up (2026-09-30, v0.9.50)
#include <stdexcept> // std::length_error - same

#include "resource.h"
#include "version.h"
#include "FishBalanceCore.h"
#include "FishBalanceWin32IO.h"
#include "FishBalanceControlLimits.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winspool.lib")

// ---------------------------------------------------------------------------
// Data model
//
// Entry, PriceLine, ProductGroup, SupplierGroup, and SimpleDate are defined
// in FishBalanceCore.h (platform-independent, unit-tested separately) -
// nothing to define here.
// ---------------------------------------------------------------------------

static std::vector<Entry> g_entries;
static bool g_autosaveFailWarned = false; // avoid repeating the same warning on every single autosave attempt while a problem persists (e.g. disk full) - reset once a save succeeds again
static bool g_backupFailWarned = false; // v0.9.31: same warn-once/reset-on-recovery pattern as g_autosaveFailWarned, but for rolling backup snapshots (see WriteBackupSnapshot)
static std::wstring g_currentFile; // empty = unsaved / using autosave only

// Finalize Day (ROADMAP.md item 3). Empty = an ordinary, editable working
// file. Non-empty = the ISO date this file was locked in as, written into
// history\<date>.fbd - the entry form, Debtor/Cash, and everything that
// would change the numbers is disabled (see ApplyFinalizedLockState) until
// Un-finalize clears this again. Set from FbdLoadResult::finalizedDate on
// load, or directly by DoFinalizeDay() on success.
static std::wstring g_finalizedDate;

// Phase 1 / F1 audit remediation: set by LoadFromFile() from the just-loaded
// document's SOURCE_FILE= marker (FbdLoadResult::sourceFile/hasSourceFile).
// wWinMain's startup sequence is the only reader - after silently reloading
// autosave.fbd, it compares g_lastLoadSourceFile against g_settings.lastFile
// before deciding whether it's safe to associate the recovered content with
// that named file (see the startup comment for the full reasoning).
static std::wstring g_lastLoadSourceFile;
static bool g_lastLoadHasSourceFile = false;

// v0.9.30: tracks whether anything has actually changed since the last
// explicit Save/Save As or the last successful load (New/Open/Recent
// Files/Restore from Backup/startup). ConfirmDiscardCurrentData() uses
// this instead of "is there any data at all" - Jack: "should be kinda
// dirty flagged - if something changes, dialog comes up. Once saved,
// then no dialog until something changes again." Deliberately does NOT
// get cleared by autosave.fbd writes or rolling backup snapshots - those
// are safety nets, not the deliberate "I'm done with this" save the
// prompt exists to protect.
static bool g_dirty = false;

// Rolling timestamped backups (ROADMAP.md item 4). Autosave fires on every
// focus-loss during normal data entry (see ARCHITECTURE.md's data flow
// notes) - far too often to snapshot every time without flooding the
// backups folder - so snapshots are throttled to at most one every
// kBackupIntervalMs, plus one unconditional snapshot on every explicit
// named-file Save/Save As (a deliberate user action, not just a keystroke).
static ULONGLONG g_lastBackupTick = 0;
static std::string g_lastBackupContent; // content of the most recent backup snapshot - see MaybeBackupOnTimer's no-change skip (v0.9.26)
const ULONGLONG kBackupIntervalMs = 3ULL * 60ULL * 1000ULL; // 3 minutes (was 10 - tightened 2026-09-24 after a real data-loss incident; cap stayed at 50 by choice, so this trades full-day rolling coverage for ~2.5hr of more frequent snapshots)
const int kMaxBackups = 50; // rolling cap - oldest snapshots pruned beyond this

// ---------------------------------------------------------------------------
// Control IDs
// ---------------------------------------------------------------------------

enum {
    ID_FILE_NEW = 1, ID_FILE_OPEN, ID_FILE_SAVE, ID_FILE_SAVEAS, ID_FILE_PRINT, ID_FILE_PRINT_PREVIEW,
    ID_FILE_EXPORT_CSV, ID_FILE_EMAIL_SUPPLIERS, ID_FILE_EXIT, ID_FILE_ABOUT, ID_EDIT_UNDO_DELETE,
    ID_TOOLS_MANAGE_NAMES, ID_FILE_RESTORE_BACKUP, ID_ENTRY_CLEAR_FLAG,
    ID_TAB = 100,
    ID_CMB_SUPPLIER = 200, ID_CMB_PRODUCT, ID_EDIT_KGS, ID_EDIT_PRICE, ID_BTN_ADD, ID_BTN_DELETE,
    ID_BTN_EDIT, ID_BTN_CANCEL_EDIT, ID_EDIT_FILTER, ID_DTP_DATE, ID_EDIT_NOTES, ID_BTN_DUPLICATE,
    ID_BTN_DUPLICATE_SUPSPEC,
    ID_LIST_ENTRIES, ID_EDIT_DEBTOR, ID_EDIT_CASH,
    ID_LIST_OVERVIEW, ID_LIST_BREAKDOWN, ID_BTN_PRINT_BREAKDOWN, ID_BTN_EMAIL_SUPPLIERS,
    ID_BTN_PRINT_PREVIEW, ID_LIST_BYSPECIES,
    ID_BTN_FINALIZE, // Finalize/Un-finalize Day (ROADMAP.md item 3) - one
                      // button, relabeled, same pattern as Add/Update Entry
    // Manage Names popup window controls
    ID_MNG_RADIO_SUPPLIER = 400, ID_MNG_RADIO_SPECIES, ID_MNG_LIST, ID_MNG_TARGET, ID_MNG_APPLY, ID_MNG_CLOSE,
    ID_MNG_EMAIL_EDIT, ID_MNG_EMAIL_SAVE,
    // Print Preview popup window controls
    ID_PREVIEW_PREV = 500, ID_PREVIEW_NEXT, ID_PREVIEW_PRINT, ID_PREVIEW_CLOSE,
    // Finalize Day date-prompt popup window controls
    ID_FIN_DTP = 600, ID_FIN_OK, ID_FIN_CANCEL,
    // Up to 8 Recent Files slots
    ID_RECENT_BASE = 900,
    // One-shot delayed retries for the combo box first-paint fix (see
    // WM_TIMER). v0.9.3/v0.9.11/v0.9.12 already tried a synchronous fix,
    // reordering WM_CREATE, and one 50ms delayed retry - still not fully
    // reliable per Jack's own v0.9.41 report ("doesn't always happen, but
    // see screenshot"), so v0.9.42 adds two further, later retries on the
    // theory that whatever theme-engine/relayout race causes this doesn't
    // always resolve within 50ms on every machine.
    ID_TIMER_FIRST_PAINT_FIX = 951,
    ID_TIMER_FIRST_PAINT_FIX2 = 953,
    ID_TIMER_FIRST_PAINT_FIX3 = 954,
    // Recurring backup-check tick (v0.9.29 fix) - see WM_TIMER. Independent
    // of AutosaveNow()/focus-loss, so an idle screen with unsaved data
    // still gets backed up on schedule, not just when the user happens to
    // tab between fields.
    ID_TIMER_BACKUP_CHECK = 952
};

// ---------------------------------------------------------------------------
// Window / control handles
// ---------------------------------------------------------------------------

static HWND g_hMainWnd = nullptr;

// The app's manifest declares DPI awareness, which means Windows will NOT
// auto-stretch our UI on higher-DPI displays (125%/150%/etc, common on
// modern monitors) - so every hardcoded pixel size in the layout code needs
// to be scaled by this factor ourselves, or text renders larger than the
// (unscaled) control it sits in and gets clipped top/bottom or side to side.
static int g_dpi = 96;
int S(int px) { return MulDiv(px, g_dpi, 96); }
static HWND hTab = nullptr;
static HWND hStatusBar = nullptr;
static HFONT g_normalFont = nullptr, g_boldFont = nullptr;

// Tab 1
static HWND hLblSupplier, hCmbSupplier, hLblProduct, hCmbProduct;
static HWND hLblKgs, hEditKgs, hLblPrice, hEditPrice, hBtnAdd, hBtnDelete, hBtnEdit, hBtnCancelEdit;
static HWND hLblFilter, hEditFilter;
static HWND hLblDate, hDtpDate, hLblNotes, hEditNotes, hBtnDuplicate, hBtnDuplicateSupSpec;
static HWND hBtnFinalize = nullptr; // "Finalize Day" / "Un-finalize Day" - relabeled in place, same pattern as Add/Update Entry
static HWND hListEntries;
static HWND hGrpRecon, hLblDebtor, hEditDebtor, hLblCash, hEditCash;
static HWND hLblBook, hLblEntered, hLblDiff;

// Tab 2
static HWND hLblOvBook, hLblOvGrand, hLblOvDiff, hListOverview;

// Tab 3
static HWND hListBreakdown, hBtnPrintBreakdown, hBtnEmailSuppliers, hBtnPrintPreview;

// Tab 4 - By Species
static HWND hListBySpecies;

static std::vector<HWND> g_tab1Ctrls, g_tab2Ctrls, g_tab3Ctrls, g_tab4Ctrls;
static bool g_diffOk = true;
static bool g_ovDiffOk = true;
// F5 (Phase 1): tracks whether Debtor/Cash currently contain a validly
// parseable sum expression at all (as opposed to a validly parseable one
// that merely doesn't balance yet - g_diffOk). Finalize Day and the report
// builder must both refuse to proceed when this is false, since there is
// no trustworthy total to check or print.
static bool g_reconciliationValid = true;

// Autocomplete state for the Supplier / Species combo boxes.
static bool g_acBusy = false;
static int g_prevSupplierLen = 0;
static int g_prevProductLen = 0;

// -1 means the form is in "add new entry" mode; otherwise it's the index in
// g_entries currently loaded into the form for editing.
static int g_editIndex = -1;

// Maps the currently-displayed row in hListEntries back to its index in
// g_entries (needed because the filter box can hide non-matching rows).
static std::vector<int> g_filteredIndices;

// Sortable-column state for hListEntries.
static int g_sortColumn = -1;
static bool g_sortAscending = true;

// Single-level undo for the last deleted row.
static bool g_hasUndo = false;
static Entry g_undoEntry;
static int g_undoIndex = -1;
static HMENU g_hEditMenu = nullptr;

// Recent Files (most-recent-first, capped at 8).
static std::vector<std::wstring> g_recentFiles;
static HMENU g_hRecentMenu = nullptr;
static const size_t kMaxRecentFiles = 8;

// Manage Supplier/Species Names popup window.
static HWND g_hManageWnd = nullptr;
static HWND g_hManageList = nullptr, g_hManageTarget = nullptr;
static HWND g_hManageRadioSupplier = nullptr, g_hManageRadioSpecies = nullptr;
static HWND g_hManageHint = nullptr, g_hManageTargetLbl = nullptr, g_hManageApplyBtn = nullptr, g_hManageCloseBtn = nullptr;
static HWND g_hManageEmailLbl = nullptr, g_hManageEmailEdit = nullptr, g_hManageEmailBtn = nullptr;
static bool g_manageIsSupplier = true;
static std::vector<std::wstring> g_manageValues;

// Supplier -> email address, persisted to emails.txt, used by Email Suppliers.
static std::map<std::wstring, std::wstring> g_supplierEmails;

// A rendered report page: a device-independent bitmap (DIB), so the exact
// same rendering can be shown on screen (Print Preview) and sent to a real
// printer (StretchDIBits) without redrawing or risking the two diverging.
struct RenderedPage {
    HBITMAP bitmap = nullptr;
    void* bits = nullptr; // owned by the DIB section behind `bitmap`, not separately freed
    int width = 0, height = 0;
};

// Print Preview popup window.
static std::vector<RenderedPage> g_previewPages;
static int g_previewPageIndex = 0;
static HWND g_hPreviewWnd = nullptr;
static HWND g_hPreviewPrevBtn = nullptr, g_hPreviewNextBtn = nullptr;
static HWND g_hPreviewPageLbl = nullptr, g_hPreviewPrintBtn = nullptr, g_hPreviewCloseBtn = nullptr;

// Finalize Day date-prompt popup window (modeled on the Manage Names popup).
static HWND g_hFinalizeWnd = nullptr, g_hFinalizeDtp = nullptr, g_hFinalizeLbl = nullptr;

// Window size/position and last-file association, persisted between runs.
struct AppSettings {
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 1050, h = 720;
    bool maximized = false;
    std::wstring lastFile;
};
static AppSettings g_settings;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

void RefreshAll(bool doAutosave = true);
void LayoutAll(HWND hwnd);
void ShowTab(int idx);
void RecalcTotals();
void CancelEdit();
void ClearUndoState();
void RefreshEntriesList();
void RefreshOverviewList();
void RefreshBySpeciesList();
void MaybeBackupOnTimer(); // ROADMAP.md item 4 - defined below AutosaveNow, which calls it
void RefreshBreakdownList();
void RememberRecentFile(const std::wstring& path);
void RebuildRecentMenu();
void PopulateManageList();
// LooksLikeEmail is declared and defined in FishBalanceCore.h.
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ManageWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK PreviewWndProc(HWND, UINT, WPARAM, LPARAM);
void PrintBreakdownReport(HWND owner);
// Finalize Day (ROADMAP.md item 3)
std::wstring HistoryDir();
void ApplyFinalizedLockState();
void DoFinalizeDay();
void DoUnfinalizeDay();
FinalizeOutcome ExecuteFinalizeDay(const std::wstring& date); // v0.9.51 test-automation refactor
LRESULT CALLBACK FinalizeWndProc(HWND, UINT, WPARAM, LPARAM);
void OpenFinalizeDatePrompt();

// ---------------------------------------------------------------------------
// Small utilities
// ---------------------------------------------------------------------------

// TrimW, ToFixed, FormatMoney, FormatNum, FormatKg, ParseDoubleW, and
// ParseSumExprStrict (formerly the lenient ParseSumExpr, replaced 2026-09-30
// per Phase 1 / F5 audit remediation - see FishBalanceCore.h) all live in
// FishBalanceCore.h so they can be unit-tested without a Win32 dependency.

// FormatDateISO/ParseISODate keep their original SYSTEMTIME-based
// signature here (nothing else in main.cpp needs to change) but delegate
// to the portable SimpleDate versions in FishBalanceCore.h for the actual
// logic. Dates are stored as ISO "YYYY-MM-DD" throughout (sorts correctly
// as plain text, unambiguous regardless of locale), converted to/from a
// SYSTEMTIME only at this one boundary and at the point of talking to the
// DateTimePicker control. Note the comctl32 macros are
// DateTime_GetSystemtime / DateTime_SetSystemtime - lowercase "time",
// easy to mistype as GetSystemTime.
std::wstring FormatDateISO(const SYSTEMTIME& st) {
    return FormatDateISO(SimpleDate{ st.wYear, st.wMonth, st.wDay });
}

bool ParseISODate(const std::wstring& s, SYSTEMTIME& out) {
    SimpleDate d;
    if (!ParseISODate(s, d)) return false;
    SYSTEMTIME st{};
    st.wYear = (WORD)d.year;
    st.wMonth = (WORD)d.month;
    st.wDay = (WORD)d.day;
    out = st;
    return true;
}

// Test-automation refactor (2026-09-30, v0.9.51): WToUtf8/Utf8ToW/OpenFileW
// moved to FishBalanceWin32IO.h unchanged (see that header's comment) so the
// Windows integration test suite can use the exact same conversion/open
// logic without linking this file. Nothing at any call site here changes -
// FishBalanceWin32IO.h is included above via the #include block near the
// top of this file, alongside FishBalanceCore.h.

// Test-automation refactor (v0.9.51): overridable so integration tests can
// point every path this app builds (autosave.fbd, settings.txt, recent.txt,
// emails.txt, backups\, history\) at an isolated temporary directory instead
// of the real folder next to the exe - never touching production data. Empty
// (the default) means "use the real exe directory," exactly the original
// behavior; production code never calls SetExeDirOverrideForTests, so this
// is a pure addition, not a behavior change, for the shipped application.
static std::wstring g_exeDirOverrideForTests;
inline void SetExeDirOverrideForTests(const std::wstring& dir) { g_exeDirOverrideForTests = dir; }

std::wstring GetExeDir() {
    if (!g_exeDirOverrideForTests.empty()) return g_exeDirOverrideForTests;
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    size_t pos = p.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"." : p.substr(0, pos);
}

// Phase 1 / F1 follow-up (2026-09-30, post-review): the single canonical
// path for autosave.fbd, so BuildFbdSaveContent()/SaveToFile() can tell
// whether a given save IS the autosave write without duplicating this
// string construction at every call site.
std::wstring AutosavePath() {
    return GetExeDir() + L"\\autosave.fbd";
}

// WriteFileAtomicUtf8 moved to FishBalanceWin32IO.h in v0.9.51 (see that
// header for the full comment) - same signature, same default behavior,
// now with an optional injectable FileWriteOps for the integration test
// suite's fault-injection tests (Phase 1 item 4). No call site here changes.

// Keeps the currently open/saved file name visible in the title bar, so it's
// always obvious what's loaded and whether it's been saved to a named file.
void UpdateTitle() {
    std::wstring name;
    if (!g_currentFile.empty()) {
        size_t pos = g_currentFile.find_last_of(L"\\/");
        name = (pos == std::wstring::npos) ? g_currentFile : g_currentFile.substr(pos + 1);
    } else {
        name = L"(unsaved)";
    }

    std::wstring title = L"Fish Balance Manager - " + name;
    if (g_hMainWnd) SetWindowTextW(g_hMainWnd, title.c_str());

    // Status bar mirrors the same filename shown in the title bar (kept in
    // sync automatically since both come from this one function), plus the
    // running app version - handy for confirming which build is actually
    // running without going through Help > About.
    if (hStatusBar) {
        std::wstring verText = std::wstring(L"Version ") + APP_VERSION;
        SendMessageW(hStatusBar, SB_SETTEXT, 0, (LPARAM)verText.c_str());
        std::wstring fileText = g_finalizedDate.empty()
            ? (L"File: " + name)
            : (L"FINALIZED (" + g_finalizedDate + L") - File: " + name);
        SendMessageW(hStatusBar, SB_SETTEXT, 1, (LPARAM)fileText.c_str());
    }
}

// Loads the app icon at the requested size. Tries the embedded resource
// first (the normal path); if that's missing for some reason (e.g. the
// resource script wasn't compiled in), falls back to app.ico sitting next
// to the exe, so the icon still shows up either way.
HICON LoadAppIcon(HINSTANCE hInstance, int size) {
    HICON h = (HICON)LoadImageW(hInstance, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON,
                                 size, size, LR_DEFAULTCOLOR);
    if (!h) {
        std::wstring path = GetExeDir() + L"\\app.ico";
        h = (HICON)LoadImageW(nullptr, path.c_str(), IMAGE_ICON, size, size,
                               LR_LOADFROMFILE | LR_DEFAULTCOLOR);
    }
    return h;
}

// ReadBytesError/DescribeReadBytesError/ReadAllBytes/ReadAllLines all moved
// to FishBalanceWin32IO.h in v0.9.51, same signatures, same default
// behavior, ReadAllBytes gaining an optional injectable FileReadOps for the
// integration test suite. No call site here changes.

// ---------------------------------------------------------------------------
// Window size/position + last-file association (settings.txt)
// ---------------------------------------------------------------------------

std::wstring SettingsPath() { return GetExeDir() + L"\\settings.txt"; }

void LoadSettings() {
    std::vector<std::wstring> lines;
    bool foundFile = ReadAllLines(SettingsPath(), lines);
    for (auto& line : lines) {
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = line.substr(0, eq), val = line.substr(eq + 1);
        if (key == L"X") g_settings.x = _wtoi(val.c_str());
        else if (key == L"Y") g_settings.y = _wtoi(val.c_str());
        else if (key == L"W") g_settings.w = _wtoi(val.c_str());
        else if (key == L"H") g_settings.h = _wtoi(val.c_str());
        else if (key == L"MAX") g_settings.maximized = (val == L"1");
        else if (key == L"LASTFILE") g_settings.lastFile = val;
    }
    if (g_settings.w < 640) g_settings.w = 1050;
    if (g_settings.h < 480) g_settings.h = 720;
    // First run (no settings.txt yet): scale the default size for the
    // current display's DPI, since WM_CREATE (where layout is DPI-scaled)
    // doesn't run until after this size has already been used to create
    // the window.
    if (!foundFile) {
        g_settings.w = S(g_settings.w);
        g_settings.h = S(g_settings.h);
    }
}

void SaveSettings() {
    if (!g_hMainWnd) return;
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    GetWindowPlacement(g_hMainWnd, &wp);
    RECT rc = wp.rcNormalPosition;

    std::string content;
    auto writeLine = [&](const std::wstring& l) {
        content += WToUtf8(l) + "\n";
    };
    writeLine(L"X=" + std::to_wstring(rc.left));
    writeLine(L"Y=" + std::to_wstring(rc.top));
    writeLine(L"W=" + std::to_wstring(rc.right - rc.left));
    writeLine(L"H=" + std::to_wstring(rc.bottom - rc.top));
    writeLine(std::wstring(L"MAX=") + (wp.showCmd == SW_SHOWMAXIMIZED ? L"1" : L"0"));
    writeLine(L"LASTFILE=" + g_currentFile);

    // Failure here stays silent (as it always has been) - losing
    // settings.txt only means window position/last-file aren't remembered
    // next launch, not any loss of actual business data. Still a genuine
    // atomic+checked write now, not a truncate-and-hope.
    WriteFileAtomicUtf8(SettingsPath(), content);
}

// ---------------------------------------------------------------------------
// Recent Files (recent.txt)
// ---------------------------------------------------------------------------

std::wstring RecentFilesPath() { return GetExeDir() + L"\\recent.txt"; }

void SaveRecentFiles() {
    std::string content;
    for (auto& p : g_recentFiles) content += WToUtf8(p) + "\n";
    // Failure stays silent, same reasoning as SaveSettings() - losing this
    // just means the Recent Files menu doesn't remember entries, not any
    // loss of actual business data.
    WriteFileAtomicUtf8(RecentFilesPath(), content);
}

void LoadRecentFiles() {
    std::vector<std::wstring> lines;
    ReadAllLines(RecentFilesPath(), lines);
    g_recentFiles.clear();
    for (auto& l : lines) if (!l.empty()) g_recentFiles.push_back(l);
}

void RebuildRecentMenu() {
    if (!g_hRecentMenu) return;
    int n = GetMenuItemCount(g_hRecentMenu);
    for (int i = n - 1; i >= 0; i--) RemoveMenu(g_hRecentMenu, i, MF_BYPOSITION);
    if (g_recentFiles.empty()) {
        AppendMenuW(g_hRecentMenu, MF_STRING | MF_GRAYED, 0, L"(none yet)");
    } else {
        for (size_t i = 0; i < g_recentFiles.size(); i++) {
            size_t pos = g_recentFiles[i].find_last_of(L"\\/");
            std::wstring name = (pos == std::wstring::npos) ? g_recentFiles[i] : g_recentFiles[i].substr(pos + 1);
            AppendMenuW(g_hRecentMenu, MF_STRING, ID_RECENT_BASE + (UINT)i, name.c_str());
        }
    }
    if (g_hMainWnd) DrawMenuBar(g_hMainWnd);
}

void RememberRecentFile(const std::wstring& path) {
    g_recentFiles.erase(std::remove(g_recentFiles.begin(), g_recentFiles.end(), path), g_recentFiles.end());
    g_recentFiles.insert(g_recentFiles.begin(), path);
    if (g_recentFiles.size() > kMaxRecentFiles) g_recentFiles.resize(kMaxRecentFiles);
    SaveRecentFiles();
    RebuildRecentMenu();
}

// ---------------------------------------------------------------------------
// Supplier email addresses (emails.txt) - used by Email Suppliers
// ---------------------------------------------------------------------------

std::wstring EmailsPath() { return GetExeDir() + L"\\emails.txt"; }

void LoadSupplierEmails() {
    std::vector<std::wstring> lines;
    ReadAllLines(EmailsPath(), lines);
    g_supplierEmails.clear();
    for (auto& l : lines) {
        size_t pos = l.find(L'|');
        if (pos == std::wstring::npos) continue;
        std::wstring sup = l.substr(0, pos);
        std::wstring email = l.substr(pos + 1);
        // Phase 1 completeness follow-up (2026-09-30, v0.9.50): companion-
        // file content sharing the Supplier-name field gets the same bound
        // applied to that field everywhere else (kMaxSupplierSpeciesLength).
        // emails.txt has never had transactional/whole-file-reject semantics
        // like .fbd loading - a line this app itself could never have
        // written (both fields are UI-limited at entry, see EM_LIMITTEXT
        // above) is simply skipped, same as the existing empty-field/no-pipe
        // cases just above, rather than failing the entire load.
        if (sup.size() > kMaxSupplierSpeciesLength || email.size() > kMaxSupplierSpeciesLength) continue;
        if (!sup.empty() && !email.empty()) g_supplierEmails[sup] = email;
    }
}

bool SaveSupplierEmails() {
    std::string content;
    for (auto& kv : g_supplierEmails) content += WToUtf8(kv.first + L"|" + kv.second) + "\n";
    return WriteFileAtomicUtf8(EmailsPath(), content);
}

// ---------------------------------------------------------------------------
// File persistence (simple pipe-delimited UTF-8 text format, *.fbd)
// ---------------------------------------------------------------------------

// Builds the full .fbd content string (Debtor/Cash, in-progress draft
// entry-form fields, then BEGIN/entries/END) from the current on-screen
// and in-memory state. Factored out of SaveToFile so the rolling-backup
// snapshot writer (WriteBackupSnapshot, see ROADMAP.md item 4) can build
// the exact same content without duplicating this logic - identical
// output to what SaveToFile has always written, just named and reused.
// finalizedDateOverride: used only by DoFinalizeDay() when writing the new
// history\<date>.fbd snapshot at the moment of finalizing, so that file is
// born already carrying FINALIZED= without a separate write-then-rewrite
// step. Every other caller passes nothing and gets g_finalizedDate (empty
// for an ordinary working file, or the locked-in date once loaded from one).
std::string BuildFbdSaveContent(const std::wstring& finalizedDateOverride = L"", bool includeSourceFile = false) {
    std::string content;
    auto writeLine = [&](const std::wstring& line) {
        content += WToUtf8(line) + "\n";
    };
    // Phase 1 completeness follow-up (2026-09-30, v0.9.50): sized to the
    // largest of the field limits this shared buffer is reused for below
    // (kMaxNotesLength/kMaxDebtorCashExprLength, both 4096) - the previous
    // 512-wchar buffer would have silently truncated a legitimately long
    // (but now UI-permitted) Debtor/Cash expression or Notes value right
    // before writing it to disk.
    wchar_t buf[kMaxNotesLength + 1];
    GetWindowTextW(hEditDebtor, buf, (int)(kMaxNotesLength + 1));
    writeLine(std::wstring(L"DEBTOR=") + buf);
    GetWindowTextW(hEditCash, buf, (int)(kMaxNotesLength + 1));
    writeLine(std::wstring(L"CASH=") + buf);

    // Phase 1 / F1 audit remediation, narrowed post-review (2026-09-30): the
    // SOURCE_FILE= identity marker is only ever READ back by wWinMain's
    // startup logic, and only from autosave.fbd specifically - so it's only
    // written here (includeSourceFile=true, passed by SaveToFile only for
    // the autosave path). Writing g_currentFile's absolute path into every
    // .fbd save (named files, history\ snapshots, backups\) was the original
    // Phase 1 implementation, but on reflection that embeds the original
    // machine's Windows username/folder path into files that routinely
    // leave the machine - emailed, backed up, or opened on another computer
    // - for no benefit, since nothing ever reads this marker out of a named/
    // history/backup file. Scoping it to autosave.fbd alone keeps the exact
    // same safety property (see FbdLoadResult::sourceFile/hasSourceFile in
    // FishBalanceCore.h) without that exposure.
    if (includeSourceFile) writeLine(L"SOURCE_FILE=" + g_currentFile);

    const std::wstring& finalizedDate = finalizedDateOverride.empty() ? g_finalizedDate : finalizedDateOverride;
    if (!finalizedDate.empty()) writeLine(L"FINALIZED=" + finalizedDate);

    // In-progress "Add Entry" form state, so it survives a crash or power
    // loss and not just a graceful close - whatever's currently sitting in
    // these fields (even if nothing/blank) is written on every save, the
    // same way Debtor/Cash already are.
    GetWindowTextW(hCmbSupplier, buf, (int)(kMaxNotesLength + 1));
    writeLine(std::wstring(L"DRAFT_SUPPLIER=") + buf);
    GetWindowTextW(hCmbProduct, buf, (int)(kMaxNotesLength + 1));
    writeLine(std::wstring(L"DRAFT_SPECIES=") + buf);
    GetWindowTextW(hEditKgs, buf, (int)(kMaxNotesLength + 1));
    writeLine(std::wstring(L"DRAFT_KGS=") + buf);
    GetWindowTextW(hEditPrice, buf, (int)(kMaxNotesLength + 1));
    writeLine(std::wstring(L"DRAFT_PRICE=") + buf);
    GetWindowTextW(hEditNotes, buf, (int)(kMaxNotesLength + 1));
    writeLine(std::wstring(L"DRAFT_NOTES=") + buf);
    if (hDtpDate) {
        SYSTEMTIME st{};
        DateTime_GetSystemtime(hDtpDate, &st);
        writeLine(L"DRAFT_DATE=" + FormatDateISO(st));
    }

    writeLine(L"BEGIN");
    for (auto& e : g_entries) {
        std::wstring line = e.supplier + L"|" + e.product + L"|" +
                             ToFixed(e.kgs, 4) + L"|" + ToFixed(e.price, 4) + L"|" +
                             e.date + L"|" + e.notes + L"|" + (e.priceFlagged ? L"1" : L"0");
        writeLine(line);
    }
    writeLine(L"END");
    return content;
}

bool SaveToFile(const std::wstring& path, std::wstring* outError = nullptr) {
    // Only the autosave write includes SOURCE_FILE= - see BuildFbdSaveContent's
    // comment. A case-insensitive compare matches how the startup identity
    // check itself compares paths (_wcsicmp), for the same reason: Windows
    // paths are case-insensitive.
    bool isAutosave = (_wcsicmp(path.c_str(), AutosavePath().c_str()) == 0);
    return WriteFileAtomicUtf8(path, BuildFbdSaveContent(L"", isAutosave), outError);
}

// Loads and STRICTLY validates a .fbd file. This function is now just the
// Win32 file I/O boundary (open, read bytes, decode UTF-8) - all parsing
// and validation logic (the part that matters, and the part every serious
// bug in this app has lived in) is ParseFbdContent() in FishBalanceCore.h,
// which takes the already-decoded content and is unit-tested directly
// there without needing a real file or a Win32 window.
//
// g_entries and the on-screen Debtor/Cash fields are only touched if the
// whole document passes validation. This matters because every caller of
// this function (DoFileOpen, the Recent Files menu, and the startup
// autosave reload) goes on to call RefreshAll(), which immediately
// re-writes autosave.fbd - so a loader that's too permissive here doesn't
// just show the user a wrong screen, it silently destroys the recovery
// copy too. See SecurityHardeningRegister.md and ARCHITECTURE.md for why
// this function gets this much scrutiny.
// Phase 1 completeness follow-up (2026-09-30, v0.9.50): outError, when
// non-null, is filled with a specific, structured reason on failure -
// combining ReadAllBytes' read-side diagnostic, a UTF-8-validity failure, or
// ParseFbdContent's own structured FbdErrorCode/line-number/message (see
// FishBalanceCore.h). Every one of this function's four callers (File >
// Open, Recent Files, Restore from Backup, startup autosave recovery) now
// shows this in its message box instead of relying solely on one fixed
// generic sentence - the sentence stays too, as a plain-language summary,
// with the specific reason appended.
bool LoadFromFile(const std::wstring& path, std::wstring* outError = nullptr) {
    // Test-automation refactor (v0.9.51): the read+decode+parse steps below
    // used to be inline here; they now live in FishBalanceWin32IO.h's
    // LoadFbdFileContent() (identical logic, moved verbatim) so the Windows
    // integration test suite can exercise "load a real file from disk" with
    // injectable FileReadOps, without linking any of this file's GUI code.
    // This wrapper is unchanged: it still owns the one thing that must stay
    // here - committing a successful parse into the app's global state.
    LoadFbdFileResult loaded = LoadFbdFileContent(path);
    if (!loaded.ok) {
        if (outError) *outError = loaded.errorMessage;
        return false;
    }
    const FbdLoadResult& result = loaded.parsed;

    // Phase 1 / F1: expose the loaded document's identity marker to
    // wWinMain's startup logic, which needs it to decide whether a
    // recovered autosave.fbd genuinely corresponds to settings.txt's
    // remembered last-opened file (see g_lastLoadSourceFile's declaration).
    // Every other caller of LoadFromFile (Open, Recent Files, Un/Finalize's
    // own re-save) already manages g_currentFile explicitly and simply
    // ignores these.
    g_lastLoadSourceFile = result.sourceFile;
    g_lastLoadHasSourceFile = result.hasSourceFile;

    g_entries = result.entries;
    SetWindowTextW(hEditDebtor, result.debtor.c_str());
    SetWindowTextW(hEditCash, result.cash.c_str());
    g_finalizedDate = result.finalizedDate;

    // Restore any in-progress "Add Entry" draft that was pending when this
    // file was last saved (see SaveToFile) - survives a crash or power
    // loss, not just a graceful close. Always restored into "add new"
    // mode, not an attempt to resume editing a specific existing row
    // (which would be a much more fragile thing to restore correctly) -
    // just whatever text was sitting in the form.
    SetWindowTextW(hCmbSupplier, result.draftSupplier.c_str());
    SetWindowTextW(hCmbProduct, result.draftSpecies.c_str());
    SetWindowTextW(hEditKgs, result.draftKgs.c_str());
    SetWindowTextW(hEditPrice, result.draftPrice.c_str());
    SetWindowTextW(hEditNotes, result.draftNotes.c_str());
    if (hDtpDate && !result.draftDate.empty()) {
        SYSTEMTIME st;
        if (ParseISODate(result.draftDate, st)) DateTime_SetSystemtime(hDtpDate, GDT_VALID, &st);
    }
    g_editIndex = -1;
    g_prevSupplierLen = (int)result.draftSupplier.size();
    g_prevProductLen = (int)result.draftSpecies.size();

    // Phase 1 / F3+F2 (2026-09-30): ParseFbdContent() is now transactional -
    // a document containing any row it can't safely reconstruct is rejected
    // outright (result.ok is false, and LoadFromFile has already returned
    // above) rather than being partially committed. result.skippedLines is
    // therefore always 0 whenever result.ok is true, so this warning is
    // unreachable in current builds; left in place only in case a future,
    // more permissive load mode reintroduces partial-document skipping.
    if (result.skippedLines > 0 && g_hMainWnd) {
        std::wstring msg = L"Warning: " + std::to_wstring(result.skippedLines) +
            (result.skippedLines == 1 ? L" row could" : L" rows could") +
            L" not be read from this file and " + (result.skippedLines == 1 ? L"was" : L"were") +
            L" skipped (an invalid or missing Supplier/Species/Kgs/Price value, or an old file "
            L"saved with a '|' character in a Supplier or Species name).";
        MessageBoxW(g_hMainWnd, msg.c_str(), L"Some Rows Skipped", MB_OK | MB_ICONWARNING);
    }
    ApplyFinalizedLockState();
    return true;
}

// ---------------------------------------------------------------------------
// Control creation helpers
// ---------------------------------------------------------------------------

HWND MakeControl(LPCWSTR cls, LPCWSTR text, DWORD style, int id, HWND parent) {
    HWND h = CreateWindowExW(0, cls, text, style | WS_CHILD, 0, 0, 0, 0,
                              parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    if (h) SendMessageW(h, WM_SETFONT, (WPARAM)g_normalFont, TRUE);
    return h;
}

void AddColumn(HWND lv, int idx, LPCWSTR text, int width) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = const_cast<LPWSTR>(text);
    col.cx = width;
    col.iSubItem = idx;
    ListView_InsertColumn(lv, idx, &col);
}

void InitFonts() {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_normalFont = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW lfBold = ncm.lfMessageFont;
    lfBold.lfWeight = FW_BOLD;
    g_boldFont = CreateFontIndirectW(&lfBold);
}

// ---------------------------------------------------------------------------
// Refresh logic
// ---------------------------------------------------------------------------

void RefreshCombos() {
    wchar_t buf[256];
    GetWindowTextW(hCmbSupplier, buf, 256);
    std::wstring curSup = buf;
    GetWindowTextW(hCmbProduct, buf, 256);
    std::wstring curProd = buf;

    SendMessageW(hCmbSupplier, CB_RESETCONTENT, 0, 0);
    SendMessageW(hCmbProduct, CB_RESETCONTENT, 0, 0);

    std::vector<std::wstring> sups, prods;
    for (auto& e : g_entries) {
        if (std::find(sups.begin(), sups.end(), e.supplier) == sups.end()) sups.push_back(e.supplier);
        if (std::find(prods.begin(), prods.end(), e.product) == prods.end()) prods.push_back(e.product);
    }
    std::sort(sups.begin(), sups.end());
    std::sort(prods.begin(), prods.end());
    for (auto& s : sups) SendMessageW(hCmbSupplier, CB_ADDSTRING, 0, (LPARAM)s.c_str());
    for (auto& p : prods) SendMessageW(hCmbProduct, CB_ADDSTRING, 0, (LPARAM)p.c_str());

    SetWindowTextW(hCmbSupplier, curSup.c_str());
    SetWindowTextW(hCmbProduct, curProd.c_str());
}

// Simple autocomplete for the Supplier / Species combo boxes: as the user
// types, suggest the first existing item that starts with the typed text and
// highlight the suggested tail (so the next keystroke overwrites it, or
// Enter/Tab accepts it). Only triggers when the text has grown - deleting
// characters (Backspace/Delete) never gets fought.
void ComboAutoComplete(HWND combo, int& prevLen) {
    if (g_acBusy) return;

    wchar_t buf[256];
    GetWindowTextW(combo, buf, 256);
    std::wstring text(buf);
    int curLen = (int)text.size();
    bool grew = curLen > prevLen;
    prevLen = curLen;

    if (!grew || text.empty()) return;

    // Only autocomplete when the caret is at the end of the text - avoids
    // odd results when editing in the middle of an existing entry.
    DWORD selStart = 0, selEnd = 0;
    SendMessageW(combo, CB_GETEDITSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    if ((int)selEnd != curLen) return;

    LRESULT idx = SendMessageW(combo, CB_FINDSTRING, (WPARAM)-1, (LPARAM)text.c_str());
    if (idx == CB_ERR) return;

    // CB_GETLBTEXT has no length-limiting parameter - it writes exactly as
    // much as the item's text needs, so the buffer must be sized to fit via
    // CB_GETLBTEXTLEN first rather than assumed to fit some fixed size.
    LRESULT fullLen = SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)idx, 0);
    if (fullLen == CB_ERR || fullLen <= 0) return;
    std::vector<wchar_t> fullBuf((size_t)fullLen + 1);
    if (SendMessageW(combo, CB_GETLBTEXT, (WPARAM)idx, (LPARAM)fullBuf.data()) == CB_ERR) return;
    std::wstring fullText(fullBuf.data());
    if (fullText.size() <= text.size()) return;

    g_acBusy = true;
    SetWindowTextW(combo, fullText.c_str());
    SendMessageW(combo, CB_SETEDITSEL, 0, MAKELPARAM((int)text.size(), (int)fullText.size()));
    g_acBusy = false;
    // Note: prevLen is deliberately left as the length of what the user
    // actually typed (set earlier), not the length of the suggested text -
    // otherwise the next keystroke looks like a deletion and autocomplete
    // stops re-triggering (e.g. typing "J" then "e" to disambiguate between
    // "Jcasement" and "Jenkins").
}

void RefreshEntriesList() {
    ListView_DeleteAllItems(hListEntries);
    g_filteredIndices.clear();

    wchar_t fbuf[256];
    GetWindowTextW(hEditFilter, fbuf, 256);
    std::wstring filter = TrimW(fbuf);
    std::transform(filter.begin(), filter.end(), filter.begin(), ::towlower);

    int row = 0;
    for (size_t i = 0; i < g_entries.size(); i++) {
        auto& e = g_entries[i];
        if (!filter.empty()) {
            std::wstring sup = e.supplier, prod = e.product;
            std::transform(sup.begin(), sup.end(), sup.begin(), ::towlower);
            std::transform(prod.begin(), prod.end(), prod.begin(), ::towlower);
            if (sup.find(filter) == std::wstring::npos && prod.find(filter) == std::wstring::npos)
                continue;
        }
        g_filteredIndices.push_back((int)i);

        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(e.supplier.c_str());
        ListView_InsertItem(hListEntries, &item);
        ListView_SetItemText(hListEntries, row, 1, const_cast<LPWSTR>(e.product.c_str()));
        std::wstring kgsS = FormatKg(e.kgs);
        ListView_SetItemText(hListEntries, row, 2, const_cast<LPWSTR>(kgsS.c_str()));
        std::wstring priceS = FormatMoney(e.price);
        if (e.priceFlagged) priceS = L"\u26A0 " + priceS; // ROADMAP.md item 7 - see the NM_CUSTOMDRAW row tint below too
        ListView_SetItemText(hListEntries, row, 3, const_cast<LPWSTR>(priceS.c_str()));
        std::wstring totalS = FormatMoney(e.Total());
        ListView_SetItemText(hListEntries, row, 4, const_cast<LPWSTR>(totalS.c_str()));
        ListView_SetItemText(hListEntries, row, 5, const_cast<LPWSTR>(e.date.c_str()));
        ListView_SetItemText(hListEntries, row, 6, const_cast<LPWSTR>(e.notes.c_str()));
        row++;
    }
}

// Sorts the underlying data by the clicked column (toggling direction on a
// repeat click of the same column), then redraws. Any in-progress edit is
// abandoned since row indices are about to change.
void SortEntriesBy(int col) {
    if (g_sortColumn == col) g_sortAscending = !g_sortAscending;
    else { g_sortColumn = col; g_sortAscending = true; }

    std::sort(g_entries.begin(), g_entries.end(), [col](const Entry& a, const Entry& b) {
        switch (col) {
            case 0: return _wcsicmp(a.supplier.c_str(), b.supplier.c_str()) < 0;
            case 1: return _wcsicmp(a.product.c_str(), b.product.c_str()) < 0;
            case 2: return a.kgs < b.kgs;
            case 3: return a.price < b.price;
            case 4: return a.Total() < b.Total();
            case 5: return a.date < b.date; // ISO format sorts correctly as plain text
            case 6: return _wcsicmp(a.notes.c_str(), b.notes.c_str()) < 0;
            default: return false;
        }
    });
    if (!g_sortAscending) std::reverse(g_entries.begin(), g_entries.end());

    if (g_editIndex != -1) CancelEdit();
    RefreshEntriesList();
}

void RecalcTotals() {
    double entered = 0;
    for (auto& e : g_entries) entered += e.Total();

    // Phase 1 completeness follow-up (2026-09-30, v0.9.50): sized to
    // kMaxDebtorCashExprLength+1, matching EM_LIMITTEXT on these controls -
    // the old 512-wchar buffer would have silently truncated a legitimately
    // long (but now UI-permitted, up to 4096 char) expression right before
    // parsing it.
    wchar_t buf[kMaxDebtorCashExprLength + 1];
    GetWindowTextW(hEditDebtor, buf, (int)(kMaxDebtorCashExprLength + 1));
    SumParseResult debtorResult = ParseSumExprStrict(buf);
    GetWindowTextW(hEditCash, buf, (int)(kMaxDebtorCashExprLength + 1));
    SumParseResult cashResult = ParseSumExprStrict(buf);

    g_reconciliationValid = debtorResult.ok && cashResult.ok;

    if (!g_reconciliationValid) {
        // Fail closed (Phase 1 / F5): an invalid Debtor/Cash entry must not
        // be silently treated as balanced (or unbalanced) against anything -
        // there is no trustworthy total to compare, so say so plainly rather
        // than showing a number computed from a partially-ignored expression.
        g_diffOk = false;
        g_ovDiffOk = false;
        const std::wstring& badField = !debtorResult.ok ? L"Debtor" : L"Cash";
        const std::wstring& badTerm = !debtorResult.ok ? debtorResult.errorTerm : cashResult.errorTerm;
        std::wstring msg = L"Difference: cannot check - " + badField +
                            L" has an invalid entry (\"" + badTerm + L"\")";

        SetWindowTextW(hLblBook, L"Book Total (Debtor + Cash): --");
        SetWindowTextW(hLblEntered, (L"Entered Total (this sheet): " + FormatMoney(entered)).c_str());
        SetWindowTextW(hLblDiff, msg.c_str());

        SetWindowTextW(hLblOvBook, L"Book Total (Debtor + Cash): --");
        SetWindowTextW(hLblOvGrand, (L"Grand Total (all suppliers): " + FormatMoney(entered)).c_str());
        SetWindowTextW(hLblOvDiff, msg.c_str());

        InvalidateRect(hLblDiff, nullptr, TRUE);
        InvalidateRect(hLblOvDiff, nullptr, TRUE);
        return;
    }

    double debtor = debtorResult.value;
    double cash = cashResult.value;
    double book = debtor + cash;
    double diff = book - entered;
    g_diffOk = std::abs(diff) < 0.005;
    g_ovDiffOk = g_diffOk;

    SetWindowTextW(hLblBook, (L"Book Total (Debtor + Cash): " + FormatMoney(book)).c_str());
    SetWindowTextW(hLblEntered, (L"Entered Total (this sheet): " + FormatMoney(entered)).c_str());
    SetWindowTextW(hLblDiff, (L"Difference: " + FormatMoney(diff) +
                               (g_diffOk ? L"   (OK - balanced)" : L"   (OUT OF BALANCE)")).c_str());

    SetWindowTextW(hLblOvBook, (L"Book Total (Debtor + Cash): " + FormatMoney(book)).c_str());
    SetWindowTextW(hLblOvGrand, (L"Grand Total (all suppliers): " + FormatMoney(entered)).c_str());
    SetWindowTextW(hLblOvDiff, (L"Difference: " + FormatMoney(diff) +
                                 (g_ovDiffOk ? L"   (OK - balanced)" : L"   (OUT OF BALANCE)")).c_str());

    InvalidateRect(hLblDiff, nullptr, TRUE);
    InvalidateRect(hLblOvDiff, nullptr, TRUE);
}

// Shared by the Total Overview tab (grouped by supplier) and the By Species
// tab (grouped by species) - same shape of report, just a different key.
// The actual aggregation is ComputeGroupedTotals() in FishBalanceCore.h
// (unit-tested there); this function is now just the ListView rendering.
void PopulateGroupedTotalsList(HWND lv, bool bySupplier) {
    ListView_DeleteAllItems(lv);
    GroupedTotalsResult totals = ComputeGroupedTotals(g_entries, bySupplier);

    int idx = 0;
    for (auto& row : totals.rows) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = idx;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(row.key.c_str());
        ListView_InsertItem(lv, &item);
        std::wstring kgsS = FormatKg(row.kgs);
        ListView_SetItemText(lv, idx, 1, const_cast<LPWSTR>(kgsS.c_str()));
        std::wstring amtS = FormatMoney(row.amt);
        ListView_SetItemText(lv, idx, 2, const_cast<LPWSTR>(amtS.c_str()));
        idx++;
    }
    LVITEMW gitem{};
    gitem.mask = LVIF_TEXT;
    gitem.iItem = idx;
    gitem.iSubItem = 0;
    std::wstring lbl = L"GRAND TOTAL";
    gitem.pszText = const_cast<LPWSTR>(lbl.c_str());
    ListView_InsertItem(lv, &gitem);
    std::wstring gk = FormatKg(totals.grandKgs);
    ListView_SetItemText(lv, idx, 1, const_cast<LPWSTR>(gk.c_str()));
    std::wstring ga = FormatMoney(totals.grandAmt);
    ListView_SetItemText(lv, idx, 2, const_cast<LPWSTR>(ga.c_str()));
}

void RefreshOverviewList() { PopulateGroupedTotalsList(hListOverview, true); }

// By Species gets its own richer view (unlike Overview, which stays a plain
// Kgs/Total breakdown per supplier): average, highest, and lowest price seen
// for each species, alongside the usual Kgs/Total. The actual aggregation
// is ComputeSpeciesStats() in FishBalanceCore.h (unit-tested there); this
// function is now just the ListView rendering.
void RefreshBySpeciesList() {
    ListView_DeleteAllItems(hListBySpecies);
    SpeciesStatsResult stats = ComputeSpeciesStats(g_entries);

    int idx = 0;
    for (auto& row : stats.rows) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = idx;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(row.species.c_str());
        ListView_InsertItem(hListBySpecies, &item);
        std::wstring kgsS = FormatKg(row.kgs);
        ListView_SetItemText(hListBySpecies, idx, 1, const_cast<LPWSTR>(kgsS.c_str()));
        std::wstring amtS = FormatMoney(row.amt);
        ListView_SetItemText(hListBySpecies, idx, 2, const_cast<LPWSTR>(amtS.c_str()));
        std::wstring avgS = FormatMoney(row.avgPrice);
        ListView_SetItemText(hListBySpecies, idx, 3, const_cast<LPWSTR>(avgS.c_str()));
        std::wstring maxS = FormatMoney(row.maxPrice);
        ListView_SetItemText(hListBySpecies, idx, 4, const_cast<LPWSTR>(maxS.c_str()));
        std::wstring minS = FormatMoney(row.minPrice);
        ListView_SetItemText(hListBySpecies, idx, 5, const_cast<LPWSTR>(minS.c_str()));
        idx++;
    }

    // Price stats don't have a meaningful single "grand" figure across
    // different species (their prices aren't comparable), so the total row
    // only carries Kgs and Total, same as before.
    LVITEMW gitem{};
    gitem.mask = LVIF_TEXT;
    gitem.iItem = idx;
    gitem.iSubItem = 0;
    std::wstring lbl = L"GRAND TOTAL";
    gitem.pszText = const_cast<LPWSTR>(lbl.c_str());
    ListView_InsertItem(hListBySpecies, &gitem);
    std::wstring gk = FormatKg(stats.grandKgs);
    ListView_SetItemText(hListBySpecies, idx, 1, const_cast<LPWSTR>(gk.c_str()));
    std::wstring ga = FormatMoney(stats.grandAmt);
    ListView_SetItemText(hListBySpecies, idx, 2, const_cast<LPWSTR>(ga.c_str()));
}

// PriceLine, ProductGroup, SupplierGroup, and BuildBreakdownData() now all
// live in FishBalanceCore.h (unchanged logic; BuildBreakdownData() there
// takes the entries vector as an explicit parameter instead of reading
// the global g_entries directly, so it can be unit-tested against known
// fixtures - call sites below now pass g_entries in explicitly).

void InsertBreakdownRow(int groupId, int itemIdx, const std::wstring& species,
                         const std::wstring& priceStr, double kgs, double amt) {
    LVITEMW item{};
    item.mask = LVIF_TEXT | LVIF_GROUPID;
    item.iItem = itemIdx;
    item.iSubItem = 0;
    item.iGroupId = groupId;
    item.pszText = const_cast<LPWSTR>(species.c_str());
    ListView_InsertItem(hListBreakdown, &item);
    ListView_SetItemText(hListBreakdown, itemIdx, 1, const_cast<LPWSTR>(priceStr.c_str()));
    std::wstring kgsS = FormatKg(kgs);
    ListView_SetItemText(hListBreakdown, itemIdx, 2, const_cast<LPWSTR>(kgsS.c_str()));
    std::wstring amtS = FormatMoney(amt);
    ListView_SetItemText(hListBreakdown, itemIdx, 3, const_cast<LPWSTR>(amtS.c_str()));
}

void RefreshBreakdownList() {
    ListView_DeleteAllItems(hListBreakdown);
    SendMessageW(hListBreakdown, LVM_REMOVEALLGROUPS, 0, 0);

    auto data = BuildBreakdownData(g_entries);

    int groupId = 0;
    int itemIdx = 0;
    for (auto& sg : data) {
        LVGROUP grp{};
        grp.cbSize = sizeof(LVGROUP);
        grp.mask = LVGF_HEADER | LVGF_GROUPID | LVGF_STATE;
        grp.pszHeader = const_cast<LPWSTR>(sg.supplier.c_str());
        grp.iGroupId = groupId;
        grp.state = LVGS_NORMAL;
        SendMessageW(hListBreakdown, LVM_INSERTGROUP, (WPARAM)-1, (LPARAM)&grp);

        for (auto& pg : sg.products) {
            for (auto& pl : pg.prices)
                InsertBreakdownRow(groupId, itemIdx++, pg.species, FormatMoney(pl.price), pl.kgs, pl.amt);
            InsertBreakdownRow(groupId, itemIdx++, pg.species + L" Total", L"", pg.totalKgs, pg.totalAmt);
        }
        InsertBreakdownRow(groupId, itemIdx++, sg.supplier + L" Total", L"", sg.totalKgs, sg.totalAmt);
        groupId++;
    }
}

// Attempts the autosave write and warns (once, not repeated while the same
// problem persists) if it fails - shared by RefreshAll() and by the
// Debtor/Cash immediate-autosave path below, so both get the same
// warn-once/reset-on-recovery behavior from one place instead of two
// copies that could drift apart.
void AutosaveNow() {
    bool ok = SaveToFile(AutosavePath());
    if (ok) {
        g_autosaveFailWarned = false; // problem (if any) has cleared - a future failure should warn again
        MaybeBackupOnTimer();
    } else if (!g_autosaveFailWarned && g_hMainWnd) {
        g_autosaveFailWarned = true;
        MessageBoxW(g_hMainWnd,
            L"Warning: the automatic backup (autosave.fbd) could not be saved just now - "
            L"the disk may be full, or the file is locked by another program (e.g. antivirus).\n\n"
            L"Your current data is still safe in memory. If you haven't saved to a named file "
            L"recently, use File > Save now to make sure nothing is lost. This warning won't "
            L"repeat again until autosave succeeds at least once.",
            L"Autosave Failed", MB_OK | MB_ICONWARNING);
    }
}

std::wstring BackupDir() { return GetExeDir() + L"\\backups"; }

// Finalize Day (ROADMAP.md item 3) locked-in snapshots, one per business
// day, named <date>.fbd (e.g. 2026-09-20.fbd). A separate folder from
// backups\ - these are deliberate, permanent records, not rolling/pruned
// safety-net copies.
std::wstring HistoryDir() { return GetExeDir() + L"\\history"; }

// Deletes the oldest backup snapshots once there are more than kMaxBackups
// of them. Filenames embed a sortable YYYYMMDD_HHMMSS timestamp (see
// WriteBackupSnapshot), so a plain lexicographic sort is also a
// chronological sort - oldest names sort first.
void PruneOldBackups() {
    std::wstring dir = BackupDir();
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\backup_*.fbd").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            names.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if ((int)names.size() <= kMaxBackups) return;
    std::sort(names.begin(), names.end());
    size_t toDelete = names.size() - (size_t)kMaxBackups;
    for (size_t i = 0; i < toDelete; i++) {
        _wremove((dir + L"\\" + names[i]).c_str());
    }
}

// Extracts a filesystem-safe label for which file was open when a backup
// snapshot was taken - just the base filename without path or extension
// (e.g. "21112" for "...\21112.fbd"), or "unsaved" if no named file is
// open yet (working from autosave.fbd only). Always safe to use directly
// in a filename: it comes from a path that was itself already a valid,
// successfully-opened Windows filename.
std::wstring CurrentFileLabelForBackup() {
    if (g_currentFile.empty()) return L"unsaved";
    size_t slash = g_currentFile.find_last_of(L"\\/");
    std::wstring name = (slash == std::wstring::npos) ? g_currentFile : g_currentFile.substr(slash + 1);
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) name = name.substr(0, dot);
    return name.empty() ? L"unsaved" : name;
}

// Writes a timestamped snapshot of `content` to the backups folder,
// independent of wherever autosave/Save is writing to.
//
// v0.9.31: this used to be "best-effort and silent on failure" - the
// WriteFileAtomicUtf8 return value was discarded outright, so a failed
// write (disk full, backups\ folder unwritable, antivirus lock, etc.) was
// completely invisible: no warning shown, and g_lastBackupContent/
// g_lastBackupTick were still updated as if the snapshot had succeeded,
// which also suppressed the near-term retry that MaybeBackupOnTimer's
// no-change skip would otherwise have allowed. Jack reported "every 3
// minute save not working" after several real Add Entry commits and
// couldn't explain it purely by the missing WM_TIMER (fixed in v0.9.29) -
// this silent failure is the other real candidate: unlike autosave.fbd
// (which does warn - see AutosaveNow), a failing rolling backup gave no
// sign anything was wrong. Now checked and warned once, same pattern as
// g_autosaveFailWarned, reset the next time a snapshot actually succeeds.
void WriteBackupSnapshot(const std::string& content) {
    std::wstring dir = BackupDir();
    CreateDirectoryW(dir.c_str(), nullptr); // no-op (fails harmlessly) if it already exists

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t stamp[32];
    swprintf(stamp, 32, L"%04d%02d%02d_%02d%02d%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    // Source-file label goes AFTER the timestamp, not before - v0.9.27,
    // Jack: "if I'm opening different files I won't know which one it's
    // snapshotting." The timestamp must stay the leading, fixed-width part
    // of the filename for PruneOldBackups' "sort by name = sort by time"
    // logic to keep working; a label before it would sort backups by
    // source file first, breaking chronological pruning entirely.
    std::wstring path = dir + L"\\backup_" + stamp + L"_" + CurrentFileLabelForBackup() + L".fbd";

    std::wstring writeError;
    bool ok = WriteFileAtomicUtf8(path, content, &writeError);
    if (ok) {
        g_backupFailWarned = false; // problem (if any) has cleared - a future failure should warn again
        g_lastBackupContent = content;
        g_lastBackupTick = GetTickCount64();
        PruneOldBackups();
    } else if (!g_backupFailWarned && g_hMainWnd) {
        g_backupFailWarned = true;
        std::wstring msg =
            L"Warning: a rolling backup snapshot could not be saved just now (" + writeError + L").\n\n"
            L"Your current data is still safe in memory, and autosave.fbd is a separate file "
            L"unaffected by this. If you haven't saved to a named file recently, use File > Save "
            L"now to make sure nothing is lost. This warning won't repeat again until a backup "
            L"snapshot succeeds.";
        MessageBoxW(g_hMainWnd, msg.c_str(), L"Backup Snapshot Failed", MB_OK | MB_ICONWARNING);
        // Deliberately NOT updating g_lastBackupContent/g_lastBackupTick on
        // failure - previously they were updated unconditionally, which made
        // MaybeBackupOnTimer think this snapshot had already been taken and
        // wait out the full interval again before trying once more. Leaving
        // them untouched means the very next AutosaveNow (e.g. on the next
        // field's focus loss) retries immediately instead of waiting.
    }
}

// Convenience overload for callers (explicit Save/Save As) that haven't
// already built the content themselves.
void WriteBackupSnapshot() { WriteBackupSnapshot(BuildFbdSaveContent()); }

// Called after every successful autosave - takes a rolling snapshot only
// if kBackupIntervalMs has elapsed since the last one, so continuous
// focus-loss autosaving during normal data entry doesn't flood the
// backups folder with near-identical files. v0.9.26: also skips writing
// entirely if nothing has actually changed since the last snapshot - no
// point spending a file (and disk write) on a duplicate of what's already
// there just because the interval happened to elapse while the sheet sat
// idle or the user was only browsing/sorting without editing anything.
void MaybeBackupOnTimer() {
    ULONGLONG now = GetTickCount64();
    if (g_lastBackupTick != 0 && (now - g_lastBackupTick) < kBackupIntervalMs) return;

    std::string content = BuildFbdSaveContent();
    if (content == g_lastBackupContent) {
        // Nothing changed - reset the timer anyway so we don't re-check
        // (rebuild + compare) on every subsequent no-op autosave before
        // the next full interval is actually up.
        g_lastBackupTick = now;
        return;
    }
    WriteBackupSnapshot(content);
}

void RefreshAll(bool doAutosave) {
    RefreshEntriesList();
    RefreshCombos();
    RecalcTotals();
    RefreshOverviewList();
    RefreshBySpeciesList();
    RefreshBreakdownList();
    if (doAutosave) AutosaveNow();
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void CommitEntryForm() {
    // Belt-and-braces: ApplyFinalizedLockState() already disables the
    // controls that would get here, but the Enter-key path calls this
    // directly, so this guard is the real backstop against editing a
    // locked-in (finalized) day.
    if (!g_finalizedDate.empty()) return;
    // Phase 1 completeness follow-up (2026-09-30, v0.9.50): sized to
    // kMaxNotesLength+1 - Notes can now legitimately hold up to 4096
    // characters (EM_LIMITTEXT on hEditNotes), so the old 256-wchar buffer
    // would have silently truncated it right before it's committed.
    // Supplier/Species/Kgs/Price all fit comfortably within this same
    // larger buffer too.
    wchar_t buf[kMaxNotesLength + 1];
    GetWindowTextW(hCmbSupplier, buf, (int)(kMaxNotesLength + 1));
    std::wstring supplier = TrimW(buf);
    GetWindowTextW(hCmbProduct, buf, (int)(kMaxNotesLength + 1));
    std::wstring product = TrimW(buf);
    GetWindowTextW(hEditKgs, buf, (int)(kMaxNotesLength + 1));
    std::wstring kgsStr = buf;
    GetWindowTextW(hEditPrice, buf, (int)(kMaxNotesLength + 1));
    std::wstring priceStr = buf;
    GetWindowTextW(hEditNotes, buf, (int)(kMaxNotesLength + 1));
    std::wstring notes = TrimW(buf);

    if (supplier.empty() || product.empty()) {
        MessageBoxW(g_hMainWnd, L"Please enter both Supplier and Species.", L"Missing Data", MB_OK | MB_ICONWARNING);
        return;
    }
    if (supplier.find(L'|') != std::wstring::npos || product.find(L'|') != std::wstring::npos) {
        MessageBoxW(g_hMainWnd, L"Supplier and Species can't contain the '|' character - "
                                 L"it's used internally as a separator when saving.", L"Invalid Data", MB_OK | MB_ICONWARNING);
        return;
    }
    if (notes.find(L'|') != std::wstring::npos) {
        MessageBoxW(g_hMainWnd, L"Notes can't contain the '|' character - "
                                 L"it's used internally as a separator when saving.", L"Invalid Data", MB_OK | MB_ICONWARNING);
        return;
    }
    double kgs, price;
    if (!ParseDoubleW(kgsStr, kgs) || kgs < 0) {
        MessageBoxW(g_hMainWnd, L"Please enter a valid Kgs value.", L"Invalid Data", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!ParseDoubleW(priceStr, price) || price < 0) {
        MessageBoxW(g_hMainWnd, L"Please enter a valid Price value.", L"Invalid Data", MB_OK | MB_ICONWARNING);
        return;
    }

    SYSTEMTIME dtpVal{};
    DateTime_GetSystemtime(hDtpDate, &dtpVal);
    std::wstring date = FormatDateISO(dtpVal);

    // Outlier price check (ROADMAP.md item 7) - "is this price a typo?"
    // against this species' OTHER entries on this same date, all
    // suppliers pooled. Excludes the row being edited from its own
    // baseline, so fixing entry 1's typo re-evaluates entry 1 correctly
    // rather than comparing it to itself. Silent as of v0.9.23 - no
    // interrupting dialog here (changed at Jack's request: a modal on
    // every keystroke-driven Add Entry broke his data-entry flow at real
    // volume). A flagged price is committed exactly as entered, just
    // marked in the Entries list (warning glyph + red tint) for review
    // later via double-click or right-click "Clear flag" - never a hard
    // block, nothing here ever refuses to accept a genuinely unusual but
    // correct price. Combined with v0.9.22's whole-group re-evaluation, a
    // false-positive flag will often clear itself automatically once more
    // similar-priced entries come in, without the user doing anything.
    int outlierExcludeIdx = (g_editIndex >= 0 && g_editIndex < (int)g_entries.size()) ? g_editIndex : -1;
    std::vector<double> outlierBaseline = GatherOtherPricesForSpeciesOnDate(g_entries, product, date, outlierExcludeIdx);
    OutlierRange outlierRange;
    bool priceFlagged = ComputeOutlierRange(outlierBaseline, outlierRange) &&
                         (price < outlierRange.low || price > outlierRange.high);

    if (g_editIndex >= 0 && g_editIndex < (int)g_entries.size()) {
        // Updating an existing row.
        Entry& e = g_entries[g_editIndex];
        e.supplier = supplier;
        e.product = product;
        e.kgs = kgs;
        e.price = price;
        e.date = date;
        e.notes = notes;
        e.priceFlagged = priceFlagged;
        g_editIndex = -1;
        SetWindowTextW(hBtnAdd, L"Add Entry");
    } else {
        // Adding a new row.
        Entry e;
        e.supplier = supplier;
        e.product = product;
        e.kgs = kgs;
        e.price = price;
        e.date = date;
        e.notes = notes;
        e.priceFlagged = priceFlagged;
        g_entries.push_back(e);
    }

    // Re-check every entry for this species/date, not just the one just
    // committed - a new/edited price can change whether an EARLIER entry
    // still looks normal too (see ROADMAP.md item 7's discussion of the
    // "first entry poisons the baseline" gap this closes).
    ReevaluateOutlierFlagsForSpeciesOnDate(g_entries, product, date);

    g_dirty = true; // v0.9.30 - an add/update means there's something to lose again

    SetWindowTextW(hEditKgs, L"");
    SetWindowTextW(hEditPrice, L"");
    SetWindowTextW(hEditNotes, L"");
    // Supplier deliberately stays populated (supports fast entry of several
    // rows for the same supplier in a row - see README.md's batch-entry
    // workflow), but Species is cleared so it doesn't silently carry over
    // into what's often actually a different species next. Focus goes back
    // to Supplier (not Species) - Jack's preference after trying v0.9.7's
    // Species-focus behavior.
    SetWindowTextW(hCmbProduct, L"");
    g_prevProductLen = 0;
    SetFocus(hCmbSupplier);

    RefreshAll();
}

// Pre-fills the form from the most recently added entry, staying in "add
// new" mode (doesn't touch g_editIndex) - handy for repeat purchases of the
// same supplier/species where only the weight or price changes.
void DuplicateLastEntry() {
    if (g_entries.empty()) {
        MessageBoxW(g_hMainWnd, L"There are no entries yet to duplicate.", L"Duplicate Last Entry", MB_OK | MB_ICONINFORMATION);
        return;
    }
    Entry& e = g_entries.back();
    SetWindowTextW(hCmbSupplier, e.supplier.c_str());
    SetWindowTextW(hCmbProduct, e.product.c_str());
    SetWindowTextW(hEditKgs, FormatKg(e.kgs).c_str());
    SetWindowTextW(hEditPrice, FormatNum(e.price).c_str());
    SetWindowTextW(hEditNotes, e.notes.c_str());
    SYSTEMTIME st;
    if (hDtpDate) {
        if (ParseISODate(e.date, st)) {
            DateTime_SetSystemtime(hDtpDate, GDT_VALID, &st);
        } else {
            GetLocalTime(&st);
            DateTime_SetSystemtime(hDtpDate, GDT_VALID, &st);
        }
    }
    g_prevSupplierLen = (int)e.supplier.size();
    g_prevProductLen = (int)e.product.size();
    SetFocus(hEditKgs);
    SendMessageW(hEditKgs, EM_SETSEL, 0, -1); // select all so typing immediately replaces it
}

// Pre-fills ONLY Supplier and Species from the most recently added entry -
// unlike Duplicate Last Entry, this deliberately leaves Kgs/Price/Notes/Date
// untouched. Complements Species now clearing after Add Entry: if the next
// row is for the same species again (not a new one), this refills it in one
// click without pulling in the old weight/price too.
void DuplicateSupplierSpecies() {
    if (g_entries.empty()) {
        MessageBoxW(g_hMainWnd, L"There are no entries yet to duplicate from.", L"Duplicate Supplier & Species", MB_OK | MB_ICONINFORMATION);
        return;
    }
    Entry& e = g_entries.back();
    SetWindowTextW(hCmbSupplier, e.supplier.c_str());
    SetWindowTextW(hCmbProduct, e.product.c_str());
    g_prevSupplierLen = (int)e.supplier.size();
    g_prevProductLen = (int)e.product.size();
    SetFocus(hEditKgs);
    SendMessageW(hEditKgs, EM_SETSEL, 0, -1); // select all so typing immediately replaces it
}

// Invalidates the single-level Undo Delete buffer and grays out the menu
// item. Must be called by anything that replaces or restructures
// g_entries wholesale (New, Open, Recent Files, Manage Names rename/merge) -
// otherwise the buffered entry (and the still-enabled menu item) silently
// outlives the sheet it was deleted from. Undoing after one of those
// operations would insert a row from a DIFFERENT, already-discarded sheet
// into the current one - and RefreshAll()'s autosave would then write that
// foreign row to disk before anyone had a chance to notice.
void ClearUndoState() {
    g_hasUndo = false;
    g_undoIndex = -1;
    if (g_hEditMenu) EnableMenuItem(g_hEditMenu, ID_EDIT_UNDO_DELETE, MF_BYCOMMAND | MF_GRAYED);
}

// Test-automation refactor (v0.9.51): the actual delete - no dialogs, no
// selection lookup - extracted out of DeleteSelectedEntry() so the headless
// end-to-end smoke test (Phase 1 item 6) can delete a specific row through a
// callable application command, without needing to drive the confirm
// MessageBoxW ("do not introduce UI automation dependencies if direct
// function/control testing is sufficient" - the blocking dialog stays in
// DeleteSelectedEntry, the caller-facing command). Returns false (and
// changes nothing) for an out-of-range index, same as the old function's
// bounds check.
bool DeleteEntryAt(int sel) {
    if (!g_finalizedDate.empty()) return false; // see CommitEntryForm's identical guard
    if (sel < 0 || sel >= (int)g_entries.size()) return false;

    Entry& e = g_entries[sel];
    g_undoEntry = e;
    g_undoIndex = sel;
    std::wstring undoProduct = e.product, undoDate = e.date; // captured before erase invalidates e
    g_hasUndo = true;
    if (g_hEditMenu) EnableMenuItem(g_hEditMenu, ID_EDIT_UNDO_DELETE, MF_BYCOMMAND | MF_ENABLED);

    g_entries.erase(g_entries.begin() + sel);
    g_dirty = true; // v0.9.30
    // Removing this entry can change whether its siblings still look like
    // outliers (the baseline they're judged against just shrank) - see
    // ReevaluateOutlierFlagsForSpeciesOnDate / ROADMAP.md item 7.
    ReevaluateOutlierFlagsForSpeciesOnDate(g_entries, undoProduct, undoDate);
    // Deleting shifts every later index down by one, and may remove the
    // row currently loaded in the form - simplest and safest is to just
    // drop out of edit mode rather than try to track the shift.
    if (g_editIndex != -1) CancelEdit();
    RefreshAll();
    return true;
}

void DeleteSelectedEntry() {
    if (!g_finalizedDate.empty()) return; // see CommitEntryForm's identical guard
    int selRow = ListView_GetNextItem(hListEntries, -1, LVNI_SELECTED);
    if (selRow < 0) {
        MessageBoxW(g_hMainWnd, L"Select a row to delete first.", L"No Selection", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (selRow < 0 || selRow >= (int)g_filteredIndices.size()) return;
    int sel = g_filteredIndices[selRow];
    if (sel < 0 || sel >= (int)g_entries.size()) return;

    Entry& e = g_entries[sel];
    std::wstring msg = L"Delete this row? You can undo this from the Edit menu.\n\n" +
        (e.date.empty() ? std::wstring() : e.date + L"\n") +
        e.supplier + L" - " + e.product + L"\n" +
        FormatKg(e.kgs) + L" kg @ " + FormatMoney(e.price) + L" = " + FormatMoney(e.Total());
    if (MessageBoxW(g_hMainWnd, msg.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    DeleteEntryAt(sel);
}

void UndoDelete() {
    if (!g_hasUndo) return;
    int idx = g_undoIndex;
    if (idx < 0) idx = 0;
    if (idx > (int)g_entries.size()) idx = (int)g_entries.size();
    g_entries.insert(g_entries.begin() + idx, g_undoEntry);
    g_dirty = true; // v0.9.30
    // Restoring it can equally change whether siblings still look normal -
    // same reasoning as the delete path above, symmetric in reverse.
    ReevaluateOutlierFlagsForSpeciesOnDate(g_entries, g_undoEntry.product, g_undoEntry.date);
    g_hasUndo = false;
    if (g_hEditMenu) EnableMenuItem(g_hEditMenu, ID_EDIT_UNDO_DELETE, MF_BYCOMMAND | MF_GRAYED);
    if (g_editIndex != -1) CancelEdit();
    RefreshAll();
}

// Resets the entry form back to "add new" mode and clears it.
void CancelEdit() {
    g_editIndex = -1;
    SetWindowTextW(hCmbSupplier, L"");
    SetWindowTextW(hCmbProduct, L"");
    SetWindowTextW(hEditKgs, L"");
    SetWindowTextW(hEditPrice, L"");
    SetWindowTextW(hEditNotes, L"");
    SetWindowTextW(hBtnAdd, L"Add Entry");
    g_prevSupplierLen = 0;
    g_prevProductLen = 0;
    if (hDtpDate) {
        SYSTEMTIME today;
        GetLocalTime(&today);
        DateTime_SetSystemtime(hDtpDate, GDT_VALID, &today);
    }
    SetFocus(hCmbSupplier);
    // Explicit, not relying on the SetWindowTextW calls above to reliably
    // fire EN_CHANGE/CBN_EDITCHANGE (they're programmatic, not genuine
    // keystrokes - a known Win32 inconsistency) - without this, clicking
    // Clear could leave a stale, un-cleared draft on disk that would
    // incorrectly reappear on next launch despite being explicitly
    // discarded here.
    AutosaveNow();
}

// Loads an existing entry's values into the form so it can be corrected,
// and switches the Add button into "Update Entry" mode.
void LoadEntryIntoForm(int idx) {
    if (idx < 0 || idx >= (int)g_entries.size()) return;
    g_editIndex = idx;
    Entry& e = g_entries[idx];
    SetWindowTextW(hCmbSupplier, e.supplier.c_str());
    SetWindowTextW(hCmbProduct, e.product.c_str());
    SetWindowTextW(hEditKgs, FormatKg(e.kgs).c_str());
    SetWindowTextW(hEditPrice, FormatNum(e.price).c_str());
    SetWindowTextW(hEditNotes, e.notes.c_str());
    SYSTEMTIME st;
    if (ParseISODate(e.date, st)) {
        DateTime_SetSystemtime(hDtpDate, GDT_VALID, &st);
    } else {
        SYSTEMTIME today;
        GetLocalTime(&today);
        DateTime_SetSystemtime(hDtpDate, GDT_VALID, &today);
    }
    SetWindowTextW(hBtnAdd, L"Update Entry");
    g_prevSupplierLen = (int)e.supplier.size();
    g_prevProductLen = (int)e.product.size();
    SetFocus(hCmbSupplier);
    SendMessageW(hCmbSupplier, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
}

// Entry point for reviewing/editing a row from the Entries list (double-
// click, or Edit Selected) - ROADMAP.md item 7. An unflagged row goes
// straight into the normal edit form, same as always. A flagged row shows
// the same-style Yes/No warning again first, re-evaluated live against
// today's CURRENT other entries (excluding this row) - so if whatever was
// skewing the baseline has since been fixed, this reflects that instead of
// repeating stale numbers. "Yes" proceeds into the normal edit form for a
// closer look/fix; "No" clears the flag without opening the edit form at
// all - the same outcome as the right-click "Clear flag" menu item, just
// reached via a review step instead of a direct dismiss.
void ReviewOrEditEntry(int idx) {
    if (idx < 0 || idx >= (int)g_entries.size()) return;
    Entry& e = g_entries[idx];
    if (e.priceFlagged) {
        std::vector<double> baseline = GatherOtherPricesForSpeciesOnDate(g_entries, e.product, e.date, idx);
        OutlierRange range;
        bool stillOutlier = ComputeOutlierRange(baseline, range) && (e.price < range.low || e.price > range.high);
        wchar_t msg[512];
        if (stillOutlier) {
            bool tooHigh = e.price > range.high;
            double limit = tooHigh ? range.high : range.low; // low is guaranteed >0 here - see CommitEntryForm's identical logic
            swprintf(msg, 512,
                L"This entry is $%.2f/kg for %s on %s.\n"
                L"The dynamic limit calculated for today is %s $%.2f/kg.\n\n"
                L"Still looks like a typo?\n\n"
                L"Click Yes to edit it, No to leave it as-is.",
                e.price, e.product.c_str(), e.date.c_str(), tooHigh ? L"max" : L"min", limit);
        } else {
            swprintf(msg, 512,
                L"This entry's price ($%.2f) was flagged earlier as unusual for %s on %s, but no "
                L"longer looks unusual against the current entries. Click Yes to edit it anyway, "
                L"No to clear the flag and leave it as-is.",
                e.price, e.product.c_str(), e.date.c_str());
        }
        int r = MessageBoxW(g_hMainWnd, msg, L"Flagged Price", MB_YESNO | MB_ICONWARNING);
        if (r == IDNO) {
            e.priceFlagged = false;
            RefreshAll();
            return; // Dismissed - no edit form opened.
        }
        // Yes - fall through into the normal edit form below.
    }
    LoadEntryIntoForm(idx);
}

// Right-click "Clear flag" (ROADMAP.md item 7) - a faster dismiss than
// ReviewOrEditEntry's dialog for when you can already see at a glance the
// price is fine and don't need the re-evaluated review text. Acts on
// whichever row the WM_CONTEXTMENU handler below selected just before
// showing the popup.
void ClearSelectedEntryFlag() {
    int selRow = ListView_GetNextItem(hListEntries, -1, LVNI_SELECTED);
    if (selRow < 0 || selRow >= (int)g_filteredIndices.size()) return;
    int idx = g_filteredIndices[selRow];
    if (idx < 0 || idx >= (int)g_entries.size()) return;
    g_entries[idx].priceFlagged = false;
    RefreshAll();
}

void EditSelectedEntry() {
    if (!g_finalizedDate.empty()) return; // see CommitEntryForm's identical guard
    int selRow = ListView_GetNextItem(hListEntries, -1, LVNI_SELECTED);
    if (selRow < 0) {
        MessageBoxW(g_hMainWnd, L"Select a row to edit first (or double-click it).", L"No Selection", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (selRow < 0 || selRow >= (int)g_filteredIndices.size()) return;
    ReviewOrEditEntry(g_filteredIndices[selRow]);
}

// Confirms before something is about to silently replace all on-screen
// data (File > New, File > Open, a Recent File, Restore from Backup).
// Returns true if it's safe to proceed (nothing meaningful to lose, or the
// user confirmed discarding it), false if the caller should abort and
// leave the current data untouched. `actionPhrase` completes the sentence
// "Discard the current data and <actionPhrase>?".
//
// v0.9.24: File > Open and the Recent Files menu had NO such check at
// all - confirmed as a real bug via Jack's own report: opening a
// different file silently discarded unsaved edits in memory, with no
// warning, and (since RefreshAll() autosaves on every load) also
// overwrote autosave.fbd with the newly-opened file's content, wiping out
// the only on-disk copy of that in-progress work too. File > New already
// had this check (Discard the current data and start a new sheet?) and
// Restore from Backup had its own near-identical inline copy - both now
// route through this one shared function instead of three separate,
// driftable copies of the same logic.
bool ConfirmDiscardCurrentData(const wchar_t* actionPhrase) {
    // v0.9.30: was "is there any data at all" (g_entries non-empty or
    // Debtor/Cash non-blank) - correct but overly blunt, since it kept
    // warning even immediately after an explicit Save, when there was
    // nothing actually at risk of being lost. Now keyed off g_dirty
    // instead, so it only asks when something has genuinely changed
    // since the last save/load.
    if (!g_dirty) return true;
    wchar_t msg[256];
    swprintf(msg, 256, L"Discard the current data and %s?", actionPhrase);
    int r = MessageBoxW(g_hMainWnd, msg, L"Confirm", MB_YESNO | MB_ICONQUESTION);
    return r == IDYES;
}

void DoFileNew() {
    if (!ConfirmDiscardCurrentData(L"start a new sheet")) return;
    // v0.9.32: File > New was the one discard path v0.9.28 missed - it was
    // reasoned as "already clears to a blank sheet, nothing to preserve,"
    // but that's backwards: it's not the new blank sheet that needs
    // protecting, it's whatever unsaved work is about to be thrown away to
    // get there, exactly like Open/Recent Files/Restore from Backup. Jack
    // found this directly: added an entry, did File > New, confirmed the
    // discard prompt, checked backups\ - nothing there.
    WriteBackupSnapshot();
    g_entries.clear();
    SetWindowTextW(hEditDebtor, L""); // triggers EN_CHANGE, which sets g_dirty - explicitly cleared below
    SetWindowTextW(hEditCash, L"");
    g_currentFile.clear();
    g_finalizedDate.clear(); // a fresh blank sheet is never locked - see Finalize Day, ROADMAP.md item 3
    ApplyFinalizedLockState();
    CancelEdit();
    ClearUndoState();
    g_dirty = false; // v0.9.30 - fresh blank sheet, nothing to lose yet
    UpdateTitle();
    RefreshAll();
}

void DoFileOpen() {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hMainWnd;
    ofn.lpstrFilter = L"Fish Balance Files (*.fbd)\0*.fbd\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"fbd";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        if (!ConfirmDiscardCurrentData(L"open the selected file")) return;
        // v0.9.28: take an unconditional backup snapshot of whatever is about
        // to be discarded, before LoadFromFile overwrites g_entries in memory.
        // This closes the exposure window the v0.9.24 data-loss incident
        // found - confirming "discard and open" isn't itself a backup, only
        // an acknowledgement, so the data being discarded still needs its own
        // snapshot the same way an explicit Save already gets one.
        WriteBackupSnapshot();
        std::wstring loadErr;
        if (LoadFromFile(file, &loadErr)) {
            g_currentFile = file;
            CancelEdit();
            ClearUndoState();
            g_dirty = false; // v0.9.30 - freshly loaded, matches disk
            UpdateTitle();
            RefreshAll();
            RememberRecentFile(file);
        } else {
            // Phase 1 completeness follow-up (2026-09-30, v0.9.50): loadErr
            // carries a specific reason (see LoadFromFile) - appended to the
            // existing plain-language summary rather than replacing it.
            MessageBoxW(g_hMainWnd,
                (L"Could not open the selected file: " + loadErr +
                 L".\n\nNothing has been changed.").c_str(),
                L"Error", MB_OK | MB_ICONERROR);
        }
    }
}

// Lets the user browse the rolling backups folder (see WriteBackupSnapshot)
// and load one to recover from a mistake. Deliberately does NOT set
// g_currentFile to the backup's path - a restored backup should be
// reviewed and explicitly Saved/Saved As by the user, not silently treated
// as "the" named file and overwritten by the next autosave/backup cycle.
void DoRestoreFromBackup() {
    std::wstring dir = BackupDir();
    DWORD attrs = GetFileAttributesW(dir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        MessageBoxW(g_hMainWnd,
            L"No backups folder exists yet. Rolling backups are taken automatically as you "
            L"work (roughly every 3 minutes) and every time you use File > Save - there just "
            L"isn't one yet this session.",
            L"No Backups Yet", MB_OK | MB_ICONINFORMATION);
        return;
    }

    wchar_t file[MAX_PATH] = L"";
    // v0.9.33: lpstrInitialDir alone isn't enough - GetOpenFileNameW only
    // honors it the very first time this process ever shows the dialog.
    // After that (e.g. an earlier File > Open into some other folder),
    // Windows reuses whatever folder was last navigated to, regardless of
    // lpstrInitialDir. Jack: "if i click restore from backup - shouldnt it
    // take me to backup folder? it didnt... last folder i used in open was
    // desktop and went there instead." The documented workaround is to
    // pre-fill lpstrFile itself with the target folder (trailing backslash,
    // no filename) - a path in lpstrFile takes priority over the
    // remembered folder, unlike lpstrInitialDir.
    std::wstring initialFile = dir + L"\\";
    wcsncpy_s(file, MAX_PATH, initialFile.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hMainWnd;
    ofn.lpstrFilter = L"Fish Balance Backups (*.fbd)\0*.fbd\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = dir.c_str(); // kept as a fallback for the true first-ever call
    ofn.lpstrDefExt = L"fbd";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    if (!ConfirmDiscardCurrentData(L"load this backup (nothing on disk is touched unless you Save afterwards)")) return;

    // v0.9.28: same reasoning as DoFileOpen - snapshot what's about to be
    // discarded before the restore overwrites it in memory.
    WriteBackupSnapshot();

    std::wstring loadErr;
    if (LoadFromFile(file, &loadErr)) {
        g_currentFile.clear();
        CancelEdit();
        ClearUndoState();
        g_dirty = false; // v0.9.30 - freshly loaded; note a subsequent Save As will re-clear it too
        UpdateTitle();
        RefreshAll();
        MessageBoxW(g_hMainWnd,
            L"Backup loaded. Review the data, then use File > Save As to keep it if this is "
            L"what you wanted.",
            L"Backup Loaded", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(g_hMainWnd,
            (L"Could not load that backup: " + loadErr + L".\n\nNothing has been changed.").c_str(),
            L"Error", MB_OK | MB_ICONERROR);
    }
}

void DoFileSaveAs() {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hMainWnd;
    ofn.lpstrFilter = L"Fish Balance Files (*.fbd)\0*.fbd\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"fbd";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&ofn)) {
        // Test-automation refactor (v0.9.51): the previous-file-restore-on-
        // failure logic below is now CoordinateSaveAs() (FishBalanceCore.h),
        // a pure function tested against injected write failures (Phase 1
        // item 4) - proving a failed Save As restores g_currentFile/title to
        // exactly what they were before, with no destination or temp file
        // incorrectly retained (WriteFileAtomicUtf8's own temp-file cleanup,
        // unchanged). This wrapper still owns the actual file dialog and the
        // Win32 write call itself, same as before.
        std::wstring previousFile = g_currentFile;
        std::wstring newFile = file;
        SaveAsOutcome outcome = CoordinateSaveAs(previousFile, newFile,
            [](const std::wstring& path, std::wstring* outError) {
                return SaveToFile(path, outError);
            });
        g_currentFile = outcome.resultingCurrentFile;
        g_dirty = outcome.dirtyAfter;
        UpdateTitle();
        if (outcome.success) {
            RememberRecentFile(newFile);
            WriteBackupSnapshot(); // deliberate user Save - always worth its own snapshot, not just the timer
            MessageBoxW(g_hMainWnd, L"Saved.", L"Save", MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(g_hMainWnd, (L"Could not save the file: " + outcome.errorMessage).c_str(), L"Error", MB_OK | MB_ICONERROR);
        }
    }
}

void DoFileSave() {
    if (g_currentFile.empty()) { DoFileSaveAs(); return; }
    std::wstring err;
    if (!SaveToFile(g_currentFile, &err))
        MessageBoxW(g_hMainWnd, (L"Could not save the file: " + err).c_str(), L"Error", MB_OK | MB_ICONERROR);
    else {
        g_dirty = false; // v0.9.30 - explicit save, matches disk again
        WriteBackupSnapshot(); // deliberate user Save - always worth its own snapshot, not just the timer
    }
}

// ---------------------------------------------------------------------------
// Finalize Day (ROADMAP.md item 3)
//
// Locked-in decisions (all confirmed with Jack directly):
//   - Finalize is blocked outright (no override) unless Debtor+Cash exactly
//     balances against the entered total (g_diffOk).
//   - Locking is enforced by disabling every control that could change the
//     numbers ("grey everything out"), not by intercepting each attempt -
//     lower risk, and reuses the same pattern already planned for
//     multi-machine read-only mode (see NETWORK_ARCHITECTURE.md).
//   - Un-finalize needs just a confirmation dialog, no reason text.
//   - After a successful Finalize, the app ASKS first ("Start a new entry
//     sheet now?") rather than auto-clearing.
//   - A locked-in day is written to history\<date>.fbd, a permanent record
//     separate from the rolling backups\ snapshots.
// ---------------------------------------------------------------------------

// Enables/disables every control that could change the entered numbers,
// based on whether g_finalizedDate is set, and relabels hBtnFinalize in
// place (same pattern as hBtnAdd's Add Entry/Update Entry relabeling).
// Called on load (LoadFromFile) and immediately after a successful
// Finalize/Un-finalize.
void ApplyFinalizedLockState() {
    bool locked = !g_finalizedDate.empty();
    BOOL enable = locked ? FALSE : TRUE;
    HWND toToggle[] = {
        hCmbSupplier, hCmbProduct, hEditKgs, hEditPrice, hEditNotes, hDtpDate,
        hBtnAdd, hBtnDelete, hBtnEdit, hBtnCancelEdit, hBtnDuplicate, hBtnDuplicateSupSpec,
        hEditDebtor, hEditCash
    };
    for (HWND h : toToggle) {
        if (h) EnableWindow(h, enable);
    }
    if (hBtnFinalize) {
        SetWindowTextW(hBtnFinalize, locked ? L"Un-finalize Day" : L"Finalize Day");
        EnableWindow(hBtnFinalize, TRUE); // always enabled - it's the one control that flips the lock
    }
    UpdateTitle(); // status bar reflects the FINALIZED state - see UpdateTitle
}

void DoUnfinalizeDay() {
    std::wstring msg = L"This day was finalized on " + g_finalizedDate +
        L" - reopen it for editing?";
    int r = MessageBoxW(g_hMainWnd, msg.c_str(), L"Un-finalize Day", MB_YESNO | MB_ICONQUESTION);
    if (r != IDYES) return;

    // Snapshot the locked-in state before touching anything, same as every
    // other place in this app that's about to change data the user might
    // want back (ConfirmDiscardCurrentData's callers all do the same).
    WriteBackupSnapshot();
    g_finalizedDate.clear();
    ApplyFinalizedLockState();
    g_dirty = true;
    if (!g_currentFile.empty()) {
        std::wstring err;
        if (!SaveToFile(g_currentFile, &err))
            MessageBoxW(g_hMainWnd, (L"Un-finalized, but could not re-save the file: " + err).c_str(),
                        L"Error", MB_OK | MB_ICONERROR);
    }
    UpdateTitle();
    RefreshAll(); // also re-syncs autosave.fbd, which AutosaveNow() always writes to regardless of g_currentFile
}

// Test-automation refactor (2026-09-30, v0.9.51): the actual Finalize Day
// persistence coordination - previously inline in FinalizeWndProc's
// ID_FIN_OK handler - now goes through CoordinateFinalizeDay()
// (FishBalanceCore.h), a pure function tested against every success/failure
// combination of history-write and named-file-sync (Phase 1 item 2). This
// wrapper supplies the real Win32 ports (WriteBackupSnapshot/
// WriteFileAtomicUtf8/SaveToFile) and applies the resulting state
// (g_finalizedDate, g_dirty, UpdateTitle/RefreshAll) exactly as the old
// inline code did - the history-record path/overwrite confirmation stay in
// the caller (FinalizeWndProc), since those are dialog/UI concerns, not
// persistence coordination. Also callable directly by the headless
// end-to-end smoke test (Phase 1 item 6) to finalize a day without going
// through the Finalize Day popup window at all.
FinalizeOutcome ExecuteFinalizeDay(const std::wstring& date) {
    std::wstring dir = HistoryDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\" + date + L".fbd";

    FinalizePersistencePorts ports;
    ports.writeBackupSnapshot = []() { WriteBackupSnapshot(); };
    ports.writeHistoryRecord = [path, date](std::wstring* outError) {
        std::string content = BuildFbdSaveContent(date);
        return WriteFileAtomicUtf8(path, content, outError);
    };
    ports.hasNamedFile = !g_currentFile.empty();
    ports.writeNamedFileSync = [](std::wstring* outError) {
        return SaveToFile(g_currentFile, outError);
    };

    FinalizeOutcome outcome = CoordinateFinalizeDay(ports, date);
    if (outcome.finalized) {
        g_finalizedDate = date;
        ApplyFinalizedLockState();
    }
    g_dirty = outcome.dirtyAfter;
    UpdateTitle();
    RefreshAll();
    return outcome;
}

LRESULT CALLBACK FinalizeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hFinalizeLbl = MakeControl(L"STATIC", L"Lock in today's balanced entries as of this date:", WS_VISIBLE, 0, hwnd);
        g_hFinalizeDtp = MakeControl(DATETIMEPICK_CLASS, L"", WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATEFORMAT, ID_FIN_DTP, hwnd);
        MakeControl(L"BUTTON", L"OK", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_DEFPUSHBUTTON, ID_FIN_OK, hwnd);
        MakeControl(L"BUTTON", L"Cancel", WS_VISIBLE | WS_TABSTOP, ID_FIN_CANCEL, hwnd);

        // Default the date to today, unless the current file's own name
        // parses as an ISO date (e.g. a file opened from history\ or named
        // by hand like 2026-09-20.fbd) - then default to that instead,
        // since that's much more likely to be the day actually being
        // finalized than "today" is (Finalize often happens a day or two
        // after the fact, per Jack's Monday/Tuesday workflow).
        SYSTEMTIME st;
        GetLocalTime(&st);
        if (!g_currentFile.empty()) {
            std::wstring base = g_currentFile;
            size_t slash = base.find_last_of(L"\\/");
            if (slash != std::wstring::npos) base = base.substr(slash + 1);
            size_t dot = base.find_last_of(L'.');
            if (dot != std::wstring::npos) base = base.substr(0, dot);
            SimpleDate d;
            if (ParseISODate(base, d)) { st.wYear = (WORD)d.year; st.wMonth = (WORD)d.month; st.wDay = (WORD)d.day; }
        }
        DateTime_SetSystemtime(g_hFinalizeDtp, GDT_VALID, &st);
        return 0;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        MoveWindow(g_hFinalizeLbl, S(15), S(15), rc.right - S(30), S(22), TRUE);
        MoveWindow(g_hFinalizeDtp, S(15), S(45), S(150), S(22), TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_FIN_OK), rc.right - S(180), S(80), S(80), S(28), TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_FIN_CANCEL), rc.right - S(90), S(80), S(80), S(28), TRUE);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_FIN_OK) {
            SYSTEMTIME st{};
            DateTime_GetSystemtime(g_hFinalizeDtp, &st);
            std::wstring date = FormatDateISO(st);

            std::wstring dir = HistoryDir();
            CreateDirectoryW(dir.c_str(), nullptr);
            std::wstring path = dir + L"\\" + date + L".fbd";
            DWORD attrs = GetFileAttributesW(path.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES) {
                std::wstring warnMsg = L"A history record for " + date +
                    L" already exists (history\\" + date + L".fbd). Overwrite it with today's data?";
                if (MessageBoxW(hwnd, warnMsg.c_str(), L"Already Finalized", MB_YESNO | MB_ICONWARNING) != IDYES)
                    return 0;
            }

            FinalizeOutcome outcome = ExecuteFinalizeDay(date);
            if (outcome.kind == FinalizeResultKind::HistoryWriteFailed) {
                MessageBoxW(hwnd, outcome.message.c_str(), L"Error", MB_OK | MB_ICONERROR);
                return 0;
            }

            DestroyWindow(hwnd);

            bool namedFileSynced = (outcome.kind == FinalizeResultKind::FinalizedClean);
            std::wstring successMsg = outcome.message + L" Start a new entry sheet now?";
            int r = MessageBoxW(g_hMainWnd, successMsg.c_str(), L"Day Finalized",
                namedFileSynced ? (MB_YESNO | MB_ICONQUESTION) : (MB_YESNO | MB_ICONWARNING));
            if (r == IDYES) {
                // Same clear-to-blank as DoFileNew() - and, like DoFileNew(),
                // this needs to actually detach from the file that was just
                // finalized, not just clear the on-screen list. A previous
                // version of this left g_currentFile/g_finalizedDate pointing
                // at the just-finalized file, which cleared the list but kept
                // every entry control disabled and the button reading
                // "Un-finalize Day" - a real bug Jack found: "clicking new
                // clears the file, but not the unfinalise button." The next
                // day's entries are a genuinely new, unlocked, unsaved sheet.
                g_entries.clear();
                SetWindowTextW(hEditDebtor, L"");
                SetWindowTextW(hEditCash, L"");
                g_currentFile.clear();
                g_finalizedDate.clear();
                ApplyFinalizedLockState();
                CancelEdit();
                ClearUndoState();
                g_dirty = false;
                UpdateTitle();
                RefreshAll();
            }
            return 0;
        }
        if (id == ID_FIN_CANCEL) { DestroyWindow(hwnd); return 0; }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_hMainWnd) EnableWindow(g_hMainWnd, TRUE);
        g_hFinalizeWnd = nullptr;
        SetForegroundWindow(g_hMainWnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void OpenFinalizeDatePrompt() {
    if (g_hFinalizeWnd) {
        SetForegroundWindow(g_hFinalizeWnd);
        return;
    }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = FinalizeWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"FinalizeDayWindow";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    RECT mrc;
    GetWindowRect(g_hMainWnd, &mrc);
    int w = S(340), h = S(150);
    int x = mrc.left + ((mrc.right - mrc.left) - w) / 2;
    int y = mrc.top + ((mrc.bottom - mrc.top) - h) / 2;

    g_hFinalizeWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"FinalizeDayWindow", L"Finalize Day",
                                      WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h,
                                      g_hMainWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    EnableWindow(g_hMainWnd, FALSE);
    ShowWindow(g_hFinalizeWnd, SW_SHOW);
    UpdateWindow(g_hFinalizeWnd);
    // Layout is handled by WM_SIZE (fires on creation, same as ManageWndProc).
}

void DoFinalizeDay() {
    if (!g_reconciliationValid) {
        MessageBoxW(g_hMainWnd,
            L"Debtor or Cash contains an entry that can't be understood as a number or sum - "
            L"a day can't be finalized until both fields contain a valid figure. Fix the entry "
            L"on Tab 1 (Book Reconciliation) first.",
            L"Invalid Entry", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!g_diffOk) {
        MessageBoxW(g_hMainWnd,
            L"Debtor + Cash doesn't balance against the entered total yet - a day can't be "
            L"finalized until it balances exactly. Fix the difference on Tab 1 (Book "
            L"Reconciliation) first.",
            L"Not Balanced", MB_OK | MB_ICONWARNING);
        return;
    }
    OpenFinalizeDatePrompt();
}

// ---------------------------------------------------------------------------
// CSV export (Excel-readable) - bundles every report into one file.
// ---------------------------------------------------------------------------

// CsvField() now lives in FishBalanceCore.h (unchanged logic).

bool ExportToCsv(const std::wstring& path, std::wstring* outError = nullptr) {
    std::string content;
    unsigned char bom[3] = { 0xEF, 0xBB, 0xBF }; // UTF-8 BOM, so Excel reads accents/symbols correctly
    content.append(reinterpret_cast<char*>(bom), 3);

    auto writeLine = [&](const std::wstring& line) {
        content += WToUtf8(line) + "\r\n";
    };
    auto csvRow = [&](std::initializer_list<std::wstring> fields) {
        std::wstring line;
        bool first = true;
        for (auto& fld : fields) {
            if (!first) line += L",";
            line += CsvField(fld);
            first = false;
        }
        writeLine(line);
    };

    writeLine(L"Fish Balance Manager Export");
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstringstream ss;
    ss << L"Generated " << std::setfill(L'0')
       << std::setw(4) << st.wYear << L"-" << std::setw(2) << st.wMonth << L"-" << std::setw(2) << st.wDay
       << L" " << std::setw(2) << st.wHour << L":" << std::setw(2) << st.wMinute;
    writeLine(ss.str());
    writeLine(L"");

    writeLine(L"Entries");
    csvRow({ L"Date", L"Supplier", L"Species", L"Kgs", L"Price", L"Total", L"Notes" });
    for (auto& e : g_entries)
        csvRow({ e.date, e.supplier, e.product, FormatKg(e.kgs), FormatNum(e.price), FormatNum(e.Total()), e.notes });
    writeLine(L"");

    writeLine(L"Overview by Supplier");
    csvRow({ L"Supplier", L"Kgs", L"Total" });
    {
        std::vector<std::wstring> keys;
        std::map<std::wstring, double> kgsSum, amtSum;
        for (auto& e : g_entries) {
            if (kgsSum.find(e.supplier) == kgsSum.end()) { kgsSum[e.supplier] = 0; amtSum[e.supplier] = 0; keys.push_back(e.supplier); }
            kgsSum[e.supplier] += e.kgs;
            amtSum[e.supplier] += e.Total();
        }
        std::sort(keys.begin(), keys.end());
        double gk = 0, ga = 0;
        for (auto& k : keys) { csvRow({ k, FormatKg(kgsSum[k]), FormatNum(amtSum[k]) }); gk += kgsSum[k]; ga += amtSum[k]; }
        csvRow({ L"GRAND TOTAL", FormatKg(gk), FormatNum(ga) });
    }
    writeLine(L"");

    writeLine(L"By Species (all suppliers)");
    csvRow({ L"Species", L"Kgs", L"Total", L"Avg Price", L"Highest Price", L"Lowest Price" });
    {
        std::vector<std::wstring> keys;
        std::map<std::wstring, double> kgsSum, amtSum, priceSum, priceMax, priceMin;
        std::map<std::wstring, int> priceCount;
        for (auto& e : g_entries) {
            if (kgsSum.find(e.product) == kgsSum.end()) {
                kgsSum[e.product] = 0; amtSum[e.product] = 0; priceSum[e.product] = 0; priceCount[e.product] = 0;
                priceMax[e.product] = e.price; priceMin[e.product] = e.price;
                keys.push_back(e.product);
            }
            kgsSum[e.product] += e.kgs;
            amtSum[e.product] += e.Total();
            priceSum[e.product] += e.price;
            priceCount[e.product]++;
            if (e.price > priceMax[e.product]) priceMax[e.product] = e.price;
            if (e.price < priceMin[e.product]) priceMin[e.product] = e.price;
        }
        std::sort(keys.begin(), keys.end());
        double gk = 0, ga = 0;
        for (auto& k : keys) {
            double avg = priceCount[k] > 0 ? priceSum[k] / priceCount[k] : 0;
            csvRow({ k, FormatKg(kgsSum[k]), FormatNum(amtSum[k]), FormatNum(avg), FormatNum(priceMax[k]), FormatNum(priceMin[k]) });
            gk += kgsSum[k]; ga += amtSum[k];
        }
        csvRow({ L"GRAND TOTAL", FormatKg(gk), FormatNum(ga), L"", L"", L"" });
    }
    writeLine(L"");

    writeLine(L"Breakdown (Supplier > Species > Price)");
    csvRow({ L"Supplier", L"Species", L"Price", L"Kgs", L"Total" });
    {
        auto data = BuildBreakdownData(g_entries);
        double grandKgs = 0, grandAmt = 0;
        for (auto& sg : data) {
            for (auto& pg : sg.products) {
                for (auto& pl : pg.prices)
                    csvRow({ sg.supplier, pg.species, FormatNum(pl.price), FormatKg(pl.kgs), FormatNum(pl.amt) });
                csvRow({ sg.supplier, pg.species + L" Total", L"", FormatKg(pg.totalKgs), FormatNum(pg.totalAmt) });
            }
            csvRow({ sg.supplier, L"SUPPLIER TOTAL", L"", FormatKg(sg.totalKgs), FormatNum(sg.totalAmt) });
            grandKgs += sg.totalKgs;
            grandAmt += sg.totalAmt;
        }
        csvRow({ L"", L"GRAND TOTAL", L"", FormatKg(grandKgs), FormatNum(grandAmt) });
    }

    return WriteFileAtomicUtf8(path, content, outError);
}

void DoExportCsv() {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hMainWnd;
    ofn.lpstrFilter = L"CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&ofn)) {
        std::wstring err;
        if (ExportToCsv(file, &err))
            MessageBoxW(g_hMainWnd, L"Exported. This file opens directly in Excel.", L"Export to CSV", MB_OK | MB_ICONINFORMATION);
        else
            MessageBoxW(g_hMainWnd, (L"Could not export the file: " + err).c_str(), L"Error", MB_OK | MB_ICONERROR);
    }
}

void DoAbout() {
    std::wstring title = std::wstring(L"About Fish Balance Manager - v") + APP_VERSION;
    MessageBoxW(g_hMainWnd,
        L"Fish Balance Manager\n\n"
        L"A native replacement for the BALANCE_PivotTable spreadsheet.\n\n"
        L"1. Enter each delivery on the Data Entry tab and fill in the Debtor/Cash "
        L"figures from the books to check they balance.\n"
        L"2. See a $ total per supplier on the Total Overview tab.\n"
        L"3. See a supplier -> species -> price breakdown with weights on the Breakdown tab.\n"
        L"4. See Kgs/$ totals per species, plus average/highest/lowest price, on the By Species tab.\n\n"
        L"Tip: on the Data Entry tab, use Tab to move between fields and press Enter after "
        L"Price (or Notes) to add the row and jump straight back to Supplier - handy for entering "
        L"a batch of deliveries quickly. The Date defaults to today; Notes is optional.\n\n"
        L"'Duplicate Last Entry' pre-fills the form from the most recent row and jumps to Kgs - "
        L"handy when the same supplier/species comes up again with just a different weight or price.\n\n"
        L"To fix a mistake: double-click a row (or select it and click 'Edit Selected Row') to "
        L"load it back into the form, correct it, then click 'Update Entry'. Click "
        L"'Clear / Cancel Edit' to abandon an edit without saving it.\n\n"
        L"Use 'Print Preview...' on the Breakdown tab (or File menu) to see exactly how the report "
        L"will look before printing, with Next/Previous page navigation. Click 'Print...' from there, "
        L"or use File > Print Breakdown... directly, to print the report. Choose the 'Microsoft Print "
        L"to PDF' printer in that dialog to save it as a PDF file instead.\n\n"
        L"Data is auto-saved next to the .exe as autosave.fbd, and the title bar shows which named "
        L"file (if any) is currently open. Use File > Save As... to keep a permanent copy you can "
        L"refer back to later, and File > Open... to load it again (File > Recent Files remembers "
        L"your last few).\n\n"
        L"A timestamped backup snapshot is also kept automatically (roughly every 3 minutes, every "
        L"time you use File > Save, and every time you switch files via File > Open, Recent Files, "
        L"or Restore from Backup) in a 'backups' folder next to the .exe, in case something gets "
        L"overwritten by mistake. Use File > Restore from Backup... to browse and load one - the "
        L"last 50 are kept.\n\n"
        L"Use the Filter box above the entries list to find rows quickly, or click a column header "
        L"to sort by it (click again to reverse). Deleting a row asks for confirmation, and "
        L"Edit > Undo Delete brings back the last one you removed.\n\n"
        L"An unusually high or low price gets a warning marker and red tint in the list automatically "
        L"(no popup while entering) - double-click a flagged row to review it, or right-click it to "
        L"clear the flag without reviewing.\n\n"
        L"Once a day's Debtor + Cash balances exactly, use 'Finalize Day' (Data Entry tab) to lock it "
        L"in - this writes a permanent copy to a 'history' folder next to the .exe and disables "
        L"further edits until you use 'Un-finalize Day' to reopen it.\n\n"
        L"If the same supplier or species ended up spelled two different ways, use "
        L"Tools > Manage Supplier / Species Names... to merge them into one. That same window is "
        L"also where you set each supplier's email address (select the supplier, no Species mode).\n\n"
        L"File > Export to CSV... saves everything - entries, per-supplier and per-species totals, "
        L"and the full breakdown - into one spreadsheet-ready file.\n\n"
        L"Use 'Email All Suppliers...' (Breakdown tab or File menu) to open a pre-filled email for "
        L"each supplier in your default email app, one at a time - you'll be asked to confirm "
        L"before each one opens, so send or close the current draft before the next appears. "
        L"Suppliers without a saved email address still get a draft, just with the To field left "
        L"blank for you to fill in.",
        title.c_str(), MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// Manage Supplier / Species Names popup window
//
// A small owner-disabled popup (not a real Win32 dialog resource, just a
// plain window like the rest of the app) that lists every distinct
// Supplier or Species currently in use, and lets the user rename/merge one
// or more of them at once - e.g. selecting both "Spanner" and "dam spanner"
// and renaming to "Spanner" folds them into a single entry everywhere.
// ---------------------------------------------------------------------------

void PopulateManageList() {
    SendMessageW(g_hManageList, LB_RESETCONTENT, 0, 0);
    g_manageValues.clear();

    std::vector<std::wstring> vals;
    std::map<std::wstring, int> counts;
    for (auto& e : g_entries) {
        const std::wstring& v = g_manageIsSupplier ? e.supplier : e.product;
        if (counts.find(v) == counts.end()) { counts[v] = 0; vals.push_back(v); }
        counts[v]++;
    }
    std::sort(vals.begin(), vals.end());
    for (auto& v : vals) {
        g_manageValues.push_back(v);
        std::wstring label = v + L"  (" + std::to_wstring(counts[v]) + (counts[v] == 1 ? L" entry)" : L" entries)");
        if (g_manageIsSupplier) {
            auto it = g_supplierEmails.find(v);
            if (it != g_supplierEmails.end() && !it->second.empty()) label += L"  - " + it->second;
        }
        SendMessageW(g_hManageList, LB_ADDSTRING, 0, (LPARAM)label.c_str());
    }
}

// Shows/hides and refreshes the Email field based on the current mode
// (Supplier vs Species) and how many items are selected in the list -
// editing an email only makes sense for a single selected supplier.
void UpdateManageEmailControls() {
    if (!g_hManageEmailLbl) return; // not created yet

    if (!g_manageIsSupplier) {
        ShowWindow(g_hManageEmailLbl, SW_HIDE);
        ShowWindow(g_hManageEmailEdit, SW_HIDE);
        ShowWindow(g_hManageEmailBtn, SW_HIDE);
        return;
    }
    ShowWindow(g_hManageEmailLbl, SW_SHOW);
    ShowWindow(g_hManageEmailEdit, SW_SHOW);
    ShowWindow(g_hManageEmailBtn, SW_SHOW);

    int selCount = (int)SendMessageW(g_hManageList, LB_GETSELCOUNT, 0, 0);
    if (selCount == 1) {
        int idx[1] = { -1 };
        SendMessageW(g_hManageList, LB_GETSELITEMS, 1, (LPARAM)idx);
        if (idx[0] >= 0 && idx[0] < (int)g_manageValues.size()) {
            std::wstring sup = g_manageValues[idx[0]];
            auto it = g_supplierEmails.find(sup);
            SetWindowTextW(g_hManageEmailEdit, it != g_supplierEmails.end() ? it->second.c_str() : L"");
            EnableWindow(g_hManageEmailEdit, TRUE);
            EnableWindow(g_hManageEmailBtn, TRUE);
            return;
        }
    }
    SetWindowTextW(g_hManageEmailEdit, L"");
    EnableWindow(g_hManageEmailEdit, FALSE);
    EnableWindow(g_hManageEmailBtn, FALSE);
}

// Enables Apply only when there's actually something it could do: at least
// one name selected in the list, and a non-empty target to rename/merge
// them to. Previously the button was always clickable and just showed a
// MessageBox error if either condition wasn't met - this reflects that
// state up front instead.
void UpdateManageApplyButton() {
    if (!g_hManageApplyBtn) return; // not created yet
    int selCount = (int)SendMessageW(g_hManageList, LB_GETSELCOUNT, 0, 0);
    wchar_t buf[256];
    GetWindowTextW(g_hManageTarget, buf, 256);
    bool hasTarget = !TrimW(buf).empty();
    EnableWindow(g_hManageApplyBtn, (selCount > 0 && hasTarget) ? TRUE : FALSE);
}

void ManageSaveEmail() {
    int selCount = (int)SendMessageW(g_hManageList, LB_GETSELCOUNT, 0, 0);
    if (selCount != 1) {
        MessageBoxW(g_hManageWnd, L"Select exactly one supplier to set its email address.", L"Select One Supplier", MB_OK | MB_ICONINFORMATION);
        return;
    }
    int idx[1] = { -1 };
    SendMessageW(g_hManageList, LB_GETSELITEMS, 1, (LPARAM)idx);
    if (idx[0] < 0 || idx[0] >= (int)g_manageValues.size()) return;
    std::wstring supplier = g_manageValues[idx[0]];

    wchar_t buf[256];
    GetWindowTextW(g_hManageEmailEdit, buf, 256);
    std::wstring email = TrimW(buf);

    if (!email.empty()) {
        if (email.find(L'|') != std::wstring::npos) {
            MessageBoxW(g_hManageWnd, L"Email addresses can't contain the '|' character.", L"Invalid Email", MB_OK | MB_ICONWARNING);
            return;
        }
        if (!LooksLikeEmail(email)) {
            MessageBoxW(g_hManageWnd, L"That doesn't look like a valid email address.", L"Invalid Email", MB_OK | MB_ICONWARNING);
            return;
        }
    }

    if (email.empty()) g_supplierEmails.erase(supplier);
    else g_supplierEmails[supplier] = email;
    bool saved = SaveSupplierEmails();

    PopulateManageList();
    // Re-select the same supplier so the email field stays populated.
    for (size_t i = 0; i < g_manageValues.size(); i++) {
        if (g_manageValues[i] == supplier) { SendMessageW(g_hManageList, LB_SETSEL, TRUE, (LPARAM)i); break; }
    }
    UpdateManageEmailControls();
    if (saved) {
        MessageBoxW(g_hManageWnd, email.empty() ? L"Email cleared." : L"Email saved.", L"Done", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(g_hManageWnd,
            L"Could not save the email address to disk - the change is still in memory for this "
            L"session, but will be lost when the app closes unless the save succeeds. Check that "
            L"emails.txt isn't open in another program and try again.",
            L"Save Failed", MB_OK | MB_ICONWARNING);
    }
}

void ManageApply() {
    int selCount = (int)SendMessageW(g_hManageList, LB_GETSELCOUNT, 0, 0);
    if (selCount <= 0) {
        MessageBoxW(g_hManageWnd, L"Select one or more names in the list to rename/merge.", L"Nothing Selected", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::vector<int> idxs((size_t)selCount);
    SendMessageW(g_hManageList, LB_GETSELITEMS, (WPARAM)selCount, (LPARAM)idxs.data());

    wchar_t buf[256];
    GetWindowTextW(g_hManageTarget, buf, 256);
    std::wstring target = TrimW(buf);
    if (target.empty()) {
        MessageBoxW(g_hManageWnd, L"Enter the name to rename/merge the selected items to.", L"Missing Target", MB_OK | MB_ICONWARNING);
        return;
    }
    if (target.find(L'|') != std::wstring::npos) {
        MessageBoxW(g_hManageWnd, L"Names can't contain the '|' character - it's used internally as a separator when saving.", L"Invalid Name", MB_OK | MB_ICONWARNING);
        return;
    }

    std::vector<std::wstring> sourceNames;
    for (int idx : idxs)
        if (idx >= 0 && idx < (int)g_manageValues.size()) sourceNames.push_back(g_manageValues[idx]);

    // Supplier email addresses are keyed by supplier name, so a
    // rename/merge of SUPPLIER names (not species - g_supplierEmails has
    // no species concept) needs to migrate any saved email(s) too,
    // otherwise the address becomes silently orphaned under a name that
    // no longer exists anywhere. This is resolved BEFORE touching
    // g_entries at all below, so if the user cancels a genuine conflict,
    // nothing has changed yet - a clean abort, not a half-applied rename.
    //
    // Conflict handling: gather every DISTINCT saved email among the
    // target's own existing email (if any) and each selected source's
    // email. Zero or one distinct value found means no real conflict -
    // proceed automatically. Exactly two distinct values (by far the most
    // common real conflict - merging two suppliers who each have a
    // different saved address) - ask which to keep. Three or more
    // distinct values is rare enough that a full picker UI wasn't judged
    // worth building; the first one found is kept, but the user is told a
    // conflict existed rather than that being silent.
    std::wstring resolvedEmail;
    bool haveResolvedEmail = false;
    if (g_manageIsSupplier) {
        std::vector<std::wstring> candidates;
        auto addCandidate = [&](const std::wstring& email) {
            if (email.empty()) return;
            for (auto& c : candidates) if (c == email) return; // already have it
            candidates.push_back(email);
        };
        auto targetIt = g_supplierEmails.find(target);
        if (targetIt != g_supplierEmails.end()) addCandidate(targetIt->second);
        for (auto& src : sourceNames) {
            if (src == target) continue;
            auto it = g_supplierEmails.find(src);
            if (it != g_supplierEmails.end()) addCandidate(it->second);
        }

        if (candidates.size() == 1) {
            resolvedEmail = candidates[0];
            haveResolvedEmail = true;
        } else if (candidates.size() == 2) {
            std::wstring msg = L"The suppliers being merged have different saved email addresses:\n\n"
                                L"Yes = keep " + candidates[0] + L"\n"
                                L"No = keep " + candidates[1] + L"\n"
                                L"Cancel = don't merge yet, so you can check the addresses first";
            int r = MessageBoxW(g_hManageWnd, msg.c_str(), L"Which Email Should Be Kept?",
                                 MB_YESNOCANCEL | MB_ICONQUESTION);
            if (r == IDCANCEL) return; // nothing has been changed yet - clean abort
            resolvedEmail = (r == IDYES) ? candidates[0] : candidates[1];
            haveResolvedEmail = true;
        } else if (candidates.size() > 2) {
            resolvedEmail = candidates[0];
            haveResolvedEmail = true;
            std::wstring msg = L"Note: the suppliers being merged have " + std::to_wstring(candidates.size()) +
                                L" different saved email addresses. Kept: " + resolvedEmail +
                                L"\n\nYou can fix this afterward via Manage Names if it kept the wrong one.";
            MessageBoxW(g_hManageWnd, msg.c_str(), L"Multiple Emails Found", MB_OK | MB_ICONWARNING);
        }
    }

    int changed = 0;
    for (auto& e : g_entries) {
        std::wstring& field = g_manageIsSupplier ? e.supplier : e.product;
        for (auto& src : sourceNames) {
            if (field == src && field != target) { field = target; changed++; break; }
        }
    }

    bool emailsChanged = false;
    if (g_manageIsSupplier && haveResolvedEmail) {
        auto targetIt = g_supplierEmails.find(target);
        if (targetIt == g_supplierEmails.end() || targetIt->second != resolvedEmail) {
            g_supplierEmails[target] = resolvedEmail;
            emailsChanged = true;
        }
        for (auto& src : sourceNames) {
            if (src == target) continue;
            if (g_supplierEmails.erase(src) > 0) emailsChanged = true;
        }
    }
    bool emailsSaved = true;
    if (emailsChanged) emailsSaved = SaveSupplierEmails();

    if (g_editIndex != -1) CancelEdit();
    // A pending Undo Delete buffer holds a COPY of a row from before this
    // rename - if it happened to carry one of the old names, undoing it
    // after this point would silently reintroduce the spelling variant
    // this rename/merge was meant to eliminate. Simplest safe rule: any
    // actual rename invalidates the buffer, rather than trying to track
    // whether that specific row was affected.
    if (changed > 0) { ClearUndoState(); g_dirty = true; } // v0.9.30
    RefreshAll();
    PopulateManageList();
    SetWindowTextW(g_hManageTarget, L"");
    UpdateManageApplyButton();
    UpdateManageEmailControls(); // selection was lost when the list repopulated above

    std::wstring msg = L"Updated " + std::to_wstring(changed) + (changed == 1 ? L" entry." : L" entries.");
    if (emailsChanged && !emailsSaved) {
        msg += L"\n\nWarning: a saved email address was migrated in memory but could not be "
               L"written to emails.txt - it will be lost if the app closes before this succeeds. "
               L"Check emails.txt isn't open in another program.";
    }
    MessageBoxW(g_hManageWnd, msg.c_str(), L"Done", MB_OK | (emailsChanged && !emailsSaved ? MB_ICONWARNING : MB_ICONINFORMATION));
}

LRESULT CALLBACK ManageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hManageRadioSupplier = MakeControl(L"BUTTON", L"Supplier", WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP, ID_MNG_RADIO_SUPPLIER, hwnd);
        g_hManageRadioSpecies = MakeControl(L"BUTTON", L"Species", WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, ID_MNG_RADIO_SPECIES, hwnd);
        g_hManageHint = MakeControl(L"STATIC", L"Select one or more names to merge/rename, or select a single Supplier to set its email address.",
                                     WS_VISIBLE, 0, hwnd);
        g_hManageList = MakeControl(L"LISTBOX", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_MULTIPLESEL | LBS_NOTIFY, ID_MNG_LIST, hwnd);
        g_hManageEmailLbl = MakeControl(L"STATIC", L"Email for selected supplier:", WS_VISIBLE, 0, hwnd);
        g_hManageEmailEdit = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP, ID_MNG_EMAIL_EDIT, hwnd);
        g_hManageEmailBtn = MakeControl(L"BUTTON", L"Save Email", WS_VISIBLE | WS_TABSTOP, ID_MNG_EMAIL_SAVE, hwnd);
        g_hManageTargetLbl = MakeControl(L"STATIC", L"Rename / merge selected to:", WS_VISIBLE, 0, hwnd);
        g_hManageTarget = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP, ID_MNG_TARGET, hwnd);
        g_hManageApplyBtn = MakeControl(L"BUTTON", L"Apply", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_DEFPUSHBUTTON, ID_MNG_APPLY, hwnd);
        g_hManageCloseBtn = MakeControl(L"BUTTON", L"Close", WS_VISIBLE | WS_TABSTOP, ID_MNG_CLOSE, hwnd);
        // Phase 1 completeness follow-up (2026-09-30, v0.9.50): g_hManageTarget
        // is a Supplier/Species NAME field like any other - without this, a
        // name longer than the 255-wchar GetWindowTextW buffer used to read it
        // (ManageApply/UpdateManageApplyButton) would be silently truncated at
        // read time instead of being visibly capped at entry time. The buffer
        // itself is unchanged (256 wchar = 255 usable chars, already exactly
        // kMaxSupplierSpeciesLength), so this makes the existing bound an
        // explicit UI limit rather than an accidental one. g_hManageEmailEdit
        // gets the same bound for consistency, even though Jack's spec didn't
        // separately name an email-address limit - same buffer, same risk.
        {
            FieldLengthLimitTargets limits;
            limits.manageTargetEdit = g_hManageTarget;
            limits.manageEmailEdit = g_hManageEmailEdit;
            ApplyFieldLengthLimits(limits);
        }

        SendMessageW(g_hManageRadioSupplier, BM_SETCHECK, BST_CHECKED, 0);
        g_manageIsSupplier = true;
        PopulateManageList();
        UpdateManageEmailControls();
        UpdateManageApplyButton();
        return 0;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        MoveWindow(g_hManageRadioSupplier, S(15), S(15), S(100), S(24), TRUE);
        MoveWindow(g_hManageRadioSpecies, S(125), S(15), S(100), S(24), TRUE);
        MoveWindow(g_hManageHint, S(15), S(45), rc.right - S(30), S(38), TRUE);

        int listBottom = rc.bottom - S(200);
        if (listBottom < S(130)) listBottom = S(130);
        MoveWindow(g_hManageList, S(15), S(90), rc.right - S(30), listBottom - S(90), TRUE);

        int emailY = listBottom + S(12);
        MoveWindow(g_hManageEmailLbl, S(15), emailY, S(200), S(22), TRUE);
        MoveWindow(g_hManageEmailEdit, S(15), emailY + S(22), rc.right - S(30) - S(95), S(22), TRUE);
        MoveWindow(g_hManageEmailBtn, rc.right - S(100), emailY + S(21), S(85), S(24), TRUE);

        int y2 = emailY + S(64);
        MoveWindow(g_hManageTargetLbl, S(15), y2, rc.right - S(30), S(22), TRUE);
        MoveWindow(g_hManageTarget, S(15), y2 + S(22), rc.right - S(30), S(22), TRUE);
        MoveWindow(g_hManageApplyBtn, rc.right - S(190), y2 + S(52), S(85), S(28), TRUE);
        MoveWindow(g_hManageCloseBtn, rc.right - S(100), y2 + S(52), S(85), S(28), TRUE);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if ((id == ID_MNG_RADIO_SUPPLIER || id == ID_MNG_RADIO_SPECIES) && code == BN_CLICKED) {
            g_manageIsSupplier = (id == ID_MNG_RADIO_SUPPLIER);
            PopulateManageList();
            UpdateManageEmailControls();
            UpdateManageApplyButton();
            return 0;
        }
        if (id == ID_MNG_LIST && code == LBN_SELCHANGE) {
            UpdateManageEmailControls();
            UpdateManageApplyButton();
            return 0;
        }
        if (id == ID_MNG_TARGET && code == EN_CHANGE) {
            UpdateManageApplyButton();
            return 0;
        }
        if (id == ID_MNG_EMAIL_SAVE) { ManageSaveEmail(); return 0; }
        if (id == ID_MNG_APPLY) { ManageApply(); return 0; }
        if (id == ID_MNG_CLOSE) { DestroyWindow(hwnd); return 0; }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_hMainWnd) EnableWindow(g_hMainWnd, TRUE);
        g_hManageWnd = nullptr;
        SetForegroundWindow(g_hMainWnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void OpenManageNamesWindow() {
    if (g_hManageWnd) {
        SetForegroundWindow(g_hManageWnd);
        return;
    }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ManageWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"ManageNamesWindow";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    RECT mrc;
    GetWindowRect(g_hMainWnd, &mrc);
    int w = S(460), h = S(540);
    int x = mrc.left + ((mrc.right - mrc.left) - w) / 2;
    int y = mrc.top + ((mrc.bottom - mrc.top) - h) / 2;

    g_hManageWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ManageNamesWindow", L"Manage Supplier / Species Names",
                                    WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h,
                                    g_hMainWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    EnableWindow(g_hMainWnd, FALSE);
    ShowWindow(g_hManageWnd, SW_SHOW);
    UpdateWindow(g_hManageWnd);
}

// ---------------------------------------------------------------------------
// Report rendering - shared by Print Preview and actual printing, so the
// two can never show different content. Renders into device-independent
// bitmaps (DIBs): reliable for on-screen display (Print Preview) AND for
// sending to a real printer via StretchDIBits, which avoids a known GDI
// pitfall where BitBlt-ing a screen-compatible bitmap directly to some
// printer drivers silently fails or prints garbage.
// ---------------------------------------------------------------------------

void FreeRenderedPages(std::vector<RenderedPage>& pages) {
    for (auto& p : pages) if (p.bitmap) DeleteObject(p.bitmap);
    pages.clear();
}

std::vector<RenderedPage> RenderReportPages(int pageWidthPx, int pageHeightPx, int dpiX, int dpiY) {
    std::vector<RenderedPage> pages;

    auto makeFont = [&](int points, bool bold) {
        LOGFONTW lf{};
        lf.lfHeight = -MulDiv(points, dpiY, 72);
        lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
        const wchar_t* face = L"Segoe UI";
#ifdef _MSC_VER
        wcsncpy_s(lf.lfFaceName, LF_FACESIZE, face, _TRUNCATE);
#else
        wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
        lf.lfFaceName[LF_FACESIZE - 1] = 0;
#endif
        return CreateFontIndirectW(&lf);
    };
    HFONT hFontTitle = makeFont(18, true);
    HFONT hFontNormal = makeFont(11, false);
    HFONT hFontBold = makeFont(11, true);

    int marginX = dpiX / 2; // 0.5"
    int marginY = dpiY / 2;
    int colSpeciesX = marginX;
    int colPriceX = marginX + MulDiv(280, dpiX, 96);
    int colKgsX = marginX + MulDiv(400, dpiX, 96);
    int colTotalX = marginX + MulDiv(510, dpiX, 96);
    int rightEdge = pageWidthPx - marginX;
    int lineHeight = MulDiv(22, dpiY, 96);

    HDC curDC = nullptr;
    int y = 0;
    int pageNum = 0;

    // Hard safety cap on page count: a normal business report (dozens of
    // suppliers/species) never comes close to this - each page holds
    // several dozen rows - so this only ever engages on pathological or
    // unexpectedly large input, and guarantees a hard ceiling on memory
    // use (this function is shared by both Print Preview and the real
    // print path, so the cap protects both). Content beyond the cap is
    // silently dropped rather than growing the page count unboundedly;
    // there's no in-report "truncated" notice, since a normal user should
    // never actually reach this limit.
    const int kMaxPages = 200;
    bool pageCapHit = false;

    auto finishPage = [&]() {
        if (curDC) { DeleteDC(curDC); curDC = nullptr; }
    };

    auto drawHeaderRow = [&]() {
        SelectObject(curDC, hFontBold);
        TextOutW(curDC, colSpeciesX, y, L"Species", 7);
        TextOutW(curDC, colPriceX, y, L"Price ($/kg)", 12);
        TextOutW(curDC, colKgsX, y, L"Kgs (weight)", 12);
        TextOutW(curDC, colTotalX, y, L"Total ($)", 9);
        y += lineHeight;
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
        HGDIOBJ oldPen = SelectObject(curDC, pen);
        MoveToEx(curDC, marginX, y, nullptr);
        LineTo(curDC, rightEdge, y);
        SelectObject(curDC, oldPen);
        DeleteObject(pen);
        y += lineHeight / 2;
        SelectObject(curDC, hFontNormal);
    };

    auto newPage = [&]() {
        if (pageNum >= kMaxPages) { pageCapHit = true; return; }
        finishPage();

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = pageWidthPx;
        bmi.bmiHeader.biHeight = -pageHeightPx; // negative = top-down DIB, matches normal GDI (0,0)-at-top-left drawing
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;

        RenderedPage page;
        page.width = pageWidthPx;
        page.height = pageHeightPx;
        page.bitmap = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &page.bits, nullptr, 0);

        curDC = CreateCompatibleDC(nullptr);
        SelectObject(curDC, page.bitmap);
        RECT full{ 0, 0, pageWidthPx, pageHeightPx };
        FillRect(curDC, &full, (HBRUSH)GetStockObject(WHITE_BRUSH));
        SetBkMode(curDC, TRANSPARENT);

        pages.push_back(page);
        pageNum++;
        y = marginY;
        SelectObject(curDC, hFontTitle);
        TextOutW(curDC, marginX, y, L"Fish Balance - Breakdown Report", 32);
        y += MulDiv(32, dpiY, 96);

        SelectObject(curDC, hFontNormal);
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::wstringstream ss;
        ss << L"Generated " << std::setfill(L'0')
           << std::setw(4) << st.wYear << L"-" << std::setw(2) << st.wMonth << L"-" << std::setw(2) << st.wDay
           << L" " << std::setw(2) << st.wHour << L":" << std::setw(2) << st.wMinute;
        std::wstring ts = ss.str();
        TextOutW(curDC, marginX, y, ts.c_str(), (int)ts.size());
        y += lineHeight * 2;

        if (pageNum == 1) {
            // Book reconciliation summary, so the printout also serves as the
            // balance check document.
            double entered = 0;
            for (auto& e : g_entries) entered += e.Total();
            // Phase 1 completeness follow-up (2026-09-30, v0.9.50): same
            // buffer-size fix as RecalcTotals() - see its comment.
            wchar_t buf[kMaxDebtorCashExprLength + 1];
            GetWindowTextW(hEditDebtor, buf, (int)(kMaxDebtorCashExprLength + 1));
            SumParseResult debtorResult = ParseSumExprStrict(buf);
            GetWindowTextW(hEditCash, buf, (int)(kMaxDebtorCashExprLength + 1));
            SumParseResult cashResult = ParseSumExprStrict(buf);

            std::wstring l1, l2, l3;
            if (!debtorResult.ok || !cashResult.ok) {
                // F5 (Phase 1): fail closed here too - a printed/PDF report
                // must never show a Book Total or Difference computed from
                // a Debtor/Cash entry that couldn't actually be parsed.
                l1 = L"Book Total (Debtor + Cash): --";
                l2 = L"Entered Total: " + FormatMoney(entered);
                l3 = L"Difference: cannot check - Debtor/Cash has an invalid entry";
            } else {
                double debtor = debtorResult.value;
                double cash = cashResult.value;
                double book = debtor + cash;
                double diff = book - entered;
                bool ok = std::abs(diff) < 0.005;
                l1 = L"Book Total (Debtor + Cash): " + FormatMoney(book);
                l2 = L"Entered Total: " + FormatMoney(entered);
                l3 = L"Difference: " + FormatMoney(diff) + (ok ? L"  (OK - balanced)" : L"  (OUT OF BALANCE)");
            }
            TextOutW(curDC, marginX, y, l1.c_str(), (int)l1.size());
            y += lineHeight;
            TextOutW(curDC, marginX, y, l2.c_str(), (int)l2.size());
            y += lineHeight;
            SelectObject(curDC, hFontBold);
            TextOutW(curDC, marginX, y, l3.c_str(), (int)l3.size());
            SelectObject(curDC, hFontNormal);
            y += lineHeight * 2;
        }

        drawHeaderRow();
    };

    auto ensureSpace = [&](int need) {
        if (y + need > pageHeightPx - marginY) newPage();
    };

    auto drawRow = [&](const std::wstring& species, const std::wstring& priceStr, double kgs, double amt, bool bold) {
        ensureSpace(lineHeight);
        SelectObject(curDC, bold ? hFontBold : hFontNormal);
        TextOutW(curDC, colSpeciesX, y, species.c_str(), (int)species.size());
        if (!priceStr.empty()) TextOutW(curDC, colPriceX, y, priceStr.c_str(), (int)priceStr.size());
        std::wstring kgsS = FormatKg(kgs);
        TextOutW(curDC, colKgsX, y, kgsS.c_str(), (int)kgsS.size());
        std::wstring amtS = FormatMoney(amt);
        TextOutW(curDC, colTotalX, y, amtS.c_str(), (int)amtS.size());
        y += lineHeight;
    };

    newPage();

    auto data = BuildBreakdownData(g_entries);
    double grandKgs = 0, grandAmt = 0;
    for (auto& sg : data) {
        ensureSpace(lineHeight * 2);
        y += lineHeight / 2;
        SelectObject(curDC, hFontBold);
        TextOutW(curDC, marginX, y, sg.supplier.c_str(), (int)sg.supplier.size());
        y += lineHeight;
        SelectObject(curDC, hFontNormal);

        for (auto& pg : sg.products) {
            for (auto& pl : pg.prices)
                drawRow(pg.species, FormatMoney(pl.price), pl.kgs, pl.amt, false);
            drawRow(pg.species + L" Total", L"", pg.totalKgs, pg.totalAmt, true);
        }
        drawRow(sg.supplier + L" Total", L"", sg.totalKgs, sg.totalAmt, true);
        grandKgs += sg.totalKgs;
        grandAmt += sg.totalAmt;
        y += lineHeight / 2;
    }

    ensureSpace(lineHeight * 2);
    HPEN pen2 = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    HGDIOBJ oldPen2 = SelectObject(curDC, pen2);
    MoveToEx(curDC, marginX, y, nullptr);
    LineTo(curDC, rightEdge, y);
    SelectObject(curDC, oldPen2);
    DeleteObject(pen2);
    y += lineHeight / 2;
    drawRow(L"GRAND TOTAL", L"", grandKgs, grandAmt, true);

    finishPage();
    DeleteObject(hFontTitle);
    DeleteObject(hFontNormal);
    DeleteObject(hFontBold);
    return pages;
}

// Prints the Breakdown report via the standard Windows print dialog. To get a
// PDF, the user just picks the built-in "Microsoft Print to PDF" printer in
// that dialog - no PDF library is needed, Windows handles the conversion.
void PrintBreakdownReport(HWND owner) {
    PRINTDLGW pd{};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = owner;
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION;
    if (!PrintDlgW(&pd)) return; // user cancelled, or no printers installed

    // PrintDlgW allocates these two blocks; we're responsible for freeing
    // them regardless of how this function exits from here on.
    auto freeDlgHandles = [&]() {
        if (pd.hDevMode) GlobalFree(pd.hDevMode);
        if (pd.hDevNames) GlobalFree(pd.hDevNames);
    };

    HDC hdc = pd.hDC;
    if (!hdc) { freeDlgHandles(); return; }

    int pageWidthPx = GetDeviceCaps(hdc, HORZRES);
    int pageHeightPx = GetDeviceCaps(hdc, VERTRES);
    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);

    std::vector<RenderedPage> pages = RenderReportPages(pageWidthPx, pageHeightPx, dpiX, dpiY);
    if (pages.empty()) { DeleteDC(hdc); freeDlgHandles(); return; }

    DOCINFOW di{};
    di.cbSize = sizeof(di);
    di.lpszDocName = L"Fish Balance - Breakdown Report";
    if (StartDocW(hdc, &di) <= 0) {
        DeleteDC(hdc);
        freeDlgHandles();
        FreeRenderedPages(pages);
        return;
    }

    for (auto& page : pages) {
        StartPage(hdc);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = page.width;
        bmi.bmiHeader.biHeight = -page.height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(hdc, 0, 0, page.width, page.height, 0, 0, page.width, page.height,
                      page.bits, &bmi, DIB_RGB_COLORS, SRCCOPY);
        EndPage(hdc);
    }

    EndDoc(hdc);
    DeleteDC(hdc);
    freeDlgHandles();
    FreeRenderedPages(pages);
}

// ---------------------------------------------------------------------------
// Print Preview popup window
// ---------------------------------------------------------------------------

void UpdatePreviewLabel() {
    if (!g_hPreviewPageLbl) return;
    std::wstring lbl = L"Page " + std::to_wstring(g_previewPageIndex + 1) + L" of " + std::to_wstring(g_previewPages.size());
    SetWindowTextW(g_hPreviewPageLbl, lbl.c_str());
    if (g_hPreviewPrevBtn) EnableWindow(g_hPreviewPrevBtn, g_previewPageIndex > 0);
    if (g_hPreviewNextBtn) EnableWindow(g_hPreviewNextBtn, g_previewPageIndex < (int)g_previewPages.size() - 1);
}

LRESULT CALLBACK PreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hPreviewPrevBtn = MakeControl(L"BUTTON", L"< Previous", WS_VISIBLE | WS_TABSTOP, ID_PREVIEW_PREV, hwnd);
        g_hPreviewNextBtn = MakeControl(L"BUTTON", L"Next >", WS_VISIBLE | WS_TABSTOP, ID_PREVIEW_NEXT, hwnd);
        g_hPreviewPageLbl = MakeControl(L"STATIC", L"", WS_VISIBLE, 0, hwnd);
        g_hPreviewPrintBtn = MakeControl(L"BUTTON", L"Print...", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_DEFPUSHBUTTON, ID_PREVIEW_PRINT, hwnd);
        g_hPreviewCloseBtn = MakeControl(L"BUTTON", L"Close", WS_VISIBLE | WS_TABSTOP, ID_PREVIEW_CLOSE, hwnd);
        UpdatePreviewLabel();
        return 0;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int toolbarY = rc.bottom - S(42);
        MoveWindow(g_hPreviewPrevBtn, S(15), toolbarY, S(95), S(28), TRUE);
        MoveWindow(g_hPreviewNextBtn, S(115), toolbarY, S(95), S(28), TRUE);
        MoveWindow(g_hPreviewPageLbl, S(220), toolbarY + S(6), S(180), S(22), TRUE);
        MoveWindow(g_hPreviewPrintBtn, rc.right - S(200), toolbarY, S(90), S(28), TRUE);
        MoveWindow(g_hPreviewCloseBtn, rc.right - S(100), toolbarY, S(85), S(28), TRUE);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        RECT pageArea = { 0, 0, rc.right, rc.bottom - S(50) };
        FillRect(hdc, &pageArea, (HBRUSH)GetStockObject(GRAY_BRUSH));

        if (g_previewPageIndex >= 0 && g_previewPageIndex < (int)g_previewPages.size()) {
            RenderedPage& page = g_previewPages[g_previewPageIndex];
            if (page.width > 0 && page.height > 0) {
                double pageAspect = (double)page.width / (double)page.height;
                int availW = (pageArea.right - pageArea.left) - S(40);
                int availH = (pageArea.bottom - pageArea.top) - S(40);
                if (availW < 10) availW = 10;
                if (availH < 10) availH = 10;
                int drawW = availW;
                int drawH = (int)(availW / pageAspect);
                if (drawH > availH) {
                    drawH = availH;
                    drawW = (int)(availH * pageAspect);
                }
                int drawX = pageArea.left + ((pageArea.right - pageArea.left) - drawW) / 2;
                int drawY = pageArea.top + ((pageArea.bottom - pageArea.top) - drawH) / 2;

                HDC memDC = CreateCompatibleDC(hdc);
                HGDIOBJ oldBmp = SelectObject(memDC, page.bitmap);
                SetStretchBltMode(hdc, HALFTONE);
                StretchBlt(hdc, drawX, drawY, drawW, drawH, memDC, 0, 0, page.width, page.height, SRCCOPY);
                SelectObject(memDC, oldBmp);
                DeleteDC(memDC);

                HPEN pen = CreatePen(PS_SOLID, 1, RGB(90, 90, 90));
                HGDIOBJ oldPen = SelectObject(hdc, pen);
                HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, drawX, drawY, drawX + drawW, drawY + drawH);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(pen);
            }
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        switch (id) {
        case ID_PREVIEW_PREV:
            if (g_previewPageIndex > 0) { g_previewPageIndex--; UpdatePreviewLabel(); InvalidateRect(hwnd, nullptr, TRUE); }
            return 0;
        case ID_PREVIEW_NEXT:
            if (g_previewPageIndex < (int)g_previewPages.size() - 1) { g_previewPageIndex++; UpdatePreviewLabel(); InvalidateRect(hwnd, nullptr, TRUE); }
            return 0;
        case ID_PREVIEW_PRINT:
            // Close the preview first, then hand off to the real print
            // dialog on the main window - safe, since DestroyWindow fully
            // processes WM_DESTROY (freeing pages, re-enabling the main
            // window) before returning here.
            DestroyWindow(hwnd);
            PrintBreakdownReport(g_hMainWnd);
            return 0;
        case ID_PREVIEW_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        FreeRenderedPages(g_previewPages);
        if (g_hMainWnd) EnableWindow(g_hMainWnd, TRUE);
        g_hPreviewWnd = nullptr;
        SetForegroundWindow(g_hMainWnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void OpenPrintPreview(HWND owner) {
    if (g_hPreviewWnd) {
        SetForegroundWindow(g_hPreviewWnd);
        return;
    }

    // Size the preview to the default printer's page, so it's a realistic
    // representation of what will actually print. If there's no default
    // printer configured at all (e.g. a fresh machine with none installed),
    // or getting its info fails, fall back to a generic Letter-sized page
    // (the declaration defaults below) so preview still works.
    int pageWidthPx = 850, pageHeightPx = 1100, dpiX = 100, dpiY = 100;
    wchar_t printerName[256];
    DWORD nameSize = 256;
    if (GetDefaultPrinterW(printerName, &nameSize)) {
        HDC icDC = CreateICW(L"WINSPOOL", printerName, nullptr, nullptr);
        if (icDC) {
            pageWidthPx = GetDeviceCaps(icDC, HORZRES);
            pageHeightPx = GetDeviceCaps(icDC, VERTRES);
            dpiX = GetDeviceCaps(icDC, LOGPIXELSX);
            dpiY = GetDeviceCaps(icDC, LOGPIXELSY);
            DeleteDC(icDC);
        }
    }

    // The preview is only ever shown shrunk down to fit a small on-screen
    // window (see `w` below), so rendering at the printer's full native DPI
    // (often 600+) wastes enormous amounts of memory for zero visible
    // benefit - a single 24-bit page at 600 DPI is roughly 100MB, and a
    // multi-page report could reach several GB. Cap the PREVIEW's DPI
    // (only the preview - PrintBreakdownReport() still uses the real
    // printer DPI for actual print quality) while scaling the pixel
    // dimensions down to match, so the real printer's page proportions
    // (Letter/A4/etc) are still preserved exactly, just at a screen-
    // appropriate resolution.
    const int kPreviewDpiCap = 150;
    int previewDpiX = std::min(dpiX, kPreviewDpiCap);
    int previewDpiY = std::min(dpiY, kPreviewDpiCap);
    int previewWidthPx = MulDiv(pageWidthPx, previewDpiX, dpiX);
    int previewHeightPx = MulDiv(pageHeightPx, previewDpiY, dpiY);

    FreeRenderedPages(g_previewPages);
    g_previewPages = RenderReportPages(previewWidthPx, previewHeightPx, previewDpiX, previewDpiY);
    g_previewPageIndex = 0;

    if (g_previewPages.empty()) {
        MessageBoxW(owner, L"Nothing to preview yet.", L"Print Preview", MB_OK | MB_ICONINFORMATION);
        return;
    }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = PreviewWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(GRAY_BRUSH);
        wc.lpszClassName = L"PrintPreviewWindow";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    RECT mrc;
    GetWindowRect(g_hMainWnd, &mrc);
    int w = S(680), h = S(820);
    int x = mrc.left + ((mrc.right - mrc.left) - w) / 2;
    int y = mrc.top + ((mrc.bottom - mrc.top) - h) / 2;

    g_hPreviewWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PrintPreviewWindow", L"Print Preview - Breakdown Report",
                                     WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, x, y, w, h,
                                     g_hMainWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    EnableWindow(g_hMainWnd, FALSE);
    ShowWindow(g_hPreviewWnd, SW_SHOW);
    UpdateWindow(g_hPreviewWnd);
}


// ---------------------------------------------------------------------------
// Email Suppliers
//
// Opens one pre-filled compose window per supplier in the user's default
// email app (via the mailto: protocol), for review before sending - no SMTP
// server or stored credentials needed. Supplier addresses come from
// Tools > Manage Supplier / Species Names... (g_supplierEmails).
// ---------------------------------------------------------------------------

// TodayDateString keeps its original no-argument signature here (nothing
// else in main.cpp needs to change) but delegates to the portable,
// date-parameterized version in FishBalanceCore.h for the actual logic,
// reading the current time via GetLocalTime at this one Win32 boundary.
// (A no-argument GreetingForNow() used to sit here too, wrapping
// GreetingForHour() the same way - removed as dead code, v0.9.43: the
// actual email body is built by BuildSupplierEmailBody() in
// FishBalanceCore.h, which takes the current hour as an explicit
// parameter from EmailSupplier() below and calls GreetingForHour()
// directly, so GreetingForNow() had no remaining caller.)
std::wstring TodayDateString() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return FormatLongDate(SimpleDate{ st.wYear, st.wMonth, st.wDay });
}

// UrlEncodeForMailto, LooksLikeEmail, PadRight, and BuildSupplierEmailBody
// now all live in FishBalanceCore.h (unchanged logic; BuildSupplierEmailBody
// there takes the current hour as an explicit parameter instead of reading
// the clock internally, so it can be unit-tested with any hour value - the
// call site in EmailSupplier below passes the real current hour in).

bool EmailSupplier(const std::wstring& email, const SupplierGroup& sg) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring subject = L"Delivery Summary - " + TodayDateString();
    if (email.empty()) subject += L" - " + sg.supplier; // no To address to identify the draft by, so name it in the subject
    std::wstring body = BuildSupplierEmailBody(sg, st.wHour);
    std::wstring mailto = L"mailto:" + email + L"?subject=" + UrlEncodeForMailto(subject) +
                           L"&body=" + UrlEncodeForMailto(body);

    // Windows can prevent a window launched from a background/already-idle
    // action from stealing focus, so it opens minimized or behind other
    // windows instead of visibly popping up - explicitly allowing the next
    // process to set itself as foreground avoids that for this case.
    AllowSetForegroundWindow(ASFW_ANY);

    HINSTANCE result = ShellExecuteW(nullptr, L"open", mailto.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    // Per MSDN, a return value > 32 means success; anything else is an error code.
    return (INT_PTR)result > 32;
}

void DoEmailSuppliers() {
    auto data = BuildBreakdownData(g_entries);
    if (data.empty()) {
        MessageBoxW(g_hMainWnd, L"There are no entries to email yet.", L"Email Suppliers", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::vector<std::wstring> withoutEmail;
    for (auto& sg : data) {
        auto it = g_supplierEmails.find(sg.supplier);
        if (it == g_supplierEmails.end() || it->second.empty()) withoutEmail.push_back(sg.supplier);
    }

    std::wstring msg = L"This will open " + std::to_wstring(data.size()) +
        (data.size() == 1 ? std::wstring(L" email") : std::wstring(L" emails")) +
        L" in your default email app, one supplier at a time - you'll be asked to confirm "
        L"before each one opens, so you can send or close each draft before the next appears.\n\n";
    if (!withoutEmail.empty()) {
        msg += L"These suppliers have no email address on file, so their draft will open with "
               L"the To field blank - fill it in before sending:\n";
        for (auto& s : withoutEmail) msg += L"  - " + s + L"\n";
        msg += L"\n";
    }
    msg += L"Continue?";

    if (MessageBoxW(g_hMainWnd, msg.c_str(), L"Email Suppliers", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    // Opened one at a time, with explicit confirmation between each, rather
    // than firing every ShellExecute call in a tight loop: many mail clients
    // (Outlook in particular) don't reliably handle a second externally-
    // triggered "compose" request while the first is still open - it can get
    // treated as "bring the existing window to front" instead of opening a
    // genuinely new one. Going one at a time avoids racing that entirely.
    for (size_t i = 0; i < data.size(); i++) {
        SupplierGroup& sg = data[i];
        auto it = g_supplierEmails.find(sg.supplier);
        std::wstring email = (it != g_supplierEmails.end()) ? it->second : L"";

        if (!EmailSupplier(email, sg)) {
            std::wstring failMsg = L"Windows couldn't open an email draft for " + sg.supplier + L".\n\n"
                L"This usually means there's no default email app set up on this PC yet. "
                L"Go to Windows Settings > Apps > Default apps, search for 'Mail' (or MAILTO), "
                L"and set a default (e.g. Outlook or the Windows Mail app).\n\n"
                L"Click OK to try the next supplier, or Cancel to stop here.";
            if (MessageBoxW(g_hMainWnd, failMsg.c_str(), L"Email Couldn't Open", MB_OKCANCEL | MB_ICONWARNING) == IDCANCEL)
                return;
            continue;
        }

        if (i + 1 < data.size()) {
            std::wstring nextMsg = L"Opened a draft for " + sg.supplier + L".\n\n"
                L"Once you've sent (or closed) it, click OK to open the next one for "
                + data[i + 1].supplier + L", or Cancel to stop here.";
            if (MessageBoxW(g_hMainWnd, nextMsg.c_str(), L"Email Suppliers", MB_OKCANCEL | MB_ICONINFORMATION) == IDCANCEL)
                return;
        }
    }

    MessageBoxW(g_hMainWnd, L"Done - all supplier emails have been opened.", L"Email Suppliers", MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void LayoutAll(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    int statusBarHeight = S(24);
    MoveWindow(hTab, 0, 0, rc.right, rc.bottom - statusBarHeight, TRUE);
    MoveWindow(hStatusBar, 0, rc.bottom - statusBarHeight, rc.right, statusBarHeight, TRUE);

    RECT disp = rc;
    disp.bottom -= statusBarHeight;
    TabCtrl_AdjustRect(hTab, FALSE, &disp);

    int left = disp.left + S(10);
    int top = disp.top + S(10);
    int right = disp.right - S(10);

    // ---- Tab 1 ----
    // v0.9.42: these 4 labels used to sit at top + S(3) (a fixed pixel
    // nudge, guessed to visually match a plain EDIT box's own text
    // baseline) while their paired controls sit at top. That guess didn't
    // hold for the two COMBOBOX controls (Supplier/Species) - a themed
    // dropdown's closed-box text doesn't vertically center at quite the
    // same offset a plain EDIT box does, and the mismatch is DPI/font
    // dependent, not a fixed number of pixels - Jack: "Supplier, species,
    // kg, price still misaligned." Fixed properly instead of re-guessing
    // another offset: each label now shares the exact same top AND height
    // (S(22), matching the visible closed-box height every one of these
    // controls actually renders at - NOT hCmbSupplier/hCmbProduct's own
    // S(200), which is their dropped-down list height, not their closed
    // size) as its paired control, with SS_CENTERIMAGE (set at creation)
    // doing the vertical centering from real font metrics - the same
    // mechanism Windows already uses to center the single line of text
    // inside the neighboring EDIT/COMBOBOX itself, so the two always agree
    // regardless of DPI or font, instead of two independent guesses that
    // can drift apart.
    MoveWindow(hLblSupplier, left, top, S(65), S(22), TRUE);
    MoveWindow(hCmbSupplier, left + S(70), top, S(150), S(200), TRUE);
    MoveWindow(hLblProduct, left + S(230), top, S(55), S(22), TRUE);
    MoveWindow(hCmbProduct, left + S(290), top, S(150), S(200), TRUE);
    MoveWindow(hLblKgs, left + S(450), top, S(35), S(22), TRUE);
    MoveWindow(hEditKgs, left + S(488), top, S(70), S(22), TRUE);
    MoveWindow(hLblPrice, left + S(568), top, S(45), S(22), TRUE);
    MoveWindow(hEditPrice, left + S(616), top, S(70), S(22), TRUE);
    MoveWindow(hBtnAdd, left + S(700), top - S(2), S(110), S(28), TRUE);

    int row1b = top + S(36);
    MoveWindow(hLblDate, left, row1b + S(3), S(40), S(22), TRUE);
    MoveWindow(hDtpDate, left + S(45), row1b, S(140), S(22), TRUE);
    MoveWindow(hLblNotes, left + S(200), row1b + S(3), S(45), S(22), TRUE);
    MoveWindow(hEditNotes, left + S(248), row1b, S(340), S(22), TRUE);
    MoveWindow(hBtnDuplicate, left + S(600), row1b - S(2), S(190), S(28), TRUE);
    MoveWindow(hBtnDuplicateSupSpec, left + S(800), row1b - S(2), S(220), S(28), TRUE);

    int row2 = row1b + S(36);
    MoveWindow(hBtnDelete, left, row2, S(180), S(28), TRUE);
    MoveWindow(hBtnEdit, left + S(190), row2, S(160), S(28), TRUE);
    MoveWindow(hBtnCancelEdit, left + S(360), row2, S(160), S(28), TRUE);
    MoveWindow(hLblFilter, left + S(535), row2 + S(4), S(40), S(22), TRUE);
    MoveWindow(hEditFilter, left + S(580), row2 + S(2), S(220), S(22), TRUE);

    int listTop = row2 + S(40);
    int reconHeight = S(170); // was S(135) - grown to fit the Finalize Day button row below the Difference line
    int listBottom = disp.bottom - reconHeight - S(10);
    if (listBottom < listTop + S(60)) listBottom = listTop + S(60);
    MoveWindow(hListEntries, left, listTop, right - left, listBottom - listTop, TRUE);

    int reconTop = listBottom + S(12);
    MoveWindow(hGrpRecon, left, reconTop, right - left, disp.bottom - reconTop - S(5), TRUE);
    MoveWindow(hLblDebtor, left + S(15), reconTop + S(26), S(130), S(22), TRUE);
    MoveWindow(hEditDebtor, left + S(150), reconTop + S(24), S(260), S(22), TRUE);
    // hLblCash was only S(65) wide - too narrow for its actual text ("Cash
    // amount(s):"), so the STATIC control silently word-wrapped it onto a
    // second line that its S(22)-tall box then clipped - "Cash" visible,
    // "amount(s):" cut off to a sliver, sitting lower than the Debtor label
    // beside it. Jack: "labels/text boxes are misaligned - one is higher
    // than the other." Widened to fit on one line; hEditCash shifted right
    // to match, same S(5) gap pattern as the Debtor label/box pair.
    MoveWindow(hLblCash, left + S(430), reconTop + S(26), S(140), S(22), TRUE);
    MoveWindow(hEditCash, left + S(575), reconTop + S(24), S(260), S(22), TRUE);
    MoveWindow(hLblBook, left + S(15), reconTop + S(58), S(320), S(22), TRUE);
    MoveWindow(hLblEntered, left + S(345), reconTop + S(58), S(320), S(22), TRUE);
    MoveWindow(hLblDiff, left + S(15), reconTop + S(84), S(460), S(24), TRUE);
    MoveWindow(hBtnFinalize, left + S(15), reconTop + S(114), S(220), S(30), TRUE);

    // ---- Tab 2 ----
    MoveWindow(hLblOvBook, left, top, S(300), S(22), TRUE);
    MoveWindow(hLblOvGrand, left + S(310), top, S(300), S(22), TRUE);
    MoveWindow(hLblOvDiff, left + S(620), top, S(300), S(22), TRUE);
    MoveWindow(hListOverview, left, top + S(32), right - left, disp.bottom - (top + S(32)) - S(5), TRUE);

    // ---- Tab 3 ----
    MoveWindow(hBtnPrintPreview, left, top, S(170), S(28), TRUE);
    MoveWindow(hBtnPrintBreakdown, left + S(180), top, S(190), S(28), TRUE);
    MoveWindow(hBtnEmailSuppliers, left + S(380), top, S(190), S(28), TRUE);
    int bdListTop = top + S(38);
    MoveWindow(hListBreakdown, left, bdListTop, right - left, disp.bottom - bdListTop - S(5), TRUE);

    // ---- Tab 4 - By Species ----
    MoveWindow(hListBySpecies, left, top, right - left, disp.bottom - top - S(5), TRUE);
}

void ShowTab(int idx) {
    int s1 = (idx == 0) ? SW_SHOW : SW_HIDE;
    int s2 = (idx == 1) ? SW_SHOW : SW_HIDE;
    int s3 = (idx == 2) ? SW_SHOW : SW_HIDE;
    int s4 = (idx == 3) ? SW_SHOW : SW_HIDE;
    for (HWND h : g_tab1Ctrls) ShowWindow(h, s1);
    for (HWND h : g_tab2Ctrls) ShowWindow(h, s2);
    for (HWND h : g_tab3Ctrls) ShowWindow(h, s3);
    for (HWND h : g_tab4Ctrls) ShowWindow(h, s4);
}

// Known Win32/visual-styles quirk: a themed ComboBox can fail to paint its
// border/dropdown-arrow chrome the very first time it appears on screen,
// rendering correctly only after some later event (a mouse hover, a focus
// change, etc.) happens to trigger a repaint. Every other control here is
// created and positioned the exact same way and paints fine immediately,
// so this is specific to these two combo boxes.
//
// This is called several times - once synchronously right after the window
// becomes visible (in wWinMain), then again from three separate delayed
// one-shot timers at increasing delays (see WM_TIMER) - as a belt-and-
// suspenders approach, since the exact same single synchronous call that
// fixed this in v0.9.3 was reported to have stopped reliably fixing it by
// v0.9.10, and even the v0.9.11/v0.9.12 fixes (one 50ms retry, plus
// reordering WM_CREATE) were still seen failing intermittently as of
// v0.9.41 ("doesn't always happen, but see screenshot" - Jack). The likely
// explanation is a race with something else (possibly the status bar
// control, or an internal WM_SIZE-triggered relayout) re-invalidating
// these controls after an attempt but before the theme engine has settled
// - and apparently that race isn't always won within 50ms on every
// machine, hence the two further, later retries added in v0.9.42.
//
// v0.9.42 escalated the fix itself, not just the retry count/timing: a
// plain RedrawWindow (even with RDW_FRAME) only invalidates and repaints
// pixels - it doesn't force ComCtl32's visual-styles engine to redo its
// theme-handle setup for the control. Toggling visibility (SW_HIDE then
// SW_SHOW) before redrawing was meant to force that, but v0.9.42 testing
// showed it still isn't enough, and neither was v0.9.44's SWP_FRAMECHANGED
// escalation (forcing non-client recalculation) - both still left it
// broken on every launch.
//
// v0.9.46 added a temporary message-logging subclass to actually see what
// happens, instead of guessing again, and it gave a clear, specific
// answer: a hover produces ONLY WM_MOUSEMOVE -> WM_PAINT -> WM_ERASEBKGND
// on the affected control - no WM_NCPAINT, no WM_NCCALCSIZE at all. That
// directly disproves the v0.9.44 theory (this was never a non-client/frame
// problem) and explains why v0.9.42/v0.9.44's hide-show/frame-change fixes
// never worked even though the log showed THEM also producing
// WM_PAINT/WM_NCPAINT/WM_ERASEBKGND cycles at 328ms, 344ms, 391ms, 407ms,
// and again at 1141-1157ms after launch - none of that is the mechanism
// that actually fixes it. Only a genuine WM_MOUSEMOVE reaching the control
// does. (Log evidence also removed the temporary diagnostic subclass once
// it had done its job, per the plan when it was added.)
//
// v0.9.46's fix is a synthetic WM_MOUSEMOVE - reproducing exactly the
// interaction the log evidence showed actually works, without needing the
// user's real cursor to physically be over the control. This replaces the
// SWP_FRAMECHANGED/hide-show approach entirely rather than adding to it,
// since the log evidence shows that approach doesn't contribute to the fix.
void FixComboBoxFirstPaint() {
    HWND boxes[] = { hCmbSupplier, hCmbProduct };
    for (HWND h : boxes) {
        if (!h) continue;
        RECT rc{};
        GetClientRect(h, &rc);
        LPARAM pos = MAKELPARAM(5, (rc.bottom - rc.top) / 2);
        SendMessageW(h, WM_MOUSEMOVE, 0, pos);
        InvalidateRect(h, nullptr, TRUE);
        UpdateWindow(h);
    }
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hMainWnd = hwnd;

        {
            HDC screenDC = GetDC(hwnd);
            g_dpi = GetDeviceCaps(screenDC, LOGPIXELSX);
            ReleaseDC(hwnd, screenDC);
            if (g_dpi <= 0) g_dpi = 96;
        }

        InitFonts();

        hTab = MakeControl(L"SysTabControl32", L"", WS_VISIBLE | WS_CLIPSIBLINGS, ID_TAB, hwnd);
        {
            TCITEMW tie{};
            tie.mask = TCIF_TEXT;
            tie.pszText = (LPWSTR)L"1. Data Entry";
            TabCtrl_InsertItem(hTab, 0, &tie);
            tie.pszText = (LPWSTR)L"2. Total Overview";
            TabCtrl_InsertItem(hTab, 1, &tie);
            tie.pszText = (LPWSTR)L"3. Breakdown";
            TabCtrl_InsertItem(hTab, 2, &tie);
            tie.pszText = (LPWSTR)L"4. By Species";
            TabCtrl_InsertItem(hTab, 3, &tie);
        }

        // Tab 1
        // v0.9.42: these 4 labels get SS_CENTERIMAGE (Jack: "Supplier,
        // species, kg, price still misaligned") - see the LayoutAll
        // comment by their MoveWindow calls for the full reasoning. Every
        // other label in the app keeps its original top+S(3) offset
        // unchanged for now, scoped to exactly the row Jack flagged.
        hLblSupplier = MakeControl(L"STATIC", L"Supplier:", WS_VISIBLE | SS_CENTERIMAGE, 0, hwnd);
        hCmbSupplier = MakeControl(L"COMBOBOX", L"", WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWN | WS_TABSTOP, ID_CMB_SUPPLIER, hwnd);
        hLblProduct = MakeControl(L"STATIC", L"Species:", WS_VISIBLE | SS_CENTERIMAGE, 0, hwnd);
        hCmbProduct = MakeControl(L"COMBOBOX", L"", WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWN | WS_TABSTOP, ID_CMB_PRODUCT, hwnd);
        hLblKgs = MakeControl(L"STATIC", L"Kgs:", WS_VISIBLE | SS_CENTERIMAGE, 0, hwnd);
        hEditKgs = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP, ID_EDIT_KGS, hwnd);
        hLblPrice = MakeControl(L"STATIC", L"Price:", WS_VISIBLE | SS_CENTERIMAGE, 0, hwnd);
        hEditPrice = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP, ID_EDIT_PRICE, hwnd);
        // Phase 1 completeness follow-up (2026-09-30, v0.9.50): make the
        // interactive form's own field-length limits real, enforced Win32
        // control limits (EM_LIMITTEXT/CB_LIMITTEXT) rather than an
        // accidental side effect of whatever fixed-size buffer a given
        // GetWindowTextW call happened to use - a hand-edited/loaded .fbd
        // file is now rejected at exactly these same limits (see
        // FishBalanceCore.h's kMaxSupplierSpeciesLength etc.), so the UI
        // and the file format agree on what's representable.
        // Test-automation refactor (v0.9.51): the actual EM_LIMITTEXT/
        // CB_LIMITTEXT calls now live in FishBalanceControlLimits.h's
        // ApplyFieldLengthLimits(), shared with the Windows integration test
        // suite's control-limit test (Phase 1 item 5) so both this real form
        // and the test's own throwaway controls are verified against the
        // exact same function - see the call after hEditCash is created
        // below, once every field it targets exists.
        hBtnAdd = MakeControl(L"BUTTON", L"Add Entry", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_DEFPUSHBUTTON, ID_BTN_ADD, hwnd);
        hBtnDelete = MakeControl(L"BUTTON", L"Delete Selected Row", WS_VISIBLE | WS_TABSTOP, ID_BTN_DELETE, hwnd);
        hBtnEdit = MakeControl(L"BUTTON", L"Edit Selected Row", WS_VISIBLE | WS_TABSTOP, ID_BTN_EDIT, hwnd);
        hBtnCancelEdit = MakeControl(L"BUTTON", L"Clear / Cancel Edit", WS_VISIBLE | WS_TABSTOP, ID_BTN_CANCEL_EDIT, hwnd);
        hBtnDuplicate = MakeControl(L"BUTTON", L"Duplicate Last Entry", WS_VISIBLE | WS_TABSTOP, ID_BTN_DUPLICATE, hwnd);
        hBtnDuplicateSupSpec = MakeControl(L"BUTTON", L"Duplicate Supplier && Species", WS_VISIBLE | WS_TABSTOP, ID_BTN_DUPLICATE_SUPSPEC, hwnd);

        hLblDate = MakeControl(L"STATIC", L"Date:", WS_VISIBLE, 0, hwnd);
        hDtpDate = MakeControl(DATETIMEPICK_CLASS, L"", WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATEFORMAT, ID_DTP_DATE, hwnd);
        hLblNotes = MakeControl(L"STATIC", L"Notes:", WS_VISIBLE, 0, hwnd);
        hEditNotes = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP, ID_EDIT_NOTES, hwnd);
        {
            SYSTEMTIME today;
            GetLocalTime(&today);
            DateTime_SetSystemtime(hDtpDate, GDT_VALID, &today);
        }

        hLblFilter = MakeControl(L"STATIC", L"Filter:", WS_VISIBLE, 0, hwnd);
        hEditFilter = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP, ID_EDIT_FILTER, hwnd);

        hListEntries = MakeControl(L"SysListView32", L"", WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER, ID_LIST_ENTRIES, hwnd);
        ListView_SetExtendedListViewStyle(hListEntries, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        AddColumn(hListEntries, 0, L"Supplier", 140);
        AddColumn(hListEntries, 1, L"Species", 150);
        AddColumn(hListEntries, 2, L"Kgs", 80);
        AddColumn(hListEntries, 3, L"Price ($/kg)", 100);
        AddColumn(hListEntries, 4, L"Total ($)", 100);
        AddColumn(hListEntries, 5, L"Date", 100);
        AddColumn(hListEntries, 6, L"Notes", 160);

        hGrpRecon = MakeControl(L"BUTTON", L"Book Reconciliation (checks Debtor + Cash against entries above)", WS_VISIBLE | BS_GROUPBOX, 0, hwnd);
        hLblDebtor = MakeControl(L"STATIC", L"Debtor amount(s):", WS_VISIBLE, 0, hwnd);
        hEditDebtor = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP, ID_EDIT_DEBTOR, hwnd);
        hLblCash = MakeControl(L"STATIC", L"Cash amount(s):", WS_VISIBLE, 0, hwnd);
        hEditCash = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP, ID_EDIT_CASH, hwnd);
        {
            FieldLengthLimitTargets limits;
            limits.supplierCombo = hCmbSupplier;
            limits.speciesCombo = hCmbProduct;
            limits.kgsEdit = hEditKgs;
            limits.priceEdit = hEditPrice;
            limits.notesEdit = hEditNotes;
            limits.debtorEdit = hEditDebtor;
            limits.cashEdit = hEditCash;
            ApplyFieldLengthLimits(limits);
        }
        hLblBook = MakeControl(L"STATIC", L"Book Total: $0.00", WS_VISIBLE, 0, hwnd);
        hLblEntered = MakeControl(L"STATIC", L"Entered Total: $0.00", WS_VISIBLE, 0, hwnd);
        hLblDiff = MakeControl(L"STATIC", L"Difference: $0.00", WS_VISIBLE, 0, hwnd);
        hBtnFinalize = MakeControl(L"BUTTON", L"Finalize Day", WS_VISIBLE | WS_TABSTOP, ID_BTN_FINALIZE, hwnd);

        // Tab 2
        hLblOvBook = MakeControl(L"STATIC", L"Book Total: $0.00", WS_VISIBLE, 0, hwnd);
        hLblOvGrand = MakeControl(L"STATIC", L"Grand Total: $0.00", WS_VISIBLE, 0, hwnd);
        hLblOvDiff = MakeControl(L"STATIC", L"Difference: $0.00", WS_VISIBLE, 0, hwnd);
        hListOverview = MakeControl(L"SysListView32", L"", WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER, ID_LIST_OVERVIEW, hwnd);
        ListView_SetExtendedListViewStyle(hListOverview, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        AddColumn(hListOverview, 0, L"Supplier", 220);
        AddColumn(hListOverview, 1, L"Kgs", 90);
        AddColumn(hListOverview, 2, L"Total ($)", 130);

        // Tab 3
        hBtnPrintPreview = MakeControl(L"BUTTON", L"Print Preview...", WS_VISIBLE | WS_TABSTOP, ID_BTN_PRINT_PREVIEW, hwnd);
        hBtnPrintBreakdown = MakeControl(L"BUTTON", L"Print / Save as PDF...", WS_VISIBLE | WS_TABSTOP, ID_BTN_PRINT_BREAKDOWN, hwnd);
        hBtnEmailSuppliers = MakeControl(L"BUTTON", L"Email All Suppliers...", WS_VISIBLE | WS_TABSTOP, ID_BTN_EMAIL_SUPPLIERS, hwnd);
        hListBreakdown = MakeControl(L"SysListView32", L"", WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER, ID_LIST_BREAKDOWN, hwnd);
        SendMessageW(hListBreakdown, LVM_ENABLEGROUPVIEW, TRUE, 0);
        ListView_SetExtendedListViewStyle(hListBreakdown, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        AddColumn(hListBreakdown, 0, L"Species", 240);
        AddColumn(hListBreakdown, 1, L"Price ($/kg)", 110);
        AddColumn(hListBreakdown, 2, L"Kgs (weight)", 100);
        AddColumn(hListBreakdown, 3, L"Total ($)", 120);

        // Tab 4 - By Species
        hListBySpecies = MakeControl(L"SysListView32", L"", WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER, ID_LIST_BYSPECIES, hwnd);
        ListView_SetExtendedListViewStyle(hListBySpecies, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        AddColumn(hListBySpecies, 0, L"Species", 200);
        AddColumn(hListBySpecies, 1, L"Kgs", 90);
        AddColumn(hListBySpecies, 2, L"Total ($)", 100);
        AddColumn(hListBySpecies, 3, L"Avg Price", 100);
        AddColumn(hListBySpecies, 4, L"Highest Price", 110);
        AddColumn(hListBySpecies, 5, L"Lowest Price", 110);

        g_tab1Ctrls = { hLblSupplier, hCmbSupplier, hLblProduct, hCmbProduct, hLblKgs, hEditKgs,
                         hLblPrice, hEditPrice, hBtnAdd, hBtnDelete, hBtnEdit, hBtnCancelEdit, hBtnDuplicate,
                         hBtnDuplicateSupSpec,
                         hLblDate, hDtpDate, hLblNotes, hEditNotes,
                         hLblFilter, hEditFilter, hListEntries, hGrpRecon,
                         hLblDebtor, hEditDebtor, hLblCash, hEditCash, hLblBook, hLblEntered, hLblDiff,
                         hBtnFinalize };
        g_tab2Ctrls = { hLblOvBook, hLblOvGrand, hLblOvDiff, hListOverview };
        g_tab3Ctrls = { hBtnPrintPreview, hBtnPrintBreakdown, hBtnEmailSuppliers, hListBreakdown };
        g_tab4Ctrls = { hListBySpecies };

        // Status bar: always visible regardless of the active tab (not
        // added to any g_tabNCtrls array, same as hTab itself) - shows the
        // running app version and the currently loaded file, kept in sync
        // with the title bar via UpdateTitle(). Deliberately created LAST,
        // after every other control, not right after the tab strip where
        // it originally sat in v0.9.8 - a regression investigation traced
        // the Supplier/Species combo box first-paint bug's reappearance to
        // this control's creation order (it's a comctl32 class being
        // instantiated for the first time in the process, immediately
        // before the combo boxes, right where they used to be among the
        // very first controls created back in v0.9.3 when the original fix
        // was confirmed working). Creating it last restores that original
        // relative order.
        hStatusBar = MakeControl(STATUSCLASSNAMEW, L"", WS_VISIBLE | SBARS_SIZEGRIP, 0, hwnd);
        {
            int parts[2] = { S(150), -1 }; // part 0: version, fixed width; part 1: filename, extends to the right edge
            SendMessageW(hStatusBar, SB_SETPARTS, 2, (LPARAM)parts);
        }

        LayoutAll(hwnd);
        ShowTab(0);

        // Load persisted window settings/recent files (the window itself is
        // already sized from these via CreateWindowExW in wWinMain).
        LoadRecentFiles();
        RebuildRecentMenu();
        LoadSupplierEmails();
        if (g_hEditMenu) EnableMenuItem(g_hEditMenu, ID_EDIT_UNDO_DELETE, MF_BYCOMMAND | MF_GRAYED);

        // Silently reload whatever was last auto-saved next to the exe. If
        // the file exists but LoadFromFile rejects it (corrupted, or the
        // last save was interrupted partway through), do NOT let the
        // RefreshAll() below silently overwrite it with a blank document -
        // warn the user and skip this session's initial autosave write
        // instead, so the unreadable file is left exactly as it was for a
        // chance at manual recovery.
        bool autosaveLoadFailed = false;
        {
            std::wstring autosavePath = AutosavePath();
            DWORD attrs = GetFileAttributesW(autosavePath.c_str());
            bool autosaveExists = (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
            std::wstring loadErr;
            if (autosaveExists && !LoadFromFile(autosavePath, &loadErr)) {
                autosaveLoadFailed = true;
                MessageBoxW(hwnd,
                    (L"The autosave file next to this program (autosave.fbd) could not be read: " +
                     loadErr +
                     L".\n\nTo avoid losing that data, it has NOT been overwritten. This session is "
                     L"starting with a blank sheet instead. The unreadable autosave.fbd is still "
                     L"in this program's folder - make a copy of it before doing anything else if "
                     L"you need help recovering the data in it.").c_str(),
                    L"Autosave Could Not Be Loaded", MB_OK | MB_ICONWARNING);
            }
            // A missing autosave.fbd (first run, or it was deliberately
            // deleted) is normal and not warned about - LoadFromFile simply
            // isn't called in that case, leaving g_entries at its default
            // empty state.
        }
        // v0.9.30: LoadFromFile's SetWindowTextW(hEditDebtor/hEditCash,...)
        // calls above trigger EN_CHANGE, which would otherwise leave this
        // fresh session starting as "dirty" for data that just came off
        // disk. Explicitly clean here regardless of which branch ran -
        // a successful reload matches disk, a failed one left a blank
        // sheet with nothing entered yet either way.
        g_dirty = false;
        // Restore the association with the last named file (if any) so
        // Save/title bar refer to it, without overwriting the freshly
        // reloaded autosave content. Skipped when the autosave failed to
        // load, so a later Save can't overwrite a real named file with the
        // blank sheet this session is starting with instead.
        //
        // Phase 1 / F1 audit remediation (2026-09-30): settings.txt's
        // LASTFILE is only written on a clean exit (SaveSettings, called
        // from WM_DESTROY) - after a crash or a forced close, it still holds
        // whatever it was at the PREVIOUS clean exit, while autosave.fbd has
        // gone on being rewritten by every edit since (possibly through
        // several New/Open/Finalize operations). Blindly trusting LASTFILE
        // here used to associate that unrelated recovered content with a
        // named file it might not correspond to at all - and the next Save
        // (explicit, or the very next autosave tick) would silently
        // overwrite that named file on disk with the mismatched content.
        // Now only re-associate when the recovered autosave's own
        // SOURCE_FILE= marker actually agrees with LASTFILE; otherwise treat
        // it as recovered-but-unsaved and require an explicit Save As,
        // exactly like an autosave written before this field existed (no
        // SOURCE_FILE= at all).
        bool identityMatches = !autosaveLoadFailed && g_lastLoadHasSourceFile &&
            _wcsicmp(g_lastLoadSourceFile.c_str(), g_settings.lastFile.c_str()) == 0;
        if (!autosaveLoadFailed && !g_settings.lastFile.empty()) {
            if (identityMatches) {
                g_currentFile = g_settings.lastFile;
            } else {
                // g_currentFile stays empty ("(unsaved)" in the title bar) -
                // the recovered data is still fully present in g_entries and
                // will be autosaved again below, just not silently tied to a
                // named file it may not actually match. A Save/Save As from
                // here writes a fresh file rather than overwriting the old
                // one with content that may not belong to it.
                MessageBoxW(hwnd,
                    (!g_lastLoadHasSourceFile
                        ? L"Recovered your last unsaved work from autosave.fbd. It isn't linked to "
                          L"a named file (this autosave predates that tracking), so use File > Save "
                          L"As to save it somewhere if you want to keep it."
                        : L"Recovered your last unsaved work from autosave.fbd. It doesn't appear "
                          L"to match \"" + g_settings.lastFile + L"\" (the last file you had open), "
                          L"so it hasn't been re-linked to that file automatically - use File > Save "
                          L"As to save it somewhere if you want to keep it, so nothing is "
                          L"accidentally overwritten.").c_str(),
                    L"Recovered Unsaved Work", MB_OK | MB_ICONINFORMATION);
            }
        }
        RefreshAll(!autosaveLoadFailed);
        UpdateTitle();

        // Delayed retries at the combo box first-paint fix (see
        // FixComboBoxFirstPaint's comment) - fire at increasing delays
        // after the window is fully set up, catching any late
        // re-invalidation the synchronous attempt in wWinMain might miss.
        // v0.9.42 added the 250ms/1000ms retries after the original 50ms
        // one-shot alone was still seen to miss intermittently.
        SetTimer(hwnd, ID_TIMER_FIRST_PAINT_FIX, 50, nullptr);
        SetTimer(hwnd, ID_TIMER_FIRST_PAINT_FIX2, 250, nullptr);
        SetTimer(hwnd, ID_TIMER_FIRST_PAINT_FIX3, 1000, nullptr);

        // v0.9.29 fix: MaybeBackupOnTimer() used to only ever run inside
        // AutosaveNow(), which itself only fires on focus-loss/explicit
        // save - so a screen left idle after one edit (cursor still in a
        // field, no further tabbing/clicking) never got backed up at all,
        // no matter how much real time passed. This recurring tick calls
        // MaybeBackupOnTimer() directly on a real clock instead, so the
        // ~3-minute rolling backup actually happens in the background.
        // 30s is just the polling granularity, not the backup interval -
        // MaybeBackupOnTimer()'s own kBackupIntervalMs/no-change checks
        // still decide whether anything actually gets written each tick.
        SetTimer(hwnd, ID_TIMER_BACKUP_CHECK, 30000, nullptr);
        return 0;
    }

    case WM_SIZE:
        LayoutAll(hwnd);
        return 0;

    case WM_CTLCOLORSTATIC: {
        HWND hCtl = (HWND)lParam;
        if (hCtl == hLblDiff || hCtl == hLblOvDiff) {
            HDC hdc = (HDC)wParam;
            bool ok = (hCtl == hLblDiff) ? g_diffOk : g_ovDiffOk;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, ok ? RGB(0, 128, 0) : RGB(200, 0, 0));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;
    }

    // IsDialogMessage (used in the message loop for Tab/Enter navigation)
    // asks the window which control is the "default" push button by
    // sending DM_GETDEFID. That message is normally answered automatically
    // for real dialog boxes (created via CreateDialog/DialogBox), but this
    // is a plain window, so without handling it ourselves IsDialogMessage
    // has no reliable way to find Add Entry - it can end up mis-resolving
    // Enter to an unrelated command instead (e.g. a menu item) rather than
    // just doing nothing. Answering it explicitly makes Enter deterministic.
    case DM_GETDEFID:
        if (hBtnAdd && IsWindowVisible(hBtnAdd) && IsWindowEnabled(hBtnAdd))
            return MAKELONG(ID_BTN_ADD, DC_HASDEFID);
        return 0;

    case WM_NOTIFY: {
        LPNMHDR hdr = (LPNMHDR)lParam;
        if (hdr->hwndFrom == hTab && hdr->code == TCN_SELCHANGE) {
            ShowTab(TabCtrl_GetCurSel(hTab));
            return 0;
        }
        if (hdr->hwndFrom == hDtpDate && hdr->code == (UINT)DTN_DATETIMECHANGE) {
            AutosaveNow(); // persist the in-progress draft - see SaveToFile's DRAFT_* fields
            return 0;
        }
        if (hdr->hwndFrom == hListEntries && hdr->code == (UINT)NM_DBLCLK) {
            LPNMITEMACTIVATE nia = (LPNMITEMACTIVATE)lParam;
            if (nia->iItem >= 0 && nia->iItem < (int)g_filteredIndices.size())
                ReviewOrEditEntry(g_filteredIndices[nia->iItem]);
            return 0;
        }
        if (hdr->hwndFrom == hListEntries && hdr->code == (UINT)LVN_COLUMNCLICK) {
            LPNMLISTVIEW nmlv = (LPNMLISTVIEW)lParam;
            SortEntriesBy(nmlv->iSubItem);
            return 0;
        }
        if (hdr->code == (UINT)NM_CUSTOMDRAW && hdr->hwndFrom == hListEntries) {
            LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lParam;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                int row = (int)cd->nmcd.dwItemSpec;
                bool flagged = row >= 0 && row < (int)g_filteredIndices.size() &&
                               g_entries[g_filteredIndices[row]].priceFlagged;
                if (flagged) {
                    // ROADMAP.md item 7 - same red used for the Debtor/Cash
                    // "out of balance" text color elsewhere in this app,
                    // paired with a light tint behind it for the whole row.
                    cd->clrTextBk = RGB(255, 235, 235);
                    cd->clrText = RGB(200, 0, 0);
                }
                return CDRF_DODEFAULT;
            }
        }
        if (hdr->code == (UINT)NM_CUSTOMDRAW &&
            (hdr->hwndFrom == hListOverview || hdr->hwndFrom == hListBreakdown || hdr->hwndFrom == hListBySpecies)) {
            LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lParam;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                wchar_t txt[256] = { 0 };
                ListView_GetItemText(hdr->hwndFrom, (int)cd->nmcd.dwItemSpec, 0, txt, 256);
                bool bold = wcsstr(txt, L"Total") != nullptr;
                SelectObject(cd->nmcd.hdc, bold ? g_boldFont : g_normalFont);
                return CDRF_NEWFONT;
            }
        }
        break;
    }

    // Right-click "Clear flag" (ROADMAP.md item 7) - only shown for a row
    // that's actually flagged; a plain row gets no context menu at all.
    case WM_CONTEXTMENU: {
        if ((HWND)wParam == hListEntries) {
            POINT pt;
            pt.x = (short)LOWORD(lParam);
            pt.y = (short)HIWORD(lParam);
            int row;
            if (pt.x == -1 && pt.y == -1) {
                // Invoked via keyboard (Shift+F10 / context menu key), not
                // a real click position - fall back to the selected row.
                row = ListView_GetNextItem(hListEntries, -1, LVNI_SELECTED);
                if (row >= 0) {
                    RECT rc;
                    ListView_GetItemRect(hListEntries, row, &rc, LVIR_BOUNDS);
                    pt.x = rc.left;
                    pt.y = rc.top;
                    ClientToScreen(hListEntries, &pt);
                }
            } else {
                POINT clientPt = pt;
                ScreenToClient(hListEntries, &clientPt);
                LVHITTESTINFO ht{};
                ht.pt = clientPt;
                row = ListView_HitTest(hListEntries, &ht);
            }
            if (row >= 0 && row < (int)g_filteredIndices.size() &&
                g_entries[g_filteredIndices[row]].priceFlagged) {
                ListView_SetItemState(hListEntries, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                HMENU hCtx = CreatePopupMenu();
                AppendMenuW(hCtx, MF_STRING, ID_ENTRY_CLEAR_FLAG, L"Clear flag");
                TrackPopupMenu(hCtx, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hMainWnd, nullptr);
                DestroyMenu(hCtx);
            }
            return 0;
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id >= ID_RECENT_BASE && id < (int)(ID_RECENT_BASE + kMaxRecentFiles)) {
            size_t idx = (size_t)(id - ID_RECENT_BASE);
            if (idx < g_recentFiles.size()) {
                std::wstring path = g_recentFiles[idx];
                if (!ConfirmDiscardCurrentData(L"open that file")) return 0;
                // v0.9.28: same reasoning as DoFileOpen - snapshot what's
                // about to be discarded before Recent Files overwrites it.
                WriteBackupSnapshot();
                std::wstring loadErr;
                if (LoadFromFile(path, &loadErr)) {
                    g_currentFile = path;
                    CancelEdit();
                    ClearUndoState();
                    g_dirty = false; // v0.9.30 - freshly loaded, matches disk
                    UpdateTitle();
                    RefreshAll();
                    RememberRecentFile(path); // move to front
                } else {
                    MessageBoxW(g_hMainWnd,
                        (L"Could not open that file: " + loadErr +
                         L".\n\nNothing has been changed.").c_str(),
                        L"Error", MB_OK | MB_ICONERROR);
                }
            }
            return 0;
        }

        switch (id) {
        case ID_BTN_ADD: CommitEntryForm(); return 0;
        case ID_BTN_DELETE: DeleteSelectedEntry(); return 0;
        case ID_BTN_EDIT: EditSelectedEntry(); return 0;
        case ID_BTN_CANCEL_EDIT: CancelEdit(); return 0;
        case ID_BTN_DUPLICATE: DuplicateLastEntry(); return 0;
        case ID_BTN_DUPLICATE_SUPSPEC: DuplicateSupplierSpecies(); return 0;
        case ID_BTN_FINALIZE:
            if (g_finalizedDate.empty()) DoFinalizeDay(); else DoUnfinalizeDay();
            return 0;
        case ID_EDIT_FILTER:
            if (code == EN_CHANGE) RefreshEntriesList();
            return 0;
        // Supplier/Species/Kgs/Price/Notes below all autosave the
        // in-progress draft on losing focus, not on every keystroke - see
        // the Debtor/Cash EN_KILLFOCUS case below for the full reasoning
        // (disk I/O frequency, not string-building cost, is the real
        // driver of the write cost this avoids).
        case ID_CMB_SUPPLIER:
            if (code == CBN_EDITCHANGE) ComboAutoComplete(hCmbSupplier, g_prevSupplierLen);
            if (code == CBN_KILLFOCUS) AutosaveNow(); // persist the in-progress draft - see SaveToFile's DRAFT_* fields
            return 0;
        case ID_CMB_PRODUCT:
            if (code == CBN_EDITCHANGE) ComboAutoComplete(hCmbProduct, g_prevProductLen);
            if (code == CBN_KILLFOCUS) AutosaveNow();
            return 0;
        case ID_EDIT_KGS:
        case ID_EDIT_PRICE:
        case ID_EDIT_NOTES:
            if (code == EN_KILLFOCUS) AutosaveNow(); // persist the in-progress draft - see SaveToFile's DRAFT_* fields
            return 0;
        case ID_EDIT_DEBTOR:
        case ID_EDIT_CASH:
            if (code == EN_CHANGE) {
                g_dirty = true; // v0.9.30
                RecalcTotals();
            }
            if (code == EN_KILLFOCUS) {
                // Autosave on losing focus (finished typing this field,
                // moved to the next), not on every keystroke.
                //
                // History, since this has changed twice: originally these
                // two fields weren't autosaved at all until some other
                // action triggered a refresh (a crash after editing only
                // Debtor/Cash could lose the change indefinitely). Fixed
                // first with a debounced SetTimer/WM_TIMER - that couldn't
                // be confirmed reliably firing (reported broken even with
                // a real built .exe, not a debugger-restart artifact), so
                // it was replaced with a plain synchronous save on every
                // keystroke instead. That worked correctly, but became a
                // real performance problem once Debtor/Cash and the
                // v0.9.14 draft fields were ALL wired the same way - every
                // keystroke anywhere in the form triggered a full atomic
                // rewrite of the entire day's file, dominated by disk I/O
                // (specifically the flush-to-stable-storage step), not by
                // how cheaply the content was assembled. Reducing WRITE
                // FREQUENCY is the fix with real leverage here, and
                // "on focus loss" is a natural, deterministic checkpoint
                // that needs no timer at all - unlike the debounce
                // attempt, there's no "did it fire" reliability question,
                // since EN_KILLFOCUS is a direct, synchronous
                // notification. Trade-off, stated plainly: a crash while a
                // field still has focus can now lose that field's most
                // recent keystrokes since the last focus change - a small,
                // bounded loss, not the whole draft or day, and a
                // reasonable price for a meaningful reduction in disk
                // writes at real business volume (500-1000 entries/day).
                AutosaveNow();
            }
            return 0;
        case ID_FILE_NEW: DoFileNew(); return 0;
        case ID_FILE_OPEN: DoFileOpen(); return 0;
        case ID_FILE_SAVE: DoFileSave(); return 0;
        case ID_FILE_SAVEAS: DoFileSaveAs(); return 0;
        case ID_FILE_RESTORE_BACKUP: DoRestoreFromBackup(); return 0;
        case ID_ENTRY_CLEAR_FLAG: ClearSelectedEntryFlag(); return 0;
        case ID_FILE_EXPORT_CSV: DoExportCsv(); return 0;
        case ID_FILE_PRINT:
        case ID_BTN_PRINT_BREAKDOWN: PrintBreakdownReport(hwnd); return 0;
        case ID_FILE_PRINT_PREVIEW:
        case ID_BTN_PRINT_PREVIEW: OpenPrintPreview(hwnd); return 0;
        case ID_FILE_EMAIL_SUPPLIERS:
        case ID_BTN_EMAIL_SUPPLIERS: DoEmailSuppliers(); return 0;
        case ID_EDIT_UNDO_DELETE: UndoDelete(); return 0;
        case ID_TOOLS_MANAGE_NAMES: OpenManageNamesWindow(); return 0;
        case ID_FILE_EXIT: DestroyWindow(hwnd); return 0;
        case ID_FILE_ABOUT: DoAbout(); return 0;
        }
        break;
    }

    case WM_TIMER: {
        if (wParam == ID_TIMER_FIRST_PAINT_FIX) {
            KillTimer(hwnd, ID_TIMER_FIRST_PAINT_FIX);
            FixComboBoxFirstPaint();
        } else if (wParam == ID_TIMER_FIRST_PAINT_FIX2) {
            KillTimer(hwnd, ID_TIMER_FIRST_PAINT_FIX2);
            FixComboBoxFirstPaint();
        } else if (wParam == ID_TIMER_FIRST_PAINT_FIX3) {
            KillTimer(hwnd, ID_TIMER_FIRST_PAINT_FIX3);
            FixComboBoxFirstPaint();
        } else if (wParam == ID_TIMER_BACKUP_CHECK) {
            MaybeBackupOnTimer();
        }
        return 0;
    }

    case WM_DESTROY: {
        KillTimer(hwnd, ID_TIMER_FIRST_PAINT_FIX);
        KillTimer(hwnd, ID_TIMER_FIRST_PAINT_FIX2);
        KillTimer(hwnd, ID_TIMER_FIRST_PAINT_FIX3);
        KillTimer(hwnd, ID_TIMER_BACKUP_CHECK);
        SaveSettings();
        bool ok = SaveToFile(AutosavePath());
        if (!ok && !g_autosaveFailWarned) {
            // Last chance to warn before the process actually exits and
            // whatever's only in memory is gone for good - worth a pause
            // here even though the app is mid-shutdown.
            MessageBoxW(hwnd,
                L"Warning: the automatic backup (autosave.fbd) could not be saved while closing - "
                L"the disk may be full, or the file is locked by another program.\n\n"
                L"The app is about to close. If you have changes you're not sure were saved, check "
                L"File > Save was used recently before closing again next time.",
                L"Autosave Failed", MB_OK | MB_ICONWARNING);
        }
        if (g_normalFont) DeleteObject(g_normalFont);
        if (g_boldFont) DeleteObject(g_boldFont);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

// Test-automation refactor (2026-09-30, v0.9.51): menu creation, window-class
// registration, and CreateWindowExW/ShowWindow/UpdateWindow, extracted out of
// wWinMain so the headless end-to-end smoke test (Phase 1 item 6) can create
// the REAL main window (triggering the real WM_CREATE control-creation and
// startup autosave-recovery logic) hidden/off-screen, without needing a
// message loop - SendMessageW/GetWindowTextW/SetWindowTextW all work
// synchronously against a window on the same thread regardless of whether
// anything is pumping its message queue. wWinMain's own remaining body
// (DPI/settings init before this call, the message loop after it) stays
// inline below, guarded by #ifndef FBM_BUILDING_TESTS, since a test binary
// supplies its own doctest main() and never needs a message loop for direct
// function/control testing ("do not introduce UI automation dependencies if
// direct function/control testing is sufficient"). Idempotent class
// registration (classRegistered guard) since a test binary may call this
// more than once across separate TEST_CASEs.
HWND CreateFishBalanceMainWindow(HINSTANCE hInstance, int nCmdShow) {
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"FishBalanceMainWindow";
        wc.hIcon = LoadAppIcon(hInstance, 32);
        wc.hIconSm = LoadAppIcon(hInstance, 16);
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    HMENU hMenu = CreateMenu();
    HMENU hFileMenu = CreatePopupMenu();
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_NEW, L"&New");
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_OPEN, L"&Open...");
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_SAVE, L"&Save");
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_SAVEAS, L"Save &As...");
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_RESTORE_BACKUP, L"Restore from &Backup...");
    g_hRecentMenu = CreatePopupMenu();
    AppendMenuW(g_hRecentMenu, MF_STRING | MF_GRAYED, 0, L"(none yet)");
    AppendMenuW(hFileMenu, MF_POPUP, (UINT_PTR)g_hRecentMenu, L"Recent Files");
    AppendMenuW(hFileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_EXPORT_CSV, L"&Export to CSV...");
    AppendMenuW(hFileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_PRINT_PREVIEW, L"Print Pre&view...");
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_PRINT, L"&Print Breakdown...");
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_EMAIL_SUPPLIERS, L"Email All &Suppliers...");
    AppendMenuW(hFileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_EXIT, L"E&xit");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, L"&File");

    g_hEditMenu = CreatePopupMenu();
    AppendMenuW(g_hEditMenu, MF_STRING | MF_GRAYED, ID_EDIT_UNDO_DELETE, L"&Undo Delete");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)g_hEditMenu, L"&Edit");

    HMENU hToolsMenu = CreatePopupMenu();
    AppendMenuW(hToolsMenu, MF_STRING, ID_TOOLS_MANAGE_NAMES, L"&Manage Supplier / Species Names...");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hToolsMenu, L"&Tools");

    HMENU hHelpMenu = CreatePopupMenu();
    AppendMenuW(hHelpMenu, MF_STRING, ID_FILE_ABOUT, L"&About");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hHelpMenu, L"&Help");

    HWND hwnd = CreateWindowExW(0, L"FishBalanceMainWindow", L"Fish Balance Manager",
        WS_OVERLAPPEDWINDOW, g_settings.x, g_settings.y, g_settings.w, g_settings.h,
        nullptr, hMenu, hInstance, nullptr);

    ShowWindow(hwnd, g_settings.maximized ? SW_SHOWMAXIMIZED : nCmdShow);
    UpdateWindow(hwnd);

    // First attempt at the combo box first-paint fix - see
    // FixComboBoxFirstPaint()'s comment for the full explanation and why
    // there's also a delayed second attempt via a timer.
    FixComboBoxFirstPaint();

    return hwnd;
}

#ifndef FBM_BUILDING_TESTS
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_DATE_CLASSES };
    InitCommonControlsEx(&icc);

    {
        HDC screenDC = GetDC(nullptr);
        g_dpi = GetDeviceCaps(screenDC, LOGPIXELSX);
        ReleaseDC(nullptr, screenDC);
        if (g_dpi <= 0) g_dpi = 96;
    }

    LoadSettings(); // window size/position + last-file, read before creating the window

    HWND hwnd = CreateFishBalanceMainWindow(hInstance, nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        // Handle Enter directly for the entry-form fields, rather than
        // relying solely on IsDialogMessage's default-button resolution
        // (DM_GETDEFID) - guarantees "type, Tab, Tab, Tab, Enter" always
        // submits the row, regardless of any dialog-manager edge cases.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            HWND focused = GetFocus();
            bool inSupplier = (focused == hCmbSupplier || IsChild(hCmbSupplier, focused));
            bool inProduct = (focused == hCmbProduct || IsChild(hCmbProduct, focused));
            bool dropdownOpen = (inSupplier && SendMessageW(hCmbSupplier, CB_GETDROPPEDSTATE, 0, 0)) ||
                                 (inProduct && SendMessageW(hCmbProduct, CB_GETDROPPEDSTATE, 0, 0));
            bool inEntryForm = inSupplier || inProduct || focused == hEditKgs || focused == hEditPrice || focused == hEditNotes;
            if (inEntryForm && !dropdownOpen) {
                CommitEntryForm();
                continue;
            }
        }

        // IsDialogMessage gives this plain window dialog-style keyboard
        // handling: Tab / Shift+Tab move between fields, and Enter clicks
        // the default button (Add Entry) - this is what makes fast batch
        // entry (tab, tab, tab, Enter, repeat) work.
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
#endif // FBM_BUILDING_TESTS

// ---------------------------------------------------------------------------
// Windows integration tests built directly into main.cpp (2026-09-30, v0.9.51)
//
// g_entries, g_hMainWnd, and most of this file's other application state are
// declared `static` at file scope - internal linkage, unreachable via
// `extern` from a separate translation unit. Rather than build a fragile,
// hand-maintained extern-declaration header just to reach them from outside,
// the two Phase 1 items that genuinely need this file's own globals and real
// window-creation code (item 5's control-limit test and item 6's headless
// end-to-end smoke test) live directly here, compiled straight into the
// Windows integration test executable alongside main.cpp itself (see
// tests/win32_integration/run_integration_tests.ps1, which compiles this
// file with -DFBM_BUILDING_TESTS). That define also guards wWinMain above -
// with it defined, this file supplies no WinMain, so it links cleanly next
// to the doctest-generated main() in tests/win32_integration/
// test_integration_main.cpp without a duplicate-entry-point error.
//
// The other Phase 1 integration areas (item 3's file-loading tests, item 4's
// Save/Save As fault injection) don't need any of this file's globals or
// window code at all - they exercise FishBalanceWin32IO.h directly - so they
// live in their own separate tests/win32_integration/test_io_integration.cpp
// and tests/win32_integration/test_save_fault_injection.cpp files instead,
// compiled into the same executable.
// ---------------------------------------------------------------------------
#ifdef FBM_BUILDING_TESTS

#include "tests/doctest_setup.h"
#include "tests/doctest.h"

namespace {

// A hidden top-level popup window, never shown (WS_VISIBLE is deliberately
// omitted), used as the parent for the throwaway EDIT/COMBOBOX controls in
// the control-limit test below - real Win32 controls, created for real, but
// nothing ever paints on screen even if this test executable is run on an
// interactive desktop. One class registration shared across every TEST_CASE
// in this translation unit.
HWND MakeHiddenTestHostWindow() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"FbmIntegrationTestHostWindow";
        RegisterClassExW(&wc);
        registered = true;
    }
    return CreateWindowExW(0, L"FbmIntegrationTestHostWindow", L"", WS_POPUP,
                            0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

// Simulates the user typing `charsToType` identical characters into `hEdit`
// (a plain EDIT control, or the editable child of a COMBOBOX) by sending raw
// WM_CHAR messages one at a time - the same message an EDIT control's window
// procedure sees for real keystrokes, and the level at which EM_LIMITTEXT/
// CB_LIMITTEXT actually enforce their cap (unlike WM_SETTEXT, which is
// documented to bypass the limit entirely - so WM_SETTEXT alone could never
// prove enforcement here). Returns the control's resulting text length, for
// the caller to compare against the configured limit.
int TypeCharsAndGetResultingLength(HWND hEdit, int charsToType) {
    for (int i = 0; i < charsToType; i++) {
        SendMessageW(hEdit, WM_CHAR, (WPARAM)L'A', 1);
    }
    return GetWindowTextLengthW(hEdit);
}

// Best-effort recursive delete of a directory tree - used only to clean up
// TempTestDir below. Never called on anything outside a path this same test
// process just created under the system temp folder.
void RemoveDirectoryTreeRecursive(const std::wstring& dir) {
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            std::wstring child = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveDirectoryTreeRecursive(child);
            } else {
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

// A fresh, empty, uniquely-named directory under the system temp path, used
// as GetExeDir()'s test override (see SetExeDirOverrideForTests) so every
// path this app builds - autosave.fbd, settings.txt, recent.txt, emails.txt,
// backups\, history\ - is redirected into complete isolation from this
// machine's real application folder and real user data for the lifetime of
// one TEST_CASE. Recursively removed when the guard goes out of scope.
struct TempTestDir {
    std::wstring path;
    TempTestDir() {
        wchar_t base[MAX_PATH];
        GetTempPathW(MAX_PATH, base);
        wchar_t unique[MAX_PATH];
        // GetTempFileNameW's real job here is just generating a collision-free
        // name - it creates an empty file to reserve it, which we immediately
        // replace with a same-named directory.
        GetTempFileNameW(base, L"fbm", 0, unique);
        DeleteFileW(unique);
        CreateDirectoryW(unique, nullptr);
        path = unique;
    }
    ~TempTestDir() {
        RemoveDirectoryTreeRecursive(path);
    }
};

// Resets every piece of mutable application state the tests below touch
// back to a fresh-launch baseline. Necessary because g_entries and friends
// are process-wide globals shared across every TEST_CASE in this
// executable, not per-test state doctest can isolate on its own.
void ResetApplicationStateForTest() {
    g_entries.clear();
    g_currentFile.clear();
    g_finalizedDate.clear();
    g_dirty = false;
    g_editIndex = -1;
    g_hasUndo = false;
    g_lastLoadSourceFile.clear();
    g_lastLoadHasSourceFile = false;
    g_autosaveFailWarned = false;
    g_backupFailWarned = false;
    g_lastBackupTick = 0;
    g_lastBackupContent.clear();
    g_settings = AppSettings{};
    g_recentFiles.clear();
    g_supplierEmails.clear();
    g_filteredIndices.clear();
    g_sortColumn = -1;
    g_sortAscending = true;
}

} // namespace

TEST_SUITE("Windows integration - control length limits (Phase 1 item 5)") {

TEST_CASE("EM_LIMITTEXT enforced on Kgs/Price/Notes/Debtor/Cash edit controls") {
    HWND host = MakeHiddenTestHostWindow();
    REQUIRE(host != nullptr);
    // ES_AUTOHSCROLL: per MSDN, a single-line EDIT control WITHOUT this
    // style rejects (does not insert) any typed character once the text
    // no longer fits within the control's own visible width - a separate
    // mechanism from EM_LIMITTEXT entirely, and the one this test actually
    // needs to get out of the way to observe EM_LIMITTEXT's own limit
    // (confirmed against a real Windows build: without it, these 50px-wide
    // throwaway controls silently capped typed input at ~6 characters, far
    // below any of the real EM_LIMITTEXT values being tested here).
    HWND hKgs = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 50, 20, host, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND hPrice = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 50, 20, host, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND hNotes = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 50, 20, host, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND hDebtor = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 50, 20, host, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND hCash = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 50, 20, host, nullptr, GetModuleHandleW(nullptr), nullptr);
    REQUIRE((hKgs && hPrice && hNotes && hDebtor && hCash));

    FieldLengthLimitTargets limits;
    limits.kgsEdit = hKgs;
    limits.priceEdit = hPrice;
    limits.notesEdit = hNotes;
    limits.debtorEdit = hDebtor;
    limits.cashEdit = hCash;
    ApplyFieldLengthLimits(limits);

    CHECK(TypeCharsAndGetResultingLength(hKgs, (int)kMaxDraftKgsPriceTextLength + 20) == (int)kMaxDraftKgsPriceTextLength);
    CHECK(TypeCharsAndGetResultingLength(hPrice, (int)kMaxDraftKgsPriceTextLength + 20) == (int)kMaxDraftKgsPriceTextLength);
    CHECK(TypeCharsAndGetResultingLength(hNotes, (int)kMaxNotesLength + 20) == (int)kMaxNotesLength);
    CHECK(TypeCharsAndGetResultingLength(hDebtor, (int)kMaxDebtorCashExprLength + 20) == (int)kMaxDebtorCashExprLength);
    CHECK(TypeCharsAndGetResultingLength(hCash, (int)kMaxDebtorCashExprLength + 20) == (int)kMaxDebtorCashExprLength);

    DestroyWindow(host);
}

TEST_CASE("CB_LIMITTEXT enforced on Supplier/Species combo box edit portions") {
    HWND host = MakeHiddenTestHostWindow();
    REQUIRE(host != nullptr);
    // CBS_AUTOHSCROLL is the combo box's own equivalent of ES_AUTOHSCROLL
    // (see the comment on the previous TEST_CASE) - without it, the same
    // visible-width typing cap applies to the combo's embedded edit
    // control, for the same reason.
    HWND hSupplier = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWN | CBS_AUTOHSCROLL,
                                      0, 0, 100, 200, host, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND hSpecies = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWN | CBS_AUTOHSCROLL,
                                     0, 0, 100, 200, host, nullptr, GetModuleHandleW(nullptr), nullptr);
    REQUIRE((hSupplier && hSpecies));

    FieldLengthLimitTargets limits;
    limits.supplierCombo = hSupplier;
    limits.speciesCombo = hSpecies;
    ApplyFieldLengthLimits(limits);

    COMBOBOXINFO cbiSup{}; cbiSup.cbSize = sizeof(cbiSup);
    REQUIRE(GetComboBoxInfo(hSupplier, &cbiSup));
    COMBOBOXINFO cbiSpec{}; cbiSpec.cbSize = sizeof(cbiSpec);
    REQUIRE(GetComboBoxInfo(hSpecies, &cbiSpec));

    CHECK(TypeCharsAndGetResultingLength(cbiSup.hwndItem, (int)kMaxSupplierSpeciesLength + 20) == (int)kMaxSupplierSpeciesLength);
    CHECK(TypeCharsAndGetResultingLength(cbiSpec.hwndItem, (int)kMaxSupplierSpeciesLength + 20) == (int)kMaxSupplierSpeciesLength);

    DestroyWindow(host);
}

TEST_CASE("Manage Names rename/merge and email fields enforce the same limit") {
    HWND host = MakeHiddenTestHostWindow();
    REQUIRE(host != nullptr);
    HWND hTarget = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 100, 20, host, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND hEmail = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 100, 20, host, nullptr, GetModuleHandleW(nullptr), nullptr);
    REQUIRE((hTarget && hEmail));

    FieldLengthLimitTargets limits;
    limits.manageTargetEdit = hTarget;
    limits.manageEmailEdit = hEmail;
    ApplyFieldLengthLimits(limits);

    CHECK(TypeCharsAndGetResultingLength(hTarget, (int)kMaxSupplierSpeciesLength + 20) == (int)kMaxSupplierSpeciesLength);
    CHECK(TypeCharsAndGetResultingLength(hEmail, (int)kMaxSupplierSpeciesLength + 20) == (int)kMaxSupplierSpeciesLength);

    DestroyWindow(host);
}

TEST_CASE("a target left null is skipped, not a crash") {
    FieldLengthLimitTargets limits; // every field defaults to nullptr
    ApplyFieldLengthLimits(limits); // must not crash
    CHECK(true);
}

} // TEST_SUITE control length limits

TEST_SUITE("Windows integration - headless end-to-end smoke test (Phase 1 item 6)") {

TEST_CASE("add, edit, delete, reconcile, save, reload and finalize via callable commands only") {
    TempTestDir tempDir;
    // The single lever that keeps every file this app touches during this
    // test inside tempDir.path instead of this machine's real application
    // folder - see SetExeDirOverrideForTests's comment at its declaration.
    SetExeDirOverrideForTests(tempDir.path);
    ResetApplicationStateForTest();
    LoadSettings(); // reads (nonexistent) settings.txt from tempDir - just defaults

    // Hidden/off-screen: nCmdShow = SW_HIDE, so no window is ever actually
    // shown on screen even though this creates the app's real main window
    // and runs its real WM_CREATE handler (control creation, autosave
    // recovery, etc.) exactly as a real launch would.
    HWND hwnd = CreateFishBalanceMainWindow(GetModuleHandleW(nullptr), SW_HIDE);
    REQUIRE(hwnd != nullptr);
    REQUIRE(g_hMainWnd == hwnd);
    // Nothing recovered from a fresh, empty temp directory - a genuinely
    // isolated first run, not a leftover from some other test or from this
    // developer's own machine.
    REQUIRE(g_entries.empty());

    // --- Add an entry through the same form fields and command the real
    // "Add Entry" button drives (CommitEntryForm) - no dialogs on this path. ---
    SetWindowTextW(hCmbSupplier, L"Smoke Test Supplier");
    SetWindowTextW(hCmbProduct, L"Smoke Test Species");
    SetWindowTextW(hEditKgs, L"10");
    SetWindowTextW(hEditPrice, L"5");
    SetWindowTextW(hEditNotes, L"");
    CommitEntryForm();
    REQUIRE(g_entries.size() == 1);
    CHECK(g_entries[0].supplier == L"Smoke Test Supplier");
    CHECK(g_entries[0].kgs == doctest::Approx(10.0));
    CHECK(g_entries[0].price == doctest::Approx(5.0));

    // --- Add a second entry, to prove delete removes the right one. ---
    SetWindowTextW(hCmbSupplier, L"Second Supplier");
    SetWindowTextW(hCmbProduct, L"Second Species");
    SetWindowTextW(hEditKgs, L"3");
    SetWindowTextW(hEditPrice, L"2");
    SetWindowTextW(hEditNotes, L"");
    CommitEntryForm();
    REQUIRE(g_entries.size() == 2);

    // --- Edit the first entry via LoadEntryIntoForm + CommitEntryForm,
    // exactly like double-clicking a row and changing a field. ---
    LoadEntryIntoForm(0);
    SetWindowTextW(hEditKgs, L"20");
    CommitEntryForm();
    REQUIRE(g_entries.size() == 2); // update, not a third row
    CHECK(g_entries[0].kgs == doctest::Approx(20.0));

    // --- Delete the second entry via the direct-function command (Phase 1
    // item 6 explicitly calls for "direct function/control testing", not
    // driving the confirm MessageBoxW that DeleteSelectedEntry() itself
    // owns). ---
    REQUIRE(DeleteEntryAt(1));
    REQUIRE(g_entries.size() == 1);
    CHECK(g_entries[0].supplier == L"Smoke Test Supplier");

    // --- Reconcile: Debtor + Cash must equal the one remaining entry's
    // total (20 kg * $5 = $100). RecalcTotals() is the same function the
    // real EN_CHANGE handlers call on every keystroke. ---
    SetWindowTextW(hEditDebtor, L"60");
    SetWindowTextW(hEditCash, L"40");
    RecalcTotals();
    CHECK(g_reconciliationValid);
    CHECK(g_diffOk); // 60 + 40 == 100, matches the one remaining entry exactly

    // --- Save to a real named file inside the isolated temp directory,
    // through the same SaveToFile() the File > Save menu command calls. ---
    std::wstring savedPath = tempDir.path + L"\\smoke_test.fbd";
    std::wstring saveErr;
    REQUIRE(SaveToFile(savedPath, &saveErr));
    g_currentFile = savedPath;
    g_dirty = false;

    // --- Reload: clear in-memory state and load the just-saved file back
    // through LoadFromFile(), the same function every Open/Recent
    // Files/startup-recovery path uses. ---
    g_entries.clear();
    std::wstring loadErr;
    REQUIRE(LoadFromFile(savedPath, &loadErr));
    REQUIRE(g_entries.size() == 1);
    CHECK(g_entries[0].supplier == L"Smoke Test Supplier");
    CHECK(g_entries[0].kgs == doctest::Approx(20.0));

    // --- Finalize through ExecuteFinalizeDay(), the same coordination
    // function the Finalize Day dialog's OK button calls - proving the
    // whole day-in-the-life sequence (add/edit/delete/reconcile/save/
    // reload/finalize) works end-to-end through nothing but callable
    // application commands, never a message loop or simulated mouse/
    // keyboard UI automation. ---
    RecalcTotals(); // re-derive g_diffOk/g_reconciliationValid after the reload above
    REQUIRE(g_reconciliationValid);
    REQUIRE(g_diffOk);
    FinalizeOutcome outcome = ExecuteFinalizeDay(L"2026-09-30");
    CHECK(outcome.finalized);
    CHECK(outcome.kind == FinalizeResultKind::FinalizedClean);
    CHECK(g_finalizedDate == L"2026-09-30");
    CHECK_FALSE(g_dirty);

    // The permanent history record must actually exist on disk, inside the
    // isolated temp directory - never this machine's real history\ folder.
    std::wstring historyPath = HistoryDir() + L"\\2026-09-30.fbd";
    DWORD historyAttrs = GetFileAttributesW(historyPath.c_str());
    CHECK(historyAttrs != INVALID_FILE_ATTRIBUTES);
    CHECK(historyPath.find(tempDir.path) == 0);

    DestroyWindow(hwnd);
    g_hMainWnd = nullptr;
    SetExeDirOverrideForTests(L""); // leave no override active for whatever test runs next
}

} // TEST_SUITE headless end-to-end smoke test

#endif // FBM_BUILDING_TESTS
