// Fish Balance Manager
// A native Windows (Win32 API) replacement for the BALANCE_PivotTable.xlsx workbook.
//
// Tab 1 "Data Entry"     - enter Supplier / Species / Kgs / Price rows, and enter the
//                           Debtor / Cash figures from the books; the app checks that
//                           Debtor + Cash equals the sum of entered rows.
// Tab 2 "Total Overview" - a $ total for every supplier, plus the grand total and the
//                           same book-balance check.
// Tab 3 "Breakdown"      - entries grouped by Supplier -> Species -> Price, with a
//                           weight (Kgs) and $ total for each, and subtotals.
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

#include "resource.h"
#include "version.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winspool.lib")

// ---------------------------------------------------------------------------
// Data model
// ---------------------------------------------------------------------------

struct Entry {
    std::wstring supplier;
    std::wstring product; // "species"
    double kgs = 0.0;
    double price = 0.0;
    std::wstring date;  // ISO format YYYY-MM-DD; empty means not set (e.g. loaded from an older file)
    std::wstring notes;
    double Total() const { return kgs * price; }
};

static std::vector<Entry> g_entries;
static std::wstring g_currentFile; // empty = unsaved / using autosave only

// ---------------------------------------------------------------------------
// Control IDs
// ---------------------------------------------------------------------------

