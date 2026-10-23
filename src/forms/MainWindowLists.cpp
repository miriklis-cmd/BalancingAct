// MainWindowLists.cpp - the main window's list views, combo boxes and balance totals.
// Moved unchanged out of main.cpp in v0.10.8 (split stage 6a). Gotcha (ARCHITECTURE.md):
// ListView_SetItemText is a multi-statement macro - always bind formatted strings to a
// named variable first, never pass a temporary's .c_str().
#include "MainWindowLists.h"

#include "../PlatformWin.h"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

#include "../core/FishBalanceCore.h"
#include "../core/FishBalanceControlLimits.h"
#include "../data/AppData.h"
#include "UiState.h"
#include "MainWindowApi.h"

// ---------------------------------------------------------------------------
// Refresh logic
// ---------------------------------------------------------------------------

void RefreshCombos() {
    wchar_t buf[256];
    GetWindowTextW(hCmbSupplier, buf, 256);
    std::wstring curSup = buf;
    GetWindowTextW(hCmbProduct, buf, 256);
    std::wstring curProd = buf;

    SendMessageW(hCmbSupplier, CB_RESETCONTENT, 0, 0);
    SendMessageW(hCmbProduct, CB_RESETCONTENT, 0, 0);

    std::vector<std::wstring> sups, prods;
    for (auto& e : g_entries) {
        if (std::find(sups.begin(), sups.end(), e.supplier) == sups.end()) sups.push_back(e.supplier);
        if (std::find(prods.begin(), prods.end(), e.product) == prods.end()) prods.push_back(e.product);
    }
    std::sort(sups.begin(), sups.end());
    std::sort(prods.begin(), prods.end());
    for (auto& s : sups) SendMessageW(hCmbSupplier, CB_ADDSTRING, 0, (LPARAM)s.c_str());
    for (auto& p : prods) SendMessageW(hCmbProduct, CB_ADDSTRING, 0, (LPARAM)p.c_str());

    SetWindowTextW(hCmbSupplier, curSup.c_str());
    SetWindowTextW(hCmbProduct, curProd.c_str());
}

// Simple autocomplete for the Supplier / Species combo boxes: as the user
// types, suggest the first existing item that starts with the typed text and
// highlight the suggested tail (so the next keystroke overwrites it, or
// Enter/Tab accepts it). Only triggers when the text has grown - deleting
// characters (Backspace/Delete) never gets fought.
void ComboAutoComplete(HWND combo, int& prevLen) {
    if (g_acBusy) return;

    wchar_t buf[256];
    GetWindowTextW(combo, buf, 256);
    std::wstring text(buf);
    int curLen = (int)text.size();
    bool grew = curLen > prevLen;
    prevLen = curLen;

    if (!grew || text.empty()) return;

    // Only autocomplete when the caret is at the end of the text - avoids
    // odd results when editing in the middle of an existing entry.
    DWORD selStart = 0, selEnd = 0;
    SendMessageW(combo, CB_GETEDITSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    if ((int)selEnd != curLen) return;

    LRESULT idx = SendMessageW(combo, CB_FINDSTRING, (WPARAM)-1, (LPARAM)text.c_str());
    if (idx == CB_ERR) return;

    // CB_GETLBTEXT has no length-limiting parameter - it writes exactly as
    // much as the item's text needs, so the buffer must be sized to fit via
    // CB_GETLBTEXTLEN first rather than assumed to fit some fixed size.
    LRESULT fullLen = SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)idx, 0);
    if (fullLen == CB_ERR || fullLen <= 0) return;
    std::vector<wchar_t> fullBuf((size_t)fullLen + 1);
    if (SendMessageW(combo, CB_GETLBTEXT, (WPARAM)idx, (LPARAM)fullBuf.data()) == CB_ERR) return;
    std::wstring fullText(fullBuf.data());
    if (fullText.size() <= text.size()) return;

    g_acBusy = true;
    SetWindowTextW(combo, fullText.c_str());
    SendMessageW(combo, CB_SETEDITSEL, 0, MAKELPARAM((int)text.size(), (int)fullText.size()));
    g_acBusy = false;
    // Note: prevLen is deliberately left as the length of what the user
    // actually typed (set earlier), not the length of the suggested text -
    // otherwise the next keystroke looks like a deletion and autocomplete
    // stops re-triggering (e.g. typing "J" then "e" to disambiguate between
    // "Jcasement" and "Jenkins").
}

