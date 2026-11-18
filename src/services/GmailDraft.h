// GmailDraft.h - creates drafts in Gmail through the Gmail API (v0.21.0). Services layer.
//
// Sign-in: the standard "installed app" OAuth flow with PKCE - the default browser opens Google's sign-in page, Google
// redirects back to a loopback address on this PC, and the app swaps the one-time code for a refresh token. The
// refresh token is stored encrypted for the current Windows user (DPAPI) in %LOCALAPPDATA%\BalancingAct\, never in
// the shared data folder, so every PC/user signs in once. Only the narrow gmail.drafts.create permission is requested:
// the app can add drafts, it cannot read, send or delete mail.
//
// The Google "client" (client_secret.json, downloaded from the Cloud console) is looked for in
// %LOCALAPPDATA%\BalancingAct\ and C:\BalancingAct\private\; if neither has it the user is asked to pick the file
// once and it is copied into %LOCALAPPDATA%\BalancingAct\. It is never part of the source or the shared data folder.
#pragma once

#include "../PlatformWin.h"
#include <string>

enum class GmailResult { Ok, Cancelled, Failed };

// Creates one draft from a complete RFC 2822 message (UTF-8). Signs in first if this PC has no usable saved sign-in.
// `owner` is disabled while the browser sign-in is pending. `loginHint` (may be empty) pre-selects the Google account.
// On Cancelled/Failed *outError holds a user-facing reason.
GmailResult GmailCreateDraft(HWND owner, const std::string& rfc2822, const std::wstring& loginHint, std::wstring* outError);

// Forgets this PC's saved sign-in (deletes the stored refresh token). Does not touch the Google account itself.
bool GmailForgetSignIn();
bool GmailHasSavedSignIn();
