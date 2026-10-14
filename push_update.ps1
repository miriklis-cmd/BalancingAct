# push_update.ps1 - stage, commit, and push the v0.10.5 update
# (Phase 2 security hardening: print/preview resource safety, CSV/email
# injection, numeric & calendar validation)
#
# Run from the repo root with:
#   powershell -ExecutionPolicy Bypass -File .\push_update.ps1
#
# Ordinary commit-and-push script - no history rewriting here. See
# DevelopmentWorkflow.md's "Git" section for why this script is written the
# way it is (commit message via a temp file + `git commit -F`, never `-m`
# with a large/multi-line string - a literal `&` in a message broke
# PowerShell's argument-passing to git.exe on 2026-09-05 and silently
# committed nothing while the script kept going anyway; checking
# $LASTEXITCODE after every git call now prevents that).

$ErrorActionPreference = "Stop"

function Test-LastExit($stepName) {
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED at step: $stepName (exit code $LASTEXITCODE)" -ForegroundColor Red
        exit 1
    }
}

# 1. Verify a remote is actually configured before doing anything else.
$remotes = git remote 2>&1
Test-LastExit "git remote"
if ([string]::IsNullOrWhiteSpace($remotes)) {
    Write-Host "FAILED: no git remote configured in this repo." -ForegroundColor Red
    exit 1
}

# 2. Check whether any history\*.fbd files are already tracked by git -
#    real finalized-day business data should never be committed (see
#    .gitignore). Checked BEFORE staging anything, so this run can't make
#    it worse.
$trackedHistory = git ls-files -- history/ 2>&1
Test-LastExit "git ls-files -- history/"
if (-not [string]::IsNullOrWhiteSpace($trackedHistory)) {
    Write-Host "STOPPING: real finalized day-file(s) already committed to git:" -ForegroundColor Red
    Write-Host $trackedHistory -ForegroundColor Red
    Write-Host ""
    Write-Host "This needs a decision before anything else is pushed - removing these" -ForegroundColor Yellow
    Write-Host "from tracking (git rm --cached) stops FUTURE commits from including them," -ForegroundColor Yellow
    Write-Host "but they will still be sitting in past commits/GitHub until those are" -ForegroundColor Yellow
    Write-Host "rewritten too. Ask for help walking through that before running this" -ForegroundColor Yellow
    Write-Host "script again." -ForegroundColor Yellow
    exit 1
}

# 3. Stage everything.
git add -A
Test-LastExit "git add -A"

# 4. Write the commit message to a temp file rather than passing it as a
#    -m argument (see the header comment above for exactly why).
$msgFile = [System.IO.Path]::GetTempFileName()
@'
v0.10.5: main.cpp split stage 4b - report rendering moved to services

v0.9.53 Phase 2 security hardening - print/preview resource safety,
CSV/email injection, numeric & calendar validation

Phase 2 of the post-audit hardening work (Phase 1 was v0.9.49-v0.9.52).
Addresses the remaining open findings from the external audit/findings
register: F10/F11 (print/preview resource handling), F12 (CSV
formula-injection leading-whitespace gap), F13 (email/mailto injection),
F14 (calendar validation), F15 (no numeric upper bound). F6 (atomic
writer) was re-verified against current source and confirmed already
disproven/fixed - correctly left alone, per Jack's explicit instruction
not to revive it.

1. Print / Print Preview resource safety (F10/F11): RenderReportPages
   rewritten with upfront dimension/DPI validation, checked GDI calls, new
   RAII wrappers (UniqueFont/UniquePen/UniqueBitmap/UniqueDC/ScopedSelect),
   one-page-at-a-time real printing (was: render every page before
   printing any), an explicit documented Print Preview memory budget, and
   AbortDoc on failure. The page-count cap's silent truncation (a newly
   discovered, tightly-coupled defect) now surfaces a warning to the user.
2. CSV/spreadsheet-injection hardening (F12): CsvField now strips NUL/CR/
   other C0 controls and the formula-injection guard looks past leading
   whitespace (closes the documented " =1+1" bypass).
3. Email/mailto hardening (F13): LooksLikeEmail rejects control and
   mailto-significant characters, applied at both entry and emails.txt
   load time; new BuildMailtoUri percent-encodes recipient/subject/body
   independently and refuses an invalid stored address outright.
4. Numeric and calendar validation (F14/F15): ParseISODate now validates
   real calendar correctness (leap years, month lengths). New explicit
   Kgs/Price ceilings (10,000kg / $250/kg - confirmed with Jack, not
   invented) enforced at entry and file load; new overflow-safe
   accumulation applied to the Entered Total.

New/updated tests across tests/test_csv_export.cpp, tests/test_email.cpp,
tests/test_parsing.cpp, tests/test_fbd_loader.cpp, and a new
tests/test_phase2_numeric_and_layout.cpp. BUSINESS_RULES.md and
DATA_FORMATS.md updated to document the new policies. No compiler is
available in the environment that produced this change - verified by
careful reading and a structural balance check only; Jack's
run_tests.bat/build_msvc.bat results are the real verification step.

See CHANGELOG.md's [0.9.53] entry and ROADMAP.md's status section for
full detail.
'@ | Out-File -FilePath $msgFile -Encoding utf8

git commit -F $msgFile
Test-LastExit "git commit"
Remove-Item $msgFile -ErrorAction SilentlyContinue

# 5. Push.
git push
Test-LastExit "git push"

Write-Host "Done: v0.10.5 committed and pushed." -ForegroundColor Green
