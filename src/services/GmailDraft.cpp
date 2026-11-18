// GmailDraft.cpp - see GmailDraft.h. Windows only: Winsock loopback listener, WinHTTP, DPAPI, BCrypt.
// winsock2.h must come before anything that pulls in windows.h (PlatformWin.h), or the old winsock.h clashes.
#include <winsock2.h>
#include <ws2tcpip.h>
#include "GmailDraft.h"

#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include "../core/GmailCore.h"

#ifdef _MSC_VER
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#endif

namespace {

const char* kScope = "https://www.googleapis.com/auth/gmail.drafts.create";
const char* kDraftsUrl = "https://gmail.googleapis.com/gmail/v1/users/me/drafts";

std::string g_accessToken;      // cached for this run only
ULONGLONG g_accessExpiresAt = 0; // GetTickCount64() value

std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
std::string Narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring LocalDir() {
    std::wstring dir;
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)) && p) dir = p;
    if (p) CoTaskMemFree(p);
    if (dir.empty()) return dir;
    dir += L"\\BalancingAct";
    CreateDirectoryW(dir.c_str(), nullptr); // already existing is fine
    return dir;
}

bool ReadBinary(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    out.clear();
    char buf[8192] = {}; DWORD got = 0;
    while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0) out.append(buf, got);
    CloseHandle(h);
    return true;
}
bool WriteBinary(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = data.empty() || (WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr) && put == data.size());
    CloseHandle(h);
    return ok;
}

// ---- refresh token, encrypted for this Windows user (DPAPI) ---------------------------------------------------
std::wstring TokenPath() { std::wstring d = LocalDir(); return d.empty() ? d : d + L"\\gmail_refresh.bin"; }

