# Business Rules

Domain decisions that have been explicitly made during development. These
aren't obvious from the code alone, and some contradict what might seem
like a "more correct" default — they're recorded here so they don't get
accidentally reversed in future work.

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
