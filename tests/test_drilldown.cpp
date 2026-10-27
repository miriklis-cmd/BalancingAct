// test_drilldown.cpp - Breakdown drill-down: BuildBreakdownRowRefs, ComputeDrillDown (v0.12.6) and
// SortDrillDownIndices (v0.12.9, click-a-header sorting).
#include "doctest_setup.h"
#include "doctest.h"
#include "../src/core/FishBalanceCore.h"

namespace {
Entry MakeEntry(const wchar_t* sup, const wchar_t* sp, double kgs, double price, const wchar_t* date) {
    Entry e;
    e.supplier = sup;
    e.product = sp;
    e.kgs = kgs;
    e.price = price;
    e.date = date;
    return e;
}

// Two suppliers, both sell Flathead; Imlay has two price lines for it, entered out of date order.
std::vector<Entry> Fixture() {
    return {
        MakeEntry(L"Imlay", L"Flathead", 10.0, 12.50, L"2026-10-21"),
        MakeEntry(L"Imlay", L"Bream",     5.0,  8.00, L"2026-10-20"),
        MakeEntry(L"Jenkins", L"Flathead", 7.0, 12.50, L"2026-10-20"),
        MakeEntry(L"Imlay", L"Flathead",   4.5, 14.00, L"2026-10-19"),
        MakeEntry(L"Imlay", L"Flathead",   3.0, 12.50, L"2026-10-19"),
    };
}
}

TEST_CASE("BuildBreakdownRowRefs lists rows in the order the Breakdown list inserts them") {
    auto entries = Fixture();
    auto data = BuildBreakdownData(entries);
    auto refs = BuildBreakdownRowRefs(data);

    // Imlay: Bream 8.00, Bream Total, Flathead 12.50, Flathead 14.00, Flathead Total, Imlay Total
    // Jenkins: Flathead 12.50, Flathead Total, Jenkins Total  => 9 rows
    REQUIRE(refs.size() == 9);
    CHECK(refs[0].kind == BreakdownRowKind::Price);
    CHECK(refs[0].supplier == L"Imlay");
    CHECK(refs[0].species == L"Bream");
    CHECK(refs[0].price == 8.00);
    CHECK(refs[1].kind == BreakdownRowKind::SpeciesTotal);
    CHECK(refs[1].species == L"Bream");
    CHECK(refs[2].price == 12.50);
    CHECK(refs[3].price == 14.00);
    CHECK(refs[4].kind == BreakdownRowKind::SpeciesTotal);
    CHECK(refs[4].species == L"Flathead");
    CHECK(refs[5].kind == BreakdownRowKind::SupplierTotal);
    CHECK(refs[5].supplier == L"Imlay");
    CHECK(refs[5].species.empty());
    CHECK(refs[6].supplier == L"Jenkins");
    CHECK(refs[8].kind == BreakdownRowKind::SupplierTotal);
    CHECK(refs[8].supplier == L"Jenkins");
}

TEST_CASE("Price row drills into only that supplier's entries of that species at exactly that price") {
    auto entries = Fixture();
    BreakdownRowRef ref;
    ref.kind = BreakdownRowKind::Price;
    ref.supplier = L"Imlay";
    ref.species = L"Flathead";
    ref.price = 12.50;
    DrillDownResult r = ComputeDrillDown(entries, ref);
    REQUIRE(r.indices.size() == 2); // not Jenkins's 12.50, not Imlay's 14.00
    // ordered by date: 19th (index 4) before 21st (index 0)
    CHECK(r.indices[0] == 4);
    CHECK(r.indices[1] == 0);
    CHECK(r.kgs == doctest::Approx(13.0));
    CHECK(r.amt == doctest::Approx(13.0 * 12.50));
}

TEST_CASE("Species total row drills into all of that supplier's entries of the species") {
    auto entries = Fixture();
    BreakdownRowRef ref;
    ref.kind = BreakdownRowKind::SpeciesTotal;
    ref.supplier = L"Imlay";
    ref.species = L"Flathead";
    DrillDownResult r = ComputeDrillDown(entries, ref);
    REQUIRE(r.indices.size() == 3);
    CHECK(entries[r.indices[0]].date == L"2026-10-19");
    CHECK(entries[r.indices[2]].date == L"2026-10-21");
    CHECK(r.kgs == doctest::Approx(17.5));
    CHECK(r.amt == doctest::Approx(10.0 * 12.50 + 4.5 * 14.00 + 3.0 * 12.50));
}

