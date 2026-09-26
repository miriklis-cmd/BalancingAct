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

if (MSVC)
    target_compile_options(FishBalanceManager PRIVATE /W4 /EHsc)
    # We ship app.manifest as an external side-by-side manifest instead of an
    # embedded resource (avoids the RC/CVTRES toolchain entirely). An embedded
    # manifest would take priority over the external one, so make sure MSVC
    # doesn't auto-generate/embed its own.
    target_link_options(FishBalanceManager PRIVATE /MANIFEST:NO)
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
)
if (MSVC)
    target_compile_options(FishBalanceTests PRIVATE /W4 /EHsc)
endif()
add_test(NAME FishBalanceCoreTests COMMAND FishBalanceTests)
