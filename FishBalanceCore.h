// FishBalanceCore.h
//
// Platform-independent logic for Fish Balance Manager: the data model,
// parsing, formatting, report aggregation, and the CSV/email string-building
// used by the app. Deliberately has ZERO dependency on windows.h or any
// Win32 type (SYSTEMTIME, HWND, etc.) so it can be compiled and unit-tested
// on any platform, completely independent of the GUI.
//
// main.cpp #includes this and calls into it instead of keeping its own
// copies. Anywhere the app genuinely needs a Win32 type at the boundary
// (e.g. the DateTimePicker control talks in SYSTEMTIME, not this header's
// SimpleDate), main.cpp keeps a small, thin wrapper function that converts
// and delegates to the function here - the wrapper is Win32-only glue, the
// actual logic lives in exactly one place, here.
//
// See tests/ for the automated test suite that exercises everything in this
// file directly, without ever creating a Win32 window.
//
// IMPORTANT: when extracting code into this header, the goal was to move
// existing logic UNCHANGED, not to fix additional bugs while extracting.
// A couple of already-known, already-documented behaviors are preserved
// deliberately (not silently fixed here) - see the comments on
// ParseISODateCore and ParseSumExpr below.

#pragma once

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <functional>

// ---------------------------------------------------------------------------
// Data model
// ---------------------------------------------------------------------------

struct Entry {
    std::wstring supplier;
    std::wstring product; // "species"
    double kgs = 0.0;
    double price = 0.0;
    std::wstring date;  // ISO format YYYY-MM-DD; empty means not set (e.g. loaded from an older file)
    std::wstring notes;
    // Set when this entry's price was flagged as a same-day outlier at
    // commit time (ROADMAP.md item 7) and the user chose to save it
    // anyway. Cleared automatically the next time this entry is
    // committed (edited) and its price no longer looks unusual against
    // that day's other entries - see CommitEntryForm in main.cpp.
    bool priceFlagged = false;
    double Total() const { return kgs * price; }
};

// A plain, platform-independent stand-in for SYSTEMTIME's year/month/day
// fields (the only ones this app's date handling ever uses). main.cpp
// converts to/from the real SYSTEMTIME at the one place it talks to the
// DateTimePicker control; everywhere else, including here, only this
// portable type is used.
struct SimpleDate {
    int year = 0, month = 0, day = 0;
};

// Shared grouping structures used both by the on-screen Breakdown list and by
// the printed/PDF report, so the two can never drift out of sync.
struct PriceLine { double price; double kgs; double amt; };
struct ProductGroup {
    std::wstring species;
    std::vector<PriceLine> prices;
    double totalKgs = 0, totalAmt = 0;
};
struct SupplierGroup {
    std::wstring supplier;
    std::vector<ProductGroup> products;
    double totalKgs = 0, totalAmt = 0;
};

// ---------------------------------------------------------------------------
// Small string/number utilities
// ---------------------------------------------------------------------------

