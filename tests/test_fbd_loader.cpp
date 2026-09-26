#include "doctest_setup.h"
#include "doctest.h"
#include "../FishBalanceCore.h"

// A reminder for anyone adding fixtures here: the real .fbd row format is
// Supplier|Species|Kgs|Price|Date|Notes|Flagged (7 fields, 6 pipes),
// Supplier|Species|Kgs|Price|Date|Notes (6 fields, 5 pipes, pre-v0.9.19 -
// Flagged defaults to false), or the older Supplier|Species|Kgs|Price
// (4 fields, 3 pipes) for backward compatibility. A stray extra pipe
// silently produces a 5-field row, which the parser correctly treats as
// "wrong field count" rather than whatever you meant to test - count your
// pipes carefully. (This bit an earlier draft of this exact test file -
// see the git history / CHANGELOG if curious.)

TEST_SUITE("ParseFbdContent - valid documents") {
    TEST_CASE("a normal, well-formed file with Date/Notes loads correctly") {
        std::wstring content =
            L"DEBTOR=100\n"
            L"CASH=50\n"
            L"BEGIN\n"
            L"Jcasement|Garfish|13.8000|17.0000|2026-08-05|\n"
            L"Jcasement|Rock Flat|5.5000|12.0000|2026-08-05|Extra fresh\n"
            L"END\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        REQUIRE(r.entries.size() == 2);
        CHECK(r.debtor == L"100");
        CHECK(r.cash == L"50");
        CHECK(r.skippedLines == 0);
        CHECK(r.entries[0].supplier == L"Jcasement");
        CHECK(r.entries[0].product == L"Garfish");
        CHECK(r.entries[0].kgs == doctest::Approx(13.8));
        CHECK(r.entries[0].price == doctest::Approx(17.0));
        CHECK(r.entries[0].date == L"2026-08-05");
        CHECK(r.entries[0].notes == L"");
        CHECK(r.entries[1].notes == L"Extra fresh");
    }

    TEST_CASE("backward compatibility: old 4-field format (no Date/Notes) loads "
              "with those fields left blank") {
        std::wstring content = L"BEGIN\nA|B|1.0|2.0\nEND\n"; // 3 pipes = 4 fields
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        REQUIRE(r.entries.size() == 1);
        CHECK(r.entries[0].date.empty());
        CHECK(r.entries[0].notes.empty());
        CHECK(r.entries[0].priceFlagged == false);
    }

    TEST_CASE("backward compatibility: pre-v0.9.19 6-field format (Date/Notes "
              "but no priceFlagged column) defaults priceFlagged to false") {
        std::wstring content = L"BEGIN\nA|B|1.0|2.0|2026-08-05|some notes\nEND\n"; // 5 pipes = 6 fields
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        REQUIRE(r.entries.size() == 1);
        CHECK(r.entries[0].date == L"2026-08-05");
        CHECK(r.entries[0].notes == L"some notes");
        CHECK(r.entries[0].priceFlagged == false);
    }

    TEST_CASE("a 7-field row with priceFlagged=1 loads as flagged") {
        std::wstring content = L"BEGIN\nA|B|1.0|2.0|2026-08-05|notes|1\nEND\n"; // 6 pipes = 7 fields
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        REQUIRE(r.entries.size() == 1);
        CHECK(r.entries[0].priceFlagged == true);
    }

    TEST_CASE("a 7-field row with priceFlagged=0 loads as not flagged") {
        std::wstring content = L"BEGIN\nA|B|1.0|2.0|2026-08-05|notes|0\nEND\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        REQUIRE(r.entries.size() == 1);
        CHECK(r.entries[0].priceFlagged == false);
    }

    TEST_CASE("a file with BEGIN/END but zero entries (fresh sheet, only "
              "Debtor/Cash filled in) is a normal, valid, empty load") {
        std::wstring content = L"DEBTOR=1000\nCASH=500\nBEGIN\nEND\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.entries.empty());
        CHECK(r.debtor == L"1000");
        CHECK(r.cash == L"500");
    }

    TEST_CASE("a file with no DEBTOR/CASH lines at all still loads (they're "
              "optional; only BEGIN/END are required structural markers)") {
        std::wstring content = L"BEGIN\nA|B|1.0|2.0\nEND\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.debtor.empty());
        CHECK(r.cash.empty());
    }
}

