#include "doctest_setup.h"
#include "doctest.h"
#include "../FishBalanceCore.h"

TEST_SUITE("LooksLikeEmail") {
    TEST_CASE("accepts plausible addresses") {
        CHECK(LooksLikeEmail(L"a@b.com"));
        CHECK(LooksLikeEmail(L"jcasement@example.com.au"));
        CHECK(LooksLikeEmail(L"first.last@example.com"));
        CHECK(LooksLikeEmail(L"user+tag@example.com"));
    }
    TEST_CASE("rejects missing or misplaced @") {
        CHECK_FALSE(LooksLikeEmail(L"no-at-sign"));
        CHECK_FALSE(LooksLikeEmail(L"@b.com"));   // nothing before @
        CHECK_FALSE(LooksLikeEmail(L"a@"));        // nothing after @, and no dot
    }
    TEST_CASE("rejects more than one @") {
        CHECK_FALSE(LooksLikeEmail(L"a@b@c.com"));
    }
    TEST_CASE("rejects a space anywhere") {
        CHECK_FALSE(LooksLikeEmail(L"a b@c.com"));
        CHECK_FALSE(LooksLikeEmail(L"a@b .com"));
    }
    TEST_CASE("rejects no dot after the @, or a dot as the very last character") {
        CHECK_FALSE(LooksLikeEmail(L"a@bcom"));  // no dot at all
        CHECK_FALSE(LooksLikeEmail(L"a@b."));    // dot is the last character
    }
    TEST_CASE("Phase 2 / F13 (2026-10-02): no longer accepted - a dot "
              "immediately after @, or a leading/trailing/doubled dot in "
              "either the local part or the domain") {
        // Previously accepted as a documented, intentionally-basic gap;
        // tightened as part of the mailto/email hardening in this phase.
        CHECK_FALSE(LooksLikeEmail(L"a@.com"));
        CHECK_FALSE(LooksLikeEmail(L".a@b.com"));
        CHECK_FALSE(LooksLikeEmail(L"a.@b.com"));
        CHECK_FALSE(LooksLikeEmail(L"a..b@c.com"));
        CHECK_FALSE(LooksLikeEmail(L"a@b..com"));
        CHECK_FALSE(LooksLikeEmail(L"a@b.com."));
        CHECK_FALSE(LooksLikeEmail(L"a@.b.com"));
    }
    TEST_CASE("Phase 2 / F13: rejects control characters - CR, LF, NUL, tab, "
              "and other C0 controls") {
        CHECK_FALSE(LooksLikeEmail(L"a@b.com\r"));
        CHECK_FALSE(LooksLikeEmail(L"a@b.com\n"));
        CHECK_FALSE(LooksLikeEmail(std::wstring(L"a@b\0.com", 8)));
        CHECK_FALSE(LooksLikeEmail(L"a\t@b.com"));
        CHECK_FALSE(LooksLikeEmail(L"a@b\r\n.com\r\nBcc:evil@evil.com"));
    }
    TEST_CASE("Phase 2 / F13: rejects mailto/URI/header-injection characters "
              "(?, &, #, %, comma, semicolon, angle brackets, etc.)") {
        CHECK_FALSE(LooksLikeEmail(L"a@b.com?subject=hi"));
        CHECK_FALSE(LooksLikeEmail(L"a@b.com&cc=evil@evil.com"));
        CHECK_FALSE(LooksLikeEmail(L"a@b.com#frag"));
        CHECK_FALSE(LooksLikeEmail(L"a%40b@evil.com"));
        CHECK_FALSE(LooksLikeEmail(L"a@b.com,evil@evil.com"));
        CHECK_FALSE(LooksLikeEmail(L"a@b.com;evil@evil.com"));
        CHECK_FALSE(LooksLikeEmail(L"<a@b.com>"));
        CHECK_FALSE(LooksLikeEmail(L"\"a\"@b.com"));
    }
    TEST_CASE("accepts Unicode (internationalized) local parts/domains that "
              "otherwise fit the supported shape") {
        CHECK(LooksLikeEmail(L"café@example.com"));
    }
}

TEST_SUITE("BuildMailtoUri") {
    TEST_CASE("a valid address builds a mailto: URI with the address, "
              "subject, and body each present and correctly separated") {
        MailtoBuildResult r = BuildMailtoUri(L"jcasement@example.com", L"Hello There", L"Body text");
        REQUIRE(r.ok);
        CHECK(r.uri.find(L"mailto:jcasement@example.com?subject=") == 0);
        CHECK(r.uri.find(L"&body=") != std::wstring::npos);
        CHECK(r.uri.find(L"Hello%20There") != std::wstring::npos);
    }
    TEST_CASE("an empty address is valid (recipient-less draft) - a "
              "deliberately supported case, not a validation failure") {
        MailtoBuildResult r = BuildMailtoUri(L"", L"Subject", L"Body");
        REQUIRE(r.ok);
        CHECK(r.uri.find(L"mailto:?subject=") == 0);
    }
    TEST_CASE("Phase 2 / F13: an invalid stored address is refused outright - "
              "ok == false with a user-facing error - rather than launching a "
              "best-effort-encoded URI built from it") {
        MailtoBuildResult r = BuildMailtoUri(L"not-an-email", L"Subject", L"Body");
        CHECK_FALSE(r.ok);
        CHECK(r.uri.empty());
        CHECK_FALSE(r.error.empty());
    }
    TEST_CASE("Phase 2 / F13: a malicious stored address containing mailto "
              "query-injection characters is refused, not partially encoded") {
        MailtoBuildResult r = BuildMailtoUri(L"a@b.com?cc=evil@evil.com", L"Subject", L"Body");
        CHECK_FALSE(r.ok);
        MailtoBuildResult r2 = BuildMailtoUri(L"a@b.com&bcc=evil@evil.com", L"Subject", L"Body");
        CHECK_FALSE(r2.ok);
    }
    TEST_CASE("Phase 2 / F13: subject/body content that itself contains "
              "mailto-meaningful characters can't inject extra query "
              "parameters into the final URI - each is percent-encoded "
              "independently") {
        MailtoBuildResult r = BuildMailtoUri(L"a@b.com", L"Subj&cc=evil@evil.com",
                                              L"Body?x=1&bcc=evil@evil.com");
        REQUIRE(r.ok);
        // Exactly two real query separators: the one before "subject=" and
        // the one before "body=" - any other '&'/'?' in the URI must have
        // come from percent-encoded content, not a literal character.
        CHECK(r.uri.find(L"&cc=") == std::wstring::npos);
        CHECK(r.uri.find(L"&bcc=") == std::wstring::npos);
        CHECK(r.uri.find(L"?x=") == std::wstring::npos);
        CHECK(r.uri.find(L"%26cc%3Devil") != std::wstring::npos); // "&cc=evil" safely encoded instead
    }
    TEST_CASE("the '@' separating local part and domain is never itself "
              "percent-encoded (RFC 6068 requires it literal)") {
        MailtoBuildResult r = BuildMailtoUri(L"user.name@sub.example.com", L"S", L"B");
        REQUIRE(r.ok);
        CHECK(r.uri.find(L"mailto:user.name@sub.example.com?") == 0);
    }
}