void RefreshEntriesList() {
    ListView_DeleteAllItems(hListEntries);
    g_filteredIndices.clear();

    wchar_t fbuf[256];
    GetWindowTextW(hEditFilter, fbuf, 256);
    std::wstring filter = TrimW(fbuf);
    std::transform(filter.begin(), filter.end(), filter.begin(), ::towlower);

    int row = 0;
    for (size_t i = 0; i < g_entries.size(); i++) {
        auto& e = g_entries[i];
        if (!filter.empty()) {
            std::wstring sup = e.supplier, prod = e.product;
            std::transform(sup.begin(), sup.end(), sup.begin(), ::towlower);
            std::transform(prod.begin(), prod.end(), prod.begin(), ::towlower);
            if (sup.find(filter) == std::wstring::npos && prod.find(filter) == std::wstring::npos)
                continue;
        }
        g_filteredIndices.push_back((int)i);

        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(e.supplier.c_str());
        ListView_InsertItem(hListEntries, &item);
        ListView_SetItemText(hListEntries, row, 1, const_cast<LPWSTR>(e.product.c_str()));
        std::wstring kgsS = FormatKg(e.kgs);
        ListView_SetItemText(hListEntries, row, 2, const_cast<LPWSTR>(kgsS.c_str()));
        std::wstring priceS = FormatMoney(e.price);
        if (e.priceFlagged) priceS = L"\u26A0 " + priceS; // ROADMAP.md item 7 - see the NM_CUSTOMDRAW row tint below too
        ListView_SetItemText(hListEntries, row, 3, const_cast<LPWSTR>(priceS.c_str()));
        std::wstring totalS = FormatMoney(e.Total());
        ListView_SetItemText(hListEntries, row, 4, const_cast<LPWSTR>(totalS.c_str()));
        ListView_SetItemText(hListEntries, row, 5, const_cast<LPWSTR>(e.date.c_str()));
        ListView_SetItemText(hListEntries, row, 6, const_cast<LPWSTR>(e.notes.c_str()));
        ListView_SetItemText(hListEntries, row, 7, const_cast<LPWSTR>(e.book.c_str())); // v0.12.0 Book column
        row++;
    }
    UpdateSelectionStatus(); // the rebuilt list has nothing selected
}

// v0.12.0: the status bar's third part shows what the selected rows add up to - the same check as
// selecting the rows in Excel and reading the sum. Blank when nothing is selected.
void UpdateSelectionStatus() {
    if (!hStatusBar || !hListEntries) return;
    std::vector<int> picked;
    int r = -1;
    while ((r = ListView_GetNextItem(hListEntries, r, LVNI_SELECTED)) >= 0) {
        if (r < (int)g_filteredIndices.size()) picked.push_back(g_filteredIndices[r]);
    }
    std::wstring text;
    if (!picked.empty()) {
        SelectionTotals t = ComputeSelectionTotals(g_entries, picked);
        text = L"Selected: " + std::to_wstring(t.count) + (t.count == 1 ? L" row | " : L" rows | ") +
               FormatKg(t.kgs) + L" kg | " + FormatMoney(t.amount);
    }
    SendMessageW(hStatusBar, SB_SETTEXT, 2, (LPARAM)text.c_str());
}

// Sorts the underlying data by the clicked column (toggling direction on a
// repeat click of the same column), then redraws. Any in-progress edit is
// abandoned since row indices are about to change.
void SortEntriesBy(int col) {
    if (g_sortColumn == col) g_sortAscending = !g_sortAscending;
    else { g_sortColumn = col; g_sortAscending = true; }

    std::sort(g_entries.begin(), g_entries.end(), [col](const Entry& a, const Entry& b) {
        switch (col) {
            case 0: return _wcsicmp(a.supplier.c_str(), b.supplier.c_str()) < 0;
            case 1: return _wcsicmp(a.product.c_str(), b.product.c_str()) < 0;
            case 2: return a.kgs < b.kgs;
            case 3: return a.price < b.price;
            case 4: return a.Total() < b.Total();
            case 5: return a.date < b.date; // ISO format sorts correctly as plain text
            case 6: return _wcsicmp(a.notes.c_str(), b.notes.c_str()) < 0;
            case 7: return _wcsicmp(a.book.c_str(), b.book.c_str()) < 0;
            default: return false;
        }
    });
    if (!g_sortAscending) std::reverse(g_entries.begin(), g_entries.end());

    if (g_editIndex != -1) CancelEdit();
    RefreshEntriesList();
}

