#include "doctest_setup.h"
#include "doctest.h"
#include "../src/core/FishBalanceCore.h"

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

    TEST_CASE("Phase 2 / F12 (2026-10-02): leading spaces/tabs before a formula "
              "character no longer bypass the guard - previously documented gap, "
              "now fixed") {
        CHECK(CsvField(L" =1+1") == L"\t =1+1");
        CHECK(CsvField(L"  +SUM(A1:A10)") == L"\t  +SUM(A1:A10)");
        CHECK(CsvField(L"\t-1") == L"\t\t-1");
        CHECK(CsvField(L" \t@cmd") == L"\t \t@cmd");
    }

    TEST_CASE("leading whitespace with NO formula character after it is left alone") {
        CHECK(CsvField(L"  plain text") == L"  plain text");
        CHECK(CsvField(L"   ") == L"   "); // all whitespace, nothing to guard against
    }

    TEST_CASE("a field needing both quoting and the formula guard gets both, "
              "with the tab prefix applied BEFORE quoting wraps around it") {
        // Traced through the implementation: the tab prefix is added to
        // the field first, then quote-wrapping wraps around the
        // already-prefixed string - so the tab ends up right after the
        // opening quote, not before it.
        CHECK(CsvField(L"=1,2") == L"\"\t=1,2\"");
    }

    TEST_CASE("Phase 2 / F12: NUL and CR are stripped entirely") {
        CHECK(CsvField(std::wstring(L"a\0b", 3)) == L"ab");
        CHECK(CsvField(L"a\rb") == L"ab");
        CHECK(CsvField(L"a\r\nb") == L"\"a\nb\""); // CRLF normalizes to a bare LF, which still forces quoting
    }

    TEST_CASE("Phase 2 / F12: other C0 control characters are stripped, but TAB "
              "and LF are preserved") {
        std::wstring withControls;
        withControls.push_back(L'a');
        withControls.push_back(0x01); // SOH - stripped
        withControls.push_back(0x1F); // unit separator - stripped
        withControls.push_back(L'\t'); // preserved
        withControls.push_back(L'b');
        CHECK(CsvField(withControls) == L"a\tb");
    }

    TEST_CASE("Unicode text passes through unchanged (no control/formula chars)") {
        CHECK(CsvField(L"Café Jcasement Æ") == L"Café Jcasement Æ");
    }

    TEST_CASE("empty value stays empty") {
        CHECK(CsvField(L"") == L"");
    }
}

TEST_SUITE("ExportToCsv-level hardening (full rows, not just CsvField)") {
    // ExportToCsv() itself lives in main.cpp (it needs g_entries, Win32 file
    // I/O, etc.) and isn't reachable from this portable suite - but every
    // field it writes goes through the csvRow()/CsvField() chokepoint this
    // suite CAN exercise directly, so a full simulated row below exercises
    // the same code path real export does, just without the Win32 parts.
    TEST_CASE("a full hostile row is defended consistently across every field") {
        std::wstring supplier = L" =cmd|'/C calc'!A1";
        std::wstring species = L"Garfish\r\nFAKE,INJECTED,ROW";
        std::wstring notes = L"has,comma \"and quote\" and\ttab";

        std::wstring row;
        auto append = [&](const std::wstring& f) {
            if (!row.empty()) row += L",";
            row += CsvField(f);
        };
        append(supplier);
        append(species);
        append(notes);

        // The formula guard fires even though the real content starts after
        // a leading space, and the field is still correctly comma/quote-safe.
        CHECK(row.find(L"\t =cmd") != std::wstring::npos);
        // No raw CR survives anywhere in the row (species field).
        CHECK(row.find(L'\r') == std::wstring::npos);
        // The embedded fake "row" text is still safely inside ONE quoted
        // field - i.e. it's not actually possible to tell, from the comma
        // count alone, that this was an injection attempt.
        CHECK(row.find(L"\"Garfish\nFAKE,INJECTED,ROW\"") != std::wstring::npos);
        // The notes field's comma/quote/tab content round-trips correctly
        // quoted.
        CHECK(row.find(L"\"has,comma \"\"and quote\"\" and\ttab\"") != std::wstring::npos);
    }
}