TEST_CASE("Same-date entries keep their entry order") {
    std::vector<Entry> entries = {
        MakeEntry(L"A", L"Bream", 1.0, 5.0, L"2026-10-20"),
        MakeEntry(L"A", L"Bream", 2.0, 5.0, L"2026-10-20"),
        MakeEntry(L"A", L"Bream", 3.0, 5.0, L"2026-10-20"),
    };
    BreakdownRowRef ref;
    ref.kind = BreakdownRowKind::SpeciesTotal;
    ref.supplier = L"A";
    ref.species = L"Bream";
    DrillDownResult r = ComputeDrillDown(entries, ref);
    REQUIRE(r.indices.size() == 3);
    CHECK(r.indices[0] == 0);
    CHECK(r.indices[1] == 1);
    CHECK(r.indices[2] == 2);
}

TEST_CASE("Supplier total row drills into every entry of the supplier, grouped by species then date") {
    auto entries = Fixture();
    BreakdownRowRef ref;
    ref.kind = BreakdownRowKind::SupplierTotal;
    ref.supplier = L"Imlay";
    DrillDownResult r = ComputeDrillDown(entries, ref);
    REQUIRE(r.indices.size() == 4);
    CHECK(entries[r.indices[0]].product == L"Bream");
    CHECK(entries[r.indices[1]].product == L"Flathead");
    CHECK(r.kgs == doctest::Approx(22.5));
}

TEST_CASE("Every Breakdown row's drill-down total matches the row's own Kgs and Total") {
    auto entries = Fixture();
    // an awkward price that is not exactly representable, and a repeated one
    entries.push_back(MakeEntry(L"Imlay", L"Bream", 2.3, 8.35, L"2026-10-22"));
    entries.push_back(MakeEntry(L"Imlay", L"Bream", 1.1, 8.35, L"2026-10-18"));
    entries.push_back(MakeEntry(L"Jenkins", L"Snapper", 0.7, 31.9, L"2026-10-22"));
    auto data = BuildBreakdownData(entries);
    auto refs = BuildBreakdownRowRefs(data);

    size_t row = 0;
    for (const auto& sg : data) {
        for (const auto& pg : sg.products) {
            for (const auto& pl : pg.prices) {
                REQUIRE(row < refs.size());
                DrillDownResult r = ComputeDrillDown(entries, refs[row]);
                CHECK(r.kgs == doctest::Approx(pl.kgs));
                CHECK(r.amt == doctest::Approx(pl.amt));
                CHECK(FormatMoney(r.amt) == FormatMoney(pl.amt));
                CHECK(FormatKg(r.kgs) == FormatKg(pl.kgs));
                row++;
            }
            REQUIRE(row < refs.size());
            DrillDownResult r = ComputeDrillDown(entries, refs[row]);
            CHECK(r.kgs == doctest::Approx(pg.totalKgs));
            CHECK(r.amt == doctest::Approx(pg.totalAmt));
            CHECK(FormatMoney(r.amt) == FormatMoney(pg.totalAmt));
            row++;
        }
        REQUIRE(row < refs.size());
        DrillDownResult r = ComputeDrillDown(entries, refs[row]);
        CHECK(r.kgs == doctest::Approx(sg.totalKgs));
        CHECK(r.amt == doctest::Approx(sg.totalAmt));
        CHECK(FormatMoney(r.amt) == FormatMoney(sg.totalAmt));
        row++;
    }
    CHECK(row == refs.size());
}

TEST_CASE("Drill-down with no matching entries is empty") {
    auto entries = Fixture();
    BreakdownRowRef ref;
    ref.kind = BreakdownRowKind::Price;
    ref.supplier = L"Imlay";
    ref.species = L"Flathead";
    ref.price = 99.0;
    DrillDownResult r = ComputeDrillDown(entries, ref);
    CHECK(r.indices.empty());
    CHECK(r.kgs == 0);
    CHECK(r.amt == 0);
    CHECK(BuildBreakdownRowRefs(BuildBreakdownData({})).empty());
}

namespace {
std::vector<double> KgsOf(const std::vector<Entry>& entries, const std::vector<size_t>& idx) {
    std::vector<double> v;
    for (size_t i : idx) v.push_back(entries[i].kgs);
    return v;
}
std::vector<double> PricesOf(const std::vector<Entry>& entries, const std::vector<size_t>& idx) {
    std::vector<double> v;
    for (size_t i : idx) v.push_back(entries[i].price);
    return v;
}
BreakdownRowRef ImlayFlatheadAllPrices() {
    BreakdownRowRef ref;
    ref.kind = BreakdownRowKind::SpeciesTotal;
    ref.supplier = L"Imlay";
    ref.species = L"Flathead";
    return ref;
}
}

