// Phase 2 (2026-10-02) automated boundary tests for:
//   - F10/F11: print/print-preview resource bounds (dimension validation,
//     DIB byte-size/overflow math, Print Preview memory budget) - pure,
//     GDI-free functions in FishBalanceCore.h, exercised directly here so
//     they're testable without a display or printer (the real GDI
//     rendering in main.cpp is NOT exercised by this portable suite - see
//     Testing.md / the manual Windows checklist for that).
//   - F15: numeric magnitude ceilings (Kgs/Price) and
//     multiplication/accumulation overflow guards.
#include "doctest_setup.h"
#include "doctest.h"
#include "../src/core/FishBalanceCore.h"
#include <limits>

TEST_SUITE("ScaleRound (portable MulDiv equivalent)") {
    TEST_CASE("matches simple proportional scaling") {
        CHECK(ScaleRound(96, 150, 96) == 150);
        CHECK(ScaleRound(100, 300, 100) == 300);
    }
    TEST_CASE("rounds to nearest") {
        CHECK(ScaleRound(1, 3, 2) == 2);  // 1.5 rounds to 2
        CHECK(ScaleRound(1, 5, 2) == 3);  // 2.5 rounds to 3 (away from zero, matching MulDiv)
    }
    TEST_CASE("handles negative values with correct sign") {
        CHECK(ScaleRound(-96, 150, 96) == -150);
    }
    TEST_CASE("a zero or negative denominator returns 0 rather than a huge or "
              "sign-flipped result") {
        CHECK(ScaleRound(100, 5, 0) == 0);
        CHECK(ScaleRound(100, 5, -1) == 0);
    }
    TEST_CASE("large inputs don't overflow the 64-bit intermediate") {
        CHECK(ScaleRound(20000, 2400, 96) == 500000);
    }
}

TEST_SUITE("ValidateReportPageDimensions") {
    TEST_CASE("accepts typical screen and printer dimensions") {
        CHECK(ValidateReportPageDimensions(850, 1100, 100, 100));   // preview fallback (Letter @ 100dpi)
        CHECK(ValidateReportPageDimensions(2550, 3300, 300, 300));  // Letter @ 300dpi
        CHECK(ValidateReportPageDimensions(4960, 7016, 600, 600));  // A4 @ 600dpi
    }
    TEST_CASE("rejects dimensions at/below the minimum") {
        std::wstring err;
        CHECK_FALSE(ValidateReportPageDimensions(0, 1100, 100, 100, &err));
        CHECK_FALSE(err.empty());
        CHECK_FALSE(ValidateReportPageDimensions(850, 0, 100, 100));
        CHECK_FALSE(ValidateReportPageDimensions(-5, 1100, 100, 100));
        CHECK_FALSE(ValidateReportPageDimensions(99, 1100, 100, 100));  // one under the floor
    }
    TEST_CASE("accepts exactly the minimum, rejects one under it") {
        CHECK(ValidateReportPageDimensions(kReportMinPagePx, kReportMinPagePx, kReportMinDpi, kReportMinDpi));
        CHECK_FALSE(ValidateReportPageDimensions(kReportMinPagePx - 1, kReportMinPagePx, kReportMinDpi, kReportMinDpi));
    }
    TEST_CASE("accepts exactly the maximum, rejects one over it") {
        CHECK(ValidateReportPageDimensions(kReportMaxPagePx, kReportMaxPagePx, kReportMaxDpi, kReportMaxDpi));
        CHECK_FALSE(ValidateReportPageDimensions(kReportMaxPagePx + 1, kReportMaxPagePx, kReportMaxDpi, kReportMaxDpi));
        CHECK_FALSE(ValidateReportPageDimensions(kReportMaxPagePx, kReportMaxPagePx, kReportMaxDpi + 1, kReportMaxDpi));
    }
    TEST_CASE("rejects a DPI outside the supported range") {
        CHECK_FALSE(ValidateReportPageDimensions(850, 1100, 1, 100));      // far too low
        CHECK_FALSE(ValidateReportPageDimensions(850, 1100, 100, 100000)); // absurdly high
    }
}

