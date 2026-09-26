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

powershell -NoProfile -Command "Get-ChildItem -Path '%~dp0' -Recurse | Unblock-File"
if errorlevel 1 (
    echo.
    echo Unblock step failed - see the error above.
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tests\run_tests.ps1"
