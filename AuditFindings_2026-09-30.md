# External Audit Findings Register — 2026-09-30

Source: an external, independent audit performed by OpenAI Codex, pasted to
Claude as a working brief with a phased remediation plan (Phase 0 through
Phase 6). This document is Claude's **Phase 0 deliverable**: every finding
in the brief has been independently verified against the actual current
source (v0.9.47, `main.cpp` 3,941 lines, `FishBalanceCore.h` 798 lines) by
reading the real code — not by trusting the audit's wording. Nothing in
this document is a code change. No production code has been modified as
part of this pass, per the audit's own Phase 0 gate and this project's
existing "verify before implementing" practice.

**Status: awaiting Jack's authorization to begin Phase 1.**

## How to read this

- **Severity**: Critical (live financial-data-loss/accounting-integrity
  path) / High / Medium / Low / Doc-only.
- **Verdict**: CONFIRMED / PARTIALLY CONFIRMED / ALREADY FIXED / INCORRECT
  — Claude's independent verdict, not the audit's claim.
- Findings are numbered F1–F29. IDs are stable — don't renumber if this
  file is extended later; append instead.

---

## Phase 1 candidates (highest priority — accounting/data-loss)

### F1 — Autosave recovery inherits named-file identity with no verification
**Severity: Critical** · **Verdict: CONFIRMED**

`wWinMain` (~3492–3527) loads `autosave.fbd` unconditionally at startup. If
that load *succeeds*, `g_currentFile` is set to `g_settings.lastFile`
(~3525) with **no check that the autosave content actually belongs to that
named file**. `UpdateTitle()` then shows that filename as if it were the
open document, and `DoFileSave()` (~1789) will happily overwrite it.

If the load *fails*, the existing hardening correctly withholds
`lastFile` (the `!autosaveLoadFailed` guard) — that path is already safe.
The gap is specifically the success case with stale/unrelated content.

