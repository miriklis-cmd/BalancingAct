// EmailNotesWindow.cpp - see EmailNotesWindow.h. Own window class, owner disabled while open, nested message loop.
// All sizes go through S() (DPI), per ARCHITECTURE.md. No default push button: Enter in a text box is a new line.
#include "EmailNotesWindow.h"
#include "ControlIds.h"
#include "MainWindowApi.h"
#include "PopupPlacement.h"
#include "UiState.h"
#include "../PlatformWin.h"
#include <string>
#include <vector>

namespace {

struct DlgState {
    const std::vector<SupplierGroup>* data = nullptr;
    std::map<std::wstring, EmailNotes> notes;
    int current = -1;
    bool done = false;
    EmailNotesResult result = EmailNotesResult::Cancel;
    HWND list = nullptr, title = nullptr, notSold = nullptr, up = nullptr, down = nullptr;
};
DlgState* g_st = nullptr;

std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}

bool HasNotes(const EmailNotes& n) {
    return !CleanNoteBlock(n.notSold).empty() || !CleanNoteBlock(n.up).empty() || !CleanNoteBlock(n.down).empty();
}

std::wstring ListLabel(size_t i) {
    const SupplierGroup& sg = (*g_st->data)[i];
    auto it = g_st->notes.find(sg.supplier);
    return sg.supplier + ((it != g_st->notes.end() && HasNotes(it->second)) ? L"   (notes)" : L"");
}

// Copies the three boxes into the notes of the supplier being shown.
void SaveCurrent() {
    if (g_st->current < 0 || static_cast<size_t>(g_st->current) >= g_st->data->size()) return;
    EmailNotes n;
    n.notSold = GetText(g_st->notSold);
    n.up = GetText(g_st->up);
    n.down = GetText(g_st->down);
    const size_t i = static_cast<size_t>(g_st->current);
    g_st->notes[(*g_st->data)[i].supplier] = n;
    // Refresh this row's "(notes)" mark without losing the selection.
    SendMessageW(g_st->list, LB_DELETESTRING, i, 0);
    SendMessageW(g_st->list, LB_INSERTSTRING, i, reinterpret_cast<LPARAM>(ListLabel(i).c_str()));
    SendMessageW(g_st->list, LB_SETCURSEL, i, 0);
}

void ShowSupplier(int idx) {
    if (idx < 0 || static_cast<size_t>(idx) >= g_st->data->size()) return;
    g_st->current = idx;
    const SupplierGroup& sg = (*g_st->data)[static_cast<size_t>(idx)];
    size_t lines = 0;
    for (const auto& pg : sg.products) lines += pg.prices.size();
    SetWindowTextW(g_st->title, (sg.supplier + L"   (" + std::to_wstring(lines) + (lines == 1 ? L" price line, " : L" price lines, ") +
                                 FormatKg(sg.totalKgs) + L" kg)").c_str());
    auto it = g_st->notes.find(sg.supplier);
    const EmailNotes n = (it != g_st->notes.end()) ? it->second : EmailNotes();
    SetWindowTextW(g_st->notSold, NewlinesToCrlf(n.notSold).c_str());
    SetWindowTextW(g_st->up, NewlinesToCrlf(n.up).c_str());
    SetWindowTextW(g_st->down, NewlinesToCrlf(n.down).c_str());
    SendMessageW(g_st->list, LB_SETCURSEL, idx, 0);
}

HWND Place(LPCWSTR cls, LPCWSTR text, DWORD style, int id, HWND parent, int x, int y, int w, int h) {
    HWND c = MakeControl(cls, text, style, id, parent);
    if (c) MoveWindow(c, S(x), S(y), S(w), S(h), TRUE);
    return c;
}

const DWORD kBox = WS_VISIBLE | WS_BORDER | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;

