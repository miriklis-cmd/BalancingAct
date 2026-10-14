// PopupPlacement.h - keeps popup windows on screen (forms layer).
//
// v0.10.7: the popups used to be centred on the main window using fixed,
// DPI-scaled sizes. At high display scaling the Print Preview popup (S(820)
// tall) was taller than the screen's usable height, so its title bar and the
// top of the page were pushed off the top of the screen (reported by Jack,
// 2026-10-14, from a screenshot of the preview). This clamps the size to the
// monitor's work area (the screen minus the taskbar) and slides the window
// back on-screen.
#pragma once

#include "../PlatformWin.h"
#include "../core/FishBalanceCore.h"

// x/y/w/h are the OUTER window rectangle about to be passed to CreateWindowExW.
inline void FitPopupToWorkArea(HWND ownerForMonitor, int& x, int& y, int& w, int& h) {
    RECT wa = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    HMONITOR mon = MonitorFromWindow(ownerForMonitor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (mon && GetMonitorInfoW(mon, &mi)) wa = mi.rcWork;

    ClampRectIntoArea(wa.left, wa.top, wa.right, wa.bottom, x, y, w, h);
}
