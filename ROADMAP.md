# Roadmap

This file is the single source of truth for "what's built, what's being
tested, and what's next." Check here first if you've lost track of where
things stand — that's exactly what this file is for.

## Status: v0.9.16 — awaiting test feedback

v0.9.0 (Date/Notes/Duplicate Last Entry) is confirmed working — see below.
v0.9.1–v0.9.15 were never separately confirmed as final before being
superseded (v0.9.9 additionally failed to compile, fixed in v0.9.10).
v0.9.16 is a superset of everything and is what should actually be
tested now (see CHANGELOG.md and SecurityHardeningRegister.md for full
detail on each fix).

### Confirmed
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
- [x] Focus-loss autosave trigger (v0.9.15) — implicitly confirmed
      working; testing this is exactly what surfaced the v0.9.16
      subtraction bug, which required the save/reload round-trip to
      have already worked correctly

### New in v0.9.16 — needs its own first check

- [ ] **Debtor/Cash subtraction now works correctly.** Type an
      expression with a minus in it - e.g. `123+11-21` - into Debtor or
      Cash and confirm the Book Total reflects the correct result
      (113 for that example, not 134). This was a real, silent bug
      found via your own testing, not a hypothetical.

### New in v0.9.15 — needs its own first check

- [ ] **Autosave now fires on losing focus, not every keystroke** - the
      main visible difference: type into any field (Supplier, Species,
      Kgs, Price, Notes, Debtor, Cash) and Tab or click to the next field
      - the draft/Debtor/Cash should still persist correctly across a
      force-close, same as v0.9.14's test, just triggered differently
      now. Also worth a general "does normal data entry still feel
      right" pass, since this touches every field in the main form.
      Live on-screen totals (Debtor/Cash) and Supplier/Species
      autocomplete suggestions should still update immediately per
      keystroke, unchanged - only the *disk write* moved to focus-loss,
      not the on-screen feedback.

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
4. **Timestamped backups** — right now autosave overwrites a single file;
   add a rolling history of backups you can recover from if something
   gets overwritten by mistake.
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
7. **Warn if a price looks like a typo/outlier — spec'd out 2026-09-04,
   genuinely simple, no new architecture needed.** Concrete example from
   Jack: "if 90% of all Blue Grenadier sold today was between $5-$10 and
   someone types $25, that's probably a typo." Scope, corrected from the
   original draft: this only needs **today's data**, already sitting in
   `g_entries` in memory - no file I/O, no cross-day queries, no
   dependency on the flat-file-vs-SQLite decision or on item 5's Price
   History feature. Could be built as a standalone quick win independent
   of everything else on this list.
   - **Mechanism**: on committing a new entry (`CommitEntryForm`),
     before accepting the price, gather every other already-added
     entry's price for the same species today from `g_entries`. If
     there's a reasonable baseline (some minimum count - e.g. at least
     2-3 existing entries for that species today, so a single prior
     entry can't itself define "normal"), compute a plausible range
     (open question: simple min/max of what's already there, vs. a
     percentile/stddev-based range less thrown off by an earlier
     outlier) and check the new price against it.
   - **Soft warning, not a hard block** - e.g. "This price ($25.00) is
     unusual for Blue Grenadier today (other entries range $5.00–
     $10.00). Add anyway?" with a way to proceed regardless, since a
     genuinely unusual-but-correct price should never be un-enterable.
   - **Open question**: compare within the same supplier only, or across
     all suppliers for that species today? Leaning toward all suppliers
     (a bigger, more stable baseline on a normal day), but worth deciding
     before building.
   - No dependency on item 5 or the architecture decision - this can be
     picked up any time.
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
