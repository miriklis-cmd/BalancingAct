# Development Workflow

This project is built through a conversational, AI-assisted workflow rather
than a conventional dev setup. This file documents how that actually works
in practice, because it's unusual enough to cause real confusion if it's
not written down (it already has, more than once).

## The core loop

1. You describe a feature or bug in chat.
2. Claude edits `main.cpp` (and supporting files) directly.
3. Claude runs a **structural check** (see below) — this is the only
   verification available; there is no compiler in Claude's environment.
4. Claude packages the whole project into a zip and delivers it.
5. **You build and test on your own Windows machine** — this is the only
   point in the loop where the code is actually compiled and run.
6. You report back what happened (worked / didn't / build error / runtime
   bug), and the loop repeats.

Claude cannot skip step 5 for you. Every feature is "unverified" until you
report it working.

## Verification without a compiler

Since there's no C++ compiler available, every change is checked by:

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

**Corrected 2026-09-05** (an earlier version of this section claimed
Claude maintains a real git repository and commits to it - verified
false: this sandbox has no `.git` folder, no SSH keys, no GitHub token,
no credential helper. Network access to github.com works, but with
nothing to authenticate with, so even reading a private repo fails, let
alone pushing).

Claude **cannot commit or push to GitHub in this environment** — no
persistent credentials, and this sandbox resets between sessions even if
it could. You own the actual GitHub repo (`miriklis-cmd/BalancingAct`)
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

## Testing responsibility

Claude cannot run the Windows executable. All functional testing —
does the feature work, does the UI look right, does the build even
succeed — happens on your machine. See Testing.md for the running
checklist of what to verify after each build.