enum {
    ID_FILE_NEW = 1, ID_FILE_OPEN, ID_FILE_SAVE, ID_FILE_SAVEAS, ID_FILE_PRINT, ID_FILE_PRINT_PREVIEW,
    ID_FILE_EXPORT_CSV, ID_FILE_EMAIL_SUPPLIERS, ID_FILE_EXIT, ID_FILE_ABOUT, ID_EDIT_UNDO_DELETE,
    ID_TOOLS_MANAGE_NAMES,
    ID_TAB = 100,
    ID_CMB_SUPPLIER = 200, ID_CMB_PRODUCT, ID_EDIT_KGS, ID_EDIT_PRICE, ID_BTN_ADD, ID_BTN_DELETE,
    ID_BTN_EDIT, ID_BTN_CANCEL_EDIT, ID_EDIT_FILTER, ID_DTP_DATE, ID_EDIT_NOTES, ID_BTN_DUPLICATE,
    ID_LIST_ENTRIES, ID_EDIT_DEBTOR, ID_EDIT_CASH,
    ID_LIST_OVERVIEW, ID_LIST_BREAKDOWN, ID_BTN_PRINT_BREAKDOWN, ID_BTN_EMAIL_SUPPLIERS,
    ID_BTN_PRINT_PREVIEW, ID_LIST_BYSPECIES,
    // Manage Names popup window controls
    ID_MNG_RADIO_SUPPLIER = 400, ID_MNG_RADIO_SPECIES, ID_MNG_LIST, ID_MNG_TARGET, ID_MNG_APPLY, ID_MNG_CLOSE,
    ID_MNG_EMAIL_EDIT, ID_MNG_EMAIL_SAVE,
    // Print Preview popup window controls
    ID_PREVIEW_PREV = 500, ID_PREVIEW_NEXT, ID_PREVIEW_PRINT, ID_PREVIEW_CLOSE,
    // Up to 8 Recent Files slots
    ID_RECENT_BASE = 900
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
static HFONT g_normalFont = nullptr, g_boldFont = nullptr;

// Tab 1
static HWND hLblSupplier, hCmbSupplier, hLblProduct, hCmbProduct;
static HWND hLblKgs, hEditKgs, hLblPrice, hEditPrice, hBtnAdd, hBtnDelete, hBtnEdit, hBtnCancelEdit;
static HWND hLblFilter, hEditFilter;
static HWND hLblDate, hDtpDate, hLblNotes, hEditNotes, hBtnDuplicate;
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

void RefreshAll();
void LayoutAll(HWND hwnd);
void ShowTab(int idx);
void RecalcTotals();
void CancelEdit();
void RefreshEntriesList();
void RefreshOverviewList();
void RefreshBySpeciesList();
void RefreshBreakdownList();
void RememberRecentFile(const std::wstring& path);
void RebuildRecentMenu();
void PopulateManageList();
bool LooksLikeEmail(const std::wstring& email);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ManageWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK PreviewWndProc(HWND, UINT, WPARAM, LPARAM);
void PrintBreakdownReport(HWND owner);

// ---------------------------------------------------------------------------
// Small utilities
// ---------------------------------------------------------------------------

std::wstring TrimW(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Dates are stored as ISO "YYYY-MM-DD" throughout (sorts correctly as plain
// text, unambiguous regardless of locale), converted to/from a SYSTEMTIME
// only at the point of talking to the DateTimePicker control. Note the
// comctl32 macros are DateTime_GetSystemtime / DateTime_SetSystemtime -
// lowercase "time", easy to mistype as GetSystemTime.
std::wstring FormatDateISO(const SYSTEMTIME& st) {
    std::wstringstream ss;
    ss << std::setfill(L'0') << std::setw(4) << st.wYear << L"-"
       << std::setw(2) << st.wMonth << L"-" << std::setw(2) << st.wDay;
    return ss.str();
}

bool ParseISODate(const std::wstring& s, SYSTEMTIME& out) {
    if (s.size() != 10 || s[4] != L'-' || s[7] != L'-') return false;
    SYSTEMTIME st{};
    st.wYear = (WORD)_wtoi(s.substr(0, 4).c_str());
    st.wMonth = (WORD)_wtoi(s.substr(5, 2).c_str());
    st.wDay = (WORD)_wtoi(s.substr(8, 2).c_str());
    if (st.wYear < 1900 || st.wMonth < 1 || st.wMonth > 12 || st.wDay < 1 || st.wDay > 31) return false;
    out = st;
    return true;
}

std::string WToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}

std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

std::wstring ToFixed(double v, int decimals) {
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(decimals) << v;
    return ss.str();
}

std::wstring FormatMoney(double v) {
    bool neg = v < 0;
    if (neg) v = -v;
    std::wstring s = ToFixed(v, 2);
    size_t dot = s.find(L'.');
    std::wstring intPart = s.substr(0, dot);
    std::wstring frac = s.substr(dot);
    std::wstring withCommas;
    int cnt = 0;
    for (int i = (int)intPart.size() - 1; i >= 0; i--) {
        withCommas.push_back(intPart[i]);
        cnt++;
        if (cnt % 3 == 0 && i != 0) withCommas.push_back(L',');
    }
    std::reverse(withCommas.begin(), withCommas.end());
    return (neg ? std::wstring(L"-$") : std::wstring(L"$")) + withCommas + frac;
}

std::wstring FormatNum(double v) {
    return ToFixed(v, 2);
}

// Kg is always displayed to 1 decimal place (business convention) - kept
// separate from FormatNum since that's also used for plain-number dollar
// amounts in the CSV export, which still need 2 decimal places.
std::wstring FormatKg(double v) {
    return ToFixed(v, 1);
}

bool ParseDoubleW(const std::wstring& sIn, double& out) {
    std::wstring s = TrimW(sIn);
    if (s.empty()) return false;
    try {
        size_t pos = 0;
        out = std::stod(s, &pos);
        return pos == s.size();
    } catch (...) {
        return false;
    }
}

// Parses simple sums like "250.7+1826+2552+286" (mirrors how the original
// spreadsheet's Debtor/Cash cells were built up from several manual figures).
double ParseSumExpr(const std::wstring& s) {
    double sum = 0;
    std::wstring cur;
    auto flush = [&]() {
        std::wstring t = TrimW(cur);
        if (!t.empty()) {
            try { sum += std::stod(t); } catch (...) {}
        }
        cur.clear();
    };
    for (wchar_t c : s) {
        if (c == L'+') flush();
        else cur.push_back(c);
    }
    flush();
    return sum;
}

std::wstring GetExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    size_t pos = p.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"." : p.substr(0, pos);
}

// Keeps the currently open/saved file name visible in the title bar, so it's
// always obvious what's loaded and whether it's been saved to a named file.
void UpdateTitle() {
    std::wstring title = L"Fish Balance Manager";
    if (!g_currentFile.empty()) {
        size_t pos = g_currentFile.find_last_of(L"\\/");
        std::wstring name = (pos == std::wstring::npos) ? g_currentFile : g_currentFile.substr(pos + 1);
        title += L" - " + name;
    } else {
        title += L" - (unsaved)";
    }
    if (g_hMainWnd) SetWindowTextW(g_hMainWnd, title.c_str());
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

// Reads a whole text file (UTF-8) into a list of lines. Returns false (with
// an empty list) if the file doesn't exist - this is expected on first run.
bool ReadAllLines(const std::wstring& path, std::vector<std::wstring>& outLines) {
    outLines.clear();
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return false; }
    std::string data((size_t)sz, 0);
    if (sz > 0) fread(&data[0], 1, (size_t)sz, f);
    fclose(f);

    std::wstring all = Utf8ToW(data);
    std::wstring cur;
    for (wchar_t c : all) {
        if (c == L'\n') {
            if (!cur.empty() && cur.back() == L'\r') cur.pop_back();
            outLines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) outLines.push_back(cur);
    return true;
}

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

    FILE* f = _wfopen(SettingsPath().c_str(), L"wb");
    if (!f) return;
    auto writeLine = [&](const std::wstring& l) {
        std::string u8 = WToUtf8(l) + "\n";
        fwrite(u8.data(), 1, u8.size(), f);
    };
    writeLine(L"X=" + std::to_wstring(rc.left));
    writeLine(L"Y=" + std::to_wstring(rc.top));
    writeLine(L"W=" + std::to_wstring(rc.right - rc.left));
    writeLine(L"H=" + std::to_wstring(rc.bottom - rc.top));
    writeLine(std::wstring(L"MAX=") + (wp.showCmd == SW_SHOWMAXIMIZED ? L"1" : L"0"));
    writeLine(L"LASTFILE=" + g_currentFile);
    fclose(f);
}

// ---------------------------------------------------------------------------
// Recent Files (recent.txt)
// ---------------------------------------------------------------------------

std::wstring RecentFilesPath() { return GetExeDir() + L"\\recent.txt"; }

void SaveRecentFiles() {
    FILE* f = _wfopen(RecentFilesPath().c_str(), L"wb");
    if (!f) return;
    for (auto& p : g_recentFiles) {
        std::string u8 = WToUtf8(p) + "\n";
        fwrite(u8.data(), 1, u8.size(), f);
    }
    fclose(f);
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
        if (!sup.empty() && !email.empty()) g_supplierEmails[sup] = email;
    }
}

void SaveSupplierEmails() {
    FILE* f = _wfopen(EmailsPath().c_str(), L"wb");
    if (!f) return;
    for (auto& kv : g_supplierEmails) {
        std::string u8 = WToUtf8(kv.first + L"|" + kv.second) + "\n";
        fwrite(u8.data(), 1, u8.size(), f);
    }
    fclose(f);
}

// ---------------------------------------------------------------------------
// File persistence (simple pipe-delimited UTF-8 text format, *.fbd)
// ---------------------------------------------------------------------------

bool SaveToFile(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    auto writeLine = [&](const std::wstring& line) {
        std::string u8 = WToUtf8(line) + "\n";
        fwrite(u8.data(), 1, u8.size(), f);
    };
    wchar_t buf[512];
    GetWindowTextW(hEditDebtor, buf, 512);
    writeLine(std::wstring(L"DEBTOR=") + buf);
    GetWindowTextW(hEditCash, buf, 512);
    writeLine(std::wstring(L"CASH=") + buf);
    writeLine(L"BEGIN");
    for (auto& e : g_entries) {
        std::wstring line = e.supplier + L"|" + e.product + L"|" +
                             ToFixed(e.kgs, 4) + L"|" + ToFixed(e.price, 4) + L"|" +
                             e.date + L"|" + e.notes;
        writeLine(line);
    }
    writeLine(L"END");
    fclose(f);
    return true;
}

