# Data Formats

There's no database — everything is small, human-readable text files next
to the exe. This documents the in-memory schema and every file format.

## In-memory schema: `Entry`

The single record type the whole app is built around (defined in
`main.cpp`):

| Field      | Type          | Notes                                              |
|------------|---------------|-----------------------------------------------------|
| `supplier` | `std::wstring`| Free text; cannot contain `\|`                      |
| `product`  | `std::wstring`| The species; cannot contain `\|`                    |
| `kgs`      | `double`      | Displayed to 1 decimal place everywhere            |
| `price`    | `double`      | Price per kg; displayed to 2 decimal places        |
| `date`     | `std::wstring`| ISO `YYYY-MM-DD`; empty if not set (older files)   |
| `notes`    | `std::wstring`| Optional free text; cannot contain `\|`             |
| `priceFlagged` | `bool`    | Set when the price was flagged as a same-day outlier at commit time and saved anyway (ROADMAP.md item 7); cleared automatically next time the entry is committed with a price that no longer looks unusual |

`Total()` is computed on demand (`kgs * price`), never stored.

`g_entries: std::vector<Entry>` is the single in-memory source of truth
for the current sheet.

## `.fbd` — the main save file format

Plain UTF-8 text. Example (a named file - see the `SOURCE_FILE=` bullet
below for why that line is absent here and only ever appears in
`autosave.fbd`):

```
DEBTOR=16853.15+340
CASH=250.7+1826+2552+286
FINALIZED=2026-08-05
DRAFT_SUPPLIER=Jcasement
DRAFT_SPECIES=Garfish
DRAFT_KGS=13.8
DRAFT_PRICE=17
DRAFT_NOTES=
DRAFT_DATE=2026-08-05
BEGIN
Jcasement|Garfish|13.8000|17.0000|2026-08-05|
Jcasement|Rock Flat|5.5000|12.0000|2026-08-05|Extra fresh
END
```

- `DEBTOR=` / `CASH=` — the raw text typed into those fields on the Data
  Entry tab. Stored as-is (including any `+`/`-`-separated sum
  expression), re-parsed on load. **Phase 1 / F5 (2026-09-30)**: parsing
  is now strict (`ParseSumExprStrict` in `FishBalanceCore.h`) - a leading
  sign is allowed only on the first term, an operator can't immediately
  follow another operator, a trailing operator is rejected, and every
  term must be a plain digits-and-at-most-one-decimal-point number
  (rejects typos like `12x`, `nan`/`inf`, and scientific notation like
  `1e10`). An expression that fails this is treated as unusable rather
  than silently dropping the bad term and computing a total from
  whatever was left - see BUSINESS_RULES.md.