**Reproduction**: open file B, let autosave.fbd get written for B, then
somehow end up with autosave.fbd holding file A's content while
`settings.txt` still says `lastFile = B` (e.g. a crash between opening B
and B's first autosave) → next launch silently binds A's content to B's
identity → Ctrl+S overwrites B with A's data.

**Depends on**: shares root cause with F3 (both are "recovered content
treated as a fully-trusted live document").

---

### F2 — `.fbd` structural validation accepts some invalid marker structures
**Severity: Medium** · **Verdict: PARTIALLY CONFIRMED**

`ParseFbdContent()` (`FishBalanceCore.h` 504–646) already rejects the two
cases the SecurityHardeningRegister #7 fix targeted: no recognized marker
at all, and `BEGIN` with no matching `END` (truncated write). That part of
the audit's framing undersells what's already fixed.

**Genuinely not checked**: multiple `BEGIN`/`END` pairs, `END` appearing
before any `BEGIN` (a bare `END` is a silent no-op, not an error), and
duplicate `DEBTOR=`/`CASH=` lines (each just overwrites the previous value
via plain assignment, ~529–534). A file with reordered/duplicated markers
still parses successfully.

**Real-world impact**: low — this format isn't hand-edited in normal use,
and the realistic crash-corruption case (truncated write) is already
caught. The remaining gap matters mainly for a hand-edited or maliciously
constructed file, which isn't this app's normal threat model per
BUSINESS_RULES.md, but is still worth tightening since the fix is cheap.

---

### F3 — Partial recovery (skipped bad rows) becomes the live, autosaved document
**Severity: Critical** · **Verdict: CONFIRMED**

`ParseFbdContent()`'s row loop (`FishBalanceCore.h` 575–621) skips
malformed rows and counts them, but still returns `ok = true`.
`LoadFromFile()` (`main.cpp` 703–751) unconditionally commits the reduced
row set to `g_entries` (718) and only *afterward* shows a warning dialog
(742–748) naming the skip count. Every caller — including the **startup
autosave path** — then calls `RefreshAll()`, which immediately re-autosaves
the reduced document, overwriting `autosave.fbd` with the smaller set.

**Real-world impact**: real and the most concrete of the three Phase-1
data-loss findings. A dismissed/half-read warning dialog and a handful of
real accounting rows (a delivery, a price) can vanish from the live book
with no undo path — and the reduced version is what gets backed up next.

---

### F4 — Real file reading is unbounded and several failure modes go unchecked
**Severity: High** · **Verdict: CONFIRMED**

`ReadAllLines()` (~469–494) and `LoadFromFile()`'s reader (~703–719) share
one pattern: `fseek`/`ftell` return values discarded, no upper size bound
before allocating the whole file into memory, `fread`'s return value
discarded (a short read is invisible — the tail is silently zero/garbage),
and `ferror()` is never checked after reading.

UTF-8 conversion (`Utf8ToW`, ~330–332) uses `MultiByteToWideChar` with flags
`0`, not `MB_ERR_INVALID_CHARS` — invalid byte sequences are silently
best-fit-substituted rather than rejected, and a conversion problem is
indistinguishable from "the file is legitimately empty."

**Real-world impact**: low-to-moderate given this is a single-user,
single-machine app reading its own previously-saved files (not untrusted
network input) — but a corrupted/truncated file (crash, disk issue, a
copy interrupted mid-transfer) is silently accepted rather than flagged,
which is a real robustness gap worth closing cheaply.

---

### F5 (Phase 1 item 5 in the brief) — `ParseSumExpr()` (reconciliation) fails open, not closed
**Severity: Critical** · **Verdict: CONFIRMED**

`ParseSumExpr()` (`FishBalanceCore.h` 190–214) has its own separate,
weaker parsing loop — it does **not** reuse the already-hardened
`ParseDoubleW()` (which has the NaN/Infinity + full-consumption fix from
SecurityHardeningRegister #8). It uses raw `std::stod` per `+`-separated
term in a `try/catch`, ignoring how much of each term was actually
consumed.

Traced against the audit's five test inputs:
| Input | Actual result |
|---|---|
| `1000+oops+250` | `1250` (bad term silently dropped) |
| `1000+12x+250` | `1262` (partial numeric prefix accepted, trailing `x` ignored) |
| `1000+nan` | `NaN` (no `isfinite` check at all) |
| `100++20` | `120` (empty term silently skipped) |
| `100+` | `100` (dangling operator silently ignored) |

`DoFinalizeDay()` (~2012–2013) gates purely on `g_diffOk`, a boolean
derived from this silently-wrong `double`. **There is no "reconciliation
text was invalid" state anywhere in the UI.** A typo in the Debtor/Cash
field can silently change the computed book total with zero visible
indication, and can produce a false "OK — balanced" result that gates a
real day-close (Finalize Day) for a live business.

This is the single highest-priority finding in the whole audit: it's the
one most directly connected to money, and the mechanism is the most
surprising (a typo silently changing a total that a human is trusting to
say "balanced").

---

## Phase 2 candidates (persistence durability / backups / state)

### F6 — Atomic writer: fixed `.tmp` filename (real gap), but audit's other claims about it are WRONG
**Severity: Low** · **Verdict: PARTIALLY CONFIRMED — audit conclusion needs correction**

`WriteFileAtomicUtf8()` (`main.cpp` 383–422) **does** check every
`fwrite`/`ferror`/`fflush`/`fclose` result (395–405) — the audit's claim
that "only `fflush` is treated as durability, nothing else is checked" is
**incorrect** against current code. Replacement uses `MoveFileExW(...,
MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` (417), which is a
legitimate atomic same-volume replace with a forced flush-to-disk — not
the "less atomic `fopen(dest,\"w\")`" pattern the audit worried about.

The one real gap: the temp filename is fixed (`path + L".tmp"`, 389), not
unique per call/process. Given this app has no documented multi-instance
usage (see F13 below), the collision window is close to theoretical.

**Correction to record**: this finding should not be treated as urgent —
recommend it ride along with F13 (multi-instance policy) rather than being
fixed in isolation.

---

### F7 — Writable state lives beside the executable, not `%LOCALAPPDATA%`
**Severity: Low (but undocumented)** · **Verdict: CONFIRMED as fact; audit's "design smell" framing is fair, but there is no existing documented rationale either way**

Confirmed via `GetExeDir()` and every path builder (`SettingsPath`,
`RecentFilesPath`, `EmailsPath`, `BackupDir`, `HistoryDir`, `autosave.fbd`)
— all beside the exe. No ARCHITECTURE.md/README.md/DevelopmentWorkflow.md/
NETWORK_ARCHITECTURE.md text was found stating this was a deliberate
simplicity/portability choice. This is a real, currently-**undocumented**
design point, not a clear regression — moving it is a genuine migration
project (legacy-file discovery, conflict handling, read-only-install-dir
handling), which is exactly why the audit correctly scoped it as Phase 2,
not Phase 1. No immediate action recommended beyond documenting the
current behavior and revisiting during the eventual Bucket C /
multi-machine work, where file location is already being redesigned
anyway (see NETWORK_ARCHITECTURE.md).

---

### F8 — Backup filename collision (one-second timestamp resolution)
**Severity: Low** · **Verdict: PARTIALLY CONFIRMED — narrower than the audit implies**

`WriteBackupSnapshot()` (~1170–1210) does use only second-resolution
timestamps with no ms/counter suffix. However, the routine 3-minute timer
path (`MaybeBackupOnTimer`) is self-throttled and content-deduplicated, so
timer-vs-timer collisions are **not reachable**. The realistic window is
narrow: two distinct manual Saves (or a Save immediately followed by
Finalize Day) of the *same open file* within the same second. Effect of a
collision is a lost duplicate snapshot (the newer content wins), not data
loss.

**Correction to record**: the audit's framing implies this is a routine
occurrence; it isn't — it's a low-frequency edge case worth a cheap fix
(add milliseconds), not an urgent one.

---

### F9 — Finalize Day: named-file save failure is silently ignored, reported as full success
**Severity: Critical** · **Verdict: CONFIRMED**

The real commit logic is `FinalizeWndProc`'s `ID_FIN_OK` handler
(1899–1933), not `DoFinalizeDay()` itself (which only does the balance
precheck). Sequence: history file write is checked and can abort (1920–
1924) → `g_finalizedDate` set (1926) → `g_dirty = false` (1928) → **line
1929: `if (!g_currentFile.empty()) SaveToFile(g_currentFile);` — return
value discarded** → unconditional "Finalized as `<date>`" success message
(1935–1937).

**Real-world impact**: if the named-file save fails (disk full,
permissions, file locked by another program), the `history\<date>.fbd`
record exists and in-memory state believes the day is finalized, but the
named `.fbd` on disk never got `FINALIZED=<date>` written into it —
reopening it later silently shows the day as **not** finalized, while the
user was told finalization fully succeeded. This can interact with F5:
if `g_diffOk` was wrong due to a `ParseSumExpr` quirk, an invalid day could
reach this code path at all.

---

## Phase 3 candidates (printing / GDI)

### F10 — Real printing renders and retains every full-resolution page before printing any of them
**Severity: High** · **Verdict: CONFIRMED**

`RenderReportPages()` (2591–2782) renders the **entire** report into a
`std::vector<RenderedPage>` of full-resolution DIBs before
`PrintBreakdownReport()` (2787) ever calls `StartDocW` (2815). Nothing is
freed until `FreeRenderedPages()` (2839), after `EndDoc`. The code's own
comment (2986–2988) states a single 24-bit page at 600 DPI is roughly
100MB — a worst-case 200-page report (`kMaxPages`, 2633) at real printer
DPI could reach tens of GB resident at once.

Print Preview shares the identical rendering function — it's protected
only by deliberately capping DPI at 150 (`kPreviewDpiCap`, 2994), not by a
separate low-memory code path. Real printing has no equivalent safeguard.

---

### F11 — GDI/print API calls inconsistently checked; font and DIB bitmap `SelectObject` never restored before deletion; no `AbortDoc` anywhere
**Severity: Medium** · **Verdict: PARTIALLY CONFIRMED**

`StartDocW` **is** checked (2815). `CreateFontIndirectW`,
`CreateDIBSection`, `CreateCompatibleDC`, `CreatePen`, `StartPage`,
`StretchDIBits`, `EndPage`, `EndDoc` are **not**. Two of the pen
select/restore/delete sequences (2648, 2769) are done correctly — but the
fonts (selected repeatedly at 2641/2654/2683/2687/2719/2721/2734/2751/2754)
and the page DIB bitmap itself (2675) are selected into the DC and **never
deselected** before `DeleteDC`/`DeleteObject` — the exact known GDI hazard
the audit describes. `AbortDoc()` has zero occurrences anywhere in the
file — no failure path exists for a mid-job GDI/print error.

**Correction to record**: the audit's blanket "not consistently checked"
framing is accurate in substance but slightly overstates it — `StartDocW`
and the pen handling in two of the drawing helpers are already done
correctly; the gap is specifically fonts, the DIB bitmap, and the other
listed GDI calls.

---

## Phase 4 candidates (input validation / injection)

### F12 — CSV formula-injection guard checks only `field[0]`, bypassable via leading whitespace
**Severity: Medium** · **Verdict: CONFIRMED**

`CsvField()` (`FishBalanceCore.h` 652–660) checks `field[0] == '='/'+'/
'-'/'@'` with no trimming. A value like `" =1+1"` (leading space) skips the
guard entirely and is written unescaped — a real, not theoretical, bypass
in Excel configurations/imports that tolerate a leading space before a
formula marker.

---

### F13 — Email recipient validation too permissive; recipient concatenated raw into `mailto:`
**Severity: Medium (mitigated by trust boundary)** · **Verdict: CONFIRMED**

`LooksLikeEmail()` (`FishBalanceCore.h` 739–746) doesn't reject CR/LF or
mailto-significant characters (`?`, `&`, `#`, `%`, `,`, `;`).
`EmailSupplier()` (`main.cpp` 3069–3087) concatenates the recipient
directly into the `mailto:` URL — only subject/body go through
`UrlEncodeForMailto()`. An address containing `?bcc=...` would append a
real parameter.

**Mitigating factor** (worth recording since it changes urgency, not
validity): supplier emails are entered by Jack himself via Manage Names —
not loaded from an untrusted file — and the architecture is explicitly
one-at-a-time mailto drafts that Jack reviews before sending (per
ARCHITECTURE.md). Real risk today is low; the gap matters if a `.fbd` is
ever hand-edited/shared, or a pasted address carries stray characters.

---

### F14 — No calendar validation; impossible dates (e.g. `2026-02-31`) accepted
**Severity: Medium** · **Verdict: CONFIRMED — already self-documented as a known, deliberate gap**

`ParseISODate()` (`FishBalanceCore.h` 100–118) only range-checks
year/month/day independently (day 1–31 regardless of month). The comment
directly above it already says this doesn't validate day-of-month and that
`2026-02-31` currently passes, "preserved here unchanged." Not a surprise
finding — it's a tracked, not-yet-fixed gap.

---

### F15 — No upper bound on Kgs/Price (beyond existing NaN/Infinity/negative checks)
**Severity: Low** · **Verdict: CONFIRMED**

`ParseDoubleW()` and `CommitEntryForm()` check non-finite and negative
values only. A price like `1e300` passes both checks; while it can't
individually overflow a `double`, summing many such entries or multiplying
`kgs * price` across report aggregation could reach `Infinity` and poison
downstream totals. Low probability given single trusted manual entry, but
cheap to bound.

---

## Phase 5 candidates (Win32 runtime robustness)

All nine items below are **CONFIRMED**, none already fixed. Grouped
together since they're all "unchecked Win32 API result" issues of similar
shape and low individual complexity to fix:

- **F16** — `EnableWindow(g_hMainWnd, FALSE)` is called right after
  `CreateWindowExW` for all three popups (Finalize ~2003–2006, Manage
  Names ~2569–2572, Print Preview ~3029–3032) with **no NULL check** on
  the returned HWND. If creation fails, the main window is disabled
  permanently (re-enable only happens in the popup's own `WM_DESTROY`,
  which never fires).
- **F17** — `RegisterClassExW` return not checked (main.cpp:3866).
- **F18** — Main window's `CreateWindowExW` return not checked
  (main.cpp:3900) — `ShowWindow`/`UpdateWindow`/the message loop all
  proceed unconditionally even on a NULL handle.
- **F19** — `InitCommonControlsEx` return not checked (main.cpp:3845).
- **F20** — `while (GetMessageW(&msg, nullptr, 0, 0))` (main.cpp:3913)
  treats a `-1` error return as truthy — an infinite spin on error rather
  than clean termination.
- **F21** — No monitor-bounds clamping of restored window x/y/w/h
  (`LoadSettings`, 502–524, only clamps minimum width/height) — a
  differently-arranged monitor setup on restore could put the window fully
  off-screen with no recovery path. Zero `MonitorFromRect`/
  `GetMonitorInfo` calls anywhere in the file.
- **F22** — No `WM_DPICHANGED` handler anywhere — dragging the window
  between differently-scaled monitors produces no re-layout.
- **F23** — `GetExeDir()` uses a fixed `wchar_t path[MAX_PATH]` buffer with
  `GetModuleFileNameW` and never checks for truncation (336–342) — every
  file path in the app (autosave, settings, backups, history) is built
  from this.
- **F24** — No single-instance protection (no mutex/window-lookup guard
  anywhere), and not documented as an accepted risk anywhere in the
  project docs — two copies of the app running on the same machine could
  both write to the same autosave/settings/backup files with no
  coordination.

---

## Documentation drift (verified directly by Claude, not delegated)

### F25 — `Testing.md` says "There's no automated test suite" — stale
**Severity: Doc-only** · **Verdict: CONFIRMED.** A 103-test doctest suite
exists, is wired into CMake/CTest, and Jack runs it routinely via
`run_tests.bat`. This line has been stale since the suite was added.

### F26 — `README.md`'s file table calls `main.cpp` "The entire application"
**Severity: Doc-only** · **Verdict: CONFIRMED.** Stale since
`FishBalanceCore.h` (798 lines of extracted domain logic) and `tests/`
exist.

### F27 — `README.md`'s "Note on testing" section doesn't mention the real automated suite
**Severity: Doc-only** · **Verdict: CONFIRMED.** Currently reads as if the
only verification path is "share a compile error and it'll get fixed" —
gives a materially wrong impression to a new reader.

### F28 — `ARCHITECTURE.md`'s "Build" section describes correctness-checking as entirely Claude's manual methods
**Severity: Doc-only** · **Verdict: CONFIRMED (incomplete, not wrong).**
Accurately describes Claude's own workflow (still true — Claude has no
compiler) but doesn't mention that Jack's own automated suite provides
real compiler-verified coverage for the portable logic, which reads as if
zero automated testing exists anywhere in the project.

### F29 — Warning-gate enforcement is inconsistent and weaker than any of the audit's or this project's own documentation implies
**Severity: Medium** · **Verdict: CONFIRMED, and worse than the audit assumed.**
`CMakeLists.txt` uses `/W4` only (no `/WX`) for both the app and test
targets. **`build_msvc.bat` and `build_mingw.bat` pass zero warning flags
at all** — not `/W4`, not even `-Wall`. `tests/run_tests.ps1` (what Jack
actually runs) also uses `/W4` only, no `/WX`. The audit's own claimed
verification ("GCC with `-Wall -Wextra -Wpedantic -Werror`") was evidently
run by the auditor with an ad-hoc invocation, not through any script this
repo actually ships — so that claim is accurate about the *code*, but
doesn't reflect what this project's *build scripts* currently enforce.

---

## Corrections to the external audit's own conclusions

1. **F6 (atomic writer)** — the audit's specific claim that only `fflush`
   is checked and nothing else is wrong; the code already checks every
   write/flush/close result and uses a real atomic `MoveFileExW` replace.
   Only the fixed temp-filename collision risk is real, and it's low-impact
   given no documented multi-instance usage.
2. **F8 (backup collisions)** — the audit's framing implies this is a
   routine risk from the normal 3-minute autosave timer; it isn't (that
   path is self-throttled and content-deduplicated) — the real window is
   narrow (same-second manual Saves of the same file).
3. **F11 (GDI checks)** — "not consistently checked" is accurate in
   substance but overstated as a blanket claim; `StartDocW` and two pen
   select/restore/delete sequences are already done correctly. The real
   gap is specifically fonts, the DIB bitmap, and the other listed calls.
4. **F29 (warning gates)** — the audit's own cited verification command
   doesn't match what's actually wired into any of this repo's four build
   paths, and the batch-file paths are worse than `/W4`-only (zero flags),
   which the audit didn't call out specifically.

## Additional findings not in the original audit brief

- **Interaction between F5 and F9**: if `ParseSumExpr`'s silent
  misparse ever produced a false "balanced" result, that directly enables
  reaching F9's Finalize-Day partial-failure path on a day that shouldn't
  have been finalizable at all. Recommend fixing F5 before or alongside F9
  rather than treating them as fully independent.
- **Process recommendation**: this register should eventually be folded
  into `SecurityHardeningRegister.md`'s existing tracked-issue format once
  Phase 1 items are actually fixed, rather than living as a permanently
  separate file — consistent with how this project already tracks
  security/integrity findings in one place.

---

## Proposed Phase 1 change set (pending authorization)

Scope: exactly the five Phase-1 items from the audit brief (F1, F2, F3,
F4, F5 above). Each is proposed as its own small, independently
buildable/testable change, per the audit's own "small phases" rule and
this project's existing workflow.

1. **F5 first** (`ParseSumExpr` fail-closed) — highest severity, most
   isolated, lowest risk to existing behavior. Change `ParseSumExpr` to
   return a structured result (e.g. `{ bool ok; double value; std::wstring
   errorTerm; }`) built on top of the *existing* hardened `ParseDoubleW()`
   instead of raw `std::stod`, requiring full term consumption and
   rejecting non-finite values. Reconciliation display shows "invalid"
   instead of a number when `ok` is false. `DoFinalizeDay()` blocks with a
   clear message when either expression is invalid, not just when
   unbalanced.
2. **F1** (autosave identity) — add a minimal identity check: only assign
   `g_currentFile = lastFile` if there's a plausible reason to trust the
   pairing (e.g. record which file autosave.fbd was last written *for*,
   alongside the existing content, and compare on load). Old autosaves
   with no such marker fall back to "recovered/unsaved" (matching how a
   load failure already behaves) rather than silently inheriting identity.
3. **F3** (partial recovery) — make `LoadFromFile()` treat any skipped row
   as a hard failure for *ordinary* Open/startup-recovery (matching how a
   totally-unparseable file already behaves), returning the exact
   failed line numbers/reasons instead of silently committing a reduced
   document. This is the biggest behavior change of the five — needs
   careful historical-fixture testing (see below) to avoid breaking
   legitimate old files that currently load with a few tolerated quirks.
4. **F2** (.fbd structural strictness) — add BEGIN/END count and ordering
   checks, and reject (or at minimum, warn distinctly rather than silently
   overwrite) duplicate `DEBTOR=`/`CASH=` lines.
5. **F4** (bounded/checked file reading) — check every `fseek`/`ftell`/
   `fread` result and `ferror()`, add a generous file-size ceiling (e.g.
   100MB — several orders of magnitude above any realistic `.fbd`), and
   switch `Utf8ToW` to `MB_ERR_INVALID_CHARS` so invalid UTF-8 is rejected
   with a distinct error rather than silently mangled or indistinguishable
   from empty content.

## Proposed tests for Phase 1

- Autosave belongs to the last named file / belongs to a different file /
  named file changed externally / named file missing / old autosave with
  no identity metadata / autosave malformed — per the audit's own list.
- `.fbd` fixtures: exactly-one-BEGIN/END (existing, must still pass);
  BEGIN-with-no-END (existing, must still reject); END-before-BEGIN
  (new, must reject); duplicate BEGIN or duplicate DEBTOR=/CASH= (new,
  must reject or flag); a handful of the actual historical 4-/6-/7-field
  fixtures already in `tests/` (must still pass unchanged — this is the
  non-negotiable compatibility gate for F3's stricter behavior).
- `ParseSumExpr`/reconciliation: the five inputs from the audit brief
  (`1000+oops+250`, `1000+12x+250`, `1000+nan`, `100++20`, `100+`) must
  all be rejected; legitimate inputs (`1000`, `1000+250`, `1000 + 250`
  with whitespace if currently supported) must still work identically to
  today.
- File-reading bounds: a file just over the new size ceiling is rejected
  with a clear message; a file with deliberately invalid UTF-8 bytes is
  rejected rather than silently mangled; a short-read simulation (where
  practical in a portable test) is detected.

---

## Phase 1 completion status (2026-09-30, v0.9.48)

Phase 1 is complete, in the priority order Jack authorized (F5 → F9 → F1 →
F3+F2 → F4, with F9 promoted in and F6 kept deferred). Each is now folded
into `SecurityHardeningRegister.md` as its own permanent entry (#10-#14)
and summarized in `CHANGELOG.md`'s `[0.9.48]` entry / `ROADMAP.md`:

- **F5** — FIXED. `ParseSumExprStrict()` in `FishBalanceCore.h`.
- **F9** — FIXED. `FinalizeWndProc`'s `ID_FIN_OK` handler in `main.cpp`.
- **F1** — FIXED. `SOURCE_FILE=` marker + startup identity check,
  `FishBalanceCore.h` + `main.cpp`.
- **F3 / F2** — FIXED (combined). `ParseFbdContent()` in
  `FishBalanceCore.h`.
- **F4** — FIXED. `ReadAllBytes()` + `Utf8ToW()` strict decoding in
  `main.cpp`.
- **F6** — still deferred, per Jack's explicit instruction (confirmed
  already fixed, not a Phase 1 defect; only the fixed-temp-filename/
  multi-instance question remains open, and it is not Phase 1 work).
- **F29** (warning gates) and **F25-F28** (doc drift) were also addressed
  as part of this pass, since Jack's authorization specifically called
  out both.

Not verified by an actual compile - no compiler is available in this
environment. Every change was checked by careful reading plus a
structural balance check; Jack's own build and the automated test suite
are the real verification step for this Phase 1 work. F10 onward (Phase
2+ of this audit) are explicitly NOT started, per Jack's instruction to
stop after Phase 1.

---

*This document does not supersede `SecurityHardeningRegister.md` — it is
the working register for this specific external audit until Phase 1 items
are fixed and folded into that file's permanent record.*
