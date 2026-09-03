# Roadmap

This file is the single source of truth for "what's built, what's being
tested, and what's next." Check here first if you've lost track of where
things stand — that's exactly what this file is for.

## Status: v0.9.12 — awaiting test feedback

v0.9.0 (Date/Notes/Duplicate Last Entry) is confirmed working — see below.
v0.9.1–v0.9.8 were never separately confirmed before being superseded.
v0.9.9 additionally failed to compile (`error C2589`, a `<windows.h>`
min/max macro collision - fixed in v0.9.10). **The v0.9.10 build itself
is now confirmed clean** (compiles successfully). v0.9.12 is a superset
of everything and is what should actually be tested now (see
CHANGELOG.md and SecurityHardeningRegister.md for full detail on each
fix).

### Confirmed this round
- [x] Focus after Add/Update Entry lands on Supplier
- [x] Status bar shows version + filename correctly
- [x] Print Preview opens normally, looks correct

### Still needs testing (carried over, not yet confirmed)
- [ ] **Atomic/checked saves** - see the read-only-`emails.txt` test
      described in chat; also just confirm normal Save/Save As/CSV
      export/autosave all still work exactly as before.
- [ ] **Supplier email survives a rename/merge** - now with an added
      wrinkle to test, see below.
- [ ] **Debtor/Cash autosaves without another action** - type into
      Debtor/Cash, wait ~1 second (no need to click Add Entry or File >
      Save), close and reopen - the value should persist.

### New in v0.9.11/v0.9.12 — needs its own first check

- [ ] **Combo box first-paint fix - now with an actual investigated,
      evidence-based root cause, not just another blind patch.** A
      systematic diff between v0.9.3 (working) and v0.9.10 (broken)
      found one concrete change: the status bar (added v0.9.8) was being
      created immediately before the Supplier/Species combo boxes,
      whereas they used to be among the very first controls created.
      Reordered creation so the status bar is created last, restoring
      that original order - see CHANGELOG.md's `[0.9.12]` entry for the
      full reasoning, including the honest caveat that this is a
      well-evidenced hypothesis, not a certainty (confirming the exact
      mechanism would need a live debugger, which isn't available here).
      The v0.9.11 belt-and-suspenders `RedrawWindow` fixes are still in
      place too either way. **Please give a clear yes/no this time**:
      close the app completely, relaunch fresh, look at Supplier/Species
      *before touching anything* - fixed or not?
- [ ] **Merging suppliers with different saved emails now asks which to
      keep.** Save two different email addresses for two different
      suppliers via Manage Names, select both in the list, merge them
      into a new/existing name, and click Apply - should get a Yes/No/
      Cancel prompt naming both addresses before anything else happens.
      Try Cancel too - nothing should change (no rename, no email
      change) if you cancel at that prompt.

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
- [ ] **General feel of the app** — one real bug found and fixed this
      round: the Manage Names hint text was visibly cut off (see
      CHANGELOG.md's `[0.9.6]` entry). Also flagged, not yet resolved:
      Supplier/Species fields aren't cleared after Add Entry (they stay
      populated from the last entry, only Kgs/Price/Notes clear) — this
      is confirmed to be **pre-existing behavior, unchanged since before
      any of this work started**, not a regression, and matches the
      documented Tab/Enter batch-entry workflow in README.md (likely
      intentional, so several rows for the same supplier can be entered
      quickly) — awaiting a decision on whether this should change.
- [ ] **Clean build check**: rebuilding (CMake, MinGW, or MSVC) produces
      zero warnings and zero errors — **not yet reconfirmed for the main
      app specifically.** What's been confirmed clean since the
      `C4530`/`C4701`/`C4996` fixes is the *separate* `tests/`
      test-suite build (`run_tests.ps1`), not a fresh "Build All" of the
      actual `FishBalanceManager` project itself — please rebuild that
      one too to confirm.
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

1. **`RefreshAll()` performance at scale** — it fully rebuilds every
   report view and does a synchronous disk write on every single Add /
   Edit / Delete, with no reset mechanism except manually starting a new
   file. Worth addressing before or alongside timestamped backups below,
   since backups on top of a growing-slower file don't fix the underlying
   scaling problem.
2. **Timestamped backups** — right now autosave overwrites a single file;
   add a rolling history of backups you can recover from if something
   gets overwritten by mistake.
3. **Price history/trend per species over time** — now unblocked by the
   Date field. Likely a new tab or view showing how a species' price has
   moved across saved sheets/dates.
4. **Highlight cheapest supplier per species** — surfaced on the
   Breakdown or By Species tab.
5. **Warn if a price looks like a typo/outlier** — compare an entered
   price against recent history for that species before accepting it.
6. **Print/PDF for Overview & By Species tabs** — currently Print Preview
   and printing only cover the Breakdown tab.
7. **Filter/search on the summary tabs** — currently only the raw Entries
   list has a filter box.

## Definition of "done" for this app (decided 2026-09-01)

Two possible finish lines were on the table; **this project's target is
the first one**:

- **(A) Stable and complete for daily use** — every item in "Next up"
  above is fixed/built, the app is audit-clean (no known data-integrity
  issues left open), `main.cpp` stays large but working. **← this is the
  active goal right now.**
- (B) Architecturally clean too — (A) plus the full `main.cpp`
  decomposition below. **Not active right now, but expected eventually**
  — `main.cpp`'s size is a real, acknowledged problem, not a hypothetical
  one; this isn't "only if it ever becomes an issue," it's "after (A),
  deliberately, so bug-fixing and large-scale reorganization don't happen
  in the same changes." (A) comes first because it's the higher-value,
  better-scoped work and because a refactor deserves the test safety net
  and a settled codebase to refactor against, not because (B) is in
  question.

So: once every item in "Next up" above is checked off, (A) is done, and
the decomposition below becomes the active next phase rather than
backlog.

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
