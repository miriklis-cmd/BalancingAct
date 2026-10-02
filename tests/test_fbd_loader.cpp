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
