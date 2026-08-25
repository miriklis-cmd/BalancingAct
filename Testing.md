# Testing

There's no automated test suite — Claude has no compiler and can't run the
Windows executable, so every check here is manual, performed by you after
building. This file exists so "what should I check" doesn't have to be
re-derived from chat scrollback every time. Update it whenever a new
feature is added; treat it as a living checklist, not a one-time list.

Use `## Full regression pass` for a from-scratch check after a large batch
of changes. Use the per-feature sections to spot-check after a small,
targeted fix.

## Build

- [ ] Build succeeds with no errors (CMake, MinGW, or MSVC — whichever
      you're using)
- [ ] No new compiler warnings that weren't there before
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

## Editing & Deleting

- [ ] Double-click a row loads it into the form; Update Entry saves the
      change; Clear/Cancel Edit discards it
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