bool LoadFromFile(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return false; }
    std::string data((size_t)sz, 0);
    if (sz > 0) fread(&data[0], 1, (size_t)sz, f);
    fclose(f);

    std::wstring all = Utf8ToW(data);
    std::vector<std::wstring> lines;
    std::wstring cur;
    for (wchar_t c : all) {
        if (c == L'\n') {
            if (!cur.empty() && cur.back() == L'\r') cur.pop_back();
            lines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) lines.push_back(cur);

    std::vector<Entry> newEntries;
    std::wstring debtor, cash;
    bool inData = false;
    int skippedLines = 0;
    for (auto& line : lines) {
        if (line.rfind(L"DEBTOR=", 0) == 0) {
            debtor = line.substr(7);
        } else if (line.rfind(L"CASH=", 0) == 0) {
            cash = line.substr(5);
        } else if (line == L"BEGIN") {
            inData = true;
        } else if (line == L"END") {
            inData = false;
        } else if (inData && !line.empty()) {
            std::vector<std::wstring> parts;
            std::wstring c2;
            for (wchar_t ch : line) {
                if (ch == L'|') { parts.push_back(c2); c2.clear(); }
                else c2.push_back(ch);
            }
            parts.push_back(c2);
            if (parts.size() == 4 || parts.size() == 6) {
                Entry e;
                e.supplier = parts[0];
                e.product = parts[1];
                try { e.kgs = std::stod(parts[2]); } catch (...) { e.kgs = 0; }
                try { e.price = std::stod(parts[3]); } catch (...) { e.price = 0; }
                if (parts.size() == 6) {
                    e.date = parts[4];
                    e.notes = parts[5];
                }
                // parts.size() == 4 means a file saved before dates/notes
                // existed - e.date and e.notes just stay empty, handled
                // gracefully everywhere they're displayed.
                newEntries.push_back(e);
            } else {
                // A field containing '|' (possible in files saved before
                // this character was blocked at entry time) splits into the
                // wrong number of parts and can't be safely reconstructed -
                // skip it, but don't lose it silently; the caller is told.
                skippedLines++;
            }
        }
    }
    g_entries = newEntries;
    SetWindowTextW(hEditDebtor, debtor.c_str());
    SetWindowTextW(hEditCash, cash.c_str());
    if (skippedLines > 0 && g_hMainWnd) {
        std::wstring msg = L"Warning: " + std::to_wstring(skippedLines) +
            (skippedLines == 1 ? L" row could" : L" rows could") +
            L" not be read from this file and " + (skippedLines == 1 ? L"was" : L"were") +
            L" skipped (likely an old file saved with a '|' character in a Supplier or Species name).";
        MessageBoxW(g_hMainWnd, msg.c_str(), L"Some Rows Skipped", MB_OK | MB_ICONWARNING);
    }
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

    wchar_t buf[512];
    GetWindowTextW(hEditDebtor, buf, 512);
    double debtor = ParseSumExpr(buf);
    GetWindowTextW(hEditCash, buf, 512);
    double cash = ParseSumExpr(buf);
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
void PopulateGroupedTotalsList(HWND lv, bool bySupplier) {
    ListView_DeleteAllItems(lv);
    std::vector<std::wstring> keys;
    std::map<std::wstring, double> kgsSum, amtSum;
    for (auto& e : g_entries) {
        const std::wstring& k = bySupplier ? e.supplier : e.product;
        if (kgsSum.find(k) == kgsSum.end()) {
            kgsSum[k] = 0;
            amtSum[k] = 0;
            keys.push_back(k);
        }
        kgsSum[k] += e.kgs;
        amtSum[k] += e.Total();
    }
    std::sort(keys.begin(), keys.end());

    double grandKgs = 0, grandAmt = 0;
    int idx = 0;
    for (auto& k : keys) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = idx;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(k.c_str());
        ListView_InsertItem(lv, &item);
        std::wstring kgsS = FormatKg(kgsSum[k]);
        ListView_SetItemText(lv, idx, 1, const_cast<LPWSTR>(kgsS.c_str()));
        std::wstring amtS = FormatMoney(amtSum[k]);
        ListView_SetItemText(lv, idx, 2, const_cast<LPWSTR>(amtS.c_str()));
        grandKgs += kgsSum[k];
        grandAmt += amtSum[k];
        idx++;
    }
    LVITEMW gitem{};
    gitem.mask = LVIF_TEXT;
    gitem.iItem = idx;
    gitem.iSubItem = 0;
    std::wstring lbl = L"GRAND TOTAL";
    gitem.pszText = const_cast<LPWSTR>(lbl.c_str());
    ListView_InsertItem(lv, &gitem);
    std::wstring gk = FormatKg(grandKgs);
    ListView_SetItemText(lv, idx, 1, const_cast<LPWSTR>(gk.c_str()));
    std::wstring ga = FormatMoney(grandAmt);
    ListView_SetItemText(lv, idx, 2, const_cast<LPWSTR>(ga.c_str()));
}

void RefreshOverviewList() { PopulateGroupedTotalsList(hListOverview, true); }