void RecalcTotals() {
    RefreshBooksList(); // v0.12.0: depends on the Debtor/Cash boxes as well as the entries
    // Phase 2 / F15 (2026-10-02): accumulation-overflow guard. Kgs/Price
    // are now bounded at every entry point (ValidateWeightKg/
    // ValidatePriceDollars), so this is defense-in-depth rather than a
    // realistically-reachable case - but the balance check this feeds is
    // the one figure in the app a real money decision gets made from, so
    // it fails closed (same as an invalid Debtor/Cash expression below)
    // instead of ever displaying an inf/-inf/NaN total as if it were real.
    double entered = 0;
    bool enteredOverflow = false;
    for (auto& e : g_entries) {
        double term;
        if (!SafeMultiplyFinite(e.kgs, e.price, term) || !SafeAccumulate(entered, term)) {
            enteredOverflow = true;
            break;
        }
    }

    // Phase 1 completeness follow-up (2026-09-30, v0.9.50): sized to
    // kMaxDebtorCashExprLength+1, matching EM_LIMITTEXT on these controls -
    // the old 512-wchar buffer would have silently truncated a legitimately
    // long (but now UI-permitted, up to 4096 char) expression right before
    // parsing it.
    wchar_t buf[kMaxDebtorCashExprLength + 1];
    GetWindowTextW(hEditDebtor, buf, (int)(kMaxDebtorCashExprLength + 1));
    SumParseResult debtorResult = ParseSumExprStrict(buf);
    GetWindowTextW(hEditCash, buf, (int)(kMaxDebtorCashExprLength + 1));
    SumParseResult cashResult = ParseSumExprStrict(buf);

    g_reconciliationValid = debtorResult.ok && cashResult.ok && !enteredOverflow;

    if (!g_reconciliationValid) {
        // Fail closed (Phase 1 / F5; extended by Phase 2 / F15 for the
        // enteredOverflow case): an invalid Debtor/Cash entry, or an
        // entered total that couldn't be computed, must not be silently
        // treated as balanced (or unbalanced) against anything - there is
        // no trustworthy total to compare, so say so plainly rather than
        // showing a number computed from a partially-ignored expression or
        // an overflowed sum.
        g_diffOk = false;
        g_ovDiffOk = false;
        std::wstring msg;
        std::wstring detail; // v0.9.60: the reason alone, shown on Tab 1 beside a "Cannot check" badge
        std::wstring enteredText;
        if (enteredOverflow) {
            detail = L"the entered total is too large to compute reliably";
            enteredText = L"--";
        } else {
            const std::wstring& badField = !debtorResult.ok ? L"Debtor" : L"Cash";
            const SumParseResult& badResult = !debtorResult.ok ? debtorResult : cashResult;
            if (badResult.tooLarge) {
                // v0.9.57: absurdly large Debtor/Cash total - say so plainly.
                detail = badField + L" total is over the maximum allowed (" +
                         FormatMoney(kMaxDebtorCashTotalDollars) + L")";
            } else {
                detail = badField + L" has an invalid entry (\"" + badResult.errorTerm + L"\")";
            }
            enteredText = FormatMoney(entered);
        }
        msg = L"Difference: cannot check - " + detail; // Total Overview tab keeps the full sentence

        SetWindowTextW(hLblBook, L"Book Total: --");
        SetWindowTextW(hLblEntered, (L"Entered Total: " + enteredText).c_str());
        SetWindowTextW(hLblStatus, L"\u2717  Cannot check");
        SetWindowTextW(hLblDiff, detail.c_str());
        InvalidateRect(hLblStatus, nullptr, TRUE);

        SetWindowTextW(hLblOvBook, L"Book Total (Debtor + Cash): --");
        SetWindowTextW(hLblOvGrand, (L"Grand Total (all suppliers): " + enteredText).c_str());
        SetWindowTextW(hLblOvDiff, msg.c_str());

        InvalidateRect(hLblDiff, nullptr, TRUE);
        InvalidateRect(hLblOvDiff, nullptr, TRUE);
        return;
    }

    double debtor = debtorResult.value;
    double cash = cashResult.value;
    double book = debtor + cash;
    double diff = book - entered;
    g_diffOk = std::isfinite(diff) && std::abs(diff) < 0.005;
    g_ovDiffOk = g_diffOk;

    SetWindowTextW(hLblBook, (L"Book Total: " + FormatMoney(book)).c_str());
    SetWindowTextW(hLblEntered, (L"Entered Total: " + FormatMoney(entered)).c_str());
    SetWindowTextW(hLblStatus, g_diffOk ? L"\u2713  Balanced" : L"\u2717  Out of balance");
    SetWindowTextW(hLblDiff, (L"Difference: " + FormatMoney(diff)).c_str());
    InvalidateRect(hLblStatus, nullptr, TRUE);

    SetWindowTextW(hLblOvBook, (L"Book Total (Debtor + Cash): " + FormatMoney(book)).c_str());
    SetWindowTextW(hLblOvGrand, (L"Grand Total (all suppliers): " + FormatMoney(entered)).c_str());
    SetWindowTextW(hLblOvDiff, (L"Difference: " + FormatMoney(diff) +
                                 (g_ovDiffOk ? L"   (OK - balanced)" : L"   (OUT OF BALANCE)")).c_str());

    InvalidateRect(hLblDiff, nullptr, TRUE);
    InvalidateRect(hLblOvDiff, nullptr, TRUE);
}

