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

TEST_SUITE("GatherOtherPricesForSpeciesOnDate") {
    TEST_CASE("only entries matching both product AND date are included") {
        std::vector<Entry> entries;
        Entry a; a.supplier = L"S1"; a.product = L"Grenadier"; a.date = L"2026-09-22"; a.price = 7; entries.push_back(a);
        Entry b; b.supplier = L"S2"; b.product = L"Grenadier"; b.date = L"2026-09-22"; b.price = 8; entries.push_back(b);
        Entry c; c.supplier = L"S1"; c.product = L"Grenadier"; c.date = L"2026-09-21"; c.price = 99; entries.push_back(c); // wrong date
        Entry d; d.supplier = L"S1"; d.product = L"Garfish";   d.date = L"2026-09-22"; d.price = 99; entries.push_back(d); // wrong species
        auto prices = GatherOtherPricesForSpeciesOnDate(entries, L"Grenadier", L"2026-09-22", -1);
        REQUIRE(prices.size() == 2);
        CHECK(prices[0] == doctest::Approx(7.0));
        CHECK(prices[1] == doctest::Approx(8.0));
    }

    TEST_CASE("excludeIndex leaves that entry's own price out of its own baseline") {
        std::vector<Entry> entries;
        Entry a; a.supplier = L"S1"; a.product = L"Grenadier"; a.date = L"2026-09-22"; a.price = 50; entries.push_back(a);
        Entry b; b.supplier = L"S2"; b.product = L"Grenadier"; b.date = L"2026-09-22"; b.price = 7;  entries.push_back(b);
        auto prices = GatherOtherPricesForSpeciesOnDate(entries, L"Grenadier", L"2026-09-22", 0);
        REQUIRE(prices.size() == 1);
        CHECK(prices[0] == doctest::Approx(7.0));
    }

    TEST_CASE("excludeIndex of -1 excludes nothing") {
        std::vector<Entry> entries;
        Entry a; a.supplier = L"S1"; a.product = L"Grenadier"; a.date = L"2026-09-22"; a.price = 50; entries.push_back(a);
        auto prices = GatherOtherPricesForSpeciesOnDate(entries, L"Grenadier", L"2026-09-22", -1);
        REQUIRE(prices.size() == 1);
    }
}

