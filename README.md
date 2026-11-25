# Fish Balance Manager

A native Windows desktop app (C++ / Win32 API) that replaces the
`BALANCE_PivotTable.xlsx` workbook.

**Project documentation:**
- [ROADMAP.md](ROADMAP.md) — what's done, what's being tested, what's next
- [CHANGELOG.md](CHANGELOG.md) — version history
- [ARCHITECTURE.md](ARCHITECTURE.md) — codebase structure and known gotchas
- [BUSINESS_RULES.md](BUSINESS_RULES.md) — domain decisions (units, tax, data integrity, email)
- [DATA_FORMATS.md](DATA_FORMATS.md) — the `Entry` schema and every file format
- [DevelopmentWorkflow.md](DevelopmentWorkflow.md) — how this project is actually built (chat-driven, no compiler on Claude's end)
- [Testing.md](Testing.md) — the manual test checklist to run after each build
- [SecurityHardeningRegister.md](SecurityHardeningRegister.md) — every security/data-integrity issue found and fixed

Current version: see `version.h`, or Help > About in the app.

- **Tab 1 – Data Entry**: enter Supplier, Species, Kgs, Price, Date (defaults
  to today), and an optional Notes field for each delivery. Enter the
  Debtor and Cash figures from your books (you can type `+`-separated sums,
  e.g. `250.7+1826+2552+286`, just like the original spreadsheet cells) and
  the app checks Debtor + Cash against the sum of what you've entered,
  flagging any mismatch in red. The Kgs box takes sums the same way (e.g.
  `24+24+24`); the entry stores the total and shows the sum again when you
  edit it.
  **Batch entry**: press Tab to move Supplier → Species → Kgs → Price →
  Notes, then press Enter to add the row — focus jumps straight back to
  Supplier so you can keep entering the next line without touching the
  mouse. Both Supplier and Species also autocomplete as you type, suggesting
  the closest existing match with the rest of the word highlighted — keep
  typing to refine it, or just Tab/Enter to accept it. "Duplicate Last
  Entry" pre-fills the form from the most recent row and jumps straight to
  Kgs with it selected — handy when the same supplier/species shows up
  again with just a different weight or price.
  **Fixing mistakes**: double-click any row in the list (or select it and
  click "Edit Selected Row") to load it back into the form. Correct the
  values and click "Update Entry" (the Add button relabels itself while
  editing) to save the change, or "Clear / Cancel Edit" to back out without
  changing anything. Deleting a row asks for confirmation first, and
  **Edit > Undo Delete** brings back the last one you removed.
  **Filter and sort**: the Filter box above the list narrows it down to
  matching Supplier/Species as you type; click any column header to sort by
  it (click again to reverse).
- **Tab 2 – Total Overview**: a Kgs and $ total for every supplier plus the
  grand total, with the same balance check repeated at the top.
- **Tab 3 – Breakdown**: entries grouped by Supplier (collapsible groups),
  then by Species and Price, showing the weight (Kgs) and $ total for each,
  with bold subtotal rows per species and per supplier.
  **Print Preview**: the "Print Preview..." button (or File menu) shows
  exactly how the report will look before you commit to printing, with
  Next/Previous page navigation and a "Print..." button that hands off to
  the real print dialog once you're happy with it.
  **Print / PDF**: the "Print / Save as PDF..." button (or File > Print
  Breakdown...) opens the standard Windows print dialog. Pick any physical
  printer to print it, or pick the built-in **"Microsoft Print to PDF"**
  printer to save the report as a PDF — no extra software required. The
  printout also includes the Debtor/Cash/Difference reconciliation at the
  top, so it doubles as your balance-check document.
- **Tab 4 – By Species**: Kgs/$ totals grouped by species across every
  supplier — useful for "how much Garfish did we buy this sheet, total?"
  style questions — plus the average, highest, and lowest price seen for
  each species, so you can spot at a glance if a price varied a lot across
  suppliers or deliveries.

**Outlier price warning**: if a price looks unusually high or low compared
to that species' other same-day entries, the row gets a warning marker and
a red tint automatically as soon as you commit it — no interrupting popup.
Double-click a flagged row to review it (confirm it's correct, or fix it
on the spot), or right-click it to clear the flag without reviewing.

**Finalize Day** (button on the Data Entry tab): once a day's Debtor +
Cash balances exactly against what's entered, click "Finalize Day" to lock
it in. You'll be asked which date it represents (your team doesn't always
balance strictly by calendar day — e.g. Monday and Tuesday together, dated
for the Tuesday), and confirming writes a permanent copy to a `history`
folder in the data folder and disables further edits (entry form,
Add/Edit/Delete/Duplicate, Debtor, Cash) until you click "Un-finalize Day"
to reopen it. Finalize is blocked outright if the day doesn't balance —
there's no way to lock in an unbalanced sheet.

