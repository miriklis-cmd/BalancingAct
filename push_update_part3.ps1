# Repo cleanup - no app version bump, just fixing what surfaced while
# checking GitHub before moving to a new conversation.
#
# BEFORE running this:
# 1. Copy .gitignore and ARCHITECTURE.md's update from the latest zip
#    into this folder.
# 2. Manually delete app.rc from this folder (it's not something Claude
#    manages - see ARCHITECTURE.md's new note for why it's vestigial and
#    shouldn't come back).
# 3. Manually delete the Qt Creator artifact files from this folder too
#    (whatever *.creator/*.config/*.files/*.includes/*.cflags/*.cxxflags
#    files are sitting here) - the new .gitignore stops this recurring,
#    but won't remove files that are already tracked.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\push_update_part3.ps1

$ErrorActionPreference = "Stop"

function Assert-Success($step) {
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED at: $step (exit code $LASTEXITCODE)" -ForegroundColor Red
        exit 1
    }
}

Write-Host "Staging all changes..." -ForegroundColor Cyan
git add -A
Assert-Success "git add"

$status = git status --porcelain
if (-not $status) {
    Write-Host "Nothing to commit - did you complete steps 1-3 above first?" -ForegroundColor Yellow
    exit 0
}

Write-Host "Files being committed:" -ForegroundColor Cyan
git status --porcelain

Write-Host "Committing..." -ForegroundColor Cyan
$commitMessage = @"
Repo cleanup: remove vestigial app.rc and accidental Qt Creator files

app.rc contained raw RC syntax to embed the manifest as a resource,
directly conflicting with the deliberate external-side-by-side-manifest
approach all three build paths actually use (CMakeLists.txt,
build_mingw.bat, build_msvc.bat all confirmed to reference only
app_icon.rc, never app.rc). Unused, but a latent footgun if ever
accidentally added to a build - see ARCHITECTURE.md's new note.

Also removes several Qt Creator project index/cache files
(*.creator/*.config/*.files/*.includes/*.cflags/*.cxxflags) that got
committed accidentally when the project was opened in Qt Creator - this
project is built via CMake/MSVC/MinGW, not Qt Creator. Added .gitignore
to stop this recurring.
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
