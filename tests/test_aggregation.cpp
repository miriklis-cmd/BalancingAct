#include "doctest_setup.h"
#include "doctest.h"
#include "../FishBalanceCore.h"

namespace {
// Shared fixture used across several test cases below:
//   Jcasement / Garfish  : 10kg @ $5, 5kg @ $5   (same price, should merge)
//   Jcasement / Snapper  : 2kg @ $20
//   Wdowns    / Garfish  : 8kg @ $6
std::vector<Entry> MakeFixture() {
    std::vector<Entry> entries;
    Entry e1; e1.supplier = L"Jcasement"; e1.product = L"Garfish"; e1.kgs = 10; e1.price = 5;
    Entry e2; e2.supplier = L"Jcasement"; e2.product = L"Garfish"; e2.kgs = 5;  e2.price = 5;
    Entry e3; e3.supplier = L"Jcasement"; e3.product = L"Snapper"; e3.kgs = 2;  e3.price = 20;
    Entry e4; e4.supplier = L"Wdowns";    e4.product = L"Garfish"; e4.kgs = 8;  e4.price = 6;
    entries.push_back(e1);
    entries.push_back(e2);
    entries.push_back(e3);
    entries.push_back(e4);
    return entries;
}
}

TEST_SUITE("BuildBreakdownData") {
    TEST_CASE("groups by Supplier -> Species -> Price, sorted alphabetically, "
              "with correct subtotals") {
        auto data = BuildBreakdownData(MakeFixture());
        REQUIRE(data.size() == 2); // Jcasement, Wdowns (alphabetical)
        CHECK(data[0].supplier == L"Jcasement");
        CHECK(data[1].supplier == L"Wdowns");

        // Jcasement's total: 10*5 + 5*5 + 2*20 = 50+25+40 = 115
        CHECK(data[0].totalKgs == 17); // 10+5+2
        CHECK(data[0].totalAmt == doctest::Approx(115));

        // Within Jcasement, Garfish's two same-price rows should merge into
        // one PriceLine at $5 (this is exactly what feeds the Breakdown
        // tab's per-price grouping and the printed report).
        REQUIRE(data[0].products.size() == 2); // Garfish, Snapper
        CHECK(data[0].products[0].species == L"Garfish");
        REQUIRE(data[0].products[0].prices.size() == 1); // merged into one price line
        CHECK(data[0].products[0].prices[0].price == 5);
        CHECK(data[0].products[0].prices[0].kgs == 15); // 10+5

        CHECK(data[1].supplier == L"Wdowns");
        CHECK(data[1].totalKgs == 8);
        CHECK(data[1].totalAmt == doctest::Approx(48));
    }

    TEST_CASE("empty entries produces an empty breakdown") {
        auto data = BuildBreakdownData({});
        CHECK(data.empty());
    }

    TEST_CASE("different prices for the same supplier/species stay as "
              "separate price lines, sorted ascending") {
        std::vector<Entry> entries;
        Entry a; a.supplier = L"S"; a.product = L"P"; a.kgs = 1; a.price = 10;
        Entry b; b.supplier = L"S"; b.product = L"P"; b.kgs = 2; b.price = 5;
        entries.push_back(a);
        entries.push_back(b);
        auto data = BuildBreakdownData(entries);
        REQUIRE(data.size() == 1);
        REQUIRE(data[0].products.size() == 1);
        REQUIRE(data[0].products[0].prices.size() == 2);
        CHECK(data[0].products[0].prices[0].price == 5);  // ascending
        CHECK(data[0].products[0].prices[1].price == 10);
    }
}

