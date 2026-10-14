#include "doctest_setup.h"
#include "doctest.h"
#include "../src/core/FishBalanceCore.h"

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

TEST_SUITE("BuildBackupFileName") {
    TEST_CASE("includes milliseconds so same-second snapshots differ") {
        std::wstring a = BuildBackupFileName(2026, 10, 14, 9, 5, 3, 120, L"day", 0);
        std::wstring b = BuildBackupFileName(2026, 10, 14, 9, 5, 3, 121, L"day", 0);
        CHECK(a == L"backup_20261014_090503_120_day.fbd");
        CHECK(a != b);
    }
    TEST_CASE("collision counter yields a distinct name; zero adds no suffix") {
        std::wstring a = BuildBackupFileName(2026, 10, 14, 9, 5, 3, 120, L"x", 0);
        std::wstring b = BuildBackupFileName(2026, 10, 14, 9, 5, 3, 120, L"x", 1);
        CHECK(b == L"backup_20261014_090503_120-1_x.fbd");
        CHECK(a != b);
    }
    TEST_CASE("names sort chronologically (fixed width)") {
        std::wstring early = BuildBackupFileName(2026, 10, 14, 9, 5, 3, 9, L"x", 0);
        std::wstring later = BuildBackupFileName(2026, 10, 14, 9, 5, 3, 10, L"x", 0);
        CHECK(early < later);
    }
}

TEST_SUITE("BuildAtomicTempPath") {
    TEST_CASE("temp path includes the process id and differs between processes") {
        CHECK(BuildAtomicTempPath(L"C:\\d\\a.fbd", 123) == L"C:\\d\\a.fbd.123.tmp");
        CHECK(BuildAtomicTempPath(L"a.fbd", 1) != BuildAtomicTempPath(L"a.fbd", 2));
    }
    TEST_CASE("temp path never equals the destination") {
        CHECK(BuildAtomicTempPath(L"a.fbd", 5) != L"a.fbd");
    }
}

TEST_SUITE("ClampRectIntoArea") {
    TEST_CASE("a popup that already fits is left exactly where it is") {
        int x = 100, y = 50, w = 600, h = 700;
        ClampRectIntoArea(0, 0, 1920, 1040, x, y, w, h);
        CHECK(x == 100);
        CHECK(y == 50);
        CHECK(w == 600);
        CHECK(h == 700);
    }
    TEST_CASE("Jack's case: popup taller than the work area is shrunk and moved to the top") {
        // 150% scaling: S(820) = 1230 tall on a 1040-tall work area, centred so y went negative.
        int x = 300, y = -95, w = 1020, h = 1230;
        ClampRectIntoArea(0, 0, 1920, 1040, x, y, w, h);
        CHECK(h == 1040);
        CHECK(y == 0);
        CHECK(x == 300);
        CHECK(w == 1020);
    }
    TEST_CASE("a popup hanging off the right/bottom is slid back on-screen") {
        int x = 1700, y = 900, w = 500, h = 400;
        ClampRectIntoArea(0, 0, 1920, 1040, x, y, w, h);
        CHECK(x + w == 1920);
        CHECK(y + h == 1040);
    }
    TEST_CASE("respects a work area that does not start at the origin (second monitor / taskbar at top)") {
        int x = -50, y = 0, w = 400, h = 300;
        ClampRectIntoArea(1920, 40, 3840, 1080, x, y, w, h);
        CHECK(x == 1920);
        CHECK(y == 40);
    }
    TEST_CASE("wider than the area is also shrunk") {
        int x = 0, y = 0, w = 5000, h = 100;
        ClampRectIntoArea(0, 0, 1920, 1040, x, y, w, h);
        CHECK(w == 1920);
        CHECK(x == 0);
    }
}
