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
#include <shobjidl.h> // v0.10.16: IFileOpenDialog (Restore from Backup folder picker)
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
#include "src/core/FishBalanceCore.h"
#include "src/data/FishBalanceWin32IO.h"
#include "src/core/FishBalanceControlLimits.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winspool.lib")

// v0.10.2 (split stage 2): the data model/globals, control IDs and window handles
// that used to be declared here now live in these headers (definitions in
// src/data/AppData.cpp and src/forms/UiState.cpp).
#include "src/data/AppData.h"
#include "src/data/Storage.h"
#include "src/data/Backups.h"
#include "src/services/CsvExport.h"
#include "src/services/Email.h"
#include "src/services/ReportRender.h"
#include "src/forms/MainWindowApi.h"
#include "src/forms/MainWindowLists.h"
#include "src/forms/MainWindowLayout.h"
#include "src/forms/ManageNamesWindow.h"
#include "src/forms/PrintPreviewWindow.h"
#include "src/forms/PopupPlacement.h"
#include "src/forms/ControlIds.h"
#include "src/forms/UiState.h"

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

void MaybeBackupOnTimer(); // ROADMAP.md item 4 - defined below AutosaveNow, which calls it
void RememberRecentFile(const std::wstring& path);
void RebuildRecentMenu();
// LooksLikeEmail is declared and defined in FishBalanceCore.h.
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
// The list/combo/totals refresh functions moved to src/forms/MainWindowLists.h (v0.10.8);
// layout, tab switching and control helpers to src/forms/MainWindowLayout.h (v0.10.9).
// Finalize Day (ROADMAP.md item 3)
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

void SaveSettings() {
    if (!g_hMainWnd) return;
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    GetWindowPlacement(g_hMainWnd, &wp);
    RECT rc = wp.rcNormalPosition;
    SaveSettingsFile(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                     wp.showCmd == SW_SHOWMAXIMIZED, g_currentFile);
}

// ---------------------------------------------------------------------------
// Recent Files (recent.txt)
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// File persistence (simple pipe-delimited UTF-8 text format, *.fbd)
// ---------------------------------------------------------------------------

// Builds the full .fbd content (as UTF-8) from the current on-screen and in-memory
// state. Shared by SaveToFile and the rolling-backup snapshot writer
// (WriteBackupSnapshot, ROADMAP.md item 4) so both write identical content.
// v0.10.12 (split stage 7a): this function now only GATHERS the state from the
// controls into an FbdSaveState; the file text itself is built by BuildFbdText()
// in FishBalanceCore.h (pure, unit-tested, round-trips through ParseFbdContent).
// Same output as before, byte for byte.
std::string BuildFbdSaveContent(const std::wstring& finalizedDateOverride = L"", bool includeSourceFile = false) {
    FbdSaveState state;
    // Phase 1 completeness follow-up (2026-09-30, v0.9.50): sized to the
    // largest of the field limits this shared buffer is reused for below
    // (kMaxNotesLength/kMaxDebtorCashExprLength, both 4096) - a smaller
    // buffer would silently truncate a legitimately long Debtor/Cash
    // expression or Notes value right before writing it to disk.
    wchar_t buf[kMaxNotesLength + 1];
    auto textOf = [&](HWND h) -> std::wstring {
        GetWindowTextW(h, buf, (int)(kMaxNotesLength + 1));
        return std::wstring(buf);
    };
    state.debtor = textOf(hEditDebtor);
    state.cash = textOf(hEditCash);

    // Phase 1 / F1 audit remediation, narrowed post-review (2026-09-30): the
    // SOURCE_FILE= identity marker is only ever READ back by wWinMain's
    // startup logic, and only from autosave.fbd specifically - so it's only
    // written for the autosave path (see SaveToFile). Writing g_currentFile's
    // absolute path into every .fbd save would embed the original machine's
    // Windows username/folder path into files that routinely leave the
    // machine (emailed, backed up, opened elsewhere) for no benefit.
    state.includeSourceFile = includeSourceFile;
    state.sourceFile = g_currentFile;

    // finalizedDateOverride: used only by DoFinalizeDay() when writing the new
    // history\<date>.fbd snapshot, so that file is born already carrying
    // FINALIZED= without a separate write-then-rewrite step. Every other
    // caller passes nothing and gets g_finalizedDate.
    state.finalizedDate = finalizedDateOverride.empty() ? g_finalizedDate : finalizedDateOverride;

    // In-progress "Add Entry" form state, so it survives a crash or power
    // loss and not just a graceful close.
    state.draftSupplier = textOf(hCmbSupplier);
    state.draftSpecies = textOf(hCmbProduct);
    state.draftKgs = textOf(hEditKgs);
    state.draftPrice = textOf(hEditPrice);
    state.draftNotes = textOf(hEditNotes);
    if (hDtpDate) {
        SYSTEMTIME st{};
        DateTime_GetSystemtime(hDtpDate, &st);
        state.hasDraftDate = true;
        state.draftDate = FormatDateISO(st);
    }
    state.entries = g_entries;
    return WToUtf8(BuildFbdText(state));
}

