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

std::wstring GetExeDir();

// v0.13.1 (Jack, 2026-11-06): emails.txt and names.txt are shared reference data, not per-exe settings, so they live
// in one fixed folder both apps (and later other machines) can use, outside the repo and outside Program Files.
std::wstring GetSharedDataDir() {
    if (!g_exeDirOverrideForTests.empty()) return g_exeDirOverrideForTests;
    static std::wstring cached;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const wchar_t* const kDir = L"C:\\BalancingAct\\data";
        CreateDirectoryW(L"C:\\BalancingAct", nullptr); // already existing is fine
        CreateDirectoryW(kDir, nullptr);
        const DWORD attrs = GetFileAttributesW(kDir);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) cached = kDir;
    }
    return cached.empty() ? GetExeDir() : cached;
}

// One-time upgrade help: a list file that still sits beside the exe (from v0.13.0 or earlier) is COPIED into the
// shared folder if the shared folder has none yet. The old file is left where it is.
static void CopyFromExeDirIfMissing(const wchar_t* fileName) {
    const std::wstring shared = GetSharedDataDir() + L"\\" + fileName;
    const std::wstring old = GetExeDir() + L"\\" + fileName;
    if (_wcsicmp(shared.c_str(), old.c_str()) == 0) return;
    if (GetFileAttributesW(shared.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    if (GetFileAttributesW(old.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    CopyFileW(old.c_str(), shared.c_str(), TRUE);
}

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

// v0.14.0: autosave.fbd, backups\ and history\ now live in the shared data folder too (Program Files is not
// writable). Anything still beside the exe is MOVED across once per run; moved files are gone from the old place,
// so a deleted autosave can never be "recovered" again from a stale copy. Does nothing when both folders are the same.
static void MigrateWritableDataOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    const std::wstring shared = GetSharedDataDir();
    const std::wstring old = GetExeDir();
    if (_wcsicmp(shared.c_str(), old.c_str()) == 0) return;
    MoveFileExW((old + L"\\autosave.fbd").c_str(), (shared + L"\\autosave.fbd").c_str(), MOVEFILE_COPY_ALLOWED);
    const wchar_t* const subs[] = { L"backups", L"history" };
    for (const wchar_t* sub : subs) {
        const std::wstring from = old + L"\\" + sub;
        const std::wstring to = shared + L"\\" + sub;
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW((from + L"\\*.fbd").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        CreateDirectoryW(to.c_str(), nullptr);
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring dest = to + L"\\" + fd.cFileName;
            if (GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES) continue; // never overwrite
            MoveFileExW((from + L"\\" + fd.cFileName).c_str(), dest.c_str(), MOVEFILE_COPY_ALLOWED);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        RemoveDirectoryW(from.c_str()); // only succeeds if now empty
    }
}

// Phase 1 / F1 follow-up (2026-09-30, post-review): the single canonical
// path for autosave.fbd, so BuildFbdSaveContent()/SaveToFile() can tell
// whether a given save IS the autosave write without duplicating this
// string construction at every call site.
std::wstring AutosavePath() {
    MigrateWritableDataOnce();
    return GetSharedDataDir() + L"\\autosave.fbd";
}

std::wstring SettingsPath() { return GetSharedDataDir() + L"\\settings.txt"; }

void LoadSettings(int dpi) {
    CopyFromExeDirIfMissing(L"settings.txt"); // v0.13.2: moved to the shared data folder
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
        else if (key == L"DIRTY") g_settings.dirtyAtExit = (val == L"1");
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
void SaveSettingsFile(int x, int y, int w, int h, bool maximized, const std::wstring& lastFile, bool dirtyAtExit) {
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
    writeLine(std::wstring(L"DIRTY=") + (dirtyAtExit ? L"1" : L"0"));

    // Failure here stays silent (as it always has been) - losing
    // settings.txt only means window position/last-file aren't remembered
    // next launch, not any loss of actual business data. Still a genuine
    // atomic+checked write now, not a truncate-and-hope.
    WriteFileAtomicUtf8(SettingsPath(), content);
}

std::wstring RecentFilesPath() { return GetSharedDataDir() + L"\\recent.txt"; }

void SaveRecentFiles() {
    std::string content;
    for (auto& p : g_recentFiles) content += WToUtf8(p) + "\n";
    // Failure stays silent, same reasoning as SaveSettings() - losing this
    // just means the Recent Files menu doesn't remember entries, not any
    // loss of actual business data.
    WriteFileAtomicUtf8(RecentFilesPath(), content);
}

void LoadRecentFiles() {
    CopyFromExeDirIfMissing(L"recent.txt"); // v0.13.2: moved to the shared data folder
    std::vector<std::wstring> lines;
    ReadAllLines(RecentFilesPath(), lines);
    g_recentFiles.clear();
    for (auto& l : lines) if (!l.empty()) g_recentFiles.push_back(l);
}

std::wstring EmailsPath() { return GetSharedDataDir() + L"\\emails.txt"; }

// v0.15.0: supplier addresses live in names.txt (S|name|date|email).
// g_supplierEmails stays as the in-memory name->address view the Manage Names
// and Email code use; it is rebuilt from names.txt here and folded back in by
// SaveNameMaster().  emails.txt is only read once, to migrate older installs.
void LoadSupplierEmails() {
    g_supplierEmails.clear();
    for (const auto& kv : g_nameMaster.suppliers.emails)
        if (!kv.second.empty()) g_supplierEmails[kv.first] = kv.second;
}

bool SaveSupplierEmails() { return SaveNameMaster(); }

std::wstring NamesPath() { return GetSharedDataDir() + L"\\names.txt"; }

// Reads one legacy emails.txt (if present) into the master; true if the file existed.
static bool MigrateOneLegacyEmailsFile(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    std::vector<std::wstring> lines;
    if (!ReadAllLines(path, lines)) return false;
    MigrateLegacyEmails(g_nameMaster, lines);
    return true;
}

static void RetireLegacyEmailsFile(const std::wstring& path) {
    // Keep the old file as emails.txt.migrated (never overwrite an earlier one).
    const std::wstring dest = path + L".migrated";
    if (GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES) DeleteFileW(dest.c_str());
    MoveFileW(path.c_str(), dest.c_str());
}

static void UpdateReportNamePreferences() {
    PreferredSupplierSpellings() = g_nameMaster.suppliers.names;
    PreferredSpeciesSpellings() = g_nameMaster.species.names;
}

void LoadNameMaster() {
    CopyFromExeDirIfMissing(L"names.txt");
    std::vector<std::wstring> lines;
    const bool found = ReadAllLines(NamesPath(), lines);
    g_nameMaster = NameMasterData();
    if (found) ParseNameMasterLines(lines, g_nameMaster);
    g_nameMaster.fileExisted = found;

    // v0.15.0: one-time migration of emails.txt (shared data folder, and the
    // old exe-folder copy) into names.txt.  Addresses already in names.txt win.
    const std::wstring sharedLegacy = EmailsPath();
    const std::wstring exeLegacy = GetExeDir() + L"\\emails.txt";
    const bool s1 = MigrateOneLegacyEmailsFile(sharedLegacy);
    const bool s2 = (exeLegacy != sharedLegacy) && MigrateOneLegacyEmailsFile(exeLegacy);
    LoadSupplierEmails(); // fill the view first so the save below keeps the migrated addresses
    if (s1 || s2) {
        if (SaveNameMaster()) {
            if (s1) RetireLegacyEmailsFile(sharedLegacy);
            if (s2) RetireLegacyEmailsFile(exeLegacy);
        }
    }
    UpdateReportNamePreferences(); // v0.17.0
}

bool SaveNameMaster() {
    UpdateReportNamePreferences(); // v0.17.0: reports show master-list spellings
    SyncListEmails(g_nameMaster.suppliers, g_supplierEmails);
    std::string content;
    for (const std::wstring& l : BuildNameMasterLines(g_nameMaster)) content += WToUtf8(l) + "\n";
    const bool ok = WriteFileAtomicUtf8(NamesPath(), content);
    if (ok) g_nameMaster.fileExisted = true;
    return ok;
}

bool AddSheetNamesToMaster(const std::wstring& dateISO) {
    bool changed = false;
    for (const Entry& e : g_entries) {
        if (NameListAdd(g_nameMaster.suppliers, e.supplier, dateISO)) changed = true;
        if (NameListAdd(g_nameMaster.species, e.product, dateISO)) changed = true;
    }
    return changed;
}

std::wstring BackupDir() { MigrateWritableDataOnce(); return GetSharedDataDir() + L"\\backups"; }

// Finalize Day (ROADMAP.md item 3) locked-in snapshots, one per business
// day, named <date>.fbd (e.g. 2026-09-20.fbd). A separate folder from
// backups\ - these are deliberate, permanent records, not rolling/pruned
// safety-net copies.
std::wstring HistoryDir() { MigrateWritableDataOnce(); return GetSharedDataDir() + L"\\history"; }

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
