# Security Hardening Register

A consolidated record of every security or data-integrity issue found and
fixed in this project. Scattered across CHANGELOG.md by version, but
worth having in one place since this app handles a real small business's
financial data (supplier balances, pricing) even though it's a single-user
offline desktop tool.

Threat model: this is an offline desktop app with no network server
component. It has no stored credentials of any kind. The realistic attack
surface is (a) memory-safety bugs in the C++ code itself, and (b) a
maliciously or accidentally malformed file being opened by the user (a
`.fbd` file, or CSV output opened in Excel).

## Fixed issues

### 1. Buffer overflow risk in autocomplete (v0.7.0)
**What**: `CB_GETLBTEXT` (used by the Supplier/Species autocomplete) was
called with a fixed 256-character buffer without first checking how long
the actual string was. `CB_GETLBTEXT` has no length-limiting parameter —
it writes exactly as much as the string needs.
**Why it wasn't already broken**: every input path that fed the combo
box's item list also happened to cap input at 256 characters, so it never
actually overflowed — but that was luck, not a guarantee.
**Fix**: query the real length via `CB_GETLBTEXTLEN` first, then size the
buffer to fit.

### 2. Resource leak on every print job (v0.7.0)
**What**: `PrintDlgW` allocates two global memory blocks (`hDevMode`,
`hDevNames`) that the caller is documented to free with `GlobalFree`. They
were never freed.
**Impact**: minor leak accumulating across a session; not a security
issue per se, but incorrect resource handling worth fixing on principle.
**Fix**: free both handles on every exit path of the print function.

### 3. Silent data corruption via unescaped delimiter (v0.7.0)
**What**: the `.fbd` save format is pipe-delimited (`|`) with no
escaping. Typing `|` into Supplier, Species, or Notes would misalign or
silently drop that row the next time the file was loaded.
**Fix**: reject the `|` character at entry time with a clear explanation,
across every field that feeds the save format (Supplier, Species, Notes,
and the Manage Names rename/merge target field).
**Also added**: `LoadFromFile` now counts and reports any row it can't
parse, rather than silently loading fewer entries than the file actually
contains — protects against files that were corrupted before this fix
existed.

### 4. No delete confirmation (v0.7.0)
**What**: a single misclick on Delete permanently removed a row with no
recovery.
**Fix**: confirmation prompt showing exactly what's about to be deleted,
plus a single-level Undo Delete (Edit menu).

### 5. Use-after-free in By Species tab (v0.8.1)
**What**: a temporary `std::wstring`'s `.c_str()` pointer was passed
directly into `ListView_SetItemText`, which is a multi-statement macro —
the temporary was destroyed before the message that reads the string
actually ran, producing garbled or blank data.
**Classification**: this is a memory-safety bug (use-after-free), not
just a display bug — the exact garbage rendered was effectively reading
freed heap memory. Low severity in practice (no attacker-controlled input
involved, purely a local rendering glitch), but the same *pattern* could
be more serious elsewhere, which is why it's recorded here rather than
only in CHANGELOG.
**Fix**: bind every formatted string to a named variable before use; every
`ListView_SetItemText` call site in the codebase was audited for the same
pattern (see ARCHITECTURE.md for the full explanation and the safe
pattern to follow going forward).

### 6. CSV formula injection (hardened proactively, v0.6.x)
**What**: Excel treats a cell value starting with `=`, `+`, `-`, or `@` as
a formula. Since Supplier/Species/Notes are free text that ends up in the
CSV export, a value starting with one of those characters would be
interpreted as a formula when the exported file is opened in Excel — a
known real-world attack class ("CSV injection").
**Fix**: any exported field starting with one of those characters is
prefixed with a tab character, neutralizing it as a formula while keeping
it readable. Added proactively while building the export feature, not in
response to a reported incident.

### 7. Opening (almost) any file could silently destroy the autosave (v0.9.1)
**What**: `LoadFromFile()` treated nearly any readable file as a
successful load — it didn't require the `BEGIN`/`END` markers, didn't
require any parsed rows, and converted an invalid Kgs/Price value to `0`
instead of rejecting the row. Every caller (`DoFileOpen`, Recent Files,
and the startup autosave reload) immediately calls `RefreshAll()`, which
autosaves right after — so selecting the wrong file, or opening a `.fbd`
that was corrupted or cut off mid-write, could silently overwrite the
real recovery copy with an empty or garbled document while reporting
success the whole time. This directly contradicted the "malformed rows
are surfaced, not silently dropped" rule in BUSINESS_RULES.md, which was
only actually enforced for the wrong-pipe-count case, not for numeric
garbage or a structurally empty/incomplete document.
**Found by**: external audit (ChatGPT, read-only static review), confirmed
by tracing every call site by hand.
**Fix**: `LoadFromFile()` now parses into local temporaries and validates
the *whole document* before committing anything to `g_entries` or the
on-screen fields:
- At least one recognized `.fbd` marker (`DEBTOR=`/`CASH=`/`BEGIN`/`END`)
  must be present, or the file is rejected outright as "not a Fish
  Balance file."