TEST_SUITE("ComputeDibPageBytes") {
    TEST_CASE("computes the expected 24bpp, row-padded byte size") {
        // 850 px * 3 bytes = 2550, which is NOT a multiple of 4, so each row
        // is padded up to the next DWORD boundary: 2552 bytes. * 1100 rows.
        CHECK(ComputeDibPageBytes(850, 1100) == 2552LL * 1100LL);
    }
    TEST_CASE("rows are padded up to a 4-byte boundary when width*3 isn't "
              "already a multiple of 4") {
        // width=1: stride = (3 + 3) & ~3 = 4 (padded from 3 up to 4).
        CHECK(ComputeDibPageBytes(1, 10) == 4LL * 10LL);
    }
    TEST_CASE("rejects non-positive dimensions") {
        CHECK(ComputeDibPageBytes(0, 100) == -1);
        CHECK(ComputeDibPageBytes(100, 0) == -1);
        CHECK(ComputeDibPageBytes(-1, 100) == -1);
    }
    TEST_CASE("rejects a page whose byte size would exceed the per-page ceiling") {
        // kReportMaxPagePx x kReportMaxPagePx is a valid dimension pair on
        // its own, but at 24bpp its byte size vastly exceeds
        // kReportMaxSinglePageBytes (64MB) - must be refused here rather
        // than attempting the allocation.
        CHECK(ComputeDibPageBytes(kReportMaxPagePx, kReportMaxPagePx) == -1);
    }
    TEST_CASE("a page just under the per-page byte ceiling is accepted, one "
              "row over the ceiling is not") {
        // width=4000 gives a fixed stride of exactly 12000 bytes/row
        // (4000*3 = 12000, already a multiple of 4 - no extra padding).
        // kReportMaxSinglePageBytes is 64MiB = 67,108,864 bytes.
        // height=5592 -> 12000*5592 = 67,104,000 bytes (just under - accepted).
        // height=5593 -> 12000*5593 = 67,116,000 bytes (just over - rejected).
        CHECK(ComputeDibPageBytes(4000, 5592) == 67104000LL);
        CHECK(ComputeDibPageBytes(4000, 5593) == -1);
        // 4000 x 5000 x 3 = 60,000,000 bytes - comfortably under - accepted.
        CHECK(ComputeDibPageBytes(4000, 5000) == 60000000LL);
    }
}

TEST_SUITE("PreviewBudgetAllows") {
    TEST_CASE("a normal preview page at a normal page count fits comfortably") {
        CHECK(PreviewBudgetAllows(850, 1100, 1));
        CHECK(PreviewBudgetAllows(1275, 1650, 50)); // Letter @ 150dpi (preview cap), 50 pages
    }
    TEST_CASE("rejects a negative or over-the-page-cap page count") {
        std::wstring err;
        CHECK_FALSE(PreviewBudgetAllows(850, 1100, -1, &err));
        CHECK_FALSE(err.empty());
        CHECK_FALSE(PreviewBudgetAllows(850, 1100, kReportMaxPages + 1));
    }
    TEST_CASE("accepts exactly kReportMaxPages at a small-enough page size") {
        // 400 x 500 px -> 1200 bytes/row (already a multiple of 4) x 500
        // rows = 600,000 bytes/page; x kReportMaxPages (200) = 120,000,000
        // bytes, comfortably under the 512MB total budget.
        CHECK(PreviewBudgetAllows(400, 500, kReportMaxPages));
    }
    TEST_CASE("a full-size preview page (Letter @ 100dpi) at kReportMaxPages "
              "exceeds the total budget, even though each page is nowhere "
              "near the per-page ceiling") {
        // 850 x 1100 x 3 bytes/px (stride rounds up to 2552) = 2,807,200
        // bytes/page; x 200 pages = 561,440,000 bytes, just over the 512MB
        // total budget - this is exactly the "many bounded pages still add
        // up" case the total-budget check (as opposed to the per-page
        // ceiling) exists for.
        long long perPage = ComputeDibPageBytes(850, 1100);
        REQUIRE(perPage > 0);
        REQUIRE(perPage < kReportMaxSinglePageBytes);
        CHECK_FALSE(PreviewBudgetAllows(850, 1100, kReportMaxPages));
    }
    TEST_CASE("rejects a page size so large that even ONE page is refused by "
              "ComputeDibPageBytes") {
        CHECK_FALSE(PreviewBudgetAllows(kReportMaxPagePx, kReportMaxPagePx, 1));
    }
    TEST_CASE("rejects a page count/size combination whose TOTAL exceeds the "
              "documented preview memory budget, even though a single page "
              "would be fine on its own") {
        // 1275 x 1650 x 3 bytes/px (Letter @ 150dpi preview cap) = a bit
        // over 6.3MB/page; kReportMaxPages (200) of those is well over
        // kReportMaxPreviewTotalBytes (512MB), even though one such page is
        // nowhere near the 64MB single-page ceiling.
        long long perPage = ComputeDibPageBytes(1275, 1650);
        REQUIRE(perPage > 0);
        REQUIRE(perPage < kReportMaxSinglePageBytes);
        CHECK_FALSE(PreviewBudgetAllows(1275, 1650, kReportMaxPages));
    }
}

