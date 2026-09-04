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
// correctly computes 113, not 134 (an earlier version of this function
// only recognized '+', so "11-21" was treated as one term and
// std::stod silently parsed just its "11" prefix, dropping the "-21"
// entirely - a real bug, not a documented gap, fixed here).
//
// NOTE: this still silently drops any term that fails to parse at all
// (e.g. "oops"), and - because it uses std::stod directly rather than
// the stricter ParseDoubleW - a term like "12x" contributes 12 with the
// trailing "x" silently ignored, rather than being rejected outright.
// This is a known, already documented gap (see
// SecurityHardeningRegister.md / ROADMAP.md's Tier 3 items), preserved
// unchanged here - only operator support was added, not term validation.
inline double ParseSumExpr(const std::wstring& s) {
    double sum = 0;
    std::wstring cur;
    double sign = 1.0; // applies to whichever term is currently being accumulated
    auto flush = [&]() {
        std::wstring t = TrimW(cur);
        if (!t.empty()) {
            try { sum += sign * std::stod(t); } catch (...) {}
        }
        cur.clear();
    };
    for (wchar_t c : s) {
        if (c == L'+') {
            flush();
            sign = 1.0;
        } else if (c == L'-') {
            flush();
            sign = -1.0;
        } else {
            cur.push_back(c);
        }
    }
    flush();
    return sum;
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
// .fbd file content parsing (portable core of LoadFromFile)
//
// Takes the FULL, ALREADY-DECODED file content as a single wstring (the
// Win32-specific "open the file, read the bytes, convert from UTF-8" step
// stays in main.cpp's LoadFromFile, since that's a genuine platform
// boundary - everything from "split into lines" onward, which is where
// every serious bug in this app has lived, is here and fully testable.
// ---------------------------------------------------------------------------

struct FbdLoadResult {
    bool ok = false;              // false => reject the whole document; caller must not
                                   // touch any live state (see LoadFromFile in main.cpp)
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
};

inline FbdLoadResult ParseFbdContent(const std::wstring& all) {
    FbdLoadResult result;

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
    bool inData = false;
    bool sawRecognizedMarker = false; // any of DEBTOR=/CASH=/BEGIN/END/DRAFT_* actually seen
    bool sawBegin = false, sawEnd = false;
    int skippedLines = 0;
    for (auto& line : lines) {
        if (line.rfind(L"DEBTOR=", 0) == 0) {
            debtor = line.substr(7);
            sawRecognizedMarker = true;
        } else if (line.rfind(L"CASH=", 0) == 0) {
            cash = line.substr(5);
            sawRecognizedMarker = true;
        } else if (line.rfind(L"DRAFT_SUPPLIER=", 0) == 0) {
            draftSupplier = line.substr(15);
            sawRecognizedMarker = true;
        } else if (line.rfind(L"DRAFT_SPECIES=", 0) == 0) {
            draftSpecies = line.substr(14);
            sawRecognizedMarker = true;
        } else if (line.rfind(L"DRAFT_KGS=", 0) == 0) {
            draftKgs = line.substr(10);
            sawRecognizedMarker = true;
        } else if (line.rfind(L"DRAFT_PRICE=", 0) == 0) {
            draftPrice = line.substr(12);
            sawRecognizedMarker = true;
        } else if (line.rfind(L"DRAFT_NOTES=", 0) == 0) {
            draftNotes = line.substr(12);
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
        } else if (line == L"BEGIN") {
            inData = true;
            sawRecognizedMarker = true;
            sawBegin = true;
        } else if (line == L"END") {
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
            double kgs = 0, price = 0;
            bool numbersOk = parts.size() >= 4 && ParseDoubleW(parts[2], kgs) && kgs >= 0 &&
                              ParseDoubleW(parts[3], price) && price >= 0;
            if ((parts.size() == 4 || parts.size() == 6) && numbersOk) {
                Entry e;
                e.supplier = TrimW(parts[0]);
                e.product = TrimW(parts[1]);
                e.kgs = kgs;
                e.price = price;
                if (parts.size() == 6) {
                    e.date = parts[4];
                    e.notes = parts[5];
                }
                // parts.size() == 4 means a file saved before dates/notes
                // existed - e.date and e.notes just stay empty, handled
                // gracefully everywhere they're displayed.
                if (e.supplier.empty() || e.product.empty()) {
                    // A row with no Supplier or Species can't have come from
                    // the app's own entry form (that's rejected at entry
                    // time) - only from a hand-edited or corrupted file.
                    // Surface it rather than silently showing a blank-named
                    // group in every report.
                    skippedLines++;
                } else {
                    newEntries.push_back(e);
                }
            } else {
                // Wrong field count (e.g. an embedded '|' from before that
                // character was blocked at entry time), or a Kgs/Price value
                // that isn't a valid finite non-negative number - can't be
                // safely reconstructed. Skip it, but don't lose it silently;
                // the caller is told how many rows this happened to.
                skippedLines++;
            }
        }
    }

    // Reject the whole document if it doesn't look like a genuine .fbd file
    // at all (no DEBTOR=/CASH=/BEGIN/END markers found - e.g. some unrelated
    // text file was selected), or if BEGIN was opened but END never
    // appeared (the write was cut off partway through, e.g. by a crash or a
    // full disk).
    if (!sawRecognizedMarker) return result; // ok stays false
    if (sawBegin && !sawEnd) return result;  // ok stays false

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
    result.skippedLines = skippedLines;
    return result;
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
