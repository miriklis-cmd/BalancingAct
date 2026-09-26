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
- **Malformed rows are surfaced, not silently dropped.** If a line in a
  `.fbd` file can't be parsed (e.g. an old file corrupted before the `|`
  restriction existed), the row is skipped and the user is shown a count
  of how many rows were unreadable, rather than the file silently loading
  with fewer entries than it should.

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
