# Business Rules

Domain decisions that have been explicitly made during development. These
aren't obvious from the code alone, and some contradict what might seem
like a "more correct" default — they're recorded here so they don't get
accidentally reversed in future work.

## Business model — read this before reasoning about `Price`

**This is a seafood agency, not a buy-and-resell wholesaler.** Suppliers
send fish on **consignment** — the agent doesn't purchase it upfront.
Instead, the agent's job is to price and sell each supplier's fish on
the day to achieve the best possible result, then account back to the
supplier for what was achieved (less the agent's commission/fee).

This matters for how `Price` (and everything derived from it) should be
understood:
- **`Price` is the market price *achieved* when selling a supplier's
  consigned fish that day** — not a purchase cost paid to the supplier,
  and not a resale markup on top of one. There is only one price in this
  business model, not two (no "cost" vs. "sell price" distinction to
  reason about).
- **`Supplier`** is who consigned the fish (owns it, gets paid based on
  what it achieves), not a vendor being paid a purchase price.
- **Debtor/Cash reconciliation** (Book Reconciliation panel) is checking
  that money collected from *buyers* for everything sold on suppliers'
  behalf reconciles against the entered sales — not checking payments
  made *to* suppliers.
- **Reconciliation fails closed (Phase 1 / F5, 2026-09-30).** Debtor/Cash
  parsing (`ParseSumExprStrict`) now rejects the whole figure outright if
  any part of it can't be understood as a number/sum, rather than
  silently dropping the bad part and computing a Book Total from whatever
  was left. A typo in either field now shows "cannot check - invalid
  entry" instead of a plausible-looking balanced or unbalanced total, and
  Finalize Day is blocked the same way it already was for an unbalanced
  total. See `AuditFindings_2026-09-30.md` (finding F5) for the reasoning
  and `DATA_FORMATS.md` for the exact grammar.
- Any future price-related feature (price history, outlier warnings,
  cheapest-supplier highlighting) should be framed as **market-rate
  benchmarking to inform today's pricing decision** ("what have we
  achieved for this species recently, so I know what to aim for today"),
  not as cost-tracking or margin/markup calculation — this distinction
  was gotten wrong twice during the Price History roadmap discussion
  (2026-09-05) before being corrected here.

## Units and formatting

- **Weight (Kg) is always displayed to 1 decimal place**, everywhere in
  the app (entries list, all report tabs, print output, CSV export,
  emails). This is a stated business convention — the operator doesn't
  weigh to finer precision than 0.1kg.
- **Money is always displayed to 2 decimal places** (cents), everywhere,
  with no exceptions. This includes the CSV export, where money is written
  as a plain number without a `$` prefix (so Excel can sum it directly) —
  don't confuse this with the Kg convention above; they're formatted by
  two genuinely different functions (`FormatKg` vs `FormatNum`/
  `FormatMoney`) and must stay that way.

## Tax

- **No GST handling.** Fresh (unprocessed) fish is GST-free under
  Australian tax law, so there is no GST-inclusive/exclusive toggle and
  none is planned. If this app is ever adapted for processed/value-added
  seafood products, this assumption would need revisiting.

## Data integrity

- **Supplier names, Species names, and Notes cannot contain the `|`
  character.** The save file format is pipe-delimited with no escaping,
  so an unescaped `|` in free text would corrupt the row on save/reload.
  This is enforced at entry time (rejected with an explanation) rather
  than silently stripped or escaped.
- **Old save files remain loadable.** The `.fbd` format has grown from 4
  fields per entry (`Supplier|Species|Kgs|Price`) to 6
  (`Supplier|Species|Kgs|Price|Date|Notes`). The loader accepts both field
  counts; missing Date/Notes on older files are simply left blank. Any
  future format change should preserve this backward-compatibility
  pattern rather than requiring a one-way migration.
- **Loading is transactional, not partial (Phase 1 / F3+F2, 2026-09-30).**
  If any row in a `.fbd` file can't be parsed (e.g. an old file corrupted
  before the `|` restriction existed), or the document's structure is
  ambiguous (a second `BEGIN`, a duplicate/unmatched `END`, or a
  duplicate `DEBTOR=`/`CASH=` line), the **whole file** is rejected - it
  is never partially loaded with some rows silently missing. This
  replaced an earlier "skip the bad row(s) and warn with a count"
  behavior, which could leave a sheet quietly missing data that then fed
  into reconciliation and reports. See `AuditFindings_2026-09-30.md`
  (findings F3/F2) and `DATA_FORMATS.md`.
