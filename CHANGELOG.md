# Changelog

All notable changes to Fish Balance Manager are recorded here. Versions
follow `MAJOR.MINOR.PATCH` loosely: MINOR for new features, PATCH for bug
fixes, MAJOR reserved for a future point where the app is considered
feature-complete and stable for daily production use.

Versions before 0.9.0 were not tracked at the time — this history has been
reconstructed from the project's development conversation as accurately as
possible, grouped into logical releases.

## [Unreleased]
- (nothing queued yet — see ROADMAP.md for what's planned next)

## [0.9.12] - Combo box regression investigated: likely root cause found
- **Investigated the v0.9.11 combo box first-paint regression properly**
  instead of layering on another blind workaround, per Jack's request.
  Method: diffed every change to `main.cpp` between v0.9.3 (when the
  original fix was confirmed working) and v0.9.10 (when the regression
  was reported) that touches `WM_CREATE`, `wWinMain`, or `LayoutAll()`.
  Ruled out: the v0.9.9 Debtor/Cash timer additions (don't run during
  startup), the v0.9.10 `NOMINMAX` define (pure compile-time macro guard,
  zero runtime effect). Found one concrete, verifiable change: the status
  bar added in v0.9.8 was being created **immediately before** the
  Supplier/Species combo boxes in `WM_CREATE` - previously (v0.9.3), the
  combo boxes were among the very first controls created, right after
  the tab strip.
  - This is a plausible root cause, not just a coincidence: the status
    bar is a comctl32 control class (`msctls_statusbar32`) being
    instantiated for the first time in the process, and this whole bug
    is fundamentally a paint/theme-initialization *timing* issue -
    exactly the kind of thing a newly-inserted control's creation could
    plausibly perturb, by shifting how the internal theme-engine
    initialization for the two combo boxes lands relative to everything
    else happening during window setup.
  - Fix: moved the status bar's creation to the very end of `WM_CREATE`
    (after all four tabs' controls exist, right before `LayoutAll()`),
    restoring the combo boxes' original relative position as being
    created early - matching the exact structure that was working back
    in v0.9.3.
  - **Honesty check**: this is a well-evidenced hypothesis based on a
    systematic diff, not a certainty - genuinely confirming the
    mechanism would need a live debugger or Spy++ trace, which isn't
    available here. The v0.9.11 belt-and-suspenders fixes (the
    synchronous + delayed-timer `RedrawWindow` calls) are left in place
    as a safety net regardless of whether this reordering turns out to
    be the real fix.

## [0.9.11] - Combo box paint regression, email merge conflict prompt
- **Regression reported: the v0.9.3 combo box first-paint fix stopped
  reliably working** (Supplier/Species combo boxes not rendering their
  border/dropdown-arrow until hovered, again). The exact code that fixed
  this in v0.9.3 is unchanged, so the precise root cause of the
  regression couldn't be pinned down without a live debugger - **this
  fix is a best-effort improvement, not a confirmed root-cause fix**,
  and needs real testing to know if it actually resolved it. Made the
  fix more robust rather than guessing blindly: extracted the redraw
  logic into a shared `FixComboBoxFirstPaint()` function, and in
  addition to the existing synchronous call right after the window
  becomes visible, added a second delayed attempt via a 50ms one-shot
  timer - this catches the case where something else (a late
  WM_SIZE-triggered relayout, possibly related to the status bar added
  in v0.9.8) re-invalidates the two combo boxes between the first
  attempt and the message loop settling. If this still isn't reliable,
  the next things to try are documented in the function's comment
  (toggling visibility, or sending `WM_THEMECHANGED` directly).
- **Added: merging/renaming suppliers with different saved email
  addresses now asks which to keep**, instead of silently picking one
  (raised as a design question after the v0.9.9 email-migration fix).
  Resolved *before* touching any entries, so canceling leaves nothing
  changed - a clean abort, not a half-applied merge. For the common case
  (exactly two different saved emails among the suppliers being merged)
  this is a Yes/No/Cancel prompt naming both addresses. For the rarer
  case of three or more different saved emails at once, a full picker
  UI wasn't judged worth building for how uncommon that is - the first
  one found is kept, but the user is now told a conflict existed and
  which one it picked, rather than that being silent as it was in
  v0.9.9.

## [0.9.10] - Fixed a build failure in v0.9.9
- **Fixed: v0.9.9 failed to compile under MSVC** with `error C2589:
  illegal token on right side of '::'` at the two `std::min()` calls
  added for the Print Preview memory fix. Root cause: `<windows.h>`
  `#define`s `min`/`max` as macros unless `NOMINMAX` is defined first,
  which textually mangles any `std::min`/`std::max` call into a broken
  expression before the compiler ever sees it as a function call - a
  well-known, easy-to-hit Win32/C++ gotcha. Fixed by defining `NOMINMAX`
  before `#include <windows.h>` - the standard, permanent fix for this
  entire class of error, not a narrow patch to just the two call sites
  that happened to trip it. Confirmed no other code in the file relied on
  the macro versions of `min`/`max` before making this change.
- This was caught by Jack's build, not by Claude - a reminder that
  `FishBalanceCore.h`'s tests (which run on Linux/g++, where this
  particular macro collision doesn't exist since `<windows.h>` isn't
  involved) can't catch every category of Win32-specific compile error;
  a real MSVC build remains necessary for full confidence on changes to
  `main.cpp` itself, not just to the portable core.

## [0.9.9] - Remaining Tier 2 audit items closed out
- **Fixed: supplier email addresses were orphaned on rename/merge.**
  Renaming or merging a supplier via Manage Names updated `g_entries` but
  never touched `g_supplierEmails`, so a saved email address became
  silently disconnected from a supplier that no longer existed under
  that name anywhere. Fixed: a rename/merge of SUPPLIER names (species
  renames don't apply - email addresses have no species concept) now
  migrates any saved email(s) to the target name and persists the
  change. Merge conflict policy when multiple merged sources have
  *different* saved emails: whichever is found first wins (or the
  target's own pre-existing email, if it had one) - documented in code
  as a deliberate simplification rather than building a full "which one
  do you want to keep" UI for a rare edge case. If the migrated email
  fails to save to disk, the existing "Updated N entries" confirmation
  now includes a warning instead of silently losing it.
- **Fixed: Debtor/Cash edits weren't autosaved until some other action
  happened to trigger a refresh.** Typing in either field only called
  `RecalcTotals()` (updates the on-screen figures) - the actual autosave
  write didn't happen until an Add/Edit/Delete elsewhere triggered
  `RefreshAll()`. A crash after editing only Debtor/Cash could lose that
  change indefinitely. Fixed with a debounced autosave: typing restarts
  an 800ms timer, so a burst of keystrokes coalesces into one disk write
  shortly after you pause, rather than either staying unsaved
  indefinitely or writing on every single character. Extracted the
  existing autosave-with-warn-once-on-failure logic out of `RefreshAll()`
  into a shared `AutosaveNow()` helper so both paths get identical
  failure handling from one place.
- **Fixed: Print Preview could use gigabytes of memory on a large
  report.** Every page was rendered as a full-resolution 24-bit bitmap at
  the *printer's* native DPI (often 600+) - a single Letter page at 600
  DPI is roughly 100MB, even though the preview window only ever shows
  it shrunk down to ~680px wide. Fixed: the preview now caps its
  rendering DPI at 150 (still crisp for on-screen viewing) while scaling
  the pixel dimensions to match, so the real printer's page proportions
  (Letter/A4/etc) are preserved exactly, just at a screen-appropriate
  resolution - roughly a 16x memory reduction per page. The actual
  `PrintBreakdownReport()` print path is untouched and still renders at
  full printer DPI for genuine print quality. Also added a hard 200-page
  safety cap directly in the shared rendering function (protects both
  Preview and the real print path) - far beyond any realistic business
  report, so this only ever engages on pathological/unexpected input,
  and guarantees a bounded ceiling on memory use regardless of caller.

This closes out every item from the original external audit's Tier 2
findings (see SecurityHardeningRegister.md) - the remaining roadmap items
are the `RefreshAll()` performance item and the feature backlog.

## [0.9.8] - Atomic/checked saves, status bar, focus fix
- **Fixed: focus after Add/Update Entry now returns to Supplier**, not
  Species (v0.9.7 had changed this to Species; reverted per follow-up
  feedback - Species still clears, Supplier still doesn't, only where
  focus lands changed back).
- **Fixed: every save path in the app is now atomic and checked** - the
  first of the remaining Tier 2 audit items. Previously `.fbd` saves
  (both autosave and named-file), `settings.txt`, `recent.txt`,
  `emails.txt`, and CSV export all opened their destination with `"wb"`
  (truncating it immediately) and never checked whether the write
  actually succeeded - a disk-full condition, a locked file, or a crash
  partway through could leave a silently truncated file while the app
  reported success. Added a shared `WriteFileAtomicUtf8()` helper that
  every one of those paths now goes through: write to a `.tmp` file in
  the same directory first, check every write/flush/close, and only
  atomically swap it into place (`MoveFileExW` with
  `MOVEFILE_REPLACE_EXISTING`) if the whole write succeeded. If anything
  fails partway, the temp file is cleaned up and the real destination is
  left completely untouched - never left half-written.
  - `.fbd` saves (`File > Save`/`Save As`) and CSV export now show the
    *specific* failure reason in their error message instead of a
    generic "could not save."
  - `SaveSupplierEmails()` now returns success/failure, and
    `ManageSaveEmail()` was fixed to actually check it - previously it
    unconditionally told the user "Email saved" regardless of whether
    the write worked (a gap flagged in the original audit).
  - Autosave failures (both the ongoing one in `RefreshAll()` and the
    final one at shutdown) now warn the user once, rather than either
    staying silent forever or spamming a dialog on every single
    Add/Edit/Delete while a problem persists - a flag resets once
    autosave succeeds again, so a *new* failure after recovery still
    gets its own warning.
  - `settings.txt`/`recent.txt` failures stay silent by design (low
    stakes - only window position/recent-files list, not business data),
    but are still genuinely atomic+checked writes now, not a
    truncate-and-hope.
- **Added: a status bar at the bottom of the window**, showing the
  running app version and the currently loaded file (title bar still
  shows the filename too, unchanged). Uses the native Win32 status bar
  control (`msctls_statusbar32`), kept in sync automatically with the
  title bar since both are now driven from the same `UpdateTitle()`
  call - no other call site needed to change.

## [0.9.7] - Data entry workflow polish
- **Changed: Species now clears after Add/Update Entry (Supplier still
  doesn't).** Previously neither field cleared, which could carry a
  species over into what's often actually a different one. Supplier
  deliberately stays populated - supports fast entry of several rows for
  the same supplier in a row (per README.md's batch-entry workflow).
  Focus now jumps to Species (was Supplier) after committing an entry,
  since Supplier is usually already correct at that point.
- **Added: "Duplicate Supplier & Species" button** on the Data Entry tab,
  next to the existing "Duplicate Last Entry". Unlike that button (which
  copies the whole last row including Kgs/Price/Notes/Date), this one
  fills only Supplier and Species from the most recent entry, leaving
  everything else untouched - for when the next row is the same
  supplier+species again but at a different weight/price, complementing
  Species now clearing after Add Entry above.
- **Fixed: Manage Names' "Apply" button was always clickable**, even with
  nothing selected in the list or an empty rename target - clicking it in
  that state just showed a MessageBox error. It now reflects that state
  directly: grayed out unless at least one name is selected AND the
  target field has something in it, updating live as the list selection
  or target text changes. ("Save Email" already had equivalent
  enable/disable logic via `UpdateManageEmailControls()` - this adds the
  matching behavior for "Apply" via a new `UpdateManageApplyButton()`.)

## [0.9.6] - Manage Names hint text cut off
- **Fixed: the hint text at the top of Tools > Manage Supplier / Species
  Names ("Select one or more names to merge/rename...") was visibly cut
  off**, with its second line clipped by the list box positioned right
  below it (confirmed via screenshot during v0.9.5 manual testing). The
  label's box only had room for one line, but the actual hint text wraps
  to two lines at normal dialog widths. Gave the label enough height for
  two lines and moved the list box (and its minimum-height clamp) down
  to match — everything below the list is positioned relative to the
  list's bottom edge, not its top, so nothing else needed to move.

## [0.9.5] - First real Windows test run, zero-warning test build
- **`tests/run_tests.ps1` was actually run for the first time on a real
  Windows/MSVC toolchain** (all 75 test cases / 215 assertions passed -
  the underlying logic was already verified with g++ on Linux before
  v0.9.4 shipped, but this was the first confirmation it also builds and
  passes on the real target compiler).
- **Fixed: doctest.h triggered a `C5285` warning** ("cannot declare a
  specialization for `std::tuple`... forbidden by `[tuple.tuple.general]`")
  once per test file (7 total) under MSVC. Root cause: by default doctest
  skips `#include`-ing the real `<tuple>`/`<ostream>`/etc. standard
  headers for faster compiles, and instead forward-declares pieces of
  `namespace std` itself (including `template <class...> class tuple;`) -
  technically not permitted by the C++ standard, which is exactly what a
  newer MSVC diagnostic (`C5285`) now flags, on top of an older one
  (`4643`) doctest already knew about and suppressed.
  - Fixed properly, not suppressed: doctest ships its own documented
    escape hatch for this, `DOCTEST_CONFIG_USE_STD_HEADERS`, which makes
    it `#include` the real standard headers instead of forward-declaring
    parts of `std` itself - removing the actual cause of the warning
    rather than telling the compiler to stop mentioning it. Added a new
    `tests/doctest_setup.h` (included before `doctest.h` in all 7 test
    files) that defines this macro in exactly one place, with the
    reasoning written down once instead of repeated in every file.
  - Re-verified after the fix: clean build under `-Wall -Wextra -Werror`
    on Linux, all 215 assertions still passing, and a clean
    AddressSanitizer/UndefinedBehaviorSanitizer pass (this genuinely
    changes which standard headers get pulled in, so it needed
    re-verification, not just an assumption that it'd still work).

## [0.9.4] - Automated test suite
- **Added the automated test suite that was always supposed to follow the
  `FishBalanceCore.h` extraction (v0.9.2)** - this was flagged as the next
  step at the time but got deferred while fixing the build warnings and
  the combo box paint bug instead. 75 test cases / 215 assertions across
  6 files in a new `tests/` folder, using
  [doctest](https://github.com/doctest/doctest) (single-header, vendored
  in as `tests/doctest.h`, MIT licensed):
  - `test_parsing.cpp` — `TrimW`, `ParseDoubleW`, `ParseSumExpr`,
    `FormatDateISO`/`ParseISODate`
  - `test_formatting.cpp` — `FormatMoney`, `FormatKg`, `FormatNum`
  - `test_fbd_loader.cpp` — the big one: valid documents, backward
    compatibility with the old 4-field format, and the specific v0.9.1
    regressions (garbage files rejected, truncated `BEGIN`-with-no-`END`
    rejected, NaN/Inf/negative rows skipped and counted, empty
    Supplier/Species rejected, whitespace trimming on load)
  - `test_aggregation.cpp` — `BuildBreakdownData`, `ComputeGroupedTotals`
    (the exact math behind the v0.9.1 Total Overview column bug),
    `ComputeSpeciesStats`
  - `test_csv_export.cpp` — `CsvField` escaping, including the
    formula-injection guard
  - `test_email.cpp` — `LooksLikeEmail`, `UrlEncodeForMailto`,
    `GreetingForHour`, `PadRight`, `BuildSupplierEmailBody`
  - Several `CHARACTERIZATION` tests are included alongside the regular
    ones - these pin down already-known, already-documented permissive
    behaviors (e.g. `ParseSumExpr` silently dropping a bad term,
    `ParseISODate` not validating day-of-month) exactly as they currently
    are, without fixing them. The point isn't that these are correct -
    it's that changing them later becomes a deliberate decision with a
    failing test to update, not an accidental side effect of some
    unrelated change.
  - Compiled and run directly (native g++, not cross-compiled) before
    being delivered: clean build under `-Wall -Wextra`, all 215
    assertions passing, and a clean pass under
    AddressSanitizer+UndefinedBehaviorSanitizer. One real mistake was
    caught this way and fixed before shipping: an early draft asserted
    `FormatMoney(1.005) == "$1.01"`, but `1.005` can't be represented
    exactly in IEEE 754 double-precision floating point (it's actually
    stored as `~1.00499999999999989342`), so it correctly rounds *down*
    to `$1.00` - not a bug in `FormatMoney`, just a bad choice of test
    value sitting exactly on a floating-point representation boundary.
    Replaced with `1.006`/`1.004`, which are safely clear of that
    boundary on either side.
  - `tests/run_tests.ps1` added for quick everyday use (compiles with
    `cl.exe` from a Developer PowerShell for VS, runs the suite, reports
    pass/fail). A matching `FishBalanceTests` target was also added to
    `CMakeLists.txt`, wired into CTest, as an alternative way to run the
    same suite.
  - Everything here is Win32-independent by design and runs with no GUI
    window ever created. Win32-only concerns (Print Preview rendering,
    actual printing, DPI switching, window restoration, dialog
    appearance, the v0.9.3 combo box paint issue) are NOT covered here
    and stay on the manual Testing.md/ROADMAP.md checklist - see
    ROADMAP.md's updated status section for exactly which checklist items
    are now covered by this suite vs. which still need a human looking at
    the actual app.

## [0.9.3] - Combo box first-paint fix
- **Fixed: the Supplier and Species combo boxes on the Data Entry tab
  didn't render their border/dropdown-arrow chrome when the app first
  opened** - they appeared blank until the mouse hovered over one of
  them, at which point both suddenly painted correctly (reported with
  screenshots showing the before/after). Every other control on the tab
  is created and positioned the exact same way and painted fine
  immediately, so this was specific to these two combo boxes - a known,
  if under-documented, Win32/visual-styles quirk where a themed ComboBox
  can fail to paint on its very first appearance, only rendering once
  some later event (hover, focus change, etc.) happens to trigger a
  repaint.
  - Fixed by explicitly forcing a repaint of both combo boxes right after
    the main window actually becomes visible (`ShowWindow`/`UpdateWindow`
    in `wWinMain`). Doing this any earlier - e.g. inside `WM_CREATE`,
    where the controls are created and laid out - wouldn't help, since
    the top-level window isn't shown on screen yet at that point
    regardless of what's invalidated.
  - **Caveat**: this is a UI paint-timing fix that can't be verified
    without an actual Windows display and a real visual-styles-enabled
    session - Claude's environment has no compiler or display to test
    against. If this specific fix doesn't resolve it, the next things to
    try would be forcing a `WM_THEMECHANGED` after `ShowWindow`, or
    checking whether the external `app.manifest` (which requests Common
    Controls v6 for visual styles) is actually being picked up correctly
    at runtime.

## [0.9.2] - Core extraction and a clean build
- **Internal refactor, no visible behavior change**: extracted the
  platform-independent logic from `main.cpp` into a new `FishBalanceCore.h`
  - data model (`Entry`, `SupplierGroup`/`ProductGroup`/`PriceLine`),
  parsing (`.fbd` content validation, date parsing, number parsing),
  formatting (money/kg), report aggregation (`BuildBreakdownData`, the
  Overview/By Species totals), and CSV/email string-building. This header
  has zero dependency on `windows.h`/Win32, so it can be compiled and
  tested independent of the GUI - this is preparatory work for the
  automated test suite (ROADMAP.md item 0), not a feature or fix in
  itself. `main.cpp` now `#include`s this header and calls into it
  instead of keeping its own copies; a handful of functions that need a
  Win32 type at the boundary (`SYSTEMTIME` for the DateTimePicker,
  `GetLocalTime` for the current time) keep a thin wrapper in `main.cpp`
  with their original signature unchanged, so no other call site needed
  to change. Verified via a temporary smoke test compiled and run
  directly (plus a clean AddressSanitizer/UndefinedBehaviorSanitizer
  pass) before being wired into `main.cpp`; that smoke test has since
  been removed in favor of the permanent doctest-based suite that comes
  next.
- **Build-quality fixes surfaced by the first real Visual Studio build of
  the refactored code** (all three are pre-existing, not introduced by
  the `FishBalanceCore.h` extraction above - see analysis below):
  - `CMakeLists.txt` was missing `/EHsc` for the MSVC compiler options
    (only `/W4` was set), which triggered a real "C++ exception handler
    used, but unwind semantics are not enabled" warning, since
    `ParseDoubleW`/`ParseSumExpr` use `try`/`catch`. `build_msvc.bat`
    already had `/EHsc` for the plain-batch build path; the CMake path
    just hadn't been given the same flag. Fixed by adding `/EHsc`
    alongside `/W4` in `CMakeLists.txt`.
  - `OpenPrintPreview()` triggered four "potentially uninitialized local
    variable" warnings for `pageWidthPx`/`pageHeightPx`/`dpiX`/`dpiY`.
    Traced by hand: every actual code path already initialized all four
    before use, so this was a false positive from the compiler's flow
    analysis being conservative about the two-branch structure - fixed by
    giving them default values at declaration (matching the existing
    fallback values exactly, so no behavior change) and removing the
    `gotPrinter` bookkeeping variable that no longer served any purpose
    once the fallback became "do nothing, the defaults already cover it."
  - Fixed the eight "`_wfopen`/`wcsncpy` may be unsafe, use
    `_wfopen_s`/`wcsncpy_s` instead" warnings properly - not by
    suppressing them, and not by switching outright (which would have
    broken the MinGW build, since `_wfopen_s`/`wcsncpy_s` are
    Microsoft-only CRT extensions). Added a single shared `OpenFileW()`
    helper (all 7 `_wfopen` call sites in the app now go through it) and
    a compile-time guard around the one `wcsncpy` call in
    `RenderReportPages`: under MSVC, both now genuinely use the safer
    `_s` functions (`_wfopen_s` validates its arguments and reports
    errors through its return code; `wcsncpy_s` with `_TRUNCATE`
    validates the destination buffer size instead of trusting the
    caller) - an actual improvement, not just satisfying the compiler.
    Under any other compiler (MinGW/GCC), the plain, already-correct
    standard functions are used unchanged, since this warning is an
    MSVC-specific CRT header annotation that GCC never raised in the
    first place.

## [0.9.1] - Data-integrity hardening (external audit follow-up)
An external audit (ChatGPT, read-only static review) plus a follow-up
internal review turned up several real defects. This release fixes the
three most serious ones — all three are things that could silently
corrupt or hide real financial data with no error shown. See
SecurityHardeningRegister.md for full technical detail on the first two.

- **Fixed: opening almost any file could silently wipe the autosave.**
  `LoadFromFile()` used to accept nearly anything as a "successful" load —
  no `BEGIN`/`END` required, no entries required, and an invalid Kgs/Price
  value was silently replaced with `0` instead of being rejected. Since
  every caller immediately autosaves after loading, selecting the wrong
  file (or opening a `.fbd` that was corrupted or cut off mid-save) could
  destroy the real recovery copy in one click, with the app reporting
  success the whole time.
  - The loader now validates the *whole document* before touching
    anything: it requires at least one recognized `.fbd` marker
    (`DEBTOR=`/`CASH=`/`BEGIN`/`END`), requires a `BEGIN` to be matched by
    an `END`, and requires Kgs/Price on every row to be a valid, finite,
    non-negative number (previously `nan`/`inf`/garbage silently became
    `0`). A row with an empty Supplier or Species (only possible via a
    hand-edited or corrupted file — the entry form already blocked this)
    is also now rejected instead of silently creating a blank-named group.
  - A file that fails validation changes nothing — the in-memory sheet and
    the on-disk autosave are both left exactly as they were, and the user
    sees a clear message instead of a silent "success."
  - This uncovered a second copy of the same risk at startup: reloading
    `autosave.fbd` on launch also ignored a load failure and then
    autosaved right over it. `RefreshAll()` now takes an optional
    "skip autosave" flag, and startup uses it when the existing autosave
    can't be read — the user is warned and the file is left untouched
    instead of being overwritten with a blank sheet.
  - `ParseDoubleW()` (shared by the entry form and the file loader) now
    also rejects `NaN`/`Infinity` outright — previously `std::stod`
    parsed `"nan"`/`"inf"` as valid, "fully consumed" numbers.
- **Fixed: Undo Delete could insert a row from a different, already-closed
  sheet into your current one.** The single-level undo buffer was never
  cleared by File > New, File > Open, Recent Files, or a Manage Names
  rename/merge — only by actually using Undo. Delete a row in one sheet,
  then open a different file, then click the (still-enabled) Edit > Undo
  Delete, and the deleted row from the *old* sheet would be silently
  inserted into the new one and autosaved immediately. A new
  `ClearUndoState()` helper is now called everywhere the document gets
  replaced or restructured, so the undo buffer (and the menu item) can't
  outlive the sheet it came from.
- **Fixed: Total Overview tab was showing the wrong number under "Total
  ($)."** The list view only had two columns defined (Supplier, Total $),
  but the code populating it always wrote Supplier → column 0, Kgs →
  column 1, dollar total → column 2 — so the column labeled "Total ($)"
  was actually displaying kilograms, and the real dollar total was being
  written to a column that didn't exist and was never shown at all. Added
  the missing Kgs column (matching the same Supplier/Kgs/Total shape the
  By Species tab already uses correctly) — no change was needed to the
  totals calculation itself, which was correct all along.

## [0.9.0] - Date & Notes
- Added a Date field per entry (calendar picker, defaults to today; stored
  as ISO `YYYY-MM-DD` so it sorts correctly as plain text)
- Added an optional Notes field per entry
- Added "Duplicate Last Entry" button (pre-fills the form from the most
  recent entry, focuses Kgs with it selected)
- `.fbd` file format extended to 6 fields per entry; old 4-field files
  still load correctly with Date/Notes left blank
- Entries list, sorting, delete confirmation, and CSV export all updated
  to include Date/Notes
- **Confirmed working** (built and tested on real hardware): date picker,
  Notes field, old-file backward compatibility, Duplicate Last Entry,
  Date column sorting, and CSV export all verified against the full
  Testing.md checklist

## [0.8.2] - Email reliability fixes
- Fixed "Email All Suppliers" only opening one draft when multiple
  suppliers were selected — mail clients (Outlook especially) don't
  reliably handle several rapid `mailto:` requests. Switched to opening
  drafts one at a time with a confirmation between each.
- Fixed "Email All Suppliers" silently doing nothing at all in some
  environments (no default mail app configured, or the launched window
  not gaining focus) — added `AllowSetForegroundWindow`, error-checked
  `ShellExecuteW`'s return value, and surfaced a clear message when a
  draft fails to open.

## [0.8.1] - By Species use-after-free fix
- Fixed the By Species tab showing garbled or blank data. Root cause: a
  temporary `std::wstring`'s `.c_str()` pointer was passed directly into
  `ListView_SetItemText`, which is a multi-statement macro — the temporary
  was destroyed before the underlying message actually read it. Audited
  every `ListView_SetItemText` call site in the codebase for the same
  pattern; only this one was affected.

## [0.8.0] - Print Preview, DPI fixes, By Species price stats
- Added a real Print Preview window (page-accurate rendering, Next/
  Previous navigation, hands off to the print dialog when ready).
  Refactored printing to render into a shared device-independent bitmap
  so preview and actual print output can never diverge.
- Fixed labels/buttons being clipped top/bottom or side to side on
  higher-DPI displays — the app declares DPI awareness but the layout
  code was using fixed pixel sizes that didn't scale to match. Added a
  DPI scale factor applied throughout all layout code.
- Added Average/Highest/Lowest price columns to the By Species tab and
  matching columns in the CSV export.

## [0.7.0] - Security and data-integrity hardening
- Fixed a buffer-overflow risk in the Supplier/Species autocomplete
  (`CB_GETLBTEXT` written into a fixed-size buffer without checking the
  real length first)
- Fixed a resource leak on every print job (`PrintDlgW`'s `hDevMode`/
  `hDevNames` handles were never freed)
- Fixed silent data corruption: Supplier/Species/Notes fields now reject
  the `|` character, since it's used internally as the save-file field
  separator with no escaping
- Added a confirmation prompt before deleting a row

## [0.6.x] - Email Suppliers
- Added "Email All Suppliers..." — opens a pre-filled `mailto:` draft per
  supplier in the user's default email app for review before sending, no
  SMTP credentials stored
- Added a Supplier → Email address list, managed from the Manage Names
  window
- Iterated on the email body template per user feedback: removed the
  dollar total, simplified price display (dropped redundant "/kg"),
  switched Kg to 1 decimal place throughout the app (previously 2)
- Blank "To" field support: suppliers without a saved email still get a
  draft opened rather than being skipped, with their name added to the
  Subject line so blank drafts can still be told apart

## [0.5.0] - Data management batch
- Undo Delete (single-level)
- Manage Supplier / Species Names window: merge or bulk-rename spelling
  variants across all entries at once
- By Species tab (Kgs/$ totals grouped by species across all suppliers)
- Export to CSV (entries, per-supplier totals, per-species totals, full
  breakdown, all in one file)
- Sortable columns and a live filter box on the entries list
- Recent Files list (File menu)
- Window size/position and last-opened-file remembered between sessions

## [0.4.0] - Branding
- Custom application icon (multi-resolution .ico, embedded via a
  dedicated resource script separate from the manifest)

## [0.3.0] - Editing & persistence
- Edit existing entries in place (double-click a row, or Edit Selected Row)
- Save As / Open / autosave to a `.fbd` file format
- Supplier/Species autocomplete while typing

## [0.2.0] - Batch entry
- Tab / Enter keyboard flow for fast sequential data entry

## [0.1.0] - Initial version
- Win32 C++ port of the original `BALANCE_PivotTable.xlsx` spreadsheet:
  Data Entry tab with Debtor/Cash book-balance check, Total Overview tab
  (per-supplier totals), Breakdown tab (Supplier > Species > Price, with
  subtotals)