TEST_SUITE("ValidateWeightKg / ValidatePriceDollars (Phase 2 / F15)") {
    TEST_CASE("accepts zero and ordinary values") {
        CHECK(ValidateWeightKg(0).ok);
        CHECK(ValidateWeightKg(25.5).ok);
        CHECK(ValidatePriceDollars(0).ok);
        CHECK(ValidatePriceDollars(12.5).ok);
    }
    TEST_CASE("accepts exactly the maximum, rejects one unit over it") {
        CHECK(ValidateWeightKg(kMaxWeightKg).ok);
        CHECK_FALSE(ValidateWeightKg(kMaxWeightKg + 0.01).ok);
        CHECK(ValidatePriceDollars(kMaxPriceDollars).ok);
        CHECK_FALSE(ValidatePriceDollars(kMaxPriceDollars + 0.01).ok);
    }
    TEST_CASE("rejects negative values") {
        NumericValidationResult r = ValidateWeightKg(-0.01);
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.error.empty());
        CHECK_FALSE(ValidatePriceDollars(-1.0).ok);
    }
    TEST_CASE("rejects NaN and infinity") {
        double nan = std::numeric_limits<double>::quiet_NaN();
        double inf = std::numeric_limits<double>::infinity();
        CHECK_FALSE(ValidateWeightKg(nan).ok);
        CHECK_FALSE(ValidateWeightKg(inf).ok);
        CHECK_FALSE(ValidateWeightKg(-inf).ok);
        CHECK_FALSE(ValidatePriceDollars(nan).ok);
        CHECK_FALSE(ValidatePriceDollars(inf).ok);
    }
    TEST_CASE("an out-of-range error message is safe to show the user as-is "
              "and mentions the actual ceiling") {
        NumericValidationResult r = ValidateWeightKg(999999);
        CHECK_FALSE(r.ok);
        CHECK(r.error.find(L"10000.0") != std::wstring::npos);
    }
}

TEST_SUITE("SafeMultiplyFinite / SafeAccumulate (Phase 2 / F15 overflow guards)") {
    TEST_CASE("ordinary multiplication and accumulation succeed") {
        double r = 0;
        CHECK(SafeMultiplyFinite(10.0, 5.0, r));
        CHECK(r == 50.0);
        double acc = 100.0;
        CHECK(SafeAccumulate(acc, 50.0));
        CHECK(acc == 150.0);
    }
    TEST_CASE("a product that overflows to infinity is rejected") {
        double r = 0;
        CHECK_FALSE(SafeMultiplyFinite(std::numeric_limits<double>::max(), 2.0, r));
    }
    TEST_CASE("an accumulation that overflows to infinity is rejected, and "
              "the accumulator is left unchanged") {
        double acc = std::numeric_limits<double>::max();
        double before = acc;
        CHECK_FALSE(SafeAccumulate(acc, std::numeric_limits<double>::max()));
        CHECK(acc == before); // not updated on failure
    }
    TEST_CASE("NaN inputs are rejected as non-finite results") {
        double r = 0;
        double nan = std::numeric_limits<double>::quiet_NaN();
        CHECK_FALSE(SafeMultiplyFinite(nan, 1.0, r));
        double acc = 1.0;
        CHECK_FALSE(SafeAccumulate(acc, nan));
    }
    TEST_CASE("realistic extreme totals under the actual business ceilings "
              "never come close to overflowing") {
        // kMaxWeightKg x kMaxPriceDollars x kMaxEntryRecords is the worst
        // case this app can actually produce once entry/load-time
        // validation is applied everywhere - nowhere near double's range.
        double perEntry = 0;
        REQUIRE(SafeMultiplyFinite(kMaxWeightKg, kMaxPriceDollars, perEntry));
        double total = 0;
        bool ok = true;
        for (size_t i = 0; i < kMaxEntryRecords; i++) {
            if (!SafeAccumulate(total, perEntry)) { ok = false; break; }
        }
        CHECK(ok);
        CHECK(std::isfinite(total));
    }
}