// By Species gets its own richer view (unlike Overview, which stays a plain
// Kgs/Total breakdown per supplier): average, highest, and lowest price seen
// for each species, alongside the usual Kgs/Total.
void RefreshBySpeciesList() {
    ListView_DeleteAllItems(hListBySpecies);
    std::vector<std::wstring> keys;
    std::map<std::wstring, double> kgsSum, amtSum, priceSum, priceMax, priceMin;
    std::map<std::wstring, int> priceCount;
    for (auto& e : g_entries) {
        const std::wstring& k = e.product;
        if (kgsSum.find(k) == kgsSum.end()) {
            kgsSum[k] = 0;
            amtSum[k] = 0;
            priceSum[k] = 0;
            priceCount[k] = 0;
            priceMax[k] = e.price;
            priceMin[k] = e.price;
            keys.push_back(k);
        }
        kgsSum[k] += e.kgs;
        amtSum[k] += e.Total();
        priceSum[k] += e.price;
        priceCount[k]++;
        if (e.price > priceMax[k]) priceMax[k] = e.price;
        if (e.price < priceMin[k]) priceMin[k] = e.price;
    }
    std::sort(keys.begin(), keys.end());

    double grandKgs = 0, grandAmt = 0;
    int idx = 0;
    for (auto& k : keys) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = idx;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(k.c_str());
        ListView_InsertItem(hListBySpecies, &item);
        std::wstring kgsS = FormatKg(kgsSum[k]);
        ListView_SetItemText(hListBySpecies, idx, 1, const_cast<LPWSTR>(kgsS.c_str()));
        std::wstring amtS = FormatMoney(amtSum[k]);
        ListView_SetItemText(hListBySpecies, idx, 2, const_cast<LPWSTR>(amtS.c_str()));
        double avg = priceCount[k] > 0 ? priceSum[k] / priceCount[k] : 0;
        std::wstring avgS = FormatMoney(avg);
        ListView_SetItemText(hListBySpecies, idx, 3, const_cast<LPWSTR>(avgS.c_str()));
        std::wstring maxS = FormatMoney(priceMax[k]);
        ListView_SetItemText(hListBySpecies, idx, 4, const_cast<LPWSTR>(maxS.c_str()));
        std::wstring minS = FormatMoney(priceMin[k]);
        ListView_SetItemText(hListBySpecies, idx, 5, const_cast<LPWSTR>(minS.c_str()));
        grandKgs += kgsSum[k];
        grandAmt += amtSum[k];
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
    std::wstring gk = FormatKg(grandKgs);
    ListView_SetItemText(hListBySpecies, idx, 1, const_cast<LPWSTR>(gk.c_str()));
    std::wstring ga = FormatMoney(grandAmt);
    ListView_SetItemText(hListBySpecies, idx, 2, const_cast<LPWSTR>(ga.c_str()));
}

// Shared grouping structures used both by the on-screen Breakdown list and by
// the printed/PDF report, so the two can never drift out of sync.
struct PriceLine { double price; double kgs; double amt; };
struct ProductGroup {
    std::wstring species;
    std::vector<PriceLine> prices;
    double totalKgs = 0, totalAmt = 0;
};
struct SupplierGroup {
    std::wstring supplier;
    std::vector<ProductGroup> products;
    double totalKgs = 0, totalAmt = 0;
};

std::vector<SupplierGroup> BuildBreakdownData() {
    std::vector<SupplierGroup> result;

    std::vector<std::wstring> suppliers;
    for (auto& e : g_entries)
        if (std::find(suppliers.begin(), suppliers.end(), e.supplier) == suppliers.end())
            suppliers.push_back(e.supplier);
    std::sort(suppliers.begin(), suppliers.end());

    for (auto& sup : suppliers) {
        SupplierGroup sg;
        sg.supplier = sup;

        std::vector<std::wstring> products;
        for (auto& e : g_entries)
            if (e.supplier == sup && std::find(products.begin(), products.end(), e.product) == products.end())
                products.push_back(e.product);
        std::sort(products.begin(), products.end());

        for (auto& prod : products) {
            ProductGroup pg;
            pg.species = prod;

            std::vector<double> prices;
            std::map<double, double> kgsByPrice, amtByPrice;
            for (auto& e : g_entries) {
                if (e.supplier == sup && e.product == prod) {
                    if (kgsByPrice.find(e.price) == kgsByPrice.end()) {
                        kgsByPrice[e.price] = 0;
                        amtByPrice[e.price] = 0;
                        prices.push_back(e.price);
                    }
                    kgsByPrice[e.price] += e.kgs;
                    amtByPrice[e.price] += e.Total();
                }
            }
            std::sort(prices.begin(), prices.end());
            for (double p : prices) {
                pg.prices.push_back({ p, kgsByPrice[p], amtByPrice[p] });
                pg.totalKgs += kgsByPrice[p];
                pg.totalAmt += amtByPrice[p];
            }
            sg.products.push_back(pg);
            sg.totalKgs += pg.totalKgs;
            sg.totalAmt += pg.totalAmt;
        }
        result.push_back(sg);
    }
    return result;
}

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

    auto data = BuildBreakdownData();

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

