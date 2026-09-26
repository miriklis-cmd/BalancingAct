# Roadmap

This file is the single source of truth for "what's built, what's being
tested, and what's next." Check here first if you've lost track of where
things stand — that's exactly what this file is for.

## Status: v0.9.39 — Dark Mode removed, back to the v0.9.33 baseline

v0.9.34 through v0.9.38 built and repeatedly patched a Dark Mode feature
that never matched what Jack actually wanted, and he asked to scrap it
entirely and revert to before it started - see CHANGELOG.md's `[0.9.39]`
entry for the full account. This environment had no saved snapshot of
v0.9.33 to restore from directly, so Jack sent back his own copy of that
build and it was restored verbatim (only `version.h` bumped forward to
0.9.39). Everything below this point, up through the v0.9.33 status
description that follows, describes that same pre-Dark-Mode baseline.
Item 11 further down has a closing note on where Dark Mode's own history
now stands; a "v2" of it may return later once Jack has a clearer mockup
of what he wants.

## Status: v0.9.33 — awaiting test feedback

v0.9.0 (Date/Notes/Duplicate Last Entry) is confirmed working — see below.
v0.9.1–v0.9.16 were never separately confirmed as final before being
superseded (v0.9.9, v0.9.17, and v0.9.19 all failed to compile — fixed
in v0.9.10, v0.9.18, and v0.9.20 respectively). v0.9.18 was confirmed
working in full. v0.9.20 fixed v0.9.19's build; v0.9.21 fixed that
version's warning-dialog wording; v0.9.22 closed the "first entry never
gets re-checked" gap; v0.9.23 fixed the IQR-floor bug and removed the
interactive Add Entry dialog. v0.9.24 fixed a real data-loss bug found
via Jack's own testing - File > Open and the Recent Files menu
had no unsaved-changes check at all, silently discarding in-progress
edits. v0.9.25 tightened the rolling backup interval from 10 to 3
minutes, prompted by that same incident. v0.9.26 made that tighter
interval not wasteful - a snapshot is now skipped entirely if nothing's
actually changed since the last one. v0.9.27 adds the source file's
name to each backup's own filename, so switching between files no
longer leaves you guessing which snapshot is which. v0.9.28 closes the
last real exposure window from the v0.9.24 incident - File > Open,
Recent Files, and Restore from Backup now each take an unconditional
backup snapshot the instant you confirm the discard prompt, not just on
the next 3-minute tick or the next explicit Save. v0.9.29 fixes a real
bug Jack found smoke-testing v0.9.28: the "roughly every 3 minutes"
rolling backup was never actually driven by a real timer - it only ran
inside the focus-loss autosave path, so a screen left idle after one
edit (no further tabbing/clicking) never got backed up no matter how
long you waited. A genuine recurring `WM_TIMER` now drives it instead.
v0.9.30 makes the discard-confirmation prompt real "unsaved changes"
tracking instead of "is there any data at all" - Jack: "should be kinda
dirty flagged? If something changes, dialog comes up. Once saved, then
no dialog until something changes again." That's now exactly how it
behaves. v0.9.31 fixes a second, previously-unnoticed bug behind Jack's
original "every 3 minute save not working" report: `WriteBackupSnapshot`
discarded the write's success/failure result outright, so a failing
rolling backup (unwritable `backups\` folder, disk full, antivirus lock)
was completely silent - no warning, and its own tracking state was
updated as if it had succeeded anyway, suppressing any near-term retry.
Found by going back through the code at Jack's explicit request after he
corrected his original account (it was v0.9.28, with ~5 active Add Entry
commits, not a fully idle screen) - the v0.9.29 timer fix alone didn't
fully explain that. Now checked and warned once, same pattern as the
existing autosave-failure warning. v0.9.32 fixes one more real gap in
the same feature, found by Jack while running the v0.9.28 checklist
directly: File > New never got a backup snapshot before discarding,
unlike Open/Recent Files/Restore from Backup - now fixed the same way.
v0.9.33 fixes a smaller UX bug Jack found in the same test pass: Restore
from Backup wasn't opening to the backups folder - a documented Windows
quirk (GetOpenFileNameW only honors lpstrInitialDir on the very first
dialog call ever; after that it reuses whichever folder was last
navigated to in any prior call, here Desktop from an earlier File >
Open), now worked around by pre-filling the dialog's file buffer with
the target folder, which does reliably override it (see CHANGELOG.md for
full detail on every fix).

### New in v0.9.19 through v0.9.23 — outlier feature, mostly confirmed
(run the doctest suite first if you haven't since v0.9.23 —
`FishBalanceCore.h` changed then, not again in v0.9.24 or v0.9.25)

- [x] **Outlier flag sets silently, no dialog at Add Entry** — confirmed
      working (2026-09-24).
- [x] **Fixing the flagged entry clears its own flag** — confirmed
      working (2026-09-24).
- [x] **Right-click "Clear flag" on a flagged row** — confirmed working
      (2026-09-24).
- [x] **Double-click a still-flagged row** shows the review dialog
      correctly — confirmed working (2026-09-24).
- [x] **Fewer than 4 same-day entries for a species** — confirmed
      nothing gets flagged (2026-09-24).
- [x] **Old `.fbd` files (pre-v0.9.19) still load correctly** —
      confirmed (2026-09-24) - loading an older file worked fine; this
      test is also what surfaced the v0.9.24 data-loss bug below, which
      was a separate, pre-existing issue in File > Open, not something
      wrong with old-file loading itself.
- [x] **The original bug report** (first-entry retroactive flagging) —
      confirmed working (2026-09-24).
- [x] **Deleting an entry re-checks its siblings** — confirmed working
      (2026-09-25).
- [x] **Undo Delete does the same** in reverse — confirmed working
      (2026-09-25).
- [x] **The floor fix** ($10 baseline / $12 not flagged / $50 still
      flagged) — confirmed working (2026-09-24).

### New in v0.9.33 — needs its own first check

- [ ] **Restore from Backup opens to the backups folder.** Use File >
      Open first (navigate anywhere else, e.g. Desktop, then cancel out
      or open something), then use File > Restore from Backup - the
      dialog should open directly into `backups\`, not wherever Open was
      last pointed.

### New in v0.9.32 — needs its own first check

- [x] **File > New now takes a backup snapshot before discarding.** —
      confirmed working (2026-09-25).
- [x] **Clicking No still takes no backup and changes nothing** —
      confirmed working (2026-09-25).

### New in v0.9.31 — needs its own first check

- [x] **Backups actually appear during normal active data entry.** —
      confirmed working (2026-09-25); the originally-reported "every 3
      minute save not working" is resolved.
- [ ] **Simulate a failure and confirm the warning appears.** Hardest to
      test directly without deliberately breaking something (e.g.
      temporarily making the `backups\` folder read-only, or renaming it
      away so `CreateDirectoryW` can't recreate it somewhere it's not
      allowed to) - if you can force a failure, confirm a "Backup
      Snapshot Failed" warning dialog appears once, doesn't repeat on
      every subsequent attempt, and autosave.fbd/your named file both
      keep working normally regardless. Not critical to test exhaustively
      - the important part is that backups quietly work in the normal
      case above.
- [x] **No change to normal, successful backup behavior** — confirmed
      working (2026-09-25).

### New in v0.9.29 — needs its own first check

- [x] **Idle backups actually happen now.** — confirmed working
      (2026-09-25); the case that was broken through v0.9.17-28 (see
      CHANGELOG.md's `[0.9.29]` entry) is fixed.
- [x] **No spam on a genuinely idle, unchanged screen.** — confirmed
      working (2026-09-25).
- [x] **Normal active data entry still feels the same** — confirmed
      working (2026-09-25).

### New in v0.9.30 — needs its own first check

- [x] **No prompt right after a Save.** — confirmed working (2026-09-25).
- [x] **Prompt comes back after the next real edit.** — confirmed working
      (2026-09-25).
- [x] **Debtor/Cash edits count as a real change.** — confirmed working
      (2026-09-25).
- [x] **A rename/merge via Manage Names counts as a real change too** —
      confirmed working (2026-09-25).
- [x] **File > New, and a fresh File > Open/Recent Files/Restore from
      Backup, all start clean** — confirmed working (2026-09-25).
- [x] **Restoring a backup, then switching away without saving it, still
      doesn't prompt** — confirmed as the wanted behavior (2026-09-25);
      Jack's happy with it as deliberately implemented (see CHANGELOG.md's
      `[0.9.30]` entry), no tightening needed. (Separately, while testing
      this, Jack found Restore from Backup's file dialog wasn't opening to
      the `backups\` folder - a real but unrelated bug, fixed in v0.9.33,
      see that entry above.)

### New in v0.9.28 — needs its own first check

- [x] **Switching files now takes a backup snapshot first.** — confirmed
      working (2026-09-25). This is the direct fix for the exposure
      window the v0.9.24 incident left open - Jack: "Umm I dunno if we
      should for switching files? Seeming as I lost work when we did,
      perhaps we should?"
- [x] **Clicking No on the discard prompt still takes no backup and
      changes nothing** — confirmed working (2026-09-25).
- [x] ~~No spurious snapshot on File > New~~ - **superseded by v0.9.32**:
      this was based on the wrong assumption that New has nothing to
      preserve. Jack found the gap directly while testing this very item
      - File > New now DOES take a snapshot before discarding, same as
      the other three paths. See the v0.9.32 checklist above instead.

### New in v0.9.24/v0.9.25/v0.9.26/v0.9.27 — needs its own first check
(no `FishBalanceCore.h` change in any of these, no doctest re-run needed)

- [x] **File > Open warns before discarding unsaved data.** — confirmed
      working (2026-09-25).
- [x] **Recent Files does the same** — confirmed working (2026-09-25).
- [x] **File > New and Restore from Backup still prompt correctly** —
      confirmed working (2026-09-25).
- [x] **No prompt when there's nothing to lose** — confirmed working
      (2026-09-25).
- [x] ~~Backups appear roughly every 3 minutes when actually editing~~ —
      superseded by the v0.9.29 checklist above, confirmed there.
- [x] **No new backup file when nothing's changed** — confirmed working
      (2026-09-25).
- [x] **Backup filenames now show the source file.** — confirmed working
      (2026-09-25).
- [x] **Pruning still works correctly with the new filename format** —
      eyeballed and confirmed sane (2026-09-25).

### Confirmed
- [x] **Rolling timestamped backups.** `backups\` folder appears next to
      the .exe with `backup_YYYYMMDD_HHMMSS.fbd` files, both from normal
      use and immediately after File > Save / Save As — confirmed
      working (2026-09-22).
- [x] **File > Restore from Backup...** Loads a chosen backup correctly,
      title bar shows "(unsaved)" afterwards, File > Save As required to
      keep it — confirmed working (2026-09-22).
- [x] **Backup pruning** — not practically testable end-to-end in one
      sitting (would need 50+ snapshots, i.e. many hours), so verified
      by a careful re-read of `PruneOldBackups` instead
      (2026-09-22): the `<=`/`>` threshold check, the fixed-width
      zero-padded timestamp making a plain lexicographic sort also a
      correct chronological one, and the delete loop targeting exactly
      the oldest excess entries were all confirmed correct. One benign,
      non-blocking edge case noted: a manually-added file matching
      `backup_*.fbd` but not the timestamp format would sort as
      "newest" (letters sort after digits) and never get pruned - not a
      concern for how this folder is actually used, since only
      `WriteBackupSnapshot` ever writes to it.
- [x] Focus after Add/Update Entry lands on Supplier
- [x] Status bar shows version + filename correctly
- [x] Print Preview opens normally, looks correct
- [x] Combo box first-paint fix — confirmed working
- [x] Atomic/checked saves — confirmed working
- [x] Email merge conflict prompt — confirmed working correctly (the
      specific case tested turned out to be a genuine conflict from a
      pre-existing orphaned email entry, not a bug — see the `[0.9.13]`
      changelog entry and "Open questions" below)
- [x] Debtor/Cash autosave — confirmed working (v0.9.13's real fix)
- [x] Focus-loss autosave trigger (v0.9.15) — confirmed working by Jack
      (testing this is exactly what surfaced the v0.9.16 subtraction bug
      below, which required the save/reload round-trip to have already
      worked correctly)
- [x] **Debtor/Cash subtraction (v0.9.16).** `123+11-21` correctly
      computes 113, not 134 — confirmed working by Jack. This was a
      real, silent bug found via his own testing, not a hypothetical.

### Still needs testing (carried over, not yet confirmed)

- [ ] **In-progress "Add Entry" draft survives a crash/power loss** -
      still the v0.9.14 test (type something without clicking Add Entry,
      force-close via Task Manager, reopen), now exercising the
      focus-loss trigger instead of per-keystroke - so click/Tab away
      from the field before force-closing, to confirm the save actually
      fired.
- [ ] **Supplier email survives a rename/merge** on a *clean* rename (no
      pre-existing orphaned entries involved) — the conflict-prompt path
      is now confirmed working, but the plain "no conflict" path hasn't
      been separately confirmed yet.

### Open questions — Jack is thinking about these, no action taken yet
- **Orphaned email entries** — renames made *before* the v0.9.9
  email-migration fix existed can leave `emails.txt` entries for names
  that no longer exist anywhere in current data (confirmed via the
  uploaded `emails.txt`: a bare "Jenkins" entry with no matching
  supplier). These are currently invisible in the Manage Names UI (it
  only lists names with current entries) and can only be found by
  opening `emails.txt` directly, or by hitting a merge-conflict prompt
  like the one that surfaced this. Options on the table: (a) leave as-is,
  hand-edit `emails.txt` to clean up when it happens; (b) extend Manage
  Names to also list orphaned email-only entries so they're visible and
  deletable through the UI. No decision yet.

### v0.9.7 — confirmed

- [x] **Species clears after Add/Update Entry, Supplier doesn't** —
      **CONFIRMED**; the focus-target follow-up is fixed in v0.9.8 above.
- [x] **"Duplicate Supplier & Species" button** — **CONFIRMED** working
      as intended.
- [x] **Manage Names "Apply" button grays out correctly** — **CONFIRMED**
      working.

### Automated — run `tests/run_tests.ps1` (or the `FishBalanceTests` /
`FishBalanceCoreTests` CMake+CTest target)

As of v0.9.4, these are covered by the automated suite and don't need
manual re-verification every time:
- [x] `tests/run_tests.ps1` builds and reports **75 test cases / 215
      assertions, all passed** — confirmed twice on a real Windows/MSVC
      toolchain (Developer PowerShell for VS): first with the `C5285`
      warning present, then again after the v0.9.5 fix, which came back
      **completely clean — zero warnings, zero errors.**
- [x] *(covered by `test_fbd_loader.cpp`)* — valid `.fbd` files load
      correctly, backward compatibility with the old 4-field format,
      garbage/non-`.fbd` files are rejected, truncated files (`BEGIN`
      with no `END`) are rejected, NaN/Inf/negative Kgs/Price rows are
      skipped and counted rather than silently zeroed, empty
      Supplier/Species rows are rejected, whitespace is trimmed on load
- [x] *(covered by `test_aggregation.cpp`)* — the Total Overview/By
      Species math itself (`ComputeGroupedTotals`, `ComputeSpeciesStats`,
      `BuildBreakdownData`) produces correct totals; this is the exact
      computation behind the v0.9.1 column-mismatch bug
- [x] *(covered by `test_csv_export.cpp` / `test_email.cpp`)* — CSV field
      escaping and the formula-injection guard, `mailto:` URI
      percent-encoding, email address sanity-checking, the email body's
      "too long, use a shortened summary" fallback

### Manual — still needs a human looking at the real app

These genuinely can't be automated without a lot of extra scaffolding
this project doesn't have (or want) yet — see the "why not automated"
reason on each:
- [x] **Total Overview tab visually shows three columns** (Supplier / Kgs
      / Total ($)) with correct dollar figures on screen — **CONFIRMED**
      working
- [x] **Delete a row, then File > New (or Open a different file, or
      Recent Files): Edit > Undo Delete is grayed out** — **CONFIRMED**
      working
- [x] **Rename/merge via Manage Names, then check Undo Delete is grayed
      out** if that rename changed anything — **CONFIRMED** working (the
      first attempt was a false alarm - tested on an empty sheet with no
      data to actually rename, and a mix-up about which "grayed out" was
      meant; re-tested properly and it's correct)
- [x] **Startup with a normal, intact `autosave.fbd` loads it exactly as
      before** — **CONFIRMED** working
- [x] **General feel of the app** — one real bug found and fixed this
      round: the Manage Names hint text was visibly cut off (see
      CHANGELOG.md's `[0.9.6]` entry). The open question about
      Supplier/Species not clearing after Add Entry is **resolved**:
      Supplier stays populated (supports fast batch entry for one
      supplier), Species clears; the "Duplicate Last Entry" and
      "Duplicate Supplier & Species" buttons cover the rest — confirmed
      correct by Jack (2026-09-24).
- [ ] **Clean build check**: rebuilding (CMake, MinGW, or MSVC) produces
      zero warnings and zero errors — **still not confirmed for the main
      app specifically.** The 2026-09-24 build log only shows
      `FishBalanceTests.exe`'s 8 steps (all `tests\*.cpp.obj` + linking)
      - `FishBalanceManager.exe`/`main.cpp.obj` don't appear at all,
      most likely because it was already up to date from an earlier
      build and Ninja skipped it as nothing-to-do. To actually confirm
      this, use **Build > Rebuild All** (not "Build All") from Visual
      Studio's CMake menu, which forces every target to recompile
      regardless of whether it looks up to date - or just check that
      the resulting log includes `main.cpp.obj` and
      `FishBalanceManager.exe`'s link step this time.
- [x] **Combo box first-paint check** (see CHANGELOG.md's `[0.9.3]`
      entry) — **CONFIRMED** working

## Status: v0.9.0 — confirmed working

The current build adds Date field, Notes field, and Duplicate Last Entry
(see CHANGELOG.md for details). **Confirmed working** — all test checklist
items passed:
- [x] Date picker opens/works, defaults to today, sticks after adding an entry
- [x] Notes field saves and shows up in the list and after a save/reload
- [x] An old `.fbd` file (saved before this version) still opens correctly
- [x] Duplicate Last Entry fills the form correctly, focus lands on Kgs
- [x] Sorting by the Date column behaves sensibly
- [x] CSV export includes Date and Notes

## Done (stable, shipped)

- Data entry: Supplier/Species autocomplete, Tab/Enter batch entry,
  Date + Notes fields, Duplicate Last Entry
- Edit existing entries in place; delete with confirmation + single-level
  undo
- Save/Open/autosave (`.fbd` format), Recent Files, window/session
  persistence
- Four report views: Data Entry (raw list, sortable/filterable), Total
  Overview (per-supplier), Breakdown (Supplier > Species > Price), By
  Species (with Avg/Highest/Lowest price)
- Print Preview + Print + PDF export (via "Microsoft Print to PDF")
- CSV export (everything, one file)
- Email All Suppliers (mailto drafts, one-at-a-time flow, works with or
  without a saved address)
- Manage Supplier/Species Names (merge/rename typos, set email addresses)
- Custom app icon, DPI-aware layout, security/data-integrity hardening

## Next up (in order)

**0. Automated testing infrastructure** — currently zero automated tests
exist; everything is verified by manual build+run per Testing.md.
- [x] **`FishBalanceCore.h` extraction — done.** Pulled the pure logic
      (data model, `.fbd` parsing/validation, date/number parsing, money/kg
      formatting, report aggregation, CSV/email string-building) out of
      `main.cpp` into a new header with zero Win32/`windows.h` dependency.
      No visible behavior change (see CHANGELOG.md's Unreleased entry) —
      `main.cpp` now includes this header and calls into it instead of
      keeping its own copies; a few functions that need a Win32 type at
      the boundary (`SYSTEMTIME`, `GetLocalTime`) kept a thin wrapper with
      their original signature, so no other call site had to change.
      Verified by compiling and running a temporary smoke test directly
      (plus a clean AddressSanitizer/UBSan pass) before wiring it in.
- [x] **doctest suite — done (v0.9.4).** 75 test cases / 215 assertions
      across 6 files in `tests/`, using doctest (single-header, vendored
      in). `tests/run_tests.ps1` for quick everyday use, plus a
      `FishBalanceTests` CMake target wired into CTest. Compiled and run
      directly with g++ on Linux before delivery (clean under
      `-Wall -Wextra`, clean under AddressSanitizer+UBSan) — not yet
      confirmed on the actual MSVC/Windows toolchain, see the checklist
      above. Covers: unit tests for every function in `FishBalanceCore.h`,
      integration tests for full `.fbd` round-trips, a regression test for
      each confirmed v0.9.1 bug, and several characterization tests
      pinning down already-known permissive behaviors on purpose (see
      CHANGELOG.md's `[0.9.4]` entry for the full breakdown and for one
      real test-writing mistake this caught and fixed before shipping).
      Win32-only concerns (Print Preview rendering, actual printing, DPI
      switching, window restoration, dialog appearance, the v0.9.3 combo
      box paint issue) stay on the manual checklist above — no automated
      substitute planned for those.

All Tier 2 findings from the v0.9.1 external audit are now closed out
(see CHANGELOG.md's v0.9.1/v0.9.8/v0.9.9 entries and
SecurityHardeningRegister.md for full detail) - what's left is one
performance item plus the feature backlog:

1. **`RefreshAll()`/autosave performance at scale — fixed in v0.9.15.**
   Was confirmed urgent, not theoretical, once the real target volume
   (500-1000 entries/day, primarily hand-typed) was confirmed. v0.9.13/
   v0.9.14 (fixing the Debtor/Cash and draft-field autosave gaps) had
   made every keystroke in Supplier/Species/Kgs/Price/Notes/Date/
   Debtor/Cash trigger a full, synchronous atomic rewrite of the entire
   day's file - cheap on a small test file, a real, felt cost as a
   day's file grows toward real volume. Fixed by switching those six
   fields from a per-keystroke trigger to a per-focus-loss trigger
   (`EN_KILLFOCUS`/`CBN_KILLFOCUS`) - the dominant cost was disk I/O
   frequency, not string-building, so this directly targets it without
   a timer (the earlier debounced-timer attempt for Debtor/Cash was
   already tried and found unreliable - see CHANGELOG.md's `[0.9.15]`
   entry for the full three-iteration history and the accepted
   trade-off).
2. **Flat-file + SQLite hybrid architecture — decided 2026-09-05, full
   deployment design in NETWORK_ARCHITECTURE.md.** Not a straight
   either/or: day-files remain the sole source of truth for the primary
   function (zero risk to the one thing that has to be bulletproof, see
   BUSINESS_RULES.md), SQLite is a separate, disposable, derived
   reporting layer built by ingesting each day-file at finalization -
   the *only* integration point between the two, so SQLite can never
   block, slow down, or corrupt live editing. Ruled out: MongoDB/other
   server-based NoSQL (wrong deployment model and data-shape fit),
   LevelDB/RocksDB (key-value only, no query language, defeats the
   purpose). Now part of **Bucket C** (see "Definition of done" below) -
   sequenced after Bucket A's single-machine "done" state, not
   concurrent with it. Item 1 (autosave performance) is unblocked by
   this and should still happen first regardless.
3. **Day-rollover with dated filenames** (raised 2026-09-04, not yet
   built) — on launch (or at midnight if left running), if today's date
   doesn't match the currently open file's date, prompt to start a new
   file named by date (e.g. `2026-09-05.fbd`) or keep working in the
   current one (for legitimate late-night entries that should count as
   the prior day). **Now a hard prerequisite for item 2's hybrid
   architecture**, not just item 5's Price History: the SQLite ingestion
   trigger *is* the day-rollover confirmation moment, and both features
   need a single, predictable "history folder" where day-files live and
   can be enumerated/rebuilt from - there's currently no enforced
   one-file-per-day naming/location convention at all (Jack's actual
   files are user-chosen names like `21112.fbd`, not date-based).
4. **DONE — Timestamped backups.** Built in v0.9.17 (compile-fixed in
   v0.9.18), confirmed working 2026-09-22 (see "Confirmed" above). A
   `backups\` folder next to the .exe holds rolling
   `backup_YYYYMMDD_HHMMSS_<sourcefile>.fbd` snapshots (the source-file
   label added in v0.9.27, below), taken at most once every 3
   minutes (tightened from 10 in v0.9.25, after a real data-loss
   incident where no backup yet existed for the lost work) off the
   autosave path plus once on every explicit File > Save/Save As,
   capped at the 50 most recent - a deliberate choice made with Jack
   NOT to raise alongside the interval tightening, so the rolling
   coverage window is now ~2.5 hours rather than a full business day
   (more frequent recent coverage, traded against less total history).
   **v0.9.26**: a timer-triggered snapshot is skipped entirely if the
   content hasn't actually changed since the last one - the tighter
   3-minute interval alone would otherwise have written a duplicate
   file every 3 minutes even while just browsing/sorting with no real
   edits. **v0.9.27**: each backup's filename now includes which source
   file was open at the time (e.g. `..._21112.fbd`, or `..._unsaved.fbd`
   with no named file open) - Jack: working across multiple files in a
   session left no way to tell which snapshot belonged to which file.
   The label goes AFTER the timestamp specifically so `PruneOldBackups`'
   sort-by-name-equals-sort-by-time logic keeps working correctly. File
   > Restore from Backup... browses and loads one (doesn't
   auto-adopt it as the current named file - explicit Save As required
   to keep it). **v0.9.28**: File > Open, Recent Files, and Restore from
   Backup now each take an unconditional snapshot the moment the discard
   prompt is confirmed, before the switch overwrites anything in
   memory - directly closing the exposure window the v0.9.24 incident
   left open (a rolling-timer or explicit-Save snapshot might not exist
   yet for in-progress work at the moment of a switch). Jack: "Umm I
   dunno if we should for switching files? Seeming as I lost work when
   we did, perhaps we should?" See CHANGELOG.md's `[0.9.17]`/`[0.9.25]`/
   `[0.9.26]`/`[0.9.27]`/`[0.9.28]` entries for full detail.
5. **Price history/trend per species over time — fleshed out
   2026-09-04, core questions answered, business model corrected
   2026-09-05.** Originally just "a new tab showing how a species'
   price moved over time"; now specified:
   - **Corrected understanding of what this app actually models (see
     BUSINESS_RULES.md's new "Business model" section)**: this is a
     seafood *agency*, not a buy-and-resell wholesaler. Suppliers send
     fish on **consignment**; the agent prices and sells it on the day
     to achieve the best possible result, then accounts back to the
     supplier for what was achieved. `Price` is the market price
     *achieved* selling a supplier's consigned fish - there's no
     separate purchase cost and resale markup to reason about, just one
     price. An earlier draft of this spec incorrectly framed this as
     "cost-tracking to inform a markup decision" - wrong on two separate
     passes before being corrected here.
   - **Actual purpose**: market-rate benchmarking to inform *today's*
     pricing decision - "what have we achieved for this species
     recently, so I know what to aim for today" (Jack: "I just want the
     ability to look at past data to make a determination on what I can
     sell a product I have for" - now correctly understood as "what
     price to achieve when selling it," not "what markup to add to a
     cost"). Explicitly **not** a margin/markup calculator - there's no
     margin to calculate in a consignment model.
   - **Species-only vs. species+supplier drill-down: confirmed both.**
     Both views matter for a pricing decision - the overall trend
     for a species, and the ability to compare what different suppliers
     have been charging for it.
   - **Date range presets (7/30/90 days + custom): confirmed.**
   - **UI placement — decided differently for the two purposes
     (2026-09-05), per the new principle in ARCHITECTURE.md:**
     - **Purpose #2 (bounded occurrence history / rare-species
       lookback): confirmed NOT a tab.** Jack's own reasoning: this
       isn't part of the app's primary purpose (balancing today's sale -
       no lost product, no wrong price, no wrong supplier attribution),
       it's an occasional lookup, and shouldn't complicate the daily
       tab row. **A menu item opening a popup window instead** - the
       same pattern already used by Manage Names and Print Preview
       (`ManageWndProc`/`PreviewWndProc` in `main.cpp`, both disabling
       the main window while open). Working name: "Tools > Species
       Price Lookup" or similar - exact naming/menu location not yet
       decided.
     - **Purpose #1 (today vs. yesterday/last week/last month/last
       year, trend): placement still open.** Jack called this
       "worthwhile to see how we did today," which is closer to the
       app's core daily-review purpose than purpose #2 is - not
       automatically assuming this is secondary tooling too. Could
       reasonably be its own tab, or integrated into an existing one
       (Total Overview? By Species?), or also a menu item - needs a
       specific decision, not inferred from purpose #2's placement.
   - **Table vs. chart: confirmed both are the real target** (not chart
     as an optional stretch). Still sequenced as two increments rather
     than one big build: **v1 = table** (Date / Avg / Min / Max / Total
     Kgs, filterable by supplier), since it's lower-risk (this app has
     zero charting infrastructure today - a line chart is a genuinely
     new kind of UI element, the first non-ListView visual component in
     the app) and delivers real value on its own the moment the
     underlying data layer exists. **v2 = chart**, built on that same
     proven data layer once v1 is working and tested, not built from
     scratch alongside it.
   - **Raw entries vs. daily aggregate**: proposed default (Jack didn't
     give a firm answer) - show the daily aggregate (avg/min/max) as the
     primary trend view, since that's what answers "what's the range
     I've been paying", but make the underlying individual entries
     visible on drill-down (e.g. selecting a specific supplier+date)
     rather than only ever showing blended numbers - "I paid Imlay $8
     last Tuesday" is exactly the kind of concrete, recent fact useful
     for an actual pricing decision, and a pure aggregate would hide it.
     Worth Jack's explicit confirmation before building, since this is
     Claude's proposal, not something he directly specified.
   - **Two purposes, clarified 2026-09-05 then unified 2026-09-05 -
     genuinely the same underlying query, not two separate builds:**
     1. **Fixed-period comparison**: "how does Blue Grenadier today
        compare to last week/month/year" - for species sold regularly
        enough that there's data at each of those fixed offsets.
     2. **Bounded occurrence history**: refined from an earlier, narrower
        framing ("what price did we achieve last time") to the real
        want - "3 months ago Species X got $32, 6 months ago it was
        $45" - the *full* history of occurrences within some practical
        search window, not just the single most recent one. Jack
        confirmed he won't know a useful starting point (that's the
        whole problem - not knowing when it was last sold), so the
        window needs a practical **upper limit** rather than a precise
        target date - Jack's proposal: search back through the current
        calendar year.
     - **Realization: #1 and #2 are the same query**, just called with
       different window widths. Both are "show occurrence history
       (grouped by date, with avg/min/max/kgs) for species X within
       date range D, optional supplier filter" - #1 uses narrow,
       specific windows (this week / equivalent week last month/year)
       because dense recent data is expected; #2 uses one wide window
       (see below) because the data is sparse and the exact dates are
       unknown. One data-fetch capability, not two.
     - **"Sale" terminology - now correctly resolved** (an earlier
       version of this note had it backwards): "what price did we
       **achieve**" is exactly the language of a consignment agency -
       the price achieved *selling* a supplier's fish on their behalf,
       not a purchase negotiation (there is no purchase in this
       business model at all - see BUSINESS_RULES.md's "Business model"
       section).
     - **Search window: confirmed.** Rolling window (not calendar year),
       defaulting to 12 months back from today, with a user-adjustable
       control to make it longer or shorter (not hardcoded to 12).
     - **Proposed unified UI**: the same date-range control already
       spec'd for purpose #1 (presets + custom) does double duty for
       both purposes - selecting a species with a narrow range serves
       #1 (period comparison), a wide range (defaulting wide, e.g. last
       12 months, when the user doesn't know how far back to look)
       serves #2 (full occurrence history), and the result is always
       "every occurrence found within the selected range, most-recent-
       first" - no separate mode to pick.
     - A hard technical ceiling on the search window (something far
       larger than any realistic business need, e.g. a few years) is
       still worth keeping regardless of the UX default, purely to bound
       worst-case query cost - much more relevant under flat files
       (bounds how many day-files could ever need scanning) than under
       SQLite (cost scales with matching rows, not calendar span).
     - **Architecture argument, sharpened further**: under flat files,
       even a *bounded* year-long backward search for a rare species
       means opening and parsing up to ~365 day-files to find the 2-3
       that actually contain it. Under SQLite, the exact same query
       regardless of window width:
       `SELECT date, AVG(price), MIN(price), MAX(price), SUM(kgs) FROM
       entries WHERE species = ? AND date >= ? GROUP BY date ORDER BY
       date DESC` - fast and indexed, cost scales with how often the
       species was actually sold, not how many days were searched.
     - **Still open**: exact-day vs. rolling-window comparison
       specifically for purpose #1's fixed offsets (today vs. exactly
       7/30/365 days ago, vs. equivalent-week averages) - see the
       earlier note on this, still unresolved.
     - Shares the same underlying data-fetch capability as the trend
       table above - not a separate data layer to build for either
       purpose.
   - **Missing/corrupted day-file within a range**: show what's
     available, note which dates failed, matching how `ParseFbdContent`
     already reports `skippedLines` rather than failing everything or
     silently dropping gaps.
   - **This is the single feature in the roadmap where the flat-file-vs-
     SQLite choice isn't stylistic - it's the concrete difference
     between hand-rolling a multi-file aggregation engine in C++ (open
     every day-file in range, parse each with `ParseFbdContent`, filter
     and aggregate by hand) versus one indexed SQL query
     (`SELECT date, AVG(price), MIN(price), MAX(price), SUM(kgs) FROM
     entries WHERE species = ? AND date BETWEEN ? AND ? GROUP BY date`).
     Worth deciding the architecture question above before starting this
     feature, not after.
   - **Prerequisite if staying on flat files**: the day-rollover feature
     above needs to exist first, to establish a predictable convention
     for which files constitute "history" to search.
   - **Not shared with item 7 below** (outlier warning) - corrected
     2026-09-04: that feature only needs *today's* already-in-memory
     data (`g_entries`), not cross-day history, so it's fully
     independent of this item and the architecture decision above - see
     item 7's own spec.
6. **Highlight cheapest supplier per species** — surfaced on the
   Breakdown or By Species tab.
7. **DONE — Outlier price warning.** Built in v0.9.19 (compile-fixed in
   v0.9.20 — a missing C++17 standard setting in `CMakeLists.txt`,
   unrelated to the feature logic itself; wording corrected in
   v0.9.21 — the original message claimed to show "other entries
   range," but the numbers shown were actually Tukey's fences, which
   deliberately sit outside that real range; two real fixes in v0.9.23
   from Jack's own live testing — see below), awaiting test
   confirmation (see checklist above). Final design, decided across
   several conversations after investigating (and rejecting most of) an
   external AI suggestion for the surrounding UI work:
   - **Range method**: Tukey's fences (IQR-based, `Q1 - 1.5*IQR` to
     `Q3 + 1.5*IQR`) - chosen over plain min/max after discussing real
     daily volume (100-500 entries/day, ~250 species on file, ~50 used
     daily, some species as few as 5 sales/day and some 100) - IQR
     degrades sensibly at both ends of that range where min/max
     wouldn't. Minimum baseline: 4 other same-day entries for that
     species before the check runs at all.
   - **IQR floor, added v0.9.23 - a real bug, not a tuning tweak**:
     found via live testing - five identical $10 entries then a
     genuinely normal $12 sixth entry triggered the warning, because a
     zero-spread baseline gives IQR=0, collapsing the fence to exactly
     the baseline price with zero tolerance for ANY deviation. Fixed by
     flooring the IQR used in the fence at 20% of the baseline's own
     median price (chosen with Jack against his exact numbers - $10
     baseline, fence becomes $7-$13, $12 passes, a genuine $50 typo
     still doesn't). Only ever widens the fence for low-variance data;
     doesn't change anything for a baseline that already has real
     spread.
   - **Scope**: all suppliers pooled per species per date, not
     same-supplier-only - confirmed given the business is a consignment
     agency achieving one market price per species per day, not a
     per-supplier price (see BUSINESS_RULES.md).
   - **Known, accepted gap — narrowed in v0.9.22, not fully closed**:
     originally, a typo on the very FIRST entry of a species that day had
     no baseline to be checked against, and stayed silently wrong even
     once later entries gave it a real baseline - nothing ever went back
     and re-asked "does this old entry still look right?" **v0.9.22
     fixes exactly that part**: every commit, delete, or undo-delete now
     silently re-checks the WHOLE species/date group, not just the one
     entry that changed - confirmed against Jack's own real numbers
     (bonito at $1111/$11/$11/$11/$111 - the $1111 entry gets correctly
     flagged once the group reaches 5). What's still NOT closed: this is
     still same-day-only, by design - a species with a bad first entry
     and never more than 3 OTHER entries that day still can't be
     checked at all (below the minimum baseline), regardless of
     ordering. Properly closing that needs cross-day history - item 5's
     territory (and realistically Bucket C's). Jack raised this
     explicitly as a possible future use for historical data, not
     committed to yet.
   - **The flag is "live" at the group level, not just per-entry**
     (v0.9.22): `ReevaluateOutlierFlagsForSpeciesOnDate` re-runs the
     leave-one-out check for every entry in a species/date group after
     anything that changes that group's prices - not just the entry
     being committed. This means a flag is a live reflection of the
     CURRENT data, not a permanent decision: an entry manually cleared
     (right-click "Clear flag", or "No" in `ReviewOrEditEntry`) can be
     silently re-flagged later if a subsequent change to a SIBLING
     entry's price makes it look unusual again. Deliberate, not an
     oversight - discussed directly with Jack, who wants exactly this
     ("that first outlier will allow every entry to be wrong").
   - **UI: no interactive dialog at Add Entry, as of v0.9.23** - the
     original design showed a Yes/No `MessageBox` at commit time (a
     custom-captioned dialog like "Force Save Anyway" was considered
     and rejected in favor of matching every other dialog in the app).
     Removed at Jack's explicit request after live use: a modal
     interrupting every flagged entry broke his keyboard-driven
     data-entry flow at real volume. `CommitEntryForm` now silently
     sets the flag and commits - never blocks. The double-click/Edit
     Selected review dialog (`ReviewOrEditEntry`) is unchanged and is
     now the ONLY interactive dialog left in this feature; it remains
     the deliberate "I'm looking at this row" review path. Flagged
     rows get a warning-glyph marker in the Price cell plus the whole
     row tinted (both, after comparing mockups - marker-only was
     considered and rejected in favor of the fuller visual).
   - **`priceFlagged` persists** as a 7th `.fbd` field, backward
     compatible with older files. See CHANGELOG.md's `[0.9.19]` entry
     for the full technical detail.
8. **Print/PDF for Overview & By Species tabs** — currently Print Preview
   and printing only cover the Breakdown tab.
9. **Filter/search on the summary tabs** — currently only the raw Entries
   list has a filter box.
10. **Encrypt `.fbd`/`emails.txt`/`settings.txt` — v2 only, explicitly
    staying as plaintext for v1 (Jack: "for v1 we can keep it as is").**
    Full design finalized 2026-09-05 in NETWORK_ARCHITECTURE.md: a
    shared business-level key (not per-machine DPAPI, which was
    considered and ruled out once multi-machine access became a
    confirmed requirement - see that doc for why), cached locally per
    machine via DPAPI so daily use needs no password typing, with
    distribution and access gated by an AD security group, and a
    physical break-glass recovery copy of the shared password. Also
    covers the future SQLite reporting layer, one scheme for both. Now
    part of **Bucket C** (see "Definition of done" below), not a
    standalone item - sequenced with the multi-machine work since a
    coherent answer needs to cover both together.
11. **Dark/light theming — scoped 2026-09-06, built in v0.9.34-v0.9.38,
    removed entirely in v0.9.39.** Built as a manual-only Light/Dark
    toggle, then patched five times (native chrome, tab strip, menu bar,
    status bar) trying to match what Jack wanted - each fix missed
    something else, and the tab strip fix in particular was documented as
    working when it never actually was. Jack asked to scrap it and revert
    to the v0.9.33 baseline rather than keep patching - see CHANGELOG.md's
    `[0.9.39]` entry. Everything below this point is the original,
    pre-build scoping writeup, kept as-is for whenever a "v2" is picked up
    from a clearer mockup of what's actually wanted. Raised via
    an external AI (Gemini) suggestion for modernizing the UI; that
    suggestion was investigated against the real codebase before being
    accepted and turned out not to be usable verbatim - two of its four
    snippets would produce a duplicate-case-label **compile error** as
    given (this app already has a `WM_NOTIFY` handler with
    `NM_CUSTOMDRAW` bold-Totals logic, and a `WM_CTLCOLORSTATIC` handler
    doing the green/red Debtor/Cash balance coloring - the suggestion
    assumed neither existed), and its manifest change would have
    silently reintroduced the exact embedded-vs-external-manifest
    conflict `ARCHITECTURE.md` already documents fixing once (the
    deleted `app.rc`). **This item is Claude's own write-up of the real
    scope, using Gemini's four phases only as a naming/direction guide**
    - Jack's explicit instruction was "gemini's is a guide, not
    verbatim," and the code, when built, will be written against this
    app's actual layout/font/custom-draw machinery, not pasted from the
    suggestion.
    - **User-facing behavior**: a Light/Dark theme, switchable three
      ways - manually (a menu setting, persisted in `settings.txt` like
      window position already is), automatically following Windows'
      own light/dark setting, or automatically by time of day. Exact
      default and whether all three coexist (e.g. "Auto" meaning
      "follow Windows" specifically, with time-of-day as a separate
      option) - open question, needs Jack's decision before building.
    - **Belongs as a menu item, not a tab** — per `ARCHITECTURE.md`'s
      tab-vs-menu principle, this doesn't answer "did today balance,"
      so it's a Tools or a new View menu entry, following the same
      popup-window-free pattern as a simple preference toggle.
    - **What already exists and doesn't need rebuilding**:
      - ComCtl32 v6 (needed for any modern control rendering) is already
        requested via the external `app.manifest` - Gemini's linker
        `/manifestdependency` pragma is redundant *and* actively
        dangerous (see above) and must not be added.
      - A font system already exists (`g_normalFont`/`g_boldFont`, built
        once from `NONCLIENTMETRICS` at startup, applied via
        `WM_SETFONT`, cleaned up on shutdown) - **do not add a second,
        parallel font system** the way Gemini's `ApplyShellTheme` does
        (which also leaks a `CreateFontW` handle every time it'd be
        called). **Font choice is an explicit open discussion for when
        this is picked up, not decided here** - keep the current
        system-metrics font (`NONCLIENTMETRICS`, respects whatever the
        user's actually configured in Windows, zero extra risk) vs. a
        fixed choice like Segoe UI Variable (more deliberately "modern"
        look, but ignores the user's own Windows font/scaling settings,
        and needs its own availability fallback on older Windows
        versions where it isn't installed) - **Claude's starting lean is
        toward keeping the current `NONCLIENTMETRICS` approach**, but
        this should be a real pros/cons conversation before building,
        not defaulted to silently.
      - `NM_CUSTOMDRAW` on the report ListViews already exists (bolds
        "Total" rows) - theming extends this handler, it doesn't add a
        second one.
    - **What's genuinely new**:
      - `ThemeMode { Light, Dark }` + a theme-colors struct (background,
        card/panel, primary/secondary text, border, zebra-row color),
        similar in shape to Gemini's `AppTheme` but as static data, not
        a redesign of how controls are created.
      - `AppSettings` gains a theme-mode field, persisted/restored the
        same way window position already is.
      - Windows' own light/dark setting can be read from the registry
        (`HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\
        Personalize\AppsUseLightTheme`) - **v1 should just check this
        once at startup**, not live-track it; reacting live to the user
        flipping Windows' setting mid-session (via
        `WM_SETTINGCHANGE`/`"ImmersiveColorSet"`) is a reasonable v2
        stretch, not a v1 requirement.
      - Title bar/frame dark mode via `DwmSetWindowAttribute` with
        `DWMWA_USE_IMMERSIVE_DARK_MODE` (value `20` on current Windows
        10/11 - define it locally rather than relying on a specific SDK
        header version) - **requires linking `dwmapi.lib`
        (`-ldwmapi` for MinGW)**, which none of the three build paths
        (`CMakeLists.txt`, `build_msvc.bat`, `build_mingw.bat`) do today
        - a real, if small, build-script change needed alongside the
        code.
      - `WM_CTLCOLORSTATIC`/`WM_CTLCOLOREDIT` extended (not replaced) in
        all three `WndProc`s (main window, Manage Names, Print Preview)
        to theme labels/edit fields while preserving the existing
        green/red diff-balance coloring logic, now theme-aware instead
        of hardcoded `RGB()` values.
      - `NM_CUSTOMDRAW` extended (not replaced) on all four report
        ListViews, plus Manage Names' list, for theme-aware text/row
        background colors and optional zebra striping, layered on top
        of (not instead of) the existing bold-Totals logic.
      - Any painted panel/"card" background must use `S()`-scaled
        coordinates recomputed inside the existing `LayoutAll()`/each
        popup's `WM_SIZE` handler, never fixed pixel literals the way
        Gemini's `RECT cardRect = {16, 50, 800, 180}` example was - this
        app's controls are repositioned dynamically on every resize, so
        a fixed-position painted decoration would drift out from under
        them exactly the way the v0.8.0 DPI bug happened.
      - **Known Windows limitation worth deciding on up front**: native
        ListView headers, scrollbars, and combo dropdown lists don't
        reliably support dark mode through documented APIs alone - real
        dark-mode chrome (the way Explorer/Notepad do it) uses
        undocumented calls (`SetWindowTheme` with
        `"DarkMode_Explorer"`/`"ItemsView"`, `uxtheme.dll`'s unexported
        `SetPreferredAppMode`). Decide before starting whether v1 ships
        with light-styled headers/scrollbars on an otherwise dark body
        (documented APIs only, lower risk) or takes on the undocumented-
        API route for full native chrome theming (higher risk, harder
        to verify without a compiler).
    - **Sequencing recommendation, not yet confirmed with Jack**: this
      is single-machine UI/UX work, so it's **Bucket A scope**, not
      gated on Bucket C. But it's also the most visually invasive,
      hardest-to-verify-without-a-compiler change proposed for this app
      so far - touches all three `WndProc`s, the layout code, and every
      ListView's custom draw. Recommend building it **after** items 6-9
      (smaller, already-spec'd, lower-risk wins) rather than folding it
      into the same batch of changes, and building it in its own small,
      individually-buildable increments the same way Bucket B plans to
      (e.g.: manual toggle + basic background/text colors first and
      confirmed working, *then* Windows-setting/time-of-day auto-
      detection, *then* ListView zebra striping, *then* DWM title-bar
      theming) rather than one large change.

## Definition of "done" for this app (decided 2026-09-01, sequencing
extended 2026-09-05)

Three finish lines now, not two - **sequenced A, then C, then B**:

- **(A) Stable and complete for single-machine daily use** — every item
  in "Next up" above is fixed/built, the app is audit-clean, `main.cpp`
  stays large but working. **← this is the active goal right now.** This
  is deliberately what "usable" means in the near term: staff can start
  using the app on a single machine (or with informal, manually-
  coordinated sharing) without waiting for the much larger effort below.
- **(C) Multi-machine, encrypted, shared access** — see "Bucket C" right
  below. Full design already worked out in NETWORK_ARCHITECTURE.md,
  covering multiple staff/machines working the same day's data,
  encryption at rest, and where it's all hosted. **Not active yet** —
  sequenced immediately after (A), ahead of (B), because it's a real,
  concretely wanted business capability (multiple staff, multiple
  machines), not an internal code-quality concern - decided 2026-09-05
  specifically so getting the current app usable isn't blocked by this
  larger undertaking.
- **(B) Architecturally clean too** — (A) plus the full `main.cpp`
  decomposition. **Still last**, now for an added reason beyond the
  original one (a refactor deserves a settled codebase and the test
  safety net to refactor against): a chunk of (C)'s work - real network
  I/O, file locking, a new read-only UI mode - is exactly the kind of
  code that benefits from the cleaner persistence-layer separation (B)
  would provide. Doing (C) first, then (B), means the decomposition can
  be shaped around what (C) actually needed, rather than guessing ahead
  of time.

## Bucket C: multi-machine, encryption, and shared hosting (designed
2026-09-05, not yet built)

Full design, reasoning, and explicitly-rejected alternatives in
**NETWORK_ARCHITECTURE.md** — this section is just the build sequence.
Proposed internal order, with dependencies noted:

1. **Day-rollover with dated filenames** — this is item 3 in "Next up"
   above, already sequenced into Bucket A since it has standalone value
   even single-machine (cleaner "did today balance" checking) - flagged
   here again because Bucket C's day-finalization trigger (for both
   SQLite ingestion and the single-writer chain's trustworthiness)
   depends on it existing first.
2. **Hosting infrastructure** — stand up the chosen option (NAS,
   self-built Linux/Samba box, or cloud-hosted SMB), join it to AD,
   configure share permissions gated by a security group, set up
   VPN/Tailscale if remote access is wanted. This is systems
   administration, not C++ development - can happen in parallel with
   Bucket A's remaining app-side work rather than waiting for it.
3. **Encryption layer** — shared key, DPAPI local caching, the one-time
   per-machine setup flow. Reasonable to build and test against a local
   file first, before the network layer exists.
4. **File locking + automatic read-only fallback** — the core
   multi-machine mechanic once a real network location exists to test
   against.
5. **Read-only UI mode** — status bar indicator, disabled controls, live
   polling refresh. Depends directly on (4) existing first.
6. **SQLite hybrid ingestion + local read-only replica caching** —
   depends on (1) for the finalization trigger and (4) for a trustworthy
   single-writer chain (see NETWORK_ARCHITECTURE.md for why the locking
   design is what makes the ingested data reliable, not just the live
   view).

## Phase 2 (after Bucket A/"done" above): main.cpp architecture decomposition

**Not active yet — sequenced deliberately after (A) above, not a
maybe-someday backlog item.** `main.cpp` has grown large and mixes several genuinely
separate responsibilities (domain model, file persistence, report
aggregation, CSV/email generation, printing, and three separate window
procedures) in one file. An external audit's diagnosis of this was that
the coupling between UI refresh, document state, and autosave is the
*root cause* of several of the data-loss bugs already found and fixed
(see SecurityHardeningRegister.md #7–#9) — so a decomposition isn't just
a tidiness exercise, it's expected to make this whole class of bug
structurally harder to reintroduce. `main.cpp` sitting at ~2,700 lines
today with several distinct responsibilities tangled together is the
concrete reason this is on the plan, not a hypothetical.

Sequenced to start once:
- The automated test suite above exists and covers the current behavior
  (characterization tests), so a refactor can be verified against a real
  safety net instead of manual re-testing alone.
- The remaining audit items above are fixed, so bug-fixing and
  large-scale file reorganization don't happen in the same changes
  (mixing the two makes it much harder to prove a fix actually fixed
  something, and easier to scatter or lose a fix mid-move).

When we do get here, decompose by ownership/responsibility rather than by
line count, and keep `main.cpp` itself as thin composition/bootstrap code
once done. Rough shape to evaluate at the time (not committed yet):
domain/document model, `.fbd` parsing + persistence, report aggregation,
CSV/email export, printing, and one file per window procedure (main
window, Manage Names, Print Preview). Do this as a series of small,
individually buildable/testable extractions — not a single big-bang
rewrite — so each step can be verified before the next one starts.

## Explicitly decided against (for now)

- **GST toggle** — not needed; fresh fish is GST-free in Australia.
- **Automatic/SMTP email sending** — deliberately avoided; would require
  storing email credentials in the app, which is a bigger security surface
  than this tool should take on. The mailto-draft approach (open in your
  own email app, review, send yourself) was chosen instead.
- **Full keyboard shortcut set** (Ctrl+S, Ctrl+P, etc.) — considered
  early on and declined in favor of other priorities; could revisit later
  if wanted.

## Ideas not yet scoped / prioritized

Raised at various points but not committed to the roadmap above:
- Multi-sheet comparison (e.g. this week vs last week side-by-side)
- Notes/remarks at the sheet level (vs. per-entry, which already exists)
- A "Today" dashboard summary tab
- File association so double-clicking a `.fbd` file opens the app
- An installer (e.g. Inno Setup) if this is ever shared beyond one machine
