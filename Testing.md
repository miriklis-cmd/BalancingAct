# Testing

How to use this file: the **Regression pass** is the quick check after any build. **Still to check** lists the things only a
person on a real machine can confirm and nobody has yet. **Reference checklist** is the by-area list to spot-check after a
targeted change. The older per-version checklists (v0.9 to v0.24.3) were removed on 2026-11-25: every feature in them has
been through the Regression pass and daily use, or is covered by the automated tests, or was replaced by a later version.

Automated tests: `run_tests.bat` (project root) runs 353 portable test cases (`tests/`, doctest) plus the Windows
integration suite (`tests/win32_integration/`: real file I/O, injected save failures, headless startup). Run it after any
change. Neither suite can see what is on screen, so the manual checks below remain.

Claude has no Windows or MSVC, so every change to `main.cpp` or the WinUI project is "should work" until Jack has built and
run it. Record results here with a name and date.

## Regression pass (W = Win32 app, U = WinUI app)

All confirmed by Jack on both apps, 2026-11-20 / 2026-11-23.

- [x] W+U: Build and `run_tests.bat` pass. App starts, last session comes back from autosave.
- [x] W+U: Add 5 entries (including a Kgs sum such as `10+5`), edit one, delete one, Undo Delete it. Totals stay right.
- [x] W+U: Type Debtor and Cash figures: the Balanced screen shows correctly; a wrong sum turns it into a clear mismatch.
- [x] W+U: Save As, New, Open the file again: identical sheet. Recent Files lists it.
- [x] W+U: Close without saving, reopen: the sheet comes back and counts as unsaved.
- [x] W+U: Total Overview, Breakdown (double-click a row for the drill-down) and By Species agree with each other.
- [x] W+U: Print Preview, then print to Microsoft Print to PDF: the PDF matches the preview.
- [x] W+U: Export to CSV opens in Excel with all sections.
- [x] W+U: Email one supplier: a correct draft opens (mailto, .eml or Gmail, per Settings).
- [x] W+U: Finalize Day locks the sheet; Un-finalize unlocks it.
- [x] W+U: Manage Names: rename a supplier, Undo, then merge two spellings; entries update.
- [x] W+U: Both apps open at once: the second one says so and does not corrupt the autosave.
- [x] W: Restore from Backup loads a chosen backup. U: same (v0.23.4, confirmed 2026-11-25).

## Still to check (only Jack can do these)

Tick each when done and add the date. Delete any you decide you do not care about.

Email (needs the other machine):
- [ ] mailto and .eml: the notes and the signature show in the draft.
- [ ] In each mode (mailto / .eml / Gmail) is the signature DOUBLED because your mail program adds its own? Tell Claude which.

Display:
- [x] v0.24.4: Win32 Settings > Email: the Signature box shows about 5 lines, the grey note under it reads in full, and OK / Cancel are fully visible, at your usual display scaling. (Jack, 2026-11-26)
- [x] Settings > Display and printing, both apps: text size Large / Extra large (applies at next start; lists bigger, columns readable), email date style, printer / paper / orientation start the Print dialog and Preview as chosen. (Jack, 2026-11-26, after the Win32 fix in v0.24.7)
- [ ] Supplier / Species boxes: several fresh launches in a row - note whether the grey strip glitch (ROADMAP "Known problems") still happens in the Win32 app. STILL HAPPENS 2026-11-26 (v0.24.7): Species box drawn in pieces plus a stuck, faded Debtor tooltip over the list. Not fixed; WinUI does not have it. Left open. Jack rates this a REAL PROBLEM (Win32).
- [x] Supplier / Species boxes (both apps): typing part of a name completes it, Tab accepts, typing on carries on; typing the start of an alias completes to the alias and is corrected to the real name when you leave the box. (Jack, 2026-11-26)

Crash and failure cases (hard kill with `taskkill /F /IM <exe name>` or Task Manager > Details > End process; do NOT use Exit or the Processes tab):
- [x] Open named file A, close normally. Open file B, kill the app. Relaunch: "Recovered Unsaved Work", B's data, title shows "(unsaved)", NOT linked to A. Save prompts Save As. (Logic is unit-tested; the screen and wording are not.) (Jack, 2026-11-26, Win32. Must be a HARD kill: `taskkill /F /IM FishBalanceManager.exe`; Task Manager "End task" on the Processes tab closes the app politely and proves nothing.)
- [x] WinUI: same crash recovery. Comes back with "(unsaved changes)" and a status bar message, no popup (by design). (Jack, 2026-11-26)
- [x] A half-typed Add Entry row survives a hard kill and reopens in the form (type supplier, species, kgs, price; wait 2 seconds; kill). Win32 works (Jack, 2026-11-27). WinUI works as of v0.24.13 (Jack, 2026-11-27); v0.24.10 to v0.24.12 fixed the saving and the display in steps.
- [x] Finalize Day while the open named file is read-only: the history file is still written and you get a warning that the open file could not be re-saved. Works in both apps (Win32 and WinUI: Jack, 2026-11-28; both use the same core finalize logic).
- [x] Win32: Backup snapshot failure shows a "Backup Snapshot Failed" warning (Jack, 2026-11-28). Method that works: close the app, rename `backups` to `backups_keep`, create an empty FILE named `backups`, start the app. (Read-only on a folder in Explorer does nothing.)
- [x] Price-flagged row: right-click Clear flag works in both apps (Jack, 2026-11-27).

