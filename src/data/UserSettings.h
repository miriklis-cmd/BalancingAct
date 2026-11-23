// UserSettings.h - the user-facing settings in settings.txt (Tools > Settings), for the WinUI app (v0.23.0 / WinUI step 14).
// The Win32 app keeps these in AppSettings (AppData.h) and rewrites the whole file; this header reads and writes the SAME
// keys one at a time through autosave_shared (so the Win32 window-position keys are never touched). Header-only.
#pragma once

#include <string>

#include "AutosaveShared.h"
#include "../core/FishBalanceCore.h"
#include "../services/PrintJob.h"

struct UserSettings {
    EmailMode emailMode = EmailMode::Mailto;       // EMAILMODE
    std::wstring gmailAccount;                     // GMAILACCOUNT
    std::wstring emailSubject, emailIntro, emailSignoff, emailSignature; // (signature: v0.24.0, EMAILSIGNATURE)  // EMAILSUBJECT / EMAILINTRO / EMAILSIGNOFF (blank = standard wording)
    int backupMinutes = 3;                         // BACKUPMINUTES (1-60)
    int backupKeep = 50;                           // BACKUPKEEP (10-500)
    int outlierSensitivity = 1;                    // OUTLIERSENS 0 relaxed, 1 normal, 2 strict
    int dateStyle = 0;                             // DATESTYLE 0 long, 1 short month, 2 dd/mm/yyyy
    int textPercent = 100;                         // TEXTSIZE 100 / 125 / 150
    std::wstring printer;                          // PRINTER (blank = Windows default)
    int printOrientation = 0;                      // PRINTORIENT 0 printer default, 1 portrait, 2 landscape
    int printPaper = 0;                            // PRINTPAPER 0 printer default, 1 A4, 2 A3
};

// Reads every key from <dir>\settings.txt; a missing file/key gives the default, an out-of-range value is clamped.
inline UserSettings LoadUserSettings(const std::wstring& dir) {
    using autosave_shared::ReadSetting;
    UserSettings s;
    s.emailMode = ParseEmailMode(ReadSetting(dir, L"EMAILMODE"));
    s.gmailAccount = ReadSetting(dir, L"GMAILACCOUNT");
    s.emailSubject = ReadSetting(dir, L"EMAILSUBJECT");
    s.emailIntro = ReadSetting(dir, L"EMAILINTRO");
    s.emailSignoff = ReadSetting(dir, L"EMAILSIGNOFF");
    s.emailSignature = ReadSetting(dir, L"EMAILSIGNATURE");
    auto num = [&](const wchar_t* key, int def) {
        const std::wstring v = ReadSetting(dir, key);
        return v.empty() ? def : (int)wcstol(v.c_str(), nullptr, 10);
    };
    s.backupMinutes = ClampBackupMinutes(num(L"BACKUPMINUTES", 3));
    s.backupKeep = ClampBackupKeep(num(L"BACKUPKEEP", 50));
    s.outlierSensitivity = ClampOutlierSensitivity(num(L"OUTLIERSENS", 1));
    s.dateStyle = ClampDateStyle(num(L"DATESTYLE", 0));
    s.textPercent = ClampTextPercent(num(L"TEXTSIZE", 100));
    s.printer = ReadSetting(dir, L"PRINTER");
    s.printOrientation = ClampPrintOrientation(num(L"PRINTORIENT", 0));
    s.printPaper = ClampPrintPaper(num(L"PRINTPAPER", 0));
    return s;
}

// Writes every key (blank wording / printer are written as blank values, which read back as "standard" / "default").
inline void SaveUserSettings(const std::wstring& dir, const UserSettings& s) {
    using autosave_shared::WriteSetting;
    WriteSetting(dir, L"EMAILMODE", EmailModeToString(s.emailMode));
    WriteSetting(dir, L"GMAILACCOUNT", s.gmailAccount);
    WriteSetting(dir, L"EMAILSUBJECT", s.emailSubject);
    WriteSetting(dir, L"EMAILINTRO", s.emailIntro);
    WriteSetting(dir, L"EMAILSIGNOFF", s.emailSignoff);
    WriteSetting(dir, L"EMAILSIGNATURE", s.emailSignature);
    WriteSetting(dir, L"BACKUPMINUTES", std::to_wstring(ClampBackupMinutes(s.backupMinutes)));
    WriteSetting(dir, L"BACKUPKEEP", std::to_wstring(ClampBackupKeep(s.backupKeep)));
    WriteSetting(dir, L"OUTLIERSENS", std::to_wstring(ClampOutlierSensitivity(s.outlierSensitivity)));
    WriteSetting(dir, L"DATESTYLE", std::to_wstring(ClampDateStyle(s.dateStyle)));
    WriteSetting(dir, L"TEXTSIZE", std::to_wstring(ClampTextPercent(s.textPercent)));
    WriteSetting(dir, L"PRINTER", s.printer);
    WriteSetting(dir, L"PRINTORIENT", std::to_wstring(ClampPrintOrientation(s.printOrientation)));
    WriteSetting(dir, L"PRINTPAPER", std::to_wstring(ClampPrintPaper(s.printPaper)));
}

inline EmailWording EmailWordingFromSettings(const UserSettings& s, const std::vector<Entry>& entries, const SimpleDate& today) {
    EmailWording w = ResolveEmailWording(s.emailSubject, s.emailIntro, s.emailSignoff, s.emailSignature);
    w.dateText = EmailDateText(entries, s.dateStyle, today); // the sheet date, not the day it is sent
    return w;
}

inline PrintDefaults PrintDefaultsFromSettings(const UserSettings& s) {
    PrintDefaults d;
    d.printer = s.printer;
    d.orientation = ClampPrintOrientation(s.printOrientation);
    d.paper = ClampPrintPaper(s.printPaper);
    return d;
}
