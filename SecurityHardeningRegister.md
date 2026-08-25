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
