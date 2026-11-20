# Testing

## Regression pass (do this once, then tick the rest off or delete what no longer applies)

A short pass that covers the core of the app. If all of it is clean, the older unticked "carried-over" checks further down can be treated as covered. Do it in the Win32 app (W) and the WinUI app (U).

- [ ] W+U: Build and `run_tests.bat` pass. App starts, last session comes back from autosave.
- [ ] W+U: Add 5 entries (including a Kgs sum such as `10+5`), edit one, delete one, Undo Delete it. Totals stay right.
- [ ] W+U: Type Debtor and Cash figures: the Balanced screen shows correctly; a wrong sum turns it into a clear mismatch.
- [ ] W+U: Save As, New, Open the file again: identical sheet. Recent Files lists it.
- [ ] W+U: Close without saving, reopen: the sheet comes back and counts as unsaved.
- [ ] W+U: Total Overview, Breakdown (double-click a row for the drill-down) and By Species agree with each other.
- [ ] W+U: Print Preview, then print to Microsoft Print to PDF: the PDF matches the preview.
- [ ] W+U: Export to CSV opens in Excel with all sections.
- [ ] W+U: Email one supplier: a correct draft opens (mailto, .eml or Gmail, per Settings).
- [ ] W+U: Finalize Day locks the sheet; Un-finalize unlocks it.
- [ ] W+U: Manage Names: rename a supplier, Undo, then merge two spellings; entries update.
- [ ] W+U: Both apps open at once: the second one says so and does not corrupt the autosave.
- [ ] W: Restore from Backup loads a chosen backup. (WinUI: not built yet.)

<!-- v0.9.67: docs refreshed for the integration suite (audit F25-F29 re-check) -->

There IS an automated test suite now (`tests/`, doctest-based, covering the
platform-independent logic in `FishBalanceCore.h` - parsing, aggregation,
CSV export, formatting; see ROADMAP.md item 0 and `run_tests.ps1`/
`run_tests.bat`). Run it after any change to `FishBalanceCore.h`. It can't
cover the Win32-specific code in `main.cpp` (window creation, message
handling, printing, file I/O) in full - a second Windows integration suite
(`tests/win32_integration/`, also run by `run_tests.bat`) covers real file I/O,
save-failure injection and headless startup, but not what you see on screen.
Claude has no compiler and can't run the
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

- [x] Finalize Day is blocked with a clear message while Debtor+Cash
      doesn't balance against the entered total; no date prompt appears
