#include "doctest_setup.h"
#include "doctest.h"
#include "../src/core/NameMaster.h"

static NameList MakeList(std::initializer_list<const wchar_t*> names) {
    NameList l;
    for (const wchar_t* n : names) NameListAdd(l, n, L"2026-01-01");
    return l;
}

TEST_SUITE("Name master list") {

TEST_CASE("CollapseNameSpaces trims and collapses") {
    CHECK(CollapseNameSpaces(L"  Port   Fish \t Co  ") == L"Port Fish Co");
    CHECK(CollapseNameSpaces(L"") == L"");
    CHECK(CollapseNameSpaces(L"   ") == L"");
}

TEST_CASE("NameKey ignores case, spaces and punctuation") {
    CHECK(NameKey(L"Port Fish Co.") == NameKey(L"port fish co"));
    CHECK(NameKey(L"Flathead (Sand)") == L"flatheadsand");
    CHECK(NameKey(L"...") == L"");
}

TEST_CASE("NameEditDistance counts a swap as one slip") {
    CHECK(NameEditDistance(L"snapper", L"snapper") == 0);
    CHECK(NameEditDistance(L"snaper", L"snapper") == 1);
    CHECK(NameEditDistance(L"snpaper", L"snapper") == 1);
    CHECK(NameEditDistance(L"abc", L"xyz") == 3);
}

TEST_CASE("ResolveName: exact, snapped and alias matches are silent") {
    NameList l = MakeList({ L"Barramundi", L"Snapper" });
    NameListMerge(l, { L"Barra", L"Barramundi" }, L"Barramundi", L"2026-01-02");

    NameResolution a = ResolveName(l, L"Snapper");
    CHECK(a.kind == NameMatch::Exact);

    NameResolution b = ResolveName(l, L"  snapper ");
    CHECK(b.kind == NameMatch::Snapped);
    CHECK(b.name == L"Snapper");

    NameResolution c = ResolveName(l, L"BARRA");
    CHECK(c.kind == NameMatch::Alias);
    CHECK(c.name == L"Barramundi");
}

TEST_CASE("ResolveName: Near for prefixes and small typos, New otherwise") {
    NameList l = MakeList({ L"Barramundi", L"Snapper", L"Gummy Shark" });

    NameResolution a = ResolveName(l, L"Barra");
    CHECK(a.kind == NameMatch::Near);
    REQUIRE(!a.similar.empty());
    CHECK(a.similar[0] == L"Barramundi");

    NameResolution b = ResolveName(l, L"Snpaer");
    CHECK(b.kind == NameMatch::Near);
    REQUIRE(!b.similar.empty());
    CHECK(b.similar[0] == L"Snapper");

    CHECK(ResolveName(l, L"Pomfret").kind == NameMatch::New);
    CHECK(ResolveName(l, L"Ba").kind == NameMatch::New);      // too short to compare
    CHECK(ResolveName(l, L"Cod").kind == NameMatch::New);
}

TEST_CASE("ResolveName: the start of an alias is a Near match for what it means") {
    NameList l = MakeList({ L"Black Flathead", L"Snapper" });
    NameListMerge(l, { L"Blk Flathead" }, L"Black Flathead", L"d");
    NameResolution r = ResolveName(l, L"Blk flat");
    CHECK(r.kind == NameMatch::Near);
    REQUIRE(r.similar.size() == 1);
    CHECK(r.similar[0] == L"Black Flathead");
    REQUIRE(r.similarVia.size() == 1);
    CHECK(r.similarVia[0] == L"Blk Flathead");
}

TEST_CASE("Near lists each name once even if the name and an alias both match") {
    NameList l = MakeList({ L"Black Flathead" });
    NameListMerge(l, { L"Black Flat" }, L"Black Flathead", L"d");
    NameResolution r = ResolveName(l, L"Black Fla");
    CHECK(r.kind == NameMatch::Near);
    CHECK(r.similar.size() == 1);
}

TEST_CASE("FindAliasStartingWith completes from aliases only, ignoring case") {
    NameList l = MakeList({ L"Black Flathead", L"Gummy Shark" });
    NameListMerge(l, { L"Blk Flathead" }, L"Black Flathead", L"d");
    NameListMerge(l, { L"Blk Flat" }, L"Black Flathead", L"d");
    CHECK(FindAliasStartingWith(l, L"blk fl") == L"Blk Flat");     // alphabetical: "blk flat" < "blk flathead"
    CHECK(FindAliasStartingWith(l, L"Blk Flath") == L"Blk Flathead");
    CHECK(FindAliasStartingWith(l, L"Blk Flathead").empty());        // nothing longer to complete to
    CHECK(FindAliasStartingWith(l, L"Gum").empty());                 // that is a name, not an alias
    CHECK(FindAliasStartingWith(l, L"").empty());
}

TEST_CASE("NameListAddAlias / RemoveAlias / AliasesOf") {
    NameList l = MakeList({ L"Black Flathead", L"Snapper" });
    CHECK(NameListAddAlias(l, L"  Blk   Flat ", L"Black Flathead") == AliasAddResult::Added);
    CHECK(NameListAddAlias(l, L"Dusky Flathead", L"Black Flathead") == AliasAddResult::Added);
    auto a = AliasesOf(l, L"Black Flathead");
    REQUIRE(a.size() == 2);
    CHECK(a[0] == L"Blk Flat");
    CHECK(AliasesOf(l, L"Snapper").empty());
    CHECK(ResolveName(l, L"blk flat").kind == NameMatch::Alias);
    CHECK(NameListAddAlias(l, L"", L"Snapper") == AliasAddResult::Empty);
    CHECK(NameListAddAlias(l, L"black-flathead", L"Black Flathead") == AliasAddResult::SameAsName);
    CHECK(NameListAddAlias(l, L"snapper", L"Black Flathead") == AliasAddResult::IsAName);
    CHECK(NameListAddAlias(l, L"X", L"Missing") == AliasAddResult::NoSuchName);
    // re-pointing replaces rather than duplicating
    CHECK(NameListAddAlias(l, L"BLK FLAT", L"Snapper") == AliasAddResult::Added);
    CHECK(l.aliases.size() == 2);
    CHECK(AliasesOf(l, L"Snapper").size() == 1);
    CHECK(NameListRemoveAlias(l, L"blk flat"));
    CHECK_FALSE(NameListRemoveAlias(l, L"blk flat"));
    CHECK_FALSE(NameListRemoveAlias(l, L""));
}

TEST_CASE("ResolveName on an empty list is always New") {
    NameList l;
    NameResolution r = ResolveName(l, L"Anything");
    CHECK(r.kind == NameMatch::New);
    CHECK(r.name == L"Anything");
}

TEST_CASE("Near list is capped at 3 and ordered closest first") {
    NameList l = MakeList({ L"Whiting King George", L"Whiting", L"Whitings", L"Whiting Sand", L"Whiting School" });
    NameResolution r = ResolveName(l, L"Whiti");
    CHECK(r.kind == NameMatch::Near);
    CHECK(r.similar.size() == 3);
    CHECK(r.similar[0] == L"Whiting");
}

TEST_CASE("NameListMerge removes sources, keeps target, records aliases") {
    NameList l = MakeList({ L"Port Fish Co.", L"port fish co", L"Port Fish Co" });
    NameListMerge(l, { L"Port Fish Co.", L"port fish co", L"Port Fish Co" }, L"Port Fish Co.", L"2026-02-01");
    CHECK(l.names.size() == 1);
    CHECK(l.names[0] == L"Port Fish Co.");
    CHECK(l.aliases.empty()); // all differ only by case/punctuation, so snapping covers them
}

TEST_CASE("NameListMerge adds a missing target and aliases real variants") {
    NameList l = MakeList({ L"Barra", L"Bass" });
    NameListMerge(l, { L"Barra" }, L"Barramundi", L"2026-02-01");
    CHECK(NameListHas(l, L"Barramundi"));
    CHECK_FALSE(NameListHas(l, L"Barra"));
    CHECK(l.aliases[L"Barra"] == L"Barramundi");
}

TEST_CASE("merging again re-points existing aliases") {
    NameList l = MakeList({ L"Barra", L"Barramundi", L"Barramundi Fillet" });
    NameListMerge(l, { L"Barra" }, L"Barramundi", L"d");
    NameListMerge(l, { L"Barramundi" }, L"Barramundi Fillet", L"d");
    CHECK(l.aliases[L"Barra"] == L"Barramundi Fillet");
    CHECK(l.aliases[L"Barramundi"] == L"Barramundi Fillet");
}

TEST_CASE("adding a name on purpose cancels an alias for it") {
    NameList l = MakeList({ L"Barramundi" });
    NameListMerge(l, { L"Barra" }, L"Barramundi", L"d");
    CHECK(ResolveName(l, L"Barra").kind == NameMatch::Alias);
    NameListAdd(l, L"Barra", L"d");
    CHECK(ResolveName(l, L"Barra").kind == NameMatch::Exact);
    CHECK(l.aliases.empty());
}

TEST_CASE("NameListRemove drops the name and aliases pointing at it") {
    NameList l = MakeList({ L"Barramundi" });
    NameListMerge(l, { L"Barra" }, L"Barramundi", L"d");
    NameListRemove(l, L"Barramundi");
    CHECK(l.names.empty());
    CHECK(l.aliases.empty());
}

TEST_CASE("FindDuplicateNameGroups groups names that share a key") {
    auto g = FindDuplicateNameGroups({ L"port fish co", L"Eden Prawns", L"Port Fish Co", L"Port Fish Co." });
    REQUIRE(g.size() == 1);
    CHECK(g[0].size() == 3);
    CHECK(FindDuplicateNameGroups({ L"A", L"B" }).empty());
}

TEST_CASE("NameAddedRecently") {
    CHECK(NameAddedRecently(L"2026-11-06", L"2026-11-06", 7));
    CHECK(NameAddedRecently(L"2026-11-01", L"2026-11-06", 7));
    CHECK_FALSE(NameAddedRecently(L"2026-10-29", L"2026-11-06", 7));
    CHECK_FALSE(NameAddedRecently(L"2026-11-07", L"2026-11-06", 7)); // future
    CHECK_FALSE(NameAddedRecently(L"", L"2026-11-06", 7));
    CHECK(NameAddedRecently(L"2026-12-30", L"2027-01-02", 7));       // across a year boundary
}

TEST_CASE("names.txt lines round-trip") {
    NameMasterData d;
    NameListAdd(d.suppliers, L"Port Fish Co.", L"2026-11-01");
    NameListAdd(d.species, L"Barramundi", L"2026-11-02");
    NameListMerge(d.species, { L"Barra" }, L"Barramundi", L"2026-11-02");
    auto lines = BuildNameMasterLines(d);

    NameMasterData e;
    ParseNameMasterLines(lines, e);
    CHECK(e.suppliers.names == d.suppliers.names);
    CHECK(e.species.names == d.species.names);
    CHECK(e.species.aliases == d.species.aliases);
    CHECK(e.suppliers.added[L"Port Fish Co."] == L"2026-11-01");
}

TEST_CASE("ParseNameMasterLines skips bad lines and dangling aliases") {
    NameMasterData e;
    ParseNameMasterLines({ L"garbage", L"X|what|ever", L"S||2026-01-01", L"S|Good|2026-01-01", L"AS|Old|Missing",
                           L"P|Snapper", L"AP|Snap|Snapper" }, e);
    CHECK(e.suppliers.names.size() == 1);
    CHECK(e.suppliers.aliases.empty());   // "Missing" is not in the list
    CHECK(e.species.names.size() == 1);   // a line without a date still loads
    CHECK(e.species.aliases[L"Snap"] == L"Snapper");
}

}
