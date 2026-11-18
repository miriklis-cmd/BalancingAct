# Fish Balance Manager - WinUI 3 prototype

A WinUI 3 (C++/WinRT) front end that shares its logic with the Win32 app. It reads and writes the same
`.fbd` files and the same data folder (`C:\BalancingAct\data`), so the two apps can be used side by side.

## Layout

    repo root
      src\                       shared code (used by BOTH apps) - never copy it; the WinUI project includes it directly
      winui\
        FishBalanceWinUI.sln
        FishBalanceWinUI\        the WinUI project (XAML, MainWindow.xaml.cpp/.h, .idl, manifests, assets)
        packages\                NuGet packages - created by restore, NOT in Git

The project finds the shared code through `$(ProjectDir)..\..` (the repo root), so it builds from any clone.

## Build

1. Visual Studio 2022 or later with the "Desktop development with C++" and "Windows App SDK" workloads.
2. Open `winui\FishBalanceWinUI.sln`.
3. Right-click the solution > Restore NuGet Packages (first time only).
4. Choose Debug | x64 and press F5.

If Visual Studio complains that `FishBalanceWinUI_TemporaryKey.pfx` is missing: open `Package.appxmanifest` >
Packaging > Choose Certificate > Create..., or copy the key from your old working folder. It is deliberately not in Git.

## Project rules

- Shared code lives in `..\..\src`. `MainWindow.xaml.cpp` pulls in several shared `.cpp` files with `#include`
  (unity style). `src\services\GmailDraft.cpp` is the exception: it is a project item with Precompiled Header
  set to "Not Using", because it must include `winsock2.h` before `windows.h`.
- New methods or event handlers need the declaration in `MainWindow.xaml.h` as well as the definition in
  `MainWindow.xaml.cpp`.
- Never commit `names.txt`, `*.fbd`, `client_secret*.json` or anything from the data folder.