- [x] Once balanced, Finalize Day prompts for a date (defaulting to today
      or the current file's own date if its name parses as one), and
      confirming writes `history\<date>.fbd` next to the exe
- [x] After finalizing, the entry form, Add/Edit/Delete/Duplicate, and
      Debtor/Cash are all disabled; the button relabels to Un-finalize Day
- [x] Status bar shows the finalized date while locked
- [x] "Start a new entry sheet now?" appears after finalizing; Yes clears
      the list/Debtor/Cash without touching the just-finalized file or its
      lock, No leaves the locked screen as-is
- [x] Un-finalize Day asks only for a Yes/No confirmation, then re-enables
      everything
- [x] Closing and reopening a finalized file restores the locked state
      correctly
- [x] Finalizing again onto a date that already has a `history\<date>.fbd`
      file warns before overwriting it

(Items above confirmed by Jack 2026-10-17, v0.10.17. Only the read-only-file case below is still unchecked.)

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

## v0.10.x checks (added v0.10.18)

Tick as you do them. Items marked (done) were confirmed by Jack in chat on the date shown.

- [x] (done 2026-10-16, v0.10.16) Save As into a different folder, then File > Restore from Backup: the picker opens in `backups\`; pick a snapshot, confirm, "Backup Loaded".
- [x] (done 2026-10-17, v0.10.17) File > Open with unsaved changes: the "Discard the current data?" prompt appears; File > Recent Files entry opens; a missing file gives an error and changes nothing.
- [x] (done, v0.10.13) Finalize Day marks the open saved file as finalized: finalize, answer Yes to a new sheet, reopen the saved file - it is locked.
- [ ] Autosave: add an entry, click away, check `autosave.fbd` is updated; kill the app and reopen - the entry is back.
- [ ] Rolling backup: edit, wait over 3 minutes, a new `backup_...fbd` appears in `backups\`; wait again without editing - no new file.
- [ ] After any File > New / Open / Restore, the next edit marks the sheet unsaved again.

## v0.13.0 checks (master name lists, tick-box Manage Names)
Back up `emails.txt` and your working folder first. Start with a real day open. (v0.15.0: FIRST RUN with an old emails.txt - afterwards names.txt has the addresses on the supplier lines, emails.txt is now emails.txt.migrated, Email Suppliers and Manage Names still show the same addresses; set/clear an address and reopen; removing an unused supplier from the list drops its address. v0.14.3: add an entry - the Supplier box should come back with its name highlighted; type a new supplier and it overwrites (first letter should still auto-complete); Tab instead keeps it. v0.14.2: tick two DIFFERENT species and Merge - the tick box should start unticked; merging should change the entries but leave both names in the list and add no alias; with the box ticked it removes the old name and records an alias. v0.14.0: FIRST RUN moves autosave.fbd, backups\ and history\ from beside the exe into C:\BalancingAct\data - confirm they appear there, the old folders are gone, and Recovery/Finalize/Restore still find them; Manage Names: select a name, type an alias, Add alias - the "Also known as" column updates, Undo reverts it, Remove alias works, a typed real name is refused; entry form: type an alias and look at the status bar, Tab changes it to the real name. v0.13.3: with an alias set up (rename one name to another in Manage Names), type the start of the alias in Species - it should auto-complete to the alias, and on leaving the box change to the real name; typing a partial alias and pressing Add should ask "use <real name>?". v0.13.2: `settings.txt` and `recent.txt` move there too - after the first run the window should reopen where it was and Recent Files should still be listed; v0.13.1: both files now live in `C:\BalancingAct\data` - put your hand-made `names.txt` THERE before the first run; an `emails.txt`/`names.txt` beside the exe is copied across once if that folder has none.)
1. First open of Tools > Manage Supplier / Species Names creates `names.txt` beside the exe and fills it from the open sheet. Suppliers/Species buttons switch lists; the Email column disappears for Species.
2. Click one supplier, then another: only the second stays selected; the Name and Email boxes show it; NOTHING gets ticked. Click a tick box: only that box changes. "Merge ticked" stays grey until two are ticked.
3. Duplicates strip: if two names differ only by case/spaces/punctuation, it names them; "Tick these" ticks exactly those.
4. Tick two suppliers, Merge ticked: a dialog with a radio per name (most-used selected) and "N entries change". Pick one, Merge: entries on the Entries tab show the kept name, the status line says how many; the kept supplier has an email if either had one. Cancel changes nothing.
5. Undo (in the window) puts entries, names and emails back. Closing the window and reopening: Undo is greyed.
6. Rename: type a new name, Rename (or Enter): entries change. Type an existing name (even different capitals): asks to merge.
7. Email: edit and Save Email; a bad address is refused; clearing the box removes it. Email All Suppliers still uses it.
8. Add Entry: type `barramundi` when `Barramundi` is in the list - it becomes `Barramundi` in the row and the form. Type a name that is a prefix or one slip from an existing one: Yes/No/Cancel question, Cancel keeps the form as typed, nothing added. A genuinely new species goes in with a status-bar note and shows in the drop-down next time.
9. After merging Barra into Barramundi, typing `Barra` on Add Entry is corrected to `Barramundi` (status bar says so).
10. A species in Manage Names with 0 entries on this sheet can be removed with Remove from list; one the sheet uses cannot.
11. Esc closes the window from the list, the boxes and the Close button. Check at your usual 150% scaling that nothing is cut off.

## v0.12.10 checks (printing code moved)
Nothing should have changed - these confirm it. Open a file with a few suppliers, then:
1. File > Print Preview: the same pages as before (title, book summary, supplier column), Previous / Next work, Close closes.
2. In the preview click Print..., pick "Microsoft Print to PDF": the PDF has the same pages.
3. File > Print Breakdown (no preview first): same PDF.
4. Cancel the Print dialog: nothing happens, no error.
5. With no entries, Print Preview says "Nothing to preview yet." (not a blank page).

## v0.11.0 checks (Kgs sums)

- [x] (done 2026-10-23, v0.11.0) Type `24+24+24` in Kgs, add: the list shows 72.0; Edit Selected shows `24+24+24` again; Update keeps it.
- [x] (done 2026-10-23, v0.11.0) Type a plain `72`: edit shows `72.0`. Type `10+abc`: error naming `abc`. Empty Kgs: error.
- [x] (done 2026-10-23, v0.11.0) Save, close, reopen the file: the sum is still there on edit. Open it in the old prototype/other build only if you accept it is refused.
- [x] (done 2026-10-23, v0.11.0) Duplicate Last Entry on a summed row fills Kgs with the sum.
- [x] (done 2026-10-23, v0.11.0) Totals, Overview, Breakdown, By Species, CSV and print all use the total (72.0), not the text.

## v0.11.1 checks (denser print)

- [ ] Print Preview of a real day's Breakdown: about 3-4 pages instead of 6-7; text still readable; no row cut off at the bottom of a page; heading, summary and column headings look right on page 1 and the column headings repeat on later pages.
- [ ] Print to PDF gives the same layout as the preview.

## v0.16.1 check
- [ ] Add a few entries on an unsaved sheet, close the app, reopen (entries come back). File > New should now ask "Discard the current data and start a new sheet?".

## v0.16.0 checks (book fixes)
- [ ] New sheet (File > New), type a first entry, Add Entry: a "No Book Yet" box offers Debtor book / Cash book / No book / Cancel. Cancel adds nothing and keeps what you typed. Debtor book: the Book box shows Book1 and the row is in it. Second entry: no prompt.
- [ ] No book: the row has no book; later entries don't ask again.
- [ ] Select 2-3 rows, right-click one of the selected rows: menu "Move 3 selected rows to book" lists existing books, New Debtor / New Cash, No book. Pick one: all selected rows change, status bar says how many, Total Overview Books table updates.
- [ ] Right-click a row that is NOT selected: only that row is selected and moved. A price-flagged row still offers "Clear flag" first.
- [ ] Finalized day: right-click offers no Move option.

## v0.12.0 checks (Books)

Use a copy of a real day, or type a few entries.
- [ ] Data Entry shows a "Book" box ("(no book)") and a Start Book... button; the layout fits and nothing overlaps at your usual scaling.
- [ ] Start Book... > "Start next Cash book (Cash1)": box shows "Cash1 (Cash)". Add 3 entries. Start Book... > next Debtor book (Book1): add 3 more, then another Debtor book (Book2) with 2.
- [ ] Grid has a Book column; the first row of each book is bold with a light blue tint. Clicking the Book header sorts by book.
- [ ] Ctrl-click / Shift-click several rows: status bar (right) shows `Selected: N rows | kg | $` and matches a calculator. Select nothing: blank.
- [ ] With 2+ rows selected, Delete Selected Row / Edit Selected Row say to select just one row; with one row they work as before.
- [ ] Editing a row inside Book1 (change the kgs) keeps it in Book1 even if the current book is Book2.
- [ ] Total Overview: Books table beside the suppliers. Type Debtor `x+y` and Cash `z` matching your books: Difference is $0.00 for each; change one amount: that book's row goes red.
- [ ] Rows with no book appear as "Not in a book"; Debtor/Cash subtotal lines add up.
- [ ] Save, close, reopen the file: books, the Book column and the current book are all still there. Open a pre-0.12 file: loads, no books.
- [ ] Start Book... > "Add to Cash1 (n rows)" returns to an earlier book; "No book" makes new rows unassigned.
- [ ] Finalize Day and the History copy keep the books; after Finalize the Start Book button is disabled; File > New clears the current book.
- [ ] Data Entry shows about 16 grid rows at 1280x720 and the smaller reconciliation box still reads clearly (Debtor/Cash, Book/Entered, status/difference, Finalize).
- [ ] A file with Kgs sums AND books round-trips (the sum is still shown when editing).

## v0.12.1 - v0.12.5 checks (Books follow-ups)

- [ ] (0.12.1) Total Overview: all six Books columns (Book, Rows, Kgs, Entered, From the book, Difference) show without a horizontal scroll at 1280 wide.
- [ ] (0.12.2) Excel importer: `python tools\xlsx_to_fbd.py BALANCE_PivotTable.xlsx out.fbd --date 2026-10-23`, open `out.fbd`: entries, books and Debtor/Cash load; entered total matches Excel's.
- [ ] (0.12.3) Debtor `(15470.2+22.4-133);(26076.2+110)` gives a total of 41,545.80 and two per-book figures; `(1+2` and `5(1+2)` show the "cannot check" message.
- [ ] (0.12.4) Kgs `8*30` adds as 240.0, Edit shows `8*30` again; `(8*30)+16` = 256.0; Debtor/Cash typed with `*` is refused.
- [ ] (0.12.5) Cash box `2436.5+56+1900` with one Cash book: Books table shows the Cash1 figure as 4,392.50 (one book), not just the first amount.
- [ ] (0.12.5) Two Cash books and Cash box `2436.5+56+1900;980.25`: Cash1 compares with 4,392.50, Cash2 with 980.25.
- [ ] (0.12.5) `100;` or `1;;2` in a box: the box total still works (the `;` counts as +, so `100;` is "cannot check" like any dangling operator) and no per-book figures appear until it is fixed.
- [ ] (0.12.5) Hover the mouse over the Debtor box, then the Cash box and keep it still for a moment: a tooltip appears explaining that `;` starts the next book, with an example; it stays readable (not cut off) at 150% scaling and disappears when the mouse moves away.
- [ ] (0.12.5) The tooltip also appears after switching to another tab and back, and after Un-finalize Day (when the boxes are enabled again).

## v0.12.9 checks (drill-down sort + Esc)
- [ ] Expect 287 portable test cases (282 + 5 sort cases in `test_drilldown.cpp`); build is clean with `/W4 /WX`.
- [ ] Double-click a `<species> Total` row that has several prices. Click the `Price ($/kg)` header: rows sort by price ascending with an up arrow on the header; click again: descending, down arrow.
- [ ] Same for `Kgs` (10.0 sorts after 4.5, not before) and `Total ($)`; also `Date`, `Species`, `Supplier`.
- [ ] Rows with the same price stay in date order when sorting by price.
- [ ] The bold total line does not change when you sort.
- [ ] Close the popup and open another row: it starts in the default order (no arrow) again.
- [ ] Press Esc with a row selected in the list: the popup closes and the main window is usable again.
- [ ] Esc on the main window itself does nothing new.

## v0.12.8 checks (Breakdown print layout)

- [ ] Print Preview of the same day that printed as 4 pages (2026-10-25 data, 140 entries): now 3 pages, with a little room left on page 3. (Layout depends on the day's data; a much bigger day can still need more.)
- [ ] The supplier name appears once, in the left column, on the supplier's first row (not on a line of its own); a supplier that runs over onto the next page is named again on that page's first row.
- [ ] "<Supplier> Total" and GRAND TOTAL are bold and in the left column; every species still has its bold "<Species> Total" line.
- [ ] A hairline and small gap separate suppliers; none directly under the column header at the top of a page.
- [ ] Pages 2 and 3 start straight with the Supplier / Species / Price / Kgs / Total header (no title or "Generated" line); page 1 still has the title, the Book Total / Entered / Difference lines.
- [ ] Species names and "<Species> Total" labels do not run into the Price column (longest names, e.g. "Ckd Sch Prawn Total"); same check in the saved PDF and on a real printer.

## v0.12.6 - v0.12.7 checks (Breakdown drill-down)

- [ ] Expect 282 portable test cases (275 + 7 in `test_drilldown.cpp`) and 25 integration; build is clean with `/W4 /WX`.
- [ ] Breakdown tab, double-click a price row (e.g. `Flathead $12.50`): a popup lists only that supplier's entries at that price, with supplier, species, date, kgs, price and total, and a bold total line. The line's kgs and dollars equal the clicked row's Kgs (weight) and Total ($).
- [ ] Double-click a `<species> Total` row: all of that supplier's entries of the species; the total line equals that row.
- [ ] Double-click a `<supplier> Total` row: every entry of that supplier; the total line equals that row.
- [ ] Two suppliers with the same species and price: each supplier's row lists only its own entries.
- [ ] Double-click a row of a supplier named with an `&` (e.g. `Jenkins & Son`): the header shows the `&` literally (no underlined letter).
- [ ] The popup is readable at 150% scaling (nothing cut off), stays inside the screen, can be resized, and Close or the window X closes it; the main window is usable again straight away.
- [ ] While the popup is open the main window cannot be clicked (like Manage Names). After closing, double-clicking another row works.
- [ ] Double-click on the empty area below the last row does nothing. Double-click on the Entries tab list still opens the edit/review as before.
- [ ] Build with `build_mingw.bat` if you have MinGW (it failed before 0.12.6 because of the `#pragma comment` lines).

## Carried-over checks (never recorded as confirmed)

These were on the old roadmap's unticked list. Several were probably checked casually; none were written down. Tick or strike each once you have looked.

- [x] Finalize Day full pass - confirmed 2026-10-17 (see the "Finalize Day" section above). Only the read-only-file case (F9) remains unchecked.
- [ ] Labels (Supplier / Species / Kgs / Price, and Cash) line up with their boxes at your usual display scaling.
- [ ] Supplier/Species combos: several fresh launches in a row - the grey-strip glitch is a **known open bug** (ROADMAP.md "Known problems"); note whether it happened.
- [ ] A forced backup failure (make `backups\` read-only): one "Backup Snapshot Failed" warning, not repeated, autosave still works.
- [ ] A half-typed Add Entry draft survives a crash: type, Tab away, kill from Task Manager, reopen.
- [ ] Renaming a supplier in Manage Names (no conflict) keeps its email address.
- [ ] A build with CMake and with MinGW (`build_mingw.bat`) has zero warnings - neither has ever been run.

## Full regression pass

Run every checklist above from scratch, in order, using a fresh
`autosave.fbd` (rename or delete the existing one first) so you're
testing the real first-run experience, not a pre-populated one.
- [ ] (0.17.0) A sheet with `lucas` and `Lucas` (e.g. the Excel-converted pivot_test.fbd): Overview, Breakdown and By Species list ONE Lucas; double-click it in Breakdown - all rows listed; totals unchanged.
- [ ] (0.17.0) Type `Jtmoyce` for a supplier whose master name is `J T Moyce`: the entry is stored as `J T Moyce`; an older entry saved as `Jtmoyce` shows under `J T Moyce` in the reports.
- [ ] (0.17.1) Supplier box: type `jtm` - `J T Moyce` appears with the end highlighted; Tab accepts it; keep typing `o` instead - it carries on (`J T Mo...`); Backspace on the highlight just deletes it. Same in Species (e.g. a name with a slash or space).
- [ ] (0.17.2) With an alias `S/Mlt` and names like `Sml Garfish`: type `s/m` then `s/ml` in Species - it offers `S/Mlt`, not `Sml ...`.
- [ ] (0.17.3) Open a saved file, add one entry, close the app WITHOUT saving, reopen: the entry is there and the title shows it as changed; File > New asks "Discard the current data...". After Save, close, reopen: File > New does not ask.

## v0.18.0 / WinUI step 13 checks (shared autosave)
- [ ] WinUI alone: add a few entries, wait 2 seconds, close with X and choose Exit-without-saving. Reopen WinUI: the entries are back ("Recovered your last session..."), title shows unsaved changes, File > New asks.
- [ ] WinUI then Win32: with WinUI closed, start Win32: it recovers the same sheet. And the reverse (Win32 closed with entries, start WinUI).
- [ ] Both open: start Win32 first, then WinUI (and the reverse): the second shows "Another Copy Is Open", starts blank. Closing the second must not change what the first autosaves. Close both, start one: it recovers the FIRST copy's work.
- [ ] Open a saved file in WinUI, add an entry, close without saving, reopen: the entry is there and the sheet counts as unsaved.
- [ ] Save, close, reopen WinUI: File > New does not ask. Look in C:\BalancingAct\data\backups after 3+ minutes of editing: backup_*.fbd files appear (from either app).
- [ ] (0.18.1) Email All Suppliers (or one supplier): the draft body has a dated intro, a ruled KG / SPECIES / PRICE table with the numbers lined up, and "kg in total" at the foot. Check it in your real email app - if it looks ragged there (proportional font), tell me.
- [ ] (0.18.4) Open a supplier email draft in Outlook: KG / SPECIES / PRICE separated by tabs; check the PRICE column lines up on rows with short and long species names (long names may be one stop out).

### Portable copy (v0.18.5)
1. Put FishBalanceManager.exe in a new folder with an empty `portable.txt`; run it. Title shows [PORTABLE TEST COPY]; a `data` folder appears beside the exe.
2. Add entries, save, close, reopen: data comes back from `data\autosave.fbd`; `C:\BalancingAct\data` is untouched (check timestamps).
3. With the real app open, start the portable copy: both run at once.
4. Delete the whole folder: nothing left behind.

## Embedded manifest (v0.18.6)

1. Copy ONLY FishBalanceManager.exe to a new folder (no .manifest file); run it. Buttons are rounded/tinted, tabs themed, Supplier/Species boxes draw correctly.
2. Delete any old FishBalanceManager.exe.manifest beside the exe; behaviour is unchanged.

## v0.19.0 checks (summary-tab filter)

1. Enter entries for 2+ suppliers/species. On Total Overview, type part of a supplier name in Filter (top right): only matching suppliers show; GRAND TOTAL (filtered) equals the sum of what is shown.
2. Switch to Breakdown and By Species: the same text is already in their Filter boxes and both lists are narrowed. Type a species instead: matching rows only.
3. Clear the box: everything returns, label goes back to GRAND TOTAL.
4. With a filter active: Books table, the Balanced check on Data Entry, Print Preview, Email Suppliers still cover the WHOLE day.
5. Add/delete an entry while filtered: lists stay filtered and update. The entries list's own Filter (Data Entry tab) is unaffected.
6. Narrow the window: the Filter box on Overview never overlaps the three labels.

## v0.20.0 checks (Tools > Email method)

1. Tools menu shows three Email method lines; "open in my mail program (mailto)" is ticked by default; the Gmail line is greyed out.
2. Choose "Outlook draft with a table (.eml)": the tick moves. Close and reopen the app: still ticked (settings.txt has EMAILMODE=eml).
3. With entries for 2+ suppliers, Breakdown > Email All Suppliers: the confirmation says "as Outlook drafts (with a price table)"; each supplier's draft opens in Outlook as an unsent message with the To address filled in and a neat three-column table; the next one only after OK.
4. A supplier with no saved address opens with To blank.
5. Switch back to mailto: behaves exactly as before.
6. If Outlook is not the .eml default: a clear message appears, and mailto still works.

## v0.21.0 checks (Gmail drafts)

Needs the Google client file (client_secret.json) in `C:\BalancingAct\private\` (or pick it when asked). Optional: add `GMAILACCOUNT=accounts@jackmiriklis.com` as a line in settings.txt (close the app first).

1. Tools > Email method > "Gmail draft": tick moves; restart keeps it.
2. Breakdown > Email All Suppliers: confirmation says "create N draft emails in Gmail ... Nothing is sent". Yes.
3. First time on this PC: info box, then the browser opens; sign in as the drafts mailbox, Allow (only "compose new draft emails" permission listed). Tab says signed in; the app continues by itself.
4. "Created N drafts ... Open Gmail Drafts now?" Yes: Gmail Drafts shows one draft per supplier, To filled in, subject "Prices - <date> - <supplier>", neat table. Edit one and send it normally.
5. Run it again: NO browser this time (saved sign-in); drafts are created straight away.
6. Tools > Gmail: forget this PC's sign-in, then Email All Suppliers again: the browser sign-in comes back.
7. Cancel the browser sign-in (close the tab, wait 3 min or click Cancel in Google): the app reports it, no crash, window usable.
8. Unplug the network, Email All Suppliers: clear "couldn't reach Google" message; saved sign-in is NOT thrown away (works again when online).
9. Do steps 3-5 on the laptop too (each PC signs in once).

## v0.22.0 checks (Tools > Settings)

1. Tools menu now has Manage Names and "Settings..."; the old Email method lines are gone.
2. Settings opens centred, main window is disabled behind it; three tabs switch cleanly; Esc/X/Cancel close without changes.
3. Email tab: change method, press OK, reopen - choice kept; restart - still kept. Email All Suppliers follows it.
4. Change subject/opening/sign-off, OK, run an email (mailto/.eml/Gmail): wording appears; {supplier}/{date} are filled. Blank boxes restore standard wording.
5. "Forget this PC's Gmail sign-in" asks first; says so if there is none.
6. Backups tab: 0 or 61 minutes, or 5 snapshots, shows a warning and stays open; valid values are kept after restart.
7. Price-flag sensitivity: switch to Strict - more yellow/flagged prices appear immediately; Relaxed - fewer. Survives restart.
7a. Data tab: path matches your data folder; Open folder opens it; version is correct (portable copy says so).
8. Window looks right at 125%/150% display scaling (no clipped text).

## v0.23.0 checks

1. Enter a sheet dated a few days ago (date picker), Email All Suppliers (any method): subject and opening line show THAT date, not today. Try each date format in Settings > Display & printing.
2. Settings > Display & printing: Large, OK - message says it applies next start; restart: lists (entries, Overview, Breakdown, By Species, drill-down) have bigger text with columns still readable; buttons/labels unchanged. Back to Normal restores.
3. Printer drop-down lists your installed printers plus "Windows default printer". Pick one, A4, Landscape, OK. Print Breakdown: the dialog opens on that printer; Print Preview page is landscape A4 shaped.
4. Choose Printer default / Printer default: behaves as before. Remove or rename the saved printer in settings.txt: falls back to the Windows default with no error.
5. Microsoft Print to PDF as the saved printer works the same way.
6. Settings tabs: four tabs fit in the window at 125%/150% display scaling.

## WinUI step 14 checks (Tools > Settings)

0. Add `src/services/GmailDraft.cpp` to the WinUI project (Add > Existing Item; C/C++ > Precompiled Headers: Not Using). Build. If anything fails to compile, send me the first error.
1. Tools menu now has "Settings...". It opens a dialog with four tabs; Cancel changes nothing, OK says "Settings saved." in the status bar.
2. Change something in WinUI, close, open the Win32 app: Tools > Settings shows the same value (and the other way round).
3. Text size Large / Extra large: lists on all tabs grow at once; check long names are not cut off badly (column widths are fixed).
4. Email All Suppliers with each method: mailto (plain), .eml (Outlook table), Gmail (drafts created, offer to open Drafts). The date in the subject/opening line is the SHEET date; wording matches what you typed.
5. Price-flag sensitivity Strict/Relaxed: flags on the Entries list change immediately.
6. Backups: set 1 minute and 10 snapshots, add entries over a few minutes: new backup_*.fbd appear about every minute and only 10 are kept.
7. Print Preview / Print Breakdown start on the chosen printer, paper and orientation.
8. Dialog fits at your usual display scaling and scrolls if the window is short.

### WinUI Print dialog

- [ ] File > Print Breakdown, Print / Save as PDF... and Print... in Print Preview all open the WinUI Print dialog (not the Windows one).
- [ ] Printer list shows installed printers with the saved printer selected (Windows default if none saved); paper / orientation start from Settings.
- [ ] Print sends the job to the chosen printer (not the Windows default); status bar says "Sent to <printer>."
- [ ] Copies = 2 gives two copies on a real printer; Cancel prints nothing.
- [ ] Microsoft Print to PDF produces a correct PDF.

### v0.23.2 checks (WinUI)

- [ ] Delete a row, then Edit > Undo Delete: the row comes back in the same place and totals return to what they were. The menu item is greyed when there is nothing to undo, and after New / Open.
- [ ] Help > About opens, shows the version (0.23.2) and scrolls if the window is short.
- [ ] Save or open a file: it appears under File > Recent Files (newest first); clicking it opens it (asks first if there are unsaved changes). The Win32 app shows the same list.
- [ ] Rename or delete a recent file on disk, then click it: a message says it no longer exists and it leaves the list.
- [ ] Move / resize the window (and once maximised), close, reopen: it comes back where it was. Delete the `WUI*` lines from settings.txt: it starts maximised.
- [ ] File > Export to CSV...: the file opens in Excel with Entries and every report, and matches the Win32 export of the same sheet.
- [ ] Win32 app: File > Export to CSV still works (the function changed signature).
