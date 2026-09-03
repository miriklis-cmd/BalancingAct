// This file's only job is to give doctest a main(). All the actual tests
// live in the other test_*.cpp files in this folder - doctest collects
// them automatically at link time via its TEST_CASE macro, no registration
// needed here.
#include "doctest_setup.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
