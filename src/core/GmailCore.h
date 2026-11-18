// GmailCore.h - the pure (no Windows, no network) parts of the Gmail draft feature (v0.21.0): PKCE, URL building,
// a tiny JSON reader for Google's replies, and the draft request body. Everything here is unit tested; the Win32
// side (sockets, WinHTTP, DPAPI) lives in src/services/GmailDraft.cpp.
#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace gmail {

// ---- SHA-256 (for the PKCE code challenge) --------------------------------------------------------------------
inline std::string Sha256(const std::string& msg) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,
        0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
        0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,
        0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,
        0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
        0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::string m = msg;
    const uint64_t bitLen = (uint64_t)msg.size() * 8;
    m += '\x80'; // (a (char)0x80 cast trips MSVC C4310)
    while (m.size() % 64 != 56) m += (char)0;
    for (int i = 7; i >= 0; i--) m += (char)((bitLen >> (i * 8)) & 0xff);
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)(unsigned char)m[off + i * 4] << 24) | ((uint32_t)(unsigned char)m[off + i * 4 + 1] << 16) |
                   ((uint32_t)(unsigned char)m[off + i * 4 + 2] << 8) | (uint32_t)(unsigned char)m[off + i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::string out;
    for (int i = 0; i < 8; i++) for (int j = 3; j >= 0; j--) out += (char)((h[i] >> (j * 8)) & 0xff);
    return out;
}

// ---- base64url (no padding), as PKCE and the Gmail "raw" field require --------------------------------------------
inline std::string Base64Url(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
        out += t[(v >> 18) & 63]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
    }
    if (i + 1 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        out += t[(v >> 18) & 63]; out += t[(v >> 12) & 63];
    } else if (i + 2 == in.size()) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
        out += t[(v >> 18) & 63]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63];
    }
    return out;
}

// PKCE: challenge = base64url(sha256(verifier)).
inline std::string PkceChallenge(const std::string& verifier) { return Base64Url(Sha256(verifier)); }

// ---- percent-encoding for query strings / form bodies (UTF-8 bytes in, ASCII out) -------------------------------
inline std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}
inline std::string UrlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') o += ' ';
        else if (s[i] == '%' && i + 2 < s.size() + 0 && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2;
        } else o += s[i];
    }
    return o;
}

inline std::string BuildAuthUrl(const std::string& authUri, const std::string& clientId, const std::string& redirectUri,
                                const std::string& scope, const std::string& challenge, const std::string& state,
                                const std::string& loginHint) {
    std::string u = authUri + "?client_id=" + UrlEncode(clientId) + "&redirect_uri=" + UrlEncode(redirectUri) +
                    "&response_type=code&scope=" + UrlEncode(scope) + "&code_challenge=" + challenge +
                    "&code_challenge_method=S256&state=" + UrlEncode(state) + "&access_type=offline&prompt=consent";
    if (!loginHint.empty()) u += "&login_hint=" + UrlEncode(loginHint);
    return u;
}

// The loopback listener receives "GET /?code=...&state=... HTTP/1.1". Returns the value of `key` in that request line's query, or "".
inline std::string QueryParamFromRequestLine(const std::string& requestLine, const std::string& key) {
    size_t sp1 = requestLine.find(' ');
    if (sp1 == std::string::npos) return "";
    size_t sp2 = requestLine.find(' ', sp1 + 1);
    std::string target = requestLine.substr(sp1 + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1);
    size_t q = target.find('?');
    if (q == std::string::npos) return "";
    std::string qs = target.substr(q + 1);
    size_t pos = 0;
    while (pos <= qs.size()) {
        size_t amp = qs.find('&', pos);
        std::string pair = qs.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos && pair.substr(0, eq) == key) return UrlDecode(pair.substr(eq + 1));
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return "";
}

// ---- a tiny JSON reader: finds the first  "key": "string"  or  "key": number  anywhere in the text ---------------
inline size_t FindJsonValueStart(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t p = 0;
    while ((p = json.find(needle, p)) != std::string::npos) {
        size_t q = p + needle.size();
        while (q < json.size() && (json[q] == ' ' || json[q] == '\t' || json[q] == '\r' || json[q] == '\n')) q++;
        if (q < json.size() && json[q] == ':') {
            q++;
            while (q < json.size() && (json[q] == ' ' || json[q] == '\t' || json[q] == '\r' || json[q] == '\n')) q++;
            return q;
        }
        p += needle.size();
    }
    return std::string::npos;
}
inline std::string JsonGetString(const std::string& json, const std::string& key) {
    size_t q = FindJsonValueStart(json, key);
    if (q == std::string::npos || q >= json.size() || json[q] != '"') return "";
    std::string o;
    for (size_t i = q + 1; i < json.size(); i++) {
        char c = json[i];
        if (c == '"') return o;
        if (c == '\\' && i + 1 < json.size()) {
            char n = json[++i];
            switch (n) {
                case 'n': o += '\n'; break; case 'r': o += '\r'; break; case 't': o += '\t'; break;
                case 'u': { // \uXXXX - only the ASCII range matters for tokens and ids; anything else becomes '?'
                    if (i + 4 < json.size()) { unsigned v = (unsigned)std::stoul(json.substr(i + 1, 4), nullptr, 16); o += v < 0x80 ? (char)v : '?'; i += 4; }
                    break; }
                default: o += n; break; // \" \\ \/
            }
        } else o += c;
    }
    return "";
}
inline long JsonGetLong(const std::string& json, const std::string& key, long fallback = 0) {
    size_t q = FindJsonValueStart(json, key);
    if (q == std::string::npos || q >= json.size()) return fallback;
    size_t e = q;
    while (e < json.size() && ((json[e] >= '0' && json[e] <= '9') || json[e] == '-')) e++;
    if (e == q) return fallback;
    return std::stol(json.substr(q, e - q));
}

// Google's downloaded client file: {"installed":{"client_id":...,"client_secret":...,"auth_uri":...,"token_uri":...}}
struct ClientFile { bool ok = false; std::string clientId, clientSecret, authUri, tokenUri; };
inline ClientFile ParseClientFile(const std::string& json) {
    ClientFile c;
    c.clientId = JsonGetString(json, "client_id");
    c.clientSecret = JsonGetString(json, "client_secret");
    c.authUri = JsonGetString(json, "auth_uri");
    c.tokenUri = JsonGetString(json, "token_uri");
    if (c.authUri.empty()) c.authUri = "https://accounts.google.com/o/oauth2/v2/auth";
    if (c.tokenUri.empty()) c.tokenUri = "https://oauth2.googleapis.com/token";
    c.ok = !c.clientId.empty();
    return c;
}

// {"message":{"raw":"<base64url of the whole RFC 2822 message>"}} - base64url has no characters that need JSON escaping.
inline std::string BuildDraftRequestJson(const std::string& rfc2822Message) {
    return "{\"message\":{\"raw\":\"" + Base64Url(rfc2822Message) + "\"}}";
}

} // namespace gmail
