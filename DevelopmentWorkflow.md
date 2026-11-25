# Development Workflow

This project is built through a conversational, AI-assisted workflow rather
than a conventional dev setup. This file documents how that actually works
in practice, because it's unusual enough to cause real confusion if it's
not written down (it already has, more than once).

## The core loop

1. You describe a feature or bug in chat.
2. Claude edits the source (`main.cpp`, `src/`, `winui/`) directly.
3. Claude runs the portable unit tests with g++ (353 cases) and a **structural check** (see below). There is no MSVC
   or Windows in Claude's environment, so `main.cpp` and the WinUI project are never compiled there.
4. Claude packages the **changed files** into a zip (new version number every time) and delivers it.
5. **You build and test on your own Windows machine** — this is the only
   point in the loop where the code is actually compiled and run.
6. You report back what happened (worked / didn't / build error / runtime
   bug), and the loop repeats.

Claude cannot skip step 5 for you. Every feature is "unverified" until you
report it working.

## Verification without a compiler

Since Claude cannot build the Windows code, every change to it is checked by:

- **Structural balance check**: a script that strips string/char literals
  and comments, then confirms braces `{}` and parens `()` are balanced.
  Catches structural mistakes (a missing closing brace, a stray edit that
  broke a block) but says nothing about actual correctness.
- **Careful manual reading**, especially against the known gotchas listed
  in ARCHITECTURE.md — several real bugs have come from repeating a
  mistake that was already made and fixed once (see
  SecurityHardeningRegister.md and CHANGELOG.md).
- **Grepping for function uniqueness and wiring** — confirming a new
  function is defined exactly once, and that every new control/menu item
  is actually created, laid out, and connected to a handler.

None of this is a substitute for compiling. Treat anything Claude ships as
"should work, not yet proven" until you've actually built and run it.

## Claude's sandbox can reset between sessions

Claude's working environment is not guaranteed to persist between
conversation turns or sessions. This has happened multiple times during
this project — Claude's local copy of the files disappears, and it has to
be restored from **the last zip actually delivered to you**. Practical
implications:

- The zip you download after each response is the real source of truth,
  not anything Claude might reference having done in an earlier message
  that was never actually packaged and delivered.
- If Claude was mid-way through a change when a reset happens, that
  in-progress work is lost and has to be redone from the last delivered
  zip — this already happened once with the Date/Notes feature (first
  attempt was abandoned mid-way; the version that shipped was a clean
  second attempt against the last known-good build).
- If something Claude describes doesn't match what you're actually
  looking at, the most likely explanation is a reset happened and Claude
  is temporarily out of sync — say so, and Claude will re-sync from the
  zip in your outputs folder.

## Git

**Updated 2026-11-25.** The repo (`miriklis-cmd/BalancingAct`) is public, so Claude can **clone and read it** in
its sandbox (useful after a reset, to rebuild the working tree). Claude has **no credentials to push** and must not
try: the sandbox has no GitHub login, and it resets between sessions anyway. You own the actual GitHub repo (`miriklis-cmd/BalancingAct`)
and the push step, using your own already-authenticated local git setup.

**Standing requirement, not optional, not just for large changes**:
**after every version bump**, Claude generates a commit message and a
small `push_update.ps1` PowerShell script (stage everything, commit with
message, push) for you to run locally, in the same response as the
version's zip. This replaced an earlier, weaker pattern (a script was
generated once early in the project, then not regenerated again for many
subsequent versions, leaving a large gap in real git history that had to
be caught up in one large "catch-up" commit) - the whole point of doing
this every time is a real, granular commit history, not another gap.

If a response ships a version bump without this, that's a process
mistake — say so, the same as any other missed step.

**Technical requirements for the script itself** (a real failure on
2026-09-05 established these the hard way: a first attempt passed a
large commit message directly as a `-m` argument, and a literal `&` in
the message broke PowerShell's argument-passing to the native `git.exe`
process, fragmenting the message and causing git to reject the whole
commit as invalid pathspecs — nothing was actually committed, and the
script then kept going anyway and hit a second, unrelated failure trying
to push):
- **Write the commit message to a temp file and use `git commit -F
  <file>`**, never `-m` with a large/multi-line string. This sidesteps
  PowerShell's native-command argument-parsing entirely, regardless of
  what characters end up in the message.
- **Check `$LASTEXITCODE` after every git invocation** and stop on
  failure. `$ErrorActionPreference = "Stop"` does NOT automatically do
  this for native/external commands the way it does for PowerShell's own
  cmdlet errors - a failed `git commit` will silently let the script
  continue to `git push` otherwise.