void RefreshAll() {
    RefreshEntriesList();
    RefreshCombos();
    RecalcTotals();
    RefreshOverviewList();
    RefreshBySpeciesList();
    RefreshBreakdownList();
    SaveToFile(GetExeDir() + L"\\autosave.fbd"); // silent autosave, never blocks the UI
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void CommitEntryForm() {
    wchar_t buf[256];
    GetWindowTextW(hCmbSupplier, buf, 256);
    std::wstring supplier = TrimW(buf);
    GetWindowTextW(hCmbProduct, buf, 256);
    std::wstring product = TrimW(buf);
    GetWindowTextW(hEditKgs, buf, 256);
    std::wstring kgsStr = buf;
    GetWindowTextW(hEditPrice, buf, 256);
    std::wstring priceStr = buf;
    GetWindowTextW(hEditNotes, buf, 256);
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

    if (g_editIndex >= 0 && g_editIndex < (int)g_entries.size()) {
        // Updating an existing row.
        Entry& e = g_entries[g_editIndex];
        e.supplier = supplier;
        e.product = product;
        e.kgs = kgs;
        e.price = price;
        e.date = date;
        e.notes = notes;
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
        g_entries.push_back(e);
    }

    SetWindowTextW(hEditKgs, L"");
    SetWindowTextW(hEditPrice, L"");
    SetWindowTextW(hEditNotes, L"");
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

void DeleteSelectedEntry() {
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

    g_undoEntry = e;
    g_undoIndex = sel;
    g_hasUndo = true;
    if (g_hEditMenu) EnableMenuItem(g_hEditMenu, ID_EDIT_UNDO_DELETE, MF_BYCOMMAND | MF_ENABLED);

    g_entries.erase(g_entries.begin() + sel);
    // Deleting shifts every later index down by one, and may remove the
    // row currently loaded in the form - simplest and safest is to just
    // drop out of edit mode rather than try to track the shift.
    if (g_editIndex != -1) CancelEdit();
    RefreshAll();
}

void UndoDelete() {
    if (!g_hasUndo) return;
    int idx = g_undoIndex;
    if (idx < 0) idx = 0;
    if (idx > (int)g_entries.size()) idx = (int)g_entries.size();
    g_entries.insert(g_entries.begin() + idx, g_undoEntry);
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

void EditSelectedEntry() {
    int selRow = ListView_GetNextItem(hListEntries, -1, LVNI_SELECTED);
    if (selRow < 0) {
        MessageBoxW(g_hMainWnd, L"Select a row to edit first (or double-click it).", L"No Selection", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (selRow < 0 || selRow >= (int)g_filteredIndices.size()) return;
    LoadEntryIntoForm(g_filteredIndices[selRow]);
}

void DoFileNew() {
    if (!g_entries.empty()) {
        int r = MessageBoxW(g_hMainWnd, L"Discard the current data and start a new sheet?", L"New",
                             MB_YESNO | MB_ICONQUESTION);
        if (r != IDYES) return;
    }
    g_entries.clear();
    SetWindowTextW(hEditDebtor, L"");
    SetWindowTextW(hEditCash, L"");
    g_currentFile.clear();
    CancelEdit();
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
        if (LoadFromFile(file)) {
            g_currentFile = file;
            CancelEdit();
            UpdateTitle();
            RefreshAll();
            RememberRecentFile(file);
        } else {
            MessageBoxW(g_hMainWnd, L"Could not open the selected file.", L"Error", MB_OK | MB_ICONERROR);
        }
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
        if (SaveToFile(file)) {
            g_currentFile = file;
            UpdateTitle();
            RememberRecentFile(file);
            MessageBoxW(g_hMainWnd, L"Saved.", L"Save", MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(g_hMainWnd, L"Could not save the file.", L"Error", MB_OK | MB_ICONERROR);
        }
    }
}

void DoFileSave() {
    if (g_currentFile.empty()) { DoFileSaveAs(); return; }
    if (!SaveToFile(g_currentFile))
        MessageBoxW(g_hMainWnd, L"Could not save the file.", L"Error", MB_OK | MB_ICONERROR);
}

// ---------------------------------------------------------------------------
// CSV export (Excel-readable) - bundles every report into one file.
// ---------------------------------------------------------------------------

std::wstring CsvField(const std::wstring& s) {
    std::wstring field = s;
    // Excel treats a field starting with =, +, -, or @ as a formula, which
    // is a known CSV-injection vector when the field came from free-text
    // user input. Prefix with a tab to neutralize it as plain text while
    // keeping it readable (a leading apostrophe would be visible in the
    // cell; a tab is not).
    if (!field.empty() && (field[0] == L'=' || field[0] == L'+' || field[0] == L'-' || field[0] == L'@'))
        field = L"\t" + field;

    bool needQuote = field.find(L',') != std::wstring::npos || field.find(L'"') != std::wstring::npos ||
                     field.find(L'\n') != std::wstring::npos;
    if (!needQuote) return field;
    std::wstring out = L"\"";
    for (wchar_t c : field) {
        if (c == L'"') out += L"\"\"";
        else out.push_back(c);
    }
    out += L"\"";
    return out;
}

bool ExportToCsv(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;

    unsigned char bom[3] = { 0xEF, 0xBB, 0xBF }; // UTF-8 BOM, so Excel reads accents/symbols correctly
    fwrite(bom, 1, 3, f);

    auto writeLine = [&](const std::wstring& line) {
        std::string u8 = WToUtf8(line) + "\r\n";
        fwrite(u8.data(), 1, u8.size(), f);
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
        auto data = BuildBreakdownData();
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

    fclose(f);
    return true;
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
        if (ExportToCsv(file))
            MessageBoxW(g_hMainWnd, L"Exported. This file opens directly in Excel.", L"Export to CSV", MB_OK | MB_ICONINFORMATION);
        else
            MessageBoxW(g_hMainWnd, L"Could not export the file.", L"Error", MB_OK | MB_ICONERROR);
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
        L"3. See a supplier -> species -> price breakdown with weights on the Breakdown tab.\n\n"
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
        L"Use the Filter box above the entries list to find rows quickly, or click a column header "
        L"to sort by it (click again to reverse). Deleting a row asks for confirmation, and "
        L"Edit > Undo Delete brings back the last one you removed.\n\n"
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
    SaveSupplierEmails();

    PopulateManageList();
    // Re-select the same supplier so the email field stays populated.
    for (size_t i = 0; i < g_manageValues.size(); i++) {
        if (g_manageValues[i] == supplier) { SendMessageW(g_hManageList, LB_SETSEL, TRUE, (LPARAM)i); break; }
    }
    UpdateManageEmailControls();
    MessageBoxW(g_hManageWnd, email.empty() ? L"Email cleared." : L"Email saved.", L"Done", MB_OK | MB_ICONINFORMATION);
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

    int changed = 0;
    for (auto& e : g_entries) {
        std::wstring& field = g_manageIsSupplier ? e.supplier : e.product;
        for (auto& src : sourceNames) {
            if (field == src && field != target) { field = target; changed++; break; }
        }
    }

    if (g_editIndex != -1) CancelEdit();
    RefreshAll();
    PopulateManageList();
    SetWindowTextW(g_hManageTarget, L"");

    std::wstring msg = L"Updated " + std::to_wstring(changed) + (changed == 1 ? L" entry." : L" entries.");
    MessageBoxW(g_hManageWnd, msg.c_str(), L"Done", MB_OK | MB_ICONINFORMATION);
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

        SendMessageW(g_hManageRadioSupplier, BM_SETCHECK, BST_CHECKED, 0);
        g_manageIsSupplier = true;
        PopulateManageList();
        UpdateManageEmailControls();
        return 0;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        MoveWindow(g_hManageRadioSupplier, S(15), S(15), S(100), S(24), TRUE);
        MoveWindow(g_hManageRadioSpecies, S(125), S(15), S(100), S(24), TRUE);
        MoveWindow(g_hManageHint, S(15), S(45), rc.right - S(30), S(22), TRUE);

        int listBottom = rc.bottom - S(200);
        if (listBottom < S(110)) listBottom = S(110);
        MoveWindow(g_hManageList, S(15), S(72), rc.right - S(30), listBottom - S(72), TRUE);

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
            return 0;
        }
        if (id == ID_MNG_LIST && code == LBN_SELCHANGE) {
            UpdateManageEmailControls();
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
        wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
        lf.lfFaceName[LF_FACESIZE - 1] = 0;
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
            wchar_t buf[512];
            GetWindowTextW(hEditDebtor, buf, 512);
            double debtor = ParseSumExpr(buf);
            GetWindowTextW(hEditCash, buf, 512);
            double cash = ParseSumExpr(buf);
            double book = debtor + cash;
            double diff = book - entered;
            bool ok = std::abs(diff) < 0.005;

            std::wstring l1 = L"Book Total (Debtor + Cash): " + FormatMoney(book);
            std::wstring l2 = L"Entered Total: " + FormatMoney(entered);
            std::wstring l3 = L"Difference: " + FormatMoney(diff) + (ok ? L"  (OK - balanced)" : L"  (OUT OF BALANCE)");
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

    auto data = BuildBreakdownData();
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
    // fall back to a generic Letter-sized page so preview still works.
    int pageWidthPx, pageHeightPx, dpiX, dpiY;
    bool gotPrinter = false;
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
            gotPrinter = true;
        }
    }
    if (!gotPrinter) {
        dpiX = dpiY = 100;
        pageWidthPx = 850;  // 8.5" at 100dpi
        pageHeightPx = 1100; // 11" at 100dpi
    }

    FreeRenderedPages(g_previewPages);
    g_previewPages = RenderReportPages(pageWidthPx, pageHeightPx, dpiX, dpiY);
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

std::wstring GreetingForNow() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (st.wHour < 12) return L"Good morning";
    if (st.wHour < 17) return L"Good afternoon";
    return L"Good evening"; // covers evening hours too, beyond just morning/afternoon
}

std::wstring TodayDateString() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    static const wchar_t* months[] = { L"January", L"February", L"March", L"April", L"May", L"June",
                                        L"July", L"August", L"September", L"October", L"November", L"December" };
    std::wstring m = (st.wMonth >= 1 && st.wMonth <= 12) ? months[st.wMonth - 1] : L"";
    return std::to_wstring(st.wDay) + L" " + m + L" " + std::to_wstring(st.wYear);
}

// Percent-encodes text for use in a mailto: URL (subject/body query values).
// Operates on the UTF-8 bytes so non-ASCII characters survive intact.
std::wstring UrlEncodeForMailto(const std::wstring& text) {
    std::string utf8 = WToUtf8(text);
    std::wstring out;
    const wchar_t* hex = L"0123456789ABCDEF";
    for (unsigned char c : utf8) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((wchar_t)c);
        } else {
            out.push_back(L'%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

// Very basic sanity check - not full RFC validation, just enough to catch
// obvious typos before they get saved.
bool LooksLikeEmail(const std::wstring& email) {
    if (email.find(L' ') != std::wstring::npos) return false;
    size_t at = email.find(L'@');
    if (at == std::wstring::npos || at == 0 || at == email.size() - 1) return false;
    size_t dot = email.find(L'.', at);
    if (dot == std::wstring::npos || dot == email.size() - 1) return false;
    return true;
}

// Pads a string with trailing spaces to at least `width` characters, for a
// best-effort aligned plain-text table (exact alignment isn't guaranteed in
// every email client, since not all render plain text in a fixed-width
// font, but this matches in the common case).
std::wstring PadRight(const std::wstring& s, size_t width) {
    std::wstring out = s;
    while (out.size() < width) out.push_back(L' ');
    return out;
}

std::wstring BuildSupplierEmailBody(const SupplierGroup& sg) {
    std::wstring body = GreetingForNow() + L",\r\n\r\n";
    body += L"Please see prices below\r\n";
    body += PadRight(L"KG", 7) + PadRight(L"Species", 18) + L"Price\r\n";
    for (auto& pg : sg.products) {
        for (auto& pl : pg.prices) {
            body += PadRight(FormatKg(pl.kgs), 7) + PadRight(pg.species, 18) + FormatMoney(pl.price) + L"\r\n";
        }
    }
    body += L"\r\nKind regards,";

    // mailto: bodies are practically capped well under the URL length some
    // mail clients/OS versions tolerate - if a supplier has an unusually
    // long list of line items, fall back to a shorter summary instead of
    // risking a mailto that silently fails to open or gets truncated.
    if (body.size() > 1500) {
        body = GreetingForNow() + L",\r\n\r\nPlease see prices below - " +
               std::to_wstring(sg.products.size()) + L" species, total weight " +
               FormatKg(sg.totalKgs) + L" kg.\r\n\r\n(Full line-by-line pricing is in the app - "
               L"this summary was shortened because the full list was too long for email.)\r\n\r\n"
               L"Kind regards,";
    }
    return body;
}

bool EmailSupplier(const std::wstring& email, const SupplierGroup& sg) {
    std::wstring subject = L"Delivery Summary - " + TodayDateString();
    if (email.empty()) subject += L" - " + sg.supplier; // no To address to identify the draft by, so name it in the subject
    std::wstring body = BuildSupplierEmailBody(sg);
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
    auto data = BuildBreakdownData();
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
    MoveWindow(hTab, 0, 0, rc.right, rc.bottom, TRUE);

    RECT disp = rc;
    TabCtrl_AdjustRect(hTab, FALSE, &disp);

    int left = disp.left + S(10);
    int top = disp.top + S(10);
    int right = disp.right - S(10);

    // ---- Tab 1 ----
    MoveWindow(hLblSupplier, left, top + S(3), S(65), S(22), TRUE);
    MoveWindow(hCmbSupplier, left + S(70), top, S(150), S(200), TRUE);
    MoveWindow(hLblProduct, left + S(230), top + S(3), S(55), S(22), TRUE);
    MoveWindow(hCmbProduct, left + S(290), top, S(150), S(200), TRUE);
    MoveWindow(hLblKgs, left + S(450), top + S(3), S(35), S(22), TRUE);
    MoveWindow(hEditKgs, left + S(488), top, S(70), S(22), TRUE);
    MoveWindow(hLblPrice, left + S(568), top + S(3), S(45), S(22), TRUE);
    MoveWindow(hEditPrice, left + S(616), top, S(70), S(22), TRUE);
    MoveWindow(hBtnAdd, left + S(700), top - S(2), S(110), S(28), TRUE);

    int row1b = top + S(36);
    MoveWindow(hLblDate, left, row1b + S(3), S(40), S(22), TRUE);
    MoveWindow(hDtpDate, left + S(45), row1b, S(140), S(22), TRUE);
    MoveWindow(hLblNotes, left + S(200), row1b + S(3), S(45), S(22), TRUE);
    MoveWindow(hEditNotes, left + S(248), row1b, S(340), S(22), TRUE);
    MoveWindow(hBtnDuplicate, left + S(600), row1b - S(2), S(190), S(28), TRUE);

    int row2 = row1b + S(36);
    MoveWindow(hBtnDelete, left, row2, S(180), S(28), TRUE);
    MoveWindow(hBtnEdit, left + S(190), row2, S(160), S(28), TRUE);
    MoveWindow(hBtnCancelEdit, left + S(360), row2, S(160), S(28), TRUE);
    MoveWindow(hLblFilter, left + S(535), row2 + S(4), S(40), S(22), TRUE);
    MoveWindow(hEditFilter, left + S(580), row2 + S(2), S(220), S(22), TRUE);

    int listTop = row2 + S(40);
    int reconHeight = S(135);
    int listBottom = disp.bottom - reconHeight - S(10);
    if (listBottom < listTop + S(60)) listBottom = listTop + S(60);
    MoveWindow(hListEntries, left, listTop, right - left, listBottom - listTop, TRUE);

    int reconTop = listBottom + S(12);
    MoveWindow(hGrpRecon, left, reconTop, right - left, disp.bottom - reconTop - S(5), TRUE);
    MoveWindow(hLblDebtor, left + S(15), reconTop + S(26), S(130), S(22), TRUE);
    MoveWindow(hEditDebtor, left + S(150), reconTop + S(24), S(260), S(22), TRUE);
    MoveWindow(hLblCash, left + S(430), reconTop + S(26), S(65), S(22), TRUE);
    MoveWindow(hEditCash, left + S(495), reconTop + S(24), S(260), S(22), TRUE);
    MoveWindow(hLblBook, left + S(15), reconTop + S(58), S(320), S(22), TRUE);
    MoveWindow(hLblEntered, left + S(345), reconTop + S(58), S(320), S(22), TRUE);
    MoveWindow(hLblDiff, left + S(15), reconTop + S(84), S(460), S(24), TRUE);

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
        hLblSupplier = MakeControl(L"STATIC", L"Supplier:", WS_VISIBLE, 0, hwnd);
        hCmbSupplier = MakeControl(L"COMBOBOX", L"", WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWN | WS_TABSTOP, ID_CMB_SUPPLIER, hwnd);
        hLblProduct = MakeControl(L"STATIC", L"Species:", WS_VISIBLE, 0, hwnd);
        hCmbProduct = MakeControl(L"COMBOBOX", L"", WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWN | WS_TABSTOP, ID_CMB_PRODUCT, hwnd);
        hLblKgs = MakeControl(L"STATIC", L"Kgs:", WS_VISIBLE, 0, hwnd);
        hEditKgs = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP, ID_EDIT_KGS, hwnd);
        hLblPrice = MakeControl(L"STATIC", L"Price:", WS_VISIBLE, 0, hwnd);
        hEditPrice = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP, ID_EDIT_PRICE, hwnd);
        hBtnAdd = MakeControl(L"BUTTON", L"Add Entry", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_DEFPUSHBUTTON, ID_BTN_ADD, hwnd);
        hBtnDelete = MakeControl(L"BUTTON", L"Delete Selected Row", WS_VISIBLE | WS_TABSTOP, ID_BTN_DELETE, hwnd);
        hBtnEdit = MakeControl(L"BUTTON", L"Edit Selected Row", WS_VISIBLE | WS_TABSTOP, ID_BTN_EDIT, hwnd);
        hBtnCancelEdit = MakeControl(L"BUTTON", L"Clear / Cancel Edit", WS_VISIBLE | WS_TABSTOP, ID_BTN_CANCEL_EDIT, hwnd);
        hBtnDuplicate = MakeControl(L"BUTTON", L"Duplicate Last Entry", WS_VISIBLE | WS_TABSTOP, ID_BTN_DUPLICATE, hwnd);

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
        hLblBook = MakeControl(L"STATIC", L"Book Total: $0.00", WS_VISIBLE, 0, hwnd);
        hLblEntered = MakeControl(L"STATIC", L"Entered Total: $0.00", WS_VISIBLE, 0, hwnd);
        hLblDiff = MakeControl(L"STATIC", L"Difference: $0.00", WS_VISIBLE, 0, hwnd);

        // Tab 2
        hLblOvBook = MakeControl(L"STATIC", L"Book Total: $0.00", WS_VISIBLE, 0, hwnd);
        hLblOvGrand = MakeControl(L"STATIC", L"Grand Total: $0.00", WS_VISIBLE, 0, hwnd);
        hLblOvDiff = MakeControl(L"STATIC", L"Difference: $0.00", WS_VISIBLE, 0, hwnd);
        hListOverview = MakeControl(L"SysListView32", L"", WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER, ID_LIST_OVERVIEW, hwnd);
        ListView_SetExtendedListViewStyle(hListOverview, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        AddColumn(hListOverview, 0, L"Supplier", 280);
        AddColumn(hListOverview, 1, L"Total ($)", 160);

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
                         hLblDate, hDtpDate, hLblNotes, hEditNotes,
                         hLblFilter, hEditFilter, hListEntries, hGrpRecon,
                         hLblDebtor, hEditDebtor, hLblCash, hEditCash, hLblBook, hLblEntered, hLblDiff };
        g_tab2Ctrls = { hLblOvBook, hLblOvGrand, hLblOvDiff, hListOverview };
        g_tab3Ctrls = { hBtnPrintPreview, hBtnPrintBreakdown, hBtnEmailSuppliers, hListBreakdown };
        g_tab4Ctrls = { hListBySpecies };

        LayoutAll(hwnd);
        ShowTab(0);

        // Load persisted window settings/recent files (the window itself is
        // already sized from these via CreateWindowExW in wWinMain).
        LoadRecentFiles();
        RebuildRecentMenu();
        LoadSupplierEmails();
        if (g_hEditMenu) EnableMenuItem(g_hEditMenu, ID_EDIT_UNDO_DELETE, MF_BYCOMMAND | MF_GRAYED);

        // Silently reload whatever was last auto-saved next to the exe.
        LoadFromFile(GetExeDir() + L"\\autosave.fbd");
        // Restore the association with the last named file (if any) so
        // Save/title bar refer to it, without overwriting the freshly
        // reloaded autosave content.
        if (!g_settings.lastFile.empty()) g_currentFile = g_settings.lastFile;
        RefreshAll();
        UpdateTitle();
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
        if (hdr->hwndFrom == hListEntries && hdr->code == (UINT)NM_DBLCLK) {
            LPNMITEMACTIVATE nia = (LPNMITEMACTIVATE)lParam;
            if (nia->iItem >= 0 && nia->iItem < (int)g_filteredIndices.size())
                LoadEntryIntoForm(g_filteredIndices[nia->iItem]);
            return 0;
        }
        if (hdr->hwndFrom == hListEntries && hdr->code == (UINT)LVN_COLUMNCLICK) {
            LPNMLISTVIEW nmlv = (LPNMLISTVIEW)lParam;
            SortEntriesBy(nmlv->iSubItem);
            return 0;
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

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id >= ID_RECENT_BASE && id < (int)(ID_RECENT_BASE + kMaxRecentFiles)) {
            size_t idx = (size_t)(id - ID_RECENT_BASE);
            if (idx < g_recentFiles.size()) {
                std::wstring path = g_recentFiles[idx];
                if (LoadFromFile(path)) {
                    g_currentFile = path;
                    CancelEdit();
                    UpdateTitle();
                    RefreshAll();
                    RememberRecentFile(path); // move to front
                } else {
                    MessageBoxW(g_hMainWnd, L"Could not open that file (it may have been moved or deleted).", L"Error", MB_OK | MB_ICONERROR);
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
        case ID_EDIT_FILTER:
            if (code == EN_CHANGE) RefreshEntriesList();
            return 0;
        case ID_CMB_SUPPLIER:
            if (code == CBN_EDITCHANGE) ComboAutoComplete(hCmbSupplier, g_prevSupplierLen);
            return 0;
        case ID_CMB_PRODUCT:
            if (code == CBN_EDITCHANGE) ComboAutoComplete(hCmbProduct, g_prevProductLen);
            return 0;
        case ID_EDIT_DEBTOR:
        case ID_EDIT_CASH:
            if (code == EN_CHANGE) RecalcTotals();
            return 0;
        case ID_FILE_NEW: DoFileNew(); return 0;
        case ID_FILE_OPEN: DoFileOpen(); return 0;
        case ID_FILE_SAVE: DoFileSave(); return 0;
        case ID_FILE_SAVEAS: DoFileSaveAs(); return 0;
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

    case WM_DESTROY:
        SaveSettings();
        SaveToFile(GetExeDir() + L"\\autosave.fbd");
        if (g_normalFont) DeleteObject(g_normalFont);
        if (g_boldFont) DeleteObject(g_boldFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

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

    HMENU hMenu = CreateMenu();
    HMENU hFileMenu = CreatePopupMenu();
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_NEW, L"&New");
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_OPEN, L"&Open...");
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_SAVE, L"&Save");
    AppendMenuW(hFileMenu, MF_STRING, ID_FILE_SAVEAS, L"Save &As...");
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

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Fish Balance Manager",
        WS_OVERLAPPEDWINDOW, g_settings.x, g_settings.y, g_settings.w, g_settings.h,
        nullptr, hMenu, hInstance, nullptr);

    ShowWindow(hwnd, g_settings.maximized ? SW_SHOWMAXIMIZED : nCmdShow);
    UpdateWindow(hwnd);

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
