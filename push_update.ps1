# push_update.ps1 - stage, commit, and push the v0.9.51 update
# (test-automation refactor: remaining Phase 1 manual tests automated)
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
v0.9.51: test-automation refactor - remaining Phase 1 manual tests automated

Jack confirmed Reconciliation invalid-input handling and Finalize blocking
had been manually checked and worked, then asked for the remaining Phase 1
manual test areas to be automated. No application behavior changes other
than two cosmetic message-wording tweaks (see CHANGELOG.md). Phase 2 was
explicitly NOT started.

- New FishBalanceCore.h pure functions (tested in
  tests/test_recovery_and_finalize.cpp, 24 test cases): DecideAutosaveRecovery(),
  CoordinateFinalizeDay(), CoordinateSaveAs().
- New FishBalanceWin32IO.h: the Win32 file-I/O half of the app, moved out of
  main.cpp with injectable FileReadOps/FileWriteOps ports for deterministic
  fault injection.
- New FishBalanceControlLimits.h: ApplyFieldLengthLimits(), extracted out of
  main.cpp's WM_CREATE handler and the Manage Names dialog.
- main.cpp refactored for testable boundaries: SetExeDirOverrideForTests(),
  DeleteEntryAt(), ExecuteFinalizeDay(), CreateFishBalanceMainWindow()
  extracted; DoFileSaveAs()/LoadFromFile() now call the new pure/boundary
  functions.
- New Windows integration test suite (tests/win32_integration/, 28 test
  cases across 3 files + main.cpp's own #ifdef FBM_BUILDING_TESTS block):
  real-file-system loading tests, Save/Save As fault injection, a Windows
  control test for EM_LIMITTEXT/CB_LIMITTEXT, and a headless end-to-end
  smoke test - every test isolated from real application data via
  SetExeDirOverrideForTests and its own unique temp directory.
- run_tests.bat now builds and runs both the portable and Windows
  integration suites from one command. CMakeLists.txt gained a
  FishBalanceIntegrationTests target.

See CHANGELOG.md's [0.9.51] entry and ROADMAP.md's v0.9.51 status section
for full detail.
'@ | Out-File -FilePath $msgFile -Encoding utf8

git commit -F $msgFile
Test-LastExit "git commit"
Remove-Item $msgFile -ErrorAction SilentlyContinue

# 5. Push.
git push
Test-LastExit "git push"

Write-Host "Done: v0.9.51 committed and pushed." -ForegroundColor Green
