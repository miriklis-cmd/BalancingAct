#include "doctest_setup.h"
#include "doctest.h"
#include "../FishBalanceCore.h"

TEST_SUITE("TrimW") {
    TEST_CASE("removes leading and trailing whitespace") {
        CHECK(TrimW(L"  hi  ") == L"hi");
        CHECK(TrimW(L"\t\r\nx\r\n") == L"x");
    }
    TEST_CASE("leaves interior whitespace alone") {
        CHECK(TrimW(L"  a b c  ") == L"a b c");
    }
    TEST_CASE("empty and all-whitespace strings become empty") {
        CHECK(TrimW(L"") == L"");
        CHECK(TrimW(L"   ") == L"");
        CHECK(TrimW(L"\t\r\n") == L"");
    }
    TEST_CASE("string with no whitespace is unchanged") {
        CHECK(TrimW(L"nospaces") == L"nospaces");
    }
}

TEST_SUITE("ParseDoubleW") {
    TEST_CASE("parses plain numbers") {
        double v;
        CHECK(ParseDoubleW(L"12.5", v));
        CHECK(v == 12.5);
        CHECK(ParseDoubleW(L"0", v));
        CHECK(v == 0);
        CHECK(ParseDoubleW(L"-3.25", v));
        CHECK(v == -3.25);
    }
    TEST_CASE("trims surrounding whitespace before parsing") {
        double v;
        CHECK(ParseDoubleW(L"  12.5  ", v));
        CHECK(v == 12.5);
    }
    TEST_CASE("rejects empty or whitespace-only input") {
        double v;
        CHECK_FALSE(ParseDoubleW(L"", v));
        CHECK_FALSE(ParseDoubleW(L"   ", v));
    }
    TEST_CASE("rejects trailing junk instead of parsing a numeric prefix") {
        double v;
        CHECK_FALSE(ParseDoubleW(L"12.5x", v));
        CHECK_FALSE(ParseDoubleW(L"12x5", v));
        CHECK_FALSE(ParseDoubleW(L"abc", v));
    }
    TEST_CASE("v0.9.1 regression: rejects NaN and Infinity outright") {
        // std::stod happily parses these as fully-consumed "successful"
        // values, which is exactly the bug fixed in v0.9.1 - a NaN/Inf
        // Kgs or Price would otherwise poison totals, sorting, and the
        // price-grouping maps downstream. See SecurityHardeningRegister.md
        // item #8.
        double v;
        CHECK_FALSE(ParseDoubleW(L"nan", v));
        CHECK_FALSE(ParseDoubleW(L"-nan", v));
        CHECK_FALSE(ParseDoubleW(L"inf", v));
        CHECK_FALSE(ParseDoubleW(L"-inf", v));
        CHECK_FALSE(ParseDoubleW(L"infinity", v));
    }
}