// Shared by the Total Overview tab (grouped by supplier) and the By Species
// tab (grouped by species) - same shape of report, just a different key.
// The actual aggregation is ComputeGroupedTotals() in FishBalanceCore.h
// (unit-tested there); this function is now just the ListView rendering.
void PopulateGroupedTotalsList(HWND lv, bool bySupplier) {
    ListView_DeleteAllItems(lv);
    GroupedTotalsResult totals = ComputeGroupedTotals(g_entries, bySupplier);

    int idx = 0;
    for (auto& row : totals.rows) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = idx;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(row.key.c_str());
        ListView_InsertItem(lv, &item);
        std::wstring kgsS = FormatKg(row.kgs);
        ListView_SetItemText(lv, idx, 1, const_cast<LPWSTR>(kgsS.c_str()));
        std::wstring amtS = FormatMoney(row.amt);
        ListView_SetItemText(lv, idx, 2, const_cast<LPWSTR>(amtS.c_str()));
        idx++;
    }
    LVITEMW gitem{};
    gitem.mask = LVIF_TEXT;
    gitem.iItem = idx;
    gitem.iSubItem = 0;
    std::wstring lbl = L"GRAND TOTAL";
    gitem.pszText = const_cast<LPWSTR>(lbl.c_str());
    ListView_InsertItem(lv, &gitem);
    std::wstring gk = FormatKg(totals.grandKgs);
    ListView_SetItemText(lv, idx, 1, const_cast<LPWSTR>(gk.c_str()));
    std::wstring ga = FormatMoney(totals.grandAmt);
    ListView_SetItemText(lv, idx, 2, const_cast<LPWSTR>(ga.c_str()));
}

void RefreshOverviewList() { PopulateGroupedTotalsList(hListOverview, true); }

