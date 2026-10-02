// Automates three areas Jack was previously re-verifying by hand after
// every change: autosave identity/recovery, Finalize Day persistence, and
// Save As failure handling (test-automation refactor, 2026-09-30, v0.9.51).
// Every function under test here is a pure decision extracted out of
// main.cpp - see FishBalanceCore.h's "Test-automation refactor" section for
// the full rationale. Nothing here touches a real file or window; fake
// inputs and fake injected persistence are all these tests use, which is
// exactly what makes them fast, deterministic, and runnable in the existing
// portable suite (no Windows integration environment needed for any of
// this - these are pure C++ decisions).
#include "doctest_setup.h"
#include "doctest.h"
#include "../FishBalanceCore.h"

TEST_SUITE("CaseInsensitiveEqualsW") {
    TEST_CASE("identical strings match") {
        CHECK(CaseInsensitiveEqualsW(L"C:\\Data\\March.fbd", L"C:\\Data\\March.fbd"));
    }
    TEST_CASE("differing only in case still matches") {
        CHECK(CaseInsensitiveEqualsW(L"C:\\Data\\March.fbd", L"c:\\data\\MARCH.FBD"));
    }
    TEST_CASE("different strings of the same length do not match") {
        CHECK_FALSE(CaseInsensitiveEqualsW(L"C:\\Data\\March.fbd", L"C:\\Data\\April.fbd"));
    }
    TEST_CASE("different lengths never match") {
        CHECK_FALSE(CaseInsensitiveEqualsW(L"C:\\Data\\March.fbd", L"C:\\Data\\March2.fbd"));
    }
    TEST_CASE("two empty strings match") {
        CHECK(CaseInsensitiveEqualsW(L"", L""));
    }
}

TEST_SUITE("DecideAutosaveRecovery - autosave identity/recovery (Phase 1 / F1)") {
    TEST_CASE("matched identity: recovered SOURCE_FILE= agrees with LASTFILE - "
              "re-associate with the named file") {
        auto d = DecideAutosaveRecovery(/*autosaveLoadFailed=*/false, /*hasSourceFile=*/true,
            /*sourceFile=*/L"C:\\Data\\March.fbd", /*lastFile=*/L"C:\\Data\\March.fbd");
        CHECK(d.outcome == AutosaveRecoveryOutcome::MatchedNamedFile);
        CHECK(d.resultingCurrentFile == L"C:\\Data\\March.fbd");
    }

    TEST_CASE("matched identity is case-insensitive, matching Windows path "
              "semantics (_wcsicmp)") {
        auto d = DecideAutosaveRecovery(false, true, L"c:\\data\\march.fbd", L"C:\\DATA\\MARCH.FBD");
        CHECK(d.outcome == AutosaveRecoveryOutcome::MatchedNamedFile);
    }

    TEST_CASE("mismatch: recovered SOURCE_FILE= names a different file than "
              "LASTFILE - stay unsaved, never silently re-link") {
        auto d = DecideAutosaveRecovery(false, true, L"C:\\Data\\April.fbd", L"C:\\Data\\March.fbd");
        CHECK(d.outcome == AutosaveRecoveryOutcome::UnsavedMismatch);
        CHECK(d.resultingCurrentFile.empty());
    }

    TEST_CASE("changed file: the user had a different file open at the time "
              "autosave.fbd was last written than settings.txt's LASTFILE "
              "remembers - same as any other mismatch, resultingCurrentFile "
              "stays empty so a later Save can't silently overwrite either "
              "file") {
        auto d = DecideAutosaveRecovery(false, true, L"C:\\Data\\NewSheet.fbd", L"C:\\Data\\OldSheet.fbd");
        CHECK(d.outcome == AutosaveRecoveryOutcome::UnsavedMismatch);
        CHECK(d.resultingCurrentFile.empty());
    }

    TEST_CASE("missing named file: LASTFILE is empty (first run, or a clean "
              "exit with no named file open) - no association attempted") {
        auto d = DecideAutosaveRecovery(false, true, L"", L"");
        CHECK(d.outcome == AutosaveRecoveryOutcome::NamedFileNotRemembered);
        CHECK(d.resultingCurrentFile.empty());
    }

    TEST_CASE("legacy autosave: recovered content has no SOURCE_FILE= marker "
              "at all (predates the field) - treated as unsaved, never "
              "guessed at") {
        auto d = DecideAutosaveRecovery(false, false, L"", L"C:\\Data\\March.fbd");
        CHECK(d.outcome == AutosaveRecoveryOutcome::UnsavedLegacyNoMarker);
        CHECK(d.resultingCurrentFile.empty());
    }

    TEST_CASE("corrupt autosave: the load itself failed - rejected outright, "
              "never reaches the identity comparison at all") {
        // Deliberately passing values that WOULD match if compared, to prove
        // autosaveLoadFailed short-circuits before any identity check runs.
        auto d = DecideAutosaveRecovery(true, true, L"C:\\Data\\March.fbd", L"C:\\Data\\March.fbd");
        CHECK(d.outcome == AutosaveRecoveryOutcome::Rejected);
        CHECK(d.resultingCurrentFile.empty());
    }

    TEST_CASE("stale settings / a since-deleted named file: matching purely "
              "on the recorded identity marker, NOT on whether that file "
              "still exists on disk - this reproduces the app's current "
              "behavior exactly (existence is not currently a gating "
              "factor), documented here rather than silently changed") {
        // No "does this file exist" input exists because the real app
        // doesn't check it either - a match re-associates regardless, and a
        // subsequent Save simply recreates the file if it's gone missing.
        auto d = DecideAutosaveRecovery(false, true, L"C:\\Data\\Deleted.fbd", L"C:\\Data\\Deleted.fbd");
        CHECK(d.outcome == AutosaveRecoveryOutcome::MatchedNamedFile);
        CHECK(d.resultingCurrentFile == L"C:\\Data\\Deleted.fbd");
    }

    TEST_CASE("ordinary Save cannot target an unrelated file: every outcome "
              "except MatchedNamedFile leaves resultingCurrentFile empty, "
              "which is what routes an ordinary Save to Save As (a fresh "
              "file) instead of overwriting whatever LASTFILE used to name") {
        CHECK(DecideAutosaveRecovery(true, true, L"X", L"X").resultingCurrentFile.empty());
        CHECK(DecideAutosaveRecovery(false, true, L"", L"").resultingCurrentFile.empty());
        CHECK(DecideAutosaveRecovery(false, true, L"A", L"B").resultingCurrentFile.empty());
        CHECK(DecideAutosaveRecovery(false, false, L"", L"B").resultingCurrentFile.empty());
    }
}

