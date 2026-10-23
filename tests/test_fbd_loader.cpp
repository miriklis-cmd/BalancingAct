#include "doctest_setup.h"
#include "doctest.h"
#include "../src/core/FishBalanceCore.h"

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

    TEST_CASE("Phase 1 completeness follow-up (v0.9.50): a Debtor/Cash-only "
              "document with NO BEGIN/END at all is now REJECTED - every "
              "save path (BuildFbdSaveContent) has always written both "
              "markers, even for a blank sheet, so this is no longer treated "
              "as a legitimate historical format") {
        std::wstring content = L"DEBTOR=100\nCASH=50\n";
        auto r = ParseFbdContent(content);
        CHECK_FALSE(r.ok);
        CHECK(r.errorCode == FbdErrorCode::MissingBegin);
        CHECK(r.entries.empty());
    }

    TEST_CASE("v0.9.50: a document with BEGIN but no END is rejected as "
              "MissingEnd specifically (distinct from MissingBegin)") {
        std::wstring content = L"DEBTOR=100\nCASH=50\nBEGIN\n";
        auto r = ParseFbdContent(content);
        CHECK_FALSE(r.ok);
        CHECK(r.errorCode == FbdErrorCode::MissingEnd);
    }

    TEST_CASE("v0.9.50: the legitimate empty sheet - DEBTOR=/CASH= followed "
              "by an empty BEGIN...END pair - still loads fine") {
        std::wstring content = L"DEBTOR=\nCASH=\nBEGIN\nEND\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.entries.empty());
        CHECK(r.debtor.empty());
        CHECK(r.cash.empty());
    }
}

