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