TEST_SUITE("UrlEncodeForMailto") {
    TEST_CASE("unreserved characters (letters, digits, - _ . ~) pass through "
              "unencoded") {
        CHECK(UrlEncodeForMailto(L"abcXYZ019-_.~") == L"abcXYZ019-_.~");
    }
    TEST_CASE("space becomes %20") {
        CHECK(UrlEncodeForMailto(L"hello world") == L"hello%20world");
    }
    TEST_CASE("percent-encodes reserved/special characters, using uppercase hex") {
        CHECK(UrlEncodeForMailto(L"100%") == L"100%25");
        CHECK(UrlEncodeForMailto(L"a&b") == L"a%26b");
        CHECK(UrlEncodeForMailto(L"a=b") == L"a%3Db");
    }
    TEST_CASE("newlines get encoded (CR and LF separately, matching the "
              "email body's \\r\\n line endings)") {
        std::wstring encoded = UrlEncodeForMailto(L"line1\r\nline2");
        CHECK(encoded == L"line1%0D%0Aline2");
    }
    TEST_CASE("empty string encodes to empty string") {
        CHECK(UrlEncodeForMailto(L"") == L"");
    }
}

TEST_SUITE("GreetingForHour") {
    TEST_CASE("morning is before 12") {
        CHECK(GreetingForHour(0) == L"Good morning");
        CHECK(GreetingForHour(11) == L"Good morning");
    }
    TEST_CASE("afternoon is 12 up to (not including) 17") {
        CHECK(GreetingForHour(12) == L"Good afternoon");
        CHECK(GreetingForHour(16) == L"Good afternoon");
    }
    TEST_CASE("evening is 17 and later") {
        CHECK(GreetingForHour(17) == L"Good evening");
        CHECK(GreetingForHour(23) == L"Good evening");
    }
}

TEST_SUITE("PadRight") {
    TEST_CASE("pads shorter strings with trailing spaces to the given width") {
        CHECK(PadRight(L"KG", 7) == L"KG     "); // 2 chars + 5 spaces = 7
    }
    TEST_CASE("a string already at or beyond the width is left unchanged") {
        CHECK(PadRight(L"1234567", 7) == L"1234567");
        CHECK(PadRight(L"12345678", 7) == L"12345678"); // longer than width - not truncated
    }
}

TEST_SUITE("BuildSupplierEmailBody") {
    TEST_CASE("includes the right greeting for the hour and the species/price "
              "table") {
        std::vector<Entry> entries;
        Entry e; e.supplier = L"Jcasement"; e.product = L"Garfish"; e.kgs = 13.8; e.price = 17.0;
        entries.push_back(e);
        auto breakdown = BuildBreakdownData(entries);
        REQUIRE(breakdown.size() == 1);

        std::wstring body = BuildSupplierEmailBody(breakdown[0], /*currentHour=*/9);
        CHECK(body.find(L"Good morning") == 0); // greeting is the very first thing
        CHECK(body.find(L"Garfish") != std::wstring::npos);
        CHECK(body.find(L"Kind regards,") != std::wstring::npos);
    }

    TEST_CASE("an unusually long list of line items falls back to a shortened "
              "summary instead of an oversized mailto: body") {
        // Synthesize a SupplierGroup with enough distinct price lines to
        // push the body over the 1500-character threshold, without going
        // through BuildBreakdownData.
        SupplierGroup sg;
        sg.supplier = L"BigSupplier";
        for (int i = 0; i < 100; i++) {
            ProductGroup pg;
            pg.species = L"Species" + std::to_wstring(i);
            pg.prices.push_back({ (double)(i + 1), 1.0, (double)(i + 1) });
            pg.totalKgs = 1.0;
            pg.totalAmt = (double)(i + 1);
            sg.products.push_back(pg);
            sg.totalKgs += 1.0;
            sg.totalAmt += (i + 1);
        }

        std::wstring body = BuildSupplierEmailBody(sg, 9);
        CHECK(body.size() <= 1500 + 400); // shortened, not the full ~100-line table
        CHECK(body.find(L"shortened because the full list was too long") != std::wstring::npos);
        CHECK(body.find(L"100 species") != std::wstring::npos);
    }
}
