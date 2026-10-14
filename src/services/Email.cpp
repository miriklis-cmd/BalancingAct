// Email.cpp - moved unchanged out of main.cpp in v0.10.4 (split stage 4a).
#include "Email.h"

#include <shellapi.h>

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
    std::wstring body = BuildSupplierEmailBody(sg, st.wHour);

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
