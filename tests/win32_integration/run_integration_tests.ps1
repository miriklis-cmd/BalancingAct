# run_integration_tests.ps1
#
# Compiles and runs the Windows integration test suite (Phase 1 test-
# automation refactor, v0.9.51) - a separate console executable
# (FishBalanceIntegrationTests.exe) that:
#   - compiles ..\..\main.cpp itself with -DFBM_BUILDING_TESTS, so its
#     control-limit test (item 5) and headless end-to-end smoke test (item 6)
#     get real access to main.cpp's own internal-linkage globals and its real
#     window-creation code, guarded from ever touching this machine's real
#     autosave/settings/backups/history/named files via
#     SetExeDirOverrideForTests - never the real application directory or
#     user data. -DFBM_BUILDING_TESTS also removes main.cpp's own WinMain, so
#     it doesn't collide with the doctest-generated main() below.
#   - links test_io_integration.cpp / test_save_fault_injection.cpp (items
#     3/4), which exercise FishBalanceWin32IO.h directly against real
#     temporary files/directories, with no dependency on main.cpp's globals.
#   - links test_integration_main.cpp, which supplies doctest's main().
#
# Every test creates its own unique temporary directory (see each file's
# TempDir/TempTestDir helper) and cleans it up when the test finishes -
# never the real application folder or real user data anywhere.
#
# Requires cl.exe on PATH - run this from a "Developer PowerShell for VS", or
# a regular PowerShell session where the VS dev environment is already set up.
#
# Usage (from inside this tests\win32_integration\ folder):
#   .\run_integration_tests.ps1
# or from anywhere:
#   powershell -ExecutionPolicy Bypass -File path\to\tests\win32_integration\run_integration_tests.ps1

$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$root = Resolve-Path (Join-Path $here "..\..")
Push-Location $here

$sources = @(
    (Join-Path $root "main.cpp"),
    "test_io_integration.cpp",
    "test_save_fault_injection.cpp",
    "test_integration_main.cpp"
)

Write-Host "Compiling FishBalanceIntegrationTests.exe..." -ForegroundColor Cyan
# Same /W4 /WX warning gate as every other build path here (see
# AuditFindings_2026-09-30.md finding F29) - a warning is a build failure,
# not something to notice later in scrollback. main.cpp needs the same
# Win32 GUI libs it always needs (it creates a real, if hidden, window in
# the headless smoke test) plus the app's own include directory, since this
# script runs from tests\win32_integration\ but main.cpp's #includes
# ("FishBalanceCore.h", "resource.h", etc.) are relative to the repo root.
& cl /nologo /W4 /WX /EHsc /std:c++17 /DUNICODE /D_UNICODE /DFBM_BUILDING_TESTS `
    /I "$root" `
    $sources `
    user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib winspool.lib `
    /Fe:FishBalanceIntegrationTests.exe
$buildExitCode = $LASTEXITCODE

Remove-Item -ErrorAction SilentlyContinue *.obj

if ($buildExitCode -ne 0) {
    Write-Host ""
    Write-Host "INTEGRATION TEST BUILD FAILED - see compiler output above." -ForegroundColor Red
    Pop-Location
    exit 1
}

Write-Host ""
Write-Host "Running Windows integration tests..." -ForegroundColor Cyan
Write-Host ""
& .\FishBalanceIntegrationTests.exe
$testExitCode = $LASTEXITCODE

Pop-Location

Write-Host ""
if ($testExitCode -eq 0) {
    Write-Host "ALL INTEGRATION TESTS PASSED" -ForegroundColor Green
} else {
    Write-Host "INTEGRATION TESTS FAILED - see doctest output above for exactly which " -ForegroundColor Red -NoNewline
    Write-Host "test, what was expected, and what was actually returned." -ForegroundColor Red
}
exit $testExitCode