- **Verify a remote is actually configured before doing anything else**
  (`git remote` returns something) - fail with a clear, actionable
  message if not, rather than let `git push` fail confusingly at the end
  after the commit already succeeded.

## Git history cleanup (a data file was committed once)

`15-10-26.fbd`, a real saved sheet, was committed in 0.10.16 and deleted in 0.10.17, but it stays in the public history.
`*.fbd` has been in `.gitignore` since 0.10.17, and since 0.24.6 `push_update.ps1` also stops if any `.fbd`, data or secret file is tracked or staged. To remove it from history, run `purge_fbd_history.ps1` (kept outside
the repo, supplied with the 0.24.5 zip) from a fresh clone, then force-push. Everyone with a clone must re-clone afterwards.
Old commit links on github.com may keep working for a while; GitHub Support can be asked to clear cached views. Treat
the data as already seen by others.

## Testing responsibility

Claude cannot run the Windows executable. All functional testing —
does the feature work, does the UI look right, does the build even
succeed — happens on your machine. See Testing.md for the running
checklist of what to verify after each build.

`run_tests.bat` (project root, added 2026-09-22) unblocks every file in
the project folder (Windows marks files extracted from a downloaded zip
as "from the internet," which blocks scripts even under a correctly-set
`RemoteSigned` execution policy) and then runs `tests\run_tests.ps1` -
run this instead of `run_tests.ps1` directly after extracting a new
zip. It bypasses execution policy for just that one run rather than
requiring you to change it, so there's nothing to set up front or
remember afterward. Needs `cl.exe` on PATH: run it, `build_msvc.bat` and
`push_update.ps1` from **"Developer PowerShell for VS"** (a plain PowerShell fails with "cl not recognized").
`push_update.ps1` is not unblocked by `run_tests.bat` if a zip is extracted *after* the tests ran - re-run
`run_tests.bat` after extracting, or `Unblock-File .\push_update.ps1`.

**Per-version routine (Jack):** extract the zip over the project folder, then `.\run_tests.bat`,
`.\build_msvc.bat`, the manual checks listed with the version, then `.\push_update.ps1`. Zips are packaged
with Windows (CRLF) line endings so git reports no line-ending warnings. Never save sheets (`.fbd`) inside the
project folder - `*.fbd` is git-ignored since 0.10.17 because a saved sheet was once committed.

## Project folder layout — what goes where in the zip

```
<project root>/
  main.cpp, version.h, resource.h, build_msvc.bat, ...
  app.manifest, app.ico, app_icon.rc
  src/
    core/FishBalanceCore.h, FishBalanceControlLimits.h   <- pure logic (was in the root before v0.10.0)
    data/  services/  forms/  PlatformWin.h              <- see src/README.md
  tests/
    run_tests.ps1, doctest.h, doctest_setup.h, test_*.cpp
    win32_integration/...
```

The test `.cpp` files themselves `#include "../src/core/FishBalanceCore.h"` -
so they need the whole test suite (`run_tests.ps1`, `doctest.h`,
`doctest_setup.h`, all `test_*.cpp`) to live inside a `tests\` subfolder
one level below the project root, never flat alongside `main.cpp`.

This broke once (v0.9.18-era) - Claude's project-knowledge view of these
files carries no folder information, so every zip up to that point
flattened the whole test suite to the project root. You'd been merging
zips into your existing correctly-structured local folder, so it mostly
went unnoticed until a stray root-level `run_tests.ps1` got run directly
and failed to find the core header. Fixed by explicitly nesting
`tests/` in the zip - Claude has the real content of every test-suite
file, this was purely a packaging-path mistake, not missing content.

`app.manifest`, `app.ico`, and `app_icon.rc` were a related but separate
gap: unlike the test suite, they weren't in Claude's project knowledge
at all (no trace, not even a stray reference), so no accurate copy could
be shipped and Claude didn't try to guess (especially `app.ico`, a
binary icon file - a guess would just ship a different icon than the
real one). **Added to the project 2026-09-21** — all three now ship at
the project root in every zip, alongside `main.cpp`.

## Line endings (why git warns "LF will be replaced by CRLF")

Jack's git has `core.autocrlf=true`, so any text file that reaches his working copy with LF endings triggers that warning on `git add`. Every delivered zip must therefore have CRLF in EVERY text file (.cpp .h .md .txt .ps1 .bat .rc .json .py .manifest .gitignore): convert after the last edit and before zipping - editing tools and scripts write LF for new or rewritten files. Binary files (.ico, .aps) are left alone. One-off silence on his side: `git config core.safecrlf false`.
