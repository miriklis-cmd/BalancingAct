// Backups.cpp - moved out of main.cpp's WriteBackupSnapshot() in v0.10.14 (split stage 7b),
// minus the warning popup (that stays in main.cpp, which owns the window).
#include "Backups.h"

#include "../PlatformWin.h"
#include <string>

#include "../core/FishBalanceCore.h"
#include "AppData.h"
#include "FishBalanceWin32IO.h"
#include "Storage.h"

bool WriteBackupSnapshotFile(const std::string& content, std::wstring* outError) {
    std::wstring dir = BackupDir();
    CreateDirectoryW(dir.c_str(), nullptr); // no-op (fails harmlessly) if it already exists

    SYSTEMTIME st{};
    GetLocalTime(&st);
    // Source-file label goes AFTER the timestamp, not before - v0.9.27,
    // Jack: "if I'm opening different files I won't know which one it's
    // snapshotting." The timestamp must stay the leading, fixed-width part
    // of the filename for PruneOldBackups' "sort by name = sort by time"
    // logic to keep working; a label before it would sort backups by
    // source file first, breaking chronological pruning entirely.
    // v0.9.64 (audit F8): milliseconds in the name, plus a counter if the
    // name somehow still exists, so a snapshot never replaces an earlier one.
    const std::wstring label = CurrentFileLabelForBackup();
    std::wstring path;
    for (int counter = 0; counter < 1000; counter++) {
        path = dir + L"\\" + BuildBackupFileName(st.wYear, st.wMonth, st.wDay, st.wHour,
                   st.wMinute, st.wSecond, st.wMilliseconds, label, counter);
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) break;
    }

    std::wstring writeError;
    bool ok = WriteFileAtomicUtf8(path, content, &writeError);
    if (ok) {
        g_lastBackupContent = content;
        g_lastBackupTick = GetTickCount64();
        PruneOldBackups();
    } else if (outError) {
        *outError = writeError;
    }
    return ok;
}
