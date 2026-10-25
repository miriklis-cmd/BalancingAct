@echo off
REM Builds FishBalanceManager.exe using MinGW-w64 (g++ and windres must be on PATH).
REM The manifest is shipped as an external side-by-side file (no resource
REM compiler needed for that part); the app icon needs a small resource
REM script (app_icon.rc) compiled and linked in.

REM v0.10.0: every .cpp of the app is listed HERE (and in build_msvc.bat, CMakeLists.txt
REM and tests\win32_integration\run_integration_tests.ps1) - add new source files to all four.
set SOURCES=main.cpp src/data/AppData.cpp src/data/Storage.cpp src/data/Backups.cpp src/services/CsvExport.cpp src/services/Email.cpp src/services/ReportRender.cpp src/forms/UiState.cpp src/forms/MainWindowLayout.cpp src/forms/MainWindowLists.cpp src/forms/ManageNamesWindow.cpp src/forms/PrintPreviewWindow.cpp src/forms/DrillDownWindow.cpp

windres app_icon.rc -O coff -o app_icon_res.o
if errorlevel 1 goto :error

REM Phase 1 (2026-09-30): -Wall -Wextra -Wpedantic -Werror now matches
REM CMakeLists.txt's warning gate (see AuditFindings_2026-09-30.md,
REM finding F29) - previously this script passed no warning flags at all.
g++ -O2 -municode -mwindows -std=c++17 -Wall -Wextra -Wpedantic -Werror -o FishBalanceManager.exe %SOURCES% app_icon_res.o -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lwinspool
if errorlevel 1 goto :error

copy /Y app.manifest FishBalanceManager.exe.manifest >nul
REM app.ico already sits next to the exe in this folder, so no copy is
REM needed for LoadAppIcon()'s file-based fallback.

echo.
echo Build succeeded: FishBalanceManager.exe
echo Built from %CD% :
findstr /C:"#define APP_VERSION" version.h
goto :eof

:error
echo.
echo Build FAILED.
