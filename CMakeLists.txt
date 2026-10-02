cmake_minimum_required(VERSION 3.15)
project(FishBalanceManager LANGUAGES CXX)
enable_language(RC)

# Explicitly require C++17, matching build_msvc.bat's /std:c++17 and
# build_mingw.bat's -std=c++17. Without this, MSVC's CMake/Ninja path
# silently falls back to an older default standard (unlike the two
# direct-invocation .bat scripts) - a pre-existing gap between the three
# build paths that only surfaced once FishBalanceCore.h needed a genuine
# C++17-only feature (v0.9.19's ComputeOutlierRange). Fixed here at the
# root rather than just avoiding C++17 features going forward, so this
# doesn't quietly resurface the next time a newer construct is used.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(FishBalanceManager WIN32 main.cpp app_icon.rc)
target_link_libraries(FishBalanceManager PRIVATE comctl32 comdlg32 shell32 winspool)

# Phase 1 (2026-09-30) warning-gate tightening: /W4 /WX for MSVC,
# -Wall -Wextra -Wpedantic -Werror for MinGW/GCC, applied to BOTH the app
# and test targets below - previously only MSVC's /W4 (no /WX) was wired
# in here, and the two .bat scripts used no warning flags at all (see
# AuditFindings_2026-09-30.md, finding F29). A warning is now a build
# failure rather than something easy to miss in scrollback.
if (MSVC)
    target_compile_options(FishBalanceManager PRIVATE /W4 /WX /EHsc)
    # We ship app.manifest as an external side-by-side manifest instead of an
    # embedded resource (avoids the RC/CVTRES toolchain entirely). An embedded
    # manifest would take priority over the external one, so make sure MSVC
    # doesn't auto-generate/embed its own.
    target_link_options(FishBalanceManager PRIVATE /MANIFEST:NO)
else()
    target_compile_options(FishBalanceManager PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Copy app.manifest next to the built exe as "<exe-name>.exe.manifest" — Windows
# picks this up automatically as a side-by-side manifest (needed for ListView
# groups / Common Controls v6) with no resource-compiler step required.
add_custom_command(TARGET FishBalanceManager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/app.manifest
        $<TARGET_FILE:FishBalanceManager>.manifest
)

# Also copy app.ico next to the exe. It's normally embedded via app_icon.rc,
# but this gives LoadAppIcon() in main.cpp a file-based fallback if the
# resource ever isn't embedded for some reason.
add_custom_command(TARGET FishBalanceManager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/app.ico
        $<TARGET_FILE_DIR:FishBalanceManager>/app.ico
)

# --- Automated test suite ---
# Exercises only the platform-independent logic in FishBalanceCore.h
# (parsing, formatting, aggregation, CSV/email string-building) - a small,
# separate console executable with no dependency on the Win32 GUI libs the
# main app needs, and no window is ever created. tests/run_tests.ps1 is the
# quick everyday way to build+run this; the target below wires the same
# suite into CTest so `ctest` (or Visual Studio's Test Explorer, if
# configured) can run it too.
enable_testing()
add_executable(FishBalanceTests
    tests/test_main.cpp
    tests/test_parsing.cpp
    tests/test_formatting.cpp
    tests/test_fbd_loader.cpp
    tests/test_aggregation.cpp
    tests/test_csv_export.cpp
    tests/test_email.cpp
    tests/test_recovery_and_finalize.cpp
)
if (MSVC)
    target_compile_options(FishBalanceTests PRIVATE /W4 /WX /EHsc)
else()
    target_compile_options(FishBalanceTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME FishBalanceCoreTests COMMAND FishBalanceTests)

# --- Windows integration test suite (Phase 1 test-automation refactor, v0.9.51) ---
# Compiles main.cpp itself with FBM_BUILDING_TESTS defined (removing its
# WinMain and enabling its own #ifdef FBM_BUILDING_TESTS test block - the
# control-limit test and headless end-to-end smoke test, Phase 1 items 5/6,
# which need main.cpp's own internal-linkage globals and real window-creation
# code) alongside the file-I/O and Save/Save As fault-injection tests (items
# 3/4, which need none of that - see tests/win32_integration/*.cpp). A
# separate executable from FishBalanceManager/FishBalanceTests above, since
# it genuinely needs main.cpp's Win32 GUI dependencies (comctl32 etc.) that
# the portable suite deliberately has none of. See
# tests/win32_integration/run_integration_tests.ps1 for the everyday way to
# build+run just this suite with the same /W4 /WX gate.
add_executable(FishBalanceIntegrationTests
    main.cpp
    tests/win32_integration/test_io_integration.cpp
    tests/win32_integration/test_save_fault_injection.cpp
    tests/win32_integration/test_integration_main.cpp
)
target_compile_definitions(FishBalanceIntegrationTests PRIVATE FBM_BUILDING_TESTS)
target_link_libraries(FishBalanceIntegrationTests PRIVATE comctl32 comdlg32 shell32 winspool)
if (MSVC)
    target_compile_options(FishBalanceIntegrationTests PRIVATE /W4 /WX /EHsc)
    target_link_options(FishBalanceIntegrationTests PRIVATE /MANIFEST:NO)
else()
    target_compile_options(FishBalanceIntegrationTests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME FishBalanceIntegrationTests COMMAND FishBalanceIntegrationTests)