TEST_CASE("SortDrillDownIndices sorts a species' entries by price, ascending and descending") {
    auto entries = Fixture();
    auto r = ComputeDrillDown(entries, ImlayFlatheadAllPrices()); // 3 entries: 12.50, 12.50, 14.00
    SortDrillDownIndices(entries, r.indices, DrillSortKey::Price, true);
    CHECK(PricesOf(entries, r.indices) == std::vector<double>{12.50, 12.50, 14.00});
    // equal prices keep the default (date) order: 10-19 (3.0 kg) before 10-21 (10.0 kg)
    CHECK(KgsOf(entries, r.indices) == std::vector<double>{3.0, 10.0, 4.5});
    SortDrillDownIndices(entries, r.indices, DrillSortKey::Price, false);
    CHECK(PricesOf(entries, r.indices) == std::vector<double>{14.00, 12.50, 12.50});
}

TEST_CASE("SortDrillDownIndices sorts by kgs and by total, numerically") {
    auto entries = Fixture();
    auto r = ComputeDrillDown(entries, ImlayFlatheadAllPrices());
    SortDrillDownIndices(entries, r.indices, DrillSortKey::Kgs, true);
    CHECK(KgsOf(entries, r.indices) == std::vector<double>{3.0, 4.5, 10.0}); // 10.0 after 4.5, not text order
    SortDrillDownIndices(entries, r.indices, DrillSortKey::Kgs, false);
    CHECK(KgsOf(entries, r.indices) == std::vector<double>{10.0, 4.5, 3.0});
    SortDrillDownIndices(entries, r.indices, DrillSortKey::Total, false); // 125.00, 63.00, 37.50
    CHECK(KgsOf(entries, r.indices) == std::vector<double>{10.0, 4.5, 3.0});
    SortDrillDownIndices(entries, r.indices, DrillSortKey::Total, true);
    CHECK(KgsOf(entries, r.indices) == std::vector<double>{3.0, 4.5, 10.0});
}

TEST_CASE("SortDrillDownIndices by date, supplier and species; Default leaves the order alone") {
    auto entries = Fixture();
    BreakdownRowRef all;
    all.kind = BreakdownRowKind::SupplierTotal;
    all.supplier = L"Imlay";
    auto r = ComputeDrillDown(entries, all); // Bream, then Flathead by date
    auto before = r.indices;
    SortDrillDownIndices(entries, r.indices, DrillSortKey::Default, false);
    CHECK(r.indices == before);
    SortDrillDownIndices(entries, r.indices, DrillSortKey::Date, true);
    for (size_t i = 1; i < r.indices.size(); i++)
        CHECK(entries[r.indices[i - 1]].date <= entries[r.indices[i]].date);
    SortDrillDownIndices(entries, r.indices, DrillSortKey::Species, false);
    CHECK(entries[r.indices.front()].product == L"Flathead");
    CHECK(entries[r.indices.back()].product == L"Bream");
    SortDrillDownIndices(entries, r.indices, DrillSortKey::Supplier, true); // all Imlay: nothing may move or vanish
    CHECK(r.indices.size() == before.size());
}

TEST_CASE("SortDrillDownIndices never changes which rows are listed, so the totals are unchanged") {
    auto entries = Fixture();
    auto data = BuildBreakdownData(entries);
    auto refs = BuildBreakdownRowRefs(data);
    const DrillSortKey keys[] = {DrillSortKey::Supplier, DrillSortKey::Species, DrillSortKey::Date,
                                 DrillSortKey::Kgs, DrillSortKey::Price, DrillSortKey::Total};
    for (const auto& ref : refs) {
        auto base = ComputeDrillDown(entries, ref);
        for (DrillSortKey k : keys) {
            for (bool asc : {true, false}) {
                auto idx = base.indices;
                SortDrillDownIndices(entries, idx, k, asc);
                auto a = idx, b = base.indices;
                std::sort(a.begin(), a.end());
                std::sort(b.begin(), b.end());
                CHECK(a == b);
            }
        }
    }
}

TEST_CASE("SortDrillDownIndices on an empty list is harmless") {
    auto entries = Fixture();
    std::vector<size_t> none;
    SortDrillDownIndices(entries, none, DrillSortKey::Price, true);
    CHECK(none.empty());
}
