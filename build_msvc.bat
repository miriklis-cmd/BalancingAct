@echo off
REM Run this from a "Developer Command Prompt for VS" so cl.exe and rc.exe are on PATH.
REM The manifest is shipped as an external side-by-side file (no resource
REM compiler needed for that part); the app icon needs a small resource
REM script (app_icon.rc) compiled and linked in.

rc /nologo app_icon.rc
if errorlevel 1 goto :error

cl /nologo /EHsc /O2 /std:c++17 /DUNICODE /D_UNICODE main.cpp app_icon.res ^
   user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib winspool.lib /Fe:FishBalanceManager.exe ^
   /link /MANIFEST:NO
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
