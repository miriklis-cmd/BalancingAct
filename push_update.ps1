# push_update.ps1 - stage, commit, and push the current version.
# The version comes from version.h and the commit message from the newest
# CHANGELOG.md entry, so this script never needs editing per release
# (v0.10.9: before this it carried a hard-coded message and printed the
# wrong version after v0.10.8 - the pushed code was right, the label wasn't).
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

# Version from version.h (#define APP_VERSION L"x.y.z").
$verLine = Select-String -Path (Join-Path $PSScriptRoot "version.h") -Pattern 'APP_VERSION\s+L"([^"]+)"' | Select-Object -First 1
if (-not $verLine) {
    Write-Host "FAILED: could not read APP_VERSION from version.h" -ForegroundColor Red
    exit 1
}
$version = $verLine.Matches[0].Groups[1].Value

# Newest CHANGELOG.md entry: "## [x.y.z] - title" and the lines under it.
$clLines = Get-Content -Path (Join-Path $PSScriptRoot "CHANGELOG.md") -Encoding UTF8
$start = -1
for ($i = 0; $i -lt $clLines.Count; $i++) {
    if ($clLines[$i] -match '^## \[\d+\.\d+\.\d+\]') { $start = $i; break }
}
if ($start -lt 0) {
    Write-Host "FAILED: no '## [x.y.z]' entry found in CHANGELOG.md" -ForegroundColor Red
    exit 1
}
if ($clLines[$start] -notmatch ('^## \[' + [regex]::Escape($version) + '\](?: - (.*))?$')) {
    Write-Host "FAILED: newest CHANGELOG.md entry is not [$version] (version.h says $version)." -ForegroundColor Red
    Write-Host "Newest entry is: $($clLines[$start])" -ForegroundColor Red
    exit 1
}
$title = $Matches[1]
$end = $clLines.Count
for ($j = $start + 1; $j -lt $clLines.Count; $j++) {
    if ($clLines[$j] -match '^## ') { $end = $j; break }
}
$body = ($clLines[($start + 1)..($end - 1)] -join "`n").Trim()
$subject = if ($title) { "v${version}: $title" } else { "v$version" }
$message = $subject + "`n`n" + $body + "`n"
# UTF-8 WITHOUT a byte-order mark (Out-File -Encoding utf8 adds one in Windows PowerShell 5.1).
[System.IO.File]::WriteAllText($msgFile, $message, (New-Object System.Text.UTF8Encoding($false)))

git commit -F $msgFile
Test-LastExit "git commit"
Remove-Item $msgFile -ErrorAction SilentlyContinue

# 5. Push.
git push
Test-LastExit "git push"

Write-Host "Done: v$version committed and pushed." -ForegroundColor Green
