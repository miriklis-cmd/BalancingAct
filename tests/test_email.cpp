#include "doctest_setup.h"
#include "doctest.h"
#include "../src/core/FishBalanceCore.h"

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

    TEST_CASE("v0.18.4: dated intro, KG / SPECIES / PRICE separated by tabs, price column on the same tab stop") {
        std::vector<Entry> entries;
        Entry a; a.supplier = L"Lucas"; a.product = L"Garfish"; a.kgs = 13.8; a.price = 25.0; entries.push_back(a);
        Entry b; b.supplier = L"Lucas"; b.product = L"Fantail Mullet"; b.kgs = 1.3; b.price = 70.0; entries.push_back(b);
        auto bd = BuildBreakdownData(entries);
        REQUIRE(bd.size() == 1);
        const std::wstring body = BuildSupplierEmailBody(bd[0], 14, L"12 November 2026");
        CHECK(body.find(L"Good afternoon,") == 0);
        CHECK(body.find(L"Please find below the prices for today's sale (12 November 2026):") != std::wstring::npos);
        // longest species is 14 chars -> 2 stops; "SPECIES" (7) and "Garfish" (7) need 2 tabs, "Fantail Mullet" needs 1
        CHECK(body.find(L"KG\tSPECIES\t\tPRICE\r\n") != std::wstring::npos);
        CHECK(body.find(L"13.8\tGarfish\t\t$25.00\r\n") != std::wstring::npos);
        CHECK(body.find(L"1.3\tFantail Mullet\t$70.00\r\n") != std::wstring::npos);
        CHECK(body.find(L"  ") == std::wstring::npos); // no runs of spaces anywhere - Outlook collapses them
        CHECK(body.find(L"kg in total") == std::wstring::npos);
        CHECK(BuildSupplierEmailBody(bd[0], 9).find(L"Please find below the prices for today's sale:") != std::wstring::npos);
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

TEST_SUITE("v0.20.0 email mode + .eml builder") {
    static SupplierGroup Sg() {
        SupplierGroup sg; sg.supplier = L"Alpha & Co";
        ProductGroup pg; pg.species = L"Ckd <Sch> Prawn"; pg.prices.push_back({22.5, 8.0, 180.0});
        sg.products.push_back(pg);
        return sg;
    }
    TEST_CASE("mode text round-trips; unknown or empty means mailto") {
        CHECK(ParseEmailMode(L"eml") == EmailMode::Eml);
        CHECK(ParseEmailMode(L"gmail") == EmailMode::Gmail);
        CHECK(ParseEmailMode(L"mailto") == EmailMode::Mailto);
        CHECK(ParseEmailMode(L"") == EmailMode::Mailto);
        CHECK(ParseEmailMode(L"junk") == EmailMode::Mailto);
        CHECK(std::wstring(EmailModeToString(EmailMode::Eml)) == L"eml");
    }
    TEST_CASE("eml is an unsent HTML draft with the table, addressed to the supplier") {
        auto r = BuildSupplierEml(Sg(), 9, L"18 November 2026", L"a@b.com", L"Prices - 18 November 2026 - Alpha");
        REQUIRE(r.ok);
        CHECK(r.content.find("X-Unsent: 1\r\nTo: a@b.com\r\nSubject: Prices - 18 November 2026 - Alpha\r\n") == 0);
        CHECK(r.content.find("Content-Type: text/html; charset=utf-8") != std::string::npos);
        CHECK(r.content.find("<table") != std::string::npos);
        CHECK(r.content.find("$22.50") != std::string::npos);
        CHECK(r.content.find("8.0") != std::string::npos);
    }
    TEST_CASE("species and date text are HTML-escaped") {
        auto r = BuildSupplierEml(Sg(), 9, L"x<y", L"", L"S");
        REQUIRE(r.ok);
        CHECK(r.content.find("Ckd &lt;Sch&gt; Prawn") != std::string::npos);
        CHECK(r.content.find("x&lt;y") != std::string::npos);
        CHECK(r.content.find("Ckd <Sch>") == std::string::npos);
    }
    TEST_CASE("blank address is allowed; bad address and header-injection are refused") {
        CHECK(BuildSupplierEml(Sg(), 9, L"", L"", L"S").ok);
        CHECK_FALSE(BuildSupplierEml(Sg(), 9, L"", L"not-an-address", L"S").ok);
        CHECK_FALSE(BuildSupplierEml(Sg(), 9, L"", L"a@b.com\r\nBcc: x@y.com", L"S").ok);
        CHECK_FALSE(BuildSupplierEml(Sg(), 9, L"", L"a@b.com", L"S\r\nBcc: x@y.com").ok);
    }
    TEST_CASE("non-ASCII subject is RFC 2047 encoded") {
        auto r = BuildSupplierEml(Sg(), 9, L"", L"", L"Caf\u00E9" /* escape, not a literal: MSVC reads BOM-less sources as ANSI */);
        REQUIRE(r.ok);
        CHECK(r.content.find("Subject: =?UTF-8?B?Q2Fmw6k=?=") != std::string::npos);
    }
    TEST_CASE("base64 handles all remainders") {
        CHECK(Base64EncodeBytes("") == "");
        CHECK(Base64EncodeBytes("f") == "Zg==");
        CHECK(Base64EncodeBytes("fo") == "Zm8=");
        CHECK(Base64EncodeBytes("foo") == "Zm9v");
    }
}

TEST_CASE("v0.21.0: BuildSupplierEml(unsent=false) has no X-Unsent line (Gmail API drafts)") {
    SupplierGroup sg; sg.supplier = L"A";
    ProductGroup pg; pg.species = L"Flathead"; pg.prices.push_back({14.0, 12.5, 175.0}); sg.products.push_back(pg);
    auto r = BuildSupplierEml(sg, 9, L"d", L"a@b.com", L"S", false);
    REQUIRE(r.ok);
    CHECK(r.content.find("X-Unsent") == std::string::npos);
    CHECK(r.content.rfind("To: a@b.com\r\nSubject: S\r\n", 0) == 0);
}

TEST_SUITE("v0.22.0 email wording + settings clamps") {
    TEST_CASE("blank wording falls back to the defaults") {
        auto w = ResolveEmailWording(L"", L"   ", L"");
        CHECK(w.subject == L"Prices - {date} - {supplier}");
        CHECK(w.intro == L"Please find below the prices for today's sale");
        CHECK(w.signoff == L"Kind regards,");
    }
    TEST_CASE("wording is single-line and capped (no header injection)") {
        auto w = ResolveEmailWording(L"Hi\r\nBcc: x@y.com", L"A\tB", std::wstring(400, L'x'));
        CHECK(w.subject == L"Hi  Bcc: x@y.com");
        CHECK(w.intro == L"A B");
        CHECK(w.signoff.size() == 150);
        CHECK(w.subject.find(L'\r') == std::wstring::npos);
    }
    TEST_CASE("subject placeholders") {
        CHECK(FormatEmailSubject(L"Prices - {date} - {supplier}", L"18 Nov", L"Alpha") == L"Prices - 18 Nov - Alpha");
        CHECK(FormatEmailSubject(L"{supplier} prices", L"d", L"A&B") == L"A&B prices");
        CHECK(FormatEmailSubject(L"No placeholders", L"d", L"S") == L"No placeholders");
        CHECK(FormatEmailSubject(L"{supplier}", L"d", L"") == L"Prices"); // empty result -> safe fallback
    }
    TEST_CASE("custom wording reaches the mailto body and the eml") {
        SupplierGroup sg; sg.supplier = L"A";
        ProductGroup pg; pg.species = L"Flathead"; pg.prices.push_back({14.0, 12.5, 175.0}); sg.products.push_back(pg);
        EmailWording w; w.intro = L"Today's numbers"; w.signoff = L"Cheers, Jack";
        auto body = BuildSupplierEmailBody(sg, 9, L"18 Nov", w);
        CHECK(body.find(L"Today's numbers (18 Nov):") != std::wstring::npos);
        CHECK(body.find(L"Cheers, Jack") != std::wstring::npos);
        CHECK(body.find(L"Kind regards") == std::wstring::npos);
        auto r = BuildSupplierEml(sg, 9, L"18 Nov", L"", L"S", true, w);
        REQUIRE(r.ok);
        CHECK(r.content.find("Today's numbers (18 Nov):") != std::string::npos);
        CHECK(r.content.find("Cheers, Jack") != std::string::npos);
        // defaults unchanged
        CHECK(BuildSupplierEmailBody(sg, 9, L"18 Nov").find(L"Please find below the prices for today's sale (18 Nov):") != std::wstring::npos);
    }
    TEST_CASE("clamps and sensitivity mapping") {
        CHECK(ClampBackupMinutes(0) == 1); CHECK(ClampBackupMinutes(3) == 3); CHECK(ClampBackupMinutes(999) == 60);
        CHECK(ClampBackupKeep(1) == 10); CHECK(ClampBackupKeep(50) == 50); CHECK(ClampBackupKeep(9999) == 500);
        CHECK(ClampOutlierSensitivity(-1) == 1); CHECK(ClampOutlierSensitivity(0) == 0); CHECK(ClampOutlierSensitivity(7) == 1);
        CHECK(OutlierFenceForSensitivity(0) == doctest::Approx(2.5));
        CHECK(OutlierFenceForSensitivity(1) == doctest::Approx(1.5));
        CHECK(OutlierFenceForSensitivity(2) == doctest::Approx(1.0));
    }
    TEST_CASE("sensitivity changes what gets flagged") {
        std::vector<double> prices = {10, 11, 12, 13, 14, 15};
        const double saved = g_outlierFence;
        OutlierRange normal, relaxed, strict;
        g_outlierFence = 1.5; REQUIRE(ComputeOutlierRange(prices, normal));
        g_outlierFence = 2.5; REQUIRE(ComputeOutlierRange(prices, relaxed));
        g_outlierFence = 1.0; REQUIRE(ComputeOutlierRange(prices, strict));
        g_outlierFence = saved;
        CHECK(relaxed.high > normal.high); CHECK(normal.high > strict.high);
        CHECK(relaxed.low < normal.low);   CHECK(normal.low < strict.low);
    }
}

TEST_CASE("v0.23.0: email date style and sheet date") {
    const SimpleDate d{ 2026, 11, 7 };
    CHECK(FormatDateStyle(d, 0) == L"7 November 2026");
    CHECK(FormatDateStyle(d, 1) == L"7 Nov 2026");
    CHECK(FormatDateStyle(d, 2) == L"07/11/2026");
    CHECK(FormatDateStyle(d, 99) == L"7 November 2026"); // unknown style = long
    CHECK(ClampDateStyle(-1) == 0); CHECK(ClampDateStyle(2) == 2); CHECK(ClampDateStyle(3) == 0);
    CHECK(ClampTextPercent(125) == 125); CHECK(ClampTextPercent(150) == 150); CHECK(ClampTextPercent(110) == 100); CHECK(ClampTextPercent(0) == 100);
    CHECK(ClampPrintOrientation(2) == 2); CHECK(ClampPrintOrientation(3) == 0);
    CHECK(ClampPrintPaper(2) == 2); CHECK(ClampPrintPaper(-5) == 0);

    const SimpleDate today{ 2026, 11, 18 };
    std::vector<Entry> none;
    CHECK(EmailDateText(none, 0, today) == L"18 November 2026"); // no entries -> today
    std::vector<Entry> sat(2);
    sat[0].date = L"2026-11-14"; sat[1].date = L"2026-11-14"; // Saturday entered on Wednesday
    CHECK(EmailDateText(sat, 0, today) == L"14 November 2026");
    CHECK(EmailDateText(sat, 2, today) == L"14/11/2026");
    sat[1].date = L"2026-11-16"; // mixed (should not happen) -> latest
    CHECK(EmailDateText(sat, 1, today) == L"16 Nov 2026");
    std::vector<Entry> bad(1); bad[0].date = L"garbage"; // undated / unparseable -> today
    CHECK(EmailDateText(bad, 0, today) == L"18 November 2026");
}

TEST_CASE("v0.23.0: the email uses EmailWording::dateText when the caller supplies the sheet date") {
    SupplierGroup sg; sg.supplier = L"Acme";
    ProductGroup pg; pg.species = L"Bonito"; pg.prices.push_back({ 5.0, 10.0, 50.0 });
    sg.products.push_back(pg);
    EmailWording w;
    w.dateText = L"14 November 2026";
    CHECK(FormatEmailSubject(w.subject, w.dateText, sg.supplier) == L"Prices - 14 November 2026 - Acme");
    std::wstring body = BuildSupplierEmailBody(sg, 9, w.dateText, w);
    CHECK(body.find(L"(14 November 2026):") != std::wstring::npos);
}

TEST_SUITE("v0.24.0 supplier notes and multi-line signature") {
    static SupplierGroup OneLine() {
        SupplierGroup sg; sg.supplier = L"Harbour & Co";
        ProductGroup pg; pg.species = L"RIBB"; pg.prices.push_back({600.0, 2.0, 1200.0}); sg.products.push_back(pg);
        return sg;
    }
    TEST_CASE("CleanMultiline keeps line breaks, trims, drops blank ends, caps lines") {
        CHECK(CleanMultiline(L"  a  \r\n\r\n b\t\r\n\n", 30, 150) == L"a\n\nb");
        CHECK(CleanMultiline(L"\n\nx", 30, 150) == L"x");
        CHECK(CleanMultiline(L"", 30, 150) == L"");
        CHECK(CleanMultiline(L"1\n2\n3\n4", 2, 150) == L"1\n2");
        CHECK(CleanMultiline(std::wstring(300, L'x'), 30, 150).size() == 150);
        CHECK(CleanMultiline(L"a\x01" L"b", 30, 150) == L"a b");
    }
    TEST_CASE("multi-line values survive the one-line settings file") {
        const std::wstring sig = L"Jack\nCompany Pty Ltd\nPhone: 0400 000 000\\ext";
        const std::wstring stored = EncodeMultiline(sig);
        CHECK(stored.find(L'\n') == std::wstring::npos);
        CHECK(DecodeMultiline(stored) == sig);
        CHECK(DecodeMultiline(L"Regards\\Jack") == L"Regards\\Jack"); // an old value with a lone backslash is unchanged
        CHECK(DecodeMultiline(L"Kind regards,") == L"Kind regards,");
    }
    TEST_CASE("the signature can have several lines and falls back to the default") {
        auto w = ResolveEmailWording(L"", L"", L"Thanks,", EncodeMultiline(L"Jack\nCompany\n0400 000 000"));
        CHECK(w.signoff == L"Thanks,");
        CHECK(w.signature == L"Jack\nCompany\n0400 000 000");
        CHECK(SignOffBlock(w) == L"Thanks,\n\nJack\nCompany\n0400 000 000");
        CHECK(ResolveEmailWording(L"", L"", L"").signoff == L"Kind regards,");
        CHECK(ResolveEmailWording(L"", L"", L"").signature.empty());           // no signature unless one is typed
        CHECK(SignOffBlock(ResolveEmailWording(L"", L"", L"")) == L"Kind regards,");
        CHECK(ResolveEmailWording(L"", L"", L"", L"\\n\\n").signature.empty());
    }
    TEST_CASE("mailto body: notes under the table, above the signature, only blocks with text") {
        SupplierGroup sg = OneLine();
        EmailWording w; w.signoff = L"Thanks,"; w.signature = L"Jack\nCompany";
        w.notes.notSold = L"17X25 GREN\n12X30 RIBB";
        w.notes.down = L"1 SAW";
        const std::wstring body = BuildSupplierEmailBody(sg, 9, L"18 Nov", w);
        const size_t ns = body.find(L"NOT SOLD:\r\n17X25 GREN\r\n12X30 RIBB\r\n");
        const size_t dn = body.find(L"DOWN:\r\n1 SAW\r\n");
        const size_t sig = body.find(L"Thanks,\r\n\r\nJack\r\nCompany");
        REQUIRE(ns != std::wstring::npos); REQUIRE(dn != std::wstring::npos); REQUIRE(sig != std::wstring::npos);
        CHECK(body.find(L"UP:") == std::wstring::npos);          // nothing typed: no heading
        CHECK(body.find(L"RIBB\t") < ns);                        // after the table
        CHECK(ns < dn); CHECK(dn < sig);                          // order: not sold, down, signature
    }
    TEST_CASE("no notes: the body is exactly as before apart from the signature") {
        SupplierGroup sg = OneLine();
        const std::wstring body = BuildSupplierEmailBody(sg, 9, L"18 Nov");
        CHECK(body.find(L"NOT SOLD") == std::wstring::npos);
        CHECK(body.size() > 0);
        CHECK(body.substr(body.size() - 13) == L"Kind regards,");
    }
    TEST_CASE("a very long mailto email is still shortened, and keeps the notes and signature") {
        SupplierGroup sg; sg.supplier = L"A";
        for (int i = 0; i < 120; ++i) {
            ProductGroup pg; pg.species = L"SPECIES" + std::to_wstring(i); pg.prices.push_back({10.0, 5.0, 50.0}); sg.products.push_back(pg);
        }
        EmailWording w; w.notes.up = L"2 O/PERCH"; w.signoff = L"Jack";
        const std::wstring body = BuildSupplierEmailBody(sg, 9, L"18 Nov", w);
        CHECK(body.find(L"shortened") != std::wstring::npos);
        CHECK(body.find(L"UP:\r\n2 O/PERCH") != std::wstring::npos);
        CHECK(body.find(L"Jack") != std::wstring::npos);
    }
    TEST_CASE("eml / Gmail HTML: notes escaped, line breaks kept, signature on several lines") {
        SupplierGroup sg = OneLine();
        EmailWording w; w.signoff = L"Thanks,"; w.signature = L"Jack\nA & B";
        w.notes.notSold = L"1X16 <RIBB>\n21.4 kg GUMMY";
        auto r = BuildSupplierEml(sg, 9, L"18 Nov", L"", L"S", false, w);
        REQUIRE(r.ok);
        CHECK(r.content.find("<p><b>NOT SOLD:</b><br>\r\n1X16 &lt;RIBB&gt;<br>\r\n21.4 kg GUMMY</p>") != std::string::npos);
        CHECK(r.content.find("<p>Thanks,<br>\r\n<br>\r\nJack<br>\r\nA &amp; B</p>") != std::string::npos);
        CHECK(r.content.find("<b>UP:") == std::string::npos);
        CHECK(r.content.find("<b>DOWN:") == std::string::npos);
        CHECK(r.content.find("</table>") < r.content.find("NOT SOLD:"));
    }
    TEST_CASE("notes cannot inject headers or markup") {
        SupplierGroup sg = OneLine();
        EmailWording w; w.notes.up = L"x\r\nBcc: evil@example.com";
        auto r = BuildSupplierEml(sg, 9, L"18 Nov", L"a@b.com", L"S", false, w);
        REQUIRE(r.ok);
        const size_t headerEnd = r.content.find("\r\n\r\n");
        CHECK(r.content.substr(0, headerEnd).find("Bcc:") == std::string::npos); // only in the body, after the headers
    }
}
