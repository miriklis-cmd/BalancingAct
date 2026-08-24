cmake_minimum_required(VERSION 3.15)
project(FishBalanceManager LANGUAGES CXX)
enable_language(RC)

add_executable(FishBalanceManager WIN32 main.cpp app_icon.rc)
target_link_libraries(FishBalanceManager PRIVATE comctl32 comdlg32 shell32 winspool)

if (MSVC)
    target_compile_options(FishBalanceManager PRIVATE /W4)
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