// v0.12.0: the Books table on Total Overview - one row per book with what was entered for it and, when
// the Debtor/Cash boxes hold a matching amount, the figure from the paper book and the difference.
// The n-th Debtor book is compared with the n-th amount in the Debtor box (and the same for Cash).
// Subtotals for all Debtor books and all Cash books follow, then anything not in a book yet.
void RefreshBooksList() {
    if (!hListBooks) return;
    ListView_DeleteAllItems(hListBooks);

    wchar_t buf[kMaxDebtorCashExprLength + 1];
    GetWindowTextW(hEditDebtor, buf, (int)(kMaxDebtorCashExprLength + 1));
    std::wstring debtorText = buf;
    GetWindowTextW(hEditCash, buf, (int)(kMaxDebtorCashExprLength + 1));
    std::wstring cashText = buf;

    BooksResult books = ComputeBookSummaries(g_entries);
    std::vector<BookCheck> checks = ComputeBookChecks(books, debtorText, cashText);

    int idx = 0;
    auto addRow = [&](const std::wstring& name, int rows, double kgs, double amount, bool haveFigure, double figure, double diff) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = idx;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(name.c_str());
        ListView_InsertItem(hListBooks, &item);
        std::wstring rowsS = std::to_wstring(rows);
        ListView_SetItemText(hListBooks, idx, 1, const_cast<LPWSTR>(rowsS.c_str()));
        std::wstring kgsS = FormatKg(kgs);
        ListView_SetItemText(hListBooks, idx, 2, const_cast<LPWSTR>(kgsS.c_str()));
        std::wstring amtS = FormatMoney(amount);
        ListView_SetItemText(hListBooks, idx, 3, const_cast<LPWSTR>(amtS.c_str()));
        std::wstring figS = haveFigure ? FormatMoney(figure) : std::wstring(L"-");
        ListView_SetItemText(hListBooks, idx, 4, const_cast<LPWSTR>(figS.c_str()));
        std::wstring diffS = haveFigure ? FormatMoney(diff) : std::wstring(L"-");
        ListView_SetItemText(hListBooks, idx, 5, const_cast<LPWSTR>(diffS.c_str()));
        idx++;
    };

    double dRows = 0, dKgs = 0, dAmt = 0, cRows = 0, cKgs = 0, cAmt = 0;
    double dFig = 0, cFig = 0;
    bool dAllFig = true, cAllFig = true, anyD = false, anyC = false;
    for (const auto& c : checks) {
        addRow(c.book.name + (c.book.type == L'C' ? L" (Cash)" : (c.book.type == L'D' ? L" (Debtor)" : L"")),
               c.book.count, c.book.kgs, c.book.amount, c.haveBookFigure, c.bookFigure, c.difference);
        if (c.book.type == L'C') {
            anyC = true; cRows += c.book.count; cKgs += c.book.kgs; cAmt += c.book.amount;
            if (c.haveBookFigure) cFig += c.bookFigure; else cAllFig = false;
        } else {
            anyD = true; dRows += c.book.count; dKgs += c.book.kgs; dAmt += c.book.amount;
            if (c.haveBookFigure) dFig += c.bookFigure; else dAllFig = false;
        }
    }
    if (anyD) addRow(L"Total - Debtor books", (int)dRows, dKgs, dAmt, dAllFig, dFig, dAmt - dFig);
    if (anyC) addRow(L"Total - Cash books", (int)cRows, cKgs, cAmt, cAllFig, cFig, cAmt - cFig);
    if (books.unassignedCount > 0)
        addRow(L"Not in a book", books.unassignedCount, books.unassignedKgs, books.unassignedAmount, false, 0, 0);
}

