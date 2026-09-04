# Architecture

Read this before making non-trivial changes to `main.cpp`. It exists
specifically so a fresh working session (human or AI) doesn't have to
rediscover the same mistakes that have already been made and fixed once.

## Big picture

Single-file Win32 C++ application, no external UI framework (no MFC, no
WTL, no Qt) — plain `CreateWindowExW` + a manual `WndProc` message loop.
No compiler is available in the environment this was originally built in,
so every change has to be checked by careful reading and structural
verification (brace/paren balance, forward-declaration order, function
uniqueness) rather than by compiling. Treat every edit with that level of
care; there's no compiler safety net catching mistakes before they ship.

## Windows in the app

There are three top-level windows, each with its own `WndProc`:

1. **Main window** (`WndProc`) — the tabbed interface (Data Entry, Total
   Overview, Breakdown, By Species). Controls for all four tabs are
   created once at startup and shown/hidden via `ShowTab()` rather than
   being created/destroyed per tab switch.
2. **Manage Names popup** (`ManageWndProc`) — a plain owned window (not a
   real Win32 dialog resource), disables the main window while open.
3. **Print Preview popup** (`PreviewWndProc`) — same pattern as Manage
   Names.

Both popups follow the same lifecycle: `EnableWindow(mainWnd, FALSE)` on
creation (so the popup behaves modally without actually being a true
Win32 modal dialog), `EnableWindow(mainWnd, TRUE)` on the popup's
`WM_DESTROY`.

## When a new feature should be a tab vs. a menu item (2026-09-05)

This came up concretely during the Price History feature discussion and
is worth recording as a general principle, not just a one-off decision.

**Every tab in this app answers the same underlying question**: "is
today's sale balanced correctly?" (See BUSINESS_RULES.md's "Business
model" section — the app's primary purpose is balancing today's sale:
making sure no product was lost, no price was recorded wrong, no fish
was attributed to the wrong supplier.) Data Entry, Total Overview,
Breakdown, and By Species are all different lenses on that one job, used
every session.

**A feature belongs in the tab row only if it's part of that same daily
job.** If a feature answers a *different* question — used occasionally,
not every session, and not about verifying today's balance — it belongs
behind a menu item opening a separate popup window instead, the same
pattern already established by Manage Names and Print Preview (see
"Windows in the app" above). Concrete test case: a tool for pricing an
unfamiliar species you haven't handled in a year is genuinely useful, but
it isn't part of "did today balance correctly" - it's an occasional,
separate lookup - so it belongs as a menu item + popup window, not a
fifth tab. A feature about *today's* result specifically (e.g. "did we
price better today than yesterday/last week/last month") is a closer
call, since it's still fundamentally about today - decide case by case
rather than assuming every price-related feature is automatically
"secondary tooling."
open, `EnableWindow(mainWnd, TRUE)` in their own `WM_DESTROY`. If you add
another popup, follow this pattern exactly — don't leave the main window
disabled if the popup closes via an unusual path.

## Known gotchas (already caused real bugs — don't repeat them)

### `ListView_SetItemText` is a macro, not a function
It expands into several separate statements, not one expression. If you
pass a temporary's `.c_str()` directly (e.g.
`ListView_SetItemText(lv, i, col, SomeFunc().c_str())`), the temporary
`std::wstring` from `SomeFunc()` is destroyed at the end of the macro's
internal assignment statement — **before** the message that actually reads
the string runs. This is a use-after-free that produces garbled or blank
list contents, and it's exactly what happened in v0.8.1 (By Species tab).

**Always** bind the formatted string to a named variable first:
```cpp
// WRONG - dangling pointer, may render garbage or nothing
ListView_SetItemText(lv, i, 1, const_cast<LPWSTR>(FormatKg(x).c_str()));

// RIGHT - named variable outlives the macro's statements
std::wstring s = FormatKg(x);
ListView_SetItemText(lv, i, 1, const_cast<LPWSTR>(s.c_str()));
```
This does NOT apply to genuine function calls like `TextOutW` or
`SetWindowTextW` — those are real single-statement function calls, so a
temporary's lifetime correctly extends through the whole call. It's
specifically the multi-statement macros (`ListView_SetItemText` is the
one used here) that are dangerous.

