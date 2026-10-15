// Backups.h - writing a rolling-backup snapshot to the backups\ folder (data layer).
// v0.10.14 (main.cpp split stage 7b): the disk half of the old WriteBackupSnapshot() in
// main.cpp. The warning popup stays in main.cpp (forms layer); this layer never shows UI.
#pragma once

#include <string>

// Writes `content` as a new timestamped snapshot (backup_YYYYMMDD_HHMMSS_mmm[-N]_<label>.fbd).
// On success: records the content and time (g_lastBackupContent / g_lastBackupTick) and prunes
// old snapshots, returns true. On failure: returns false with the reason in *outError (if
// given) and deliberately leaves g_lastBackupContent / g_lastBackupTick UNTOUCHED, so the very
// next autosave retries immediately instead of waiting out the whole interval (v0.9.31).
bool WriteBackupSnapshotFile(const std::string& content, std::wstring* outError);
