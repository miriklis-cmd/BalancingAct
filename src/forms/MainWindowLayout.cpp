// MainWindowLayout.cpp - the main window's control helpers, layout and tab switching.
// Moved unchanged out of main.cpp in v0.10.9 (split stage 6b). Gotcha (ARCHITECTURE.md):
// every layout dimension goes through S() (DPI scaling) - never a raw pixel literal.
#include "MainWindowLayout.h"

#include "../PlatformWin.h"
#include <cwchar>
#include <string>
#include <vector>

#include "../services/ReportRender.h" // UniqueFont / UniqueDC / ScopedSelect
#include "UiState.h"
#include "MainWindowApi.h" // MakeControl

// ---------------------------------------------------------------------------
// Control creation helpers
// ---------------------------------------------------------------------------

HWND MakeControl(LPCWSTR cls, LPCWSTR text, DWORD style, int id, HWND parent) {
    HWND h = CreateWindowExW(0, cls, text, style | WS_CHILD, 0, 0, 0, 0,
                              parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    if (h) SendMessageW(h, WM_SETFONT, (WPARAM)g_normalFont, TRUE);
    return h;
}

void AddColumn(HWND lv, int idx, LPCWSTR text, int width) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = const_cast<LPWSTR>(text);
    col.cx = S(width); // v0.9.61: DPI-scaled like every other layout dimension (columns were fixed pixels, so text truncated at 125-150% scaling)
    col.iSubItem = idx;
    ListView_InsertColumn(lv, idx, &col);
}

