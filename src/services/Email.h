// Email.h - building and launching one supplier's price email (services layer).
// v0.10.4 (main.cpp split stage 4a). The confirmation/progress message boxes
// that drive this one supplier at a time stay in main.cpp's DoEmailSuppliers()
// (forms layer).
#pragma once

#include "../PlatformWin.h"
#include <string>
#include "../core/FishBalanceCore.h"

// Today's date in the long form used in email subjects, e.g. "Wednesday 14 October 2026".
std::wstring TodayDateString();

// Opens a mailto: draft for `sg` in the default mail client. Returns false and
// fills *outError (user-facing text) if the address is invalid or Windows
// can't launch a mail client.
bool EmailSupplier(const std::wstring& email, const SupplierGroup& sg, std::wstring* outError = nullptr);

// v0.20.0: opens the supplier's email the way the Tools > Email method setting says.
//   Mailto: as EmailSupplier above.  Eml: writes an unsent HTML draft (.eml with a real table) into the temp folder
//   and opens it with the PC's .eml handler (classic Outlook).  Gmail: not built yet - behaves as Mailto.
bool EmailSupplierWith(EmailMode mode, const std::wstring& email, const SupplierGroup& sg, std::wstring* outError = nullptr);
