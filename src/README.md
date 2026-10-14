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

Status: the split is staged (see ROADMAP.md). Right now `core/` and `data/` hold the existing headers and
`main.cpp` (still at the repo root) holds everything else; code moves into `services/` and `forms/` stage by stage.

New `.cpp` files must be added to the source list in all four build paths: `build_msvc.bat`,
`build_mingw.bat`, `CMakeLists.txt`, and `tests/win32_integration/run_integration_tests.ps1`.