TEST_SUITE("ComputeOutlierRange") {
    TEST_CASE("fewer than 4 prices: returns false, no range computed") {
        OutlierRange range;
        CHECK(ComputeOutlierRange({5.0, 6.0, 7.0}, range) == false);
    }

    TEST_CASE("exactly 4 prices (even count) - hand-checked Tukey's fences") {
        // Sorted: 3, 5, 8, 12. Q1 = mean(3,5) = 4. Q3 = mean(8,12) = 10.
        // IQR = 6. low = 4 - 9 = -5. high = 10 + 9 = 19.
        OutlierRange range;
        REQUIRE(ComputeOutlierRange({12.0, 3.0, 8.0, 5.0}, range) == true);
        CHECK(range.low == doctest::Approx(-5.0));
        CHECK(range.high == doctest::Approx(19.0));
    }

    TEST_CASE("5 prices (odd count) - hand-checked Tukey's fences, middle "
              "element excluded from both halves") {
        // Sorted: 2, 4, 6, 8, 10. Lower half {2,4} -> Q1=3. Upper half
        // {8,10} -> Q3=9 (the middle element, 6, is in neither half).
        // IQR = 6. low = 3 - 9 = -6. high = 9 + 9 = 18.
        OutlierRange range;
        REQUIRE(ComputeOutlierRange({10.0, 2.0, 6.0, 4.0, 8.0}, range) == true);
        CHECK(range.low == doctest::Approx(-6.0));
        CHECK(range.high == doctest::Approx(18.0));
    }

    TEST_CASE("Jack's own example: most Blue Grenadier $5-$10, a $25 entry "
              "falls outside the computed range, a normal $7 does not") {
        std::vector<double> baseline = {5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
        OutlierRange range;
        REQUIRE(ComputeOutlierRange(baseline, range) == true);
        CHECK(25.0 > range.high);
        CHECK_FALSE(7.0 < range.low);
        CHECK_FALSE(7.0 > range.high);
    }

    TEST_CASE("v0.9.23 floor fix: five identical $10 entries used to give "
              "IQR=0 and flag ANY deviation - Jack hit this with a "
              "genuinely normal $12 sixth entry. With the 20% floor, $12 "
              "is now within range, but a real typo like $50 still isn't") {
        std::vector<double> baseline = {10.0, 10.0, 10.0, 10.0, 10.0};
        OutlierRange range;
        REQUIRE(ComputeOutlierRange(baseline, range) == true);
        CHECK(range.low == doctest::Approx(7.0));  // 10 - 1.5*(0.20*10)
        CHECK(range.high == doctest::Approx(13.0)); // 10 + 1.5*(0.20*10)
        CHECK_FALSE(12.0 > range.high);  // the genuine false positive Jack reported
        CHECK_FALSE(12.0 < range.low);
        CHECK(50.0 > range.high);        // a real typo still gets caught
    }

    TEST_CASE("the floor only ever widens the fence, never narrows a "
              "baseline that already has real spread (re-checks the "
              "existing even/odd-count cases above still hold)") {
        OutlierRange range;
        REQUIRE(ComputeOutlierRange({12.0, 3.0, 8.0, 5.0}, range) == true);
        CHECK(range.low == doctest::Approx(-5.0));
        CHECK(range.high == doctest::Approx(19.0));
    }
}

TEST_SUITE("ReevaluateOutlierFlagsForSpeciesOnDate") {
    TEST_CASE("Jack's reported scenario: a first-entry typo isn't caught "
              "until enough LATER entries exist to check it against - "
              "this is exactly the gap this function closes") {
        // Real numbers from the bug report: $1111 typo entered first,
        // then four correctly-priced $11/$11/$11/$111 entries after it.
        std::vector<Entry> entries;
        for (double p : {1111.0, 11.0, 11.0, 11.0, 111.0}) {
            Entry e; e.supplier = L"J Casement"; e.product = L"bonito";
            e.date = L"2026-09-22"; e.kgs = 111.0; e.price = p;
            entries.push_back(e);
        }
        ReevaluateOutlierFlagsForSpeciesOnDate(entries, L"bonito", L"2026-09-22");
        CHECK(entries[0].priceFlagged == true);  // the $1111 typo
        CHECK(entries[1].priceFlagged == false);
        CHECK(entries[2].priceFlagged == false);
        CHECK(entries[3].priceFlagged == false);
        CHECK(entries[4].priceFlagged == false); // the $111 entry that finally exposed it
    }

    TEST_CASE("with only 4 total entries (so each one's own leave-one-out "
              "baseline is just 3), nothing is flagged - below minimum") {
        std::vector<Entry> entries;
        for (double p : {1111.0, 11.0, 11.0, 11.0}) {
            Entry e; e.supplier = L"S"; e.product = L"bonito"; e.date = L"2026-09-22";
            e.kgs = 1; e.price = p;
            entries.push_back(e);
        }
        ReevaluateOutlierFlagsForSpeciesOnDate(entries, L"bonito", L"2026-09-22");
        for (auto& e : entries) CHECK(e.priceFlagged == false);
    }

    TEST_CASE("entries for a different species or date are left untouched") {
        std::vector<Entry> entries;
        Entry a; a.supplier=L"S"; a.product=L"bonito";  a.date=L"2026-09-22"; a.price=1111; a.priceFlagged=true; entries.push_back(a);
        Entry b; b.supplier=L"S"; b.product=L"garfish";  b.date=L"2026-09-22"; b.price=1111; b.priceFlagged=true; entries.push_back(b); // different species
        Entry c; c.supplier=L"S"; c.product=L"bonito";  c.date=L"2026-09-21"; c.price=1111; c.priceFlagged=true; entries.push_back(c); // different date
        ReevaluateOutlierFlagsForSpeciesOnDate(entries, L"bonito", L"2026-09-22");
        // Only entries[0] is in-scope; the other two keep whatever flag they had.
        CHECK(entries[1].priceFlagged == true);
        CHECK(entries[2].priceFlagged == true);
    }

    TEST_CASE("a previously-flagged entry is un-flagged once the data around "
              "it changes enough to no longer look unusual") {
        std::vector<Entry> entries;
        for (double p : {1111.0, 11.0, 11.0, 11.0, 111.0}) {
            Entry e; e.supplier = L"S"; e.product = L"bonito"; e.date = L"2026-09-22";
            e.kgs = 1; e.price = p;
            entries.push_back(e);
        }
        ReevaluateOutlierFlagsForSpeciesOnDate(entries, L"bonito", L"2026-09-22");
        REQUIRE(entries[0].priceFlagged == true); // confirmed flagged first, same as above

        entries[0].price = 11.0; // "fixing" the typo, as if the user edited it
        ReevaluateOutlierFlagsForSpeciesOnDate(entries, L"bonito", L"2026-09-22");
        CHECK(entries[0].priceFlagged == false);
    }
}
