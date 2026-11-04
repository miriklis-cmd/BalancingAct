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

// Shows the standard Print dialog and prints the Breakdown report for `entries`. (To get a PDF, pick the
// built-in "Microsoft Print to PDF" printer.) Every failure is reported in a message box, never silently.
void PrintBreakdownJob(HWND owner, const std::vector<Entry>& entries,
                       const std::wstring& debtorText, const std::wstring& cashText);

// Page size for the on-screen preview: the default printer's page (or a Letter-sized fallback), at a capped
// DPI so a preview never needs a full printer-resolution bitmap. Returns false and fills *error when the
// size is not acceptable.
struct PreviewPageSpec {
    int widthPx = 850, heightPx = 1100, dpiX = 100, dpiY = 100;
};
bool GetPreviewPageSpec(PreviewPageSpec& spec, std::wstring* error);
