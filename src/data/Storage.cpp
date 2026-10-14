// Storage.cpp - where the app's own files live and how settings, recent
// files, supplier emails and rolling-backup housekeeping are read/written
// (data layer). Moved out of main.cpp unchanged in v0.10.3 (split stage 3),
// apart from the two small signature changes noted in Storage.h.
#include "Storage.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "FishBalanceWin32IO.h"

// Test-automation refactor (v0.9.51): overridable so integration tests can
// point every path this app builds (autosave.fbd, settings.txt, recent.txt,
// emails.txt, backups\, history\) at an isolated temporary directory instead
// of the real folder next to the exe - never touching production data. Empty
// (the default) means "use the real exe directory," exactly the original
// behavior; production code never calls SetExeDirOverrideForTests, so this
// is a pure addition, not a behavior change, for the shipped application.
static std::wstring g_exeDirOverrideForTests;
void SetExeDirOverrideForTests(const std::wstring& dir) { g_exeDirOverrideForTests = dir; }

std::wstring GetExeDir() {
    if (!g_exeDirOverrideForTests.empty()) return g_exeDirOverrideForTests;
    // v0.9.66 (audit F23): the old fixed MAX_PATH buffer silently truncated
    // long install paths (GetModuleFileNameW returns the buffer size when it
    // truncated). Grow the buffer until the whole path fits; fall back to "."
    // (current directory) only if the call fails outright.
    std::wstring p;
    for (DWORD cap = MAX_PATH; cap <= 32768; cap *= 2) {
        std::vector<wchar_t> buf(cap);
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), cap);
        if (n == 0) break;                       // failed outright
        if (n < cap) { p.assign(buf.data(), n); break; } // fit with room for the terminator
        // n == cap: truncated, retry with a bigger buffer
    }
    if (p.empty()) return L".";
    size_t pos = p.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"." : p.substr(0, pos);
}

// Phase 1 / F1 follow-up (2026-09-30, post-review): the single canonical
// path for autosave.fbd, so BuildFbdSaveContent()/SaveToFile() can tell
// whether a given save IS the autosave write without duplicating this
// string construction at every call site.
std::wstring AutosavePath() {
    return GetExeDir() + L"\\autosave.fbd";
}

std::wstring SettingsPath() { return GetExeDir() + L"\\settings.txt"; }

void LoadSettings(int dpi) {
    std::vector<std::wstring> lines;
    bool foundFile = ReadAllLines(SettingsPath(), lines);
    for (auto& line : lines) {
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = line.substr(0, eq), val = line.substr(eq + 1);
        if (key == L"X") g_settings.x = _wtoi(val.c_str());
        else if (key == L"Y") g_settings.y = _wtoi(val.c_str());
        else if (key == L"W") g_settings.w = _wtoi(val.c_str());
        else if (key == L"H") g_settings.h = _wtoi(val.c_str());
        else if (key == L"MAX") g_settings.maximized = (val == L"1");
        else if (key == L"LASTFILE") g_settings.lastFile = val;
    }
    if (g_settings.w < 640) g_settings.w = 1050;
    if (g_settings.h < 480) g_settings.h = 720;
    // First run (no settings.txt yet): scale the default size for the
    // current display's DPI, since WM_CREATE (where layout is DPI-scaled)
    // doesn't run until after this size has already been used to create
    // the window.
    if (!foundFile) {
        g_settings.w = MulDiv(g_settings.w, dpi, 96);
        g_settings.h = MulDiv(g_settings.h, dpi, 96);
    }
}

// Writes settings.txt from already-gathered window placement (the caller,
// SaveSettings() in main.cpp, reads the placement from the live window -
// that part is UI and stays in the forms layer).
void SaveSettingsFile(int x, int y, int w, int h, bool maximized, const std::wstring& lastFile) {
    std::string content;
    auto writeLine = [&](const std::wstring& l) {
        content += WToUtf8(l) + "\n";
    };
    writeLine(L"X=" + std::to_wstring(x));
    writeLine(L"Y=" + std::to_wstring(y));
    writeLine(L"W=" + std::to_wstring(w));
    writeLine(L"H=" + std::to_wstring(h));
    writeLine(std::wstring(L"MAX=") + (maximized ? L"1" : L"0"));
    writeLine(L"LASTFILE=" + lastFile);

    // Failure here stays silent (as it always has been) - losing
    // settings.txt only means window position/last-file aren't remembered
    // next launch, not any loss of actual business data. Still a genuine
    // atomic+checked write now, not a truncate-and-hope.
    WriteFileAtomicUtf8(SettingsPath(), content);
}