- **Every `.fbd` document always has exactly one `BEGIN` and one matching
  `END` (Phase 1 / F4, completed v0.9.50).** A `DEBTOR=`/`CASH=`-only
  document with neither marker, which older versions accepted, is now
  rejected the same as any other structurally-ambiguous file - no file
  this app has ever saved lacked both markers, so this closes a real gap
  rather than breaking a genuine historical format. The only valid empty
  sheet is `DEBTOR=` / `CASH=` / `BEGIN` / `END`.
- **Loading shows a specific reason, not one generic message (Phase 1 /
  F4, completed v0.9.50).** File > Open, Recent Files, Restore from
  Backup, and startup autosave recovery each now show why a file was
  rejected - file not found, locked/access denied, too large, not valid
  UTF-8, or the specific structural/content problem with a line number
  where applicable - rather than one fixed "could not open" sentence for
  every case. See `DATA_FORMATS.md`.
- **Record, line, and field lengths are bounded (Phase 1 / F4, completed
  v0.9.50).** Generous, documented limits (entry records, decoded line
  length, Supplier/Species/Notes/Debtor/Cash/draft fields/`SOURCE_FILE=`)
  are enforced identically on loaded files and on interactive entry, so a
  value the UI would reject can never arrive via a hand-edited file, and
  vice versa. Over-limit input is rejected with a specific message, never
  silently truncated. See `DATA_FORMATS.md` for the exact limits.
- **A memory-allocation failure while loading shows a clear error rather
  than crashing or loading a partial document (Phase 1 / F4, completed
  v0.9.50).** Reading, decoding, and parsing a `.fbd` file all guard
  against `std::bad_alloc`/`std::length_error` at their allocation
  boundaries; on failure, the in-memory document, the source file, and
  the autosave are all left exactly as they were. See
  `SecurityHardeningRegister.md` item 16.
- **A recovered autosave isn't blindly re-linked to a named file (Phase 1
  / F1, 2026-09-30).** `settings.txt`'s remembered last-opened file is
  only ever written on a clean exit, so after a crash it can be stale
  relative to what `autosave.fbd` actually contains. The app now checks
  the recovered autosave's own recorded identity (`SOURCE_FILE=`) before
  re-associating it with that named file; on a mismatch (or an old
  autosave with no identity recorded at all) the recovered data still
  loads, but as unsaved work requiring Save As, rather than risking a
  silent overwrite of the wrong file on the next save. See
  `DATA_FORMATS.md` for the full mechanism.
- **File reads are bounded and checked (Phase 1 / F4, 2026-09-30, completed
  v0.9.50).** Every whole-file read (`.fbd`, `settings.txt`, `recent.txt`,
  `emails.txt`) rejects files over 100MB and checks every I/O call's
  result rather than assuming success, distinguishing the specific reason
  (not found, locked/access denied, too large, short read, I/O error);
  decoding as UTF-8 rejects invalid byte sequences instead of silently
  substituting replacement characters. See `DATA_FORMATS.md`.

## Price statistics (By Species tab)

- **Average/Highest/Lowest price** on the By Species tab are computed over
  the individual price values entered per transaction (a simple mean, min,
  and max of the recorded `Entry.price` values for that species) — **not**
  a quantity-weighted average (i.e. not `total $ / total kg`). This
  matches a "what prices have we seen for this species" mental model
  rather than a "what did we effectively pay per kg overall" one. If a
  weighted average is ever wanted, it should be added as an additional
  column, not a replacement — both are legitimate but different metrics.
