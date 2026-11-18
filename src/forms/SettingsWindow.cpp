// SettingsWindow.cpp - see SettingsWindow.h. Same popup pattern as the Finalize Day prompt: own window class,
// owner disabled while open, re-enabled in WM_DESTROY. All sizes go through S() (DPI), per ARCHITECTURE.md.
#include "SettingsWindow.h"
#include "ControlIds.h"
#include "MainWindowApi.h"
#include "PopupPlacement.h"
#include "UiState.h"
#include "../PlatformWin.h"
#include "../data/AppData.h"
#include "../data/Storage.h"
#include "../services/GmailDraft.h"
#include "../../version.h"
#include <shellapi.h>
#include <string>
#include <vector>

namespace {

HWND g_hSettingsWnd = nullptr;
HWND g_hSetTab = nullptr;
std::vector<HWND> g_pages[4]; // controls on each tab page
HWND g_hSubject, g_hIntro, g_hSignoff, g_hGmailAccount, g_hBackupMin, g_hBackupKeep;
HWND g_hPrinter, g_hOrient, g_hPaper;
std::vector<std::wstring> g_printerNames; // index i in the printer combo = i-1 here (item 0 is "Windows default printer")

HWND Add(int page, LPCWSTR cls, LPCWSTR text, DWORD style, int id, HWND parent, int x, int y, int w, int h) {
    HWND c = MakeControl(cls, text, style, id, parent);
    if (c) {
        MoveWindow(c, S(x), S(y), S(w), S(h), TRUE);
        g_pages[page].push_back(c);
    }
    return c;
}

void ShowPage(int page) {
    for (int p = 0; p < 4; p++)
        for (HWND c : g_pages[p]) ShowWindow(c, p == page ? SW_SHOW : SW_HIDE);
}

std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}

// Parses a whole-number edit box; false if empty / not a number / outside [lo, hi].
bool ReadIntInRange(HWND edit, int lo, int hi, int* out) {
    std::wstring t = TrimW(GetText(edit));
    if (t.empty() || t.size() > 6) return false;
    for (wchar_t ch : t) if (ch < L'0' || ch > L'9') return false;
    int v = _wtoi(t.c_str());
    if (v < lo || v > hi) return false;
    *out = v;
    return true;
}

void SwitchToPage(int page) {
    TabCtrl_SetCurSel(g_hSetTab, page);
    ShowPage(page);
}

