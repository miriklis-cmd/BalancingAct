# Run AFTER git push --set-upstream origin master has succeeded, and
# AFTER actually extracting the v0.9.16 zip's contents into this folder
# (overwriting existing files, adding new ones - tests/,
# NETWORK_ARCHITECTURE.md, FishBalanceCore.h, etc.) The previous commit
# only captured 2 files because that extraction step was likely missed -
# this captures the real remainder with an honest, accurate message
# rather than reusing the same big "v0.9.1 through v0.9.16" text a
# second time.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\push_update_part2.ps1

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
    Write-Host "Nothing to commit - the folder may already be up to date, or the zip extraction still hasn't happened." -ForegroundColor Yellow
    exit 0
}

Write-Host "Files being committed:" -ForegroundColor Cyan
git status --porcelain

Write-Host "Committing..." -ForegroundColor Cyan
$commitMessage = @"
Sync remaining v0.9.1-v0.9.16 project files (main.cpp, tests/, docs)

The prior commit (Catch-up sync: v0.9.1 through v0.9.16) only captured 2
files, since the actual project files hadn't been copied into this
folder yet at that point. This commit captures the real remainder: the
current main.cpp, FishBalanceCore.h, the full tests/ suite, and all
project documentation (ARCHITECTURE.md, BUSINESS_RULES.md,
NETWORK_ARCHITECTURE.md, SecurityHardeningRegister.md, ROADMAP.md,
CHANGELOG.md, DATA_FORMATS.md), build files (CMakeLists.txt,
build_msvc.bat, build_mingw.bat), and version.h.
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
