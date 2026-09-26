# push_update.ps1 - stage, commit, and push the v0.9.43 update
# (full project audit: drift/staleness cleanup, one real bug found)
#
# Run from the repo root with:
#   powershell -ExecutionPolicy Bypass -File .\push_update.ps1
#
# Ordinary commit-and-push script - no history rewriting here, unlike the
# v0.9.39 fix-up script this replaces. See DevelopmentWorkflow.md's "Git"
# section for why this script is written the way it is (commit message via
# a temp file + `git commit -F`, never `-m` with a large/multi-line string -
# a literal `&` in a message broke PowerShell's argument-passing to git.exe
# on 2026-09-05 and silently committed nothing while the script kept going
# anyway; checking $LASTEXITCODE after every git call now prevents that).
#
# v0.9.43 ADDS ONE NEW CHECK: this version's audit found that `history\`
# (the Finalize Day folder, added v0.9.40) was never added to .gitignore
# until now - so IF a finalized day was on disk on a machine that already
# ran an earlier version of this script, real business data may already be
# committed. Step 2 below checks for that and stops before touching
# anything if it finds one, rather than silently committing more of it or
# staying quiet about what's already there.

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

# 2. Check whether any history\*.fbd files are already tracked by git from
#    before history\ was added to .gitignore in this version. This is
#    checked BEFORE staging anything, so this run can't make it worse.
$trackedHistory = git ls-files -- history/ 2>&1
Test-LastExit "git ls-files -- history/"
if (-not [string]::IsNullOrWhiteSpace($trackedHistory)) {
    Write-Host "STOPPING: real finalized day-file(s) already committed to git:" -ForegroundColor Red
    Write-Host $trackedHistory -ForegroundColor Red
    Write-Host ""
    Write-Host "This needs a decision before anything else is pushed - removing these" -ForegroundColor Yellow
    Write-Host "from tracking (git rm --cached) stops FUTURE commits from including them," -ForegroundColor Yellow
    Write-Host "but they will still be sitting in past commits/GitHub until those are" -ForegroundColor Yellow
    Write-Host "rewritten too (the same kind of history rewrite v0.9.39 did for Dark Mode)." -ForegroundColor Yellow
    Write-Host "Ask for help walking through that before running this script again." -ForegroundColor Yellow
    exit 1
}

# 3. Stage everything.
git add -A
Test-LastExit "git add -A"

# 4. Write the commit message to a temp file rather than passing it as a
#    -m argument (see the header comment above for exactly why).
$msgFile = [System.IO.Path]::GetTempFileName()
@'
v0.9.43: full project audit - drift/staleness cleanup, one real bug found

- Real bug: history\ (Finalize Day's folder, v0.9.40) was missing from
  .gitignore. Fixed - added alongside the existing backups\ entry.
- Stale docs/comments fixed: main.cpp's header comment and the Help >
  About dialog both still described only 3 tabs (missing "By Species");
  About also never mentioned Finalize Day or outlier price-flag review.
  ARCHITECTURE.md said "three top-level windows" (missing the Finalize
  Day popup) and had a garbled leftover paragraph fragment from an
  earlier edit. README.md never mentioned Finalize Day or outlier
  flagging, never mentioned the backups\/history\ folders, and had its
  own stale .fbd format example (missing the Flagged and FINALIZED=
  fields) - replaced with a pointer to DATA_FORMATS.md so it can't drift
  out of sync silently again.
- Dead code removed: GreetingForNow() in main.cpp had no remaining
  caller.
- Not fixed (flagged for the eventual main.cpp decomposition instead,
  see ROADMAP.md): RecalcTotals()/RenderReportPages() duplicate the same
  book-balance arithmetic, and ExportToCsv() hand-rolls aggregation
  instead of reusing FishBalanceCore.h's tested functions.

See CHANGELOG.md's [0.9.43] entry for full detail, and check the console
output above from THIS script for whether it found a history\ file
already tracked by git - if so, it stopped without committing anything
further and needs a decision before re-running.
'@ | Out-File -FilePath $msgFile -Encoding utf8

git commit -F $msgFile
Test-LastExit "git commit"
Remove-Item $msgFile -ErrorAction SilentlyContinue

# 5. Push.
git push
Test-LastExit "git push"

Write-Host "Done: v0.9.43 committed and pushed." -ForegroundColor Green
