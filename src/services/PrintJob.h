// PrintJob.h - printing the Breakdown report and sizing its preview (services layer).
//
// v0.12.10: moved out of main.cpp / PrintPreviewWindow.cpp so the Win32 app and the WinUI prototype run the
// SAME print code. Nothing here touches the Win32 app's globals - the entries and the Debtor/Cash text are
// parameters. The owner window is only used as the parent of the print dialog and of any error message.
#pragma once

#include "../PlatformWin.h"
#include <string>
#include <vector>
#include "../core/FishBalanceCore.h"

// v0.23.0 (Tools > Settings > Printing): where the Print dialog and the preview start. Blank printer = the Windows
// default printer; orientation 0 / paper 0 = whatever the printer is set to. The user can still change everything in
// the Print dialog - these are only the starting choices.
struct PrintDefaults {
    std::wstring printer;
    int orientation = 0; // 0 printer default, 1 portrait, 2 landscape
    int paper = 0;       // 0 printer default, 1 A4, 2 A3
};
// The names of the installed printers (for the Settings drop-down). Empty if none / enumeration fails.
std::vector<std::wstring> ListInstalledPrinters();

// Shows the standard Print dialog and prints the Breakdown report for `entries`. (To get a PDF, pick the
// built-in "Microsoft Print to PDF" printer.) Every failure is reported in a message box, never silently.
void PrintBreakdownJob(HWND owner, const std::vector<Entry>& entries,
                       const std::wstring& debtorText, const std::wstring& cashText,
                       const PrintDefaults& defaults = PrintDefaults());

// v0.23.2: print straight to `printer` with no Windows dialog (WinUI shows its own Print dialog and calls this).
// copies is passed to the driver (most honour it); orientation / paper as in PrintDefaults.
struct PrintChoice {
    std::wstring printer;
    int orientation = 0; // 0 printer default, 1 portrait, 2 landscape
    int paper = 0;       // 0 printer default, 1 A4, 2 A3
    int copies = 1;
};
void PrintBreakdownJobDirect(HWND owner, const std::vector<Entry>& entries,
                             const std::wstring& debtorText, const std::wstring& cashText,
                             const PrintChoice& choice);

// Page size for the on-screen preview: the default printer's page (or a Letter-sized fallback), at a capped
// DPI so a preview never needs a full printer-resolution bitmap. Returns false and fills *error when the
// size is not acceptable.
struct PreviewPageSpec {
    int widthPx = 850, heightPx = 1100, dpiX = 100, dpiY = 100;
};
bool GetPreviewPageSpec(PreviewPageSpec& spec, std::wstring* error, const PrintDefaults& defaults = PrintDefaults());