std::wstring RecentFilesPath() { return GetExeDir() + L"\\recent.txt"; }

void SaveRecentFiles() {
    std::string content;
    for (auto& p : g_recentFiles) content += WToUtf8(p) + "\n";
    // Failure stays silent, same reasoning as SaveSettings() - losing this
    // just means the Recent Files menu doesn't remember entries, not any
    // loss of actual business data.
    WriteFileAtomicUtf8(RecentFilesPath(), content);
}

void LoadRecentFiles() {
    std::vector<std::wstring> lines;
    ReadAllLines(RecentFilesPath(), lines);
    g_recentFiles.clear();
    for (auto& l : lines) if (!l.empty()) g_recentFiles.push_back(l);
}

std::wstring EmailsPath() { return GetExeDir() + L"\\emails.txt"; }

void LoadSupplierEmails() {
    std::vector<std::wstring> lines;
    ReadAllLines(EmailsPath(), lines);
    g_supplierEmails.clear();
    for (auto& l : lines) {
        size_t pos = l.find(L'|');
        if (pos == std::wstring::npos) continue;
        std::wstring sup = l.substr(0, pos);
        std::wstring email = l.substr(pos + 1);
        // Phase 1 completeness follow-up (2026-09-30, v0.9.50): companion-
        // file content sharing the Supplier-name field gets the same bound
        // applied to that field everywhere else (kMaxSupplierSpeciesLength).
        // emails.txt has never had transactional/whole-file-reject semantics
        // like .fbd loading - a line this app itself could never have
        // written (both fields are UI-limited at entry, see EM_LIMITTEXT
        // above) is simply skipped, same as the existing empty-field/no-pipe
        // cases just above, rather than failing the entire load.
        if (sup.size() > kMaxSupplierSpeciesLength || email.size() > kMaxSupplierSpeciesLength) continue;
        // Phase 2 / F13 (2026-10-02): emails.txt is untrusted input - a
        // hand-edited or otherwise-tampered file could otherwise smuggle in
        // an address containing characters LooksLikeEmail() would have
        // rejected at entry time (see EmailSupplier/BuildMailtoUri). Skip
        // (don't load) any address that doesn't pass the same check used at
        // entry time, same "skip the bad line, don't fail the whole load"
        // policy as the other checks in this loop.
        if (!email.empty() && !LooksLikeEmail(email)) continue;
        if (!sup.empty() && !email.empty()) g_supplierEmails[sup] = email;
    }
}

bool SaveSupplierEmails() {
    std::string content;
    for (auto& kv : g_supplierEmails) content += WToUtf8(kv.first + L"|" + kv.second) + "\n";
    return WriteFileAtomicUtf8(EmailsPath(), content);
}

std::wstring BackupDir() { return GetExeDir() + L"\\backups"; }

// Finalize Day (ROADMAP.md item 3) locked-in snapshots, one per business
// day, named <date>.fbd (e.g. 2026-09-20.fbd). A separate folder from
// backups\ - these are deliberate, permanent records, not rolling/pruned
// safety-net copies.
std::wstring HistoryDir() { return GetExeDir() + L"\\history"; }

// Deletes the oldest backup snapshots once there are more than kMaxBackups
// of them. Filenames embed a sortable YYYYMMDD_HHMMSS timestamp (see
// WriteBackupSnapshot), so a plain lexicographic sort is also a
// chronological sort - oldest names sort first.
void PruneOldBackups() {
    std::wstring dir = BackupDir();
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\backup_*.fbd").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            names.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if ((int)names.size() <= kMaxBackups) return;
    std::sort(names.begin(), names.end());
    size_t toDelete = names.size() - (size_t)kMaxBackups;
    for (size_t i = 0; i < toDelete; i++) {
        _wremove((dir + L"\\" + names[i]).c_str());
    }
}

// Extracts a filesystem-safe label for which file was open when a backup
// snapshot was taken - just the base filename without path or extension
// (e.g. "21112" for "...\21112.fbd"), or "unsaved" if no named file is
// open yet (working from autosave.fbd only). Always safe to use directly
// in a filename: it comes from a path that was itself already a valid,
// successfully-opened Windows filename.
std::wstring CurrentFileLabelForBackup() {
    if (g_currentFile.empty()) return L"unsaved";
    size_t slash = g_currentFile.find_last_of(L"\\/");
    std::wstring name = (slash == std::wstring::npos) ? g_currentFile : g_currentFile.substr(slash + 1);
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) name = name.substr(0, dot);
    return name.empty() ? L"unsaved" : name;
}
