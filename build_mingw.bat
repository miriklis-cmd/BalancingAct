@echo off
REM Builds FishBalanceManager.exe using MinGW-w64 (g++ and windres must be on PATH).
REM The manifest is shipped as an external side-by-side file (no resource
REM compiler needed for that part); the app icon needs a small resource
REM script (app_icon.rc) compiled and linked in.

windres app_icon.rc -O coff -o app_icon_res.o
if errorlevel 1 goto :error

REM Phase 1 (2026-09-30): -Wall -Wextra -Wpedantic -Werror now matches
REM CMakeLists.txt's warning gate (see AuditFindings_2026-09-30.md,
REM finding F29) - previously this script passed no warning flags at all.
g++ -O2 -municode -mwindows -std=c++17 -Wall -Wextra -Wpedantic -Werror -o FishBalanceManager.exe main.cpp app_icon_res.o -lcomctl32 -lcomdlg32 -lshell32 -lwinspool
if errorlevel 1 goto :error

copy /Y app.manifest FishBalanceManager.exe.manifest >nul
REM app.ico already sits next to the exe in this folder, so no copy is
REM needed for LoadAppIcon()'s file-based fallback.

echo.
echo Build succeeded: FishBalanceManager.exe
goto :eof

:error
echo.
echo Build FAILED.
