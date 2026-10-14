// PrintPreviewWindow.h - the Print Preview popup (forms layer).
// Moved out of main.cpp unchanged in v0.10.6 (split stage 5).
#pragma once

#include "../PlatformWin.h"

// Renders the report and opens the preview popup (disables the main window until it closes).
void OpenPrintPreview(HWND owner);