TEST_SUITE("ParseFbdContent - v0.9.1 regression: per-row numeric validation") {
    TEST_CASE("FIXED (Phase 1 / F3+F2, was: skipped and counted): a NaN/Infinity/"
              "negative Kgs or Price row now rejects the WHOLE document") {
        // Previously each bad row just incremented skippedLines and the good
        // row still loaded. Reconciliation/loading must fail closed instead -
        // a document containing even one row it can't safely reconstruct is
        // rejected outright, not partially committed.
        CHECK_FALSE(ParseFbdContent(L"BEGIN\nA|B|nan|2.0\nEND\n").ok);
        CHECK_FALSE(ParseFbdContent(L"BEGIN\nA|B|1.0|inf\nEND\n").ok);
        CHECK_FALSE(ParseFbdContent(L"BEGIN\nA|B|-1.0|2.0\nEND\n").ok);
        CHECK_FALSE(ParseFbdContent(L"BEGIN\nA|B|1.0|-2.0\nEND\n").ok);
    }

    TEST_CASE("Phase 2 / F15 (2026-10-02): a row with Kgs or Price over the "
              "business maximum rejects the whole document, same as NaN/"
              "infinity/negative - the exact ceilings (confirmed with Jack, "
              "2026-10-02) are kMaxWeightKg=10,000kg and "
              "kMaxPriceDollars=$250/kg") {
        CHECK_FALSE(ParseFbdContent(L"BEGIN\nA|B|10000.01|2.0\nEND\n").ok);
        CHECK_FALSE(ParseFbdContent(L"BEGIN\nA|B|1.0|250.01\nEND\n").ok);
        // Exactly at the ceiling is still valid.
        CHECK(ParseFbdContent(L"BEGIN\nA|B|10000|2.0\nEND\n").ok);
        CHECK(ParseFbdContent(L"BEGIN\nA|B|1.0|250\nEND\n").ok);
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
    TEST_CASE("FIXED (Phase 1 / F3+F2, was: skipped and counted): a row with an "
              "empty Supplier or Species now rejects the WHOLE document") {
        // Neither of these could come from the app's own entry form (that
        // path already rejects empty Supplier/Species) - only from a
        // hand-edited or corrupted file.
        CHECK_FALSE(ParseFbdContent(L"BEGIN\n|Species|1.0|2.0\nEND\n").ok);
        CHECK_FALSE(ParseFbdContent(L"BEGIN\nSupplier||1.0|2.0\nEND\n").ok);
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

TEST_SUITE("ParseFbdContent - malformed rows (Phase 1 / F3+F2: transactional, "
           "no more partial documents)") {
    TEST_CASE("a row with the wrong field count (not 4, 6, or 7) rejects the "
              "WHOLE document") {
        std::wstring content = L"BEGIN\nA|B|C\nEND\n"; // 2 pipes = 3 fields
        CHECK_FALSE(ParseFbdContent(content).ok);
    }

    TEST_CASE("a 5-field row (between the two valid pre-flag/pre-outlier "
              "counts) rejects the WHOLE document, not silently accepted as "
              "one format or the other") {
        std::wstring content = L"BEGIN\nA|B|1.0|2.0|2026-08-05\nEND\n"; // 4 pipes = 5 fields
        CHECK_FALSE(ParseFbdContent(content).ok);
    }

    TEST_CASE("FIXED (was: only the bad row skipped, good rows still loaded): "
              "one bad row among good ones now rejects the WHOLE document") {
        // This is the core of the F3+F2 remediation: silently committing a
        // sheet that's missing rows the user never asked to drop is exactly
        // the "partial document looks fine" failure mode Finalize Day and
        // reconciliation depend on not happening.
        std::wstring content =
            L"BEGIN\n"
            L"Good|Fish|1.0|2.0\n"
            L"A|B|C\n"                 // wrong field count
            L"AlsoGood|Fish|3.0|4.0\n"
            L"END\n";
        auto r = ParseFbdContent(content);
        CHECK_FALSE(r.ok);
        CHECK(r.entries.empty()); // nothing partially committed on rejection
    }

    TEST_CASE("an otherwise-good file with every row valid still loads fine "
              "(sanity check that strictness didn't break the normal case)") {
        std::wstring content =
            L"BEGIN\n"
            L"Good|Fish|1.0|2.0\n"
            L"AlsoGood|Fish|3.0|4.0\n"
            L"END\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        REQUIRE(r.entries.size() == 2);
    }
}

TEST_SUITE("ParseFbdContent - Phase 1 / F3+F2: structural strictness") {
    TEST_CASE("a second BEGIN block is REJECTED (multiple BEGIN/END pairs "
              "aren't a supported format)") {
        std::wstring content = L"BEGIN\nA|B|1.0|2.0\nEND\nBEGIN\nC|D|3.0|4.0\nEND\n";
        CHECK_FALSE(ParseFbdContent(content).ok);
    }

    TEST_CASE("a second END with no intervening BEGIN is REJECTED") {
        std::wstring content = L"BEGIN\nA|B|1.0|2.0\nEND\nEND\n";
        CHECK_FALSE(ParseFbdContent(content).ok);
    }

    TEST_CASE("an END with no BEGIN at all is REJECTED") {
        std::wstring content = L"DEBTOR=1\nCASH=2\nEND\n";
        CHECK_FALSE(ParseFbdContent(content).ok);
    }

    TEST_CASE("a duplicate DEBTOR= line is REJECTED rather than letting the "
              "last one silently win") {
        std::wstring content = L"DEBTOR=100\nDEBTOR=200\nBEGIN\nEND\n";
        CHECK_FALSE(ParseFbdContent(content).ok);
    }

    TEST_CASE("a duplicate CASH= line is REJECTED rather than letting the "
              "last one silently win") {
        std::wstring content = L"CASH=50\nCASH=75\nBEGIN\nEND\n";
        CHECK_FALSE(ParseFbdContent(content).ok);
    }

    TEST_CASE("a single well-formed BEGIN/END pair with single DEBTOR/CASH "
              "lines still loads fine (sanity check)") {
        std::wstring content = L"DEBTOR=100\nCASH=50\nBEGIN\nA|B|1.0|2.0\nEND\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.debtor == L"100");
        CHECK(r.cash == L"50");
        REQUIRE(r.entries.size() == 1);
    }

    // Phase 1 / F2 follow-up (post-review, 2026-09-30): these three cases
    // were the specific gap found during the completeness review - a
    // non-empty line that isn't a recognized KEY= marker, isn't BEGIN/END,
    // and sits outside the data section used to silently match no branch
    // and be ignored, instead of being treated the same as any other
    // structurally-ambiguous/hand-edited content.
    TEST_CASE("an unrecognized non-empty line BEFORE BEGIN is REJECTED "
              "(previously silently ignored)") {
        std::wstring content = L"stray garbage line\nBEGIN\nA|B|1.0|2.0\nEND\n";
        CHECK_FALSE(ParseFbdContent(content).ok);
    }

    TEST_CASE("an unrecognized non-empty line AFTER END is REJECTED "
              "(previously silently ignored)") {
        std::wstring content = L"BEGIN\nA|B|1.0|2.0\nEND\nstray garbage line\n";
        CHECK_FALSE(ParseFbdContent(content).ok);
    }

    TEST_CASE("a genuinely blank line before BEGIN or after END is still "
              "harmless (only non-empty stray content is rejected)") {
        std::wstring content = L"\nDEBTOR=1\nCASH=2\nBEGIN\nA|B|1.0|2.0\nEND\n\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        REQUIRE(r.entries.size() == 1);
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

    TEST_CASE("v0.9.50: a DRAFT_* marker on its own is enough to recognize "
              "the document as a genuine .fbd file rather than unrelated "
              "text, but it still needs its own BEGIN/END pair like any "
              "other document - draft-only content no longer exempts a "
              "file from the structural requirement") {
        std::wstring content = L"DRAFT_SUPPLIER=Test\nBEGIN\nEND\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.draftSupplier == L"Test");
    }

    TEST_CASE("v0.9.50: a DRAFT_* marker on its own with NO BEGIN/END is "
              "rejected - recognized-but-structurally-incomplete, same as "
              "the Debtor/Cash-only case above") {
        std::wstring content = L"DRAFT_SUPPLIER=Test\n";
        auto r = ParseFbdContent(content);
        CHECK_FALSE(r.ok);
        CHECK(r.errorCode == FbdErrorCode::MissingBegin);
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

TEST_SUITE("ParseFbdContent - Phase 1 completeness follow-up (v0.9.50): "
           "bounded record/line/field validation") {
    // Per Jack's instruction: construct large fixtures programmatically
    // rather than as enormous in-source string literals.

    TEST_CASE("Supplier exactly at the 255-character limit is accepted; one "
              "character over is rejected as FieldTooLong") {
        std::wstring atLimit(kMaxSupplierSpeciesLength, L'A');
        std::wstring overLimit(kMaxSupplierSpeciesLength + 1, L'A');

        std::wstring okContent = L"BEGIN\n" + atLimit + L"|Species|1.0|2.0\nEND\n";
        auto rOk = ParseFbdContent(okContent);
        REQUIRE(rOk.ok);
        REQUIRE(rOk.entries.size() == 1);
        CHECK(rOk.entries[0].supplier.size() == kMaxSupplierSpeciesLength);

        std::wstring overContent = L"BEGIN\n" + overLimit + L"|Species|1.0|2.0\nEND\n";
        auto rOver = ParseFbdContent(overContent);
        CHECK_FALSE(rOver.ok);
        CHECK(rOver.errorCode == FbdErrorCode::FieldTooLong);
    }

    TEST_CASE("Species exactly at the 255-character limit is accepted; one "
              "character over is rejected as FieldTooLong") {
        std::wstring atLimit(kMaxSupplierSpeciesLength, L'B');
        std::wstring overLimit(kMaxSupplierSpeciesLength + 1, L'B');

        std::wstring okContent = L"BEGIN\nSupplier|" + atLimit + L"|1.0|2.0\nEND\n";
        auto rOk = ParseFbdContent(okContent);
        REQUIRE(rOk.ok);
        REQUIRE(rOk.entries.size() == 1);

        std::wstring overContent = L"BEGIN\nSupplier|" + overLimit + L"|1.0|2.0\nEND\n";
        auto rOver = ParseFbdContent(overContent);
        CHECK_FALSE(rOver.ok);
        CHECK(rOver.errorCode == FbdErrorCode::FieldTooLong);
    }

    TEST_CASE("row Notes exactly at the 4096-character limit is accepted; "
              "one character over is rejected as FieldTooLong") {
        std::wstring atLimit(kMaxNotesLength, L'n');
        std::wstring overLimit(kMaxNotesLength + 1, L'n');

        std::wstring okContent = L"BEGIN\nSupplier|Species|1.0|2.0|2026-08-05|" + atLimit + L"\nEND\n";
        auto rOk = ParseFbdContent(okContent);
        REQUIRE(rOk.ok);
        REQUIRE(rOk.entries.size() == 1);
        CHECK(rOk.entries[0].notes.size() == kMaxNotesLength);

        std::wstring overContent = L"BEGIN\nSupplier|Species|1.0|2.0|2026-08-05|" + overLimit + L"\nEND\n";
        auto rOver = ParseFbdContent(overContent);
        CHECK_FALSE(rOver.ok);
        CHECK(rOver.errorCode == FbdErrorCode::FieldTooLong);
    }

    TEST_CASE("DEBTOR= value exactly at the 4096-character limit is "
              "accepted; one character over is rejected as FieldTooLong") {
        std::wstring atLimit(kMaxDebtorCashExprLength, L'1');
        std::wstring overLimit(kMaxDebtorCashExprLength + 1, L'1');

        auto rOk = ParseFbdContent(L"DEBTOR=" + atLimit + L"\nBEGIN\nEND\n");
        REQUIRE(rOk.ok);
        CHECK(rOk.debtor.size() == kMaxDebtorCashExprLength);

        auto rOver = ParseFbdContent(L"DEBTOR=" + overLimit + L"\nBEGIN\nEND\n");
        CHECK_FALSE(rOver.ok);
        CHECK(rOver.errorCode == FbdErrorCode::FieldTooLong);
    }

    TEST_CASE("CASH= value exactly at the 4096-character limit is accepted; "
              "one character over is rejected as FieldTooLong") {
        std::wstring atLimit(kMaxDebtorCashExprLength, L'2');
        std::wstring overLimit(kMaxDebtorCashExprLength + 1, L'2');

        auto rOk = ParseFbdContent(L"CASH=" + atLimit + L"\nBEGIN\nEND\n");
        REQUIRE(rOk.ok);
        CHECK(rOk.cash.size() == kMaxDebtorCashExprLength);

        auto rOver = ParseFbdContent(L"CASH=" + overLimit + L"\nBEGIN\nEND\n");
        CHECK_FALSE(rOver.ok);
        CHECK(rOver.errorCode == FbdErrorCode::FieldTooLong);
    }

    TEST_CASE("DRAFT_SUPPLIER=/DRAFT_SPECIES= exactly at their 255-character "
              "limit are accepted; one character over is rejected as "
              "FieldTooLong") {
        std::wstring atLimit(kMaxSupplierSpeciesLength, L'C');
        std::wstring overLimit(kMaxSupplierSpeciesLength + 1, L'C');

        auto rOk = ParseFbdContent(L"DRAFT_SUPPLIER=" + atLimit + L"\nDRAFT_SPECIES=" + atLimit + L"\nBEGIN\nEND\n");
        REQUIRE(rOk.ok);
        CHECK(rOk.draftSupplier.size() == kMaxSupplierSpeciesLength);
        CHECK(rOk.draftSpecies.size() == kMaxSupplierSpeciesLength);

        auto rOverSup = ParseFbdContent(L"DRAFT_SUPPLIER=" + overLimit + L"\nBEGIN\nEND\n");
        CHECK_FALSE(rOverSup.ok);
        CHECK(rOverSup.errorCode == FbdErrorCode::FieldTooLong);

        auto rOverSpec = ParseFbdContent(L"DRAFT_SPECIES=" + overLimit + L"\nBEGIN\nEND\n");
        CHECK_FALSE(rOverSpec.ok);
        CHECK(rOverSpec.errorCode == FbdErrorCode::FieldTooLong);
    }

    TEST_CASE("DRAFT_NOTES= exactly at the 4096-character limit is "
              "accepted; one character over is rejected as FieldTooLong") {
        std::wstring atLimit(kMaxNotesLength, L'd');
        std::wstring overLimit(kMaxNotesLength + 1, L'd');

        auto rOk = ParseFbdContent(L"DRAFT_NOTES=" + atLimit + L"\nBEGIN\nEND\n");
        REQUIRE(rOk.ok);
        CHECK(rOk.draftNotes.size() == kMaxNotesLength);

        auto rOver = ParseFbdContent(L"DRAFT_NOTES=" + overLimit + L"\nBEGIN\nEND\n");
        CHECK_FALSE(rOver.ok);
        CHECK(rOver.errorCode == FbdErrorCode::FieldTooLong);
    }

    TEST_CASE("DRAFT_KGS=/DRAFT_PRICE= exactly at their 256-character limit "
              "are accepted; one character over is rejected as "
              "FieldTooLong") {
        std::wstring atLimit(kMaxDraftKgsPriceTextLength, L'9');
        std::wstring overLimit(kMaxDraftKgsPriceTextLength + 1, L'9');

        auto rOk = ParseFbdContent(L"DRAFT_KGS=" + atLimit + L"\nDRAFT_PRICE=" + atLimit + L"\nBEGIN\nEND\n");
        REQUIRE(rOk.ok);
        CHECK(rOk.draftKgs.size() == kMaxDraftKgsPriceTextLength);
        CHECK(rOk.draftPrice.size() == kMaxDraftKgsPriceTextLength);

        auto rOverKgs = ParseFbdContent(L"DRAFT_KGS=" + overLimit + L"\nBEGIN\nEND\n");
        CHECK_FALSE(rOverKgs.ok);
        CHECK(rOverKgs.errorCode == FbdErrorCode::FieldTooLong);

        auto rOverPrice = ParseFbdContent(L"DRAFT_PRICE=" + overLimit + L"\nBEGIN\nEND\n");
        CHECK_FALSE(rOverPrice.ok);
        CHECK(rOverPrice.errorCode == FbdErrorCode::FieldTooLong);
    }

    TEST_CASE("SOURCE_FILE= exactly at the 32767-character limit is "
              "accepted; one character over is rejected as FieldTooLong") {
        std::wstring atLimit(kMaxSourceFileLength, L'x');
        std::wstring overLimit(kMaxSourceFileLength + 1, L'x');

        auto rOk = ParseFbdContent(L"SOURCE_FILE=" + atLimit + L"\nBEGIN\nEND\n");
        REQUIRE(rOk.ok);
        CHECK(rOk.hasSourceFile);
        CHECK(rOk.sourceFile.size() == kMaxSourceFileLength);

        auto rOver = ParseFbdContent(L"SOURCE_FILE=" + overLimit + L"\nBEGIN\nEND\n");
        CHECK_FALSE(rOver.ok);
        CHECK(rOver.errorCode == FbdErrorCode::FieldTooLong);
    }

    TEST_CASE("a decoded line exactly at the 65536-character limit passes "
              "the line-length gate (it is rejected for a DIFFERENT, more "
              "specific reason - unrecognized content outside the data "
              "section - proving LineTooLong itself didn't fire early); one "
              "character over is rejected specifically as LineTooLong") {
        std::wstring atLimit(kMaxLineLength, L'z');
        std::wstring overLimit(kMaxLineLength + 1, L'z');

        auto rAt = ParseFbdContent(atLimit + L"\nBEGIN\nEND\n");
        CHECK_FALSE(rAt.ok);
        CHECK(rAt.errorCode == FbdErrorCode::UnexpectedContentOutsideDataSection);

        auto rOver = ParseFbdContent(overLimit + L"\nBEGIN\nEND\n");
        CHECK_FALSE(rOver.ok);
        CHECK(rOver.errorCode == FbdErrorCode::LineTooLong);
    }

    TEST_CASE("exactly kMaxEntryRecords (100,000) valid rows load "
              "successfully; one more row over the limit is rejected as "
              "RecordLimitExceeded") {
        std::wstring atLimitContent = L"BEGIN\n";
        for (size_t i = 0; i < kMaxEntryRecords; i++) {
            atLimitContent += L"Supplier|Species|1.0|2.0\n";
        }
        atLimitContent += L"END\n";
        auto rAt = ParseFbdContent(atLimitContent);
        REQUIRE(rAt.ok);
        CHECK(rAt.entries.size() == kMaxEntryRecords);

        std::wstring overLimitContent = L"BEGIN\n";
        for (size_t i = 0; i < kMaxEntryRecords + 1; i++) {
            overLimitContent += L"Supplier|Species|1.0|2.0\n";
        }
        overLimitContent += L"END\n";
        auto rOver = ParseFbdContent(overLimitContent);
        CHECK_FALSE(rOver.ok);
        CHECK(rOver.errorCode == FbdErrorCode::RecordLimitExceeded);
    }
}

TEST_SUITE("ParseFbdContent - Phase 1 / F1: SOURCE_FILE identity marker") {
    TEST_CASE("a SOURCE_FILE= line is parsed and hasSourceFile is set") {
        std::wstring content = L"SOURCE_FILE=C:\\Data\\March.fbd\nBEGIN\nEND\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.hasSourceFile);
        CHECK(r.sourceFile == L"C:\\Data\\March.fbd");
    }

    TEST_CASE("an empty SOURCE_FILE= (an unsaved sheet at the time it was "
              "written) is still a present marker, not a missing one") {
        std::wstring content = L"SOURCE_FILE=\nBEGIN\nEND\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.hasSourceFile);
        CHECK(r.sourceFile.empty());
    }

    TEST_CASE("a document with no SOURCE_FILE= line at all (any save from "
              "before this field existed) leaves hasSourceFile false - "
              "callers must not treat missing metadata as an empty match") {
        std::wstring content = L"DEBTOR=1\nCASH=2\nBEGIN\nEND\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK_FALSE(r.hasSourceFile);
        CHECK(r.sourceFile.empty());
    }

    TEST_CASE("SOURCE_FILE doesn't interfere with normal entry/Debtor/Cash "
              "parsing") {
        std::wstring content =
            L"DEBTOR=100\n"
            L"CASH=50\n"
            L"SOURCE_FILE=C:\\Data\\March.fbd\n"
            L"BEGIN\n"
            L"A|B|1.0|2.0\n"
            L"END\n";
        auto r = ParseFbdContent(content);
        REQUIRE(r.ok);
        CHECK(r.debtor == L"100");
        CHECK(r.cash == L"50");
        REQUIRE(r.entries.size() == 1);
        CHECK(r.hasSourceFile);
        CHECK(r.sourceFile == L"C:\\Data\\March.fbd");
    }
}

// v0.10.12 (main.cpp split stage 7a): BuildFbdText() is the pure builder behind every
// save (main.cpp only gathers the state from the controls). These pin the exact file
// format and prove the builder and ParseFbdContent agree on it.
TEST_SUITE("BuildFbdText - building .fbd content") {
    static FbdSaveState SampleState() {
        FbdSaveState s;
        s.debtor = L"1000+250.5";
        s.cash = L"75";
        s.draftSupplier = L"Jenkins";
        s.draftSpecies = L"Flathead";
        s.draftKgs = L"12";
        s.draftPrice = L"9.5";
        s.draftNotes = L"half done";
        s.hasDraftDate = true;
        s.draftDate = L"2026-10-14";
        Entry a; a.supplier = L"Samantha J"; a.product = L"Rock flat"; a.kgs = 1011.0; a.price = 20.0;
        a.date = L"2026-10-13"; a.notes = L"big one"; a.priceFlagged = true;
        Entry b; b.supplier = L"Jenkins"; b.product = L"Flathead"; b.kgs = 12.5; b.price = 3.25;
        b.date = L"2026-10-14";
        s.entries = { a, b };
        return s;
    }

    TEST_CASE("exact output for a typical working file (no SOURCE_FILE, not finalized)") {
        CHECK(BuildFbdText(SampleState()) ==
            L"DEBTOR=1000+250.5\n"
            L"CASH=75\n"
            L"DRAFT_SUPPLIER=Jenkins\n"
            L"DRAFT_SPECIES=Flathead\n"
            L"DRAFT_KGS=12\n"
            L"DRAFT_PRICE=9.5\n"
            L"DRAFT_NOTES=half done\n"
            L"DRAFT_DATE=2026-10-14\n"
            L"BEGIN\n"
            L"Samantha J|Rock flat|1011.0000|20.0000|2026-10-13|big one|1\n"
            L"Jenkins|Flathead|12.5000|3.2500|2026-10-14||0\n"
            L"END\n");
    }

    TEST_CASE("SOURCE_FILE= is written only when includeSourceFile is set, right after CASH=") {
        FbdSaveState s = SampleState();
        s.sourceFile = L"C:\\Data\\Day.fbd";
        CHECK(BuildFbdText(s).find(L"SOURCE_FILE=") == std::wstring::npos);
        s.includeSourceFile = true;
        std::wstring text = BuildFbdText(s);
        CHECK(text.find(L"CASH=75\nSOURCE_FILE=C:\\Data\\Day.fbd\nDRAFT_SUPPLIER=") != std::wstring::npos);
        s.sourceFile = L"";   // an unsaved sheet: marker present but empty
        CHECK(BuildFbdText(s).find(L"SOURCE_FILE=\n") != std::wstring::npos);
    }

    TEST_CASE("FINALIZED= is written only for a finalized day, after SOURCE_FILE=/CASH=") {
        FbdSaveState s = SampleState();
        CHECK(BuildFbdText(s).find(L"FINALIZED=") == std::wstring::npos);
        s.finalizedDate = L"2026-10-14";
        CHECK(BuildFbdText(s).find(L"CASH=75\nFINALIZED=2026-10-14\nDRAFT_SUPPLIER=") != std::wstring::npos);
    }

    TEST_CASE("no DRAFT_DATE= line when there is no date picker") {
        FbdSaveState s = SampleState();
        s.hasDraftDate = false;
        CHECK(BuildFbdText(s).find(L"DRAFT_DATE=") == std::wstring::npos);
    }

    TEST_CASE("an empty sheet still writes DEBTOR/CASH/DRAFT/BEGIN/END and loads back") {
        FbdSaveState s;
        std::wstring text = BuildFbdText(s);
        CHECK(text ==
            L"DEBTOR=\nCASH=\nDRAFT_SUPPLIER=\nDRAFT_SPECIES=\nDRAFT_KGS=\nDRAFT_PRICE=\nDRAFT_NOTES=\nBEGIN\nEND\n");
        auto r = ParseFbdContent(text);
        REQUIRE(r.ok);
        CHECK(r.entries.empty());
    }

    TEST_CASE("round trip: everything BuildFbdText writes, ParseFbdContent reads back") {
        FbdSaveState s = SampleState();
        s.includeSourceFile = true;
        s.sourceFile = L"C:\\Data\\Day.fbd";
        s.finalizedDate = L"2026-10-14";
        auto r = ParseFbdContent(BuildFbdText(s));
        REQUIRE(r.ok);
        CHECK(r.debtor == s.debtor);
        CHECK(r.cash == s.cash);
        CHECK(r.hasSourceFile);
        CHECK(r.sourceFile == s.sourceFile);
        CHECK(r.finalizedDate == s.finalizedDate);
        CHECK(r.draftSupplier == s.draftSupplier);
        CHECK(r.draftSpecies == s.draftSpecies);
        CHECK(r.draftKgs == s.draftKgs);
        CHECK(r.draftPrice == s.draftPrice);
        CHECK(r.draftNotes == s.draftNotes);
        CHECK(r.draftDate == s.draftDate);
        REQUIRE(r.entries.size() == 2);
        for (size_t i = 0; i < 2; i++) {
            CHECK(r.entries[i].supplier == s.entries[i].supplier);
            CHECK(r.entries[i].product == s.entries[i].product);
            CHECK(r.entries[i].kgs == doctest::Approx(s.entries[i].kgs));
            CHECK(r.entries[i].price == doctest::Approx(s.entries[i].price));
            CHECK(r.entries[i].date == s.entries[i].date);
            CHECK(r.entries[i].notes == s.entries[i].notes);
            CHECK(r.entries[i].priceFlagged == s.entries[i].priceFlagged);
        }
    }
}

// v0.11.0: Kgs typed as a sum ("24+24+24") keeps the text as typed in an optional 8th field, while
// Entry::kgs always holds the evaluated total. Plain-number rows stay 7 fields so old builds can read them.
TEST_SUITE("Kgs expression (v0.11.0)") {
    TEST_CASE("KgsExprToStore keeps real sums and drops plain numbers") {
        CHECK(KgsExprToStore(L"24+24+24") == L"24+24+24");
        CHECK(KgsExprToStore(L"  10 + 10.5 ") == L"10 + 10.5");
        CHECK(KgsExprToStore(L"100-5") == L"100-5");
        CHECK(KgsExprToStore(L"-5+10") == L"-5+10");
        CHECK(KgsExprToStore(L"72") == L"");
        CHECK(KgsExprToStore(L" 72.5 ") == L"");
        CHECK(KgsExprToStore(L"-5") == L""); // a leading sign alone is not a sum
        CHECK(KgsExprToStore(L"") == L"");
        CHECK(KgsExprToStore(std::wstring(kMaxKgsExprLength + 1, L'1') + L"+1") == L"");
    }

    TEST_CASE("KgsBoxText shows the expression when there is one, else the formatted total") {
        Entry e; e.kgs = 72.0;
        CHECK(KgsBoxText(e, L"72.0") == L"72.0");
        e.kgsExpr = L"24+24+24";
        CHECK(KgsBoxText(e, L"72.0") == L"24+24+24");
    }

    TEST_CASE("a sum round-trips through BuildFbdText and ParseFbdContent") {
        FbdSaveState s;
        Entry a; a.supplier = L"A"; a.product = L"Mussels"; a.kgs = 72.0; a.price = 4.4;
        a.date = L"2026-10-15"; a.kgsExpr = L"24+24+24";
        Entry b; b.supplier = L"B"; b.product = L"Salmon"; b.kgs = 7.8; b.price = 3.5; b.date = L"2026-10-15";
        s.entries = { a, b };
        std::wstring text = BuildFbdText(s);
        CHECK(text.find(L"A|Mussels|72.0000|4.4000|2026-10-15||0|24+24+24\n") != std::wstring::npos);
        CHECK(text.find(L"B|Salmon|7.8000|3.5000|2026-10-15||0\n") != std::wstring::npos); // still 7 fields
        auto r = ParseFbdContent(text);
        REQUIRE(r.ok);
        REQUIRE(r.entries.size() == 2);
        CHECK(r.entries[0].kgs == doctest::Approx(72.0));
        CHECK(r.entries[0].kgsExpr == L"24+24+24");
        CHECK(r.entries[1].kgsExpr == L"");
    }

    TEST_CASE("an expression that no longer matches the stored total is dropped, the file still loads") {
        auto r = ParseFbdContent(L"BEGIN\nA|B|72.0000|2.0000|2026-10-15||0|24+24+25\nEND\n");
        REQUIRE(r.ok);
        REQUIRE(r.entries.size() == 1);
        CHECK(r.entries[0].kgs == doctest::Approx(72.0));
        CHECK(r.entries[0].kgsExpr == L"");
    }

    TEST_CASE("an unparseable or oversized expression is dropped, the file still loads") {
        auto r = ParseFbdContent(L"BEGIN\nA|B|72.0000|2.0000|2026-10-15||0|abc\nEND\n");
        REQUIRE(r.ok);
        CHECK(r.entries[0].kgsExpr == L"");
        std::wstring longExpr = L"1";
        while (longExpr.size() <= kMaxKgsExprLength) longExpr += L"+0";
        auto r2 = ParseFbdContent(L"BEGIN\nA|B|1.0000|2.0000|2026-10-15||0|" + longExpr + L"\nEND\n");
        REQUIRE(r2.ok);
        CHECK(r2.entries[0].kgsExpr == L"");
    }

    TEST_CASE("a plain-number 8th field is dropped (nothing to remember)") {
        auto r = ParseFbdContent(L"BEGIN\nA|B|72.0000|2.0000|2026-10-15||0|72\nEND\n");
        REQUIRE(r.ok);
        CHECK(r.entries[0].kgsExpr == L"");
    }

    TEST_CASE("a 9-field row is still rejected as malformed") {
        auto r = ParseFbdContent(L"BEGIN\nA|B|72.0000|2.0000|2026-10-15||0|24+24+24|x\nEND\n");
        CHECK_FALSE(r.ok);
    }
}
