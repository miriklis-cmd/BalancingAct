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

### 10. Reconciliation (`ParseSumExpr`) silently mis-parsed a malformed Debtor/Cash entry (v0.9.48)
**What**: the Debtor/Cash sum parser silently dropped any term that failed
to parse (`1000+oops+250` evaluated to `1250`, not an error) and, because
it used `std::stod` directly rather than the stricter `ParseDoubleW`, a
term with trailing junk like `12x` contributed its numeric prefix (`12`)
with the junk silently ignored. Either way, a typo in the one field this
app exists to reconcile could produce a plausible-looking but wrong total,
with no visible sign anything was off.
**Found by**: external audit (OpenAI Codex), confirmed against the actual
source - see `AuditFindings_2026-09-30.md` finding F5.
**Fix**: replaced with `ParseSumExprStrict()` (`FishBalanceCore.h`), which
rejects the whole expression (not just the bad term) for any term that
doesn't parse as a strict digits-and-one-decimal-point number (also
rejecting `nan`/`inf` and scientific notation), any operator immediately
following another operator, and any dangling trailing operator. The
Debtor/Cash line now shows "cannot check - invalid entry" and Finalize Day
is blocked, instead of computing a number from partially-ignored input.

### 11. Finalize Day silently ignored a failed named-file save (v0.9.48)
**What**: `FinalizeWndProc`'s `ID_FIN_OK` handler discarded the return
value of the `SaveToFile(g_currentFile)` call that syncs the open named
file's `FINALIZED=` marker after the permanent `history\<date>.fbd` record
is written. A failure (disk full, file locked, permissions) left the named
file silently out of sync with the just-finalized state, while the user
saw an unqualified "Finalized as <date>" success message either way.
**Found by**: external audit, confirmed against the actual source - see
`AuditFindings_2026-09-30.md` finding F9 (promoted into Phase 1 by Jack
given it directly affects the integrity of finalized business records).
**Fix**: the result is now checked; on failure the permanent history
record (already safely written) still stands, but the success dialog is
replaced with a warning naming the problem and instructing a manual Save,
and the document is left marked dirty so the mismatch isn't lost.

