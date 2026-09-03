#include "doctest_setup.h"
#include "doctest.h"
#include "../FishBalanceCore.h"

TEST_SUITE("CsvField") {
    TEST_CASE("plain text passes through unchanged") {
        CHECK(CsvField(L"plain") == L"plain");
        CHECK(CsvField(L"Jcasement") == L"Jcasement");
        CHECK(CsvField(L"") == L"");
    }

    TEST_CASE("fields containing a comma get quoted") {
        CHECK(CsvField(L"has,comma") == L"\"has,comma\"");
    }

    TEST_CASE("fields containing a double-quote get quoted, with the quote "
              "itself doubled") {
        CHECK(CsvField(L"has\"quote") == L"\"has\"\"quote\"");
    }

    TEST_CASE("fields containing a newline get quoted") {
        CHECK(CsvField(L"line1\nline2") == L"\"line1\nline2\"");
    }

    TEST_CASE("formula-injection guard: a field starting with = + - or @ gets "
              "a tab prefix so Excel doesn't interpret it as a formula") {
        // Excel treats a leading =, +, -, or @ as the start of a formula -
        // a known CSV-injection vector when the field came from free-text
        // user input (Supplier/Species/Notes). See SecurityHardeningRegister.md.
        CHECK(CsvField(L"=1+1") == L"\t=1+1");
        CHECK(CsvField(L"+SUM(A1:A10)") == L"\t+SUM(A1:A10)");
        CHECK(CsvField(L"-1") == L"\t-1");
        CHECK(CsvField(L"@cmd") == L"\t@cmd");
    }

    TEST_CASE("the formula-injection guard only looks at the FIRST character") {
        // Documented, known gap (SecurityHardeningRegister.md / ROADMAP.md
        // Tier 3): a leading space before the formula character bypasses
        // this guard. Preserved as-is here as a characterization test, not
        // fixed as part of this test-suite addition - fixing it is a
        // separate, deliberate change.
        CHECK(CsvField(L" =1+1") == L" =1+1"); // NOT tab-prefixed - known gap
    }

    TEST_CASE("a field needing both quoting and the formula guard gets both, "
              "with the tab prefix applied BEFORE quoting wraps around it") {
        // Traced through the implementation: the tab prefix is added to
        // the field first, then quote-wrapping wraps around the
        // already-prefixed string - so the tab ends up right after the
        // opening quote, not before it.
        CHECK(CsvField(L"=1,2") == L"\"\t=1,2\"");
    }
}
