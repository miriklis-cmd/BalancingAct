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

## [0.9.43] - Full project audit: drift/staleness cleanup, one real bug found

Jack asked for a full audit of every file while testing v0.9.42, plus a
proposal for splitting up `main.cpp`. This entry covers the audit fixes;
see ROADMAP.md's "main.cpp architecture decomposition" section for the
split proposal (not executed - still correctly sequenced after Bucket A/C,
this was just planning).

**One real bug found: `history\` was never added to `.gitignore`.**
`backups\` has been gitignored since it was introduced, but the newer
`history\` folder (Finalize Day, v0.9.40) was missed. Since the exe builds
directly into this repo folder, `git add -A` in `push_update.ps1` would
have committed finalized day-files - real supplier/pricing data - straight
into version control. **If v0.9.40, v0.9.41, or v0.9.42 was already
pushed with a finalized day on record, check the repo for a
`history\*.fbd` file that shouldn't be there and remove it from git
history if so** (this is exactly the kind of thing the v0.9.39 dark-mode
history rewrite dealt with - ask if a hand and it can be walked through
again). Fixed going forward by adding `history/` to `.gitignore`.

**Stale documentation/comments found and fixed** (none affect behavior,
all are drift between what the code does and what it says about itself):
- `main.cpp`'s own top-of-file header comment described only 3 tabs
  ("Data Entry", "Total Overview", "Breakdown") - the app has had a 4th
  ("By Species") since long before this file's header was last touched.
  Added.
- `DoAbout()`'s Help > About dialog text had the exact same gap (missing
  tab 4) - this one ships to Jack, not just other developers reading the
  source - and also never mentioned Finalize Day or the outlier
  price-flag review workflow, both real, shipped features. Added all
  three.
- `ARCHITECTURE.md` said "there are three top-level windows" - missed
  `FinalizeWndProc` (added v0.9.40), the 4th. Also had a garbled,
  duplicated half-paragraph fragment ("open, `EnableWindow(mainWnd,
  TRUE)`...") left over from an earlier edit, sitting with no context
  after an unrelated section - removed and replaced with a genuinely
  useful note on why Finalize Day is a tab-1 button, not a menu item.
- `README.md` never mentioned Finalize Day or outlier price-flagging at
  all (both real, shipped features), never mentioned the `backups\`/
  `history\` folders the app actually creates, and had its own
  hand-copied `.fbd` format example that had gone stale - missing the
  `Flagged` field (added v0.9.19) and the `FINALIZED=` field (added
  v0.9.40). Replaced the duplicated example with a pointer to
  DATA_FORMATS.md (the actual authoritative copy) specifically so this
  can't drift out of sync silently again.
- `Testing.md`'s "Finalize Day" section header still said "v0.9.40-0.9.41"
  after v0.9.42 added more fixes to the same feature. Updated.

**Dead code removed**: `GreetingForNow()` in `main.cpp` had no remaining
caller - `BuildSupplierEmailBody()` (FishBalanceCore.h) takes the current
hour as an explicit parameter from `EmailSupplier()` and calls
`GreetingForHour()` directly, so this no-argument wrapper was never
actually invoked anywhere. Removed; `TodayDateString()` right below it
(which IS still used) kept as-is.

**Found, not fixed - flagged for a future pass, needs a compiler to touch
safely**: two places where real (not just cosmetic) logic is duplicated
rather than shared, found during the audit:
- Book-balance arithmetic (`entered`/`debtor`/`cash`/`book`/`diff`) is
  computed independently in `RecalcTotals()` and again in
  `RenderReportPages()` (for the printed report), with `DoFinalizeDay()`'s
  balance gate depending on `RecalcTotals()`'s result staying in sync via
  the `g_diffOk` global. Candidate: factor into one testable
  `ComputeBookBalance()` in FishBalanceCore.h.
- `ExportToCsv()` hand-rolls its own per-supplier and per-species
  aggregation instead of calling the already-existing, already-tested
  `ComputeGroupedTotals()`/`ComputeSpeciesStats()` in FishBalanceCore.h -
  meaning the CSV export and the on-screen Overview/By Species tabs could
  silently diverge if that logic ever changes in one place and not the
  other.
Neither touched in this pass - both are real functional code paths in a
financial-balancing app, and changing them without a compiler to verify
against is a different risk category than fixing a comment.

## [0.9.42] - Supplier/Species/Kgs/Price label alignment, combo box first-paint fix escalated

Two more issues from the same live-testing pass as v0.9.41, both
pre-existing and unrelated to Finalize Day itself:

- **Supplier/Species/Kgs/Price labels vertically misaligned with their
  boxes.** These 4 labels sat at a fixed `top + S(3)` pixel nudge, guessed
  years ago to visually match a plain EDIT box's text baseline - a guess
  that never held for the two COMBOBOX controls (Supplier/Species), since
  a themed dropdown's closed-box text doesn't center at the same offset an
  EDIT box's does, and the mismatch is DPI/font-dependent rather than a
  fixed pixel count. Jack: "labels to textboxes still misaligned.
  Supplier, species, kg, price still misaligned." Fixed properly instead
  of re-guessing another offset: each label now shares the exact same top
  and height as its paired control and uses `SS_CENTERIMAGE` to center its
  text from real font metrics - the same mechanism Windows already uses to
  center the text inside the neighboring EDIT/COMBOBOX, so the two always
  agree regardless of DPI or font instead of two independent guesses that
  can drift apart. (Important detail: the label height matches the
  combo's *closed* height, `S(22)`, not `hCmbSupplier`/`hCmbProduct`'s own
  `S(200)`, which is their dropped-down list height.)
- **Supplier/Species combo boxes still occasionally not painting their
  border/dropdown-arrow until hovered.** A known, long-lived Win32 theming
  race (see the `[0.9.3]`/`[0.9.11]`/`[0.9.12]` entries below) that three
  earlier rounds of fixes narrowed but never fully closed - Jack: "Doesnt
  always happen, but see screenshot for example." Escalated on both axes
  this function's own comment had already flagged as the next things to
  try: (1) two more delayed retries (250ms, 1000ms) added alongside the
  existing 50ms one, on the theory that the race doesn't always resolve
  that quickly on every machine; (2) the fix itself now toggles the
  controls' visibility (`SW_HIDE` then `SW_SHOW`) before redrawing, which
  forces ComCtl32's visual-styles engine to actually redo its theme setup
  for the control rather than just repainting pixels it may already
  consider clean - closer to root cause than a plain `RedrawWindow` call
  alone. Still not provably root-caused without a live debugger, same
  honesty caveat as the `[0.9.12]` entry.

## [0.9.41] - Two real bugs found in v0.9.40's first live check

- **Cash label was word-wrapping and getting clipped.** `hLblCash`'s box
  was only 65px wide - too narrow for its actual text, "Cash amount(s):" -
  so the STATIC control silently wrapped it onto a second line that its
  22px-tall box then clipped, leaving "Cash" visible and "amount(s):" cut
  to a sliver sitting lower than the Debtor label beside it. Jack noticed
  it as "labels/text boxes are misaligned - one is higher than the other"
  while looking closely at the screen after finalizing a day for the first
  time - a pre-existing layout bug, not something the Finalize Day change
  introduced, just never previously looked at that closely. Widened the
  label and shifted `hEditCash` right to match.
- **"Start a new entry sheet now?" left the new sheet locked.** Choosing
  Yes on Finalize Day's follow-up prompt cleared the on-screen list but
  deliberately left `g_currentFile`/`g_finalizedDate` pointing at the file
  that was just finalized - the reasoning at the time was "the finalized
  file shouldn't be touched," but that also meant the fresh, blank "next
  day" sheet inherited the finalized lock: every entry control stayed
  disabled and the button still read "Un-finalize Day." Jack: "Finalise -
  then clicking new clears the file, but not the unfinalise button." Fixed
  to detach fully - clear `g_currentFile` and `g_finalizedDate` and
  re-apply the (now unlocked) state, exactly like File > New already does.

## [0.9.40] - Finalize Day (ROADMAP.md item 3)

A new way to lock in a business day once it's genuinely balanced, replacing
the vaguer "day-rollover" idea that used to sit in the roadmap. Built after
extensive back-and-forth with Jack about how his team's actual workflow
runs (Monday/Tuesday often balanced together as one Tuesday-dated day,
Saturday sales often balanced on Monday with Saturday's date, occasional
"special week" exceptions around Easter/Christmas) — the upshot was that
auto-detecting "today doesn't match this file's date" would be wrong more
often than it was right, so this feature doesn't try. Instead, staff
finalize a day explicitly, whenever they've actually finished balancing it.

**How it works:**
- A new **Finalize Day** button sits on Tab 1, under Book Reconciliation.
- Finalize is blocked outright — no override — unless Debtor + Cash
  exactly balances against the entered total. Jack: "for finalise - you
  should stop allowing entries once finalize is clicked."
- Clicking it (once balanced) prompts for the date this day is being
  finalized as, defaulting to today, or to the current file's own name if
  that already looks like an ISO date (e.g. a file named `2026-09-20.fbd`).
- Confirming writes a permanent snapshot to a new `history\<date>.fbd` file
  (a new subfolder next to the exe, separate from the rolling `backups\`
  safety-net snapshots) and marks the working file itself as finalized
  (`FINALIZED=<date>` — see DATA_FORMATS.md).
- Once finalized, every control that could change the numbers — the entry
  form, Add/Edit/Delete/Duplicate, Debtor, Cash — is disabled ("grey
  everything out"), and the button relabels itself to **Un-finalize Day**.
  This was chosen over Jack's own first idea (intercept every add/delete
  attempt and ask to un-finalize or discard) for being much lower-risk to
  implement correctly, and for reusing the same disable-everything pattern
  already planned for multi-machine read-only mode.
- After a successful finalize, the app asks first — "Start a new entry
  sheet now?" — rather than silently clearing the screen.
- **Un-finalize Day** just asks for confirmation (no reason required),
  takes a backup snapshot first, then re-enables everything.
- A `FINALIZED=` line in a `.fbd` file is validated the same strict way
  `DRAFT_DATE=` already is — an invalid or hand-edited value is dropped
  rather than treated as a valid lock.

New unit tests in `test_fbd_loader.cpp` cover a valid `FINALIZED=` line,
an ordinary file with no such line, and an invalid value being dropped.

## [0.9.39] - Dark Mode removed entirely — reverted to the v0.9.33 baseline

Dark Mode went through five straight iterations (v0.9.34 through v0.9.38)
and never matched what Jack actually wanted, each one fixing what the
previous claimed to fix while missing something else - low contrast, then
native controls staying light, then the tab strip never actually going
dark despite being documented as fixed, then the menu bar, then the status
bar. Jack's final word on it: "you've made a real mess of dark mode -
scrap it all together, you're not listening or understanding... revert to
back before we started it." He's planning a "v2" later with a clearer
mockup of exactly what he wants, rather than iterating blind against
screenshots after the fact.

**What happened**: this development environment has no git history and no
saved snapshot of every past version, so a literal file restore wasn't
possible from this side. Jack located and sent back his own saved copy of
the v0.9.33 build - the last version before any Dark Mode work started -
and every file in this release is that build's content, verbatim, with
only `version.h` bumped forward to `0.9.39` and this entry added. No
Dark Mode code, menu item, settings field, or build dependency
(`dwmapi`/`uxtheme`) remains anywhere in the app. Confirmed by a fresh
structural balance check and non-ASCII scan of `main.cpp`, same as every
other release.

**Not lost, just parked**: the CHANGELOG/ROADMAP/Testing.md entries for
`[0.9.34]` through `[0.9.38]` below are left in place as a historical
record of what was tried and why each attempt fell short - useful context
if/when Dark Mode comes back as a proper v2 feature, scoped from a clear
mockup instead of guessed at.

## [0.9.33] - Restore from Backup now reliably opens to the backups folder
- **Jack, smoke-testing Restore from Backup**: "shouldnt it take me to
  backup folder? it didnt. last folder i used in open was desktop and
  went there instead."
- Root cause: this is documented Windows behavior, not a logic bug -
  `GetOpenFileNameW`'s `lpstrInitialDir` is only honored the very first
  time a process ever shows that common dialog. On every later call
  (regardless of which of our functions triggered it - File > Open,
  Restore from Backup, whichever came first), Windows reuses whatever
  folder the user last navigated to in *any* prior call, ignoring
  `lpstrInitialDir` entirely. `DoRestoreFromBackup()`'s `lpstrInitialDir`
  was already correctly set to the backups folder - it was just being
  silently overridden by the earlier File > Open into Desktop.
- Fix: `lpstrFile` is now pre-filled with the backups folder path (plus a
  trailing backslash, no filename) before the dialog opens. A path
  supplied in `lpstrFile` takes priority over Windows' remembered folder,
  unlike `lpstrInitialDir` - this reliably forces the dialog to open in
  `backups\` every time, regardless of where any other file dialog was
  last used. `lpstrInitialDir` is kept as a harmless fallback for a
  genuine first-ever call.

## [0.9.32] - File > New now takes a backup snapshot before discarding too
- **Jack found this directly while smoke-testing v0.9.28's checklist**:
  added an entry, did File > New, confirmed the discard prompt, checked
  `backups\` afterward - nothing there.
- v0.9.28 gave File > Open, Recent Files, and Restore from Backup an
  unconditional backup snapshot right before discarding unsaved data, but
  deliberately left File > New out, reasoned at the time as "New already
  clears to a blank sheet, nothing to preserve." That reasoning was
  backwards: it's not the new blank sheet that needs protecting, it's
  whatever unsaved work is being thrown away to reach it - exactly the
  same risk as the other three paths, and exactly the kind of gap the
  v0.9.24 data-loss incident this whole feature exists to close.
- Fix: `DoFileNew()` now calls `WriteBackupSnapshot()` right after the
  discard prompt is confirmed, same pattern and same place in the flow as
  `DoFileOpen()`/`DoRestoreFromBackup()`/the Recent Files handler.
- This also means the old v0.9.28 checklist item "no spurious snapshot on
  File > New" is now wrong on purpose - New backing up before a genuine
  discard is the correct, intended behavior as of this version.

## [0.9.31] - Rolling backup snapshots now warn on failure instead of failing silently
- **Jack's report**: "Every 3 minute save not working" — after v0.9.29
  added the real `WM_TIMER` for `MaybeBackupOnTimer()`, Jack retested and
  corrected his original account: it was actually v0.9.28, and he'd made
  "maybe 5" separate Add Entry commits over roughly a minute before
  checking `backups\` after about 3 minutes and finding nothing. He asked
  to "look properly at the code" rather than re-diagnose from his own
  uncertain recollection of timing.
- On inspection, `MaybeBackupOnTimer()`'s guard (`g_lastBackupTick != 0 &&
  ...`) does **not** skip the very first call of a session — with 5 Add
  Entry commits, each routing through `RefreshAll()` → `AutosaveNow()` →
  `MaybeBackupOnTimer()`, a backup snapshot should have been written on
  the very first commit regardless of the v0.9.29 timer fix. So the
  missing-timer explanation, while real and worth fixing, didn't fully
  account for what Jack described.
- Root cause found: `WriteBackupSnapshot(const std::string&)` called
  `WriteFileAtomicUtf8(path, content)` and **discarded its return value**.
  A failed write (backups\ folder unwritable, disk full, antivirus lock,
  etc.) was completely silent — no warning shown to Jack — and worse,
  `g_lastBackupContent`/`g_lastBackupTick` were still updated as if the
  snapshot had succeeded. That meant `MaybeBackupOnTimer()`'s "nothing
  changed since last snapshot" skip logic believed a (non-existent)
  snapshot was already current, and its interval guard believed one had
  just been taken — both suppressing any near-term retry. A persistently
  failing backup folder could go bad indefinitely without any sign
  anything was wrong, unlike autosave.fbd's failures (which already warn
  via `g_autosaveFailWarned`/`AutosaveNow`).
- Fix: `WriteBackupSnapshot` now checks `WriteFileAtomicUtf8`'s return
  value. On success, behavior is unchanged. On failure: shows a one-time
  warning dialog (same warn-once/reset-on-recovery pattern as autosave's
  own failure warning, via a new `g_backupFailWarned` flag), and — new —
  deliberately does **not** update `g_lastBackupContent`/`g_lastBackupTick`
  on failure, so the very next autosave (e.g. the next field's focus loss)
  retries immediately instead of waiting out a full 3-minute interval
  believing a snapshot already succeeded.
- This is a real, previously-unnoticed bug independent of the v0.9.29
  timer fix, and a credible full explanation for backups Jack expected
  but didn't see even while actively entering data. It doesn't rule out
  the v0.9.29 fix also having mattered (both could have compounded), but
  it closes the gap that the timer fix alone left open.

## [0.9.30] - Discard prompt now tracks real unsaved changes, not just "any data"
- **Jack's request after smoke-testing v0.9.28**: "should be kinda?
  dirty flagged?. If something changes, dialog comes up. Once saved,
  then no dialog until something changes again." Previously,
  `ConfirmDiscardCurrentData` fired whenever there was *any* data on
  screen (entries, or Debtor/Cash text) regardless of whether it had
  just been saved - so File > Save followed immediately by File > Open
  still asked to confirm, even though nothing was actually at risk.
- **Fix**: new `g_dirty` flag, set `true` on every actual data change -
  `CommitEntryForm` (add/update), `DeleteSelectedEntry`, `UndoDelete`,
  a rename/merge via Manage Names that actually changed anything, and
  Debtor/Cash `EN_CHANGE` (every keystroke, matching how those fields
  already autosave on focus-loss) - and set back to `false` on every
  point where the in-memory state provably matches something durable on
  disk again: File > New (blank sheet, nothing to lose), a successful
  File > Open/Recent Files/Restore from Backup load, a successful
  File > Save/Save As, and the startup autosave.fbd reload.
  `ConfirmDiscardCurrentData` now checks `g_dirty` instead of "is there
  any data at all."
- **Deliberately NOT cleared by**: autosave.fbd writes or rolling backup
  snapshots (including the v0.9.28 file-switch snapshot) - those are
  safety nets running in the background, not the user's own deliberate
  "I'm done with this" action the prompt exists to key off.
- **One subtlety worth documenting**: `SetWindowTextW` on the Debtor/Cash
  fields fires `EN_CHANGE` the same as a real keystroke does, so the
  clear-to-blank calls in `DoFileNew` and the Debtor/Cash restores inside
  `LoadFromFile` would otherwise mark the sheet dirty immediately after
  a load. Each of those call sites explicitly sets `g_dirty = false`
  *after* the load/clear completes, so the flag ends up correct despite
  the intermediate `EN_CHANGE` noise.
- No `FishBalanceCore.h` change - pure `main.cpp` logic, no doctest
  re-run needed.

## [0.9.29] - Fix: rolling backups didn't actually run on a real timer
- **Real gap Jack found while smoke-testing v0.9.28**: made an edit, then
  left the app idle - no backup ever appeared, no matter how long he
  waited. Reported as "every 3 minute save not working."
- **Root cause**: `MaybeBackupOnTimer()` was never wired to an actual
  `WM_TIMER`. It only ran *inside* `AutosaveNow()`, which itself only
  fires on focus-loss (`EN_KILLFOCUS`/`CBN_KILLFOCUS`) or an explicit
  action like Save. So "roughly every 3 minutes" only held true while
  actively tabbing/clicking between fields - the moment you stopped
  interacting with the form (cursor still in a field, no more focus
  changes), the elapsed-time check inside `MaybeBackupOnTimer()` never
  got evaluated again, regardless of how much real time passed. Exactly
  the case - unsaved edit, then idle - this feature exists to protect.
- **Fix**: a new recurring `WM_TIMER` (`ID_TIMER_BACKUP_CHECK`, ticking
  every 30 seconds, started in the main window's `WM_CREATE` and killed
  in `WM_DESTROY`) now calls `MaybeBackupOnTimer()` directly, independent
  of any UI activity. 30 seconds is just the polling granularity, not the
  backup interval - `MaybeBackupOnTimer()`'s own `kBackupIntervalMs`
  (3 minutes) and no-change-skip logic (v0.9.26) are unchanged and still
  decide whether anything actually gets written on each tick.
- Autosave-on-focus-loss still also calls `MaybeBackupOnTimer()` as
  before (harmless - the interval/no-change checks make a redundant call
  a no-op), so active data entry behaves exactly as it did in v0.9.17-28.
  The only real-world difference is idle screens with unsaved data now
  actually get backed up on schedule.
- No `FishBalanceCore.h` change - pure `main.cpp` logic, no doctest
  re-run needed.

## [0.9.28] - Backup snapshot taken on every file switch, not just Save
- **Real gap Jack flagged**: "Umm I dunno if we should for switching
  files? Seeming as I lost work when we did, perhaps we should?" - in
  reference to the v0.9.24 data-loss incident. That fix made File > Open
  and Recent Files *ask* before discarding unsaved data, but the
  confirmation prompt is not itself a backup - answering "Yes, discard"
  still destroys whatever wasn't saved yet if nothing had backed it up
  in the meantime (the rolling timer might be up to 3 minutes stale, and
  there might be no explicit Save at all for in-progress work).
- **Fix**: `DoFileOpen`, the Recent Files menu handler, and
  `DoRestoreFromBackup` now each call `WriteBackupSnapshot()`
  unconditionally, immediately after the user confirms the discard
  prompt and immediately before `LoadFromFile` overwrites `g_entries` in
  memory. This is the same unconditional snapshot explicit Save/Save As
  already took (see `[0.9.17]`) - switching files is now treated as an
  equally deliberate, equally backup-worthy action, not just a read.
- Whatever was on screen right before the switch is now always
  recoverable via File > Restore from Backup, even if it was never
  explicitly saved and the rolling timer hadn't ticked yet - closing the
  last real exposure window this class of incident depends on.
- No `FishBalanceCore.h` change - pure `main.cpp` logic, no doctest
  re-run needed.

## [0.9.27] - Backup filenames now show which source file they're from
- **Real gap Jack flagged**: with backups just named
  `backup_YYYYMMDD_HHMMSS.fbd`, there was no way to tell which source
  file a snapshot belonged to if you'd worked in more than one `.fbd`
  file in a session - a real problem in Restore from Backup's file
  list, where picking the wrong one is exactly the kind of mistake this
  feature exists to protect against.
- **Fix**: new `CurrentFileLabelForBackup()` extracts the current named
  file's base name (no path, no extension - e.g. `21112` for
  `...\21112.fbd`), or `unsaved` if no named file is open yet.
  Filenames become `backup_YYYYMMDD_HHMMSS_<label>.fbd` - visible
  directly in the standard Open dialog Restore from Backup already
  uses, no new UI needed.
- **The label goes AFTER the timestamp, deliberately** - the timestamp
  has to stay the leading, fixed-width part of the filename for
  `PruneOldBackups`' "sort by name = sort by time" logic to keep
  working. A label placed first would sort backups by source file
  before time, breaking chronological pruning outright. Hand-verified
  the sort order still holds: two backups with different timestamps
  sort correctly regardless of the label, since string comparison
  resolves on the fixed-width timestamp portion first; only a
  same-second collision (effectively impossible in practice) would ever
  fall through to comparing labels, which is a harmless tiebreak.
- `PruneOldBackups`' `backup_*.fbd` glob still matches both old- and
  new-format filenames, so nothing already in an existing `backups\`
  folder needs to change.
- No `FishBalanceCore.h` change - pure `main.cpp` logic, no doctest
  re-run needed.

## [0.9.26] - Rolling backups now skip writing when nothing's changed
- **Real waste identified by Jack**: with the interval down to 3
  minutes (v0.9.25), a snapshot was still being written every 3 minutes
  regardless of whether the underlying data had actually changed since
  the last one - e.g. just switching tabs or sorting a column can
  trigger `AutosaveNow()`, and if that happened to land on a
  3-minute-elapsed check, a byte-identical duplicate file got written
  for no reason.
- **Fix**: `MaybeBackupOnTimer` now builds the current `.fbd` content
  and compares it against the last snapshot's content before writing -
  if nothing's changed, it skips the write entirely (no new file, no
  disk write, no wasted slot in the 50-backup cap), but still resets
  the timer so it doesn't re-check on every subsequent no-op autosave
  before the next full interval.
  - `WriteBackupSnapshot` split into a content-taking version and a
    no-arg convenience overload (used by explicit Save/Save As, which
    remain unconditional - "you just explicitly saved" is still worth
    its own snapshot regardless of content match).
  - New `g_lastBackupContent` tracks what was actually last written, so
    the comparison has something real to check against.
- No `FishBalanceCore.h` change - pure `main.cpp` logic, no doctest
  re-run needed.

## [0.9.25] - Rolling backup interval tightened to 3 minutes
- **Prompted by a real loss**: Jack lost in-progress edits to the
  v0.9.24 File > Open bug (fixed that version), and it turned out no
  rolling backup existed yet for that work - the 10-minute interval
  hadn't elapsed, and no explicit Save had happened either. Tightening
  this doesn't undo that loss, but shrinks the exposure window for
  other ways work could still be lost before a fix exists (a crash, a
  power cut) - not just the File > Open case, which v0.9.24 already
  closed off entirely.
- `kBackupIntervalMs` changed from 10 to 3 minutes.
- **Deliberate tradeoff, decided with Jack**: the 50-backup cap was NOT
  raised to match - at 3 minutes, 50 backups covers roughly 2.5 hours
  of rolling history, down from a full business day at the old
  10-minute/50-cap combination. Jack chose more frequent recent
  coverage over full-day coverage; noted here so it's a known,
  deliberate choice if it comes up again later.
- Every place that mentioned "every 10 minutes" updated to match: the
  About dialog, the "No Backups Yet" message, and `Testing.md`'s
  checklist. `CHANGELOG.md`'s `[0.9.17]` entry is left as-is - it's a
  historical record of what was true at that version, not something
  retroactively corrected.
- No `FishBalanceCore.h` change - pure `main.cpp` constant + message
  text, no doctest re-run needed.

## [0.9.24] - Fix: File > Open / Recent Files could silently discard
  unsaved data
- **Real data-loss bug, confirmed via Jack's own testing**: opened an
  old `.fbd` file, then went back to the file he'd been editing - his
  in-progress edits were gone. Root cause: `DoFileOpen()` had **no
  unsaved-changes check at all** - it loaded the newly-selected file
  immediately, and since `RefreshAll()` autosaves on every load, this
  also overwrote `autosave.fbd` with the new file's content, wiping out
  the only on-disk copy of the in-progress work too. The Recent Files
  menu had the identical gap. File > New already had this exact kind of
  check ("Discard the current data and start a new sheet?"), and
  Restore from Backup (v0.9.17) had its own near-identical inline copy
  - Open and Recent Files were simply missing it.
- **Fix**: new shared `ConfirmDiscardCurrentData()`, used by all four
  places that can replace the on-screen sheet (New, Open, Recent Files,
  Restore from Backup) instead of three separate, driftable copies of
  similar logic. Checks entries, Debtor, and Cash (not just entries,
  which is what New's old inline check did) before prompting; prompts
  only when there's actually something to lose.
- No `FishBalanceCore.h` change - pure `main.cpp` logic, no doctest
  re-run needed this time.
- **If you lost work to this bug**: check `backups\` via File > Restore
  from Backup - a rolling snapshot from before the file switch may
  still have it, depending on timing (see ROADMAP.md item 4).

## [0.9.23] - Two fixes from live testing: IQR floor + silent flagging
- **Real bug found via live testing**: five identical $10 entries for a
  species, then a genuinely normal $12 sixth entry, triggered the
  outlier warning. Root cause confirmed: a baseline with zero price
  spread gives IQR=0, which collapses Tukey's fence to exactly the
  baseline price itself - ANY deviation at all, even a cent, was being
  flagged. Not a tuning issue, a real defect in the formula for
  low-variance data (likely common for steadier species).
  - **Fix**: `ComputeOutlierRange` now floors the IQR used in the fence
    at a percentage of the baseline's own median price (20%, chosen
    with Jack after checking it against his exact numbers - $10
    baseline, $12 now within the resulting $7-$13 fence). Only ever
    widens the fence for a tight/low-variance baseline; every existing
    test case with real spread in the data is unaffected (hand-checked
    and covered by a new doctest case confirming the floor doesn't
    change anything when it isn't needed).
- **Removed the interactive Yes/No dialog from Add Entry**, at Jack's
  explicit request: a modal interrupting every flagged entry broke his
  keyboard-driven data-entry flow at real volume (100-500 entries/day).
  `CommitEntryForm` now silently sets `priceFlagged` and commits -
  never blocks, never interrupts. The double-click/Edit Selected review
  dialog (`ReviewOrEditEntry`) is unchanged and remains the deliberate
  review path. Combined with v0.9.22's whole-group re-evaluation, a
  false-positive flag will often clear itself automatically as more
  similar-priced entries come in, without any user action.
- New doctest coverage in `tests/test_aggregation.cpp` for the floor fix
  (Jack's exact $10/$12 numbers, plus confirmation a genuine $50 typo
  still gets caught, plus a regression check that existing spread-in-
  the-data cases are unaffected). **`FishBalanceCore.h` changed again -
  run the doctest suite before testing this build.**

## [0.9.22] - Outlier check now re-evaluates the whole species/date group
- **Closes a real gap Jack hit with live data**: a typo entered as the
  FIRST entry for a species that day had no baseline to be checked
  against (below the minimum of 4), silently became part of the data,
  and was never re-evaluated just because later, correctly-priced
  entries came in around it - even once there were enough of them to
  make the typo obvious. Confirmed with Jack's own numbers: bonito at
  $1111, $11, $11, $11, $111 - the $1111 entry was never flagged, even
  after the 5th entry gave it a real baseline to be judged against.
- **New `ReevaluateOutlierFlagsForSpeciesOnDate`** (`FishBalanceCore.h`):
  after anything that changes which prices exist for a species/date - a
  commit (add/edit), a delete, or an undo-delete - every entry in that
  group is silently re-checked leave-one-out against the CURRENT full
  set, not just the entry that triggered the change. Using the same
  bonito numbers: once the 5th entry existed, re-evaluating the whole
  group correctly flagged the $1111 entry and left the other four
  alone (hand-verified and covered by a new doctest case using these
  exact numbers).
- **The flag is now live at the group level, not just per-entry**: an
  entry manually cleared (right-click "Clear flag", or "No" in
  `ReviewOrEditEntry`) can be silently re-flagged later if a
  SUBSEQUENT change to a sibling entry's price makes it look unusual
  again. This is a deliberate behavior change, not an oversight - see
  ROADMAP.md item 7 for the reasoning.
- Wired into `CommitEntryForm` (after add/edit), `DeleteSelectedEntry`,
  and `UndoDelete` - deletion/undo needed the product/date captured
  before the vector mutation invalidates the entry reference.
- New doctest coverage in `tests/test_aggregation.cpp`: Jack's exact
  reported scenario as a named test case, a below-minimum-baseline
  case, a different-species/date isolation case, and an unflag-on-fix
  case. **`FishBalanceCore.h` changed again - run the doctest suite
  before testing this build.**

## [0.9.21] - Outlier warning wording fix
- **The v0.9.19 warning text was factually wrong**, not just off-brief:
  it said "other entries range $X-$Y", but $X-$Y was Tukey's fences (the
  computed threshold), not the actual range of today's other entries -
  those fences deliberately sit outside the real observed spread by
  design, so the sentence was describing the wrong thing entirely, not
  just phrased differently than requested.
- **Rewritten to match Jack's original wording**, and one-sided: shows
  only the bound actually crossed ("the dynamic limit calculated for
  today is max $X/kg" for a too-high price, "min $X/kg" for a too-low
  one) rather than always showing both — which also resolves the
  factual problem above, since "the dynamic limit" correctly describes
  what the number is.
  - `CommitEntryForm`'s warning and `ReviewOrEditEntry`'s re-evaluated
    review dialog both updated to match.
  - The low-bound display no longer needs the `displayLow` negative-
    clamp workaround from v0.9.19 - showing the low bound only when it
    was actually crossed means it's mathematically guaranteed positive
    at that point (price >= 0 by validation, and price < low to trigger
    that branch).
  - Button captions remain plain `MessageBox` Yes/No (a custom-captioned
    popup like Jack's original mockup was considered and explicitly
    declined - see ROADMAP.md item 7) - the distinction is in the
    sentence, not the buttons.
- No `FishBalanceCore.h` change - pure message text in `main.cpp`, no
  doctest re-run needed this time.

## [0.9.20] - Fix: v0.9.19 build failure (C++17 inline variable)
- **v0.9.19 did not compile via the CMake/Ninja path** —
  `error C7525: inline variables require at least '/std:c++17'` on
  `FishBalanceCore.h`'s `kMinBaselineForOutlierCheck` constant, added in
  v0.9.19. Root cause, found on investigation: `CMakeLists.txt` never
  set a C++ standard at all - unlike `build_msvc.bat`
  (`/std:c++17`) and `build_mingw.bat` (`-std=c++17`), which both
  already specified it correctly. This is a pre-existing gap between
  the three build paths that simply never surfaced before, because
  nothing in the codebase had needed a genuinely C++17-only construct
  through the CMake path until this one variable.
- **Two-part fix**:
  1. Changed `kMinBaselineForOutlierCheck` from `inline const` (a
     C++17-only "inline variable") to `static const` - functionally
     identical for a header-only constant that's never address-taken
     across translation units, and valid in any C++ standard back to
     C++98.
  2. Added `set(CMAKE_CXX_STANDARD 17)` /
     `set(CMAKE_CXX_STANDARD_REQUIRED ON)` to `CMakeLists.txt`, matching
     the two `.bat` scripts, so this class of mismatch can't quietly
     recur the next time a genuine C++17 feature is used.
- Every other `inline` in `FishBalanceCore.h` is on a function (fine in
  any standard) - checked the whole file to confirm this was the only
  inline-variable case.
- No behavior change from v0.9.19's intended design.

## [0.9.19] - Outlier price warning (ROADMAP.md item 7)
- **New: warns when a price looks like a typo, compared against that
  species' other entries on the same date (all suppliers pooled).**
  Same-day only, deliberately - no cross-day history, no dependency on
  item 5 (Price History) or the flat-file-vs-SQLite decision behind it.
  - **Range method**: Tukey's fences (IQR-based) - `Q1 - 1.5*IQR` to
    `Q3 + 1.5*IQR`, computed via median-of-halves quartiles. Needs at
    least 4 other entries for that species/date before it runs at all -
    below that, no check, no warning, no flag.
  - **Soft warning, never a hard block**: a standard Yes/No `MessageBox`
    ("Click Yes to go back and fix it, No to save it as entered
    anyway") - a genuinely unusual but correct price is always
    enterable.
  - **New `priceFlagged` field on `Entry`**, persisted as a 7th
    pipe-delimited field in `.fbd` (`Supplier|Species|Kgs|Price|Date|
    Notes|Flagged`) - backward compatible with both the pre-v0.9.19
    6-field format and the pre-v0.9.0 4-field format.
  - **Flagged rows are marked in the Entries list**: a warning glyph
    prefixed on the Price cell, plus the whole row tinted (reusing the
    app's existing red for the Debtor/Cash "out of balance" text) - via
    a new `NM_CUSTOMDRAW` handler on that list.
  - **The flag is "live," not set-once**: `CommitEntryForm` re-runs the
    same check on every commit, add or update, excluding the row being
    edited from its own baseline. Fixing a bad entry's own price
    correctly re-evaluates and clears its own flag; it does not
    retroactively re-check other entries that were flagged because of
    it.
  - **Two ways to review/dismiss a flagged row**: double-click (or Edit
    Selected) re-evaluates it live and shows the same-style dialog
    before falling through to the normal edit form - "No" clears the
    flag without opening the form at all. Right-click → "Clear flag" is
    a faster direct dismiss for when the row's already obviously fine at
    a glance (new `WM_CONTEXTMENU` handler, only shown on flagged rows).
  - `FishBalanceCore.h` changed (`Entry.priceFlagged`, the `.fbd`
    parser, and two new portable functions -
    `GatherOtherPricesForSpeciesOnDate` and `ComputeOutlierRange`) - new
    doctest coverage added in `tests/test_fbd_loader.cpp` (7-field
    parsing, backward compat with 4/6-field rows, a 5-field rejection
    case) and `tests/test_aggregation.cpp` (the IQR math, hand-checked
    for both even and odd baseline counts, plus Jack's own $5-$10/$25
    Blue Grenadier example). **Run the doctest suite before testing
    this build.**
  - `DATA_FORMATS.md` updated for the new field and row format.

## [0.9.18] - Fix: v0.9.17 build failure (MaybeBackupOnTimer)
- **v0.9.17 did not compile** — `error C3861: 'MaybeBackupOnTimer':
  identifier not found` at the `AutosaveNow()` call site. Root cause:
  `AutosaveNow()` calls `MaybeBackupOnTimer()`, but that function (along
  with `WriteBackupSnapshot`/`PruneOldBackups`/`BackupDir`) wasn't
  defined until further down the file - a plain declaration-before-use
  ordering mistake, not caught by the structural brace/paren balance
  check used in place of an actual compiler (that check verifies
  matching braces/parens, not that every identifier used is declared
  first - a real gap, noted for next time).
- **Fix**: added a forward declaration for `MaybeBackupOnTimer()` next
  to the file's other forward declarations, the same pattern already
  used for every other function called before its definition in this
  file (`RefreshAll`, `CancelEdit`, etc.). No other new v0.9.17 symbol
  had this problem - checked every definition/call-site pair for
  `BuildFbdSaveContent`, `BackupDir`, `PruneOldBackups`,
  `WriteBackupSnapshot`, `DoRestoreFromBackup` and confirmed each is
  defined before (or forward-declared before) every place it's called.
- No behavior change from v0.9.17's intended design - same rolling
  backup feature, same throttling, same Restore from Backup menu item.
  The v0.9.17 test checklist in ROADMAP.md still applies once this
  version actually builds.

## [0.9.17] - Timestamped rolling backups (ROADMAP.md item 4)
- **New: `backups\` folder next to the .exe, holding rolling timestamped
  snapshots of the current sheet** — right up until now, autosave only
  ever overwrote a single `autosave.fbd`, so a mistake (a bad merge, an
  accidental File > New/Open discarding unsaved work, or anything else
  that silently clobbers the current data) had no way to recover an
  earlier version. Snapshots are named `backup_YYYYMMDD_HHMMSS.fbd`.
  - **Throttled, not per-autosave**: autosave already fires on every
    field focus-loss during normal data entry (v0.9.15) - snapshotting
    every single one of those would flood the folder with near-identical
    files at real volume (500-1000 entries/day). A snapshot is taken at
    most once every 10 minutes from the autosave path
    (`MaybeBackupOnTimer`), plus one unconditional snapshot on every
    explicit File > Save / Save As (a deliberate user action, worth its
    own point-in-time copy regardless of the timer).
  - **Rolling cap of 50 backups** — oldest snapshots are pruned once the
    count exceeds that, so the folder doesn't grow unbounded
    (`PruneOldBackups`, sorts by the sortable timestamp embedded in the
    filename).
  - **New menu item: File > Restore from Backup...** — a standard Open
    dialog pointed at the `backups\` folder, reusing the same strict
    `LoadFromFile`/`ParseFbdContent` validation as opening any other
    `.fbd` file, so a corrupted or non-.fbd file is rejected the same
    way. Deliberately does **not** set the restored backup as the
    current named file - the user reviews it on screen first and must
    explicitly File > Save As to keep it, rather than the very next
    autosave/backup cycle silently starting to overwrite a
    timestamp-named backup file as if it were the real document.
  - Best-effort and silent on individual failure (no warning dialog like
    the real `autosave.fbd` write has) - missing one rolling snapshot
    isn't worth interrupting data entry over, since `autosave.fbd` and/or
    the named file remain the actual save path this feature backs up,
    not replaces.
  - Refactored `SaveToFile` to extract its content-building logic into a
    new `BuildFbdSaveContent()`, reused by both the real save path and
    the backup snapshot writer - identical output, no behavior change to
    existing saves, just avoids a second copy of that logic drifting out
    of sync.
  - `backups\` added to `.gitignore` alongside `autosave.fbd` and the
    other per-machine runtime files - these are user data generated at
    runtime, not project source.
  - `FishBalanceCore.h` untouched by this change (pure Win32/file-I/O
    addition in `main.cpp`), so the doctest suite doesn't need
    re-running for this one - see DevelopmentWorkflow.md.

## [0.9.16] - Debtor/Cash: subtraction now actually works
- **Fixed a real bug found during v0.9.15 manual testing**: typing
  `123+11-21` into Debtor or Cash silently computed `134`, not `113`.
  `ParseSumExpr` only ever recognized `+` as an operator between terms -
  a `-` character just got absorbed into whatever term it appeared in,
  so `"11-21"` was treated as one single term, and `std::stod` on that
  string silently parses just the `"11"` prefix, dropping the `"-21"`
  entirely. Not a documented/accepted gap like the parser's other known
  permissive behaviors (an invalid term like `"oops"` being dropped,
  `"12x"` contributing just `12`) - this one was a genuine bug that
  produced a wrong number with no indication anything was off.
  - Fixed properly: both `+` and `-` are now recognized as operators
    between terms, with a running sign applied to whichever term
    follows - `123+11-21` now correctly computes `113`. A leading minus
    also works (`-50+100` = `50`).
  - 5 new test cases added to `test_parsing.cpp`, including the exact
    real-world scenario that surfaced this (`123+11-21` → `113`) as a
    named regression test, plus confirming plain addition-only
    expressions are unaffected. Full suite: 83 test cases / 244
    assertions, all passing, clean under
    AddressSanitizer/UndefinedBehaviorSanitizer.
  - The parser's other already-documented permissive behaviors
    (dropping an unparseable term entirely, accepting `"12x"` as `12`)
    are deliberately unchanged - only operator support was added here,
    not term-level validation. Those remain tracked separately in
    SecurityHardeningRegister.md/ROADMAP.md.

## [0.9.15] - Autosave performance: write on focus loss, not every keystroke
- **Fixed the real performance cost of v0.9.13/v0.9.14's per-keystroke
  autosave** - item 1 on the roadmap, now confirmed urgent at the real
  target volume (500-1000 entries/day). Every keystroke in
  Supplier/Species/Kgs/Price/Notes/Date/Debtor/Cash was triggering a
  full, synchronous atomic rewrite of the entire day's file. Analysis:
  at real volume, the dominant cost is disk I/O frequency (specifically
  the flush-to-stable-storage step in `WriteFileAtomicUtf8`), not how
  cheaply the content string gets assembled beforehand - a day's worth
  of text (tens to ~100KB at 1000 entries) is trivial to build in
  memory, but the same fixed per-write disk overhead paid once per
  character adds up fast.
  - Fixed by switching all six fields from `EN_CHANGE`/`CBN_EDITCHANGE`
    (fires per keystroke) to `EN_KILLFOCUS`/`CBN_KILLFOCUS` (fires once,
    when the field loses focus - moving to the next field, clicking a
    button, switching tabs, etc.). A natural, deterministic checkpoint
    that needs no timer at all - deliberately avoiding the exact
    unreliability that sank the v0.9.11-v0.9.13 debounced-timer attempt
    for Debtor/Cash, since a direct notification has no "did it actually
    fire" question the way a `SetTimer`/`WM_TIMER` did.
  - `RecalcTotals()` (Debtor/Cash's live on-screen total) and
    `ComboAutoComplete()` (Supplier/Species's live suggestions) are
    unaffected - both stay on their original per-keystroke triggers,
    since neither does any disk I/O.
  - **Trade-off, stated plainly**: a crash while a field still has focus
    can now lose that field's most recent keystrokes since the last
    focus change - a small, bounded loss (at most one field's recent
    typing), not the whole draft or day. The existing unconditional
    final save in `WM_DESTROY` still covers a graceful close regardless
    of focus state, unchanged.
  - Full history of this fix (three iterations - no autosave, then an
    unreliable timer, then unthrottled per-keystroke, now this) is
    documented directly in the code comment at the fix site, not just
    here, so the reasoning survives without needing to dig through
    conversation history.

## [0.9.14] - In-progress "Add Entry" draft survives a crash or power loss
- **Added: the Add Entry form (Supplier/Species/Kgs/Price/Notes/Date) now
  persists as you type**, not just after clicking Add Entry — so a crash
  or power loss no longer loses a row you were in the middle of typing.
  Requested explicitly (Jack: "in case of crash or power loss, yes").
  - New `.fbd` format fields: `DRAFT_SUPPLIER=`, `DRAFT_SPECIES=`,
    `DRAFT_KGS=`, `DRAFT_PRICE=`, `DRAFT_NOTES=`, `DRAFT_DATE=` — written
    on every save (autosave included) alongside the existing `DEBTOR=`/
    `CASH=` fields, documented in `DATA_FORMATS.md`. Fully backward
    compatible: a file with none of these lines (anything saved before
    this version) just loads with an empty draft.
  - Every field that previously had no change-tracking at all now does:
    added `EN_CHANGE` handling for Kgs/Price/Notes, added
    `DTN_DATETIMECHANGE` handling for the date picker (neither existed
    before), and extended the existing Supplier/Species autocomplete
    handlers to also persist the draft.
  - On load, the draft restores into the form in "add new" mode - not an
    attempt to resume editing a specific existing row, which would be a
    much more fragile thing to restore correctly (the underlying entries
    are already safe in the file regardless of the form's draft state).
  - "Clear / Cancel Edit" now explicitly persists the cleared state
    immediately, rather than relying on the `SetWindowTextW` calls that
    reset the fields to reliably fire their own change notifications
    (they're programmatic, not genuine keystrokes - a known Win32
    inconsistency already documented elsewhere in this codebase) -
    without this, clicking Clear could leave a stale, un-cleared draft on
    disk that would incorrectly reappear on next launch.
  - Same design tradeoff as the v0.9.13 Debtor/Cash fix: a direct,
    synchronous save on every keystroke rather than a debounced timer,
    favoring guaranteed correctness over write-frequency optimization -
    now applied consistently across every field in the entry form, not
    just Debtor/Cash.
  - 5 new test cases added to `test_fbd_loader.cpp` covering the new
    format fields: correct parsing, invalid-date rejection (same
    validation `ParseISODate` already applies everywhere else), backward
    compatibility with pre-v0.9.14 files, and that draft-only content
    (no `BEGIN`/`END`/`DEBTOR`/`CASH`) is still recognized as a genuine
    `.fbd` document rather than rejected as unrelated text. Full suite:
    80 test cases / 237 assertions, all passing.

## [0.9.13] - Debtor/Cash autosave: removed the debounce timer entirely
- **Fixed: Debtor/Cash still wasn't reliably autosaving**, confirmed
  broken even when running the actual built `.exe` directly (ruling out
  the earlier "Visual Studio's F5 terminates the debuggee rather than
  closing gracefully" theory from v0.9.11/v0.9.12). Root cause not fully
  provable without a live debugger, but the `SetTimer`/`WM_TIMER`
  debounce mechanism itself was the obvious suspect - it's genuinely more
  complex than every other autosave path in the app, and its correctness
  depends on `WM_TIMER` messages surviving this app's main message loop
  (`IsDialogMessageW` processing every message before normal dispatch),
  which couldn't be independently verified. Rather than keep guessing at
  an unprovable async mechanism, replaced it with a direct, synchronous
  call to `AutosaveNow()` on every Debtor/Cash keystroke - the exact same
  proven-reliable pattern `RefreshAll()` already uses for every
  Add/Edit/Delete elsewhere in the app. This trades the "coalesce rapid
  typing into one write" optimization for guaranteed correctness; the
  file is small enough that a write per keystroke isn't a real
  performance concern, and it now matches the app's already-established
  performance profile rather than introducing a new one. (If the
  separately-tracked `RefreshAll()` performance item is ever addressed,
  this call site benefits automatically with no further change needed
  here.) Removed the now-dead timer plumbing entirely (`ID_TIMER_DEBTOR_CASH_AUTOSAVE`
  and its `WM_TIMER`/`WM_DESTROY` handling) rather than leaving unused,
  unreliable code in place.
- **Confirmed working, not actually a bug**: the "which email should be
  kept" merge-conflict prompt (v0.9.11) fired correctly on what looked
  like a single-supplier rename. Investigated using the uploaded
  `emails.txt`: an orphaned email entry for a bare "Jenkins" (with no
  current entries anywhere in the data - a leftover from a rename made
  before the v0.9.9 email-migration fix existed) collided with "Jenkins
  & Son"'s own saved email when renaming the latter to "Jenkins". Working
  as designed - see ROADMAP.md for the follow-up decision on whether to
  build a way to see/clean up these pre-existing orphaned entries through
  the UI (currently invisible there since Manage Names only lists names
  with current entries).

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
