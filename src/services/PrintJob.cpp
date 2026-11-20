// PrintJob.cpp - the print job for the Breakdown report, shared by the Win32 app and the WinUI prototype.
// Moved unchanged out of main.cpp in v0.12.10 except that the entries and the Debtor/Cash text are passed in.
#include "PrintJob.h"

#include <commdlg.h>   // PrintDlgW (explicit: some projects define WIN32_LEAN_AND_MEAN)
#include <winspool.h>  // GetDefaultPrinterW
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "ReportRender.h"

std::vector<std::wstring> ListInstalledPrinters() {
    std::vector<std::wstring> out;
    DWORD needed = 0, count = 0;
    const DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
    EnumPrintersW(flags, nullptr, 4, nullptr, 0, &needed, &count);
    if (needed == 0) return out;
    std::vector<BYTE> buf(needed);
    if (!EnumPrintersW(flags, nullptr, 4, buf.data(), needed, &needed, &count)) return out;
    const PRINTER_INFO_4W* info = reinterpret_cast<const PRINTER_INFO_4W*>(buf.data());
    for (DWORD i = 0; i < count; i++) if (info[i].pPrinterName) out.push_back(info[i].pPrinterName);
    std::sort(out.begin(), out.end());
    return out;
}

namespace {
// v0.23.0: the printer to start from: the saved one if it is still installed, else the Windows default ("" if none).
std::wstring ResolvePrinterName(const PrintDefaults& d) {
    if (!d.printer.empty()) {
        for (const auto& n : ListInstalledPrinters()) if (n == d.printer) return n;
    }
    wchar_t name[256];
    DWORD size = 256;
    return GetDefaultPrinterW(name, &size) ? std::wstring(name) : std::wstring();
}

// A DEVMODE for `printer` with the saved orientation/paper applied (returned in a movable global block, the form
// PrintDlgW takes and frees). Returns nullptr on any failure - callers then simply don't pre-fill.
HGLOBAL BuildDevMode(const std::wstring& printer, const PrintDefaults& d) {
    if (printer.empty()) return nullptr;
    HANDLE hPrinter = nullptr;
    if (!OpenPrinterW(const_cast<LPWSTR>(printer.c_str()), &hPrinter, nullptr) || !hPrinter) return nullptr;
    HGLOBAL result = nullptr;
    LONG size = DocumentPropertiesW(nullptr, hPrinter, const_cast<LPWSTR>(printer.c_str()), nullptr, nullptr, 0);
    if (size > 0) {
        std::vector<BYTE> buf((size_t)size);
        DEVMODEW* dm = reinterpret_cast<DEVMODEW*>(buf.data());
        if (DocumentPropertiesW(nullptr, hPrinter, const_cast<LPWSTR>(printer.c_str()), dm, nullptr, DM_OUT_BUFFER) == IDOK) {
            const std::vector<BYTE> untouched = buf; // the driver's own default, used if it refuses our changes
            if (d.orientation == 1 || d.orientation == 2) {
                dm->dmFields |= DM_ORIENTATION;
                dm->dmOrientation = (short)(d.orientation == 2 ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT);
            }
            if (d.paper == 1 || d.paper == 2) {
                dm->dmFields |= DM_PAPERSIZE;
                dm->dmFields &= ~(DWORD)(DM_PAPERLENGTH | DM_PAPERWIDTH);
                dm->dmPaperSize = (short)(d.paper == 2 ? DMPAPER_A3 : DMPAPER_A4);
            }
            // Let the driver validate/merge the changes; if it refuses, we simply use the untouched printer default.
            const bool accepted = DocumentPropertiesW(nullptr, hPrinter, const_cast<LPWSTR>(printer.c_str()), dm, dm, DM_IN_BUFFER | DM_OUT_BUFFER) == IDOK;
            const BYTE* src = accepted ? buf.data() : untouched.data();
            HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)size);
            if (g) {
                void* dst = GlobalLock(g);
                if (dst) { memcpy(dst, src, (size_t)size); GlobalUnlock(g); result = g; }
                else GlobalFree(g);
            }
        }
    }
    ClosePrinter(hPrinter);
    return result;
}

