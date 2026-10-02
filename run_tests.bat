@echo off
:: Unblocks every file in this project folder (removes the "downloaded
:: from the internet" mark Windows adds to files extracted from a zip -
:: this is separate from execution policy and blocks scripts even under
:: RemoteSigned), then runs the doctest suite.
::
:: This does NOT change your execution policy - it bypasses it for just
:: this one run instead, so there's nothing to set up front and nothing
:: to remember afterward. Safe to run every time you get a new zip.
::
:: Needs cl.exe on PATH - run this from a "Developer Command Prompt for
:: VS" or "Developer PowerShell for VS" (Start Menu > Visual Studio).
::
:: Usage: run (or double-click) this file from anywhere - it finds its
:: own folder automatically, so it doesn't matter where the project
:: actually lives on disk.

:: v0.9.51: now runs BOTH the portable unit test suite (tests\run_tests.ps1)
:: and the Windows integration test suite
:: (tests\win32_integration\run_integration_tests.ps1) from this one command,
:: per Jack's request ("Update run_tests.bat so one command builds and runs
:: both portable and Windows integration suites"). Each suite builds and
:: runs independently; this script reports failure (and a non-zero exit
:: code) if EITHER one fails, after both have had a chance to run, so a
:: portable-suite failure doesn't hide an integration-suite result or vice
:: versa.

powershell -NoProfile -Command "Get-ChildItem -Path '%~dp0' -Recurse | Unblock-File"
if errorlevel 1 (
    echo.
    echo Unblock step failed - see the error above.
    exit /b 1
)

echo.
echo ===== Portable unit tests =====
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tests\run_tests.ps1"
set PORTABLE_EXIT=%ERRORLEVEL%

echo.
echo ===== Windows integration tests =====
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tests\win32_integration\run_integration_tests.ps1"
set INTEGRATION_EXIT=%ERRORLEVEL%

echo.
if %PORTABLE_EXIT% NEQ 0 (
    echo Portable unit tests: FAILED
) else (
    echo Portable unit tests: PASSED
)
if %INTEGRATION_EXIT% NEQ 0 (
    echo Windows integration tests: FAILED
) else (
    echo Windows integration tests: PASSED
)

if %PORTABLE_EXIT% NEQ 0 exit /b %PORTABLE_EXIT%
if %INTEGRATION_EXIT% NEQ 0 exit /b %INTEGRATION_EXIT%
exit /b 0
