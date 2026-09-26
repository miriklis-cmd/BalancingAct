# push_update.ps1 - stage, commit, and push the v0.9.41 update
# (two real bugs fixed from v0.9.40's first live check)
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

# 2. Stage everything.
git add -A
Test-LastExit "git add -A"

# 3. Write the commit message to a temp file rather than passing it as a
#    -m argument (see the header comment above for exactly why).
$msgFile = [System.IO.Path]::GetTempFileName()
@'
v0.9.41: fix two real bugs found in v0.9.40's first live check

- Cash label box was too narrow for its own text and silently
  word-wrapped/clipped, making it sit visibly lower than the Debtor
  label beside it. Widened and realigned.
- Choosing "Start a new entry sheet now?" after finalizing left the new
  sheet still locked (button still read "Un-finalize Day", entry
  controls still disabled) because g_finalizedDate/g_currentFile weren't
  actually cleared for it. Now detaches fully, same as File > New.

See CHANGELOG.md's [0.9.41] entry for full detail.
'@ | Out-File -FilePath $msgFile -Encoding utf8

git commit -F $msgFile
Test-LastExit "git commit"
Remove-Item $msgFile -ErrorAction SilentlyContinue

# 4. Push.
git push
Test-LastExit "git push"

Write-Host "Done: v0.9.41 committed and pushed." -ForegroundColor Green
