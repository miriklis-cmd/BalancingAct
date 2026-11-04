// PrintPreviewWindow.cpp - moved unchanged out of main.cpp in v0.10.6 (split stage 5).
#include "PrintPreviewWindow.h"

#include "../PlatformWin.h"
#include <algorithm>
#include <cwchar>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../core/FishBalanceCore.h"
#include "../core/FishBalanceControlLimits.h"
#include "../data/AppData.h"
#include "../data/FishBalanceWin32IO.h"
#include "../data/Storage.h"
#include "../services/PrintJob.h"
#include "../services/ReportRender.h"
#include "ControlIds.h"
#include "UiState.h"
#include "MainWindowApi.h"
#include "PopupPlacement.h"

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

                // Phase 2 / F11 (2026-10-02): both CreateCompatibleDC and
                // CreatePen can fail (e.g. the system is out of GDI
                // handles) - null-checked now so a failure here just skips
                // drawing this page's preview instead of risking undefined
                // behavior from selecting into/drawing with a null handle.
                HDC memDC = CreateCompatibleDC(hdc);
                if (memDC) {
                    HGDIOBJ oldBmp = SelectObject(memDC, page.bitmap);
                    SetStretchBltMode(hdc, HALFTONE);
                    StretchBlt(hdc, drawX, drawY, drawW, drawH, memDC, 0, 0, page.width, page.height, SRCCOPY);
                    SelectObject(memDC, oldBmp);
                    DeleteDC(memDC);
                }

                HPEN pen = CreatePen(PS_SOLID, 1, RGB(90, 90, 90));
                if (pen) {
                    HGDIOBJ oldPen = SelectObject(hdc, pen);
                    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                    Rectangle(hdc, drawX, drawY, drawX + drawW, drawY + drawH);
                    SelectObject(hdc, oldPen);
                    SelectObject(hdc, oldBrush);
                    DeleteObject(pen);
                }
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

    // Page size comes from the default printer (capped DPI, validated) - shared with the WinUI prototype.
    PreviewPageSpec spec;
    std::wstring budgetError;
    if (!GetPreviewPageSpec(spec, &budgetError)) {
        MessageBoxW(owner, budgetError.c_str(), L"Print Preview", MB_OK | MB_ICONERROR);
        return;
    }
    const int previewWidthPx = spec.widthPx, previewHeightPx = spec.heightPx;
    const int previewDpiX = spec.dpiX, previewDpiY = spec.dpiY;

    FreeRenderedPages(g_previewPages);
    std::wstring debtorText, cashText;
    GetDebtorCashText(debtorText, cashText);
    RenderReportResult result = RenderReportPages(g_entries, previewWidthPx, previewHeightPx, previewDpiX, previewDpiY, debtorText, cashText);
    g_previewPageIndex = 0;

    if (!result.ok) {
        MessageBoxW(owner, result.error.c_str(), L"Print Preview", MB_OK | MB_ICONERROR);
        return;
    }
    g_previewPages = std::move(result.pages);

    if (g_previewPages.empty()) {
        MessageBoxW(owner, L"Nothing to preview yet.", L"Print Preview", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (result.pageCapHit) {
        MessageBoxW(owner, (L"This report has more than " + std::to_wstring(kReportMaxPages) +
            L" pages. Only the first " + std::to_wstring(kReportMaxPages) +
            L" are shown here and will print.").c_str(), L"Print Preview", MB_OK | MB_ICONWARNING);
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
    FitPopupToWorkArea(g_hMainWnd, x, y, w, h); // v0.10.7: never taller/wider than the screen

    g_hPreviewWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PrintPreviewWindow", L"Print Preview - Breakdown Report",
                                     WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, x, y, w, h,
                                     g_hMainWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    EnableWindow(g_hMainWnd, FALSE);
    ShowWindow(g_hPreviewWnd, SW_SHOW);
    UpdateWindow(g_hPreviewWnd);
}