// By Species gets its own richer view (unlike Overview, which stays a plain
// Kgs/Total breakdown per supplier): average, highest, and lowest price seen
// for each species, alongside the usual Kgs/Total. The actual aggregation
// is ComputeSpeciesStats() in FishBalanceCore.h (unit-tested there); this
// function is now just the ListView rendering.
void RefreshBySpeciesList() {
    ListView_DeleteAllItems(hListBySpecies);
    SpeciesStatsResult stats = ComputeSpeciesStats(g_entries);

    int idx = 0;
    for (auto& row : stats.rows) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = idx;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(row.species.c_str());
        ListView_InsertItem(hListBySpecies, &item);
        std::wstring kgsS = FormatKg(row.kgs);
        ListView_SetItemText(hListBySpecies, idx, 1, const_cast<LPWSTR>(kgsS.c_str()));
        std::wstring amtS = FormatMoney(row.amt);
        ListView_SetItemText(hListBySpecies, idx, 2, const_cast<LPWSTR>(amtS.c_str()));
        std::wstring avgS = FormatMoney(row.avgPrice);
        ListView_SetItemText(hListBySpecies, idx, 3, const_cast<LPWSTR>(avgS.c_str()));
        std::wstring maxS = FormatMoney(row.maxPrice);
        ListView_SetItemText(hListBySpecies, idx, 4, const_cast<LPWSTR>(maxS.c_str()));
        std::wstring minS = FormatMoney(row.minPrice);
        ListView_SetItemText(hListBySpecies, idx, 5, const_cast<LPWSTR>(minS.c_str()));
        idx++;
    }

    // Price stats don't have a meaningful single "grand" figure across
    // different species (their prices aren't comparable), so the total row
    // only carries Kgs and Total, same as before.
    LVITEMW gitem{};
    gitem.mask = LVIF_TEXT;
    gitem.iItem = idx;
    gitem.iSubItem = 0;
    std::wstring lbl = L"GRAND TOTAL";
    gitem.pszText = const_cast<LPWSTR>(lbl.c_str());
    ListView_InsertItem(hListBySpecies, &gitem);
    std::wstring gk = FormatKg(stats.grandKgs);
    ListView_SetItemText(hListBySpecies, idx, 1, const_cast<LPWSTR>(gk.c_str()));
    std::wstring ga = FormatMoney(stats.grandAmt);
    ListView_SetItemText(hListBySpecies, idx, 2, const_cast<LPWSTR>(ga.c_str()));
}

// PriceLine, ProductGroup, SupplierGroup, and BuildBreakdownData() now all
// live in FishBalanceCore.h (unchanged logic; BuildBreakdownData() there
// takes the entries vector as an explicit parameter instead of reading
// the global g_entries directly, so it can be unit-tested against known
// fixtures - call sites below now pass g_entries in explicitly).

void InsertBreakdownRow(int groupId, int itemIdx, const std::wstring& species,
                         const std::wstring& priceStr, double kgs, double amt) {
    LVITEMW item{};
    item.mask = LVIF_TEXT | LVIF_GROUPID;
    item.iItem = itemIdx;
    item.iSubItem = 0;
    item.iGroupId = groupId;
    item.pszText = const_cast<LPWSTR>(species.c_str());
    ListView_InsertItem(hListBreakdown, &item);
    ListView_SetItemText(hListBreakdown, itemIdx, 1, const_cast<LPWSTR>(priceStr.c_str()));
    std::wstring kgsS = FormatKg(kgs);
    ListView_SetItemText(hListBreakdown, itemIdx, 2, const_cast<LPWSTR>(kgsS.c_str()));
    std::wstring amtS = FormatMoney(amt);
    ListView_SetItemText(hListBreakdown, itemIdx, 3, const_cast<LPWSTR>(amtS.c_str()));
}

void RefreshBreakdownList() {
    ListView_DeleteAllItems(hListBreakdown);
    SendMessageW(hListBreakdown, LVM_REMOVEALLGROUPS, 0, 0);

    auto data = BuildBreakdownData(g_entries);

    int groupId = 0;
    int itemIdx = 0;
    for (auto& sg : data) {
        LVGROUP grp{};
        grp.cbSize = sizeof(LVGROUP);
        grp.mask = LVGF_HEADER | LVGF_GROUPID | LVGF_STATE;
        grp.pszHeader = const_cast<LPWSTR>(sg.supplier.c_str());
        grp.iGroupId = groupId;
        grp.state = LVGS_NORMAL;
        SendMessageW(hListBreakdown, LVM_INSERTGROUP, (WPARAM)-1, (LPARAM)&grp);

        for (auto& pg : sg.products) {
            for (auto& pl : pg.prices)
                InsertBreakdownRow(groupId, itemIdx++, pg.species, FormatMoney(pl.price), pl.kgs, pl.amt);
            InsertBreakdownRow(groupId, itemIdx++, pg.species + L" Total", L"", pg.totalKgs, pg.totalAmt);
        }
        InsertBreakdownRow(groupId, itemIdx++, sg.supplier + L" Total", L"", sg.totalKgs, sg.totalAmt);
        groupId++;
    }
}
