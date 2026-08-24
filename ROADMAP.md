# Roadmap

This file is the single source of truth for "what's built, what's being
tested, and what's next." Check here first if you've lost track of where
things stand — that's exactly what this file is for.

## Status: v0.9.0 — awaiting test feedback

The current build adds Date field, Notes field, and Duplicate Last Entry
(see CHANGELOG.md for details). **Not yet confirmed working** — testing in
progress. Specific things worth checking:
- Date picker opens/works, defaults to today, sticks after adding an entry
- Notes field saves and shows up in the list and after a save/reload
- An old `.fbd` file (saved before this version) still opens correctly
- Duplicate Last Entry fills the form correctly, focus lands on Kgs
- Sorting by the Date column behaves sensibly
- CSV export includes Date and Notes

## Done (stable, shipped)

- Data entry: Supplier/Species autocomplete, Tab/Enter batch entry,
  Date + Notes fields, Duplicate Last Entry
- Edit existing entries in place; delete with confirmation + single-level
  undo
- Save/Open/autosave (`.fbd` format), Recent Files, window/session
  persistence
- Four report views: Data Entry (raw list, sortable/filterable), Total
  Overview (per-supplier), Breakdown (Supplier > Species > Price), By
  Species (with Avg/Highest/Lowest price)
- Print Preview + Print + PDF export (via "Microsoft Print to PDF")
- CSV export (everything, one file)
- Email All Suppliers (mailto drafts, one-at-a-time flow, works with or
  without a saved address)
- Manage Supplier/Species Names (merge/rename typos, set email addresses)
- Custom app icon, DPI-aware layout, security/data-integrity hardening

## Next up (in order)

1. **Timestamped backups** — right now autosave overwrites a single file;
   add a rolling history of backups you can recover from if something gets
   overwritten by mistake.
2. **Price history/trend per species over time** — now unblocked by the
   Date field. Likely a new tab or view showing how a species' price has
   moved across saved sheets/dates.
3. **Highlight cheapest supplier per species** — surfaced on the
   Breakdown or By Species tab.
4. **Warn if a price looks like a typo/outlier** — compare an entered
   price against recent history for that species before accepting it.
5. **Print/PDF for Overview & By Species tabs** — currently Print Preview
   and printing only cover the Breakdown tab.
6. **Filter/search on the summary tabs** — currently only the raw Entries
   list has a filter box.

## Explicitly decided against (for now)

- **GST toggle** — not needed; fresh fish is GST-free in Australia.
- **Automatic/SMTP email sending** — deliberately avoided; would require
  storing email credentials in the app, which is a bigger security surface
  than this tool should take on. The mailto-draft approach (open in your
  own email app, review, send yourself) was chosen instead.
- **Full keyboard shortcut set** (Ctrl+S, Ctrl+P, etc.) — considered
  early on and declined in favor of other priorities; could revisit later
  if wanted.

## Ideas not yet scoped / prioritized

Raised at various points but not committed to the roadmap above:
- Multi-sheet comparison (e.g. this week vs last week side-by-side)
- Notes/remarks at the sheet level (vs. per-entry, which already exists)
- A "Today" dashboard summary tab
- File association so double-clicking a `.fbd` file opens the app
- An installer (e.g. Inno Setup) if this is ever shared beyond one machine
