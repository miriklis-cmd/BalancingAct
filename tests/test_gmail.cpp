#include "doctest_setup.h"
#include "doctest.h"
#include "../src/core/GmailCore.h"

static std::string Hex(const std::string& b) {
    static const char* h = "0123456789abcdef"; std::string o;
    for (unsigned char c : b) { o += h[c >> 4]; o += h[c & 15]; }
    return o;
}

TEST_SUITE("GmailCore (v0.21.0)") {
    TEST_CASE("SHA-256 known vectors") {
        CHECK(Hex(gmail::Sha256("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        CHECK(Hex(gmail::Sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        CHECK(Hex(gmail::Sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    }
    TEST_CASE("PKCE challenge matches the RFC 7636 example") {
        CHECK(gmail::PkceChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
    }
    TEST_CASE("base64url has no padding and uses - and _") {
        CHECK(gmail::Base64Url("f") == "Zg");
        CHECK(gmail::Base64Url("fo") == "Zm8");
        CHECK(gmail::Base64Url("foo") == "Zm9v");
        CHECK(gmail::Base64Url(std::string("\xfb\xff\xfe")) == "-__-");
    }
    TEST_CASE("url encode / decode") {
        CHECK(gmail::UrlEncode("a b&c=d/e") == "a%20b%26c%3Dd%2Fe");
        CHECK(gmail::UrlDecode("a%20b%26c+d") == "a b&c d");
    }
    TEST_CASE("auth URL carries PKCE, state, offline access and an optional login hint") {
        auto u = gmail::BuildAuthUrl("https://a/auth", "cid", "http://127.0.0.1:5", "scope x", "CH", "ST", "accounts@x.com");
        CHECK(u.find("https://a/auth?client_id=cid&redirect_uri=http%3A%2F%2F127.0.0.1%3A5") == 0);
        CHECK(u.find("&code_challenge=CH&code_challenge_method=S256&state=ST") != std::string::npos);
        CHECK(u.find("scope=scope%20x") != std::string::npos);
        CHECK(u.find("access_type=offline") != std::string::npos);
        CHECK(u.find("&login_hint=accounts%40x.com") != std::string::npos);
        CHECK(gmail::BuildAuthUrl("a", "c", "r", "s", "h", "t", "").find("login_hint") == std::string::npos);
    }
    TEST_CASE("query params from the loopback request line") {
        std::string l = "GET /?state=S1&code=4%2F0AbC-d HTTP/1.1";
        CHECK(gmail::QueryParamFromRequestLine(l, "code") == "4/0AbC-d");
        CHECK(gmail::QueryParamFromRequestLine(l, "state") == "S1");
        CHECK(gmail::QueryParamFromRequestLine(l, "error") == "");
        CHECK(gmail::QueryParamFromRequestLine("GET /?error=access_denied HTTP/1.1", "error") == "access_denied");
        CHECK(gmail::QueryParamFromRequestLine("garbage", "code") == "");
    }
    TEST_CASE("JSON reader: strings, escapes, numbers, whitespace, missing keys") {
        std::string j = "{\n \"access_token\" : \"ya29.a\\/b\",\n \"expires_in\": 3599, \"refresh_token\":\"1//x\\u0041\"}";
        CHECK(gmail::JsonGetString(j, "access_token") == "ya29.a/b");
        CHECK(gmail::JsonGetString(j, "refresh_token") == "1//xA");
        CHECK(gmail::JsonGetLong(j, "expires_in") == 3599);
        CHECK(gmail::JsonGetString(j, "nope") == "");
        CHECK(gmail::JsonGetLong(j, "nope", 7) == 7);
        CHECK(gmail::JsonGetString("{\"error\":\"invalid_grant\"}", "error") == "invalid_grant");
    }
    TEST_CASE("Google's client file parses; missing urls get Google's defaults") {
        auto c = gmail::ParseClientFile("{\"installed\":{\"client_id\":\"123.apps.googleusercontent.com\",\"client_secret\":\"sec\",\"auth_uri\":\"https://accounts.google.com/o/oauth2/auth\",\"token_uri\":\"https://oauth2.googleapis.com/token\"}}");
        CHECK(c.ok); CHECK(c.clientId == "123.apps.googleusercontent.com"); CHECK(c.clientSecret == "sec");
        CHECK(c.tokenUri == "https://oauth2.googleapis.com/token");
        CHECK(gmail::ParseClientFile("{\"installed\":{\"client_id\":\"x\"}}").tokenUri == "https://oauth2.googleapis.com/token");
        CHECK_FALSE(gmail::ParseClientFile("not json").ok);
    }
    TEST_CASE("draft request body wraps the base64url message") {
        CHECK(gmail::BuildDraftRequestJson("foo") == "{\"message\":{\"raw\":\"Zm9v\"}}");
    }
}