**Tools > Manage Supplier / Species Names...** lists every distinct name
currently in use (Supplier or Species) with a count of entries for each.
Select one or more (e.g. "Spanner" and "dam spanner"), type the name you
want them merged/renamed to, and click Apply — every matching entry is
updated at once. Handy for cleaning up typos or inconsistent spelling before
they split your totals across two buckets. When Supplier mode is selected
and exactly one supplier is highlighted, an **Email** field appears so you
can set (or clear) that supplier's email address, saved in `names.txt`.

**Email All Suppliers...** (button on the Breakdown tab, or File menu) makes a
price email for each supplier on the current sheet. How it opens is set in
**Tools > Settings > Email**: as a Gmail draft (saved in your Gmail Drafts
folder, nothing is sent), as an Outlook draft with a price table (`.eml`,
classic Outlook), or in your default mail program via `mailto:` (plain text).
The app never sends anything itself, and no email password is stored.

First a dialog lists the suppliers and lets you type optional notes for each:
**NOT SOLD**, **UP** (arrived over the manifest) and **DOWN** (short of the
manifest). Type them as the supplier should read them. The headings are added
for you and left out when a box is empty. The dialog starts blank every time;
**Skip - no notes** goes straight on (and asks first if you had typed notes).
Suppliers with no sales are not listed. If you have already made the emails
for the sheet on screen, the app asks before making a second set, because
suppliers can take a second email to mean the prices changed.

`mailto:` and `.eml` emails open **one at a time** with a confirmation between
each (many mail clients do not handle several requests in quick succession);
Gmail drafts are all created together. Suppliers with a saved address get it
filled in; those without still get a draft with the To field blank and the
supplier's name added to the Subject. Subject, opening line, sign-off and a
multi-line signature can all be changed in Settings. An email reads:

```
Good afternoon,

Please find below the prices for today's sale (18 November 2026):
KG     SPECIES           PRICE
13.8   Species one       $17.00
5.5    Species two       $12.00

NOT SOLD:
(whatever you typed)

Kind regards,

Your name
Your company | phone
```

The greeting matches the time of day. In `mailto:` mode, if a supplier has an
unusually long list, the email falls back to a shortened summary (your notes
and signature are kept) instead of risking a link too long for some mail
clients; Gmail and `.eml` emails have no such limit.

**File > Export to CSV...** saves everything — the raw entries, per-supplier
totals, per-species totals, and the full breakdown — into one file that
opens directly in Excel.

Weights (Kg) are always displayed to 1 decimal place throughout the app,
emails, printouts, and CSV export; money is always displayed to 2 decimal
places (cents).

Data auto-saves to `autosave.fbd` next to the .exe after every change, so
nothing is lost between sessions. Use **File > Save As...** to save a named
file you can come back to later (e.g. one per month), and **File > Open...**
to load one again — **File > Recent Files** remembers your last 8. The title
bar always shows which file (if any) is currently open, or "(unsaved)" if
you're just working in the autosave. The window remembers its size,
position, and which file was open, and restores them next time you launch.

## Building

No third-party libraries are required — only the standard Windows SDK
headers/libs that ship with Visual Studio.

### Build (MSVC)

Open a **Developer Command Prompt for VS**, `cd` into this folder, then run:

```
build_msvc.bat
```

## Files