TEST_SUITE("ParseFbdContent - v0.9.1 regression: whole-document rejection") {
    TEST_CASE("a totally unrelated text file is REJECTED, not silently accepted "
              "as an empty sheet") {
        // This is the core of the v0.9.1 fix (SecurityHardeningRegister.md
        // item #7): before it, opening any readable file "succeeded" and
        // the caller would immediately autosave the empty/garbled result
        // over a real recovery file.
        std::wstring content = L"Hello this is just some random text.\nNothing special here.\n";
        auto r = ParseFbdContent(content);
        CHECK_FALSE(r.ok);
        CHECK(r.entries.empty());
    }

    TEST_CASE("an empty file is REJECTED") {
        auto r = ParseFbdContent(L"");
        CHECK_FALSE(r.ok);
    }

    TEST_CASE("whitespace-only content is REJECTED") {
        auto r = ParseFbdContent(L"\n\n   \n\t\n");
        CHECK_FALSE(r.ok);
    }

    TEST_CASE("BEGIN with no matching END (a write cut off partway through, e.g. "
              "by a crash or a full disk) is REJECTED") {
        std::wstring content = L"DEBTOR=1\nCASH=2\nBEGIN\nA|B|1.0|2.0\n"; // no END
        auto r = ParseFbdContent(content);
        CHECK_FALSE(r.ok);
    }

    TEST_CASE("a document with only DEBTOR/CASH and no BEGIN/END at all is still "
              "accepted (DEBTOR=/CASH= alone are recognized markers) - only a "
              "BEGIN with no END is rejected, not the absence of BEGIN entirely") {
        std::wstring content = L"DEBTOR=100\nCASH=50\n";
        auto r = ParseFbdContent(content);
        CHECK(r.ok);
        CHECK(r.entries.empty());
    }
}

TEST_SUITE("ParseFbdContent - v0.9.1 regression: per-row numeric validation") {
    TEST_CASE("NaN, Infinity, and negative Kgs/Price rows are skipped and "
              "counted, not silently zeroed") {
        // Each row below is deliberately malformed in one field; the last
        // row is the only fully valid one. 3 pipes each = 4 fields (the
        // legacy format), so field-count is never the reason these are
        // rejected - only the numeric value is bad.
        std::wstring content =
            L"BEGIN\n"
            L"A|B|nan|2.0\n"
            L"A|B|1.0|inf\n"
            L"A|B|-1.0|2.0\n"
            L"A|B|1.0|-2.0\n"
            L"A|B|1.0|2.0\n" // the one good row
            L"END\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        REQUIRE(r.entries.size() == 1);
        CHECK(r.entries[0].kgs == 1.0);
        CHECK(r.entries[0].price == 2.0);
        CHECK(r.skippedLines == 4);
    }

    TEST_CASE("zero Kgs/Price is valid (not the same as negative)") {
        std::wstring content = L"BEGIN\nA|B|0|0\nEND\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        REQUIRE(r.entries.size() == 1);
        CHECK(r.skippedLines == 0);
    }
}

TEST_SUITE("ParseFbdContent - v0.9.1 regression: field validation parity with "
           "the interactive entry form") {
    TEST_CASE("a row with an empty Supplier or Species is skipped, not turned "
              "into a blank-named report group") {
        // Neither of these could come from the app's own entry form (that
        // path already rejects empty Supplier/Species) - only from a
        // hand-edited or corrupted file.
        std::wstring content = L"BEGIN\n|Species|1.0|2.0\nSupplier||1.0|2.0\nEND\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.entries.empty());
        CHECK(r.skippedLines == 2);
    }

    TEST_CASE("leading/trailing whitespace on Supplier/Species from a "
              "hand-edited file is trimmed, matching what the interactive "
              "entry form already does") {
        // README.md explicitly invites users to hand-edit .fbd files, so a
        // stray space from a text editor shouldn't create an invisible
        // duplicate supplier that fragments totals.
        std::wstring content = L"BEGIN\n Jcasement | Garfish |1.0|2.0\nEND\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        REQUIRE(r.entries.size() == 1);
        CHECK(r.entries[0].supplier == L"Jcasement");
        CHECK(r.entries[0].product == L"Garfish");
    }
}

TEST_SUITE("ParseFbdContent - malformed rows") {
    TEST_CASE("a row with the wrong field count (not 4, 6, or 7) is skipped "
              "and counted") {
        std::wstring content = L"BEGIN\nA|B|C\nEND\n"; // 2 pipes = 3 fields
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.entries.empty());
        CHECK(r.skippedLines == 1);
    }

    TEST_CASE("a 5-field row (between the two valid pre-flag/pre-outlier "
              "counts) is still rejected, not silently accepted as one or "
              "the other") {
        std::wstring content = L"BEGIN\nA|B|1.0|2.0|2026-08-05\nEND\n"; // 4 pipes = 5 fields
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.entries.empty());
        CHECK(r.skippedLines == 1);
    }

    TEST_CASE("good and bad rows in the same file: only the bad ones are "
              "skipped, the good ones still load") {
        std::wstring content =
            L"BEGIN\n"
            L"Good|Fish|1.0|2.0\n"
            L"A|B|C\n"                 // wrong field count
            L"AlsoGood|Fish|3.0|4.0\n"
            L"END\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        REQUIRE(r.entries.size() == 2);
        CHECK(r.skippedLines == 1);
        CHECK(r.entries[0].supplier == L"Good");
        CHECK(r.entries[1].supplier == L"AlsoGood");
    }
}

