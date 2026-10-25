# Roadmap

This is the current plan: where we are, what is next, what is not built, and what is known broken. The long history
(every version's test checklist, the 0.9.x status notes) is frozen in `ROADMAP_ARCHIVE.md`; version-by-version detail
is in `CHANGELOG.md`; manual checks still to do are in `Testing.md`.

## Where we are (v0.12.0, 2026-10-24)

The app is complete and in use for single-machine daily work: data entry, four report tabs, Finalize Day, autosave,
rolling backups, restore, print/PDF (Breakdown tab), CSV export, supplier emails, outlier-price flagging, Books (v0.12.0: mark where each Debtor/Cash book starts, check per-book totals), 267 portable
and 25 Windows integration tests.

**Current project: splitting `main.cpp` (was ~5,000 lines, now ~2,860) into `src/core`, `src/data`, `src/services`,
`src/forms`.** Behaviour should not change; each stage is its own version, built and tested by Jack before the next.

| Stage | What | Version | State |
|---|---|---|---|
| 1-5 | layout, shared state, data/, services/ (print, export, email), popups | 0.10.0-0.10.7 | done, verified |
| 6a / 6b | main window lists; layout, tab switching, control helpers | 0.10.8 / 0.10.9 | done, verified |
| 6c / 6d | WM_CREATE / WndProc / wWinMain | - | skipped on purpose: WinUI 3 would replace them |
| 7a | `.fbd` file text built by tested core code (`BuildFbdText`) | 0.10.12 | done, verified |
| 7b | autosave / rolling-backup disk write to `src/data/Backups.*` | 0.10.14 | done, verified |
| 7c | Open and Recent Files share `OpenNamedFile` (rest of file operations reviewed: nothing else worth moving) | 0.10.17 | done, verified |
| 7d | Finalize / Un-finalize persistence | - | reviewed, nothing left to move: the coordination is already the tested core `CoordinateFinalizeDay` (0.9.51); what remains in `ExecuteFinalizeDay` is glue that must touch the lock state, title and lists |
| 7e | entry commit / duplicate / delete / undo validation separated from controls | - | dropped for now: the validation rules are already pure core functions (`ValidateWeightKg`, `ValidatePriceDollars`, parsing); the rest reads and writes controls directly, so separating it is the front-end rewrite WinUI 3 would do anyway |

Fixes found along the way: Finalize Day now marks the open saved file as finalized (0.10.13); Restore from Backup no
longer fails silently (0.10.15) and always opens in `backups\` (0.10.16); `*.fbd` is git-ignored (0.10.17).

## Next, in order

1. **Decide: call the split finished and move on.** Stages 7d/7e have nothing worth moving (above); `BuildFbdSaveContent` still gathers its values from the controls, which is front-end territory. Remaining `main.cpp` (~2,860 lines) is the window code a front-end replacement would swap out.
2. Jack works through the open manual checks in `Testing.md` ("Carried-over checks") so the app's verified state is on record (Finalize Day full pass is already confirmed, 2026-10-17).
3. **WinUI 3 prototype (in progress, separate folder `FishBalanceWinUI`, not in this repo yet)** - Data Entry and Total Overview tabs, Open/Save/Save As, Finalize/Un-finalize with lock and banner, unusual-price flag, Different Date warning, Kgs sums, Enter-to-add, Books (build 5a: Book box + Start Book menu, Book column, multi-select with selected-rows total, Books table on Overview; needs the v0.12.2 core); reuses `src/core` and `src/data` unchanged. Build 5a. Approved by Jack; was: only with his go-ahead. Small, throwaway-able; reuses `src/core` and
   `src/data`. May make the stage 6c/6d work and the combo-box glitch (below) moot.
4. **0.12.0 built, awaiting Jack's test: Books** (Win32 + core; WinUI prototype next after sign-off). See CHANGELOG and Testing.md "v0.12.0 checks". Open follow-ups: type/rename book names by hand, Book column in CSV/print, books in WinUI, whether the layout of the Book box / Start Book button needs moving (Jack is thinking about it).
   0.11.0 done: Kgs can be typed as a sum and the sum is remembered** (Win32 + core; also in the WinUI prototype). Next feature candidates, none started (specs below): price history per species,
   cheapest-supplier highlight, print/PDF for the Overview and By Species tabs, filter on the summary tabs.
5. **1.0** only when Jack decides it is ready for prime-time release.

## Not built yet

- **Price history / trend per species** (item 5 spec below) - table first, chart second.
- **Highlight cheapest supplier per species** (Breakdown or By Species tab).
- **Print / PDF for the Total Overview and By Species tabs** - only Breakdown prints today.
- **Filter / search on the summary tabs** - only the entries list has a filter box.
- **Multi-machine, encryption, shared hosting (Bucket C)** - designed in `NETWORK_ARCHITECTURE.md`, nothing built.
- **Encrypting `.fbd` / `emails.txt` / `settings.txt`** - v2 only; plaintext by decision for v1.
- **Audit items still open** (`AuditFindings_2026-09-30.md`): F7 (data files live next to the exe, not in
  `%LOCALAPPDATA%`), F16-F22 (unchecked Win32 call results, message-loop error, no monitor clamp for the main window
  on restore, no `WM_DPICHANGED`). The F16-F22 group is Win32 plumbing that WinUI 3 would replace - skip if that goes ahead.
- **Ideas, not scoped:** multi-sheet comparison, sheet-level notes, a Today dashboard tab, double-click `.fbd` file
  association, an installer, a dark theme (built then removed in 0.9.34-0.9.39).

## Known problems

- **Supplier / Species combo boxes sometimes paint a grey strip** between the text area and the arrow on first
  display (intermittent). Four code fixes (0.9.42-0.9.46) and a clip-style experiment (0.10.10, reverted in 0.10.11, made
  the combos vanish) did not cure it; the workaround code (`FixComboBoxFirstPaint`, timers) is still in. It needs a
  diagnostic build, not another guess; WinUI 3 would likely remove it. See `ARCHITECTURE.md`.
- **Never built or run by Jack:** the MinGW build (`build_mingw.bat`) and the CMake build. Only MSVC (batch file or
  Visual Studio) is exercised. Keep them in step with source-file changes; update this line the first time they are built.

## Decisions on record

- Versioning: the split and its fixes are 0.10.x; next feature 0.11.0; 1.0 = release-ready.
- Layering (`src/README.md`): forms -> services -> data -> core; nothing points back up. No Windows UI call in core/data/services.
- Every new `.cpp` goes into all four build paths (`build_msvc.bat`, `build_mingw.bat`, `CMakeLists.txt`,
  `tests/win32_integration/run_integration_tests.ps1`).
- Sequencing changed: the original plan did the multi-machine work (Bucket C) before the `main.cpp` split. In practice
  the split is being done first; Bucket C will be shaped by the split code rather than the other way round.
- Finalize Day: workflow, rules and design are in `BUSINESS_RULES.md` and `CHANGELOG.md` `[0.9.40]` (built; not detection-based).
- Outlier price flag: Tukey fences, IQR floor of 20% of the median, silent flag, live per species/day - see `CHANGELOG.md` `[0.9.19]`-`[0.9.23]`.

## Explicitly decided against (for now)

- **GST toggle** — not needed; fresh fish is GST-free in Australia.
- **Automatic/SMTP email sending** — deliberately avoided; would require
  storing email credentials in the app, which is a bigger security surface
  than this tool should take on. The mailto-draft approach (open in your
  own email app, review, send yourself) was chosen instead.
- **Full keyboard shortcut set** (Ctrl+S, Ctrl+P, etc.) — considered
  early on and declined in favor of other priorities; could revisit later
  if wanted.

## Specs for features not yet built

(Carried over unchanged from the old "Next up" list, so the item numbers 5, 6, 8, 9, 10 and the cross-references between them
are the old numbers: item 3 = Finalize Day, item 7 = outlier warning, both built - see `ROADMAP_ARCHIVE.md` and `CHANGELOG.md`.)

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

## Bucket C: multi-machine, encryption, shared hosting (designed 2026-09-05, not built)

Full design, reasoning, and explicitly-rejected alternatives in
**NETWORK_ARCHITECTURE.md** — this section is just the build sequence.
Proposed internal order, with dependencies noted:

1. **DONE — Finalize Day** — this was item 3 in the old "Next up" list, built in
   v0.9.40. Flagged here again because Bucket C's SQLite ingestion (item 6
   below) and the single-writer chain's trustworthiness depend on the
   `history\<date>.fbd` convention it established.
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
