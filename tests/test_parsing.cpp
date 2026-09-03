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

TEST_SUITE("ParseSumExpr") {
    TEST_CASE("sums plus-separated terms") {
        CHECK(ParseSumExpr(L"250.7+1826+2552+286") == doctest::Approx(250.7 + 1826 + 2552 + 286));
        CHECK(ParseSumExpr(L"1+2+3") == 6);
    }
    TEST_CASE("handles a single term with no plus sign") {
        CHECK(ParseSumExpr(L"42") == 42);
    }
    TEST_CASE("empty string sums to zero") {
        CHECK(ParseSumExpr(L"") == 0);
    }
    TEST_CASE("tolerates whitespace around terms") {
        CHECK(ParseSumExpr(L" 1 + 2 + 3 ") == 6);
    }
    TEST_CASE("CHARACTERIZATION (documented gap, not fixed): a bad term is silently "
              "dropped rather than failing the whole expression") {
        // See SecurityHardeningRegister.md / ROADMAP.md Tier 3 - this is a
        // known, already-documented permissive behavior, preserved exactly
        // as-is. This test exists so a future change to this behavior is a
        // deliberate decision, not an accidental side effect of some other
        // refactor.
        CHECK(ParseSumExpr(L"1000+500+oops+250") == 1750);
    }
    TEST_CASE("CHARACTERIZATION (documented gap, not fixed): a term with trailing "
              "junk contributes its numeric prefix instead of being rejected") {
        // Because this uses std::stod directly rather than the stricter
        // ParseDoubleW, "12x" contributes 12 with the "x" silently ignored.
        CHECK(ParseSumExpr(L"12x+3") == 15);
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