// The port the printer is attached to (e.g. "USB001", "WSD-..."), or "" if unknown. Network printers need it filled in.
std::wstring PortOf(const std::wstring& printer) {
    HANDLE h = nullptr;
    if (!OpenPrinterW(const_cast<LPWSTR>(printer.c_str()), &h, nullptr) || !h) return std::wstring();
    std::wstring port;
    DWORD need = 0;
    GetPrinterW(h, 5, nullptr, 0, &need);
    if (need > 0) {
        std::vector<BYTE> pb(need);
        if (GetPrinterW(h, 5, pb.data(), need, &need)) {
            const PRINTER_INFO_5W* pi = reinterpret_cast<const PRINTER_INFO_5W*>(pb.data());
            if (pi->pPortName) port = pi->pPortName;
        }
    }
    ClosePrinter(h);
    return port;
}

// A DEVNAMES block naming `printer` so the Print dialog opens with it selected. nullptr on failure.
HGLOBAL BuildDevNames(const std::wstring& printer) {
    if (printer.empty()) return nullptr;
    const std::wstring driver = L"winspool";
    const std::wstring port = PortOf(printer);
    const size_t headerWords = sizeof(DEVNAMES) / sizeof(wchar_t);
    const size_t totalWords = headerWords + driver.size() + 1 + printer.size() + 1 + port.size() + 1;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, totalWords * sizeof(wchar_t));
    if (!g) return nullptr;
    DEVNAMES* dn = static_cast<DEVNAMES*>(GlobalLock(g));
    if (!dn) { GlobalFree(g); return nullptr; }
    wchar_t* base = reinterpret_cast<wchar_t*>(dn);
    size_t pos = headerWords;
    dn->wDriverOffset = (WORD)pos;
    memcpy(base + pos, driver.c_str(), (driver.size() + 1) * sizeof(wchar_t));
    pos += driver.size() + 1;
    dn->wDeviceOffset = (WORD)pos;
    memcpy(base + pos, printer.c_str(), (printer.size() + 1) * sizeof(wchar_t));
    pos += printer.size() + 1;
    dn->wOutputOffset = (WORD)pos;
    memcpy(base + pos, port.c_str(), (port.size() + 1) * sizeof(wchar_t));
    dn->wDefault = 0;
    GlobalUnlock(g);
    return g;
}
// Prints the Breakdown report onto `hdc` (a printer DC) and deletes the DC. Every failure is reported in a message box.
void PrintOnDc(HWND owner, HDC hdc, const std::vector<Entry>& entries,
               const std::wstring& debtorText, const std::wstring& cashText) {
    int pageWidthPx = GetDeviceCaps(hdc, HORZRES);
    int pageHeightPx = GetDeviceCaps(hdc, VERTRES);
    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);

    std::wstring dimError;
    if (!ValidateReportPageDimensions(pageWidthPx, pageHeightPx, dpiX, dpiY, &dimError)) {
        MessageBoxW(owner, dimError.c_str(), L"Print", MB_OK | MB_ICONERROR);
        DeleteDC(hdc);
        return;
    }

    DOCINFOW di{};
    di.cbSize = sizeof(di);
    di.lpszDocName = L"Fish Balance - Breakdown Report";
    if (StartDocW(hdc, &di) <= 0) {
        MessageBoxW(owner, L"Windows couldn't start the print job.", L"Print", MB_OK | MB_ICONERROR);
        DeleteDC(hdc);
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

    if (result.pageCapHit) {
        MessageBoxW(owner, (L"This report has more than " + std::to_wstring(kReportMaxPages) +
            L" pages. Only the first " + std::to_wstring(kReportMaxPages) +
            L" were printed.").c_str(), L"Print", MB_OK | MB_ICONWARNING);
    }
}

// Windows 11 22H2+ replaces the classic Print dialog with a "unified" one that ignores the printer we pre-select (and
// has an empty preview pane). Giving PrintDlg a print hook procedure makes Windows use the classic dialog for this call
// only (Microsoft Q&A, "force use of the old print dialog in Windows 11 22H2"). The hook does nothing: returning 0
// leaves every message to the dialog's own handling.
UINT_PTR CALLBACK PrintHookProc(HWND, UINT, WPARAM, LPARAM) { return 0; }
} // namespace

