# Run from inside your local BalancingAct repo folder, AFTER copying the
# v0.9.16 zip's contents into it (overwriting existing files, adding new
# ones - tests/, NETWORK_ARCHITECTURE.md, FishBalanceCore.h, etc.)
#
# Usage (from inside the repo folder):
#   powershell -ExecutionPolicy Bypass -File .\push_update.ps1
#
# Fixed from the previous version, which failed two ways: (1) a literal
# '&' in the commit message broke PowerShell's argument-passing to the
# native git.exe process, fragmenting the message and causing git to
# reject the whole commit as invalid pathspecs - fixed by writing the
# message to a temp file and using `git commit -F`, which sidesteps
# command-line argument parsing entirely, regardless of what characters
# are in the message; (2) the script didn't check $LASTEXITCODE after
# each git call, so it kept going after the commit had already failed -
# fixed by checking explicitly and stopping on any real failure.

$ErrorActionPreference = "Stop"

function Assert-Success($step) {
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED at: $step (exit code $LASTEXITCODE)" -ForegroundColor Red
        exit 1
    }
}

# Check a remote actually exists before doing anything else - this repo
# folder may never have been connected to GitHub with `git remote add`.
$remotes = git remote
if (-not $remotes) {
    Write-Host "No git remote configured in this folder." -ForegroundColor Red
    Write-Host "Run this once, then re-run this script:" -ForegroundColor Yellow
    Write-Host "  git remote add origin https://github.com/miriklis-cmd/BalancingAct.git" -ForegroundColor Yellow
    exit 1
}

Write-Host "Staging all changes..." -ForegroundColor Cyan
git add -A
Assert-Success "git add"

Write-Host "Committing..." -ForegroundColor Cyan
$commitMessage = @"
Catch-up sync: v0.9.1 through v0.9.16 (data integrity, testing, UX, docs)

This is a catch-up commit covering everything since the last real sync
point (v0.9.0 confirmed working) - not a per-version history, since
intermediate snapshots weren't preserved. Grouped by category:

Data-integrity fixes (external audit follow-up):
- LoadFromFile: transactional loading, rejects malformed/truncated/NaN-
  laden files instead of silently corrupting the recovery autosave
- Undo Delete no longer contaminates a different sheet after New/Open/
  Recent Files/rename
- Total Overview tab: fixed showing Kgs under the "Total (`$)" column
- Every save path (.fbd, settings.txt, recent.txt, emails.txt, CSV) now
  atomic and checked, not a truncate-and-hope
- Supplier email addresses now migrate correctly on rename/merge, with a
  user prompt when merged suppliers have conflicting saved emails
- Debtor/Cash and the in-progress Add Entry draft now persist reliably
  (autosave on focus-loss, not per-keystroke - a real performance fix at
  business volume of 500-1000 entries/day)
- Debtor/Cash expression parser: subtraction now works correctly
  (123+11-21 computes 113, not 134)
- Print Preview memory use capped (150 DPI preview cap + page limit)

Testing infrastructure (new):
- FishBalanceCore.h: platform-independent core logic extracted from
  main.cpp, zero Win32 dependency
- doctest-based suite in tests/ - 83 test cases / 244 assertions,
  covering parsing, formatting, aggregation, CSV/email logic, and a
  regression test for every fixed bug above

Build quality:
- Zero warnings under MSVC /W4 (was 12), all genuinely fixed not
  suppressed - CMakeLists.txt /EHsc, real safe-CRT usage under MSVC,
  portable fallback under MinGW, uninitialized-variable false positives
  cleaned up
- NOMINMAX fix for a std::min/std::max + windows.h macro collision

UI/UX:
- Combo box first-paint rendering bug fixed
- Manage Names hint text no longer cut off
- Status bar added (version + current filename)
- Data entry workflow: Species clears after Add Entry (Supplier stays),
  new Duplicate Supplier and Species button, Manage Names Apply button
  reflects whether there's actually anything to apply

Architecture and planning (docs):
- ARCHITECTURE.md: tab-vs-menu-item placement principle
- BUSINESS_RULES.md: corrected business model documentation (consignment
  agency, not buy-resell - Price is market price achieved, not cost)
- NETWORK_ARCHITECTURE.md (new): full multi-machine/encryption/hosting
  design - shared-key encryption with local DPAPI caching, AD-gated
  access, single-writer file locking with read-only fallback, SQLite
  hybrid reporting layer
- SecurityHardeningRegister.md: encryption plan finalized, cross-
  referenced rather than duplicated
- ROADMAP.md: three-bucket sequencing (A: single-machine complete; C:
  multi-machine/security; B: main.cpp decomposition), Price History
  feature fully spec'd

Process fix:
- DevelopmentWorkflow.md: corrected an inaccurate claim that Claude
  maintains a real git repo and commits to it (verified false - no
  credentials exist in Claude's sandbox). Established that a fresh
  commit-and-push script is generated after every version bump from now
  on, in PowerShell (.ps1), written via a temp file to avoid the
  argument-passing bug that broke this exact script the first time.
"@

$tempFile = [System.IO.Path]::GetTempFileName()
Set-Content -Path $tempFile -Value $commitMessage -Encoding UTF8
git commit -F $tempFile
Assert-Success "git commit"
Remove-Item $tempFile

Write-Host "Pushing to origin..." -ForegroundColor Cyan
git push
Assert-Success "git push"

Write-Host "Done." -ForegroundColor Green
