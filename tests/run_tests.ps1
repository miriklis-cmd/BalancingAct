# run_tests.ps1
#
# Compiles and runs the FishBalanceCore.h automated test suite (doctest).
# This is a small, separate console executable (FishBalanceTests.exe) -
# no GUI window ever opens, and it's completely independent of building
# the actual FishBalanceManager.exe app.
#
# Requires cl.exe on PATH - run this from a "Developer PowerShell for VS"
# (Start Menu > Visual Studio > Developer PowerShell for VS), or a regular
# PowerShell session where you've already run vcvarsall.bat / the VS dev
# environment setup.
#
# Usage (from inside this tests/ folder):
#   .\run_tests.ps1
# or from anywhere:
#   powershell -ExecutionPolicy Bypass -File path\to\tests\run_tests.ps1

$ErrorActionPreference = "Stop"
$testDir = $PSScriptRoot
Push-Location $testDir

$sources = @(
    "test_main.cpp",
    "test_parsing.cpp",
    "test_formatting.cpp",
    "test_fbd_loader.cpp",
    "test_aggregation.cpp",
    "test_csv_export.cpp",
    "test_email.cpp"
)

Write-Host "Compiling FishBalanceTests.exe..." -ForegroundColor Cyan
& cl /nologo /EHsc /W4 /std:c++17 /DUNICODE /D_UNICODE $sources /Fe:FishBalanceTests.exe
$buildExitCode = $LASTEXITCODE

# Clean up the .obj files cl.exe drops in this folder by default - keep
# the tests/ folder tidy between runs.
Remove-Item -ErrorAction SilentlyContinue *.obj

if ($buildExitCode -ne 0) {
    Write-Host ""
    Write-Host "BUILD FAILED - see compiler output above." -ForegroundColor Red
    Pop-Location
    exit 1
}

Write-Host ""
Write-Host "Running tests..." -ForegroundColor Cyan
Write-Host ""
& .\FishBalanceTests.exe
$testExitCode = $LASTEXITCODE

Pop-Location

Write-Host ""
if ($testExitCode -eq 0) {
    Write-Host "ALL TESTS PASSED" -ForegroundColor Green
} else {
    Write-Host "TESTS FAILED - see doctest output above for exactly which " -ForegroundColor Red -NoNewline
    Write-Host "test, what was expected, and what was actually returned." -ForegroundColor Red
}
exit $testExitCode
