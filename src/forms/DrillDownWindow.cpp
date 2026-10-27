// DrillDownWindow.cpp - the Breakdown drill-down popup (v0.12.6). Same lifecycle as the other popups
// (ARCHITECTURE.md "Windows in the app"): EnableWindow(main, FALSE) on creation, re-enabled in this
// window's own WM_DESTROY. Every dimension goes through S(); list text goes through named variables
// (ListView_SetItemText is a multi-statement macro).
#include "DrillDownWindow.h"

#include "../PlatformWin.h"
#include <string>
#include <vector>

#include "../core/FishBalanceCore.h"
#include "../data/AppData.h"
#include "ControlIds.h"
#include "UiState.h"
#include "MainWindowApi.h"
#include "MainWindowLayout.h"
#include "PopupPlacement.h"

namespace {

HWND g_hDrillWnd = nullptr;
HWND g_hDrillDesc = nullptr;
HWND g_hDrillList = nullptr;
HWND g_hDrillTotal = nullptr;
HWND g_hDrillClose = nullptr;
BreakdownRowRef g_drillRef; // the Breakdown row this window was opened for
int g_drillSortCol = -1;    // list column last clicked (-1 = default order: species, then date)
bool g_drillSortAsc = true;

// Never fail quietly: say what could not be done and the Windows error code.
void ReportDrillFailure(const wchar_t* what) {
    DWORD err = GetLastError(); // read first, before anything else can change it
    std::wstring msg = std::wstring(L"The entry list could not be opened: ") + what + L".";
    if (err != 0) msg += L"\n\nWindows error code " + std::to_wstring((unsigned long)err) + L".";
    MessageBoxW(g_hMainWnd, msg.c_str(), L"Cannot Open Entry List", MB_OK | MB_ICONERROR);
}

// One line saying which Breakdown row the list belongs to.
std::wstring DescribeDrillRow(const BreakdownRowRef& ref) {
    switch (ref.kind) {
    case BreakdownRowKind::Price:
        return ref.supplier + L"  -  " + ref.species + L"  at  " + FormatMoney(ref.price) + L" /kg";
    case BreakdownRowKind::SpeciesTotal:
        return ref.supplier + L"  -  " + ref.species + L"  (all prices)";
    case BreakdownRowKind::SupplierTotal:
        return ref.supplier + L"  -  all species";
    }
    return ref.supplier;
}

// Esc closes the popup whichever control has the keyboard focus (the list or the Close button). The popup is a
// plain window, not a dialog, so there is no IDCANCEL handling for free - the children are subclassed instead.
LRESULT CALLBACK DrillChildSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        DestroyWindow(GetParent(hwnd));
        return 0; // hwnd is destroyed with its parent - nothing may touch it after this
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, DrillChildSubclassProc, 1);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Shows the sort arrow on the clicked column's header (and clears it from the others).
void UpdateDrillSortArrow() {
    HWND header = ListView_GetHeader(g_hDrillList);
    if (!header) return;
    int count = Header_GetItemCount(header);
    for (int i = 0; i < count; i++) {
        HDITEMW hd{};
        hd.mask = HDI_FORMAT;
        if (!Header_GetItem(header, i, &hd)) continue;
        hd.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == g_drillSortCol) hd.fmt |= g_drillSortAsc ? HDF_SORTUP : HDF_SORTDOWN;
        Header_SetItem(header, i, &hd);
    }
}

DrillSortKey DrillSortKeyForColumn(int col) {
    switch (col) {
    case 0: return DrillSortKey::Supplier;
    case 1: return DrillSortKey::Species;
    case 2: return DrillSortKey::Date;
    case 3: return DrillSortKey::Kgs;
    case 4: return DrillSortKey::Price;
    case 5: return DrillSortKey::Total;
    }
    return DrillSortKey::Default;
}

void FillDrillList() {
    ListView_DeleteAllItems(g_hDrillList);
    DrillDownResult res = ComputeDrillDown(g_entries, g_drillRef);
    SortDrillDownIndices(g_entries, res.indices, DrillSortKeyForColumn(g_drillSortCol), g_drillSortAsc);
    UpdateDrillSortArrow();

    int row = 0;
    size_t notShown = 0; // rows the list control refused - reported next to the total, never dropped silently
    for (size_t idx : res.indices) {
        const Entry& e = g_entries[idx];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(e.supplier.c_str());
        if (ListView_InsertItem(g_hDrillList, &item) < 0) { notShown++; continue; }
        ListView_SetItemText(g_hDrillList, row, 1, const_cast<LPWSTR>(e.product.c_str()));
        ListView_SetItemText(g_hDrillList, row, 2, const_cast<LPWSTR>(e.date.c_str()));
        std::wstring kgsS = FormatKg(e.kgs);
        ListView_SetItemText(g_hDrillList, row, 3, const_cast<LPWSTR>(kgsS.c_str()));
        std::wstring priceS = FormatMoney(e.price);
        ListView_SetItemText(g_hDrillList, row, 4, const_cast<LPWSTR>(priceS.c_str()));
        std::wstring totalS = FormatMoney(e.Total());
        ListView_SetItemText(g_hDrillList, row, 5, const_cast<LPWSTR>(totalS.c_str()));
        row++;
    }

    // Same formatting as the Breakdown row's own Kgs and Total ($) columns, so the figures can be
    // compared by eye.
    std::wstring total = L"Total:  " + std::to_wstring(res.indices.size()) +
                         (res.indices.size() == 1 ? L" entry" : L" entries") +
                         L"   |   " + FormatKg(res.kgs) + L" kg   |   " + FormatMoney(res.amt);
    if (notShown > 0)
        total += L"   |   WARNING: " + std::to_wstring(notShown) + L" row(s) could not be shown - the total above still includes them";
    SetWindowTextW(g_hDrillTotal, total.c_str());
}