void InitFonts() {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_normalFont = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW lfBold = ncm.lfMessageFont;
    lfBold.lfWeight = FW_BOLD;
    g_boldFont = CreateFontIndirectW(&lfBold);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

// v0.9.60: button icons + taller list rows, per Jack's mockup.
//
// MakeGlyphButtonImageList renders one Segoe MDL2 Assets glyph (the icon
// font built into Windows 10/11 - no icon files/resources needed) into a
// 32-bit alpha image list holding the six button states in the order
// BCM_SETIMAGELIST expects (normal, hot, pressed, disabled, defaulted,
// stylus-hot). Returns nullptr - and the button simply keeps its plain text
// - if anything fails, including the font not actually being installed
// (checked via GetTextFace, since GDI silently substitutes a different font
// when the requested one is missing).
HIMAGELIST MakeGlyphButtonImageList(wchar_t glyph, COLORREF color, int sizePx) {
    if (sizePx < 8 || sizePx > 128) return nullptr;
    LOGFONTW lf{};
    lf.lfHeight = -sizePx;
    lf.lfWeight = FW_NORMAL;
    lf.lfQuality = ANTIALIASED_QUALITY;
    const wchar_t* face = L"Segoe MDL2 Assets";
#ifdef _MSC_VER
    wcsncpy_s(lf.lfFaceName, LF_FACESIZE, face, _TRUNCATE);
#else
    wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
    lf.lfFaceName[LF_FACESIZE - 1] = 0;
#endif
    UniqueFont font(CreateFontIndirectW(&lf));
    if (!font) return nullptr;
    UniqueDC dc(CreateCompatibleDC(nullptr));
    if (!dc) return nullptr;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sizePx;
    bmi.bmiHeader.biHeight = -sizePx; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hbm = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbm || !bits) { if (hbm) DeleteObject(hbm); return nullptr; }

    const size_t n = (size_t)sizePx * (size_t)sizePx;
    std::vector<unsigned char> coverage(n, 0);
    bool glyphOk = false;
    {
        // Draw while the bitmap is selected into the DC; everything that
        // hands the bitmap to another API happens AFTER this scope has
        // restored the DC (v0.9.61: a bitmap must not be selected into a DC
        // when passed to ImageList_Add / CreateIconIndirect - v0.9.60 did
        // that and produced solid black squares instead of icons).
        ScopedSelect selBmp(dc.get(), hbm);
        ScopedSelect selFont(dc.get(), font.get());
        wchar_t actual[LF_FACESIZE] = {};
        if (GetTextFaceW(dc.get(), LF_FACESIZE, actual) > 0 && _wcsicmp(actual, face) == 0) {
            // Black glyph on white; the resulting grey level becomes the alpha channel.
            RECT r{ 0, 0, sizePx, sizePx };
            FillRect(dc.get(), &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
            SetBkMode(dc.get(), TRANSPARENT);
            SetTextColor(dc.get(), RGB(0, 0, 0));
            DrawTextW(dc.get(), &glyph, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            GdiFlush();
            const unsigned char* px = static_cast<const unsigned char*>(bits);
            for (size_t i = 0; i < n; i++) coverage[i] = (unsigned char)(255 - px[i * 4]); // B channel of the grey render
            glyphOk = true;
        }
    }
    if (!glyphOk) { DeleteObject(hbm); return nullptr; }

    HBITMAP hMask = CreateBitmap(sizePx, sizePx, 1, 1, nullptr); // all-zero mask; the colour bitmap's alpha does the work
    HIMAGELIST il = ImageList_Create(sizePx, sizePx, ILC_COLOR32, 6, 0);
    bool ok = (hMask != nullptr && il != nullptr);
    if (ok) {
        const COLORREF states[6] = { color, color, color, RGB(160, 160, 160), color, color };
        for (int st = 0; st < 6 && ok; st++) {
            unsigned char* out = static_cast<unsigned char*>(bits);
            const unsigned r8 = GetRValue(states[st]), g8 = GetGValue(states[st]), b8 = GetBValue(states[st]);
            for (size_t i = 0; i < n; i++) {
                out[i * 4 + 0] = (unsigned char)b8; // straight (non-premultiplied) BGRA, as icons expect
                out[i * 4 + 1] = (unsigned char)g8;
                out[i * 4 + 2] = (unsigned char)r8;
                out[i * 4 + 3] = coverage[i];
            }
            ICONINFO ii{};
            ii.fIcon = TRUE;
            ii.hbmMask = hMask;
            ii.hbmColor = hbm;
            HICON hIcon = CreateIconIndirect(&ii); // copies both bitmaps
            if (!hIcon) { ok = false; break; }
            if (ImageList_AddIcon(il, hIcon) < 0) ok = false;
            DestroyIcon(hIcon);
        }
    }
    if (hMask) DeleteObject(hMask);
    DeleteObject(hbm);
    if (!ok && il) { ImageList_Destroy(il); il = nullptr; }
    return ok ? il : nullptr;
}

// Attaches a glyph icon to the left of a push button's text. On any
// failure the button is left exactly as it was (plain text).
void AddButtonGlyphIcon(HWND btn, wchar_t glyph, COLORREF color) {
    if (!btn) return;
    HIMAGELIST il = MakeGlyphButtonImageList(glyph, color, S(16));
    if (!il) return;
    BUTTON_IMAGELIST bil{};
    bil.himl = il;
    bil.margin = { S(10), 0, S(6), 0 };
    bil.uAlign = BUTTON_IMAGELIST_ALIGN_LEFT;
    if (SendMessageW(btn, BCM_SETIMAGELIST, 0, (LPARAM)&bil))
        g_buttonIconLists.push_back(il); // freed at WM_DESTROY
    else
        ImageList_Destroy(il);
}

// Standard trick for taller ListView rows: give the list a (blank) small
// image list of the desired height. The ListView owns and frees it.
void SetListRowHeight(HWND lv, int heightPx) {
    if (!lv || heightPx < 1) return;
    HIMAGELIST il = ImageList_Create(1, heightPx, ILC_COLOR, 1, 1);
    if (il) ListView_SetImageList(lv, il, LVSIL_SMALL);
}

void LayoutAll(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    int statusBarHeight = S(24);
    MoveWindow(hTab, 0, 0, rc.right, rc.bottom - statusBarHeight, TRUE);
    MoveWindow(hStatusBar, 0, rc.bottom - statusBarHeight, rc.right, statusBarHeight, TRUE);

    RECT disp = rc;
    disp.bottom -= statusBarHeight;
    TabCtrl_AdjustRect(hTab, FALSE, &disp);

    int left = disp.left + S(10);
    int top = disp.top + S(10);
    int right = disp.right - S(10);

    // ---- Tab 1 ----
    // v0.9.42: these 4 labels used to sit at top + S(3) (a fixed pixel
    // nudge, guessed to visually match a plain EDIT box's own text
    // baseline) while their paired controls sit at top. That guess didn't
    // hold for the two COMBOBOX controls (Supplier/Species) - a themed
    // dropdown's closed-box text doesn't vertically center at quite the
    // same offset a plain EDIT box does, and the mismatch is DPI/font
    // dependent, not a fixed number of pixels - Jack: "Supplier, species,
    // kg, price still misaligned." Fixed properly instead of re-guessing
    // another offset: each label now shares the exact same top AND height
    // (S(22), matching the visible closed-box height every one of these
    // controls actually renders at - NOT hCmbSupplier/hCmbProduct's own
    // S(200), which is their dropped-down list height, not their closed
    // size) as its paired control, with SS_CENTERIMAGE (set at creation)
    // doing the vertical centering from real font metrics - the same
    // mechanism Windows already uses to center the single line of text
    // inside the neighboring EDIT/COMBOBOX itself, so the two always agree
    // regardless of DPI or font, instead of two independent guesses that
    // can drift apart.
    // v0.9.59: Data Entry layout reworked to match Jack's mockup (2026-10-14
    // screenshot): field labels sit ABOVE their boxes instead of beside
    // them, the four entry fields plus Add Entry stretch to fill the window
    // width, Filter sits at the right end of the button row, and the
    // reconciliation box is grouped into columns (Debtor/Cash on the left,
    // Book/Entered/Difference in the middle, Finalize Day at the right).
    // Positions only - no control, handler or text-formatting logic changed.
    // All sizes go through S() (DPI scaling - see ARCHITECTURE.md).
    const int lblH = S(18);   // label row height
    const int boxH = S(22);   // closed height of every EDIT/COMBOBOX/date box
    const int btnH = S(28);
    const int gap = S(14);
    const int labelToBox = S(20);

    // -- Row A: Supplier | Species | Kgs | Price | Add Entry --
    const int addW = S(120);
    int fieldsW = (right - left) - addW - 4 * gap;
    if (fieldsW < S(400)) fieldsW = S(400);
    int supW = fieldsW * 3 / 10, spW = fieldsW * 3 / 10;
    int kgW = fieldsW * 2 / 10, prW = fieldsW - supW - spW - kgW;
    int x = left;
    int boxY = top + labelToBox;
    MoveWindow(hLblSupplier, x, top, supW, lblH, TRUE);
    MoveWindow(hCmbSupplier, x, boxY, supW, S(200), TRUE); // S(200) = dropped-down list height, closed box is boxH
    x += supW + gap;
    MoveWindow(hLblProduct, x, top, spW, lblH, TRUE);
    MoveWindow(hCmbProduct, x, boxY, spW, S(200), TRUE);
    x += spW + gap;
    MoveWindow(hLblKgs, x, top, kgW, lblH, TRUE);
    MoveWindow(hEditKgs, x, boxY, kgW, boxH, TRUE);
    x += kgW + gap;
    MoveWindow(hLblPrice, x, top, prW, lblH, TRUE);
    MoveWindow(hEditPrice, x, boxY, prW, boxH, TRUE);
    x += prW + gap;
    MoveWindow(hBtnAdd, x, boxY - S(3), addW, btnH, TRUE);

    // -- Row B: Date | Notes (stretches) | Duplicate buttons --
    int row1b = boxY + boxH + S(8); // v0.12.0: tighter rows so the grid can show ~16 entries
    // v0.12.0: Date | Notes | Book (which book new entries go into) | the two Duplicate buttons
    int dateW = S(150), dup1W = S(190), dup2W = S(220), bookW = S(190);
    int notesW = (right - left) - dateW - bookW - dup1W - dup2W - 5 * gap;
    if (notesW < S(120)) notesW = S(120);
    int boxYb = row1b + labelToBox;
    x = left;
    MoveWindow(hLblDate, x, row1b, dateW, lblH, TRUE);
    MoveWindow(hDtpDate, x, boxYb, dateW, boxH, TRUE);
    x += dateW + gap;
    MoveWindow(hLblNotes, x, row1b, notesW, lblH, TRUE);
    MoveWindow(hEditNotes, x, boxYb, notesW, boxH, TRUE);
    x += notesW + gap;
    MoveWindow(hLblBookCur, x, row1b, bookW, lblH, TRUE);
    MoveWindow(hEditBookCur, x, boxYb, bookW, boxH, TRUE);
    x += bookW + gap;
    MoveWindow(hBtnDuplicate, x, boxYb - S(3), dup1W, btnH, TRUE);
    x += dup1W + gap;
    MoveWindow(hBtnDuplicateSupSpec, x, boxYb - S(3), dup2W, btnH, TRUE);

    // -- Row C: Delete | Edit | Cancel Edit ............ Filter --
    int row2 = boxYb + boxH + S(8);
    MoveWindow(hBtnDelete, left, row2, S(180), btnH, TRUE);
    MoveWindow(hBtnEdit, left + S(190), row2, S(160), btnH, TRUE);
    MoveWindow(hBtnCancelEdit, left + S(360), row2, S(160), btnH, TRUE);
    MoveWindow(hBtnStartBook, left + S(530), row2, S(130), btnH, TRUE);
    int filterW = S(260);
    int filterX = right - filterW;
    MoveWindow(hEditFilter, filterX, row2 + S(3), filterW, boxH, TRUE);
    MoveWindow(hLblFilter, filterX - S(50), row2 + S(7), S(45), lblH, TRUE);

    int listTop = row2 + btnH + S(8);
    // v0.12.0: the reconciliation box keeps its layout but is two tight rows (was ~140px tall).
    int reconHeight = S(66);
    int listBottom = disp.bottom - reconHeight - S(12);
    if (listBottom < listTop + S(60)) listBottom = listTop + S(60);
    MoveWindow(hListEntries, left, listTop, right - left, listBottom - listTop, TRUE);

    // -- Reconciliation box --
    // v0.9.60, per Jack's mockup: left column Debtor / Cash; thin divider;
    // middle column Book Total / Entered Total (bold); thin divider; a
    // status column (bold tick/cross badge, then the Difference or - when
    // the check fails closed - the reason, which can need two lines); and
    // Finalize Day at the far right.
    int reconTop = listBottom + S(10);
    MoveWindow(hGrpRecon, left, reconTop, right - left, disp.bottom - reconTop - S(3), TRUE);
    int rowR1 = reconTop + S(15);
    int rowR2 = reconTop + S(40);
    int c1x = left + S(15);
    MoveWindow(hLblDebtor, c1x, rowR1, S(115), boxH, TRUE);
    MoveWindow(hEditDebtor, c1x + S(120), rowR1, S(200), boxH, TRUE);
    MoveWindow(hLblCash, c1x, rowR2, S(115), boxH, TRUE);
    MoveWindow(hEditCash, c1x + S(120), rowR2, S(200), boxH, TRUE);
    int div1x = c1x + S(120) + S(200) + gap;
    int divH = S(48);
    MoveWindow(hDivRecon1, div1x, rowR1 - S(4), S(2), divH, TRUE);
    int c2x = div1x + S(2) + gap;
    int c2w = S(230);
    MoveWindow(hLblBook, c2x, rowR1, c2w, boxH, TRUE);
    MoveWindow(hLblEntered, c2x, rowR2, c2w, boxH, TRUE);
    int div2x = c2x + c2w + gap;
    MoveWindow(hDivRecon2, div2x, rowR1 - S(4), S(2), divH, TRUE);
    int c3x = div2x + S(2) + gap;
    int finW = S(140);
    int finX = right - S(15) - finW;
    int c3w = finX - gap - c3x;
    if (c3w < S(160)) c3w = S(160);
    MoveWindow(hLblStatus, c3x, rowR1, c3w, boxH, TRUE);
    MoveWindow(hLblDiff, c3x, rowR2, c3w, boxH, TRUE);
    MoveWindow(hBtnFinalize, finX, rowR1 + S(8), finW, S(30), TRUE);

    // ---- Tab 2 ----
    MoveWindow(hLblOvBook, left, top, S(300), S(22), TRUE);
    MoveWindow(hLblOvGrand, left + S(310), top, S(300), S(22), TRUE);
    MoveWindow(hLblOvDiff, left + S(620), top, S(300), S(22), TRUE);
    // v0.12.0: supplier table on the left (the bigger share), Books table on the right
    {
        int ovTop = top + S(32);
        int ovH = disp.bottom - ovTop - S(5);
        int ovGap = S(12);
        int booksW = S(640); // columns add up to ~570 + scrollbar room, so no horizontal scrolling
        if (booksW > (right - left) * 11 / 20) booksW = (right - left) * 11 / 20;
        int supW2 = (right - left) - booksW - ovGap;
        MoveWindow(hListOverview, left, ovTop, supW2, ovH, TRUE);
        MoveWindow(hListBooks, left + supW2 + ovGap, ovTop, booksW, ovH, TRUE);
    }

    // ---- Tab 3 ----
    MoveWindow(hBtnPrintPreview, left, top, S(170), S(28), TRUE);
    MoveWindow(hBtnPrintBreakdown, left + S(180), top, S(190), S(28), TRUE);
    MoveWindow(hBtnEmailSuppliers, left + S(380), top, S(190), S(28), TRUE);
    int bdListTop = top + S(38);
    MoveWindow(hListBreakdown, left, bdListTop, right - left, disp.bottom - bdListTop - S(5), TRUE);

    // ---- Tab 4 - By Species ----
    MoveWindow(hListBySpecies, left, top, right - left, disp.bottom - top - S(5), TRUE);
}

void ShowTab(int idx) {
    int s1 = (idx == 0) ? SW_SHOW : SW_HIDE;
    int s2 = (idx == 1) ? SW_SHOW : SW_HIDE;
    int s3 = (idx == 2) ? SW_SHOW : SW_HIDE;
    int s4 = (idx == 3) ? SW_SHOW : SW_HIDE;
    for (HWND h : g_tab1Ctrls) ShowWindow(h, s1);
    for (HWND h : g_tab2Ctrls) ShowWindow(h, s2);
    for (HWND h : g_tab3Ctrls) ShowWindow(h, s3);
    for (HWND h : g_tab4Ctrls) ShowWindow(h, s4);
}

// Known Win32/visual-styles quirk: a themed ComboBox can fail to paint its
// border/dropdown-arrow chrome the very first time it appears on screen,
// rendering correctly only after some later event (a mouse hover, a focus
// change, etc.) happens to trigger a repaint. Every other control here is
// created and positioned the exact same way and paints fine immediately,
// so this is specific to these two combo boxes.
//
// This is called several times - once synchronously right after the window
// becomes visible (in wWinMain), then again from three separate delayed
// one-shot timers at increasing delays (see WM_TIMER) - as a belt-and-
// suspenders approach, since the exact same single synchronous call that
// fixed this in v0.9.3 was reported to have stopped reliably fixing it by
// v0.9.10, and even the v0.9.11/v0.9.12 fixes (one 50ms retry, plus
// reordering WM_CREATE) were still seen failing intermittently as of
// v0.9.41 ("doesn't always happen, but see screenshot" - Jack). The likely
// explanation is a race with something else (possibly the status bar
// control, or an internal WM_SIZE-triggered relayout) re-invalidating
// these controls after an attempt but before the theme engine has settled
// - and apparently that race isn't always won within 50ms on every
// machine, hence the two further, later retries added in v0.9.42.
//
// v0.9.42 escalated the fix itself, not just the retry count/timing: a
// plain RedrawWindow (even with RDW_FRAME) only invalidates and repaints
// pixels - it doesn't force ComCtl32's visual-styles engine to redo its
// theme-handle setup for the control. Toggling visibility (SW_HIDE then
// SW_SHOW) before redrawing was meant to force that, but v0.9.42 testing
// showed it still isn't enough, and neither was v0.9.44's SWP_FRAMECHANGED
// escalation (forcing non-client recalculation) - both still left it
// broken on every launch.
//
// v0.9.46 added a temporary message-logging subclass to actually see what
// happens, instead of guessing again, and it gave a clear, specific
// answer: a hover produces ONLY WM_MOUSEMOVE -> WM_PAINT -> WM_ERASEBKGND
// on the affected control - no WM_NCPAINT, no WM_NCCALCSIZE at all. That
// directly disproves the v0.9.44 theory (this was never a non-client/frame
// problem) and explains why v0.9.42/v0.9.44's hide-show/frame-change fixes
// never worked even though the log showed THEM also producing
// WM_PAINT/WM_NCPAINT/WM_ERASEBKGND cycles at 328ms, 344ms, 391ms, 407ms,
// and again at 1141-1157ms after launch - none of that is the mechanism
// that actually fixes it. Only a genuine WM_MOUSEMOVE reaching the control
// does. (Log evidence also removed the temporary diagnostic subclass once
// it had done its job, per the plan when it was added.)
//
// v0.9.46's fix is a synthetic WM_MOUSEMOVE - reproducing exactly the
// interaction the log evidence showed actually works, without needing the
// user's real cursor to physically be over the control. This replaces the
// SWP_FRAMECHANGED/hide-show approach entirely rather than adding to it,
// since the log evidence shows that approach doesn't contribute to the fix.
void FixComboBoxFirstPaint() {
    HWND boxes[] = { hCmbSupplier, hCmbProduct };
    for (HWND h : boxes) {
        if (!h) continue;
        RECT rc{};
        GetClientRect(h, &rc);
        LPARAM pos = MAKELPARAM(5, (rc.bottom - rc.top) / 2);
        SendMessageW(h, WM_MOUSEMOVE, 0, pos);
        InvalidateRect(h, nullptr, TRUE);
        UpdateWindow(h);
    }
}