inline std::wstring TrimW(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Dates are stored as ISO "YYYY-MM-DD" throughout (sorts correctly as plain
// text, unambiguous regardless of locale).
inline std::wstring FormatDateISO(const SimpleDate& d) {
    std::wstringstream ss;
    ss << std::setfill(L'0') << std::setw(4) << d.year << L"-"
       << std::setw(2) << d.month << L"-" << std::setw(2) << d.day;
    return ss.str();
}

// NOTE: this does NOT validate that the day is actually valid for the given
// month/year (e.g. "2026-02-31" currently passes). That's a known, already
// documented gap (see SecurityHardeningRegister.md / ROADMAP.md) - it's
// preserved here unchanged rather than silently fixed as part of this
// extraction, so this refactor doesn't also change behavior.
inline bool ParseISODate(const std::wstring& s, SimpleDate& out) {
    if (s.size() != 10 || s[4] != L'-' || s[7] != L'-') return false;
    SimpleDate d;
    // wcstol (standard C, <cwchar>) used instead of the Microsoft-specific
    // _wtoi so this file has no platform-specific CRT dependency - same
    // "parse leading digits" behavior either way for these fixed-width,
    // already-validated substrings.
    d.year = (int)wcstol(s.substr(0, 4).c_str(), nullptr, 10);
    d.month = (int)wcstol(s.substr(5, 2).c_str(), nullptr, 10);
    d.day = (int)wcstol(s.substr(8, 2).c_str(), nullptr, 10);
    if (d.year < 1900 || d.month < 1 || d.month > 12 || d.day < 1 || d.day > 31) return false;
    out = d;
    return true;
}

inline std::wstring ToFixed(double v, int decimals) {
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(decimals) << v;
    return ss.str();
}

inline std::wstring FormatMoney(double v) {
    bool neg = v < 0;
    if (neg) v = -v;
    std::wstring s = ToFixed(v, 2);
    size_t dot = s.find(L'.');
    std::wstring intPart = s.substr(0, dot);
    std::wstring frac = s.substr(dot);
    std::wstring withCommas;
    int cnt = 0;
    for (int i = (int)intPart.size() - 1; i >= 0; i--) {
        withCommas.push_back(intPart[i]);
        cnt++;
        if (cnt % 3 == 0 && i != 0) withCommas.push_back(L',');
    }
    std::reverse(withCommas.begin(), withCommas.end());
    return (neg ? std::wstring(L"-$") : std::wstring(L"$")) + withCommas + frac;
}

inline std::wstring FormatNum(double v) {
    return ToFixed(v, 2);
}

// Kg is always displayed to 1 decimal place (business convention) - kept
// separate from FormatNum since that's also used for plain-number dollar
// amounts in the CSV export, which still need 2 decimal places.
inline std::wstring FormatKg(double v) {
    return ToFixed(v, 1);
}

inline bool ParseDoubleW(const std::wstring& sIn, double& out) {
    std::wstring s = TrimW(sIn);
    if (s.empty()) return false;
    try {
        size_t pos = 0;
        double v = std::stod(s, &pos);
        if (pos != s.size()) return false; // trailing junk, e.g. "12abc"
        if (!std::isfinite(v)) return false; // std::stod happily parses "nan"/"inf"/"-inf" as a
                                              // fully-consumed, "successful" value - reject those
                                              // explicitly since they'd otherwise poison totals,
                                              // sorting, and the price-grouping maps downstream.
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

// Parses simple sums/differences like "250.7+1826+2552+286" or
// "123+11-21" (mirrors how the original spreadsheet's Debtor/Cash cells
// were built up from several manual figures, including corrections/
// deductions). Both '+' and '-' are recognized as operators between
// terms - a '-' negates whichever term follows it, so "123+11-21"
// correctly computes 113, not 134.
//
// Result type for the strict parser below: reconciliation must fail
// closed, so a malformed expression can no longer silently resolve to
// "whatever std::stod managed to salvage" - the caller gets an explicit
// ok=false and must treat the figure as unusable rather than displaying
// a number that looks plausible but is quietly wrong.
struct SumParseResult {
    bool ok = false;
    double value = 0.0;
    std::wstring errorTerm; // the offending term/fragment, for user-facing messages
};

// Phase 1 / F5 audit remediation (2026-09-30): replaces the old lenient
// ParseSumExpr(), which silently dropped any term that failed to parse
// (e.g. "oops" in "1000+oops+250" contributed nothing and the mismatch
// was invisible) and, because it called std::stod directly instead of
// the stricter ParseDoubleW, let a term like "12x" contribute 12 with
// the trailing "x" silently ignored. Both behaviours meant a typo in the
// Debtor/Cash box could quietly produce a balanced-looking total instead
// of an obvious error - exactly backwards for a reconciliation check.
//
// Grammar:
//   - empty/whitespace-only input is VALID and evaluates to 0 (preserves
//     the common case of an untouched Debtor/Cash field).
//   - a leading '+' or '-' is allowed only on the very first term of the
//     whole expression (e.g. "-50+100" is valid, matching the old
//     behaviour for that case).
//   - a '+'/'-' appearing anywhere else with no term accumulated since
//     the previous operator is rejected outright - one rule that cleanly
//     covers "100++20", "100+-20", and "100--20": each has an operator
//     immediately following another operator (or the leading sign) with
//     nothing in between.
//   - a trailing operator with nothing after it ("100+") is rejected as
//     a missing term.
//   - each term must consist of digits with at most one decimal point.
//     This rejects "12x" (trailing junk), "nan"/"inf"/"-inf" (std::stod
//     would otherwise happily parse these as "successful" - see
//     ParseDoubleW's comment above), and scientific notation like
//     "1e10" (deliberately not supported here - unrealistic for
//     manually-summed cash figures, and one less thing to explain to
//     users); it also means a term is rejected as "invalid" before
//     ParseDoubleW is even consulted for these cases.
// Unlike the old ParseSumExpr, a single bad term invalidates the WHOLE
// expression rather than being silently dropped or partially applied.
inline SumParseResult ParseSumExprStrict(const std::wstring& sIn) {
    SumParseResult result;
    std::wstring s = TrimW(sIn);
    if (s.empty()) {
        result.ok = true;
        result.value = 0.0;
        return result;
    }

    auto isValidTermChars = [](const std::wstring& t) {
        bool sawDigit = false;
        bool sawDot = false;
        for (wchar_t ch : t) {
            if (ch >= L'0' && ch <= L'9') {
                sawDigit = true;
            } else if (ch == L'.') {
                if (sawDot) return false; // a second decimal point
                sawDot = true;
            } else {
                return false; // letters, a second sign, etc.
            }
        }
        return sawDigit; // "." alone (or empty) has no digits - invalid
    };

    auto fail = [&](const std::wstring& badFragment) {
        result.ok = false;
        result.value = 0.0;
        result.errorTerm = badFragment;
        return result;
    };

    double sum = 0.0;
    double curSign = 1.0;
    std::wstring curTerm;
    const size_t n = s.size();

    for (size_t i = 0; i < n; i++) {
        wchar_t c = s[i];
        if (c == L'+' || c == L'-') {
            if (i == 0) {
                // leading sign on the very first term - not a separator
                curSign = (c == L'-') ? -1.0 : 1.0;
                continue;
            }
            std::wstring t = TrimW(curTerm);
            if (t.empty()) {
                // an operator right after another operator (or the leading
                // sign) with nothing accumulated in between
                return fail(s.substr(0, i + 1));
            }
            if (!isValidTermChars(t)) {
                return fail(t);
            }
            double v = 0.0;
            if (!ParseDoubleW(t, v)) {
                return fail(t);
            }
            sum += curSign * v;
            curTerm.clear();
            curSign = (c == L'-') ? -1.0 : 1.0;
        } else {
            curTerm.push_back(c);
        }
    }

    std::wstring lastTerm = TrimW(curTerm);
    if (lastTerm.empty()) {
        return fail(s); // dangling trailing operator, e.g. "100+"
    }
    if (!isValidTermChars(lastTerm)) {
        return fail(lastTerm);
    }
    double v = 0.0;
    if (!ParseDoubleW(lastTerm, v)) {
        return fail(lastTerm);
    }
    sum += curSign * v;

    result.ok = true;
    result.value = sum;
    return result;
}

// ---------------------------------------------------------------------------
// Report aggregation
// ---------------------------------------------------------------------------

// Supplier -> Species -> Price grouping, used by the on-screen Breakdown
// tab, the printed/PDF report, and the CSV export's Breakdown section.
inline std::vector<SupplierGroup> BuildBreakdownData(const std::vector<Entry>& entries) {
    std::vector<SupplierGroup> result;

    std::vector<std::wstring> suppliers;
    for (auto& e : entries)
        if (std::find(suppliers.begin(), suppliers.end(), e.supplier) == suppliers.end())
            suppliers.push_back(e.supplier);
    std::sort(suppliers.begin(), suppliers.end());

    for (auto& sup : suppliers) {
        SupplierGroup sg;
        sg.supplier = sup;

        std::vector<std::wstring> products;
        for (auto& e : entries)
            if (e.supplier == sup && std::find(products.begin(), products.end(), e.product) == products.end())
                products.push_back(e.product);
        std::sort(products.begin(), products.end());

        for (auto& prod : products) {
            ProductGroup pg;
            pg.species = prod;

            std::vector<double> prices;
            std::map<double, double> kgsByPrice, amtByPrice;
            for (auto& e : entries) {
                if (e.supplier == sup && e.product == prod) {
                    if (kgsByPrice.find(e.price) == kgsByPrice.end()) {
                        kgsByPrice[e.price] = 0;
                        amtByPrice[e.price] = 0;
                        prices.push_back(e.price);
                    }
                    kgsByPrice[e.price] += e.kgs;
                    amtByPrice[e.price] += e.Total();
                }
            }
            std::sort(prices.begin(), prices.end());
            for (double p : prices) {
                pg.prices.push_back({ p, kgsByPrice[p], amtByPrice[p] });
                pg.totalKgs += kgsByPrice[p];
                pg.totalAmt += amtByPrice[p];
            }
            sg.products.push_back(pg);
            sg.totalKgs += pg.totalKgs;
            sg.totalAmt += pg.totalAmt;
        }
        result.push_back(sg);
    }
    return result;
}

// Grouped Kgs/$ totals by an arbitrary key (Supplier for the Total Overview
// tab, Species for part of the By Species tab) - the pure computation
// behind PopulateGroupedTotalsList(), with all HWND/ListView rendering
// stripped out so it can be tested directly. This is exactly the
// computation behind the v0.9.1 "Total Overview showed Kgs under the
// Total ($) column" bug - the bug was in the caller's column definitions,
// not here, but this is the function a regression test needs to call.
struct GroupedTotal {
    std::wstring key;
    double kgs = 0;
    double amt = 0;
};
struct GroupedTotalsResult {
    std::vector<GroupedTotal> rows; // sorted by key
    double grandKgs = 0, grandAmt = 0;
};

inline GroupedTotalsResult ComputeGroupedTotals(const std::vector<Entry>& entries, bool bySupplier) {
    GroupedTotalsResult result;
    std::vector<std::wstring> keys;
    std::map<std::wstring, double> kgsSum, amtSum;
    for (auto& e : entries) {
        const std::wstring& k = bySupplier ? e.supplier : e.product;
        if (kgsSum.find(k) == kgsSum.end()) {
            kgsSum[k] = 0;
            amtSum[k] = 0;
            keys.push_back(k);
        }
        kgsSum[k] += e.kgs;
        amtSum[k] += e.Total();
    }
    std::sort(keys.begin(), keys.end());

    for (auto& k : keys) {
        GroupedTotal row;
        row.key = k;
        row.kgs = kgsSum[k];
        row.amt = amtSum[k];
        result.rows.push_back(row);
        result.grandKgs += row.kgs;
        result.grandAmt += row.amt;
    }
    return result;
}

// Per-species Kgs/$ totals plus average/highest/lowest price seen - the
// pure computation behind RefreshBySpeciesList(), with rendering stripped
// out.
struct SpeciesStat {
    std::wstring species;
    double kgs = 0, amt = 0;
    double avgPrice = 0, maxPrice = 0, minPrice = 0;
};
struct SpeciesStatsResult {
    std::vector<SpeciesStat> rows; // sorted by species
    double grandKgs = 0, grandAmt = 0;
};

inline SpeciesStatsResult ComputeSpeciesStats(const std::vector<Entry>& entries) {
    SpeciesStatsResult result;
    std::vector<std::wstring> keys;
    std::map<std::wstring, double> kgsSum, amtSum, priceSum, priceMax, priceMin;
    std::map<std::wstring, int> priceCount;
    for (auto& e : entries) {
        const std::wstring& k = e.product;
        if (kgsSum.find(k) == kgsSum.end()) {
            kgsSum[k] = 0;
            amtSum[k] = 0;
            priceSum[k] = 0;
            priceCount[k] = 0;
            priceMax[k] = e.price;
            priceMin[k] = e.price;
            keys.push_back(k);
        }
        kgsSum[k] += e.kgs;
        amtSum[k] += e.Total();
        priceSum[k] += e.price;
        priceCount[k]++;
        if (e.price > priceMax[k]) priceMax[k] = e.price;
        if (e.price < priceMin[k]) priceMin[k] = e.price;
    }
    std::sort(keys.begin(), keys.end());

    for (auto& k : keys) {
        SpeciesStat row;
        row.species = k;
        row.kgs = kgsSum[k];
        row.amt = amtSum[k];
        row.avgPrice = priceCount[k] > 0 ? priceSum[k] / priceCount[k] : 0;
        row.maxPrice = priceMax[k];
        row.minPrice = priceMin[k];
        result.rows.push_back(row);
        result.grandKgs += row.kgs;
        result.grandAmt += row.amt;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Outlier price detection (ROADMAP.md item 7)
//
// "Is this price a typo?" - checked against that species' OTHER entries on
// the same date (all suppliers pooled - see ROADMAP.md's discussion of
// why). Deliberately a same-day check only: no cross-day history, no
// dependency on item 5 (Price History) or the flat-file-vs-SQLite
// decision behind it.
// ---------------------------------------------------------------------------

// Every entry for `product` on `date`, excluding `excludeIndex` (pass -1 to
// exclude nothing) - so editing an existing entry compares it against the
// OTHER entries, never against its own old price.
inline std::vector<double> GatherOtherPricesForSpeciesOnDate(
        const std::vector<Entry>& entries, const std::wstring& product,
        const std::wstring& date, int excludeIndex) {
    std::vector<double> prices;
    for (size_t i = 0; i < entries.size(); i++) {
        if ((int)i == excludeIndex) continue;
        if (entries[i].product == product && entries[i].date == date)
            prices.push_back(entries[i].price);
    }
    return prices;
}

struct OutlierRange { double low = 0, high = 0; };

// Tukey's fences: sort the baseline prices, split into halves for Q1/Q3
// (median-of-halves method - for an odd count, the middle element is
// excluded from both halves), then flag anything outside
// [Q1 - 1.5*IQR, Q3 + 1.5*IQR]. Returns false (range left untouched) if
// there are fewer than kMinBaselineForOutlierCheck prices to work with -
// too few points for quartiles to mean anything, so no check is possible.
// static, not inline: this app's CMakeLists.txt build doesn't set
// /std:c++17 (only tests/run_tests.ps1's manual cl invocation does), and
// a plain namespace-scope "inline" variable is a C++17-only feature -
// static const has been valid since long before C++11 and needs nothing
// newer, with the same effect here (this header is only ever consumed
// as a value, never address-taken across translation units).
static const size_t kMinBaselineForOutlierCheck = 4;

// See the floor comment inside ComputeOutlierRange below for why this
// exists. 20% chosen with Jack 2026-09-22, after 5 identical $10 entries
// flagged a genuinely normal $12 sixth entry (IQR=0 without this floor).
static const double kOutlierFloorPercent = 0.20;

inline bool ComputeOutlierRange(std::vector<double> prices, OutlierRange& outRange) {
    if (prices.size() < kMinBaselineForOutlierCheck) return false;
    std::sort(prices.begin(), prices.end());
    size_t n = prices.size();

    auto medianOfRange = [&](size_t lo, size_t hi) {
        size_t count = hi - lo; // half-open [lo, hi)
        if (count % 2 == 1) return prices[lo + count / 2];
        return (prices[lo + count / 2 - 1] + prices[lo + count / 2]) / 2.0;
    };

    size_t half = n / 2;
    double q1 = medianOfRange(0, half);
    double q3 = (n % 2 == 0) ? medianOfRange(half, n) : medianOfRange(half + 1, n);
    double iqr = q3 - q1;

    // Floor: a baseline with little or no price spread (e.g. several
    // identical entries - confirmed with Jack: five $10 entries in a row
    // gives IQR = 0, which without this floor makes the fence collapse to
    // exactly [$10, $10] and flags ANY deviation at all, even a cent) would
    // otherwise make Tukey's fence useless for a market where normal
    // day-to-day price movement exists. Floors the IQR used in the fence
    // at a percentage of the baseline's own median price, so a tight/
    // uniform baseline still allows reasonable movement instead of
    // demanding an exact match. 20% chosen with Jack 2026-09-22.
    double median = medianOfRange(0, n);
    double effectiveIqr = std::max(iqr, kOutlierFloorPercent * median);

    outRange.low = q1 - 1.5 * effectiveIqr;
    outRange.high = q3 + 1.5 * effectiveIqr;
    return true;
}

// Silently re-checks EVERY entry for `product` on `date`, not just one -
// each entry judged leave-one-out against the CURRENT full group (same
// method as a single check, just applied to the whole group at once).
// Call this after anything that changes which prices exist for a
// species/date - a commit (add/edit), a delete, or an undo-delete -
// since any of those can shift the baseline every sibling entry is
// judged against. A flag is therefore a live reflection of the current
// data, not a permanent record of a past decision: an entry manually
// cleared (or dismissed via ReviewOrEditEntry) can be silently
// re-flagged later if a subsequent change to a SIBLING entry makes it
// look unusual again - see the discussion around ROADMAP.md item 7 for
// why this is the intended behavior, not a bug.
inline void ReevaluateOutlierFlagsForSpeciesOnDate(std::vector<Entry>& entries,
        const std::wstring& product, const std::wstring& date) {
    for (size_t i = 0; i < entries.size(); i++) {
        if (entries[i].product != product || entries[i].date != date) continue;
        std::vector<double> baseline = GatherOtherPricesForSpeciesOnDate(entries, product, date, (int)i);
        OutlierRange range;
        entries[i].priceFlagged = ComputeOutlierRange(baseline, range) &&
                                   (entries[i].price < range.low || entries[i].price > range.high);
    }
}


//
// Takes the FULL, ALREADY-DECODED file content as a single wstring (the
// Win32-specific "open the file, read the bytes, convert from UTF-8" step
// stays in main.cpp's LoadFromFile, since that's a genuine platform
// boundary - everything from "split into lines" onward, which is where
// every serious bug in this app has lived, is here and fully testable.
// ---------------------------------------------------------------------------

// Phase 1 completeness follow-up (2026-09-30, v0.9.50): structured parse
// diagnostics, requested so a rejected file can show the user something more
// useful than one generic "could not open" message. Each value names a
// distinct class of structural/content problem ParseFbdContent can reject
// on; FbdLoadResult::errorCode/errorLine/errorMessage below are only
// meaningful when ok is false. This intentionally does NOT distinguish
// every one of the read-side codes (file-not-found, access failure, short
// read, etc.) - those apply to the actual file read in main.cpp's
// ReadAllBytes/LoadFromFile, before content ever reaches this function; see
// ReadBytesError in main.cpp for that half of the diagnostic surface.
enum class FbdErrorCode {
    None = 0,               // ok == true; not a real error
    NotARecognizedFile,     // no DEBTOR=/CASH=/BEGIN/END/DRAFT_*/etc. marker at all
    MissingBegin,           // no BEGIN anywhere in the document
    MissingEnd,             // BEGIN present but no matching END
    DuplicateOrMisorderedMarker, // 2nd BEGIN, 2nd/unmatched END, or END before BEGIN
    DuplicateSingletonMetadata,  // 2nd DEBTOR= or 2nd CASH= line
    UnexpectedContentOutsideDataSection, // stray non-empty line before BEGIN/after END
    LineTooLong,            // a single decoded line exceeds kMaxLineLength
    FieldTooLong,           // a single KEY=/row field exceeds its documented limit
    RecordLimitExceeded,    // more than kMaxEntryRecords data rows
    MalformedEntryRow,      // wrong field count / unparseable shape
    InvalidNumericField,    // Kgs/Price not a finite, non-negative number
    EmptyRequiredField,     // empty Supplier or Species on a data row
};

// Phase 1 completeness follow-up (2026-09-30, v0.9.50): generous, documented
// bounds - far above any realistic business usage - that exist purely to
// stop a pathological or maliciously-crafted file from causing an
// unbounded allocation, a multi-gigabyte in-memory document, or a
// multi-million-row report/print/CSV pass. None of these are expected to
// ever be hit in real use; hitting one always means either a corrupted file
// or something that was never a genuine Fish Balance document. Interactive
// entry (CommitEntryForm et al. in main.cpp) enforces the same limits so a
// value the UI would reject can never be smuggled in via a hand-edited or
// otherwise-crafted file, and vice versa.
static const size_t kMaxEntryRecords = 100000;
static const size_t kMaxLineLength = 65536;
static const size_t kMaxSupplierSpeciesLength = 255;
static const size_t kMaxNotesLength = 4096;
static const size_t kMaxDebtorCashExprLength = 4096;
static const size_t kMaxDraftKgsPriceTextLength = 256;
static const size_t kMaxSourceFileLength = 32767;

struct FbdLoadResult {
    bool ok = false;              // false => reject the whole document; caller must not
                                   // touch any live state (see LoadFromFile in main.cpp)
    FbdErrorCode errorCode = FbdErrorCode::None; // meaningful only when ok == false
    int errorLine = 0;            // 1-based source line number, or 0 when not applicable
    std::wstring errorMessage;    // short, safe, human-readable reason - never echoes a
                                   // full field value verbatim, so a giant or sensitive
                                   // field can't end up rendered straight into a dialog
    std::vector<Entry> entries;
    std::wstring debtor;
    std::wstring cash;
    int skippedLines = 0;
    // In-progress "Add Entry" form draft (typed but not yet committed with
    // the Add Entry button) - persisted so a crash or power loss doesn't
    // lose it, the same way a graceful close never did. Empty strings mean
    // no draft was pending. draftDate is only ever set to a genuinely valid
    // ISO date (or left empty) - see ParseFbdContent.
    std::wstring draftSupplier, draftSpecies, draftKgs, draftPrice, draftNotes, draftDate;
    // Set only for a file that's been through Finalize Day (ROADMAP.md item
    // 3) and written into the history\ folder - a genuinely valid ISO date
    // (same validation as draftDate) recording which business day this
    // snapshot was locked in as, or empty for an ordinary (unlocked)
    // working file. main.cpp uses this to lock the entry form/Debtor/Cash
    // and relabel the Finalize button when such a file is loaded.
    std::wstring finalizedDate;
    // Phase 1 / F1 audit remediation (2026-09-30, narrowed on later review):
    // the full path of whichever named file (g_currentFile) was open in
    // main.cpp at the moment this content was written - empty if no named
    // file was open (an unsaved sheet). In practice only ever written into
    // autosave.fbd (see BuildFbdSaveContent/SaveToFile in main.cpp) -
    // deliberately NOT written into named-file saves, history\ snapshots, or
    // backups\, since those routinely leave the machine (emailed, backed
    // up, opened elsewhere) and nothing ever reads this marker back out of
    // them anyway; embedding the original machine's absolute path/username
    // in every persisted file for no benefit was an oversight in the first
    // version of this fix. Used only at startup: main.cpp checks whether a
    // recovered autosave.fbd's own marker actually corresponds to
    // settings.txt's remembered last-opened file before silently
    // re-associating the two - see hasSourceFile below and the startup
    // logic in wWinMain. A file this field is parsed from that isn't
    // actually autosave.fbd (e.g. a named file, or one hand-edited to add
    // this line) is parsed the same way, but nothing currently reads the
    // result for any file other than the one just-loaded autosave.
    std::wstring sourceFile;
    // False for a file written before this field existed (or any file with
    // no SOURCE_FILE= line at all, e.g. hand-edited) - distinguishes "no
    // named file was open" (sourceFile empty, hasSourceFile true) from
    // "this save predates identity tracking, don't trust any comparison
    // against it" (hasSourceFile false).
    bool hasSourceFile = false;
};

inline FbdLoadResult ParseFbdContent(const std::wstring& all) {
    FbdLoadResult result;

    // reject() sets the structured diagnostic fields and returns the
    // still-not-ok result - every rejection path below goes through this
    // instead of a bare `return result;`, so every way this function can
    // fail carries a specific code, an optional 1-based line number (0 when
    // not applicable, e.g. a document-level check after the loop), and a
    // short human-readable reason. Never echoes a full field's content
    // verbatim (a Supplier/Notes/Debtor value could be long, or a hand-typed
    // line could contain something the user wouldn't want redisplayed) -
    // messages name the problem and its location, not the offending text.
    auto reject = [&](FbdErrorCode code, int line, const std::wstring& msg) -> FbdLoadResult {
        result.errorCode = code;
        result.errorLine = line;
        result.errorMessage = msg;
        return result;
    };

    std::vector<std::wstring> lines;
    std::wstring cur;
    for (wchar_t c : all) {
        if (c == L'\n') {
            if (!cur.empty() && cur.back() == L'\r') cur.pop_back();
            lines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) lines.push_back(cur);

    std::vector<Entry> newEntries;
    std::wstring debtor, cash;
    std::wstring draftSupplier, draftSpecies, draftKgs, draftPrice, draftNotes, draftDate;
    std::wstring finalizedDate;
    std::wstring sourceFile;
    bool hasSourceFile = false;
    bool inData = false;
    bool sawRecognizedMarker = false; // any of DEBTOR=/CASH=/BEGIN/END/DRAFT_* actually seen
    bool sawBegin = false, sawEnd = false;
    bool sawDebtorLine = false, sawCashLine = false;
    int beginCount = 0, endCount = 0;
    int skippedLines = 0;
    int lineNo = 0;

    // Phase 1 / F3+F2 audit remediation (2026-09-30): the structural markers
    // below (BEGIN/END, DEBTOR=, CASH=) used to be tracked only loosely -
    // multiple BEGIN/END pairs, an END with no matching BEGIN, and duplicate
    // DEBTOR=/CASH= lines were all silently tolerated (the last one seen
    // simply won), rather than being treated as signs of a corrupted or
    // hand-edited file. Reject the whole document outright instead - a
    // reconciliation/loading feature must fail closed on a structurally
    // ambiguous file rather than guess which of two conflicting values to
    // trust.
    for (auto& line : lines) {
        lineNo++;

        // Phase 1 completeness follow-up (2026-09-30, v0.9.50): bound each
        // decoded line's length before doing anything else with it - applies
        // uniformly to every kind of line (a marker, a data row, or stray
        // content), so a single absurdly long line can't reach any of the
        // field-specific parsing below at all.
        if (line.size() > kMaxLineLength) {
            return reject(FbdErrorCode::LineTooLong, lineNo,
                L"a line is too long (over " + std::to_wstring(kMaxLineLength) + L" characters)");
        }

        if (line.rfind(L"DEBTOR=", 0) == 0) {
            if (sawDebtorLine) return reject(FbdErrorCode::DuplicateSingletonMetadata, lineNo, L"duplicate DEBTOR= line");
            sawDebtorLine = true;
            debtor = line.substr(7);
            if (debtor.size() > kMaxDebtorCashExprLength) {
                return reject(FbdErrorCode::FieldTooLong, lineNo,
                    L"DEBTOR= value is too long (over " + std::to_wstring(kMaxDebtorCashExprLength) + L" characters)");
            }
            sawRecognizedMarker = true;
        } else if (line.rfind(L"CASH=", 0) == 0) {
            if (sawCashLine) return reject(FbdErrorCode::DuplicateSingletonMetadata, lineNo, L"duplicate CASH= line");
            sawCashLine = true;
            cash = line.substr(5);
            if (cash.size() > kMaxDebtorCashExprLength) {
                return reject(FbdErrorCode::FieldTooLong, lineNo,
                    L"CASH= value is too long (over " + std::to_wstring(kMaxDebtorCashExprLength) + L" characters)");
            }
            sawRecognizedMarker = true;
        } else if (line.rfind(L"DRAFT_SUPPLIER=", 0) == 0) {
            draftSupplier = line.substr(15);
            if (draftSupplier.size() > kMaxSupplierSpeciesLength) {
                return reject(FbdErrorCode::FieldTooLong, lineNo, L"DRAFT_SUPPLIER= value is too long");
            }
            sawRecognizedMarker = true;
        } else if (line.rfind(L"DRAFT_SPECIES=", 0) == 0) {
            draftSpecies = line.substr(14);
            if (draftSpecies.size() > kMaxSupplierSpeciesLength) {
                return reject(FbdErrorCode::FieldTooLong, lineNo, L"DRAFT_SPECIES= value is too long");
            }
            sawRecognizedMarker = true;
        } else if (line.rfind(L"DRAFT_KGS=", 0) == 0) {
            draftKgs = line.substr(10);
            if (draftKgs.size() > kMaxDraftKgsPriceTextLength) {
                return reject(FbdErrorCode::FieldTooLong, lineNo, L"DRAFT_KGS= value is too long");
            }
            sawRecognizedMarker = true;
        } else if (line.rfind(L"DRAFT_PRICE=", 0) == 0) {
            draftPrice = line.substr(12);
            if (draftPrice.size() > kMaxDraftKgsPriceTextLength) {
                return reject(FbdErrorCode::FieldTooLong, lineNo, L"DRAFT_PRICE= value is too long");
            }
            sawRecognizedMarker = true;
        } else if (line.rfind(L"DRAFT_NOTES=", 0) == 0) {
            draftNotes = line.substr(12);
            if (draftNotes.size() > kMaxNotesLength) {
                return reject(FbdErrorCode::FieldTooLong, lineNo, L"DRAFT_NOTES= value is too long");
            }
            sawRecognizedMarker = true;
        } else if (line.rfind(L"DRAFT_DATE=", 0) == 0) {
            // Only accepted if it's a genuinely valid ISO date - a
            // corrupted/malformed value is dropped rather than being
            // passed through to whatever tries to parse it later (the
            // DateTimePicker default of "today" is a safe fallback).
            std::wstring candidate = line.substr(11);
            SimpleDate d;
            if (ParseISODate(candidate, d)) draftDate = candidate;
            sawRecognizedMarker = true;
        } else if (line.rfind(L"SOURCE_FILE=", 0) == 0) {
            // Phase 1 / F1: identity marker, not a financial figure - unlike
            // DEBTOR=/CASH= a duplicate here isn't treated as corruption,
            // the last one seen simply wins (matches how every other single-
            // value marker except DEBTOR=/CASH= already behaves).
            sourceFile = line.substr(12);
            if (sourceFile.size() > kMaxSourceFileLength) {
                return reject(FbdErrorCode::FieldTooLong, lineNo, L"SOURCE_FILE= value is too long");
            }
            hasSourceFile = true;
            sawRecognizedMarker = true;
        } else if (line.rfind(L"FINALIZED=", 0) == 0) {
            // Same validation approach as DRAFT_DATE just above - a
            // corrupted/hand-edited value is dropped rather than treating
            // the file as locked based on garbage.
            std::wstring candidate = line.substr(10);
            SimpleDate d;
            if (ParseISODate(candidate, d)) finalizedDate = candidate;
            sawRecognizedMarker = true;
        } else if (line == L"BEGIN") {
            beginCount++;
            if (beginCount > 1) {
                return reject(FbdErrorCode::DuplicateOrMisorderedMarker, lineNo, L"a second BEGIN was found");
            }
            inData = true;
            sawRecognizedMarker = true;
            sawBegin = true;
        } else if (line == L"END") {
            endCount++;
            if (endCount > 1) {
                return reject(FbdErrorCode::DuplicateOrMisorderedMarker, lineNo, L"a second END was found");
            }
            if (beginCount == 0) {
                return reject(FbdErrorCode::DuplicateOrMisorderedMarker, lineNo, L"END appeared before any BEGIN");
            }
            inData = false;
            sawRecognizedMarker = true;
            sawEnd = true;
        } else if (inData && !line.empty()) {
            std::vector<std::wstring> parts;
            std::wstring c2;
            for (wchar_t ch : line) {
                if (ch == L'|') { parts.push_back(c2); c2.clear(); }
                else c2.push_back(ch);
            }
            parts.push_back(c2);

            // Phase 1 completeness follow-up (2026-09-30, v0.9.50): field
            // count is checked FIRST, before touching parts[2]/parts[3] at
            // all - keeps "wrong shape" (MalformedEntryRow) and "right shape
            // but a bad number" (InvalidNumericField) as distinct, correctly
            // attributed diagnostics instead of a fixed size>=4 heuristic
            // that could misclassify a wrong-count-and-bad-number row.
            if (parts.size() != 4 && parts.size() != 6 && parts.size() != 7) {
                // Wrong field count (e.g. an embedded '|' from before that
                // character was blocked at entry time) - can't be safely
                // reconstructed, and (Phase 1 / F3+F2) is no longer silently
                // dropped - it invalidates the whole load so the caller can
                // tell the user plainly rather than showing a sheet that's
                // quietly missing data.
                return reject(FbdErrorCode::MalformedEntryRow, lineNo, L"row does not have a supported field count");
            }

            double kgs = 0, price = 0;
            bool numbersOk = ParseDoubleW(parts[2], kgs) && kgs >= 0 &&
                              ParseDoubleW(parts[3], price) && price >= 0;
            if (!numbersOk) {
                // A Kgs/Price value that isn't a valid finite non-negative
                // number - same "can't be safely reconstructed" reasoning.
                return reject(FbdErrorCode::InvalidNumericField, lineNo, L"row has an invalid or negative Kgs/Price value");
            }

            Entry e;
            e.supplier = TrimW(parts[0]);
            e.product = TrimW(parts[1]);
            e.kgs = kgs;
            e.price = price;
            if (parts.size() == 6 || parts.size() == 7) {
                e.date = parts[4];
                e.notes = parts[5];
            }
            if (parts.size() == 7) {
                e.priceFlagged = (parts[6] == L"1");
            }
            // parts.size() == 4 means a file saved before dates/notes
            // existed; parts.size() == 6 means a file saved before the
            // outlier-flag field existed (ROADMAP.md item 7) - in both
            // cases the missing field(s) just default (empty / false),
            // handled gracefully everywhere they're displayed.
            if (e.supplier.empty() || e.product.empty()) {
                // A row with no Supplier or Species can't have come from
                // the app's own entry form (that's rejected at entry
                // time) - only from a hand-edited or corrupted file.
                // Phase 1 / F3+F2: previously this incremented
                // skippedLines and silently continued, committing a
                // partial document once the loop finished. Reject the
                // WHOLE document instead - the caller must not build a
                // sheet that's missing rows the user never asked to drop.
                return reject(FbdErrorCode::EmptyRequiredField, lineNo, L"row has an empty Supplier or Species");
            }
            if (e.supplier.size() > kMaxSupplierSpeciesLength || e.product.size() > kMaxSupplierSpeciesLength) {
                return reject(FbdErrorCode::FieldTooLong, lineNo, L"row's Supplier or Species is too long");
            }
            if (e.notes.size() > kMaxNotesLength) {
                return reject(FbdErrorCode::FieldTooLong, lineNo, L"row's Notes is too long");
            }
            if (newEntries.size() >= kMaxEntryRecords) {
                return reject(FbdErrorCode::RecordLimitExceeded, lineNo,
                    L"more than " + std::to_wstring(kMaxEntryRecords) + L" entry rows");
            }
            newEntries.push_back(e);
        } else if (!line.empty()) {
            // F2 follow-up (post-review, 2026-09-30): a non-empty line that
            // isn't one of the recognized KEY= markers, isn't BEGIN/END, and
            // appears OUTSIDE the data section (before the first BEGIN,
            // after END, or anywhere once inData is false) used to simply
            // match no branch above and fall through silently - "entry
            // before BEGIN", "entry after END", and unrecognized stray
            // content were all tolerated exactly like the bare-END-as-no-op
            // gap F2 already closed above. Reject the whole document
            // instead, consistent with treating this kind of structural
            // ambiguity as a sign of a corrupted or hand-edited file. A
            // genuinely blank line outside the data section is still
            // harmless and ignored (matches the !line.empty() guard already
            // used for rows inside BEGIN/END).
            return reject(FbdErrorCode::UnexpectedContentOutsideDataSection, lineNo,
                L"unrecognized content outside the BEGIN/END data section");
        }
    }

    // Phase 1 completeness follow-up (2026-09-30, v0.9.50): a legitimate
    // .fbd document now ALWAYS has exactly one BEGIN and one matching END,
    // even an entirely empty sheet (DEBTOR=/CASH= with nothing else still
    // needs its own empty BEGIN...END pair) - a Debtor/Cash-only document
    // with no data-section markers at all is no longer accepted as a
    // legitimate historical format. This was tightened on further review:
    // the actual historical-compatibility requirement is about entry-ROW
    // shape (4/6/7-field rows, handled above), never about omitting the
    // structural section entirely - nothing in this app's own history ever
    // wrote a file with DEBTOR=/CASH= but no BEGIN/END at all, since every
    // save path (BuildFbdSaveContent in main.cpp) has always written both,
    // even for a blank sheet. sawRecognizedMarker is checked first so a
    // totally unrelated text file gets the more accurate "not a Fish
    // Balance file" reason rather than "missing BEGIN".
    if (!sawRecognizedMarker) return reject(FbdErrorCode::NotARecognizedFile, 0, L"not a Fish Balance (.fbd) file");
    if (!sawBegin) return reject(FbdErrorCode::MissingBegin, 0, L"no BEGIN marker found");
    if (!sawEnd) return reject(FbdErrorCode::MissingEnd, 0, L"BEGIN was never matched by an END");

    result.ok = true;
    result.entries = std::move(newEntries);
    result.debtor = debtor;
    result.cash = cash;
    result.draftSupplier = draftSupplier;
    result.draftSpecies = draftSpecies;
    result.draftKgs = draftKgs;
    result.draftPrice = draftPrice;
    result.draftNotes = draftNotes;
    result.draftDate = draftDate;
    result.finalizedDate = finalizedDate;
    result.sourceFile = sourceFile;
    result.hasSourceFile = hasSourceFile;
    result.skippedLines = skippedLines;
    return result;
}

// ---------------------------------------------------------------------------
// Test-automation refactor (2026-09-30, v0.9.51): platform-independent
// COORDINATION logic for three areas Jack previously had to re-verify by
// hand after every change (autosave identity/recovery, Finalize Day
// persistence, Save As failure) - extracted here, out of main.cpp, so the
// existing portable doctest suite can exercise the actual decision logic
// with fake inputs/fake persistence, not just read the code and trust it.
// Each function below is a pure decision: given already-known facts (a load
// result, a set of injected success/failure outcomes), what should the
// caller do? All the genuinely platform-specific work - reading files,
// showing dialogs, actually writing to disk - stays in main.cpp exactly
// where it was; these functions never touch a file, a window, or global
// state themselves.
// ---------------------------------------------------------------------------

// Ordinal (not locale-aware) case-insensitive comparison - matches the
// semantics of the Windows-only `_wcsicmp` this replaces at the one call
// site that needed to move here (DecideAutosaveRecovery, just below), which
// compares two already-normalized Windows file paths, never natural-language
// text. Deliberately simple (ASCII-range `towupper`) rather than pulling in
// full Unicode case-folding - identical behavior to `_wcsicmp` for the kind
// of paths this is ever actually called with.
inline bool CaseInsensitiveEqualsW(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (towupper((wint_t)a[i]) != towupper((wint_t)b[i])) return false;
    }
    return true;
}

// --- Autosave identity/recovery (Phase 1 / F1, extracted from wWinMain's
// startup sequence in main.cpp) -------------------------------------------
//
// Reproduces the EXACT decision wWinMain's startup block already made
// inline - this is a refactor for testability, not a behavior change (see
// CHANGELOG.md / ROADMAP.md for this version). In particular it preserves
// one existing quirk rather than silently fixing it: if autosave.fbd simply
// doesn't exist (first run, or deliberately deleted) but settings.txt still
// remembers a LASTFILE from a previous session, the app still shows a
// "Recovered your last unsaved work..." message even though nothing was
// actually recovered (autosaveLoadFailed is false and hasSourceFile is
// false in that case, same as a genuine legacy-autosave-with-no-marker
// scenario, since main.cpp never distinguishes "no autosave" from
// "autosave exists but never got a chance to set hasSourceFile" before
// calling this). Flagged in CHANGELOG.md as a pre-existing quirk, not
// fixed here per the instruction to preserve current behavior exactly.
enum class AutosaveRecoveryOutcome {
    Rejected,                 // autosave.fbd existed but failed to load - blank sheet, original file untouched
    NamedFileNotRemembered,   // no LASTFILE at all - no association attempted, no message shown
    MatchedNamedFile,         // recovered content's SOURCE_FILE= agrees with LASTFILE - re-associate
    UnsavedLegacyNoMarker,    // recovered content has no SOURCE_FILE= marker at all (predates the field)
    UnsavedMismatch,          // recovered content's SOURCE_FILE= doesn't match LASTFILE
};

struct AutosaveRecoveryDecision {
    AutosaveRecoveryOutcome outcome = AutosaveRecoveryOutcome::NamedFileNotRemembered;
    // What g_currentFile should become - only ever non-empty for
    // MatchedNamedFile. The caller (main.cpp) proves "ordinary Save cannot
    // target an unrelated file" simply by using this value: every outcome
    // except MatchedNamedFile leaves it empty, so a subsequent Save routes
    // to Save As (a fresh file) rather than silently overwriting whatever
    // LASTFILE used to point at.
    std::wstring resultingCurrentFile;
};

inline AutosaveRecoveryDecision DecideAutosaveRecovery(
        bool autosaveLoadFailed, bool hasSourceFile, const std::wstring& sourceFile,
        const std::wstring& lastFile) {
    AutosaveRecoveryDecision d;
    if (autosaveLoadFailed) {
        d.outcome = AutosaveRecoveryOutcome::Rejected;
        return d;
    }
    if (lastFile.empty()) {
        d.outcome = AutosaveRecoveryOutcome::NamedFileNotRemembered;
        return d;
    }
    bool identityMatches = hasSourceFile && CaseInsensitiveEqualsW(sourceFile, lastFile);
    if (identityMatches) {
        d.outcome = AutosaveRecoveryOutcome::MatchedNamedFile;
        d.resultingCurrentFile = lastFile;
        return d;
    }
    d.outcome = hasSourceFile ? AutosaveRecoveryOutcome::UnsavedMismatch
                              : AutosaveRecoveryOutcome::UnsavedLegacyNoMarker;
    return d;
}

// --- Finalize Day persistence coordination (Phase 1 / F9, extracted from
// FinalizeWndProc's ID_FIN_OK handler in main.cpp) --------------------------
//
// The three writes involved (rolling backup snapshot, the permanent history
// record, and keeping a currently-open named file's own FINALIZED= marker
// in sync) are injected as ports so a test can make any combination
// independently succeed or fail without touching a real filesystem. Mirrors
// the real handler's exact sequencing and exact behavior on each outcome -
// see CHANGELOG.md for this version.
enum class FinalizeResultKind {
    HistoryWriteFailed,        // nothing was finalized - g_finalizedDate must NOT be set, dirty untouched
    FinalizedNamedFileSyncFailed, // history record IS locked in, but the open named file didn't get its own copy updated
    FinalizedClean,            // both writes (or just the history write, if no named file is open) succeeded
};

struct FinalizeOutcome {
    FinalizeResultKind kind = FinalizeResultKind::HistoryWriteFailed;
    bool finalized = false;   // true iff the permanent history record was actually written - caller sets
                              // g_finalizedDate/calls ApplyFinalizedLockState() only when this is true
    bool dirtyAfter = true;   // caller's new g_dirty value - ONLY meaningful when finalized is true;
                              // on HistoryWriteFailed the caller must leave g_dirty exactly as it was
    std::wstring message;     // user-facing text, already fully composed
};

// Ports: every write this coordination needs, injected as std::function so
// tests can supply fakes. writeBackupSnapshot has no failure signal because
// the real WriteBackupSnapshot() is already deliberately best-effort/silent
// (see its own comment in main.cpp) - a failed rolling backup does not and
// never has blocked Finalize Day.
struct FinalizePersistencePorts {
    std::function<void()> writeBackupSnapshot;
    std::function<bool(std::wstring* outError)> writeHistoryRecord;
    bool hasNamedFile = false;
    std::function<bool(std::wstring* outError)> writeNamedFileSync; // only called when hasNamedFile
};

inline FinalizeOutcome CoordinateFinalizeDay(const FinalizePersistencePorts& ports, const std::wstring& isoDate) {
    FinalizeOutcome out;
    if (ports.writeBackupSnapshot) ports.writeBackupSnapshot();

    std::wstring historyErr;
    bool historyOk = ports.writeHistoryRecord && ports.writeHistoryRecord(&historyErr);
    if (!historyOk) {
        out.kind = FinalizeResultKind::HistoryWriteFailed;
        out.finalized = false;
        out.message = L"Could not write the history record: " + historyErr;
        return out;
    }

    out.finalized = true;
    if (ports.hasNamedFile) {
        std::wstring namedErr;
        bool namedOk = ports.writeNamedFileSync && ports.writeNamedFileSync(&namedErr);
        if (!namedOk) {
            out.kind = FinalizeResultKind::FinalizedNamedFileSyncFailed;
            out.dirtyAfter = true; // the open file no longer matches what was just finalized
            out.message = L"Finalized as " + isoDate + L". The permanent history record was saved "
                          L"successfully, but the open file could not be re-saved to match it: " +
                          namedErr + L"\nUse File > Save to retry before closing the app.";
            return out;
        }
    }
    out.kind = FinalizeResultKind::FinalizedClean;
    out.dirtyAfter = false;
    out.message = L"Finalized as " + isoDate + L".";
    return out;
}

// --- Save As failure coordination (Phase 1 / F1, extracted from
// DoFileSaveAs in main.cpp) --------------------------------------------------
//
// Mirrors DoFileSaveAs's exact existing logic: the new path is provisionally
// adopted BEFORE the write is attempted (so the write's own SOURCE_FILE=
// identity marker is correct for the save it's actually part of), then
// reverted if the write fails - the production behavior this proves is
// "a failed Save As restores the previous file association," not a new
// invariant being introduced.
struct SaveAsOutcome {
    bool success = false;
    std::wstring resultingCurrentFile; // == newFile on success, == previousFile (reverted) on failure
    bool dirtyAfter = true;            // only meaningful when success is true (false = matches disk)
    std::wstring errorMessage;         // only meaningful when success is false
};

inline SaveAsOutcome CoordinateSaveAs(const std::wstring& previousFile, const std::wstring& newFile,
        const std::function<bool(const std::wstring& path, std::wstring* outError)>& writeFn) {
    SaveAsOutcome out;
    std::wstring err;
    bool ok = writeFn && writeFn(newFile, &err);
    if (ok) {
        out.success = true;
        out.resultingCurrentFile = newFile;
        out.dirtyAfter = false;
    } else {
        out.success = false;
        out.resultingCurrentFile = previousFile; // reverted - never left pointing at the failed target
        out.errorMessage = err;
    }
    return out;
}

// ---------------------------------------------------------------------------
// CSV export helpers
// ---------------------------------------------------------------------------

inline std::wstring CsvField(const std::wstring& s) {
    std::wstring field = s;
    // Excel treats a field starting with =, +, -, or @ as a formula, which
    // is a known CSV-injection vector when the field came from free-text
    // user input. Prefix with a tab to neutralize it as plain text while
    // keeping it readable (a leading apostrophe would be visible in the
    // cell; a tab is not).
    if (!field.empty() && (field[0] == L'=' || field[0] == L'+' || field[0] == L'-' || field[0] == L'@'))
        field = L"\t" + field;

    bool needQuote = field.find(L',') != std::wstring::npos || field.find(L'"') != std::wstring::npos ||
                     field.find(L'\n') != std::wstring::npos;
    if (!needQuote) return field;
    std::wstring out = L"\"";
    for (wchar_t c : field) {
        if (c == L'"') out += L"\"\"";
        else out.push_back(c);
    }
    out += L"\"";
    return out;
}

// ---------------------------------------------------------------------------
// Email helpers
// ---------------------------------------------------------------------------

// A small, self-contained UTF-16 -> UTF-8 encoder used ONLY inside this
// header (by UrlEncodeForMailto below), so this file has no dependency on
// Win32's WideCharToMultiByte. main.cpp's own WToUtf8() is untouched and
// remains the one used for actual file I/O - this exists purely so the
// mailto: percent-encoding logic (which operates on UTF-8 bytes) can be
// compiled and tested on any platform. Handles the full Unicode range via
// UTF-16 surrogate pairs, matching what WideCharToMultiByte(CP_UTF8, ...)
// produces for the same input on Windows.
inline std::string PortableWToUtf8(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (size_t i = 0; i < w.size(); i++) {
        uint32_t cp = (uint32_t)(uint16_t)w[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size()) {
            uint32_t low = (uint32_t)(uint16_t)w[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                i++;
            }
        }
        if (cp <= 0x7F) {
            out.push_back((char)cp);
        } else if (cp <= 0x7FF) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// Percent-encodes text for use in a mailto: URL (subject/body query
// values). Operates on the UTF-8 bytes so non-ASCII characters survive
// intact.
inline std::wstring UrlEncodeForMailto(const std::wstring& text) {
    std::string utf8 = PortableWToUtf8(text);
    std::wstring out;
    const wchar_t* hex = L"0123456789ABCDEF";
    for (unsigned char c : utf8) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((wchar_t)c);
        } else {
            out.push_back(L'%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

// Very basic sanity check - not full RFC validation, just enough to catch
// obvious typos before they get saved.
inline bool LooksLikeEmail(const std::wstring& email) {
    if (email.find(L' ') != std::wstring::npos) return false;
    size_t at = email.find(L'@');
    if (at == std::wstring::npos || at == 0 || at == email.size() - 1) return false;
    size_t dot = email.find(L'.', at);
    if (dot == std::wstring::npos || dot == email.size() - 1) return false;
    return true;
}

inline std::wstring GreetingForHour(int hour) {
    if (hour < 12) return L"Good morning";
    if (hour < 17) return L"Good afternoon";
    return L"Good evening"; // covers evening hours too, beyond just morning/afternoon
}

inline std::wstring FormatLongDate(const SimpleDate& d) {
    static const wchar_t* months[] = { L"January", L"February", L"March", L"April", L"May", L"June",
                                        L"July", L"August", L"September", L"October", L"November", L"December" };
    std::wstring m = (d.month >= 1 && d.month <= 12) ? months[d.month - 1] : L"";
    return std::to_wstring(d.day) + L" " + m + L" " + std::to_wstring(d.year);
}

// Pads a string with trailing spaces to at least `width` characters, for a
// best-effort aligned plain-text table (exact alignment isn't guaranteed in
// every email client, since not all render plain text in a fixed-width
// font, but this matches in the common case).
inline std::wstring PadRight(const std::wstring& s, size_t width) {
    std::wstring out = s;
    while (out.size() < width) out.push_back(L' ');
    return out;
}

// currentHour is passed in (rather than read via GetLocalTime internally)
// specifically so this function has no Win32 dependency and can be tested
// directly with any hour value - main.cpp's call site passes the real
// current hour.
inline std::wstring BuildSupplierEmailBody(const SupplierGroup& sg, int currentHour) {
    std::wstring body = GreetingForHour(currentHour) + L",\r\n\r\n";
    body += L"Please see prices below\r\n";
    body += PadRight(L"KG", 7) + PadRight(L"Species", 18) + L"Price\r\n";
    for (auto& pg : sg.products) {
        for (auto& pl : pg.prices) {
            body += PadRight(FormatKg(pl.kgs), 7) + PadRight(pg.species, 18) + FormatMoney(pl.price) + L"\r\n";
        }
    }
    body += L"\r\nKind regards,";

    // mailto: bodies are practically capped well under the URL length some
    // mail clients/OS versions tolerate - if a supplier has an unusually
    // long list of line items, fall back to a shorter summary instead of
    // risking a mailto that silently fails to open or gets truncated.
    if (body.size() > 1500) {
        body = GreetingForHour(currentHour) + L",\r\n\r\nPlease see prices below - " +
               std::to_wstring(sg.products.size()) + L" species, total weight " +
               FormatKg(sg.totalKgs) + L" kg.\r\n\r\n(Full line-by-line pricing is in the app - "
               L"this summary was shortened because the full list was too long for email.)\r\n\r\n"
               L"Kind regards,";
    }
    return body;
}
