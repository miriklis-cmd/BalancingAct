// AutosaveShared.h - the shared data folder, the "who owns autosave.fbd" lock, and the settings.txt DIRTY flag, for
// apps that share C:\BalancingAct\data (v0.18.0: the Win32 app and the WinUI app). Header-only, Win32 API only.
//
// Why a lock: both apps write the same autosave.fbd. If both are open, each would overwrite the other's work every few
// seconds, and the one that starts second would "recover" the first one's live sheet. So the FIRST app to start owns
// autosave.fbd: it holds an exclusively opened autosave.lock for as long as it runs. A second app finds the lock taken,
// tells the user, and neither reads nor writes autosave.fbd for that session. The lock vanishes when its owner exits
// OR crashes (the OS closes the handle), so there is never a stale lock to clean up.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "FishBalanceWin32IO.h"

namespace autosave_shared {

// Same folder as GetSharedDataDir() in Storage.cpp (the Win32 app keeps using that one; this is for the WinUI app,
// which does not compile Storage.cpp). Falls back to "." if the folder cannot be created.
inline std::wstring DataDir() {
    static std::wstring cached;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const wchar_t* const kDir = L"C:\\BalancingAct\\data";
        CreateDirectoryW(L"C:\\BalancingAct", nullptr);
        CreateDirectoryW(kDir, nullptr);
        const DWORD attrs = GetFileAttributesW(kDir);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) cached = kDir;
    }
    return cached.empty() ? std::wstring(L".") : cached;
}

inline HANDLE& LockHandle() { static HANDLE h = INVALID_HANDLE_VALUE; return h; }

// True if this process now owns autosave.fbd (or already did). False ONLY when another running copy holds the lock.
// Any other failure (read-only folder, ...) counts as "owner": autosave itself will then report the real problem.
inline bool TryAcquireOwnership(const std::wstring& dir) {
    if (LockHandle() != INVALID_HANDLE_VALUE) return true;
    HANDLE h = CreateFileW((dir + L"\\autosave.lock").c_str(), GENERIC_WRITE, 0 /* no sharing */, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() != ERROR_SHARING_VIOLATION;
    LockHandle() = h; // deliberately never closed: lives until the process ends
    return true;
}

// settings.txt is "KEY=value" lines (see DATA_FORMATS.md). These read or change ONE key and leave the rest alone, so
// the WinUI app can share the file without clobbering the Win32 app's window position.
inline std::wstring ReadSetting(const std::wstring& dir, const wchar_t* key) {
    std::vector<std::wstring> lines;
    if (!ReadAllLines(dir + L"\\settings.txt", lines)) return std::wstring();
    const std::wstring prefix = std::wstring(key) + L"=";
    for (const auto& l : lines)
        if (l.compare(0, prefix.size(), prefix) == 0) return l.substr(prefix.size());
    return std::wstring();
}

// Sets one key (adding it if missing). Does nothing if settings.txt does not exist yet (the Win32 app creates it).
inline void WriteSetting(const std::wstring& dir, const wchar_t* key, const std::wstring& value) {
    const std::wstring path = dir + L"\\settings.txt";
    std::vector<std::wstring> lines;
    if (!ReadAllLines(path, lines)) return;
    const std::wstring prefix = std::wstring(key) + L"=";
    bool found = false;
    for (auto& l : lines)
        if (l.compare(0, prefix.size(), prefix) == 0) { l = prefix + value; found = true; }
    if (!found) lines.push_back(prefix + value);
    std::string content;
    for (const auto& l : lines) content += WToUtf8(l) + "\n";
    std::wstring ignored;
    WriteFileAtomicUtf8(path, content, &ignored);
}

} // namespace autosave_shared
