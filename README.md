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
  flagging any mismatch in red.
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
folder next to the exe and disables further edits (entry form,
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
can set (or clear) that supplier's email address, saved to `emails.txt`.

**Email All Suppliers...** (button on the Breakdown tab, or File menu) opens
a pre-filled email for each supplier in the current sheet, in your default
email app (Outlook, etc.) via the `mailto:` protocol, ready for you to
review and send. They open **one at a time** — you'll be asked to confirm
before each one, so you can send or close the current draft before the next
appears. (Many mail clients, Outlook included, don't reliably handle several
`mailto:` requests fired in quick succession — going one at a time sidesteps
that entirely rather than relying on timing.) Nothing is sent automatically
and no email credentials are stored anywhere. Suppliers with a saved email
address get it filled in automatically; suppliers without one still get a
draft opened, just with the To field left blank for you to fill in — since
there's no address to tell those drafts apart by, the supplier's name is
added to the Subject line in that case. Each email reads:

```
Good afternoon,

Please see prices below
KG     Species           Price
13.8   Garfish           $17.00
5.5    Rock Flat         $12.00

Kind regards,
```

The greeting automatically matches the time of day (morning / afternoon /
evening) when you click Send All. If a supplier has an unusually long list
of species, the email falls back to a shortened summary instead of risking
a `mailto:` link that's too long for some mail clients to open.

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
headers/libs that ship with Visual Studio or MinGW-w64.

### Option A: CMake (recommended, works with either toolchain)

```
cmake -B build -G "Visual Studio 17 2022"      REM or -G "MinGW Makefiles"
cmake --build build --config Release
```

The resulting `FishBalanceManager.exe` will be under `build\Release\` (MSVC)
or `build\` (MinGW).

### Option B: MinGW-w64, one command

Install MinGW-w64 (e.g. via MSYS2) so `g++` and `windres` are on your PATH,
then from this folder run:

```
build_mingw.bat
```

### Option C: MSVC command line

Open a **Developer Command Prompt for VS**, `cd` into this folder, then run:

```
build_msvc.bat
```

## Files

| File              | Purpose                                             |
|-------------------|------------------------------------------------------|
| `main.cpp`        | The entire application                              |
| `app.manifest`    | Requests Common Controls v6 (needed for grouped list). Shipped as an external side-by-side manifest (`FishBalanceManager.exe.manifest`) rather than embedded via the resource compiler, to avoid RC/CVTRES toolchain issues. |
| `app.ico`         | The application icon (multi-resolution: 16–256px), embedded into the exe via `app_icon.rc` |
| `resource.h`       | Shared resource ID (`IDI_APPICON`) used by both `main.cpp` and `app_icon.rc` |
| `app_icon.rc`     | Minimal resource script that embeds only the icon — kept separate from the manifest so it can't cause the duplicate-resource conflict a combined script hit earlier |
| `CMakeLists.txt`  | Cross-toolchain build definition; copies the manifest and icon next to the exe automatically |
| `build_mingw.bat` | Quick build with MinGW-w64                           |
| `build_msvc.bat`  | Quick build with MSVC                                |

**Important:** `FishBalanceManager.exe` and `FishBalanceManager.exe.manifest`
must stay together in the same folder — if you copy the exe elsewhere, copy
the `.manifest` file alongside it, or the app will still run but the
Breakdown tab's grouped list may lose its modern styling. The icon itself is
embedded directly in the .exe, so it doesn't need `app.ico` alongside it —
that file is only a fallback the app checks if the embedded icon is ever
missing.

The app also creates a few small files/folders next to the exe as you use
it: `autosave.fbd` (automatic backup of your current data), `settings.txt`
(window size/position and last file), `recent.txt` (your Recent Files
list), `emails.txt` (supplier email addresses), a `backups\` folder
(rolling timestamped snapshots, taken automatically roughly every 3
minutes and on every Save — see File > Restore from Backup), and a
`history\` folder (one permanent file per day you've clicked "Finalize
Day" on, named by date). These aren't required to run the app and can be
deleted to reset that state, but don't delete `autosave.fbd` if you want
to keep unsaved work, and `history\` files are your permanent finalized
records, not disposable like the others.

## Note on testing

This was written directly against the Win32 API without access to a Windows
compiler to verify the build — no Windows toolchain is available in the
environment this was created in. The API usage follows standard, well-known
patterns, but if you hit a compile error when building it on your machine,
share the error message and it can be fixed directly.

## Data file format

`.fbd` files are plain UTF-8 text, pipe-delimited, human-readable and
hand-editable if needed. **See [DATA_FORMATS.md](DATA_FORMATS.md) for the
full, authoritative format** (every field, backward-compatibility rules
for older files, and the `settings.txt`/`recent.txt`/`emails.txt`/CSV
formats too) — not duplicated here to avoid this section drifting out of
sync with the real format the way an earlier version of it did (it was
missing the `Flagged` field added in v0.9.19 and the `FINALIZED=` field
added in v0.9.40 for quite a while before being caught).
