// Email.cpp - moved unchanged out of main.cpp in v0.10.4 (split stage 4a).
#include "Email.h"

#include <shellapi.h>
#include "../data/FishBalanceWin32IO.h" // WriteFileAtomicUtf8 (v0.20.0 .eml draft file)

// TodayDateString keeps its original no-argument signature here (nothing
// else in main.cpp needs to change) but delegates to the portable,
// date-parameterized version in FishBalanceCore.h for the actual logic,
// reading the current time via GetLocalTime at this one Win32 boundary.
// (A no-argument GreetingForNow() used to sit here too, wrapping
// GreetingForHour() the same way - removed as dead code, v0.9.43: the
// actual email body is built by BuildSupplierEmailBody() in
// FishBalanceCore.h, which takes the current hour as an explicit
// parameter from EmailSupplier() below and calls GreetingForHour()
// directly, so GreetingForNow() had no remaining caller.)
std::wstring TodayDateString() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return FormatLongDate(SimpleDate{ st.wYear, st.wMonth, st.wDay });
}

// Phase 2 / F13 (2026-10-02): mailto construction now goes through
// BuildMailtoUri() (FishBalanceCore.h), which percent-encodes the
// recipient/subject/body independently and refuses outright (rather than
// best-effort-encoding) a non-empty stored address that fails
// LooksLikeEmail() - see that function's comment. outError, when non-null,
// is set to a short, user-facing reason on any failure (an invalid stored
// address, or ShellExecuteW failing to launch a mail client).
bool EmailSupplier(const std::wstring& email, const SupplierGroup& sg, std::wstring* outError) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    // v0.9.58: supplier name is always in the subject (was only added when
    // there was no To address). Safe for any supplier text: BuildMailtoUri
    // percent-encodes the subject independently of the other components.
    std::wstring subject = L"Prices - " + TodayDateString() + L" - " + sg.supplier;
    std::wstring body = BuildSupplierEmailBody(sg, st.wHour, TodayDateString());

    MailtoBuildResult built = BuildMailtoUri(email, subject, body);
    if (!built.ok) {
        if (outError) *outError = built.error;
        return false;
    }

    // Windows can prevent a window launched from a background/already-idle
    // action from stealing focus, so it opens minimized or behind other
    // windows instead of visibly popping up - explicitly allowing the next
    // process to set itself as foreground avoids that for this case.
    AllowSetForegroundWindow(ASFW_ANY);

    HINSTANCE result = ShellExecuteW(nullptr, L"open", built.uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    // Per MSDN, a return value > 32 means success; anything else is an error code.
    if ((INT_PTR)result <= 32) {
        if (outError) *outError = L"Windows couldn't open an email draft. This usually means there's no "
            L"default email app set up on this PC yet. Go to Windows Settings > Apps > Default apps, "
            L"search for 'Mail' (or MAILTO), and set a default (e.g. Outlook or the Windows Mail app).";
        return false;
    }
    return true;
}

// v0.20.0: .eml draft. Written to %TEMP%\BalancingAct_emails\ with a name made only of safe characters, then
// opened by the shell. Each supplier gets its own file; the files are small and left for Windows' temp cleanup.
static std::wstring SafeFileStem(const std::wstring& s) {
    std::wstring o;
    for (wchar_t ch : s) o += (iswalnum(ch) && ch < 0x80) ? ch : L'_';
    if (o.size() > 40) o.resize(40);
    return o.empty() ? std::wstring(L"supplier") : o;
}

bool EmailSupplierWith(EmailMode mode, const std::wstring& email, const SupplierGroup& sg, std::wstring* outError) {
    if (mode != EmailMode::Eml) return EmailSupplier(email, sg, outError);

    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring subject = L"Prices - " + TodayDateString() + L" - " + sg.supplier;
    EmlBuildResult built = BuildSupplierEml(sg, st.wHour, TodayDateString(), email, subject);
    if (!built.ok) { if (outError) *outError = built.error; return false; }

    wchar_t tmp[MAX_PATH] = {};
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH) { if (outError) *outError = L"Couldn't find the temporary folder to write the email draft."; return false; }
    std::wstring dir = std::wstring(tmp) + L"BalancingAct_emails";
    CreateDirectoryW(dir.c_str(), nullptr); // already existing is fine
    wchar_t stamp[32];
    swprintf(stamp, 32, L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::wstring path = dir + L"\\Prices_" + SafeFileStem(sg.supplier) + L"_" + stamp + L".eml";

    std::wstring werr;
    if (!WriteFileAtomicUtf8(path, built.content, &werr)) {
        if (outError) *outError = L"Couldn't write the email draft file: " + werr;
        return false;
    }
    AllowSetForegroundWindow(ASFW_ANY);
    HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        if (outError) *outError = L"Windows couldn't open the email draft file. The .eml file type needs to be opened by "
            L"Outlook (classic). Choose 'Open in my mail program (mailto)' under Tools > Email method instead, "
            L"or set Outlook as the default for .eml files in Windows Settings > Apps > Default apps.";
        return false;
    }
    return true;
}
