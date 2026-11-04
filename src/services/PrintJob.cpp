// PrintJob.cpp - the print job for the Breakdown report, shared by the Win32 app and the WinUI prototype.
// Moved unchanged out of main.cpp in v0.12.10 except that the entries and the Debtor/Cash text are passed in.
#include "PrintJob.h"

#include <algorithm>
#include <string>

#include "ReportRender.h"

void PrintBreakdownJob(HWND owner, const std::vector<Entry>& entries,
                       const std::wstring& debtorText, const std::wstring& cashText) {
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

    RenderReportResult result = RenderReportPages(entries, pageWidthPx, pageHeightPx, dpiX, dpiY, debtorText, cashText, onPageReady);

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

bool GetPreviewPageSpec(PreviewPageSpec& spec, std::wstring* error) {
    // Size the preview to the default printer's page, so it's a realistic representation of what will
    // actually print. If there's no default printer configured at all (e.g. a fresh machine with none
    // installed), or getting its info fails, fall back to a generic Letter-sized page (the PreviewPageSpec
    // defaults) so preview still works.
    int pageWidthPx = spec.widthPx, pageHeightPx = spec.heightPx, dpiX = spec.dpiX, dpiY = spec.dpiY;
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

    // The preview is only ever shown shrunk down to fit a small on-screen window, so rendering at the
    // printer's full native DPI (often 600+) wastes enormous amounts of memory for zero visible benefit - a
    // single 24-bit page at 600 DPI is roughly 100MB, and a multi-page report could reach several GB. Cap the
    // PREVIEW's DPI (only the preview - real printing still uses the real printer DPI for print quality) while
    // scaling the pixel dimensions down to match, so the real printer's page proportions are preserved exactly.
    const int kPreviewDpiCap = 150;
    spec.dpiX = (std::min)(dpiX, kPreviewDpiCap);
    spec.dpiY = (std::min)(dpiY, kPreviewDpiCap);
    spec.widthPx = MulDiv(pageWidthPx, spec.dpiX, dpiX);
    spec.heightPx = MulDiv(pageHeightPx, spec.dpiY, dpiY);

    // Sanity-check the page size/DPI up front - does even a SINGLE page at this resolution fit at all?
    // (The total-memory bound across every page is enforced progressively inside RenderReportPages.)
    std::wstring budgetError;
    if (!ValidateReportPageDimensions(spec.widthPx, spec.heightPx, spec.dpiX, spec.dpiY, &budgetError) ||
        !PreviewBudgetAllows(spec.widthPx, spec.heightPx, 1, &budgetError)) {
        if (error) *error = budgetError;
        return false;
    }
    return true;
}