LRESULT CALLBACK DrillWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // SS_NOPREFIX: supplier names such as "Jenkins & Son" must show the '&' literally.
        std::wstring desc = DescribeDrillRow(g_drillRef);
        g_hDrillDesc = MakeControl(L"STATIC", desc.c_str(), WS_VISIBLE | SS_NOPREFIX | SS_ENDELLIPSIS, 0, hwnd);
        g_hDrillList = MakeControl(L"SysListView32", L"",
                                   WS_VISIBLE | WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                   ID_DRILL_LIST, hwnd);
        g_hDrillTotal = MakeControl(L"STATIC", L"", WS_VISIBLE | SS_NOPREFIX, 0, hwnd);
        g_hDrillClose = MakeControl(L"BUTTON", L"Close", WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, ID_DRILL_CLOSE, hwnd);
        if (!g_hDrillDesc || !g_hDrillList || !g_hDrillTotal || !g_hDrillClose) {
            SetLastError(ERROR_INVALID_WINDOW_HANDLE); // CreateWindowExW then returns NULL and OpenDrillDownWindow reports it
            return -1;
        }
        if (g_boldFont) {
            SendMessageW(g_hDrillDesc, WM_SETFONT, (WPARAM)g_boldFont, TRUE);
            SendMessageW(g_hDrillTotal, WM_SETFONT, (WPARAM)g_boldFont, TRUE);
        }
        ListView_SetExtendedListViewStyle(g_hDrillList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        AddColumn(g_hDrillList, 0, L"Supplier", 150);
        AddColumn(g_hDrillList, 1, L"Species", 150);
        AddColumn(g_hDrillList, 2, L"Date", 100);
        AddColumn(g_hDrillList, 3, L"Kgs", 90);
        AddColumn(g_hDrillList, 4, L"Price ($/kg)", 105);
        AddColumn(g_hDrillList, 5, L"Total ($)", 115);
        SetListRowHeight(g_hDrillList, S(22));
        if (!SetWindowSubclass(g_hDrillList, DrillChildSubclassProc, 1, 0) ||
            !SetWindowSubclass(g_hDrillClose, DrillChildSubclassProc, 1, 0)) {
            SetLastError(ERROR_INVALID_WINDOW_HANDLE); // Esc would not work - report it rather than run without it
            return -1;
        }
        FillDrillList();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lParam;
        mm->ptMinTrackSize.x = S(520);
        mm->ptMinTrackSize.y = S(300);
        return 0;
    }
    case WM_SIZE: {
        if (!g_hDrillList) return 0;
        RECT rc;
        GetClientRect(hwnd, &rc);
        int pad = S(12);
        int bottomBar = S(52);
        int listTop = S(38);
        int listH = rc.bottom - listTop - bottomBar;
        if (listH < S(60)) listH = S(60);
        MoveWindow(g_hDrillDesc, pad, S(10), rc.right - 2 * pad, S(22), TRUE);
        MoveWindow(g_hDrillList, pad, listTop, rc.right - 2 * pad, listH, TRUE);
        MoveWindow(g_hDrillTotal, pad, listTop + listH + S(14), rc.right - 2 * pad - S(100), S(22), TRUE);
        MoveWindow(g_hDrillClose, rc.right - pad - S(85), listTop + listH + S(10), S(85), S(28), TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_DRILL_CLOSE) { DestroyWindow(hwnd); return 0; }
        break;
    case WM_NOTIFY: {
        LPNMHDR hdr = (LPNMHDR)lParam;
        if (hdr->hwndFrom == g_hDrillList && hdr->code == (UINT)LVN_COLUMNCLICK) {
            int col = ((LPNMLISTVIEW)lParam)->iSubItem;
            if (g_drillSortCol == col) g_drillSortAsc = !g_drillSortAsc; // same column again: flip direction
            else { g_drillSortCol = col; g_drillSortAsc = true; }
            FillDrillList();
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_hMainWnd) EnableWindow(g_hMainWnd, TRUE);
        g_hDrillWnd = nullptr;
        g_hDrillDesc = g_hDrillList = g_hDrillTotal = g_hDrillClose = nullptr;
        SetForegroundWindow(g_hMainWnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

void OpenDrillDownWindow(const BreakdownRowRef& ref) {
    if (g_hDrillWnd) {
        SetForegroundWindow(g_hDrillWnd);
        return;
    }
    g_drillRef = ref;
    g_drillSortCol = -1; // each opening starts in the default order
    g_drillSortAsc = true;

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = DrillWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"DrillDownWindow";
        if (!RegisterClassExW(&wc)) {
            ReportDrillFailure(L"the window class could not be registered");
            return;
        }
        classRegistered = true;
    }

    RECT mrc;
    GetWindowRect(g_hMainWnd, &mrc);
    int w = S(780), h = S(520);
    int x = mrc.left + ((mrc.right - mrc.left) - w) / 2;
    int y = mrc.top + ((mrc.bottom - mrc.top) - h) / 2;
    FitPopupToWorkArea(g_hMainWnd, x, y, w, h); // never taller/wider than the screen (v0.10.7)

    g_hDrillWnd = CreateWindowExW(0, L"DrillDownWindow", L"Entries Behind This Row",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, x, y, w, h,
                                  g_hMainWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_hDrillWnd) {
        ReportDrillFailure(L"the window could not be created"); // main window stays enabled
        return;
    }
    EnableWindow(g_hMainWnd, FALSE);
    ShowWindow(g_hDrillWnd, SW_SHOW);
    UpdateWindow(g_hDrillWnd);
    if (g_hDrillList) SetFocus(g_hDrillList);
}
