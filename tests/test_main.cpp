// This file's only job is to give doctest a main(). All the actual tests
// live in the other test_*.cpp files in this folder - doctest collects
// them automatically at link time via its TEST_CASE macro, no registration
// needed here.
#include "doctest_setup.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../version.h"

// v0.10.1: print which version/source this test executable was actually
// compiled from, before doctest's own output (so a stale folder or stale
// build can't pass unnoticed). Runs as a static initializer, before main().
#include <cstdio>
namespace {
struct PrintBuildStamp {
    PrintBuildStamp() {
        std::printf("Fish Balance Manager TEST BUILD - version %ls\n  compiled from: %s\n\n",
                    APP_VERSION, __FILE__);
    }
} g_printBuildStamp;
}
