// ReportRender.cpp - moved out of main.cpp in v0.10.5 (split stage 4b).
// Report rendering - shared by Print Preview and actual printing, so the
// two can never show different content. Renders into device-independent
// bitmaps (DIBs): reliable for on-screen display (Print Preview) AND for
// sending to a real printer via StretchDIBits, which avoids a known GDI
// pitfall where BitBlt-ing a screen-compatible bitmap directly to some
// printer drivers silently fails or prints garbage.
#include "ReportRender.h"

#include <algorithm>
#include <cwchar>
#include <map>

#include "../data/AppData.h"

// Phase 2 / F10-F11 (2026-10-02) resource-safety rewrite - summary (full
// writeup in CHANGELOG.md):
//   - Preview already rendered at a capped, screen-appropriate DPI before
//     this phase (kPreviewDpiCap in OpenPrintPreview, below), not printer
//     DPI - but nothing validated the resulting dimensions or bounded the
//     TOTAL memory an in-memory page collection could use. Both are now
//     checked up front, before any GDI object is created, via
//     ValidateReportPageDimensions/PreviewBudgetAllows (FishBalanceCore.h).
//   - Real printing used to render EVERY page at full printer DPI into
//     memory, THEN print them all from that in-memory collection - an
//     unbounded-in-practice amount of memory for a large report.
//     RenderReportPages() now accepts an optional per-page callback; when
//     provided (printing), each page is handed to the caller and freed
//     immediately instead of being accumulated, so printing never holds
//     more than one full-resolution page bitmap at a time. Preview (no
//     callback) still accumulates all pages, but only at the bounded
//     preview resolution, within the budget checked above.
//   - Every font/DC/bitmap creation is now checked, with a small set of
//     RAII wrappers (just below) so a partially-built page's resources are
//     always released on any failure path.
//   - RenderReportPages() now returns a RenderReportResult (ok/error/
//     pageCapHit) instead of a bare vector, so a failure - or hitting the
//     page-count cap - is reported rather than silent.

void FreeRenderedPages(std::vector<RenderedPage>& pages) {
    for (auto& p : pages) if (p.bitmap) DeleteObject(p.bitmap);
    pages.clear();
}

