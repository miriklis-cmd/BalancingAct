// This file's only job is to give doctest a main() for the Windows
// integration test executable - same pattern as tests/test_main.cpp for the
// portable suite. The actual tests live in:
//   - test_io_integration.cpp / test_save_fault_injection.cpp (this folder,
//     Phase 1 items 3/4 - exercise FishBalanceWin32IO.h directly, no GUI code)
//   - main.cpp itself, inside its #ifdef FBM_BUILDING_TESTS block (Phase 1
//     items 5/6 - need main.cpp's own internal-linkage globals and real
//     window-creation code, which a separate translation unit can't reach)
// run_integration_tests.ps1 compiles main.cpp (with -DFBM_BUILDING_TESTS,
// so it contributes no WinMain) together with this file and the two test_*
// files above into one console executable; doctest collects every TEST_CASE
// across all of them automatically at link time.
#include "../doctest_setup.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "../doctest.h"