Gmail failure paths (the normal path is in daily use):
- [ ] Cancel the browser sign-in: the app reports it, no crash, window usable.
- [ ] Unplug the network, Email All Suppliers: a clear "couldn't reach Google" message and the saved sign-in is kept.
- [ ] Do the first sign-in once on the laptop too (each PC signs in once).

Printing and copies:
- [ ] A real physical printer: page not garbled, species names do not run into the Price column. WinUI Print dialog: Copies = 2 gives two copies.

Portable and copied exe:
- [x] Portable copy: exe in a new folder with an empty `portable.txt`: title shows [PORTABLE TEST COPY], a `data` folder appears, `C:\BalancingAct\data` untouched, runs alongside the real app, deleting the folder leaves nothing behind. (Jack, 2026-11-28, on the other machine.)
- [ ] Copy ONLY the exe (no manifest file) to a new folder and run it: buttons and tabs themed, boxes draw correctly.

WinUI Settings:
- [ ] Backups: 1 minute and 10 snapshots; add entries over a few minutes: new `backup_*.fbd` about every minute, only 10 kept.

## Reference checklist (spot-check after a targeted change)

Everything in this section passed in the Regression pass and in daily use. Use it to decide what to look at after a change.

### Build
- Build succeeds with no warnings (`/W4 /WX`; a warning fails the build). The Win32 app (`build_msvc.bat`) and the WinUI solution both build.
- `run_tests.bat` passes (portable and integration suites).

### Data Entry
- Tab order Supplier, Species, Kgs, Price, Notes; Enter adds the row and returns focus to Supplier.
- Date defaults to today; Notes optional; Duplicate Last Entry pre-fills and selects Kgs.
- `|` in Supplier / Species / Notes is refused with a message.
- Kgs accepts sums and products (`24+24+24`, `8*30`, `(8*30)+16`); Edit shows the text again; list shows the total.
- Debtor / Cash: `1000+oops+250`, `100++20`, a trailing `+` are all "cannot check - invalid entry" and Finalize refuses; `-50+100` is valid; fixing it returns to normal. (Unit-tested.)
- Unusual prices are flagged silently (yellow marker); a bad first price is flagged once enough others exist; deleting or Undo re-checks. (Unit-tested.)
- Books: Start Book, Book column, move selected rows to a book, per-book check on Total Overview; first entry on a new sheet asks for a book.

### Editing and filters
- Double-click edits; flagged rows ask first; Delete confirms; Edit > Undo Delete; column sorting (Date sorts by date); Filter box narrows live. WinUI Overview / Breakdown / By Species filter boxes are on the roadmap (B3).

### Save / Load / Files
- New / Open / Save / Save As, title shows file name or "(unsaved)"; Recent Files; window position restored.
- Autosave survives a restart; backups appear in `C:\BalancingAct\data\backups` about every `BACKUPMINUTES`; Restore from Backup opens there and always needs Save As afterwards.
- A hand-edited `.fbd` with a second `BEGIN`, a second `DEBTOR=`, a bad row or a negative Kgs is rejected as a whole. Old 4- and 6-field files still load. (Unit-tested.)

### Finalize Day
- Blocked while out of balance; asks for a date; writes `history\<date>.fbd`; locks entry and Debtor/Cash; Un-finalize asks yes/no; reopening a finalized file is still locked; re-finalizing a date warns before overwrite; Yes to "start a new sheet" gives an unlocked blank sheet.

### Reports, CSV, printing
- Total Overview, Breakdown (drill-down on every row type; click a header to sort), By Species (average / highest / lowest price, not weighted) agree. Names differing only in case, spaces or punctuation are one supplier.
- CSV has Entries, Overview, By Species, Breakdown and opens in Excel.
- Print Preview matches the PDF; page count is about 3 for a day of ~140 entries; the supplier name shows once on its first row.
- After any change touching By Species, check it specifically (see ARCHITECTURE.md `ListView_SetItemText` gotcha).

### Email
- Notes dialog opens every time with only suppliers that had sales; NOT SOLD / UP / DOWN per supplier; Skip asks first if notes were typed; Cancel makes nothing; blank headings are left out.
- Second set on the same sheet asks first (default No); opening another file resets it.
- Gmail: drafts in Gmail with a table, notes under it, signature last; mailto and `.eml` open one at a time with a confirmation.
- A supplier with no saved address still gets a draft (To blank, name in Subject).

### Manage Names
- Select, tick, merge (radio to choose the kept name), rename, aliases, Undo, remove unused names; a supplier's email survives a rename; Esc closes.

### Both apps and settings
- A setting changed in one app shows in the other (same `settings.txt`).
- WinUI: Undo Delete, Help > About, Recent Files, remembered window position, CSV export, Restore from Backup, own Print dialog.
- No clipped labels at 125% and 150% display scaling; resizing keeps the layout sensible.