TEST_SUITE("ParseSumExprStrict") {
    // --- Behavior carried over unchanged from the old ParseSumExpr ---

    TEST_CASE("sums plus-separated terms") {
        auto r1 = ParseSumExprStrict(L"250.7+1826+2552+286");
        CHECK(r1.ok);
        CHECK(r1.value == doctest::Approx(250.7 + 1826 + 2552 + 286));

        auto r2 = ParseSumExprStrict(L"1+2+3");
        CHECK(r2.ok);
        CHECK(r2.value == 6);
    }
    TEST_CASE("handles a single term with no plus sign") {
        auto r = ParseSumExprStrict(L"42");
        CHECK(r.ok);
        CHECK(r.value == 42);
    }
    TEST_CASE("empty (or whitespace-only) string is valid and sums to zero") {
        // Preserves the common case of an untouched Debtor/Cash field.
        auto r1 = ParseSumExprStrict(L"");
        CHECK(r1.ok);
        CHECK(r1.value == 0);

        auto r2 = ParseSumExprStrict(L"   ");
        CHECK(r2.ok);
        CHECK(r2.value == 0);
    }
    TEST_CASE("tolerates whitespace around terms") {
        auto r = ParseSumExprStrict(L" 1 + 2 + 3 ");
        CHECK(r.ok);
        CHECK(r.value == 6);
    }
    TEST_CASE("subtraction is a real operator, not silently swallowed into "
              "the following term") {
        // Real business scenario: Cash="123+11-21" should compute as
        // 123 + 11 - 21 = 113.
        auto r1 = ParseSumExprStrict(L"123+11-21");
        CHECK(r1.ok);
        CHECK(r1.value == doctest::Approx(113));

        auto r2 = ParseSumExprStrict(L"100-50");
        CHECK(r2.ok);
        CHECK(r2.value == 50);

        auto r3 = ParseSumExprStrict(L"10-3-2"); // multiple subtractions in a row
        CHECK(r3.ok);
        CHECK(r3.value == 5);

        auto r4 = ParseSumExprStrict(L"10+5-3+2"); // mixed +/-
        CHECK(r4.ok);
        CHECK(r4.value == 14);
    }
    TEST_CASE("a leading sign on the first term is allowed") {
        auto r1 = ParseSumExprStrict(L"-50+100");
        CHECK(r1.ok);
        CHECK(r1.value == 50);

        auto r2 = ParseSumExprStrict(L"+100");
        CHECK(r2.ok);
        CHECK(r2.value == 100);
    }

    // --- Phase 1 / F5 audit remediation: fail CLOSED instead of silently
    //     dropping or truncating a bad term ---

    TEST_CASE("FIXED (was a documented gap): a bad term now fails the whole "
              "expression instead of being silently dropped") {
        auto r = ParseSumExprStrict(L"1000+oops+250");
        CHECK_FALSE(r.ok);
    }
    TEST_CASE("FIXED (was a documented gap): a term with trailing junk is "
              "now rejected instead of contributing its numeric prefix") {
        auto r1 = ParseSumExprStrict(L"1000+12x+250");
        CHECK_FALSE(r1.ok);

        auto r2 = ParseSumExprStrict(L"12x+3");
        CHECK_FALSE(r2.ok);
    }
    TEST_CASE("rejects a dangling trailing operator") {
        auto r = ParseSumExprStrict(L"100+");
        CHECK_FALSE(r.ok);
    }
    TEST_CASE("rejects an operator immediately following another operator") {
        // One rule covers all three shapes: an operator with nothing
        // accumulated since the previous operator (or the leading sign).
        CHECK_FALSE(ParseSumExprStrict(L"100++20").ok);
        CHECK_FALSE(ParseSumExprStrict(L"100+-20").ok);
        CHECK_FALSE(ParseSumExprStrict(L"100--20").ok);
    }
    TEST_CASE("rejects NaN/Infinity terms, same policy as ParseDoubleW") {
        CHECK_FALSE(ParseSumExprStrict(L"nan").ok);
        CHECK_FALSE(ParseSumExprStrict(L"inf").ok);
        CHECK_FALSE(ParseSumExprStrict(L"-inf").ok);
        CHECK_FALSE(ParseSumExprStrict(L"100+nan").ok);
    }
    TEST_CASE("rejects scientific notation (deliberately unsupported)") {
        CHECK_FALSE(ParseSumExprStrict(L"1e10").ok);
        CHECK_FALSE(ParseSumExprStrict(L"100+1e2").ok);
    }
    TEST_CASE("still sums plain addition-only expressions correctly "
              "(no regression from strictness)") {
        auto r1 = ParseSumExprStrict(L"250.7+1826+2552+286");
        CHECK(r1.ok);
        CHECK(r1.value == doctest::Approx(250.7 + 1826 + 2552 + 286));

        auto r2 = ParseSumExprStrict(L"1+2+3");
        CHECK(r2.ok);
        CHECK(r2.value == 6);
    }
}

TEST_SUITE("FormatDateISO / ParseISODate") {
    TEST_CASE("round-trips a normal date") {
        SimpleDate d{2026, 8, 5};
        CHECK(FormatDateISO(d) == L"2026-08-05");
        SimpleDate parsed;
        CHECK(ParseISODate(L"2026-08-05", parsed));
        CHECK(parsed.year == 2026);
        CHECK(parsed.month == 8);
        CHECK(parsed.day == 5);
    }
    TEST_CASE("zero-pads single-digit month and day") {
        CHECK(FormatDateISO(SimpleDate{2026, 1, 2}) == L"2026-01-02");
    }
    TEST_CASE("rejects malformed input") {
        SimpleDate d;
        CHECK_FALSE(ParseISODate(L"", d));
        CHECK_FALSE(ParseISODate(L"2026/08/05", d));  // wrong separators
        CHECK_FALSE(ParseISODate(L"26-08-05", d));    // wrong length
        CHECK_FALSE(ParseISODate(L"2026-13-05", d));  // month out of range
        CHECK_FALSE(ParseISODate(L"2026-00-05", d));  // month out of range
        CHECK_FALSE(ParseISODate(L"2026-08-00", d));  // day out of range
        CHECK_FALSE(ParseISODate(L"2026-08-32", d));  // day out of range
        CHECK_FALSE(ParseISODate(L"1899-08-05", d));  // year below minimum
    }
    TEST_CASE("CHARACTERIZATION (documented gap, not fixed): day-of-month isn't "
              "validated against the actual month/year") {
        // See SecurityHardeningRegister.md / ROADMAP.md Tier 4 - e.g.
        // "2026-02-31" (February only has 28/29 days) is currently
        // accepted. Preserved exactly as-is; this test exists so fixing
        // it later is a deliberate decision, not an accidental side
        // effect of an unrelated change.
        SimpleDate d;
        CHECK(ParseISODate(L"2026-02-31", d));
        CHECK(d.month == 2);
        CHECK(d.day == 31);
    }
}