- `SOURCE_FILE=` — added **Phase 1 / F1 (2026-09-30, narrowed on later
  review)**. The full path of whichever named file was open when this
  save was written (blank if no named file was open - an unsaved sheet).
  **Written into `autosave.fbd` only** - never into a named file's own
  save, a `history\` record, or a `backups\` snapshot, since none of
  those are ever read back for this marker (only `autosave.fbd`'s own
  copy is consulted, and only at startup). The first version of this fix
  wrote it into every `.fbd` save; that was narrowed once it was pointed
  out that an absolute path (including the Windows username in it) was
  then being embedded, for no functional benefit, into every file that
  routinely leaves the original machine - emailed to an accountant,
  backed up to cloud storage, copied to another computer. Used only at
  startup: before silently re-associating a recovered `autosave.fbd`
  with `settings.txt`'s `LASTFILE`, the app checks that this line
  actually agrees with `LASTFILE` - see `settings.txt`'s section below
  for why. Not present in any file written before this version, or in
  any named/history/backup file ever; absence is treated as "unknown
  identity", not as "no named file", and handled accordingly.
- `FINALIZED=` — added v0.9.40 (Finalize Day, ROADMAP.md item 3). Present
  only once a day has been locked in via the Finalize Day button; holds
  the ISO date it was finalized as. Absent on an ordinary, still-editable
  working file. Validated as a real ISO date on load exactly like
  `DRAFT_DATE=` — an invalid/hand-edited value is silently dropped rather
  than treated as a valid lock. Loading a file with this line disables the
  entry form, Add/Edit/Delete/Duplicate, and Debtor/Cash (see
  BUSINESS_RULES.md) until Un-finalize Day clears it again.
- `DRAFT_SUPPLIER=` / `DRAFT_SPECIES=` / `DRAFT_KGS=` / `DRAFT_PRICE=` /
  `DRAFT_NOTES=` / `DRAFT_DATE=` — whatever's currently typed into the
  "Add Entry" form (Supplier/Species/Kgs/Price/Notes/Date), *before*
  clicking Add Entry. Written on every save (autosave included) so an
  in-progress row survives a crash or power loss, not just a graceful
  close — added in v0.9.14. All optional; a file with none of these
  lines (any file saved before v0.9.14, or a save with a genuinely empty
  form) loads with an empty draft, restoring nothing into the form.
  `DRAFT_DATE=` is validated as a real ISO date on load — an invalid
  value is silently dropped rather than passed through.
- `BEGIN` / `END` — bracket the entry rows. **Phase 1 / F3+F4 (2026-09-30,
  extended twice on later review)**: loading is now strict about document
  structure - a second `BEGIN`, a duplicate or unmatched `END`, or a
  duplicate `DEBTOR=`/`CASH=` line all reject the whole document rather
  than letting the last one seen silently win. **A legitimate `.fbd`
  document now ALWAYS has exactly one `BEGIN` and one matching `END`,
  with no exception** (v0.9.50) - a `DEBTOR=`/`CASH=`-only document with
  neither marker present, which older versions of this app accepted, is
  now rejected. This isn't a historical-compatibility break: every save
  path (`BuildFbdSaveContent()`) has always written both markers
  unconditionally, even for a completely blank sheet, so no file this
  app itself ever produced is affected. The only supported empty sheet is
  `DEBTOR=` / `CASH=` / `BEGIN` / `END`. A non-empty line that isn't a
  recognized `KEY=` marker, isn't `BEGIN`/`END`, and sits outside the data
  section (before the first `BEGIN`, after `END`, or in a file with no
  `BEGIN` at all) also rejects the whole document - "entry before
  `BEGIN`", "entry after `END`", and other unrecognized stray content
  used to simply be ignored until v0.9.49 closed that gap; a genuinely
  blank line in that position is still harmless.
- Each entry row is pipe-delimited:
  `Supplier|Species|Kgs|Price|Date|Notes|Flagged`.
  - Kgs/Price are written with 4 decimal places of precision internally
    (display rounding to 1dp/2dp happens only when rendering, never on
    the stored value).
  - `Flagged` is `1` if `priceFlagged` is true, `0` otherwise - added
    v0.9.19 (ROADMAP.md item 7).
  - **Backward compatibility**: rows with only 4 fields (no Date/Notes/
    Flagged) are accepted — this is the pre-v0.9.0 format. Rows with 6
    fields (Date/Notes but no Flagged) are accepted — this is the
    pre-v0.9.19 format. Both default the missing field(s) to empty/false.
  - **Phase 1 / F2 (2026-09-30)**: a row that doesn't parse into exactly
    4, 6, or 7 fields, has an invalid/negative Kgs or Price, or has an
    empty Supplier or Species, now rejects the **whole document** -
    loading is transactional, not partial. Before this change such a row
    was silently skipped (with a warning showing a count) while every
    other row still loaded; that meant a single corrupted line could
    cause a sheet to quietly load with data missing. See
    BUSINESS_RULES.md and `AuditFindings_2026-09-30.md` (F3/F2).

`autosave.fbd` (next to the exe) uses this same format and is
continuously overwritten on every data change. Named files created via
File > Save As use the identical format with a user-chosen filename.

## `history\` — Finalize Day snapshots

Added v0.9.40 (ROADMAP.md item 3). A folder next to the exe, created on
first use, holding one permanent `.fbd` file per finalized business day,
named by that day's ISO date: `history\2026-09-20.fbd`. Same `.fbd` format
as above (including the `FINALIZED=` line), written once at the moment of
finalizing and not touched again unless the day is re-finalized (with a
confirm-overwrite prompt). Separate from the rolling, throttled/pruned
`backups\` folder (ROADMAP.md item 4) that already exists next to the exe
for crash/mistake recovery — `history\` files are deliberate, permanent
records of a locked-in day, never pruned. **Phase 1 / F9 (2026-09-30)**:
this permanent history write is the one that actually locks the day in,
and it's checked; a separate, second save that keeps the *named* working
file's own `FINALIZED=` marker in sync with it (named files never carry
`SOURCE_FILE=` - see that bullet above) can fail independently (disk
full, file locked, permissions) without undoing the finalize - see
BUSINESS_RULES.md for how that's now reported.

## `settings.txt` — window/session state

Plain UTF-8, `KEY=value` per line:

```
X=120
Y=80
W=1400
H=900
MAX=0
LASTFILE=C:\Users\jack\Documents\august-week1.fbd
```

- `X`/`Y`/`W`/`H` — window position and size (physical pixels).
- `MAX` — `1` if the window was maximized, `0` otherwise.
- `LASTFILE` — path of the last-open named file. Only ever written on a
  clean exit (`SaveSettings`, called from `WM_DESTROY`), so after a crash
  or a forced close it still holds whatever it was at the *previous*
  clean exit, while `autosave.fbd` has gone on being rewritten by every
  edit since. **Phase 1 / F1 (2026-09-30)**: because of that gap, this
  value is no longer trusted on its own at startup - the recovered
  `autosave.fbd`'s own `SOURCE_FILE=` line must agree with it before the
  app re-associates the recovered content with this file. When they
  disagree (or the autosave predates `SOURCE_FILE=` entirely), the
  recovered content is still loaded but treated as unsaved - the title
  bar shows "(unsaved)" and a message explains that Save As is needed to
  keep it, rather than the app silently linking mismatched recovered
  content to this path and risking an overwrite on the next save.

## `recent.txt` — Recent Files list

Plain UTF-8, one file path per line, most-recent-first, capped at 8
entries (`kMaxRecentFiles`).

## `emails.txt` — supplier email addresses

Plain UTF-8, pipe-delimited, one supplier per line:

```
Jcasement|jcasement@example.com
Wdowns|wdowns.fish@example.com
```

Managed via Tools > Manage Supplier / Species Names (select a single
supplier to see/edit its email). A supplier with no line in this file has
no saved address — "Email All Suppliers" still creates a draft for them,
just with a blank "To" field.

**Phase 1 / F4 completeness follow-up (v0.9.50)**: a line whose Supplier
name or email address exceeds `kMaxSupplierSpeciesLength` (255 characters
— the same bound applied to the Supplier field everywhere else) is
skipped on load, the same way a line with no `|` or an empty field already
was — `emails.txt` has never had `.fbd`'s whole-file-reject semantics, so
one bad line is dropped rather than failing the entire load. The Manage
Names dialog's own Supplier-name and email-address fields
(`g_hManageTarget`/`g_hManageEmailEdit`) now also enforce this limit via
`EM_LIMITTEXT`, so the UI can't produce a value this load-time guard would
then have to drop.

## CSV export (`File > Export to CSV...`)

Not a persistence format (nothing reads it back in) — a one-way export for
opening in Excel. UTF-8 with a BOM (so Excel reads accented characters
correctly), one file containing four sections back to back: Entries,
Overview by Supplier, By Species, and Breakdown. Money is written as a
plain number (no `$`) so Excel can sum it directly; see BUSINESS_RULES.md.
Fields are also guarded against CSV formula injection (a leading
`= + - @` gets neutralized) since Supplier/Species/Notes are free text
that ends up in a file Excel will interpret.

## File reading limits (Phase 1 / F4, 2026-09-30, completed v0.9.50)

Every whole-file read in the app (`.fbd` files, `settings.txt`,
`recent.txt`, `emails.txt`) goes through one shared, bounded, checked
reader (`ReadAllBytes` in `main.cpp`): it rejects a file larger than
100MB outright, and checks every `fseek`/`fread` call's result instead of
assuming success. Decoding those bytes as UTF-8 (`Utf8ToW`) also now
rejects a file that isn't valid UTF-8, rather than silently substituting
replacement characters for invalid bytes. Either failure is treated the
same as a missing file by every caller.

`ReadAllBytes` returns a specific `ReadBytesError` (`main.cpp`) rather
than a bare success/fail bool - file not found, could not open (locked/
access denied), seek failure, size-query failure, too large, allocation
failure, short read, or a generic read/I/O failure are all distinguished.
`LoadFromFile()` combines this with `.fbd` content-level diagnostics (see
`FbdErrorCode` just below) into one specific, human-readable reason shown
to the user at every one of its four call sites: File > Open, Recent
Files, Restore from Backup, and startup autosave recovery. A `.fbd` load
failure never touches live document state, the source file, or the
autosave - the same transactional guarantee this format has always had
for content-level rejections.

A `std::bad_alloc`/`std::length_error` at the byte-buffer allocation,
UTF-8 decoding, line-splitting, or parsing/entry-vector-growth stage is
caught deliberately (never a broad `catch (...)`) and reported as a plain
"not enough memory" message instead of crashing or continuing with a
partial document - see `SecurityHardeningRegister.md` item 16 for exactly
which boundaries this covers and how it was verified.

## Bounded record/line/field validation (Phase 1 / F4, v0.9.50)

Generous, documented limits - far above any realistic business usage -
exist purely to stop a pathological or corrupted file from causing an
unbounded allocation or a multi-million-row report/print/CSV pass. All
are defined in `FishBalanceCore.h` and enforced identically on loaded
`.fbd` content and on interactive entry (the matching Win32 control gets
an `EM_LIMITTEXT`/`CB_LIMITTEXT` call in `main.cpp`, so a value the UI
would reject can never be smuggled in via a hand-edited file, and vice
versa):

| Field | Limit |
|---|---|
| Whole file size | 100 MiB (`kMaxReadableFileBytes`, unchanged from F4's original fix) |
| Entry records | 100,000 (`kMaxEntryRecords`) |
| Decoded line length | 65,536 characters (`kMaxLineLength`) |
| Supplier / Species (rows and drafts) | 255 characters each (`kMaxSupplierSpeciesLength`) |
| Notes (rows and drafts) | 4,096 characters (`kMaxNotesLength`) |
| `DEBTOR=` / `CASH=` expression | 4,096 characters each (`kMaxDebtorCashExprLength`) |
| Draft Kgs / Price text | 256 characters each (`kMaxDraftKgsPriceTextLength`) |
| `SOURCE_FILE=` | 32,767 characters (`kMaxSourceFileLength`) |

Over-limit input is rejected with a specific `FieldTooLong`/`LineTooLong`/
`RecordLimitExceeded` diagnostic (see `FbdErrorCode` below) - it is never
silently truncated. The Supplier-name field is also bounded in
`emails.txt` (see that section below), the one companion file that shares
a field type with `.fbd` content.

## Structured parse diagnostics (`FbdErrorCode`, v0.9.50)

`ParseFbdContent()`'s result (`FbdLoadResult` in `FishBalanceCore.h`) now
carries a specific `FbdErrorCode`, a 1-based `errorLine` (0 when not
applicable), and a short `errorMessage` whenever `ok` is false, instead of
just a bare `false`. Distinguishes: not a recognized Fish Balance file,
missing `BEGIN`, missing `END`, a duplicate/misordered marker, duplicate
singleton metadata (`DEBTOR=`/`CASH=` seen twice), unexpected content
outside the data section, a line or field over its length limit, the
entry-record limit exceeded, a malformed entry row (wrong field count), an
invalid numeric field, and an empty required field. The message never
echoes a full field's content verbatim, so a long or unusual Supplier/
Notes/Debtor value can't end up rendered straight into a dialog box.
