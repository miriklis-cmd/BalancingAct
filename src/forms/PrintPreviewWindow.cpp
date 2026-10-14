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
#include "../services/ReportRender.h"
#include "ControlIds.h"
#include "UiState.h"
#include "MainWindowApi.h"

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

    // Phase 2 / F10 (2026-10-02): sanity-check the page size/DPI up front -
    // does even a SINGLE page at this resolution fit at all (dimensions in
    // range, and not so large that one page alone blows the per-page
    // ceiling)? The actual TOTAL-memory bound across every page is enforced
    // progressively inside RenderReportPages itself, based on the real
    // per-page byte size - see its comment - rather than pre-checked here
    // against the worst-case page count, which would wrongly refuse an
    // ordinary, legitimate multi-page report.
    std::wstring budgetError;
    if (!ValidateReportPageDimensions(previewWidthPx, previewHeightPx, previewDpiX, previewDpiY, &budgetError) ||
        !PreviewBudgetAllows(previewWidthPx, previewHeightPx, 1, &budgetError)) {
        MessageBoxW(owner, budgetError.c_str(), L"Print Preview", MB_OK | MB_ICONERROR);
        return;
    }

    FreeRenderedPages(g_previewPages);
    std::wstring debtorText, cashText;
    GetDebtorCashText(debtorText, cashText);
    RenderReportResult result = RenderReportPages(previewWidthPx, previewHeightPx, previewDpiX, previewDpiY, debtorText, cashText);
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

    g_hPreviewWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PrintPreviewWindow", L"Print Preview - Breakdown Report",
                                     WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, x, y, w, h,
                                     g_hMainWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    EnableWindow(g_hMainWnd, FALSE);
    ShowWindow(g_hPreviewWnd, SW_SHOW);
    UpdateWindow(g_hPreviewWnd);
}
