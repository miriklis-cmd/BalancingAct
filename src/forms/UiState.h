// UiState.h - window handles and per-window UI state (forms layer).
//
// v0.10.2 (main.cpp split stage 2): these globals used to be `static` in
// main.cpp; now `extern` here, defined once in UiState.cpp. Meaning and
// initial values are unchanged. RenderedPage stays here for now and moves
// to the services layer when printing is split out (stage 4).
#pragma once

#include "../PlatformWin.h"
#include <string>
#include <vector>

struct RenderedPage {
    HBITMAP bitmap = nullptr;
    void* bits = nullptr; // owned by the DIB section behind `bitmap`, not separately freed
    int width = 0, height = 0;
};

// Scales a 96-dpi pixel size by the current monitor DPI (g_dpi).
int S(int px);

extern HWND g_hMainWnd;

// The app's manifest declares DPI awareness, which means Windows will NOT
// auto-stretch our UI on higher-DPI displays (125%/150%/etc, common on
// modern monitors) - so every hardcoded pixel size in the layout code needs
// to be scaled by this factor ourselves, or text renders larger than the
// (unscaled) control it sits in and gets clipped top/bottom or side to side.
extern int g_dpi;
extern HWND hTab;
extern HWND hStatusBar;
extern HFONT g_normalFont, g_boldFont;

// Tab 1
extern HWND hLblSupplier, hCmbSupplier, hLblProduct, hCmbProduct;
extern HWND hLblKgs, hEditKgs, hLblPrice, hEditPrice, hBtnAdd, hBtnDelete, hBtnEdit, hBtnCancelEdit;
extern HWND hLblFilter, hEditFilter;
extern HWND hLblDate, hDtpDate, hLblNotes, hEditNotes, hBtnDuplicate, hBtnDuplicateSupSpec;
extern HWND hBtnFinalize; // "Finalize Day" / "Un-finalize Day" - relabeled in place, same pattern as Add/Update Entry
extern HWND hListEntries;
extern HWND hGrpRecon, hLblDebtor, hEditDebtor, hLblCash, hEditCash;
extern HWND hLblBook, hLblEntered, hLblDiff;
// v0.9.60: Tab 1 reconciliation polish (see CHANGELOG [0.9.60]): a bold
// "tick + Balanced" / "cross + Out of balance" badge, and two thin vertical
// divider lines between the reconciliation columns.
extern HWND hLblStatus, hDivRecon1, hDivRecon2;
// Owned icon image lists for the Delete/Edit/Cancel Edit buttons (BCM_SETIMAGELIST
// does not take ownership) - destroyed at WM_DESTROY.
extern std::vector<HIMAGELIST> g_buttonIconLists;

// Tab 2
extern HWND hLblOvBook, hLblOvGrand, hLblOvDiff, hListOverview;

// Tab 3
extern HWND hListBreakdown, hBtnPrintBreakdown, hBtnEmailSuppliers, hBtnPrintPreview;

// Tab 4 - By Species
extern HWND hListBySpecies;

extern std::vector<HWND> g_tab1Ctrls, g_tab2Ctrls, g_tab3Ctrls, g_tab4Ctrls;
extern bool g_diffOk;
extern bool g_ovDiffOk;
// F5 (Phase 1): tracks whether Debtor/Cash currently contain a validly
// parseable sum expression at all (as opposed to a validly parseable one
// that merely doesn't balance yet - g_diffOk). Finalize Day and the report
// builder must both refuse to proceed when this is false, since there is
// no trustworthy total to check or print.
extern bool g_reconciliationValid;

// Autocomplete state for the Supplier / Species combo boxes.
extern bool g_acBusy;
extern int g_prevSupplierLen;
extern int g_prevProductLen;

// -1 means the form is in "add new entry" mode; otherwise it's the index in
// g_entries currently loaded into the form for editing.
extern int g_editIndex;

// Maps the currently-displayed row in hListEntries back to its index in
// g_entries (needed because the filter box can hide non-matching rows).
extern std::vector<int> g_filteredIndices;

// Sortable-column state for hListEntries.
extern int g_sortColumn;
extern bool g_sortAscending;
extern HMENU g_hEditMenu;
extern HMENU g_hRecentMenu;

// Manage Supplier/Species Names popup window.
extern HWND g_hManageWnd;
extern HWND g_hManageList, g_hManageTarget;
extern HWND g_hManageRadioSupplier, g_hManageRadioSpecies;
extern HWND g_hManageHint, g_hManageTargetLbl, g_hManageApplyBtn, g_hManageCloseBtn;
extern HWND g_hManageEmailLbl, g_hManageEmailEdit, g_hManageEmailBtn;
extern bool g_manageIsSupplier;
extern std::vector<std::wstring> g_manageValues;

// A rendered report page: a device-independent bitmap (DIB), so the exact
// same rendering can be shown on screen (Print Preview) and sent to a real
// printer (StretchDIBits) without redrawing or risking the two diverging.

// Print Preview popup window.
extern std::vector<RenderedPage> g_previewPages;
extern int g_previewPageIndex;
extern HWND g_hPreviewWnd;
extern HWND g_hPreviewPrevBtn, g_hPreviewNextBtn;
extern HWND g_hPreviewPageLbl, g_hPreviewPrintBtn, g_hPreviewCloseBtn;

// Finalize Day date-prompt popup window (modeled on the Manage Names popup).
extern HWND g_hFinalizeWnd, g_hFinalizeDtp, g_hFinalizeLbl;