// Renders the Breakdown report into one or more DIB pages.
//
// If `onPageReady` is empty (the Print Preview call site), every rendered
// page is accumulated into the returned RenderReportResult::pages, exactly
// as before this phase.
//
// If `onPageReady` is set (the real-printing call site), each page is
// handed to that callback as soon as it's finished and is NEVER added to
// the returned vector - the caller owns it from that point on (prints it
// and frees it) before the next page is even rendered, so at most one
// full-resolution page bitmap exists in memory at a time. Returning false
// from the callback aborts rendering immediately (used when printing
// itself has failed) - whatever was still in progress is cleaned up and
// RenderReportPages returns ok == false.
RenderReportResult RenderReportPages(int pageWidthPx, int pageHeightPx, int dpiX, int dpiY,
                                      const std::wstring& debtorText, const std::wstring& cashText,
                                      const std::function<bool(RenderedPage&)>& onPageReady) {
    RenderReportResult out;

    std::wstring dimError;
    if (!ValidateReportPageDimensions(pageWidthPx, pageHeightPx, dpiX, dpiY, &dimError)) {
        out.error = dimError;
        return out;
    }

    auto makeFont = [&](int points, bool bold) -> UniqueFont {
        LOGFONTW lf{};
        lf.lfHeight = -MulDiv(points, dpiY, 72);
        lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
        const wchar_t* face = L"Segoe UI";
#ifdef _MSC_VER
        wcsncpy_s(lf.lfFaceName, LF_FACESIZE, face, _TRUNCATE);
#else
        wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
        lf.lfFaceName[LF_FACESIZE - 1] = 0;
#endif
        return UniqueFont(CreateFontIndirectW(&lf));
    };
    // v0.11.1: denser layout (was 18/11/11pt on 22px rows, which needed ~2x the pages of the old Excel pivot print).
    UniqueFont hFontTitle = makeFont(14, true);
    UniqueFont hFontNormal = makeFont(9, false);
    UniqueFont hFontBold = makeFont(9, true);
    if (!hFontTitle || !hFontNormal || !hFontBold) {
        out.error = L"Couldn't create fonts for the report.";
        return out;
    }

    int marginX = dpiX / 2; // 0.5"
    int marginY = MulDiv(dpiY, 2, 5); // 0.4" top/bottom (v0.11.1; was 0.5")
    int colSpeciesX = marginX;
    int colPriceX = marginX + MulDiv(280, dpiX, 96);
    int colKgsX = marginX + MulDiv(400, dpiX, 96);
    int colTotalX = marginX + MulDiv(510, dpiX, 96);
    int rightEdge = pageWidthPx - marginX;
    int lineHeight = MulDiv(15, dpiY, 96); // v0.11.1: was 22px
    if (lineHeight < 1) lineHeight = 1; // guards ensureSpace/newPage against a zero-progress loop at extreme DPI

    UniqueDC curDC;
    RenderedPage curPage; // the page curDC is currently drawing into
    int y = 0;
    int pageNum = 0;
    bool failed = false;

    // Phase 2 / F10 (2026-10-02): the page-count cap enforces the TOTAL
    // preview memory budget, not just a fixed count. Printing (onPageReady
    // set) never accumulates more than one page, so the plain
    // kReportMaxPages count cap applies as-is. Preview (no callback)
    // accumulates every page, so its effective cap is further reduced,
    // when needed, to however many pages of THIS specific size actually
    // fit within kReportMaxPreviewTotalBytes - checking against the real
    // per-page size rather than refusing a legitimate, ordinary-sized
    // report just because kReportMaxPages copies of a much larger
    // hypothetical page would be too much memory.
    int kMaxPages = kReportMaxPages;
    if (!onPageReady) {
        long long perPageBytes = ComputeDibPageBytes(pageWidthPx, pageHeightPx);
        if (perPageBytes < 0) {
            out.error = L"The report page size is too large to preview.";
            return out;
        }
        long long budgetPages = kReportMaxPreviewTotalBytes / perPageBytes;
        if (budgetPages < (long long)kMaxPages)
            kMaxPages = (budgetPages < 1) ? 0 : (int)budgetPages;
        // Known, documented trade-off: for an unusually large report whose
        // page count falls between this memory-derived cap and the flat
        // kReportMaxPages (200), Print Preview can show FEWER pages than
        // PrintBreakdownReport would actually print (printing isn't bound
        // by this check - it never holds more than one page in memory at
        // once). Only possible at report sizes far beyond anything this
        // business has ever produced; pageCapHit still surfaces a warning
        // to the user either way.
    }
    if (kMaxPages < 1) {
        out.error = L"The report page size is too large to preview even one page within the memory budget.";
        return out;
    }

    // Hands the just-completed page to the caller (accumulate, or stream
    // via onPageReady) and releases curDC. Called both between pages and
    // once at the very end.
    auto finishPage = [&]() {
        curDC.reset();
        if (curPage.bitmap) {
            if (onPageReady) {
                if (!onPageReady(curPage)) failed = true; // caller owns/frees curPage either way
            } else {
                out.pages.push_back(curPage);
            }
            curPage = RenderedPage{};
        }
    };

    auto drawHeaderRow = [&]() {
        ScopedSelect selFont(curDC.get(), hFontBold.get());
        TextOutW(curDC.get(), colSpeciesX, y, L"Species", 7);
        TextOutW(curDC.get(), colPriceX, y, L"Price ($/kg)", 12);
        TextOutW(curDC.get(), colKgsX, y, L"Kgs (weight)", 12);
        TextOutW(curDC.get(), colTotalX, y, L"Total ($)", 9);
        y += lineHeight;
        UniquePen pen(CreatePen(PS_SOLID, 1, RGB(0, 0, 0)));
        if (pen) {
            ScopedSelect selPen(curDC.get(), pen.get());
            MoveToEx(curDC.get(), marginX, y, nullptr);
            LineTo(curDC.get(), rightEdge, y);
        }
        y += lineHeight / 2;
    };

    // Returns false (without setting `failed`) when the page-count cap was
    // reached - content beyond it is dropped cleanly (reported via
    // out.pageCapHit) rather than drawn past the bottom of the last page.
    // Returns false WITH `failed` set to true when a GDI resource failed to
    // allocate (reported via out.error) - rendering stops immediately.
    auto newPage = [&]() -> bool {
        if (failed) return false;
        if (pageNum >= kMaxPages) { out.pageCapHit = true; return false; }
        finishPage();
        if (failed) return false;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = pageWidthPx;
        bmi.bmiHeader.biHeight = -pageHeightPx; // negative = top-down DIB, matches normal GDI (0,0)-at-top-left drawing
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;

        RenderedPage page;
        page.width = pageWidthPx;
        page.height = pageHeightPx;
        page.bitmap = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &page.bits, nullptr, 0);
        if (!page.bitmap) {
            out.error = L"Couldn't allocate a page for the report (out of memory, or the page size is unsupported).";
            failed = true;
            return false;
        }

        HDC dc = CreateCompatibleDC(nullptr);
        if (!dc) {
            DeleteObject(page.bitmap);
            out.error = L"Couldn't create a drawing surface for the report.";
            failed = true;
            return false;
        }
        curDC.reset(dc);
        SelectObject(curDC.get(), page.bitmap); // first selection into a fresh memory DC - nothing to restore
        curPage = page;

        RECT full{ 0, 0, pageWidthPx, pageHeightPx };
        FillRect(curDC.get(), &full, (HBRUSH)GetStockObject(WHITE_BRUSH));
        SetBkMode(curDC.get(), TRANSPARENT);

        pageNum++;
        y = marginY;
        {
            ScopedSelect selFont(curDC.get(), hFontTitle.get());
            TextOutW(curDC.get(), marginX, y, L"Fish Balance - Breakdown Report", 32);
        }
        y += MulDiv(24, dpiY, 96);

        SYSTEMTIME st;
        GetLocalTime(&st);
        std::wstringstream ss;
        ss << L"Generated " << std::setfill(L'0')
           << std::setw(4) << st.wYear << L"-" << std::setw(2) << st.wMonth << L"-" << std::setw(2) << st.wDay
           << L" " << std::setw(2) << st.wHour << L":" << std::setw(2) << st.wMinute;
        std::wstring ts = ss.str();
        {
            // v0.9.56: explicitly select the normal report font. Without
            // this the DC's default (tiny fixed-pixel) font was used, which
            // looked fine at the low preview resolution but was unreadably
            // small at printer/PDF resolution (regression from the v0.9.53
            // RAII rewrite, found in Jack's manual print check).
            ScopedSelect selFont(curDC.get(), hFontNormal.get());
            TextOutW(curDC.get(), marginX, y, ts.c_str(), (int)ts.size());
        }
        y += lineHeight + lineHeight / 2;

        if (pageNum == 1) {
            // Book reconciliation summary, so the printout also serves as
            // the balance check document. Phase 2 / F15 (2026-10-02):
            // overflow-safe accumulation - see RecalcTotals()'s identical
            // guard for why.
            double entered = 0;
            bool enteredOverflow = false;
            for (auto& e : g_entries) {
                double term;
                if (!SafeMultiplyFinite(e.kgs, e.price, term) || !SafeAccumulate(entered, term)) {
                    enteredOverflow = true;
                    break;
                }
            }
            // Phase 1 completeness follow-up (2026-09-30, v0.9.50): same
            // buffer-size fix as RecalcTotals() - see its comment.
            // v0.10.5: the Debtor/Cash text is now passed in by the caller
            // (main.cpp reads it from the edit boxes, same fixed-size buffer
            // as before) so this layer never touches a control.
            SumParseResult debtorResult = ParseSumExprStrict(debtorText);
            SumParseResult cashResult = ParseSumExprStrict(cashText);

            std::wstring l1, l2, l3;
            if (enteredOverflow) {
                l1 = L"Book Total (Debtor + Cash): --";
                l2 = L"Entered Total: --";
                l3 = L"Difference: cannot check - the entered total is too large to compute reliably";
            } else if (!debtorResult.ok || !cashResult.ok) {
                // F5 (Phase 1): fail closed here too - a printed/PDF report
                // must never show a Book Total or Difference computed from
                // a Debtor/Cash entry that couldn't actually be parsed.
                l1 = L"Book Total (Debtor + Cash): --";
                l2 = L"Entered Total: " + FormatMoney(entered);
                l3 = (!debtorResult.ok ? debtorResult.tooLarge : cashResult.tooLarge)
                         ? L"Difference: cannot check - Debtor/Cash total is over the maximum allowed"
                         : L"Difference: cannot check - Debtor/Cash has an invalid entry";
            } else {
                double debtor = debtorResult.value;
                double cash = cashResult.value;
                double book = debtor + cash;
                double diff = book - entered;
                bool ok = std::isfinite(diff) && std::abs(diff) < 0.005;
                l1 = L"Book Total (Debtor + Cash): " + FormatMoney(book);
                l2 = L"Entered Total: " + FormatMoney(entered);
                l3 = L"Difference: " + FormatMoney(diff) + (ok ? L"  (OK - balanced)" : L"  (OUT OF BALANCE)");
            }
            {
                ScopedSelect selFontNormal(curDC.get(), hFontNormal.get()); // see the v0.9.56 note on the "Generated" line above
                TextOutW(curDC.get(), marginX, y, l1.c_str(), (int)l1.size());
                y += lineHeight;
                TextOutW(curDC.get(), marginX, y, l2.c_str(), (int)l2.size());
                y += lineHeight;
            }
            {
                ScopedSelect selFont(curDC.get(), hFontBold.get());
                TextOutW(curDC.get(), marginX, y, l3.c_str(), (int)l3.size());
            }
            y += lineHeight + lineHeight / 2;
        }

        drawHeaderRow();
        return true;
    };

    auto ensureSpace = [&](int need) {
        if (failed) return;
        if (y + need > pageHeightPx - marginY) newPage();
    };

    auto drawRow = [&](const std::wstring& species, const std::wstring& priceStr, double kgs, double amt, bool bold) {
        if (failed || out.pageCapHit) return;
        ensureSpace(lineHeight);
        if (failed || out.pageCapHit) return; // page cap: drop remaining content cleanly rather than drawing past the page
        ScopedSelect selFont(curDC.get(), (bold ? hFontBold : hFontNormal).get());
        TextOutW(curDC.get(), colSpeciesX, y, species.c_str(), (int)species.size());
        if (!priceStr.empty()) TextOutW(curDC.get(), colPriceX, y, priceStr.c_str(), (int)priceStr.size());
        std::wstring kgsS = FormatKg(kgs);
        TextOutW(curDC.get(), colKgsX, y, kgsS.c_str(), (int)kgsS.size());
        std::wstring amtS = FormatMoney(amt);
        TextOutW(curDC.get(), colTotalX, y, amtS.c_str(), (int)amtS.size());
        y += lineHeight;
    };

    newPage();

    if (!failed && !out.pageCapHit) {
        auto data = BuildBreakdownData(g_entries);
        double grandKgs = 0, grandAmt = 0;
        for (auto& sg : data) {
            if (failed || out.pageCapHit) break;
            ensureSpace(lineHeight * 2);
            if (failed || out.pageCapHit) break;
            y += lineHeight / 2;
            {
                ScopedSelect selFont(curDC.get(), hFontBold.get());
                TextOutW(curDC.get(), marginX, y, sg.supplier.c_str(), (int)sg.supplier.size());
            }
            y += lineHeight;

            for (auto& pg : sg.products) {
                if (failed || out.pageCapHit) break;
                for (auto& pl : pg.prices) {
                    if (failed || out.pageCapHit) break;
                    drawRow(pg.species, FormatMoney(pl.price), pl.kgs, pl.amt, false);
                }
                if (failed || out.pageCapHit) break;
                drawRow(pg.species + L" Total", L"", pg.totalKgs, pg.totalAmt, true);
            }
            if (failed || out.pageCapHit) break;
            drawRow(sg.supplier + L" Total", L"", sg.totalKgs, sg.totalAmt, true);
            grandKgs += sg.totalKgs;
            grandAmt += sg.totalAmt;
            y += lineHeight / 2;
        }

        if (!failed && !out.pageCapHit) {
            ensureSpace(lineHeight * 2);
            if (!failed && !out.pageCapHit) {
                UniquePen pen2(CreatePen(PS_SOLID, 1, RGB(0, 0, 0)));
                if (pen2) {
                    ScopedSelect selPen(curDC.get(), pen2.get());
                    MoveToEx(curDC.get(), marginX, y, nullptr);
                    LineTo(curDC.get(), rightEdge, y);
                }
                y += lineHeight / 2;
                drawRow(L"GRAND TOTAL", L"", grandKgs, grandAmt, true);
            }
        }
    }

    finishPage(); // hand off/accumulate the last page

    if (failed) {
        if (out.error.empty()) out.error = L"The report couldn't be rendered.";
        if (!onPageReady) FreeRenderedPages(out.pages); // printing: caller already owns/freed streamed pages
        out.pages.clear();
        out.ok = false;
        return out;
    }

    out.ok = true;
    return out;
}
