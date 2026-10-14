# src/ - layered source layout (introduced v0.10.0)

| Folder | Holds | May depend on |
|---|---|---|
| `core/` | Pure logic, no Windows calls: parsing, aggregation, formatting, business rules, control limits | nothing |
| `data/` | Anything that touches disk: file I/O, `.fbd` save/load, settings, recent files, supplier emails, autosave, backups, history | core |
| `services/` | Work done with the data: report rendering/printing, CSV export, email building, Finalize Day, entry operations | core, data |
| `forms/` | All windows and controls: main window + tabs, layout, refresh, popups, window procedures, entry point | everything below |

**Rule: dependencies only point downward.** `core` never knows about a window; `data` never calls
`MessageBox`; `services` never touches a control. That is what lets the UI layer be replaced (e.g. by
WinUI 3) without rewriting the rest.

Status (v0.10.2): the split is staged (see ROADMAP.md). Right now `core/` and `data/` hold the existing headers and
`main.cpp` (still at the repo root) holds everything else; code moves into `services/` and `forms/` stage by stage.

`PlatformWin.h` is the single place that sets Windows macros and includes `<windows.h>`; include it (directly or via a
layer header) rather than `<windows.h>` itself.

Storage (`data/Storage`): exe-folder paths, settings, recent-files and supplier-email files, backup pruning.

Services (`services/`): `CsvExport` (CSV report), `Email` (supplier price email via mailto:). `ReportRender` (Breakdown report to bitmaps, shared by Print Preview and printing).

Popups (`forms/ManageNamesWindow`, `forms/PrintPreviewWindow`): the Manage Names and Print Preview windows. `forms/MainWindowApi.h` lists what the main window (still in `main.cpp`) provides to them.

Main window lists (`forms/MainWindowLists`, v0.10.8): the entry list, Overview / By Species / Breakdown lists, Supplier/Species combo refresh + autocomplete, and `RecalcTotals` (the balance badge). Layout, control creation and the window procedure are still in `main.cpp` (stage 6b-6d).

`forms/PopupPlacement.h` keeps popup windows inside the monitor work area (`FitPopupToWorkArea`).

Global state: document/persistence state is in `data/AppData`, window handles and UI state are in `forms/UiState`.

New `.cpp` files must be added to the source list in all four build paths: `build_msvc.bat`,
`build_mingw.bat`, `CMakeLists.txt`, and `tests/win32_integration/run_integration_tests.ps1`.
