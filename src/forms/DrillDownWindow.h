// DrillDownWindow.h - the Breakdown drill-down popup (forms layer). v0.12.6.
// Double-click a row on the Breakdown tab to see the entries behind it (supplier, species, date,
// kgs, price, total) with a total that matches the row. Read-only; disables the main window while open.
#pragma once

#include "../core/FishBalanceCore.h" // BreakdownRowRef

// Opens the popup for one Breakdown row (see BuildBreakdownRowRefs in FishBalanceCore.h).
void OpenDrillDownWindow(const BreakdownRowRef& ref);