### DPI scaling
The manifest declares the app DPI-aware, which means Windows will **not**
auto-stretch the UI on higher-DPI displays. Every layout dimension must be
wrapped in `S(px)` (defined near the top of `main.cpp`), which scales by
the actual screen DPI queried at startup. A raw pixel literal in
`LayoutAll()` or `ManageWndProc`/`PreviewWndProc`'s `WM_SIZE` handler will
look fine at 100% scaling and clip text at 125%/150%/etc. This has already
caused a real bug (v0.8.0) — check any new layout code uses `S()`
consistently.

### `IsDialogMessage` and the default button
The main window relies on `IsDialogMessage()` in the message loop for
Tab/Enter keyboard navigation. For this to reliably click a "default"
button on Enter, the window must answer `DM_GETDEFID` itself (handled as a
case in `WndProc`) — a plain `CreateWindowExW` window doesn't get this for
free the way a real dialog resource would. There's also a belt-and-braces
explicit Enter-key interception in the message loop for the entry-form
fields specifically, so batch entry doesn't depend on that mechanism alone.

### Printer output must use a DIB, not a screen-compatible bitmap
`RenderReportPages()` renders each page into a `CreateDIBSection` bitmap
and sends it to the printer via `StretchDIBits`. Don't switch this to
`CreateCompatibleBitmap` + `BitBlt` to a printer DC — some printer drivers
silently fail or produce garbage output when blitting a screen-compatible
bitmap directly. DIBs + `StretchDIBits` are reliable across drivers and
this same rendering function is reused for both Print Preview (on-screen)
and actual printing — they can never visually diverge because they're
built from identical draw calls.

### `mailto:` and multiple suppliers
Don't fire several `ShellExecuteW("open", "mailto:...")` calls in a tight
loop, even with a delay between them — mail clients (Outlook especially)
don't reliably handle a second externally-triggered "compose" request
while the first is still open. `DoEmailSuppliers()` opens one draft at a
time and waits for explicit user confirmation before opening the next.
Also always check `ShellExecuteW`'s return value (`> 32` means success) —
it fails silently otherwise, which looked exactly like "nothing happens"
in an earlier bug.

### Win32 forward declarations
Functions are used before they're textually defined in a few places
(e.g. `CancelEdit` is called by `DeleteSelectedEntry`, which appears
earlier in the file). Forward declarations live in one block near the top
of `main.cpp`. If you add a function that's called from code above its
definition, add it there rather than reordering large chunks of the file.

## Data flow

- `g_entries: std::vector<Entry>` is the single source of truth for the
  current sheet's data. Every tab/report is derived from it on demand via
  `RefreshAll()` → `RefreshEntriesList()`, `RefreshOverviewList()`,
  `RefreshBySpeciesList()`, `RefreshBreakdownList()`.
- `RefreshAll()` also silently autosaves to `autosave.fbd` — this runs on
  every single data change, so it should stay cheap. Don't add expensive
  work to the refresh path without considering this.
- `BuildBreakdownData()` groups `g_entries` into
  `Supplier → Species → Price` and is reused by the Breakdown tab, the
  printed report, and the CSV export's Breakdown section — keep it as the
  one place that grouping logic lives.

## Build

No compiler is available in the development environment this project was
built in. All correctness checking is done by:
1. Reading the diff carefully against the specific gotchas above.
2. A structural balance check (parens/braces, accounting for string and
   char literals) run after every edit.
3. Grepping for each new/changed function to confirm it's defined exactly
   once and used consistently.

None of this substitutes for an actual compile — always build and run
after a batch of changes, and report back anything the compiler flags.
See README.md for the three supported build paths (CMake, MinGW, MSVC).