TEST_SUITE("CoordinateFinalizeDay - Finalize Day persistence (Phase 1 / F9)") {
    // Every combination of {history succeeds/fails} x {no named file / named
    // file sync succeeds/fails}, per Jack's instruction to test every
    // success/failure combination.

    TEST_CASE("no named file open, history write succeeds -> clean finalize") {
        bool backupCalled = false;
        FinalizePersistencePorts ports;
        ports.writeBackupSnapshot = [&]() { backupCalled = true; };
        ports.writeHistoryRecord = [](std::wstring*) { return true; };
        ports.hasNamedFile = false;
        auto out = CoordinateFinalizeDay(ports, L"2026-09-30");
        CHECK(backupCalled);
        CHECK(out.kind == FinalizeResultKind::FinalizedClean);
        CHECK(out.finalized);
        CHECK_FALSE(out.dirtyAfter);
        CHECK(out.message == L"Finalized as 2026-09-30.");
    }

    TEST_CASE("no named file open, history write fails -> nothing is "
              "finalized; partial failure is never reported as success") {
        FinalizePersistencePorts ports;
        ports.writeBackupSnapshot = []() {};
        ports.writeHistoryRecord = [](std::wstring* err) { *err = L"disk full"; return false; };
        ports.hasNamedFile = false;
        auto out = CoordinateFinalizeDay(ports, L"2026-09-30");
        CHECK(out.kind == FinalizeResultKind::HistoryWriteFailed);
        CHECK_FALSE(out.finalized);
        CHECK(out.message.find(L"disk full") != std::wstring::npos);
    }

    TEST_CASE("named file open, history succeeds, named-file sync succeeds "
              "-> clean finalize") {
        FinalizePersistencePorts ports;
        ports.writeBackupSnapshot = []() {};
        ports.writeHistoryRecord = [](std::wstring*) { return true; };
        ports.hasNamedFile = true;
        ports.writeNamedFileSync = [](std::wstring*) { return true; };
        auto out = CoordinateFinalizeDay(ports, L"2026-09-30");
        CHECK(out.kind == FinalizeResultKind::FinalizedClean);
        CHECK(out.finalized);
        CHECK_FALSE(out.dirtyAfter);
    }

    TEST_CASE("named file open, history succeeds, named-file sync FAILS -> "
              "still finalized (the permanent record is what matters), but "
              "reported as a warning, not a plain success, and left dirty") {
        FinalizePersistencePorts ports;
        ports.writeBackupSnapshot = []() {};
        ports.writeHistoryRecord = [](std::wstring*) { return true; };
        ports.hasNamedFile = true;
        ports.writeNamedFileSync = [](std::wstring* err) { *err = L"file is locked"; return false; };
        auto out = CoordinateFinalizeDay(ports, L"2026-09-30");
        CHECK(out.kind == FinalizeResultKind::FinalizedNamedFileSyncFailed);
        CHECK(out.finalized); // the history record - what actually locks the day in - did succeed
        CHECK(out.dirtyAfter); // the open file no longer matches; must not be silently forgotten
        CHECK(out.message.find(L"file is locked") != std::wstring::npos);
        CHECK(out.message.find(L"Use File > Save to retry") != std::wstring::npos);
    }

    TEST_CASE("named file open, history FAILS -> named-file sync is never "
              "even attempted (nothing to sync, since nothing was "
              "finalized)") {
        bool namedFileSyncCalled = false;
        FinalizePersistencePorts ports;
        ports.writeBackupSnapshot = []() {};
        ports.writeHistoryRecord = [](std::wstring* err) { *err = L"permission denied"; return false; };
        ports.hasNamedFile = true;
        ports.writeNamedFileSync = [&](std::wstring*) { namedFileSyncCalled = true; return true; };
        auto out = CoordinateFinalizeDay(ports, L"2026-09-30");
        CHECK(out.kind == FinalizeResultKind::HistoryWriteFailed);
        CHECK_FALSE(out.finalized);
        CHECK_FALSE(namedFileSyncCalled);
    }

    TEST_CASE("a failed finalize is never reported with FinalizedClean or "
              "finalized=true - the specific proof partial failure can't "
              "look like complete success") {
        FinalizePersistencePorts failHistory;
        failHistory.writeHistoryRecord = [](std::wstring*) { return false; };
        auto out1 = CoordinateFinalizeDay(failHistory, L"2026-09-30");
        CHECK_FALSE(out1.finalized);
        CHECK(out1.kind != FinalizeResultKind::FinalizedClean);

        FinalizePersistencePorts failNamed;
        failNamed.writeHistoryRecord = [](std::wstring*) { return true; };
        failNamed.hasNamedFile = true;
        failNamed.writeNamedFileSync = [](std::wstring*) { return false; };
        auto out2 = CoordinateFinalizeDay(failNamed, L"2026-09-30");
        CHECK(out2.kind != FinalizeResultKind::FinalizedClean); // finalized, but not CLEANLY - a distinct kind
    }
}