### 12. Stale `settings.txt` could cause a recovered autosave to silently overwrite the wrong named file (v0.9.48)
**What**: `settings.txt`'s `LASTFILE` is only written on a clean exit. At
startup, the app unconditionally restored `g_currentFile = LASTFILE`
whenever the autosave reload succeeded - but after a crash (or several
File > Open/New operations that ran without a clean exit in between),
`autosave.fbd`'s actual content could belong to a completely different
file than whatever `LASTFILE` still remembered. The next Save (explicit,
or the very next autosave tick) would then silently overwrite that
unrelated named file on disk with the mismatched recovered content.
**Found by**: external audit, confirmed against the actual source - see
`AuditFindings_2026-09-30.md` finding F1.
**Fix**: `autosave.fbd` now stamps a `SOURCE_FILE=` marker recording which
named file (if any) was open when it was written (later narrowed to
autosave.fbd only - named files, `history\` records, and `backups\`
snapshots don't carry it, since nothing ever reads their copy back and
embedding an absolute path/username in files that routinely leave the
machine had no benefit). Startup only restores the `LASTFILE` association
when the recovered autosave's own marker agrees with it; otherwise the
data is still recovered, but treated as unsaved (title bar shows
"(unsaved)"), requiring an explicit Save As before anything is written
back to a named file.

### 13. `.fbd` loading committed partial documents and tolerated structurally ambiguous files (v0.9.48)
**What**: a row that failed to parse (wrong field count, invalid/negative
Kgs or Price, empty Supplier/Species) was skipped and counted, but every
other row in the file was still committed - a single corrupted line could
cause a sheet to quietly load with data missing, which then fed directly
into reconciliation and every report. Separately, a second `BEGIN`, a
duplicate or unmatched `END`, or a duplicate `DEBTOR=`/`CASH=` line were
all silently tolerated (the last one seen simply won).
**Found by**: external audit, confirmed against the actual source - see
`AuditFindings_2026-09-30.md` findings F3 and F2 (combined into one
remediation workstream per Jack's authorization).
**Fix**: `ParseFbdContent()` now rejects the whole document for any of the
above - loading is transactional (all rows commit or none do) and
structurally strict (exactly one `BEGIN`...`END` pair, at most one
`DEBTOR=`/`CASH=` line each). Every legitimate historical row format
(4/6/7-field) is still accepted unchanged.

### 14. Unchecked, unbounded file reads and lossy UTF-8 decoding (v0.9.48)
**What**: `ReadAllLines` and `LoadFromFile` each duplicated an unchecked
`fseek`/`ftell`/`fread` sequence - no check of `fseek`'s or `fread`'s
return value, no `ferror()` check, and no upper bound on file size, so a
partially-failed read could silently produce truncated content
indistinguishable from a genuinely short file, and a huge or corrupted
file had no limit on how much memory reading it could consume. Separately,
`Utf8ToW()` decoded without `MB_ERR_INVALID_CHARS`, so invalid UTF-8 bytes
were silently replaced with U+FFFD rather than reported.
**Found by**: external audit, confirmed against the actual source - see
`AuditFindings_2026-09-30.md` finding F4. This also resolves the
previously-accepted "large `.fbd` files could cause a large allocation"
risk below, which is superseded by this fix.
**Fix**: both reading paths now go through one shared, checked
`ReadAllBytes()` helper (100MB size cap, every I/O call's result
checked), and `Utf8ToW()` reports invalid UTF-8 via an optional out-
parameter instead of silently substituting replacement characters.

### 15. Two gaps found on a post-delivery completeness review of items 12/13 (v0.9.49)
**What**: before Jack ran the real Windows build for v0.9.48, he asked for
a precise, source-cited completeness check of items 12 and 13 above
against their original scope. Two genuine, in-scope gaps turned up:
(a) `SOURCE_FILE=` (item 12) was being written into **every** `.fbd`
save - named files, `history\` records, and `backups\` snapshots
included - even though nothing ever reads any of those copies back
(only `autosave.fbd`'s own copy is ever consulted, at startup); every
file that routinely leaves the original machine was carrying an
absolute path with the Windows username in it, for no functional
benefit. (b) `ParseFbdContent()` (item 13) rejected a duplicate/unmatched
`BEGIN`/`END` or a bad row, but a non-empty line that wasn't a recognized
`KEY=` marker and sat outside the data section entirely (before the
first `BEGIN`, after `END`, or in a file with no `BEGIN` at all) matched
no branch and was silently ignored - "entry before `BEGIN`", "entry
after `END`", and other stray content weren't actually covered by item
13's transactional/structural-strictness claim.
**Found by**: Jack's own review request, confirmed against the actual
source - not a new external audit finding.
**Fix**: (a) `SOURCE_FILE=` is now written only into `autosave.fbd`
(`BuildFbdSaveContent()`'s new `includeSourceFile` parameter, set by
`SaveToFile()` comparing the target path against a new `AutosavePath()`
helper). (b) `ParseFbdContent()` now rejects the whole document for any
non-empty, unrecognized line outside the data section, the same way it
already rejects the other structural-ambiguity cases; a genuinely blank
line in that position is still harmless. See DATA_FORMATS.md and
CHANGELOG.md's `[0.9.49]` entry.

### 16. Phase 1 / F4 completeness: the authorization also required a hard
BEGIN/END structural rule, bounded record/line/field validation,
structured read/parse diagnostics, and allocation-failure handling -
none of which were actually done yet (v0.9.50)
**What**: after v0.9.49 shipped, Jack reviewed that release against F4's
original authorization and found it was not actually complete. v0.9.48/
v0.9.49 closed checked reads, the whole-file size limit, and strict UTF-8
decoding (item 14 above) - but F4 also explicitly required: (a) a
document-level rule that a legitimate `.fbd` file always has exactly one
`BEGIN` and one matching `END` (a Debtor/Cash-only document with neither
was, until this version, still accepted - a gap, not an intentional
historical-compatibility allowance); (b) bounded validation on entry-
record count, decoded line length, and individual field lengths
(Supplier/Species/Notes/Debtor/Cash expression/draft fields/
`SOURCE_FILE=`); (c) structured diagnostics distinguishing the specific
read-side and parse-side failure reasons (file-not-found vs. access
failure vs. too-large vs. short-read vs. invalid-UTF-8 vs. each distinct
structural/content problem), surfaced to the user everywhere a file is
loaded (File > Open, Recent Files, Restore from Backup, startup autosave
recovery) instead of one generic message; (d) explicit handling of
allocation failures at the read/decode/parse boundary so a pathological
input produces a clear error instead of an uncontrolled crash or a
partially-committed document. An earlier answer in this same review cycle
incorrectly described these as outside F4's authorized scope; that
characterization was wrong and has been corrected here and throughout
this file's history - they were part of the original authorization and
were simply not yet implemented.
**Found by**: Jack's own review of the Phase 1 authorization text against
what was actually delivered - not a new external audit finding.
**Fix**:
  - (a) `ParseFbdContent()` (`FishBalanceCore.h`) now rejects any document
    with no `BEGIN`, a `BEGIN` with no matching `END`, or (already true
    since v0.9.49) a duplicate/misordered marker - the only accepted empty
    sheet is `DEBTOR=` / `CASH=` / `BEGIN` / `END`. Nothing in this app's
    own save history ever wrote a file without both markers -
    `BuildFbdSaveContent()` has always written both unconditionally, even
    for a blank sheet - so no genuine historical file format is broken by
    tightening this.
  - (b) New documented limits in `FishBalanceCore.h` (`kMaxEntryRecords` =
    100,000; `kMaxLineLength` = 65,536; `kMaxSupplierSpeciesLength` = 255;
    `kMaxNotesLength` = 4,096; `kMaxDebtorCashExprLength` = 4,096;
    `kMaxDraftKgsPriceTextLength` = 256; `kMaxSourceFileLength` = 32,767),
    enforced identically on loaded `.fbd` content and on interactive entry
    (`EM_LIMITTEXT`/`CB_LIMITTEXT` on every corresponding control, so the
    UI can never produce a value the loader would then reject) and on the
    Supplier-name field shared with `emails.txt` (skips an over-limit line
    on load rather than rejecting the whole file, matching that file's
    existing forgiving-parse style). Over-limit input is rejected with a
    specific diagnostic, never silently truncated - four pre-existing
    fixed-size `GetWindowTextW` buffers (`BuildFbdSaveContent`,
    `RecalcTotals`, the print/PDF report builder, `CommitEntryForm`) were
    resized to match, since raising the UI limits without resizing them
    would have reintroduced silent truncation at the exact moment a value
    is read back out of the control.
  - (c) New `FbdErrorCode` enum (`FishBalanceCore.h`, content/parse-level)
    and `ReadBytesError` enum (`main.cpp`, file-I/O-level) between them
    distinguish every category F4 named. `LoadFromFile()` now takes an
    optional `outError` parameter combining both into one specific,
    human-readable reason (with a 1-based line number for parse failures),
    threaded through all four load call sites' message boxes.
  - (d) `std::bad_alloc`/`std::length_error` are now caught deliberately
    (never a broad `catch (...)`) around every allocation boundary named
    in the authorization: the raw byte-buffer allocation in
    `ReadAllBytes()`, UTF-8 decoding (`Utf8ToW`, called from both
    `ReadAllLines` and `LoadFromFile`), and line-splitting/parsing/entry-
    vector growth (all inside `ParseFbdContent()`, wrapped at its call site
    in `LoadFromFile`). Each boundary reports a specific "not enough
    memory" message and leaves live document state, the source file, and
    the autosave untouched - the same transactional guarantee as any other
    load failure. **Deliberately not automated**: reliably forcing
    `std::bad_alloc` in a portable, deterministic unit test (short of
    something like a custom fault-injecting allocator, which was judged
    disproportionate to this app's risk profile) isn't practical, so this
    path is verified by code review only - the boundaries above were
    checked by inspection for (i) narrow, specific `catch` clauses with no
    broad `catch (...)`, (ii) no partial mutation of `g_entries`/live state
    before the try block's result is fully committed, and (iii) no file
    write happening between the allocation attempt and the error return.
    What *is* automated instead, and should catch the realistic version of
    this risk long before an actual allocation failure: the new bounded
    size/record/line/field limits in (b), which stop a hostile or
    corrupted input from ever reaching an allocation large enough to fail
    in the first place.
**Tests**: `tests/test_fbd_loader.cpp` gained boundary tests (exactly-at
and one-over) for every limit in (b), regression tests for the missing-
`BEGIN`/Debtor-Cash-only rejection in (a), and `errorCode` assertions
confirming the specific `FbdErrorCode` fired in each case - all fixtures
built programmatically (loops constructing long strings/many rows) rather
than as large in-source literals.

## Deliberately accepted risk (not fixed, by design)

- **No exception/crash telemetry.** If the app crashes, there's no
  automatic reporting mechanism. Acceptable for a single-user offline
  tool with no server component to report to.

## Deferred to v2 (flagged for investigation, not permanently accepted)

- **Plaintext storage of `.fbd`/`emails.txt`/`settings.txt` — raised
  2026-09-05, staying as-is for v1, full design finalized for v2.**
  Anyone who obtains one of these files (stolen/lost device, a copied
  backup, an intercepted email) can read its full contents in Notepad —
  no encryption. Jack: "I don't like the idea of someone taking the
  `.fbd` file or `.txt` file and knowing what we're doing." Confirmed
  requirement that shaped the design: each computer runs its own copy of
  the app, with team members reviewing each other's work across
  machines - ruling out any single-machine-scoped scheme (plain DPAPI)
  in favor of a shared business-level key, cached locally per machine via
  DPAPI, with distribution gated by an AD security group. **Full design,
  including everything ruled out along the way and why (a custom
  backend/KMS, machine-bound keys synced via a backend, plain
  obfuscation) is in NETWORK_ARCHITECTURE.md** - not duplicated here to
  avoid this register drifting out of sync with the authoritative
  version. Sequenced as part of Bucket C in ROADMAP.md, after the
  current single-machine app reaches its "done" state.

## Process note

Every new feature that constructs a file path, reads external input
(including a user-selected file), or formats free text into a
machine-readable output (CSV, the `.fbd` format, `mailto:` URLs) should be
checked against the patterns above before being considered done. See
ARCHITECTURE.md for the technical detail behind each fix.
