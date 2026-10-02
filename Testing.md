# Testing

There IS an automated test suite now (`tests/`, doctest-based, covering the
platform-independent logic in `FishBalanceCore.h` - parsing, aggregation,
CSV export, formatting; see ROADMAP.md item 0 and `run_tests.ps1`/
`run_tests.bat`). Run it after any change to `FishBalanceCore.h`. It can't
cover the Win32-specific code in `main.cpp` (window creation, message
handling, printing, file I/O) - Claude has no compiler and can't run the
Windows executable, so everything below is still manual, performed by you
after building. This file exists so "what should I check" doesn't have to
be re-derived from chat scrollback every time. Update it whenever a new
feature is added; treat it as a living checklist, not a one-time list.

Use `## Full regression pass` for a from-scratch check after a large batch
of changes. Use the per-feature sections to spot-check after a small,
targeted fix.

## Build

- [ ] Build succeeds with no errors (CMake, MinGW, or MSVC — whichever
      you're using)
- [ ] **(v0.9.48)** Build succeeds with the tightened warning gate now
      wired into all four build paths - `/W4 /WX` (MSVC) or
      `-Wall -Wextra -Wpedantic -Werror` (MinGW/GCC). A warning is now a
      build FAILURE, not just scrollback to skim past - if this is the
      first time this fires (i.e. an existing warning surfaces that
      predates this change), report the exact compiler output so it can
      be fixed at the root cause rather than suppressed.
- [ ] The automated test suite passes (`tests\run_tests.ps1`, or the
      `FishBalanceCoreTests` CTest target) - it also now builds under the
      same tightened warning gate
- [ ] Debug launch works in Visual Studio (Startup Item is set to
      `FishBalanceManager.exe`, not left on a default/CMakeLists target)

## Data Entry

- [ ] Supplier/Species autocomplete suggests and highlights correctly as
      you type; typing past the suggested prefix re-searches (e.g. `J`
      then `e` finds a different match than `J` alone)
- [ ] Tab moves through Supplier → Species → Kgs → Price → Notes in order
- [ ] Enter (from any of those fields) adds the row and returns focus to
      Supplier
- [ ] Date picker defaults to today, opens/closes normally, selected date
      is saved with the entry
- [ ] Notes field is optional, saves correctly, shows in the list
- [ ] Duplicate Last Entry pre-fills the form from the most recent entry
      and puts focus in Kgs with the value pre-selected
- [ ] Entering a `|` in Supplier/Species/Notes is rejected with a clear
      message (not silently corrupted)
- [ ] **(v0.9.48 / F5)** Type `1000+oops+250` into Debtor or Cash: the
      Difference line shows "cannot check - invalid entry" (with the bad
      term named) instead of a number, and Finalize Day refuses with an
      "Invalid Entry" message rather than the usual "Not Balanced" one
- [ ] **(v0.9.48 / F5)** Type `100++20`, `100+-20`, `100--20`, or a
      trailing `100+` into Debtor or Cash: all rejected the same way
- [ ] **(v0.9.48 / F5)** Type `-50+100` into Debtor: still accepted and
      computes to `50` (leading sign on the first term is still valid)
- [ ] **(v0.9.48 / F5)** Fix the invalid entry back to a normal sum: the
      Difference line and Finalize Day both return to normal immediately
- [x] Supplier/Species/Kgs/Price labels line up vertically with their
      boxes (v0.9.42 fix) - confirmed working 2026-09-30
- [x] Supplier/Species combo boxes paint their border/dropdown-arrow
      immediately on startup (v0.9.47 fix - synthetic `WM_MOUSEMOVE`).
      **Confirmed fixed by Jack 2026-09-30** - box now renders correctly
      on load. Closes a bug tracked since `[0.9.3]`.
- [ ] Outlier price flagging (v0.9.23: silent, no dialog): after 4+
      same-day entries for a species in a normal range, an entry way
      outside that range commits immediately with no popup, showing the
      warning marker + red tint in the Entries list
- [ ] Enter a bad price FIRST for a species (before any other entries that
      day), then 4+ correctly-priced entries after it - the first entry
      should retroactively get flagged once the group is large enough,
      not stay silently wrong; deleting/undoing an entry should also
      re-check its siblings the same way
- [ ] Enter 5+ identical (or near-identical) prices for a species, then a
      price within ~20% of that (e.g. five $10s then a $12) - should NOT
      be flagged; a genuinely wrong price on the same baseline (e.g. $50)
      should still be flagged

## Editing & Deleting

- [ ] Double-click a row loads it into the form; Update Entry saves the
      change; Clear/Cancel Edit discards it
- [ ] Double-clicking a FLAGGED row shows the review dialog first (No
      clears the flag without opening the form, Yes proceeds to edit);
      double-clicking a normal row skips straight to the edit form
- [ ] Right-click a flagged row shows "Clear flag"; right-click a normal
      row shows no context menu
- [ ] Delete asks for confirmation showing the correct row's details
- [ ] Edit > Undo Delete restores the most recently deleted row
- [ ] Sorting: click each column header, confirm ascending/descending
      toggles and the order is actually correct (including Date, which
      should sort chronologically)
- [ ] Filter box narrows the list live and clears correctly

## Save / Load / Files

- [ ] New / Open / Save / Save As all behave as expected; title bar shows
      the correct filename or "(unsaved)"
- [ ] Autosave survives an app restart (close without saving, reopen,
      data is still there)
- [ ] Recent Files list shows the last few files and opens them correctly
- [ ] An **old `.fbd` file** (saved before Date/Notes existed) still loads
      correctly, with blank Date/Notes rather than an error
- [ ] Window size, position, and maximized state are restored on restart
- [ ] A `backups\` folder appears next to the .exe with timestamped
      `backup_YYYYMMDD_HHMMSS.fbd` snapshots after normal use (roughly
      every 3 minutes) and immediately after File > Save / Save As
- [ ] File > Restore from Backup... loads a chosen backup correctly,
      title bar shows "(unsaved)" afterwards (not the backup's own
      filename), and File > Save As is required to keep it
- [ ] **(v0.9.48 / F1)** Normal case: open a named file, make a change
      (autosave fires), close the app normally, relaunch - it reopens
      still associated with that same named file (title bar shows its
      name, not "(unsaved)")
- [ ] **(v0.9.48 / F1)** Crash-recovery mismatch case: open named file A,
      close normally (so `settings.txt` remembers A). Relaunch, open a
      *different* named file B, then kill the app from Task Manager
      (simulating a crash - do NOT use File > Exit). Relaunch again: the
      app should show an "Recovered Unsaved Work" message and open with
      B's data but the title bar showing "(unsaved)", NOT silently
      re-associated with A. Doing a Save from here should prompt Save As
      / write a fresh file, not silently overwrite A.
- [ ] **(v0.9.48 / F1)** Old-autosave case: if you have an `autosave.fbd`
      left over from before this version (no `SOURCE_FILE=` line), the
      first launch after upgrading shows the same "Recovered Unsaved
      Work" message once; the data itself should still be intact.
- [ ] **(v0.9.48 / F2/F3)** Hand-edit a `.fbd` file to have two `BEGIN`
      lines, or two `DEBTOR=` lines, or a corrupted row (wrong number of
      `|`s, or a negative Kgs) mixed in with otherwise-good rows, then
      File > Open it: the WHOLE file should be rejected with the usual
      "could not open" message - it should NOT open with only the good
      rows loaded.

## Finalize Day (v0.9.40-0.9.42, ROADMAP.md item 3)

- [x] Debtor/Cash labels line up vertically and neither wraps/clips
      (v0.9.41 fix) - confirmed working 2026-09-30
- [x] Finalize, then "Start a new entry sheet now?" → Yes gives a fully
      unlocked, unsaved blank sheet — not still locked to the finalized
      file (v0.9.41 fix) - confirmed working 2026-09-30

- [ ] Finalize Day is blocked with a clear message while Debtor+Cash
      doesn't balance against the entered total; no date prompt appears
- [ ] Once balanced, Finalize Day prompts for a date (defaulting to today
      or the current file's own date if its name parses as one), and
      confirming writes `history\<date>.fbd` next to the exe
- [ ] After finalizing, the entry form, Add/Edit/Delete/Duplicate, and
      Debtor/Cash are all disabled; the button relabels to Un-finalize Day
- [ ] Status bar shows the finalized date while locked
- [ ] "Start a new entry sheet now?" appears after finalizing; Yes clears
      the list/Debtor/Cash without touching the just-finalized file or its
      lock, No leaves the locked screen as-is
- [ ] Un-finalize Day asks only for a Yes/No confirmation, then re-enables
      everything
- [ ] Closing and reopening a finalized file restores the locked state
      correctly
- [ ] Finalizing again onto a date that already has a `history\<date>.fbd`
      file warns before overwriting it
- [ ] **(v0.9.48 / F9)** Hard-to-trigger, but worth a sanity pass: finalize
      a day while the currently-open named file is on a read-only or
      otherwise write-protected path (e.g. mark the file read-only in
      Explorer first). The permanent `history\<date>.fbd` record should
      still be written and the day should still be marked finalized, but
      you should see a warning that the open file couldn't be re-saved
      and needs a manual Save - not an unqualified "Finalized" success
      message. Remove the read-only flag and use File > Save to confirm
      it then saves normally.

## Reports

- [ ] Total Overview: per-supplier totals and grand total are correct
- [ ] Breakdown: Supplier > Species > Price grouping and subtotals are
      correct
- [ ] By Species: Kgs/Total correct; Avg/Highest/Lowest price columns
      populate correctly (this tab has broken before — see
      ARCHITECTURE.md's `ListView_SetItemText` gotcha; check it
      specifically after any change touching this tab)
- [ ] CSV export includes all four sections (Entries, Overview, By
      Species, Breakdown) with correct data, opens cleanly in Excel

## Printing

- [ ] Print Preview shows an accurate page render, Next/Previous page
      navigation works if there's more than one page
- [ ] "Print..." from preview hands off to the real print dialog
- [ ] Printing to "Microsoft Print to PDF" produces a correct PDF
- [ ] Printing to a real physical printer produces correct output (can't
      be verified without a printer, but at minimum verify the page
      doesn't come out garbled if a printer is available)

## Email

- [ ] Emailing one supplier opens a correct, pre-filled draft
- [ ] Emailing multiple suppliers opens them **one at a time** with a
      confirmation between each — not all at once
- [ ] A supplier with no saved email still gets a draft, with the To
      field blank and the supplier's name in the Subject line
- [ ] A supplier with a saved email gets it filled in automatically

## Manage Names

- [ ] Selecting multiple names and typing a target correctly
      merges/renames them across every entry
- [ ] Setting/clearing an email address for a single supplier works and
      persists (`emails.txt`)

## Display / DPI

- [ ] No labels or buttons are clipped top/bottom or side to side,
      especially at non-100% display scaling (125%/150%) if you have a
      way to test that
- [ ] Resizing the main window keeps everything laid out sensibly

## Full regression pass

Run every checklist above from scratch, in order, using a fresh
`autosave.fbd` (rename or delete the existing one first) so you're
testing the real first-run experience, not a pre-populated one.