- These per-species price stats intentionally do **not** appear on the
  Total Overview tab (grouped by supplier) — average/highest/lowest price
  across a supplier's mixed species wouldn't be a meaningful figure.

## Email

- **No automatic/SMTP sending.** Emailing suppliers opens a pre-filled
  draft in the user's own default email app (via `mailto:`) for them to
  review and send manually. This was a deliberate choice to avoid storing
  email server credentials in the app, which would be a much larger
  security surface than this tool should take on.
- **Suppliers without a saved email address are still included**, with
  the "To" field left blank rather than being skipped entirely — the user
  can fill it in before sending. Since a blank-To draft has no address to
  visually distinguish it by, the supplier's name is appended to the
  Subject line in that case only.
- **Multiple suppliers are emailed one at a time**, with an explicit
  confirmation dialog between each — not all at once. See
  ARCHITECTURE.md for the technical reason (mail clients don't reliably
  handle rapid successive `mailto:` requests).

## File persistence philosophy

- The app **autosaves continuously** to `autosave.fbd` next to the exe —
  every data change is saved immediately, with no explicit "Save" step
  required to avoid losing work.
- **Named files** (via File > Save As / Open) are a separate, deliberate
  action for keeping permanent records (e.g. one file per week/month). The
  title bar always shows which named file, if any, is currently
  associated with the open data, so it's clear whether "Save" will write
  to a real file or just prompt for one.

## Finalize Day (ROADMAP.md item 3, added v0.9.40)

- **A day is a business decision, not a calendar date.** Jack's team
  doesn't necessarily balance one calendar day per file: Monday and
  Tuesday are often balanced together as one Tuesday-dated day (Monday
  isn't an "official" market day but does see sales), Saturday sales are
  often balanced on Monday with Saturday's own date, and on "special"
  weeks (Easter, Christmas) Monday can be an official sale balanced
  separately from Tuesday. The app deliberately does **not** try to
  detect a mismatch between today's date and the open file's date — that
  would be wrong more often than right against this real workflow.
  Instead, staff explicitly click **Finalize Day** once they've actually
  finished balancing, and choose the date it represents at that point.
- **Finalize is blocked outright — no override — unless Debtor+Cash
  exactly balances** against the entered total (the same balance check
  already shown on Tab 1's Book Reconciliation panel). There is no way to
  lock in a day that doesn't balance.
- **Locking disables every control that could change the numbers**
  (entry form, Add/Edit/Delete/Duplicate, Debtor, Cash) rather than
  intercepting each attempted change — chosen directly with Jack over his
  own initial idea (ask to un-finalize or discard on every attempted
  add/delete) for much lower implementation risk, and because it reuses
  the same "grey everything out" pattern already planned for Bucket C's
  multi-machine read-only mode.
- **Un-finalizing needs only a confirmation dialog**, no reason text —
  it's a normal, expected correction workflow (a mistake noticed after
  the fact), not something that needs justifying.
- **A finalized day is written to `history\<date>.fbd`** — a permanent
  record, separate from the rolling, pruned `backups\` safety-net
  snapshots (see DATA_FORMATS.md). This is also the single, predictable,
  enumerable convention the Bucket C SQLite ingestion plan
  (NETWORK_ARCHITECTURE.md) and the future Price History feature both
  need for "which files count as history."
- **The history write is what actually locks the day in; syncing the
  working file is a checked, honestly-reported second step (Phase 1 / F9,
  2026-09-30).** After the permanent `history\<date>.fbd` record is
  written and checked, the app also re-saves the currently-open named
  file (if any) so its own `FINALIZED=` marker matches (named files never
  carry `SOURCE_FILE=` - see DATA_FORMATS.md).
  That second save's result is now checked: if it fails (disk full, file
  locked, permissions), the day is still finalized (the permanent record
  is safe), but the user is told plainly that the open file didn't
  re-save and needs a manual Save before closing, rather than being shown
  an unqualified "Finalized" success message while the open file quietly
  fell out of sync. See `AuditFindings_2026-09-30.md` (finding F9).
