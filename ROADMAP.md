# Roadmap

This is the current plan: where we are, what is next, what is not built, and what is known broken. The long history
(every version's test checklist, the 0.9.x status notes) is frozen in `ROADMAP_ARCHIVE.md`; version-by-version detail
is in `CHANGELOG.md`; manual checks still to do are in `Testing.md`.

## Where we are (v0.24.5, 2026-11-25)

**v0.24.x (email notes and signature) tested OK by Jack on both apps, 2026-11-25.** What is in it: a per-supplier notes dialog shown every time before the emails are made (NOT SOLD / UP / DOWN, free text, starts blank, Skip asks first if notes were typed); a signature kept separate from the sign-off (several lines, under the sign-off); a warning before a second set of emails for the same sheet. WinUI and Win32 both have all three. Gmail drafts are the mode in daily use. **Still to check (needs the other machine):** whether mailto and .eml mail programs add their own signature on top of ours (Testing.md "v0.24.0 checks"). v0.24.4 (Win32 Settings > Email page was cut off) is written, not yet run by Jack.

**Before that:** v0.23.4 (WinUI Restore from Backup, MinGW/CMake builds removed) and v0.23.3 (Phase A housekeeping) tested OK; Win32 and WinUI regression passes clean (2026-11-23); WinUI Print dialog, Undo Delete, About, Recent Files, window position and Export to CSV done (v0.23.1-0.23.2). Both apps share `src/`, `version.h` and the data folder `C:\BalancingAct\data`; WinUI is the main app, Win32 is still used (D1). 353 unit tests.

Older status notes (v0.10 to v0.23.2) were moved to `ROADMAP_ARCHIVE.md` on 2026-11-25.

**Design note to settle with the shared-repository (Postgres) work - names, aliases and emails are reference data (Jack, 2026-11-06; no actual lists belong in this file or the repo):**
- Eventually shared tables (suppliers with email, species, aliases) read by every machine and by the analysis side; `names.txt` / `emails.txt` are the simple version of the same data.
- Put all reading/writing of them behind one small data-layer interface (file today, database later) so the location becomes a setting; both apps already share `src/data`. DECIDED (Jack, 2026-11-06): `names.txt`, `emails.txt`, `settings.txt` and `recent.txt` live in `C:\BalancingAct\data` (v0.13.1, Win32; WinUI reads the same `emails.txt`, reads `names.txt` too from step 9).
- Keep a local cached copy per PC so a machine still works when the share/server is down (the Win 7 box especially).
- Several machines: adding a name should read-merge-write atomically (adds combine); rename/merge is riskier, so allow Manage Names on one PC only.
- Installed under Program Files, nothing may be written beside the exe: autosave, settings, recent files, backups, history and these lists all move to a ProgramData (or AppData) folder at install time - separate roadmap item.
- Analysis wants more than clean spelling: never delete aliases (old files map through them on import); room for dates/retired flags; many names embed a form (frozen, smoked, live, whole, H&G, fillets), so later split into base species + form.

## Next, in order (full audit 2026-11-20, v0.23.2)

The old "Next" list (split stages, WinUI prototype steps) is finished or history; it is in `ROADMAP_ARCHIVE.md` and `CHANGELOG.md`. This is the plan from here. Each phase can be done in any order unless a note says otherwise.

**Phase A - housekeeping (ALL DONE, closed 2026-11-25)**
- A1. DONE (checked on GitHub 2026-11-25). Root-folder duplicates of `FishBalanceCore.h` and `FishBalanceWin32IO.h` are OLD copies (1,739 vs 2,503 lines for Core). Nothing builds from them (`main.cpp`, `src/` and the tests all include the `src/` versions), but a future include could silently pick up stale code. `FishBalanceControlLimits.h` at the root is an identical copy. Delete the three root copies (`git rm`); `tools/fbd_breakdown.cpp` already builds with `-I src/core`.
- A2. DONE (checked on GitHub 2026-11-25). The `docs/` folder holds older copies of `CHANGELOG.md` and `ROADMAP.md` (about 130 lines behind). Decide whether it is needed; if not, delete it so there is one source of truth.
- A3. DONE (checked on GitHub 2026-11-25). `COMMIT_MESSAGE.txt` is stale (still v0.13.0); `push_update.ps1` builds the message from `CHANGELOG.md`. Delete it.
- A4. DONE v0.23.3. The shared data folder was defined twice: `DataDir()` in `AutosaveShared.h` (used by WinUI) and `GetSharedDataDir()` in `Storage.cpp` (Win32). Unify so the two apps cannot drift apart.
- A5. DONE v0.23.3. Docs drift: audit finding F7 (data beside the exe) is done in practice - the data lives in `C:\BalancingAct\data` (v0.13.2) - so mark it closed (a hard-coded path rather than `%LOCALAPPDATA%` is the decision on record); `README.md` / `ARCHITECTURE.md` should describe the WinUI edition and `winui/`; `AuditFindings_2026-09-30.md` open-items table to be refreshed.
- A6. DONE (Jack, 2026-11-23: Win32 and WinUI clean). A "Regression pass" of 13 checks is now at the top of `Testing.md` (v0.23.3). `Testing.md` was cleaned up in v0.24.5: the old per-version lists are gone and 19 checks that only Jack can do remain under "Still to check".
- A8. **OPEN (Jack):** remove `15-10-26.fbd` (a real saved sheet) from the public GitHub history with `purge_fbd_history.ps1` (supplied with the 0.24.5 zip), then force-push. See SecurityHardeningRegister item 21 and DevelopmentWorkflow "Git history cleanup".
- A7. DONE v0.23.4: `build_mingw.bat`, `CMakeLists.txt` and `CMakeSettings.json` removed (never run by Jack); MSVC is the build. Docs updated. Jack deletes the three files (command in the delivery message).

**Phase B - WinUI catch-up (the app Jack will use daily)**
- B1. DONE v0.23.4 (tested OK by Jack 2026-11-25). Restore from Backup (file picker in `backups\`, safety snapshot of the current sheet first).
- B2. Crash-recovery named file. (The "exit exception" seen when running WinUI under Visual Studio is thrown from VS's own XAML inspector, `WinUITap.dll`, with no app frames in the stack; Jack saw it twice, 2026-11-23. Not pursued unless it shows when run outside the debugger.)
- B3. Filter box on Total Overview, Breakdown and By Species (Win32 has it since v0.19.0; WinUI only filters the entries list).
- B4. Start-up behaviour setting: resume silently, but ask first if the loaded sheet is from an earlier session (decide).
- B5. Dark theme (WinUI only).
- B6. Check the WinUI menus against Win32 once more after B1-B3 (Email All Suppliers is on a button, not in a menu).

**Phase C - new features (both apps share the logic in `src/`)**
- C1. Highlight the cheapest supplier per species.
- C2. Print / PDF for Total Overview and By Species (only Breakdown prints today).
- C3. Price history / trend per species (needs a storage decision first: a database or the existing `history\` files; spec below).
- C4. Ideas, not scoped: multi-sheet comparison, sheet-level notes, a Today dashboard tab, double-click `.fbd` association.

**Phase D - delivery and robustness**
- D1. **Decided 2026-11-20: WinUI is the main app; Win32 is still used.** New features go in `src/` (shared) and WinUI first; Win32 gets them when it is cheap (shared code) and otherwise bug fixes only. F16-F22 (D3) are therefore low priority.
- D2. Installer / packaging: the WinUI package is signed with a temporary key, so it only installs on this PC through Visual Studio. A second machine needs a proper signing certificate or a different packaging route.
- D3. If Win32 stays: F16-F22 as one small stage (unchecked window-creation results, message-loop error, main-window position not clamped to monitors on restore, no `WM_DPICHANGED`). WinUI already checks the saved position is on a screen.
- D4. DONE (checked 2026-11-20): the Google project's audience is **Internal** (own Workspace organisation), so sign-in does not expire after 7 days and no Google verification is needed. Only accounts on that domain can sign in. Do not click "Make external".
- D5. Test coverage: `src/` logic is covered (353 tests) but WinUI-only code (settings, recent files, window placement, dialogs) has none. Move the logic that can be tested (recent-list handling, placement validation) into `src/` with tests when it next changes.
- D6. WinUI should be layered like Win32 (it already shares core/data/services in `src/`; its forms layer is the problem). `winui/.../MainWindow.xaml.cpp` is 4,500 lines in one file; split it into `winui/.../Forms/` pieces (Manage Names, Settings, Print, About, Drill-down) one dialog at a time, starting next time one of them is edited. Decided 2026-11-20: WinUI is the main app, Win32 stays in use. Keep the unity-include rule for `PrintJob.cpp` / `ReportRender.cpp` / `Email.cpp` / `CsvExport.cpp` in mind (they are included in this file, not project items).

**Phase E - later / only if decided**
- E1. Multi-machine, shared hosting, encryption (Bucket C, designed in `NETWORK_ARCHITECTURE.md`, nothing built).
- E1a. DONE v0.24.0-0.24.3 (both apps; tested OK 2026-11-25): per-supplier email notes dialog, separate multi-line signature, already-emailed warning, Skip asks before discarding typed notes. Open question: whether mailto / .eml mail programs add their own signature on top of ours (Testing.md v0.24.0 checks).
- E1b. **Stock tracking: received (manifests) vs sold vs not sold - FLAGGED FOR A v2 DISCUSSION (Jack, 2026-11-23: "too complicated at the moment").** Background: fishermen send manifests (paper or electronic, every supplier a different layout: kgs per species only, or boxes + kgs, or kgs + boxes + sizes + grades); Jack checks them before the sale starts, unsold fish is sold the next day, the day after, or (frozen) much later, and at the end of the day a not-sold list is made and used to reconcile that what was received equals what was sold. The app has only the sale lines, so it cannot know what is unsold. Design sketch (not agreed, not started): a stock file in the data folder with one running balance per supplier + species; a Received entry section (kgs per species from the manifest, boxes / sizes / grades only as a free-text note); Finalize Day takes the day's sales off the balance (so a sale of fish that arrived days ago still counts); each supplier's email lists their current balance as "not sold"; an end-of-day check compares the physical count with the balance and flags any species that differs. Limits: a total per supplier + species, not per lot; negative balance is flagged, not guessed; frozen vs fresh kept apart by species name ("Snapper (frozen)"); electronic manifests could later be pasted or imported. Needs a decision on the file format first, and both apps would have to read it. Interim: the per-supplier "Not sold" note at email time (see CHANGELOG once built).
- E2. **1.0** only when Jack decides it is ready for prime-time release. Suggested bar: Phase A done, Phase B done, the Testing.md regression pass clean on both apps, a second-machine install working (D2).

## Not built yet

- **Price history / trend per species** (item 5 spec below) - table first, chart second.
- **Highlight cheapest supplier per species** (Breakdown or By Species tab).
- **Print / PDF for the Total Overview and By Species tabs** - only Breakdown prints today.
- ~~Filter / search on the summary tabs~~ - done in v0.19.0 (Win32; WinUI port not done).
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
- (Removed v0.23.4: the MinGW and CMake builds. Only MSVC is built.)

## Decisions on record

- Versioning: the split and its fixes are 0.10.x; next feature 0.11.0; 1.0 = release-ready.
- Layering (`src/README.md`): forms -> services -> data -> core; nothing points back up. No Windows UI call in core/data/services.
- Every new `.cpp` goes into both build paths (`build_msvc.bat`, `tests/win32_integration/run_integration_tests.ps1`).
- Sequencing changed: the original plan did the multi-machine work (Bucket C) before the `main.cpp` split. In practice
  the split is being done first; Bucket C will be shaped by the split code rather than the other way round.
- Finalize Day: workflow, rules and design are in `BUSINESS_RULES.md` and `CHANGELOG.md` `[0.9.40]` (built; not detection-based).
- Outlier price flag: Tukey fences, IQR floor of 20% of the median, silent flag, live per species/day - see `CHANGELOG.md` `[0.9.19]`-`[0.9.23]`.

- 2026-11-04: both apps stay. The Win32 app is the main app and the common denominator (WinUI 3 cannot run on Windows 7 or Server 2008 R2, and needs Windows 10 1809+). Whether the Windows 7 box and/or the 2008 R2 DC will run the app, or Postgres, is undecided ("might / might not"): keep Win7-kernel compatibility cheap (no Windows 10-only API without a fallback, e.g. POSIX rename needs the retry fallback) but do not test on Win7 until Jack decides. Features needed by every machine go into Win32 first; WinUI is an optional front end on 10/11 machines.

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

**Decisions on record, 2026-11-04 (price tracker / database):** the price tracker is the reason for the database. It is a **tab of its own ("5. Price Guide")**, not a flyout
or a strip on Data Entry (a mockup that removed Data Entry controls was rejected; nothing may be removed from Data Entry). The database will be a **networked PostgreSQL**
database from the start, NOT the per-machine SQLite replica in the old design (Jack is already migrating another project from SQLite to PostgreSQL and found it painful). This
replaces item 6 of Bucket C. Rules carried over: the `.fbd` files stay the only source of truth; the database is derived and disposable (a Rebuild command re-reads every
finalized day); entry, saving and Finalize Day must NEVER depend on the database being reachable (a failed ingest is queued and retried, never blocks Finalize); the
single writer does the ingest, as one transaction per date (delete + insert), so it is repeatable. Open: where Postgres runs (it needs a Linux or similar box, not the
2008 R2 DC), how Win32 talks to it (libpq with its DLLs beside the exe, or ODBC), how credentials are stored on each machine, and backups.

**Decisions on record, 2026-11-02 (multi-machine):** Jack will go multi-machine "sooner rather than later". All clients run Windows 11 and
authenticate against one domain controller, a Windows Server 2008 R2 box that stays as the DC (it is end-of-life, so it should NOT hold the
data). Data goes on a separate, patched storage box (NAS, or a small Linux machine running Samba) joined to the domain; a Hyper-V Linux VM on the
2008 R2 host was considered and advised against (the unpatched host stays exposed). Sequence: file locking with read-only fallback first, then
the read-only UI, encryption, hosting in parallel, SQLite price history last. First task, DONE on Jack's PC (local NTFS, 8/8 passed, 2026-11-02): a throwaway test (`lockspike`, v2) of the writer lock (a separate `<file>.lock` held open
with FILE_SHARE_READ) alongside the atomic save (temp file + rename) while readers poll. Results: a second writer is refused and the lock names its holder; readers
never see a torn file; a killed writer's lock is released at once. FINDING: a plain rename over the file fails ("access denied") if ANY reader has it open at that moment,
even one that shares delete. Two fixes both work: retry the rename every 20 ms for up to ~2 s (4 retries in 40 saves, worst wait 16 ms with 3 readers polling every 5 ms),
and the Windows 10+ POSIX-semantics rename (replaces an open file). Plan for the app: try POSIX rename, fall back to retry. STILL TO TEST: the same selftest over a real
network share (Windows-to-Windows SMB first, then the chosen NAS/Samba box), the POSIX rename over SMB, and how long a dead writer's lock lasts after a network drop. Open: the storage box choice, whether
encryption is in the first multi-machine release, and the failure mode when the DC is down (new SMB sessions need it; local autosave must keep working).

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

- v0.21.0 (2026-11-18): Gmail draft option built (Gmail API, Internal OAuth, drafts.create only). Written, not yet run live by Jack.
- v0.23.2 (2026-11-20): WinUI Undo Delete, About, Recent Files, window position, Export to CSV (`ExportToCsv` now takes the entries). Tested OK by Jack.
- v0.23.1 (2026-11-20) WinUI Print dialog (own dialog, direct printing via `PrintBreakdownJobDirect`), debug line removed. Tested OK by Jack.
- v0.23.1 (2026-11-18) + WinUI step 14: Tools > Settings in WinUI (same keys as Win32), written not compiled.
- v0.23.0 (2026-11-18): emails use the sheet date (fix); Settings > Display & printing: list text size, email date format, print defaults (printer / A4-A3 / orientation).
- v0.22.0 (2026-11-18): Tools > Settings modal window (email method + wording, backups, price-flag sensitivity, data folder). Replaced the Tools > Email method items.
- v0.20.0 (2026-11-18): Tools > Email method (mailto / .eml). Gmail draft option greyed out pending the Gmail API test (Cloud project + Internal OAuth client created; test script with Jack).
- v0.19.0 (2026-11-17): shared Filter box on Overview / Breakdown / By Species (filtered totals). Written, not yet run by Jack.
- v0.18.6 (2026-11-16): manifest embedded in exe (copied exe keeps themed buttons/combos).
- v0.18.5 (2026-11-16): portable/deletable Win32 test copy via portable.txt marker. Supplier-email `.eml` approach still waiting on main-PC test.
