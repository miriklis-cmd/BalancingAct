// UiState.cpp - definitions for the globals declared in UiState.h.
#include "UiState.h"

HWND g_hMainWnd = nullptr;
int g_dpi = 96;
HWND hTab = nullptr;
HWND hStatusBar = nullptr;
HFONT g_normalFont = nullptr, g_boldFont = nullptr;
HWND hLblSupplier, hCmbSupplier, hLblProduct, hCmbProduct;
HWND hLblKgs, hEditKgs, hLblPrice, hEditPrice, hBtnAdd, hBtnDelete, hBtnEdit, hBtnCancelEdit;
HWND hLblFilter, hEditFilter;
HWND hLblDate, hDtpDate, hLblNotes, hEditNotes, hBtnDuplicate, hBtnDuplicateSupSpec;
HWND hBtnFinalize = nullptr;
HWND hListEntries;
HWND hLblBookCur, hEditBookCur, hBtnStartBook;
std::wstring g_currentBook;
wchar_t g_currentBookType = 0;
HWND hGrpRecon, hLblDebtor, hEditDebtor, hLblCash, hEditCash;
HWND hLblBook, hLblEntered, hLblDiff;
HWND hLblStatus, hDivRecon1, hDivRecon2;
std::vector<HIMAGELIST> g_buttonIconLists;
HWND hLblOvBook, hLblOvGrand, hLblOvDiff, hListOverview;
HWND hListBooks = nullptr;
HWND hListBreakdown, hBtnPrintBreakdown, hBtnEmailSuppliers, hBtnPrintPreview;
HWND hListBySpecies;
std::vector<HWND> g_tab1Ctrls, g_tab2Ctrls, g_tab3Ctrls, g_tab4Ctrls;
bool g_diffOk = true;
bool g_ovDiffOk = true;
bool g_reconciliationValid = true;
bool g_acBusy = false;
int g_prevSupplierLen = 0;
int g_prevProductLen = 0;
int g_editIndex = -1;
std::vector<int> g_filteredIndices;
int g_sortColumn = -1;
bool g_sortAscending = true;
HMENU g_hEditMenu = nullptr;
HMENU g_hRecentMenu = nullptr;
HWND g_hManageWnd = nullptr;
HWND g_hManageList = nullptr, g_hManageTarget = nullptr;
HWND g_hManageRadioSupplier = nullptr, g_hManageRadioSpecies = nullptr;
HWND g_hManageHint = nullptr, g_hManageTargetLbl = nullptr, g_hManageApplyBtn = nullptr, g_hManageCloseBtn = nullptr;
HWND g_hManageEmailLbl = nullptr, g_hManageEmailEdit = nullptr, g_hManageEmailBtn = nullptr;
bool g_manageIsSupplier = true;
std::vector<std::wstring> g_manageValues;
std::vector<RenderedPage> g_previewPages;
int g_previewPageIndex = 0;
HWND g_hPreviewWnd = nullptr;
HWND g_hPreviewPrevBtn = nullptr, g_hPreviewNextBtn = nullptr;
HWND g_hPreviewPageLbl = nullptr, g_hPreviewPrintBtn = nullptr, g_hPreviewCloseBtn = nullptr;
HWND g_hFinalizeWnd = nullptr, g_hFinalizeDtp = nullptr, g_hFinalizeLbl = nullptr;

int S(int px) { return MulDiv(px, g_dpi, 96); }
