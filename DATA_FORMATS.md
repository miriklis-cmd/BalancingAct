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

Plain UTF-8 text. Example:

```
DEBTOR=16853.15+340
CASH=250.7+1826+2552+286
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
  Entry tab. Stored as-is (including any `+`-separated sum expression),
  re-parsed on load.
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
- `BEGIN` / `END` — bracket the entry rows.
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
  - Rows that don't parse into exactly 4, 6, or 7 fields are skipped, and
    the user is warned with a count (see BUSINESS_RULES.md).

`autosave.fbd` (next to the exe) uses this same format and is
continuously overwritten on every data change. Named files created via
File > Save As use the identical format with a user-chosen filename.

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
- `LASTFILE` — path of the last-open named file, restored as the
  associated file (not re-loaded from disk — the content always comes
  from `autosave.fbd`, which is always at least as current).

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

## CSV export (`File > Export to CSV...`)

Not a persistence format (nothing reads it back in) — a one-way export for
opening in Excel. UTF-8 with a BOM (so Excel reads accented characters
correctly), one file containing four sections back to back: Entries,
Overview by Supplier, By Species, and Breakdown. Money is written as a
plain number (no `$`) so Excel can sum it directly; see BUSINESS_RULES.md.
Fields are also guarded against CSV formula injection (a leading
`= + - @` gets neutralized) since Supplier/Species/Notes are free text
that ends up in a file Excel will interpret.