| File              | Purpose                                             |
|-------------------|------------------------------------------------------|
| `main.cpp`        | The Win32 application's main window, startup, entry-form actions, file operations and the Finalize Day popup (the rest has moved to `src/` - see below) |
| `src/core/FishBalanceCore.h` | Platform-independent domain logic extracted from `main.cpp` (parsing, aggregation, formatting) so it can be unit-tested without a Win32 dependency - see `tests/` |
| `src/data/FishBalanceWin32IO.h` | Win32 file I/O (atomic write, load) behind small ports so it can be fault-injection tested - see `tests/win32_integration/` |
| `src/data/AppData.*`, `src/forms/UiState.*`, `src/forms/ControlIds.h`, `src/PlatformWin.h` | Shared global state, control IDs and Windows include settings split out of `main.cpp` (v0.10.2) - see `src/README.md` |
| `src/data/Backups.*` | Writing a timestamped rolling-backup snapshot to `backups\` (v0.10.14) |
| `src/data/Storage.*` | Where the app's files live and how settings / recent files / supplier emails are read and written (v0.10.3) |
| `src/services/*` | CSV export, supplier-email building and report rendering, split out of `main.cpp` (v0.10.4-0.10.5) |
| `src/forms/SettingsWindow.*`, `src/forms/EmailNotesWindow.*` | The Settings window (email mode and wording, backups, display, printing) and the Email Suppliers notes dialog (v0.24.0) |
| `src/forms/ManageNamesWindow.*`, `src/forms/PrintPreviewWindow.*`, `src/forms/MainWindowApi.h` | The Manage Names and Print Preview popups, split out of `main.cpp` (v0.10.6) |
| `src/forms/DrillDownWindow.*` | The Breakdown drill-down popup: double-click a Breakdown row to list the entries behind it (v0.12.6) |
| `src/forms/MainWindowLayout.*` | The main window's layout, tab switching, glyph icons and control helpers (v0.10.9) |
| `src/forms/MainWindowLists.*` | The main window's list views, combo refresh/autocomplete and balance totals (v0.10.8) |
| `src/core/FishBalanceControlLimits.h` | Shared control/input limits |
| `winui/` | The WinUI 3 edition of the app (own solution `winui/FishBalanceWinUI.slnx`; see `winui/README.md`). It uses the SAME `src/` logic, `version.h` and data folder as the Win32 app. |
| `tests/`          | The automated test suites (doctest): portable unit tests for `FishBalanceCore.h`, plus `tests/win32_integration/` for real file I/O and an end-to-end smoke test - see "Note on testing" below |
| `app.manifest`    | Requests Common Controls v6 (needed for grouped list). Embedded in the exe by `app_icon.rc` (v0.18.6), so a copied exe keeps its styling. |
| `app.ico`         | The application icon (multi-resolution: 16–256px), embedded into the exe via `app_icon.rc` |
| `resource.h`       | Shared resource ID (`IDI_APPICON`) used by both `main.cpp` and `app_icon.rc` |
| `app_icon.rc`     | Minimal resource script that embeds only the icon — kept separate from the manifest so it can't cause the duplicate-resource conflict a combined script hit earlier |
| `build_msvc.bat`  | Quick build with MSVC (the one in regular use)       |
| `ROADMAP.md`, `ROADMAP_ARCHIVE.md` | The current plan; and the frozen history of the old roadmap (v0.10.18) |

**Note (v0.18.6):** the manifest (visual styles) and the icon are both embedded in the .exe, so it can be copied anywhere on its own. The icon is
embedded directly in the .exe, so it doesn't need `app.ico` alongside it —
that file is only a fallback the app checks if the embedded icon is ever
missing.

The app also creates a few small files/folders in `C:\BalancingAct\data` as you use
it (shared by the Win32 and WinUI apps): `autosave.fbd` (automatic backup of your current data), `settings.txt`
(window size/position, last file and settings), `recent.txt` (your Recent Files
list), `names.txt` (master Supplier / Species lists and supplier email addresses), a `backups\` folder
(rolling timestamped snapshots, taken automatically roughly every 3
minutes and on every Save — see File > Restore from Backup), and a
`history\` folder (one permanent file per day you've clicked "Finalize
Day" on, named by date). These aren't required to run the app and can be
deleted to reset that state, but don't delete `autosave.fbd` if you want
to keep unsaved work, `names.txt` holds your supplier addresses, and `history\` files are your permanent finalized
records, not disposable like the others.

## Note on testing

There IS an automated test suite (`tests/`, using the doctest framework) -
it covers `FishBalanceCore.h`, the platform-independent parsing/
aggregation/formatting logic the app is built on, and runs via
`run_tests.bat` / `tests/run_tests.ps1`. `run_tests.bat` runs BOTH the
portable suite and the Windows integration suite (real files, injected
save failures, headless startup) - run it after any change.

What it can't cover is the Win32-specific code in `main.cpp` itself
(window creation, message handling, printing, file dialogs) and the WinUI
screens - there is no Windows compiler where the code is written, so those
are built and checked on a real Windows machine. If a build fails, the exact
compiler error is enough to fix it. See `Testing.md` for the manual checklist
that covers that behaviour.

## Data file format

`.fbd` files are plain UTF-8 text, pipe-delimited, human-readable and
hand-editable if needed. **See [DATA_FORMATS.md](DATA_FORMATS.md) for the
full, authoritative format** (every field, backward-compatibility rules
for older files, and the `settings.txt`/`recent.txt`/`emails.txt`/CSV
formats too) — not duplicated here to avoid this section drifting out of
sync with the real format the way an earlier version of it did (it was
missing the `Flagged` field added in v0.9.19 and the `FINALIZED=` field
added in v0.9.40 for quite a while before being caught).