// finalizedDateOverride: see BuildFbdSaveContent. Finalize Day passes the date it is
// finalizing so the open named file is written WITH its FINALIZED= marker (v0.10.13 -
// before that, g_finalizedDate was only set after this save, so the file was re-saved
// as an unfinalized day).
bool SaveToFile(const std::wstring& path, std::wstring* outError = nullptr,
                const std::wstring& finalizedDateOverride = L"") {
    // Only the autosave write includes SOURCE_FILE= - see BuildFbdSaveContent's
    // comment. A case-insensitive compare matches how the startup identity
    // check itself compares paths (_wcsicmp), for the same reason: Windows
    // paths are case-insensitive.
    bool isAutosave = (_wcsicmp(path.c_str(), AutosavePath().c_str()) == 0);
    return WriteFileAtomicUtf8(path, BuildFbdSaveContent(finalizedDateOverride, isAutosave), outError);
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
    g_allowMixedDatesThisSession = false; // a different sheet - ask again if its dates get mixed
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
// Autosave / backup / refresh-all
// ---------------------------------------------------------------------------

// Attempts the autosave write and warns (once, not repeated while the same
// problem persists) if it fails - shared by RefreshAll() and by the
// Debtor/Cash immediate-autosave path below, so both get the same
// warn-once/reset-on-recovery behavior from one place instead of two
// copies that could drift apart. The warn-once rule itself is NoteSaveOutcome()
// in FishBalanceCore.h (unit-tested); this function only shows the popup.
void AutosaveNow() {
    bool ok = SaveToFile(AutosavePath());
    if (ok) MaybeBackupOnTimer();
    if (NoteSaveOutcome(ok, g_autosaveFailWarned, g_hMainWnd != nullptr)) {
        MessageBoxW(g_hMainWnd,
            L"Warning: the automatic backup (autosave.fbd) could not be saved just now - "
            L"the disk may be full, or the file is locked by another program (e.g. antivirus).\n\n"
            L"Your current data is still safe in memory. If you haven't saved to a named file "
            L"recently, use File > Save now to make sure nothing is lost. This warning won't "
            L"repeat again until autosave succeeds at least once.",
            L"Autosave Failed", MB_OK | MB_ICONWARNING);
    }
}

// Writes a timestamped snapshot of `content` to the backups folder,
// independent of wherever autosave/Save is writing to.
//
// v0.9.31: this used to be "best-effort and silent on failure" - a failed
// write (disk full, backups\ unwritable, antivirus lock, etc.) was completely
// invisible. Now checked and warned once, same pattern as g_autosaveFailWarned,
// reset the next time a snapshot actually succeeds.
// v0.10.14 (split stage 7b): the disk write moved to WriteBackupSnapshotFile()
// in src/data/Backups.cpp (it deliberately leaves the "last backup" bookkeeping
// untouched on failure, so the next autosave retries immediately); this function
// only shows the warning popup.
void WriteBackupSnapshot(const std::string& content) {
    std::wstring writeError;
    bool ok = WriteBackupSnapshotFile(content, &writeError);
    if (NoteSaveOutcome(ok, g_backupFailWarned, g_hMainWnd != nullptr)) {
        std::wstring msg =
            L"Warning: a rolling backup snapshot could not be saved just now (" + writeError + L").\n\n"
            L"Your current data is still safe in memory, and autosave.fbd is a separate file "
            L"unaffected by this. If you haven't saved to a named file recently, use File > Save "
            L"now to make sure nothing is lost. This warning won't repeat again until a backup "
            L"snapshot succeeds.";
        MessageBoxW(g_hMainWnd, msg.c_str(), L"Backup Snapshot Failed", MB_OK | MB_ICONWARNING);
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
    if (!BackupIntervalElapsed(g_lastBackupTick, now, kBackupIntervalMs)) return;

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
    if (!ParseDoubleW(kgsStr, kgs)) {
        MessageBoxW(g_hMainWnd, L"Please enter a valid Kgs value.", L"Invalid Data", MB_OK | MB_ICONWARNING);
        return;
    }
    // Phase 2 / F15 (2026-10-02): centralized magnitude check, applied the
    // same way here and at file-load time (ParseFbdContent) - see
    // ValidateWeightKg's comment in FishBalanceCore.h for where the
    // 10,000kg ceiling came from.
    NumericValidationResult kgsCheck = ValidateWeightKg(kgs);
    if (!kgsCheck.ok) {
        MessageBoxW(g_hMainWnd, kgsCheck.error.c_str(), L"Invalid Data", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!ParseDoubleW(priceStr, price)) {
        MessageBoxW(g_hMainWnd, L"Please enter a valid Price value.", L"Invalid Data", MB_OK | MB_ICONWARNING);
        return;
    }
    NumericValidationResult priceCheck = ValidatePriceDollars(price);
    if (!priceCheck.ok) {
        MessageBoxW(g_hMainWnd, priceCheck.error.c_str(), L"Invalid Data", MB_OK | MB_ICONWARNING);
        return;
    }

    SYSTEMTIME dtpVal{};
    DateTime_GetSystemtime(hDtpDate, &dtpVal);
    std::wstring date = FormatDateISO(dtpVal);

    // v0.9.63 (Jack, 2026-10-14): a sheet is normally one day - Finalize Day
    // closes it - so an entry dated differently from everything already on
    // the sheet (e.g. yesterday's entries, then one for today) is confirmed
    // first. Yes = add it and stop asking until the sheet changes; No =
    // don't add (the form keeps what was typed, so just fix the date).
    // Defaults to No so a quick Enter can't wave it through by accident.
    // Checked before anything is modified, and for edits it ignores the row
    // being edited (so re-dating the only row on a sheet never prompts).
    if (!g_allowMixedDatesThisSession) {
        int dateExcludeIdx = (g_editIndex >= 0 && g_editIndex < (int)g_entries.size()) ? g_editIndex : -1;
        if (IsNewDateOnSheet(g_entries, date, dateExcludeIdx)) {
            std::vector<std::wstring> sheetDates = DistinctSheetDates(g_entries, dateExcludeIdx);
            std::wstring listed;
            for (size_t i = 0; i < sheetDates.size() && i < 3; i++) {
                if (i) listed += L", ";
                listed += sheetDates[i];
            }
            if (sheetDates.size() > 3) listed += L" and " + std::to_wstring(sheetDates.size() - 3) + L" more";
            std::wstring msg = L"The entries already on this sheet are dated " + listed +
                               L", but this entry is dated " + date + L".\n\n"
                               L"A sheet is normally one day's entries. Add this entry anyway?\n\n"
                               L"Yes = add it, and don't ask again for this session.\n"
                               L"No = don't add it (change the Date and try again).";
            int r = MessageBoxW(g_hMainWnd, msg.c_str(), L"Different Date", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
            if (r != IDYES) return;
            g_allowMixedDatesThisSession = true;
        }
    }

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
    std::vector<double> outlierBaseline = GatherOtherPricesForSpecies(g_entries, product, outlierExcludeIdx); // v0.9.62: whole sheet, not same date
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
    ReevaluateOutlierFlagsForSpecies(g_entries, product); // v0.9.62: whole sheet, not same date

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
    std::wstring undoProduct = e.product; // captured before erase invalidates e
    g_hasUndo = true;
    if (g_hEditMenu) EnableMenuItem(g_hEditMenu, ID_EDIT_UNDO_DELETE, MF_BYCOMMAND | MF_ENABLED);

    g_entries.erase(g_entries.begin() + sel);
    g_dirty = true; // v0.9.30
    // Removing this entry can change whether its siblings still look like
    // outliers (the baseline they're judged against just shrank) - see
    // ReevaluateOutlierFlagsForSpeciesOnDate / ROADMAP.md item 7.
    ReevaluateOutlierFlagsForSpecies(g_entries, undoProduct); // v0.9.62
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
    ReevaluateOutlierFlagsForSpecies(g_entries, g_undoEntry.product); // v0.9.62
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
        std::vector<double> baseline = GatherOtherPricesForSpecies(g_entries, e.product, idx); // v0.9.62
        OutlierRange range;
        bool stillOutlier = ComputeOutlierRange(baseline, range) && (e.price < range.low || e.price > range.high);
        wchar_t msg[512];
        if (stillOutlier) {
            bool tooHigh = e.price > range.high;
            double limit = tooHigh ? range.high : range.low; // low is guaranteed >0 here - see CommitEntryForm's identical logic
            swprintf(msg, 512,
                L"This entry is $%.2f/kg for %s.\n"
                L"The dynamic limit calculated from the other %s entries on this sheet is %s $%.2f/kg.\n\n"
                L"Still looks like a typo?\n\n"
                L"Click Yes to edit it, No to leave it as-is.",
                e.price, e.product.c_str(), e.product.c_str(), tooHigh ? L"max" : L"min", limit);
        } else {
            swprintf(msg, 512,
                L"This entry's price ($%.2f) was flagged earlier as unusual for %s, but no "
                L"longer looks unusual against the other entries on this sheet. Click Yes to edit it anyway, "
                L"No to clear the flag and leave it as-is.",
                e.price, e.product.c_str());
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
    g_allowMixedDatesThisSession = false; // fresh sheet
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

// v0.10.17 (split stage 7c): File > Open and File > Recent Files were two near-identical
// copies of "confirm discard, snapshot what is about to be lost, load, reset the form, remember
// the file" - now one shared function so they cannot drift apart again.
// discardPhrase completes "Discard the current data and ...?"; errLead starts the failure message.
static void OpenNamedFile(const std::wstring& path, const wchar_t* discardPhrase, const wchar_t* errLead) {
    if (!ConfirmDiscardCurrentData(discardPhrase)) return;
    // v0.9.28: take an unconditional backup snapshot of whatever is about
    // to be discarded, before LoadFromFile overwrites g_entries in memory.
    // This closes the exposure window the v0.9.24 data-loss incident
    // found - confirming "discard and open" isn't itself a backup, only
    // an acknowledgement, so the data being discarded still needs its own
    // snapshot the same way an explicit Save already gets one.
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
        // Phase 1 completeness follow-up (2026-09-30, v0.9.50): loadErr carries a specific reason.
        MessageBoxW(g_hMainWnd,
            (std::wstring(errLead) + loadErr + L".\n\nNothing has been changed.").c_str(),
            L"Error", MB_OK | MB_ICONERROR);
    }
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
    if (GetOpenFileNameW(&ofn))
        OpenNamedFile(file, L"open the selected file", L"Could not open the selected file: ");
}

// v0.10.16: modern folder-forcing picker for Restore from Backup. The classic
// GetOpenFileNameW remembers the last folder used anywhere in the app and ignores both
// lpstrInitialDir and the pre-filled-folder trick on some systems (Jack, 2026-10-15:
// after saving a file elsewhere, Restore opened there instead of in backups). IFileOpenDialog
// with SetFolder ALWAYS opens in the folder given. Returns 1 = picked (outPath set),
// 0 = cancelled, -1 = could not be used (caller falls back to the classic dialog).
static int PickFileInFolderModern(HWND owner, const std::wstring& folder, std::wstring& outPath) {
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool uninit = SUCCEEDED(hrInit); // RPC_E_CHANGED_MODE: already initialised another way; just use it
    int result = -1;
    IFileOpenDialog* dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dlg))) && dlg) {
        COMDLG_FILTERSPEC filters[] = {
            { L"Fish Balance Backups (*.fbd)", L"*.fbd" },
            { L"All Files (*.*)", L"*.*" },
        };
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST);
        dlg->SetFileTypes(2, filters);
        dlg->SetFileTypeIndex(1);
        dlg->SetDefaultExtension(L"fbd");
        IShellItem* folderItem = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&folderItem))) && folderItem) {
            dlg->SetFolder(folderItem);
            folderItem->Release();
        }
        HRESULT hr = dlg->Show(owner);
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            result = 0;
        } else if (SUCCEEDED(hr)) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    outPath = path;
                    CoTaskMemFree(path);
                    result = 1;
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    if (uninit) CoUninitialize();
    return result;
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
    bool havePick = false;
    {
        std::wstring picked;
        int r = PickFileInFolderModern(g_hMainWnd, dir, picked);
        if (r == 0) return; // cancelled
        if (r == 1 && picked.size() < MAX_PATH) {
            wcsncpy_s(file, MAX_PATH, picked.c_str(), _TRUNCATE);
            havePick = true;
        }
        // r == -1 (modern dialog unavailable) or path too long: classic dialog below.
    }
    if (!havePick) {
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
        if (!GetOpenFileNameW(&ofn)) {
            // v0.10.15: this used to just `return`, which made a picker that FAILED to open
            // (as opposed to one the user cancelled) look like "Restore from Backup does
            // nothing" (Jack, 2026-10-15). CommDlgExtendedError() is 0 for a plain cancel.
            DWORD pickerErr = CommDlgExtendedError();
            if (pickerErr == 0) return; // user cancelled
            // Retry once without the folder pre-filled into the file name box - some systems
            // reject a bare folder there; the dialog then falls back to lpstrInitialDir.
            file[0] = L'\0';
            if (!GetOpenFileNameW(&ofn)) {
                pickerErr = CommDlgExtendedError();
                if (pickerErr == 0) return; // cancelled the retry
                wchar_t codeText[96];
                swprintf(codeText, 96, L"The file picker could not be opened (error code 0x%04X).", (unsigned)pickerErr);
                MessageBoxW(g_hMainWnd, (std::wstring(codeText) +
                    L"\n\nThe backups are in:\n" + dir +
                    L"\n\nYou can also use File > Open on one of those files.").c_str(),
                    L"Restore from Backup", MB_OK | MB_ICONWARNING);
                return;
            }
        }

    }

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
    ports.writeNamedFileSync = [date](std::wstring* outError) {
        // v0.10.13: g_finalizedDate is only set AFTER CoordinateFinalizeDay returns, so the
        // open file must be told the date explicitly or it is saved as an unfinalized day.
        return SaveToFile(g_currentFile, outError, date);
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
                g_allowMixedDatesThisSession = false; // next day's sheet starts fresh
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
    FitPopupToWorkArea(g_hMainWnd, x, y, w, h); // v0.10.7: never taller/wider than the screen

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
            L"on Tab 1 (Reconciliation) first.",
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

// Reads the raw Debtor and Cash box text (same fixed-size buffer the old
// in-renderer code used) so it can be handed to RenderReportPages().
void GetDebtorCashText(std::wstring& debtor, std::wstring& cash) {
    wchar_t buf[kMaxDebtorCashExprLength + 1];
    GetWindowTextW(hEditDebtor, buf, (int)(kMaxDebtorCashExprLength + 1));
    debtor = buf;
    GetWindowTextW(hEditCash, buf, (int)(kMaxDebtorCashExprLength + 1));
    cash = buf;
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

    std::wstring dimError;
    if (!ValidateReportPageDimensions(pageWidthPx, pageHeightPx, dpiX, dpiY, &dimError)) {
        MessageBoxW(owner, dimError.c_str(), L"Print", MB_OK | MB_ICONERROR);
        DeleteDC(hdc);
        freeDlgHandles();
        return;
    }

    DOCINFOW di{};
    di.cbSize = sizeof(di);
    di.lpszDocName = L"Fish Balance - Breakdown Report";
    if (StartDocW(hdc, &di) <= 0) {
        MessageBoxW(owner, L"Windows couldn't start the print job.", L"Print", MB_OK | MB_ICONERROR);
        DeleteDC(hdc);
        freeDlgHandles();
        return;
    }

    // Phase 2 / F10 (2026-10-02): pages are printed one at a time as
    // they're rendered - this callback receives each finished page, prints
    // it immediately, and frees it before RenderReportPages renders the
    // next one, so at most one full-resolution page bitmap exists in
    // memory for the whole print job (see RenderReportPages's comment).
    bool printFailed = false;
    std::wstring printError;
    auto onPageReady = [&](RenderedPage& page) -> bool {
        bool pageOk = true;
        if (StartPage(hdc) <= 0) {
            printError = L"Windows couldn't start a page while printing.";
            pageOk = false;
        } else {
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = page.width;
            bmi.bmiHeader.biHeight = -page.height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 24;
            bmi.bmiHeader.biCompression = BI_RGB;
            int blitResult = StretchDIBits(hdc, 0, 0, page.width, page.height, 0, 0, page.width, page.height,
                                            page.bits, &bmi, DIB_RGB_COLORS, SRCCOPY);
            if (blitResult == 0 || blitResult == (int)GDI_ERROR) {
                printError = L"Windows couldn't draw a page while printing.";
                pageOk = false;
            } else if (EndPage(hdc) <= 0) {
                printError = L"Windows couldn't finish a page while printing.";
                pageOk = false;
            }
        }
        if (page.bitmap) DeleteObject(page.bitmap); // this callback owns the page once handed it, regardless of pageOk
        if (!pageOk) printFailed = true;
        return pageOk;
    };

    std::wstring debtorText, cashText;
    GetDebtorCashText(debtorText, cashText);
    RenderReportResult result = RenderReportPages(pageWidthPx, pageHeightPx, dpiX, dpiY, debtorText, cashText, onPageReady);

    if (printFailed || !result.ok) {
        // AbortDoc, not EndDoc: StartDocW succeeded, so the spooler has an
        // open job for this document that must be explicitly cancelled
        // rather than left to finish (EndDoc) or just abandoned.
        AbortDoc(hdc);
        DeleteDC(hdc);
        freeDlgHandles();
        std::wstring msg = !printError.empty() ? printError : result.error;
        if (msg.empty()) msg = L"Printing failed.";
        MessageBoxW(owner, msg.c_str(), L"Print", MB_OK | MB_ICONERROR);
        return;
    }

    if (EndDoc(hdc) <= 0) {
        MessageBoxW(owner, L"Windows reported a problem finishing the print job - the document may not "
                            L"have printed correctly.", L"Print", MB_OK | MB_ICONWARNING);
    }
    DeleteDC(hdc);
    freeDlgHandles();

    if (result.pageCapHit) {
        MessageBoxW(owner, (L"This report has more than " + std::to_wstring(kReportMaxPages) +
            L" pages. Only the first " + std::to_wstring(kReportMaxPages) +
            L" were printed.").c_str(), L"Print", MB_OK | MB_ICONWARNING);
    }
}

// ---------------------------------------------------------------------------
// Email Suppliers
//
// Opens one pre-filled compose window per supplier in the user's default
// email app (via the mailto: protocol), for review before sending - no SMTP
// server or stored credentials needed. Supplier addresses come from
// Tools > Manage Supplier / Species Names... (g_supplierEmails).
// ---------------------------------------------------------------------------

// UrlEncodeForMailto, LooksLikeEmail, PadRight, and BuildSupplierEmailBody
// now all live in FishBalanceCore.h (unchanged logic; BuildSupplierEmailBody
// there takes the current hour as an explicit parameter instead of reading
// the clock internally, so it can be unit-tested with any hour value - the
// call site in EmailSupplier below passes the real current hour in).

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

        std::wstring emailErr;
        if (!EmailSupplier(email, sg, &emailErr)) {
            std::wstring failMsg = L"Couldn't open an email draft for " + sg.supplier + L":\n\n" + emailErr +
                L"\n\nClick OK to try the next supplier, or Cancel to stop here.";
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
        hLblSupplier = MakeControl(L"STATIC", L"Supplier", WS_VISIBLE, 0, hwnd);
        hCmbSupplier = MakeControl(L"COMBOBOX", L"", WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWN | WS_TABSTOP, ID_CMB_SUPPLIER, hwnd);
        hLblProduct = MakeControl(L"STATIC", L"Species", WS_VISIBLE, 0, hwnd);
        hCmbProduct = MakeControl(L"COMBOBOX", L"", WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWN | WS_TABSTOP, ID_CMB_PRODUCT, hwnd);
        hLblKgs = MakeControl(L"STATIC", L"Kgs", WS_VISIBLE, 0, hwnd);
        hEditKgs = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP, ID_EDIT_KGS, hwnd);
        hLblPrice = MakeControl(L"STATIC", L"Price / kg", WS_VISIBLE, 0, hwnd);
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

        hLblDate = MakeControl(L"STATIC", L"Date", WS_VISIBLE, 0, hwnd);
        hDtpDate = MakeControl(DATETIMEPICK_CLASS, L"", WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATEFORMAT, ID_DTP_DATE, hwnd);
        hLblNotes = MakeControl(L"STATIC", L"Notes", WS_VISIBLE, 0, hwnd);
        // ES_AUTOHSCROLL (2026-10-02): without this, a single-line EDIT
        // control rejects any further typed character once the text no
        // longer fits the control's own visible width (a separate
        // mechanism from EM_LIMITTEXT below) - discovered while building
        // v0.9.51's Windows control-limit test, which hit exactly this
        // wall on its own throwaway controls. At this field's real on-
        // screen width (~340px), that meant typing could silently stop
        // dead well short of the documented 4,096-character Notes limit.
        // This only affects character-by-character typing - pasting and
        // loading a long value from a file were never affected.
        hEditNotes = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_EDIT_NOTES, hwnd);
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
        AddColumn(hListEntries, 3, L"Price ($/kg)", 115);
        AddColumn(hListEntries, 4, L"Total ($)", 125);
        AddColumn(hListEntries, 5, L"Date", 110);
        AddColumn(hListEntries, 6, L"Notes", 160);

        hGrpRecon = MakeControl(L"BUTTON", L"Reconciliation (checks Debtor + Cash against entries above)", WS_VISIBLE | BS_GROUPBOX, 0, hwnd);
        hLblDebtor = MakeControl(L"STATIC", L"Debtor amount(s):", WS_VISIBLE, 0, hwnd);
        // ES_AUTOHSCROLL - see hEditNotes' comment above for why. Debtor/
        // Cash are ~260px wide on screen, well short of what's needed to
        // visibly fit anywhere near the 4,096-character limit without it.
        hEditDebtor = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_EDIT_DEBTOR, hwnd);
        hLblCash = MakeControl(L"STATIC", L"Cash amount(s):", WS_VISIBLE, 0, hwnd);
        hEditCash = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_EDIT_CASH, hwnd);
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
        hLblStatus = MakeControl(L"STATIC", L"", WS_VISIBLE, 0, hwnd);
        hDivRecon1 = MakeControl(L"STATIC", L"", WS_VISIBLE | SS_ETCHEDVERT, 0, hwnd);
        hDivRecon2 = MakeControl(L"STATIC", L"", WS_VISIBLE | SS_ETCHEDVERT, 0, hwnd);
        // Bold totals/status per the mockup.
        if (g_boldFont) {
            for (HWND hb : { hLblBook, hLblEntered, hLblDiff, hLblStatus })
                if (hb) SendMessageW(hb, WM_SETFONT, (WPARAM)g_boldFont, TRUE);
        }
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

        AddButtonGlyphIcon(hBtnDelete, L'\uE74D', RGB(196, 43, 28));  // trash
        AddButtonGlyphIcon(hBtnEdit, L'\uE70F', RGB(0, 95, 184));     // pencil
        AddButtonGlyphIcon(hBtnCancelEdit, L'\uE711', RGB(90, 90, 90)); // cancel (x)
        for (HWND lv : { hListEntries, hListOverview, hListBreakdown, hListBySpecies })
            SetListRowHeight(lv, S(28));

        g_tab1Ctrls = { hLblSupplier, hCmbSupplier, hLblProduct, hCmbProduct, hLblKgs, hEditKgs,
                         hLblPrice, hEditPrice, hBtnAdd, hBtnDelete, hBtnEdit, hBtnCancelEdit, hBtnDuplicate,
                         hBtnDuplicateSupSpec,
                         hLblDate, hDtpDate, hLblNotes, hEditNotes,
                         hLblFilter, hEditFilter, hListEntries, hGrpRecon,
                         hLblDebtor, hEditDebtor, hLblCash, hEditCash, hLblBook, hLblEntered, hLblDiff,
                         hLblStatus, hDivRecon1, hDivRecon2,
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
        if (hCtl == hLblDiff || hCtl == hLblStatus || hCtl == hLblOvDiff) {
            HDC hdc = (HDC)wParam;
            bool ok = (hCtl == hLblDiff || hCtl == hLblStatus) ? g_diffOk : g_ovDiffOk;
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
                OpenNamedFile(path, L"open that file", L"Could not open that file: ");
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
        for (HIMAGELIST il : g_buttonIconLists) ImageList_Destroy(il);
        g_buttonIconLists.clear();
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
    // v0.9.64 (audit F24): single-instance guard. Two copies running at once
    // would both write the same autosave/settings/backup files. If another
    // copy already holds the mutex, bring its window to the front and exit.
    // The handle is deliberately kept open for the life of the process.
    HANDLE singleInstanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\FishBalanceManager_SingleInstance");
    if (singleInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"FishBalanceMainWindow", nullptr);
        if (existing) {
            if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        } else {
            MessageBoxW(nullptr, L"Fish Balance Manager is already running.", L"Fish Balance Manager", MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    }

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_DATE_CLASSES };
    InitCommonControlsEx(&icc);

    {
        HDC screenDC = GetDC(nullptr);
        g_dpi = GetDeviceCaps(screenDC, LOGPIXELSX);
        ReleaseDC(nullptr, screenDC);
        if (g_dpi <= 0) g_dpi = 96;
    }

    LoadSettings(g_dpi); // window size/position + last-file, read before creating the window

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
    g_allowMixedDatesThisSession = false;
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
    LoadSettings(g_dpi); // reads (nonexistent) settings.txt from tempDir - just defaults

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

    // v0.10.13 regression (Jack, 2026-10-15: "when I went back to the saved file it wasn't
    // finalised"): BOTH files on disk must carry the finalized date - the permanent history
    // record AND the open named file that was re-saved as part of finalizing.
    LoadFbdFileResult historyOnDisk = LoadFbdFileContent(historyPath);
    REQUIRE(historyOnDisk.ok);
    CHECK(historyOnDisk.parsed.finalizedDate == L"2026-09-30");
    LoadFbdFileResult namedOnDisk = LoadFbdFileContent(savedPath);
    REQUIRE(namedOnDisk.ok);
    CHECK(namedOnDisk.parsed.finalizedDate == L"2026-09-30");
    CHECK(namedOnDisk.parsed.entries.size() == 1);

    DestroyWindow(hwnd);
    g_hMainWnd = nullptr;
    SetExeDirOverrideForTests(L""); // leave no override active for whatever test runs next
}

} // TEST_SUITE headless end-to-end smoke test

#endif // FBM_BUILDING_TESTS