void PrintBreakdownJob(HWND owner, const std::vector<Entry>& entries,
                       const std::wstring& debtorText, const std::wstring& cashText,
                       const PrintDefaults& defaults) {
    PRINTDLGW pd{};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = owner;
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION;
    // The classic dialog (forced by the hook) is only needed when a specific printer has been chosen in Settings: the
    // new Windows 11 dialog cannot be told which printer to start on, it always starts on the Windows default. With
    // "Windows default printer" chosen (blank), the new-style dialog is used.
    if (!defaults.printer.empty()) {
        pd.Flags |= PD_ENABLEPRINTHOOK;
        pd.lpfnPrintHook = PrintHookProc;
    }
    // v0.23.0: start from the saved printer / paper / orientation. Only pre-fill when BOTH blocks were built; the
    // dialog frees whatever is in pd.hDevMode/hDevNames, so they must never be left half-set.
    {
        const std::wstring name = ResolvePrinterName(defaults);
        HGLOBAL dn = BuildDevNames(name);
        HGLOBAL dm = dn ? BuildDevMode(name, defaults) : nullptr; // may be null: the dialog then uses the printer's defaults
        if (dn) { pd.hDevNames = dn; pd.hDevMode = dm; }
        // Visible in Visual Studio's Output window while debugging: which pre-fill step worked.
        OutputDebugStringW((L"[print] saved='" + defaults.printer + L"' using='" + name + L"' devnames=" +
                            (dn ? L"ok" : L"FAILED") + L" devmode=" + (dm ? L"ok" : L"none") + L"\n").c_str());
    }
    if (!PrintDlgW(&pd)) return; // user cancelled, or no printers installed

    // PrintDlgW allocates these two blocks; we're responsible for freeing
    // them regardless of how this function exits from here on.
    auto freeDlgHandles = [&]() {
        if (pd.hDevMode) GlobalFree(pd.hDevMode);
        if (pd.hDevNames) GlobalFree(pd.hDevNames);
    };

    HDC hdc = pd.hDC;
    if (!hdc) { freeDlgHandles(); return; }

    PrintOnDc(owner, hdc, entries, debtorText, cashText); // deletes hdc
    freeDlgHandles();
}

// v0.23.2 (WinUI's own Print dialog): print straight to a chosen printer with no Windows dialog.
void PrintBreakdownJobDirect(HWND owner, const std::vector<Entry>& entries,
                             const std::wstring& debtorText, const std::wstring& cashText,
                             const PrintChoice& choice) {
    PrintDefaults d;
    d.printer = choice.printer;
    d.orientation = choice.orientation;
    d.paper = choice.paper;
    const std::wstring name = ResolvePrinterName(d);
    if (name.empty()) {
        MessageBoxW(owner, L"No printer is available. Install a printer (or use Microsoft Print to PDF) and try again.",
                    L"Print", MB_OK | MB_ICONERROR);
        return;
    }
    HGLOBAL hdm = BuildDevMode(name, d);
    DEVMODEW* dm = hdm ? static_cast<DEVMODEW*>(GlobalLock(hdm)) : nullptr;
    if (dm && choice.copies > 1) {
        dm->dmFields |= DM_COPIES;
        dm->dmCopies = static_cast<short>(choice.copies > 99 ? 99 : choice.copies);
    }
    HDC hdc = CreateDCW(L"WINSPOOL", name.c_str(), nullptr, dm);
    if (hdm) { if (dm) GlobalUnlock(hdm); GlobalFree(hdm); }
    if (!hdc) {
        MessageBoxW(owner, (L"Windows couldn't open the printer \"" + name + L"\".").c_str(), L"Print", MB_OK | MB_ICONERROR);
        return;
    }
    PrintOnDc(owner, hdc, entries, debtorText, cashText);
}

bool GetPreviewPageSpec(PreviewPageSpec& spec, std::wstring* error, const PrintDefaults& defaults) {
    // Size the preview to the default printer's page, so it's a realistic representation of what will
    // actually print. If there's no default printer configured at all (e.g. a fresh machine with none
    // installed), or getting its info fails, fall back to a generic Letter-sized page (the PreviewPageSpec
    // defaults) so preview still works.
    int pageWidthPx = spec.widthPx, pageHeightPx = spec.heightPx, dpiX = spec.dpiX, dpiY = spec.dpiY;
    // v0.23.0: the saved printer (if still installed, else the default) with the saved paper/orientation, so the
    // preview's page shape matches what will print.
    const std::wstring printerName = ResolvePrinterName(defaults);
    if (!printerName.empty()) {
        HGLOBAL hdm = BuildDevMode(printerName, defaults);
        const DEVMODEW* dm = hdm ? static_cast<const DEVMODEW*>(GlobalLock(hdm)) : nullptr;
        HDC icDC = CreateICW(L"WINSPOOL", printerName.c_str(), nullptr, dm);
        if (hdm) { if (dm) GlobalUnlock(hdm); GlobalFree(hdm); }
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