LRESULT CALLBACK NotesWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_st->list = Place(L"LISTBOX", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                           ID_NOTES_LIST, hwnd, 16, 16, 220, 338);
        for (size_t i = 0; i < g_st->data->size(); i++)
            SendMessageW(g_st->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ListLabel(i).c_str()));
        g_st->title = Place(L"STATIC", L"", WS_VISIBLE | SS_ENDELLIPSIS, 0, hwnd, 252, 18, 432, 20);
        Place(L"STATIC", L"NOT SOLD", WS_VISIBLE, 0, hwnd, 252, 46, 200, 18);
        g_st->notSold = Place(L"EDIT", L"", kBox, ID_NOTES_NOTSOLD, hwnd, 252, 66, 432, 100);
        Place(L"STATIC", L"UP  (arrived over the manifest)", WS_VISIBLE, 0, hwnd, 252, 176, 210, 18);
        g_st->up = Place(L"EDIT", L"", kBox, ID_NOTES_UP, hwnd, 252, 196, 210, 90);
        Place(L"STATIC", L"DOWN  (short of the manifest)", WS_VISIBLE, 0, hwnd, 474, 176, 210, 18);
        g_st->down = Place(L"EDIT", L"", kBox, ID_NOTES_DOWN, hwnd, 474, 196, 210, 90);
        Place(L"STATIC", L"Type it as the supplier should read it: bins x weight (17X25 GREN) or kg (21.4 kg GUMMY). "
                         L"The headings are added for you; a box left empty adds nothing.", WS_VISIBLE, 0, hwnd, 252, 294, 432, 36);
        Place(L"BUTTON", L"Next supplier", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, ID_NOTES_NEXT, hwnd, 252, 330, 130, 26);
        Place(L"BUTTON", L"Skip - no notes", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, ID_NOTES_SKIP, hwnd, 292, 380, 130, 28);
        Place(L"BUTTON", L"Create emails", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, ID_NOTES_CREATE, hwnd, 432, 380, 130, 28);
        Place(L"BUTTON", L"Cancel", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL, hwnd, 572, 380, 112, 28);
        for (HWND e : { g_st->notSold, g_st->up, g_st->down }) SendMessageW(e, EM_LIMITTEXT, 4000, 0);
        ShowSupplier(0);
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id == ID_NOTES_LIST && code == LBN_SELCHANGE) {
            const int sel = static_cast<int>(SendMessageW(g_st->list, LB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel != g_st->current) { SaveCurrent(); ShowSupplier(sel); }
            return 0;
        }
        if (id == ID_NOTES_NEXT) {
            SaveCurrent();
            ShowSupplier((g_st->current + 1) % static_cast<int>(g_st->data->size()));
            SetFocus(g_st->notSold);
            return 0;
        }
        if (id == ID_NOTES_CREATE) { SaveCurrent(); g_st->result = EmailNotesResult::Create; g_st->done = true; DestroyWindow(hwnd); return 0; }
        if (id == ID_NOTES_SKIP) {
            // Skip throws away anything typed: ask first, and stay in the dialog on No.
            SaveCurrent();
            bool any = false;
            for (const auto& kv : g_st->notes) if (HasNotes(kv.second)) { any = true; break; }
            if (any && MessageBoxW(hwnd, L"You have typed notes. Skip and throw them away?", L"Email Suppliers",
                                   MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
                return 0;
        }
        if (id == ID_NOTES_SKIP) { g_st->result = EmailNotesResult::Skip; g_st->done = true; DestroyWindow(hwnd); return 0; }
        if (id == IDCANCEL) { g_st->result = EmailNotesResult::Cancel; g_st->done = true; DestroyWindow(hwnd); return 0; }
        break;
    }
    case WM_CLOSE:
        g_st->result = EmailNotesResult::Cancel;
        g_st->done = true;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_st->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

EmailNotesResult ShowEmailNotesDialog(const std::vector<SupplierGroup>& data, std::map<std::wstring, EmailNotes>& notes) {
    notes.clear();
    if (data.empty()) return EmailNotesResult::Skip;

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = NotesWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"EmailNotesWindow";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    DlgState st;
    st.data = &data;
    g_st = &st;

    // Client area 700 x 424 (96-dpi).
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_DLGMODALFRAME;
    RECT rc = { 0, 0, S(700), S(424) };
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    RECT mrc;
    GetWindowRect(g_hMainWnd, &mrc);
    int x = mrc.left + ((mrc.right - mrc.left) - w) / 2;
    int y = mrc.top + ((mrc.bottom - mrc.top) - h) / 2;
    FitPopupToWorkArea(g_hMainWnd, x, y, w, h);

    EnableWindow(g_hMainWnd, FALSE);
    HWND hwnd = CreateWindowExW(exStyle, L"EmailNotesWindow", L"Email Suppliers - notes for each supplier", style, x, y, w, h,
                                g_hMainWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        EnableWindow(g_hMainWnd, TRUE);
        g_st = nullptr;
        return EmailNotesResult::Skip; // could not open the dialog: carry on without notes rather than block the emails
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetFocus(st.notSold);

    MSG m;
    while (!st.done && GetMessageW(&m, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    EnableWindow(g_hMainWnd, TRUE);
    SetForegroundWindow(g_hMainWnd);

    const EmailNotesResult r = st.result;
    if (r == EmailNotesResult::Create) notes = st.notes;
    g_st = nullptr;
    return r;
}