bool SaveRefreshToken(const std::string& token) {
    std::wstring path = TokenPath();
    if (path.empty()) return false;
    DATA_BLOB in{ (DWORD)token.size(), (BYTE*)token.data() }, out{};
    if (!CryptProtectData(&in, L"BalancingAct Gmail", nullptr, nullptr, nullptr, 0, &out)) return false;
    bool ok = WriteBinary(path, std::string((const char*)out.pbData, out.cbData));
    LocalFree(out.pbData);
    return ok;
}
bool LoadRefreshToken(std::string& token) {
    std::wstring path = TokenPath();
    std::string blob;
    if (path.empty() || !ReadBinary(path, blob) || blob.empty()) return false;
    DATA_BLOB in{ (DWORD)blob.size(), (BYTE*)blob.data() }, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) return false;
    token.assign((const char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return !token.empty();
}

// ---- Google client file -----------------------------------------------------------------------------------------
bool LoadClient(HWND owner, gmail::ClientFile* client, std::wstring* err) {
    std::wstring local = LocalDir();
    std::vector<std::wstring> candidates;
    if (!local.empty()) candidates.push_back(local + L"\\client_secret.json");
    candidates.push_back(L"C:\\BalancingAct\\private\\client_secret.json");
    for (auto& p : candidates) {
        std::string json;
        if (ReadBinary(p, json)) {
            *client = gmail::ParseClientFile(json);
            if (client->ok) return true;
        }
    }
    MessageBoxW(owner,
        L"To create drafts in Gmail this PC needs the Google client file (client_secret.json) that you downloaded from the "
        L"Google Cloud console.\n\nClick OK and choose that file. It is copied to this PC's private app folder and is not "
        L"shared or saved with your data.",
        L"Gmail drafts - one-time setup", MB_OK | MB_ICONINFORMATION);
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Google client file (client_secret*.json)\0client_secret*.json\0All JSON files\0*.json\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Choose client_secret.json";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) { *err = L"No Google client file was chosen, so no Gmail draft was created."; return false; }
    std::string json;
    if (!ReadBinary(file, json) || !(*client = gmail::ParseClientFile(json)).ok) {
        *err = L"That file doesn't look like a Google client_secret.json (no client_id in it).";
        return false;
    }
    if (!local.empty()) WriteBinary(local + L"\\client_secret.json", json);
    return true;
}

// ---- HTTPS POST via WinHTTP -------------------------------------------------------------------------------------------
bool HttpsPost(const std::string& url, const std::string& contentType, const std::string& bearer, const std::string& body,
               int* status, std::string* response, std::wstring* err) {
    std::wstring wurl = Widen(url);
    URL_COMPONENTS uc{};
    wchar_t host[256] = {}, path[2048] = {};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { *err = L"Internal error: bad web address."; return false; }

    HINTERNET ses = WinHttpOpen(L"BalancingAct/0.21", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { *err = L"Couldn't start a web connection."; return false; }
    WinHttpSetTimeouts(ses, 10000, 10000, 30000, 30000);
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"POST", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    bool ok = false;
    if (req) {
        std::wstring hdr = L"Content-Type: " + Widen(contentType) + L"\r\n";
        if (!bearer.empty()) hdr += L"Authorization: Bearer " + Widen(bearer) + L"\r\n";
        if (WinHttpSendRequest(req, hdr.c_str(), (DWORD)-1L, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
            WinHttpReceiveResponse(req, nullptr)) {
            DWORD code = 0, sz = sizeof(code);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
            *status = (int)code;
            response->clear();
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                std::string chunk(avail, '\0'); DWORD got = 0;
                if (!WinHttpReadData(req, &chunk[0], avail, &got)) break;
                response->append(chunk.data(), got);
            }
            ok = true;
        }
    }
    if (!ok) *err = L"Couldn't reach Google (is this PC online?). Windows error " + std::to_wstring(GetLastError()) + L".";
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return ok;
}

std::string RandomUrlSafe(size_t nBytes) {
    std::string raw(nBytes, '\0');
    // The verifier and state protect the sign-in, so never fall back to predictable bytes: empty means "failed".
    if (BCryptGenRandom(nullptr, (PUCHAR)&raw[0], (ULONG)nBytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return std::string();
    return gmail::Base64Url(raw);
}

std::string TokenForm(const std::vector<std::pair<std::string, std::string>>& kv) {
    std::string s;
    for (auto& p : kv) { if (!s.empty()) s += '&'; s += p.first + "=" + gmail::UrlEncode(p.second); }
    return s;
}

// RAII: disables the owner window while we wait for the browser, and always re-enables it.
struct OwnerDisabler {
    HWND h; bool did = false;
    explicit OwnerDisabler(HWND w) : h(w) { if (h && IsWindowEnabled(h)) { EnableWindow(h, FALSE); did = true; } }
    ~OwnerDisabler() { if (did) { EnableWindow(h, TRUE); SetForegroundWindow(h); } }
};

// ---- interactive sign-in -------------------------------------------------------------------------------------------
GmailResult SignIn(HWND owner, const gmail::ClientFile& client, const std::wstring& loginHint, std::string* refreshOut,
                   std::wstring* err) {
    MessageBoxW(owner,
        (L"Your web browser will now open so you can sign in to Google. Sign in as the account where the drafts should be "
         L"saved" + (loginHint.empty() ? std::wstring() : L" (" + loginHint + L")") +
         L", then click Allow. Come back to this window afterwards.\n\nThe app is only asking for permission to ADD drafts - it "
         L"cannot read, send or delete your email.").c_str(),
        L"Gmail drafts - sign in", MB_OK | MB_ICONINFORMATION);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { *err = L"Couldn't start networking."; return GmailResult::Failed; }
    struct WsaCleanup { ~WsaCleanup() { WSACleanup(); } } wsaCleanup;

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) { *err = L"Couldn't open a local port for the sign-in reply."; return GmailResult::Failed; }
    struct SockClose { SOCKET s; ~SockClose() { closesocket(s); } } lsClose{ ls };
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int alen = sizeof(addr);
    if (bind(ls, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(ls, 4) != 0 || getsockname(ls, (sockaddr*)&addr, &alen) != 0) {
        *err = L"Couldn't open a local port for the sign-in reply."; return GmailResult::Failed;
    }
    const std::string redirect = "http://127.0.0.1:" + std::to_string(ntohs(addr.sin_port));
    const std::string verifier = RandomUrlSafe(48), state = RandomUrlSafe(16);
    if (verifier.empty() || state.empty()) { *err = L"Windows couldn't supply secure random numbers for the sign-in."; return GmailResult::Failed; }
    const std::string url = gmail::BuildAuthUrl(client.authUri, client.clientId, redirect, kScope,
                                                gmail::PkceChallenge(verifier), state, Narrow(loginHint));
    AllowSetForegroundWindow(ASFW_ANY);
    if ((INT_PTR)ShellExecuteW(nullptr, L"open", Widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL) <= 32) {
        *err = L"Couldn't open your web browser for the Google sign-in."; return GmailResult::Failed;
    }

    OwnerDisabler disabler(owner);
    const ULONGLONG deadline = GetTickCount64() + 3 * 60 * 1000;
    std::string code, gotError;
    while (code.empty() && gotError.empty()) {
        if (GetTickCount64() > deadline) { *err = L"The Google sign-in wasn't completed within 3 minutes, so no draft was created."; return GmailResult::Cancelled; }
        MSG m; // keep the (disabled) window repainting while we wait
        while (PeekMessageW(&m, nullptr, WM_PAINT, WM_PAINT, PM_REMOVE)) DispatchMessageW(&m);
        fd_set rd; FD_ZERO(&rd); FD_SET(ls, &rd);
        timeval tv{ 0, 100000 };
        if (select(0, &rd, nullptr, nullptr, &tv) <= 0) continue;
        SOCKET cs = accept(ls, nullptr, nullptr);
        if (cs == INVALID_SOCKET) continue;
        DWORD rto = 2000; setsockopt(cs, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rto, sizeof(rto));
        char buf[4096] = {}; int n = recv(cs, buf, sizeof(buf) - 1, 0);
        std::string line;
        if (n > 0) { buf[n] = 0; line = std::string(buf, buf + n); line = line.substr(0, line.find("\r\n")); }
        std::string c = gmail::QueryParamFromRequestLine(line, "code"), e = gmail::QueryParamFromRequestLine(line, "error");
        std::string st = gmail::QueryParamFromRequestLine(line, "state");
        const bool real = !c.empty() || !e.empty();
        std::string page = real ? (!c.empty() && st == state
            ? "<html><body style='font-family:sans-serif'><h3>BalancingAct: signed in.</h3><p>You can close this tab and go back to the app.</p></body></html>"
            : "<html><body style='font-family:sans-serif'><h3>BalancingAct: sign-in was not completed.</h3><p>You can close this tab.</p></body></html>")
            : "";
        std::string resp = real ? "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: " + std::to_string(page.size()) + "\r\n\r\n" + page
                                : "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        send(cs, resp.data(), (int)resp.size(), 0);
        closesocket(cs);
        if (real) {
            if (!e.empty()) gotError = e;
            else if (st != state) gotError = "state_mismatch";
            else code = c;
        }
    }
    if (!gotError.empty()) {
        *err = gotError == "access_denied" ? std::wstring(L"Google sign-in was cancelled, so no draft was created.")
                                           : L"Google sign-in failed (" + Widen(gotError) + L").";
        return gotError == "access_denied" ? GmailResult::Cancelled : GmailResult::Failed;
    }

    int status = 0; std::string resp;
    if (!HttpsPost(client.tokenUri, "application/x-www-form-urlencoded", "",
                   TokenForm({ {"code", code}, {"client_id", client.clientId}, {"client_secret", client.clientSecret},
                               {"redirect_uri", redirect}, {"grant_type", "authorization_code"}, {"code_verifier", verifier} }),
                   &status, &resp, err))
        return GmailResult::Failed;
    std::string access = gmail::JsonGetString(resp, "access_token"), refresh = gmail::JsonGetString(resp, "refresh_token");
    if (status != 200 || access.empty()) {
        *err = L"Google didn't accept the sign-in (" + Widen(gmail::JsonGetString(resp, "error")) + L" " +
               Widen(gmail::JsonGetString(resp, "error_description")) + L").";
        return GmailResult::Failed;
    }
    g_accessToken = access;
    g_accessExpiresAt = GetTickCount64() + (ULONGLONG)(gmail::JsonGetLong(resp, "expires_in", 3000) - 60) * 1000;
    if (!refresh.empty()) { SaveRefreshToken(refresh); *refreshOut = refresh; }
    return GmailResult::Ok;
}

// Returns Ok with g_accessToken valid, using the cache, then the saved refresh token, then (if needed) the browser sign-in.
GmailResult EnsureAccessToken(HWND owner, const gmail::ClientFile& client, const std::wstring& loginHint, bool forceRefresh,
                              std::wstring* err) {
    if (!forceRefresh && !g_accessToken.empty() && GetTickCount64() < g_accessExpiresAt) return GmailResult::Ok;
    std::string refresh;
    if (LoadRefreshToken(refresh)) {
        int status = 0; std::string resp; std::wstring netErr;
        if (!HttpsPost(client.tokenUri, "application/x-www-form-urlencoded", "",
                       TokenForm({ {"client_id", client.clientId}, {"client_secret", client.clientSecret},
                                   {"refresh_token", refresh}, {"grant_type", "refresh_token"} }), &status, &resp, &netErr)) {
            *err = netErr; return GmailResult::Failed; // offline etc.: don't throw away a good saved sign-in
        }
        std::string access = gmail::JsonGetString(resp, "access_token");
        if (status == 200 && !access.empty()) {
            g_accessToken = access;
            g_accessExpiresAt = GetTickCount64() + (ULONGLONG)(gmail::JsonGetLong(resp, "expires_in", 3000) - 60) * 1000;
            return GmailResult::Ok;
        }
        if (gmail::JsonGetString(resp, "error") != "invalid_grant") {
            *err = L"Google didn't accept the saved sign-in (" + Widen(gmail::JsonGetString(resp, "error")) + L").";
            return GmailResult::Failed;
        }
        GmailForgetSignIn(); // revoked or expired: fall through to a fresh sign-in
    }
    g_accessToken.clear();
    std::string newRefresh;
    return SignIn(owner, client, loginHint, &newRefresh, err);
}

} // namespace

bool GmailHasSavedSignIn() { std::string t; return LoadRefreshToken(t); }

bool GmailForgetSignIn() {
    g_accessToken.clear();
    g_accessExpiresAt = 0;
    std::wstring p = TokenPath();
    return p.empty() || DeleteFileW(p.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
}

GmailResult GmailCreateDraft(HWND owner, const std::string& rfc2822, const std::wstring& loginHint, std::wstring* outError) {
    std::wstring dummy; if (!outError) outError = &dummy;
    gmail::ClientFile client;
    if (!LoadClient(owner, &client, outError)) return GmailResult::Cancelled;

    const std::string body = gmail::BuildDraftRequestJson(rfc2822);
    for (int attempt = 0; attempt < 2; attempt++) {
        GmailResult r = EnsureAccessToken(owner, client, loginHint, attempt > 0, outError);
        if (r != GmailResult::Ok) return r;
        int status = 0; std::string resp; std::wstring netErr;
        if (!HttpsPost(kDraftsUrl, "application/json", g_accessToken, body, &status, &resp, &netErr)) { *outError = netErr; return GmailResult::Failed; }
        if (status == 200) return GmailResult::Ok;
        if (status == 401 && attempt == 0) { g_accessToken.clear(); continue; } // token expired early: refresh once and retry
        *outError = L"Gmail refused the draft (HTTP " + std::to_wstring(status) + L"): " +
                    Widen(gmail::JsonGetString(resp, "message"));
        return GmailResult::Failed;
    }
    return GmailResult::Failed;
}