TEST_SUITE("ParseFbdContent - line ending handling") {
    TEST_CASE("CRLF line endings are handled the same as LF") {
        std::wstring content = L"BEGIN\r\nA|B|1.0|2.0\r\nEND\r\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        REQUIRE(r.entries.size() == 1);
    }

    TEST_CASE("a final line with no trailing newline is still read") {
        std::wstring content = L"BEGIN\nA|B|1.0|2.0\nEND"; // no trailing \n
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        REQUIRE(r.entries.size() == 1);
    }
}

TEST_SUITE("ParseFbdContent - draft entry form persistence") {
    TEST_CASE("all six draft fields parse correctly") {
        std::wstring content =
            L"DEBTOR=100\n"
            L"CASH=50\n"
            L"DRAFT_SUPPLIER=Jcasement\n"
            L"DRAFT_SPECIES=Garfish\n"
            L"DRAFT_KGS=13.8\n"
            L"DRAFT_PRICE=17\n"
            L"DRAFT_NOTES=call before delivery\n"
            L"DRAFT_DATE=2026-08-05\n"
            L"BEGIN\n"
            L"END\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.draftSupplier == L"Jcasement");
        CHECK(r.draftSpecies == L"Garfish");
        CHECK(r.draftKgs == L"13.8");
        CHECK(r.draftPrice == L"17");
        CHECK(r.draftNotes == L"call before delivery");
        CHECK(r.draftDate == L"2026-08-05");
    }

    TEST_CASE("a file with no draft lines at all (old files, or a file saved "
              "with an empty form) has empty draft fields - backward "
              "compatible with files from before this feature existed") {
        std::wstring content = L"DEBTOR=1\nCASH=2\nBEGIN\nA|B|1.0|2.0\nEND\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.draftSupplier.empty());
        CHECK(r.draftSpecies.empty());
        CHECK(r.draftKgs.empty());
        CHECK(r.draftPrice.empty());
        CHECK(r.draftNotes.empty());
        CHECK(r.draftDate.empty());
    }

    TEST_CASE("an invalid DRAFT_DATE is dropped rather than passed through, "
              "same validation ParseISODate already applies everywhere else") {
        std::wstring content = L"BEGIN\nEND\nDRAFT_DATE=not-a-date\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.draftDate.empty());
    }

    TEST_CASE("a valid DRAFT_DATE on its own (no BEGIN/END/DEBTOR/CASH) is "
              "still enough to recognize the document as a genuine .fbd "
              "file, not reject it as unrelated text") {
        std::wstring content = L"DRAFT_SUPPLIER=Test\n";
        auto r = ParseFbdContent(content);
        CHECK(r.ok);
        CHECK(r.draftSupplier == L"Test");
    }

    TEST_CASE("draft fields don't interfere with normal entry parsing") {
        std::wstring content =
            L"DRAFT_SUPPLIER=NotYetAdded\n"
            L"BEGIN\n"
            L"RealSupplier|RealSpecies|1.0|2.0\n"
            L"END\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        REQUIRE(r.entries.size() == 1);
        CHECK(r.entries[0].supplier == L"RealSupplier");
        CHECK(r.draftSupplier == L"NotYetAdded");
    }

    TEST_CASE("a valid FINALIZED line is parsed (Finalize Day, ROADMAP.md "
              "item 3) - marks this document as a locked history\\ snapshot") {
        std::wstring content = L"FINALIZED=2026-09-20\nBEGIN\nEND\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.finalizedDate == L"2026-09-20");
    }

    TEST_CASE("an ordinary (non-finalized) file has an empty finalizedDate") {
        std::wstring content = L"BEGIN\nEND\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.finalizedDate.empty());
    }

    TEST_CASE("an invalid FINALIZED value is dropped rather than passed "
              "through, same validation as DRAFT_DATE") {
        std::wstring content = L"BEGIN\nEND\nFINALIZED=not-a-date\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.finalizedDate.empty());
    }
}