TEST_SUITE("ComputeGroupedTotals - v0.9.1 regression: the Total Overview column bug") {
    // This is the exact computation behind the v0.9.1 bug where the Total
    // Overview tab's "Total ($)" column was actually showing Kgs, because
    // the list view was missing a column, not because this math was wrong.
    // This test pins down that the math itself has always been correct -
    // the fix was in main.cpp's column definitions, not here.
    TEST_CASE("grouping by supplier produces correct Kgs and dollar totals "
              "per supplier, plus a correct grand total") {
        auto result = ComputeGroupedTotals(MakeFixture(), /*bySupplier=*/true);
        REQUIRE(result.rows.size() == 2);
        CHECK(result.rows[0].key == L"Jcasement");
        CHECK(result.rows[0].kgs == 17);
        CHECK(result.rows[0].amt == doctest::Approx(115));
        CHECK(result.rows[1].key == L"Wdowns");
        CHECK(result.rows[1].kgs == 8);
        CHECK(result.rows[1].amt == doctest::Approx(48));
        CHECK(result.grandKgs == 25);           // 17+8
        CHECK(result.grandAmt == doctest::Approx(163)); // 115+48
    }

    TEST_CASE("grouping by species instead of supplier") {
        auto result = ComputeGroupedTotals(MakeFixture(), /*bySupplier=*/false);
        REQUIRE(result.rows.size() == 2); // Garfish, Snapper
        for (auto& row : result.rows) {
            if (row.key == L"Garfish") {
                CHECK(row.kgs == 23); // 10+5+8 across both suppliers
                CHECK(row.amt == doctest::Approx(50 + 25 + 48));
            }
        }
    }

    TEST_CASE("empty entries produces zero grand totals and no rows") {
        auto result = ComputeGroupedTotals({}, true);
        CHECK(result.rows.empty());
        CHECK(result.grandKgs == 0);
        CHECK(result.grandAmt == 0);
    }
}

TEST_SUITE("ComputeSpeciesStats") {
    TEST_CASE("computes Kgs/Total plus average/highest/lowest price per species") {
        auto result = ComputeSpeciesStats(MakeFixture());
        REQUIRE(result.rows.size() == 2); // Garfish, Snapper

        bool foundGarfish = false;
        for (auto& row : result.rows) {
            if (row.species == L"Garfish") {
                foundGarfish = true;
                CHECK(row.kgs == 23);  // 10+5+8
                CHECK(row.amt == doctest::Approx(50 + 25 + 48));
                // Prices seen for Garfish: 5, 5, 6 -> avg 16/3
                CHECK(row.avgPrice == doctest::Approx((5.0 + 5.0 + 6.0) / 3.0));
                CHECK(row.maxPrice == 6);
                CHECK(row.minPrice == 5);
            }
        }
        CHECK(foundGarfish);
    }

    TEST_CASE("a species with only one entry has avg == max == min == that price") {
        std::vector<Entry> entries;
        Entry e; e.supplier = L"S"; e.product = L"Only"; e.kgs = 1; e.price = 7;
        entries.push_back(e);
        auto result = ComputeSpeciesStats(entries);
        REQUIRE(result.rows.size() == 1);
        CHECK(result.rows[0].avgPrice == 7);
        CHECK(result.rows[0].maxPrice == 7);
        CHECK(result.rows[0].minPrice == 7);
    }

    TEST_CASE("average price is NOT quantity-weighted (a documented business "
              "rule, not a bug - see BUSINESS_RULES.md)") {
        // Two entries at very different weights but we still want a simple
        // mean of the price values, not total$/totalKg.
        std::vector<Entry> entries;
        Entry a; a.supplier = L"S"; a.product = L"P"; a.kgs = 1000; a.price = 1;
        Entry b; b.supplier = L"S"; b.product = L"P"; b.kgs = 1;    b.price = 9;
        entries.push_back(a);
        entries.push_back(b);
        auto result = ComputeSpeciesStats(entries);
        REQUIRE(result.rows.size() == 1);
        // Simple mean of (1, 9) = 5, NOT the quantity-weighted
        // (1000*1 + 1*9) / 1001 =~ 1.008
        CHECK(result.rows[0].avgPrice == doctest::Approx(5.0));
    }
}