- A `BEGIN` must be matched by an `END`, or the file is treated as a
  truncated/interrupted write and rejected.
- Kgs/Price on every row must parse as a finite, non-negative number
  (see #8 below) — a bad value now skips and counts the row, same as the
  existing wrong-pipe-count handling, instead of silently becoming `0`.
- A row with an empty Supplier or Species (only reachable via a
  hand-edited or corrupted file — the entry form already blocks this) is
  now rejected instead of silently creating a blank-named report group.

A file that fails any of these checks changes nothing: the in-memory
sheet and the on-disk autosave are both left untouched, and the user sees
a specific message instead of a silent "success."

The same risk existed a second time at startup, since the initial
`autosave.fbd` reload's return value was ignored before this fix.
`RefreshAll()` now takes an optional `doAutosave` flag (default `true`,
so all nine other call sites are unaffected); startup passes `false` when
the existing autosave fails to load, so a corrupted/interrupted autosave
is left alone (with a warning shown) instead of being immediately
overwritten with a blank sheet.

### 8. NaN/Infinity accepted as valid Kgs or Price (v0.9.1)
**What**: `ParseDoubleW()` checked that `std::stod` consumed the whole
string, but `std::stod` happily parses `"nan"`, `"inf"`, and `"-inf"` as
fully-consumed, "successful" values — and the existing `value < 0` guard
in `CommitEntryForm()` doesn't catch `NaN` either (`NaN < 0` is `false`
in IEEE 754). A non-finite value could poison totals, sorting, and the
`std::map<double, ...>` price-grouping used by `BuildBreakdownData()`,
then propagate into CSV export, printing, and emails.
**Found by**: external audit.
**Fix**: `ParseDoubleW()` now explicitly rejects non-finite values via
`std::isfinite()`. Since this function is shared by both the interactive
entry form and (as of the #7 fix) the file loader, fixing it once closes
the gap in both places at once.

### 9. Undo Delete could insert a row from a different, already-closed
   sheet into the current one (v0.9.1)
**What**: the single-level undo buffer (`g_hasUndo`/`g_undoEntry`/
`g_undoIndex`) and the enabled state of the "Edit > Undo Delete" menu
item were only ever set by `DeleteSelectedEntry()` and cleared by
`UndoDelete()` itself — never by `DoFileNew()`, `DoFileOpen()`, the
Recent Files handler, or a Manage Names rename/merge. Sequence: delete a
row in Sheet A (undo buffer now holds it, menu item enabled) → open Sheet
B → click the still-enabled Undo Delete → Sheet A's deleted row is
silently inserted into Sheet B's data and autosaved immediately, with no
warning that anything crossed sheets. The same mechanism meant that
deleting a row, then using Manage Names to fix a misspelled supplier,
then undoing, would silently reintroduce the old misspelling into the
freshly-renamed data.
**Found by**: internal follow-up audit (not flagged by the external
audit), confirmed by tracing every assignment to the three undo-state
variables.
**Fix**: added a `ClearUndoState()` helper (clears the buffer and grays
the menu item) and call it from `DoFileNew()`, `DoFileOpen()` (on
success), the Recent Files handler, and `ManageApply()` (when a
rename/merge actually changed anything) — anywhere the document is
replaced or restructured wholesale.

## Deliberately accepted risk (not fixed, by design)

- **Large `.fbd` files could cause a large allocation.** `LoadFromFile`
  reads the whole file into memory before parsing. A malicious or corrupt
  multi-gigabyte file could cause a large allocation or `bad_alloc`. Not
  fixed: the user already has full read/write access to their own
  filesystem, so this isn't a privilege-boundary issue, just a
  self-inflicted edge case (opening your own huge file). Not worth the
  complexity of a size cap at this app's scale.
- **No exception/crash telemetry.** If the app crashes, there's no
  automatic reporting mechanism. Acceptable for a single-user offline
  tool with no server component to report to.

## Process note

Every new feature that constructs a file path, reads external input
(including a user-selected file), or formats free text into a
machine-readable output (CSV, the `.fbd` format, `mailto:` URLs) should be
checked against the patterns above before being considered done. See
ARCHITECTURE.md for the technical detail behind each fix.