TEST_SUITE("CoordinateSaveAs - Save As failure handling (Phase 1 / F1)") {
    TEST_CASE("successful write adopts the new file and clears dirty") {
        auto out = CoordinateSaveAs(L"C:\\Old.fbd", L"C:\\New.fbd",
            [](const std::wstring&, std::wstring*) { return true; });
        CHECK(out.success);
        CHECK(out.resultingCurrentFile == L"C:\\New.fbd");
        CHECK_FALSE(out.dirtyAfter);
    }

    TEST_CASE("a failed write restores the PREVIOUS file association exactly "
              "- proves Save As can't leave the app pointing at a file it "
              "never actually wrote") {
        auto out = CoordinateSaveAs(L"C:\\Old.fbd", L"C:\\New.fbd",
            [](const std::wstring&, std::wstring* err) { *err = L"disk full"; return false; });
        CHECK_FALSE(out.success);
        CHECK(out.resultingCurrentFile == L"C:\\Old.fbd");
        CHECK(out.errorMessage == L"disk full");
    }

    TEST_CASE("a failed Save As from an unsaved (no previous file) state "
              "reverts to empty, not to the attempted new path") {
        auto out = CoordinateSaveAs(L"", L"C:\\New.fbd",
            [](const std::wstring&, std::wstring* err) { *err = L"access denied"; return false; });
        CHECK_FALSE(out.success);
        CHECK(out.resultingCurrentFile.empty());
    }

    TEST_CASE("the write function receives the NEW path, never the previous "
              "one, regardless of outcome") {
        std::wstring seenPath;
        CoordinateSaveAs(L"C:\\Old.fbd", L"C:\\New.fbd",
            [&](const std::wstring& p, std::wstring*) { seenPath = p; return true; });
        CHECK(seenPath == L"C:\\New.fbd");
    }
}
