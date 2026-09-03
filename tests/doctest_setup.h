// Included before "doctest.h" in every test_*.cpp file in this folder.
//
// By default doctest avoids #include-ing the real <tuple>/<ostream>/etc.
// standard headers (for faster compiles) by forward-declaring pieces of
// namespace std itself instead - including `template <class...> class
// tuple;`. That's technically not permitted by the C++ standard (you can't
// declare your own things in namespace std), which doctest's authors
// already knew about for older MSVC warnings, but a newer MSVC diagnostic
// (C5285: "cannot declare a specialization for 'std::tuple' ... forbidden
// by [tuple.tuple.general]") flags it too, and isn't in doctest's existing
// suppression list.
//
// Defining this macro switches doctest to the alternative, fully
// standards-compliant path it already ships: just #include the real
// standard headers instead of forward-declaring parts of std itself. This
// is the actual fix (removes the problem at its root) rather than a
// suppression of the warning doctest happened to trigger.
#pragma once
#define DOCTEST_CONFIG_USE_STD_HEADERS