bool ApplyAndSave(HWND hwnd) {
    int minutes = 0, keep = 0;
    if (!ReadIntInRange(g_hBackupMin, 1, 60, &minutes)) {
        SwitchToPage(1);
        MessageBoxW(hwnd, L"The backup interval must be a whole number of minutes from 1 to 60.", L"Settings", MB_OK | MB_ICONWARNING);
        SetFocus(g_hBackupMin);
        return false;
    }
    if (!ReadIntInRange(g_hBackupKeep, 10, 500, &keep)) {
        SwitchToPage(1);
        MessageBoxW(hwnd, L"The number of backup snapshots to keep must be a whole number from 10 to 500.", L"Settings", MB_OK | MB_ICONWARNING);
        SetFocus(g_hBackupKeep);
        return false;
    }

    EmailMode mode = EmailMode::Mailto;
    if (IsDlgButtonChecked(hwnd, ID_SET_EMAIL_EML) == BST_CHECKED) mode = EmailMode::Eml;
    else if (IsDlgButtonChecked(hwnd, ID_SET_EMAIL_GMAIL) == BST_CHECKED) mode = EmailMode::Gmail;
    int sens = 1;
    if (IsDlgButtonChecked(hwnd, ID_SET_SENS_RELAXED) == BST_CHECKED) sens = 0;
    else if (IsDlgButtonChecked(hwnd, ID_SET_SENS_STRICT) == BST_CHECKED) sens = 2;

    g_settings.emailMode = mode;
    g_settings.gmailAccount = TrimW(GetText(g_hGmailAccount));
    // Blank = default wording. Stored cleaned (single line); a value equal to the default is stored as blank.
    const EmailWording def;
    auto keepIfCustom = [](const std::wstring& raw, const std::wstring& fallback) {
        std::wstring c = CleanWordingLine(raw, fallback);
        return c == fallback ? std::wstring() : c;
    };
    g_settings.emailSubject = keepIfCustom(GetText(g_hSubject), def.subject);
    g_settings.emailIntro = keepIfCustom(GetText(g_hIntro), def.intro);
    g_settings.emailSignoff = keepIfCustom(GetText(g_hSignoff), def.signoff);
    const int oldText = ClampTextPercent(g_settings.textPercent);
    g_settings.textPercent = IsDlgButtonChecked(hwnd, ID_SET_TEXT_125) == BST_CHECKED ? 125
                           : (IsDlgButtonChecked(hwnd, ID_SET_TEXT_150) == BST_CHECKED ? 150 : 100);
    g_settings.dateStyle = IsDlgButtonChecked(hwnd, ID_SET_DATE_SHORT) == BST_CHECKED ? 1
                         : (IsDlgButtonChecked(hwnd, ID_SET_DATE_NUM) == BST_CHECKED ? 2 : 0);
    {
        const int ps = (int)SendMessageW(g_hPrinter, CB_GETCURSEL, 0, 0);
        // Item 0 = "Windows default printer" = blank. If the PC has no printers the list is just that one item.
        g_settings.printer = (ps >= 1 && (size_t)(ps - 1) < g_printerNames.size()) ? g_printerNames[(size_t)ps - 1] : std::wstring();
        const int pp = (int)SendMessageW(g_hPaper, CB_GETCURSEL, 0, 0);
        const int po = (int)SendMessageW(g_hOrient, CB_GETCURSEL, 0, 0);
        g_settings.printPaper = ClampPrintPaper(pp < 0 ? 0 : pp);
        g_settings.printOrientation = ClampPrintOrientation(po < 0 ? 0 : po);
    }
    g_settings.backupMinutes = minutes;
    g_settings.backupKeep = keep;

    const bool textChanged = (ClampTextPercent(g_settings.textPercent) != oldText);
    const bool sensChanged = (sens != g_settings.outlierSensitivity);
    g_settings.outlierSensitivity = sens;
    ApplyOutlierSensitivitySetting();

    bool flagsChanged = false;
    if (sensChanged) {
        // Re-judge every entry on the sheet against the new fence, so flags match the new sensitivity straight away.
        std::vector<bool> before;
        for (const auto& e : g_entries) before.push_back(e.priceFlagged);
        std::vector<std::wstring> done;
        for (size_t i = 0; i < g_entries.size(); i++) {
            const std::wstring key = GroupKey(g_entries[i].product);
            bool seen = false;
            for (const auto& d : done) if (d == key) { seen = true; break; }
            if (seen) continue;
            done.push_back(key);
            ReevaluateOutlierFlagsForSpecies(g_entries, g_entries[i].product);
        }
        for (size_t i = 0; i < g_entries.size(); i++) if (g_entries[i].priceFlagged != before[i]) flagsChanged = true;
    }
    SaveSettings();
    if (flagsChanged) { g_dirty = true; RefreshAll(); }
    if (textChanged) MessageBoxW(hwnd, L"The new text size will be used the next time you start the app.", L"Settings", MB_OK | MB_ICONINFORMATION);
    return true;
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        for (auto& p : g_pages) p.clear();
        g_hSetTab = MakeControl(WC_TABCONTROLW, L"", WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS, ID_SET_TAB, hwnd);
        MoveWindow(g_hSetTab, S(8), S(8), S(544), S(414), TRUE);
        const wchar_t* names[4] = { L"Email", L"Backups & flags", L"Display & printing", L"Data & about" };
        for (int i = 0; i < 4; i++) {
            TCITEMW it{}; it.mask = TCIF_TEXT;
            it.pszText = const_cast<LPWSTR>(names[i]);
            TabCtrl_InsertItem(g_hSetTab, i, &it);
        }

        // ---- page 0: Email ----
        Add(0, L"STATIC", L"How should \"Email All Suppliers\" open each email?", WS_VISIBLE, 0, hwnd, 24, 48, 510, 20);
        Add(0, L"BUTTON", L"Open in my mail program (mailto) - plain text", WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON, ID_SET_EMAIL_MAILTO, hwnd, 36, 72, 490, 22);
        Add(0, L"BUTTON", L"Outlook draft with a price table (.eml) - needs classic Outlook", WS_VISIBLE | BS_AUTORADIOBUTTON, ID_SET_EMAIL_EML, hwnd, 36, 96, 490, 22);
        Add(0, L"BUTTON", L"Gmail draft - saved in the Gmail Drafts folder, nothing is sent", WS_VISIBLE | BS_AUTORADIOBUTTON, ID_SET_EMAIL_GMAIL, hwnd, 36, 120, 490, 22);
        Add(0, L"STATIC", L"Gmail account (optional):", WS_VISIBLE | WS_GROUP, 0, hwnd, 36, 156, 160, 20);
        g_hGmailAccount = Add(0, L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_SET_GMAIL_ACCOUNT, hwnd, 200, 152, 320, 24);
        Add(0, L"BUTTON", L"Forget this PC's Gmail sign-in", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, ID_SET_GMAIL_FORGET, hwnd, 200, 182, 220, 26);
        Add(0, L"STATIC", L"Wording (used by all three methods):", WS_VISIBLE, 0, hwnd, 24, 226, 510, 20);
        Add(0, L"STATIC", L"Subject:", WS_VISIBLE, 0, hwnd, 36, 256, 100, 20);
        g_hSubject = Add(0, L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_SET_SUBJECT, hwnd, 140, 252, 380, 24);
        Add(0, L"STATIC", L"Opening line:", WS_VISIBLE, 0, hwnd, 36, 290, 100, 20);
        g_hIntro = Add(0, L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_SET_INTRO, hwnd, 140, 286, 380, 24);
        Add(0, L"STATIC", L"Sign-off:", WS_VISIBLE, 0, hwnd, 36, 324, 100, 20);
        g_hSignoff = Add(0, L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_SET_SIGNOFF, hwnd, 140, 320, 380, 24);
        Add(0, L"STATIC", L"In the subject, {supplier} and {date} are filled in for each email. The opening line is followed by the date "
                          L"and a colon. Leave a box blank to use the standard wording.", WS_VISIBLE, 0, hwnd, 36, 354, 490, 40);
        for (HWND e : { g_hSubject, g_hIntro, g_hSignoff }) SendMessageW(e, EM_LIMITTEXT, 150, 0);

        // ---- page 1: Backups & price flags ----
        Add(1, L"STATIC", L"Backups", WS_VISIBLE, 0, hwnd, 24, 48, 510, 20);
        Add(1, L"STATIC", L"Take a backup snapshot at most every", WS_VISIBLE, 0, hwnd, 36, 80, 250, 20);
        g_hBackupMin = Add(1, L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, ID_SET_BACKUP_MIN, hwnd, 290, 76, 50, 24);
        Add(1, L"STATIC", L"minutes (only if something changed)", WS_VISIBLE, 0, hwnd, 348, 80, 190, 20);
        Add(1, L"STATIC", L"Keep the most recent", WS_VISIBLE, 0, hwnd, 36, 114, 250, 20);
        g_hBackupKeep = Add(1, L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, ID_SET_BACKUP_KEEP, hwnd, 290, 110, 50, 24);
        Add(1, L"STATIC", L"snapshots (older ones are deleted)", WS_VISIBLE, 0, hwnd, 348, 114, 190, 20);
        Add(1, L"STATIC", L"Price flagging (possible typo in a price)", WS_VISIBLE, 0, hwnd, 24, 164, 510, 20);
        Add(1, L"BUTTON", L"Relaxed - only flag prices that are far from the others", WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON, ID_SET_SENS_RELAXED, hwnd, 36, 190, 490, 22);
        Add(1, L"BUTTON", L"Normal (recommended)", WS_VISIBLE | BS_AUTORADIOBUTTON, ID_SET_SENS_NORMAL, hwnd, 36, 214, 490, 22);
        Add(1, L"BUTTON", L"Strict - flag smaller differences too", WS_VISIBLE | BS_AUTORADIOBUTTON, ID_SET_SENS_STRICT, hwnd, 36, 238, 490, 22);
        Add(1, L"STATIC", L"A price is only checked once there are at least 4 other prices for that species on the sheet. Changing this "
                          L"re-checks every entry on the current sheet straight away.", WS_VISIBLE | WS_GROUP, 0, hwnd, 36, 272, 490, 44);

        // ---- page 2: Display & printing (v0.23.0) ----
        Add(2, L"STATIC", L"Text size in the lists (takes effect next time the app starts)", WS_VISIBLE, 0, hwnd, 24, 48, 510, 20);
        Add(2, L"BUTTON", L"Normal", WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON, ID_SET_TEXT_100, hwnd, 36, 72, 150, 22);
        Add(2, L"BUTTON", L"Large (125%)", WS_VISIBLE | BS_AUTORADIOBUTTON, ID_SET_TEXT_125, hwnd, 190, 72, 150, 22);
        Add(2, L"BUTTON", L"Extra large (150%)", WS_VISIBLE | BS_AUTORADIOBUTTON, ID_SET_TEXT_150, hwnd, 344, 72, 180, 22);
        Add(2, L"STATIC", L"Date format in emails", WS_VISIBLE | WS_GROUP, 0, hwnd, 24, 112, 510, 20);
        Add(2, L"BUTTON", L"18 November 2026", WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON, ID_SET_DATE_LONG, hwnd, 36, 136, 150, 22);
        Add(2, L"BUTTON", L"18 Nov 2026", WS_VISIBLE | BS_AUTORADIOBUTTON, ID_SET_DATE_SHORT, hwnd, 190, 136, 150, 22);
        Add(2, L"BUTTON", L"18/11/2026", WS_VISIBLE | BS_AUTORADIOBUTTON, ID_SET_DATE_NUM, hwnd, 344, 136, 180, 22);
        Add(2, L"STATIC", L"Emails carry the sheet date (the date on the entries), not the day you send them.", WS_VISIBLE | WS_GROUP, 0, hwnd, 36, 162, 490, 20);
        Add(2, L"STATIC", L"Printing - where the Print dialog and Print Preview start", WS_VISIBLE, 0, hwnd, 24, 204, 510, 20);
        Add(2, L"STATIC", L"Printer:", WS_VISIBLE, 0, hwnd, 36, 238, 100, 20);
        g_hPrinter = Add(2, L"COMBOBOX", L"", WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_SET_PRINTER, hwnd, 140, 234, 380, 220);
        Add(2, L"STATIC", L"Paper:", WS_VISIBLE, 0, hwnd, 36, 274, 100, 20);
        g_hPaper = Add(2, L"COMBOBOX", L"", WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_SET_PAPER, hwnd, 140, 270, 200, 160);
        Add(2, L"STATIC", L"Orientation:", WS_VISIBLE, 0, hwnd, 36, 310, 100, 20);
        g_hOrient = Add(2, L"COMBOBOX", L"", WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_SET_ORIENT, hwnd, 140, 306, 200, 160);
        Add(2, L"STATIC", L"You can still change anything in the Print dialog. If the saved printer is no longer installed, the Windows "
                          L"default printer is used. To make a PDF, choose \"Microsoft Print to PDF\" as the printer.", WS_VISIBLE | WS_GROUP, 0, hwnd, 36, 346, 490, 56);
        SendMessageW(g_hPrinter, CB_ADDSTRING, 0, (LPARAM)L"Windows default printer");
        g_printerNames = ListInstalledPrinters();
        for (const auto& n : g_printerNames) SendMessageW(g_hPrinter, CB_ADDSTRING, 0, (LPARAM)n.c_str());
        for (const wchar_t* t : { L"Printer default", L"A4", L"A3" }) SendMessageW(g_hPaper, CB_ADDSTRING, 0, (LPARAM)t);
        for (const wchar_t* t : { L"Printer default", L"Portrait", L"Landscape" }) SendMessageW(g_hOrient, CB_ADDSTRING, 0, (LPARAM)t);

        // ---- page 3: Data folder & about ----
        Add(3, L"STATIC", L"Your data folder (entries, backups, names, settings):", WS_VISIBLE, 0, hwnd, 24, 48, 510, 20);
        HWND path = Add(3, L"EDIT", GetSharedDataDir().c_str(), WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_READONLY | ES_AUTOHSCROLL, ID_SET_DATA_PATH, hwnd, 36, 74, 490, 24);
        (void)path;
        Add(3, L"BUTTON", L"Open folder", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, ID_SET_OPEN_DATA, hwnd, 36, 106, 130, 26);
        std::wstring ver = std::wstring(L"Fish Balance Manager version ") + APP_VERSION;
        if (IsPortableMode()) ver += L"  (portable test copy)";
        Add(3, L"STATIC", ver.c_str(), WS_VISIBLE, 0, hwnd, 24, 160, 510, 20);
        Add(3, L"STATIC", L"These settings are saved in settings.txt in the data folder, together with the window position.", WS_VISIBLE, 0, hwnd, 24, 186, 510, 40);

        MakeControl(L"BUTTON", L"OK", WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_DEFPUSHBUTTON, ID_SET_OK, hwnd);
        MakeControl(L"BUTTON", L"Cancel", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, ID_SET_CANCEL, hwnd);
        MoveWindow(GetDlgItem(hwnd, ID_SET_OK), S(372), S(432), S(86), S(28), TRUE);
        MoveWindow(GetDlgItem(hwnd, ID_SET_CANCEL), S(466), S(432), S(86), S(28), TRUE);

        // Load current values.
        CheckRadioButton(hwnd, ID_SET_EMAIL_MAILTO, ID_SET_EMAIL_GMAIL,
            g_settings.emailMode == EmailMode::Eml ? ID_SET_EMAIL_EML
            : (g_settings.emailMode == EmailMode::Gmail ? ID_SET_EMAIL_GMAIL : ID_SET_EMAIL_MAILTO));
        SetWindowTextW(g_hGmailAccount, g_settings.gmailAccount.c_str());
        const EmailWording w = CurrentEmailWording();
        SetWindowTextW(g_hSubject, w.subject.c_str());
        SetWindowTextW(g_hIntro, w.intro.c_str());
        SetWindowTextW(g_hSignoff, w.signoff.c_str());
        SetWindowTextW(g_hBackupMin, std::to_wstring(ClampBackupMinutes(g_settings.backupMinutes)).c_str());
        SetWindowTextW(g_hBackupKeep, std::to_wstring(ClampBackupKeep(g_settings.backupKeep)).c_str());
        const int s = ClampOutlierSensitivity(g_settings.outlierSensitivity);
        CheckRadioButton(hwnd, ID_SET_SENS_RELAXED, ID_SET_SENS_STRICT,
            s == 0 ? ID_SET_SENS_RELAXED : (s == 2 ? ID_SET_SENS_STRICT : ID_SET_SENS_NORMAL));
        const int tp = ClampTextPercent(g_settings.textPercent);
        CheckRadioButton(hwnd, ID_SET_TEXT_100, ID_SET_TEXT_150, tp == 125 ? ID_SET_TEXT_125 : (tp == 150 ? ID_SET_TEXT_150 : ID_SET_TEXT_100));
        const int ds = ClampDateStyle(g_settings.dateStyle);
        CheckRadioButton(hwnd, ID_SET_DATE_LONG, ID_SET_DATE_NUM, ds == 1 ? ID_SET_DATE_SHORT : (ds == 2 ? ID_SET_DATE_NUM : ID_SET_DATE_LONG));
        int printerSel = 0;
        for (size_t k = 0; k < g_printerNames.size(); k++) if (g_printerNames[k] == g_settings.printer) printerSel = (int)k + 1;
        SendMessageW(g_hPrinter, CB_SETCURSEL, printerSel, 0);
        SendMessageW(g_hPaper, CB_SETCURSEL, ClampPrintPaper(g_settings.printPaper), 0);
        SendMessageW(g_hOrient, CB_SETCURSEL, ClampPrintOrientation(g_settings.printOrientation), 0);
        ShowPage(0);
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lParam;
        if (nm->idFrom == ID_SET_TAB && nm->code == TCN_SELCHANGE) ShowPage(TabCtrl_GetCurSel(g_hSetTab));
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == ID_SET_OK) { if (ApplyAndSave(hwnd)) DestroyWindow(hwnd); return 0; }
        if (id == ID_SET_CANCEL) { DestroyWindow(hwnd); return 0; }
        if (id == ID_SET_OPEN_DATA) {
            ShellExecuteW(nullptr, L"open", GetSharedDataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (id == ID_SET_GMAIL_FORGET) {
            if (!GmailHasSavedSignIn()) {
                MessageBoxW(hwnd, L"This PC has no saved Gmail sign-in.", L"Gmail", MB_OK | MB_ICONINFORMATION);
            } else if (MessageBoxW(hwnd, L"Forget this PC's saved Gmail sign-in? You will be asked to sign in to Google again the next "
                                         L"time you create Gmail drafts. (Your Google account is not changed.)",
                                   L"Gmail", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                GmailForgetSignIn();
            }
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_hMainWnd) EnableWindow(g_hMainWnd, TRUE);
        g_hSettingsWnd = nullptr;
        if (g_hMainWnd) SetForegroundWindow(g_hMainWnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

void OpenSettingsWindow() {
    if (g_hSettingsWnd) { SetForegroundWindow(g_hSettingsWnd); return; }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"SettingsWindow";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    // Client area is 560 x 470 (96-dpi); AdjustWindowRectEx turns that into the outer size for the caption/border.
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_DLGMODALFRAME;
    RECT rc = { 0, 0, S(560), S(470) };
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    RECT mrc;
    GetWindowRect(g_hMainWnd, &mrc);
    int x = mrc.left + ((mrc.right - mrc.left) - w) / 2;
    int y = mrc.top + ((mrc.bottom - mrc.top) - h) / 2;
    FitPopupToWorkArea(g_hMainWnd, x, y, w, h);

    g_hSettingsWnd = CreateWindowExW(exStyle, L"SettingsWindow", L"Settings", style, x, y, w, h,
                                     g_hMainWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    EnableWindow(g_hMainWnd, FALSE);
    ShowWindow(g_hSettingsWnd, SW_SHOW);
    UpdateWindow(g_hSettingsWnd);
}
