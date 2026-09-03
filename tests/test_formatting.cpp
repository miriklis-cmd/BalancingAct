#include "doctest_setup.h"
#include "doctest.h"
#include "../FishBalanceCore.h"

TEST_SUITE("FormatMoney") {
    TEST_CASE("formats with a dollar sign and 2 decimal places") {
        CHECK(FormatMoney(0) == L"$0.00");
        CHECK(FormatMoney(1.5) == L"$1.50");
        CHECK(FormatMoney(1.006) == L"$1.01"); // rounds up
        CHECK(FormatMoney(1.004) == L"$1.00"); // rounds down
        // Note: a value like 1.005 is deliberately NOT used here - IEEE 754
        // doubles can't represent it exactly (it's actually stored as
        // ~1.00499999999999989342), so it correctly rounds DOWN to $1.00,
        // not up to $1.01 as naive intuition suggests. That's the same
        // well-known class of gotcha as 0.1+0.2 != 0.3 in any language
        // using binary floating point - not a bug in FormatMoney, just a
        // bad choice of test value sitting exactly on a representation
        // boundary. 1.006/1.004 above are safely clear of that boundary
        // on either side.
    }
    TEST_CASE("inserts thousands separators") {
        CHECK(FormatMoney(1234.5) == L"$1,234.50");
        CHECK(FormatMoney(1234567.89) == L"$1,234,567.89");
        CHECK(FormatMoney(999) == L"$999.00"); // no separator needed under 1000
    }
    TEST_CASE("handles negative values with the minus before the dollar sign") {
        CHECK(FormatMoney(-42.1) == L"-$42.10");
        CHECK(FormatMoney(-1234.5) == L"-$1,234.50");
    }
}

TEST_SUITE("FormatKg / FormatNum") {
    TEST_CASE("FormatKg always uses exactly 1 decimal place (business convention)") {
        CHECK(FormatKg(13.849) == L"13.8");
        CHECK(FormatKg(0) == L"0.0");
        CHECK(FormatKg(5) == L"5.0");
    }
    TEST_CASE("FormatNum always uses exactly 2 decimal places") {
        CHECK(FormatNum(13.849) == L"13.85"); // rounds
        CHECK(FormatNum(0) == L"0.00");
        CHECK(FormatNum(5) == L"5.00");
    }
    TEST_CASE("FormatKg and FormatNum do NOT insert thousands separators "
              "(that's FormatMoney-specific)") {
        CHECK(FormatKg(1234.5) == L"1234.5");
        CHECK(FormatNum(1234.5) == L"1234.50");
    }
}
