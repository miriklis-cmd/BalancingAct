# push_update.ps1 - commit and push the v0.9.29-v0.9.33 updates
#
# Run from the repo root with:
#   powershell -ExecutionPolicy Bypass -File .\push_update.ps1
#
# Follows DevelopmentWorkflow.md's Git section:
# - commit message goes through a temp file + `git commit -F`, never `-m`
#   with a large/multi-line string (a literal `&` in a message previously
#   broke PowerShell's argument-passing to git.exe)
# - $LASTEXITCODE is checked after every git invocation
# - a remote is confirmed to exist before doing anything else
#
# This one script covers five version bumps at once (v0.9.29-v0.9.33)
# since push_update.ps1 wasn't regenerated between them - it's a single
# commit covering all five, not five separate commits, since they were
# never pushed individually in between.

$ErrorActionPreference = "Stop"

function Test-LastExit($stepName) {
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED at step: $stepName (exit code $LASTEXITCODE)" -ForegroundColor Red
        exit 1
    }
}

# 1. Verify a remote is actually configured before doing anything else.
$remotes = git remote 2>&1
Test-LastExit "git remote"
if ([string]::IsNullOrWhiteSpace($remotes)) {
    Write-Host "FAILED: no git remote configured in this repo. Add one (e.g. 'git remote add origin <url>') before running this script." -ForegroundColor Red
    exit 1
}

# 2. Stage everything.
git add -A
Test-LastExit "git add -A"

# 3. Write the commit message to a temp file and commit with -F.
$commitMessage = @'
v0.9.29-v0.9.33: real backup timer, dirty-flag discard prompt, silent backup-failure fix, File > New backup gap, Restore dialog folder fix

v0.9.29 - Every-3-minutes rolling backup wasn't actually driven by a
real timer:
- Jack: "Every 3 minute save not working." MaybeBackupOnTimer() only
  ever ran inside the focus-loss autosave path - a screen left idle
  after one edit (no further tabbing/clicking) never got backed up no
  matter how long it sat.
- Fix: a genuine recurring WM_TIMER (ID_TIMER_BACKUP_CHECK, 30s poll)
  now drives it independently of focus-loss activity.

v0.9.30 - Discard-confirmation prompt now tracks real unsaved changes:
- Jack: "should be kinda? dirty flagged?. If something changes, dialog
  comes up. Once saved, then no dialog until something changes again."
- Fix: new g_dirty flag, set on every real data mutation (entry
  commit/delete/undo, Debtor/Cash edit, Manage Names apply), cleared on
  New/Open/Recent Files/Restore from Backup/Save/Save As/startup.
  ConfirmDiscardCurrentData() now checks g_dirty instead of "is there
  any data at all."

v0.9.31 - Rolling backup snapshots now warn on failure instead of
failing silently:
- Jack retested v0.9.29's fix and corrected his original account: it
  was actually v0.9.28, with ~5 active Add Entry commits over roughly a
  minute, not a fully idle screen - asked to "look properly at the
  code" rather than re-diagnose from his own uncertain recollection.
- Root cause found: WriteBackupSnapshot() discarded
  WriteFileAtomicUtf8's return value outright. A failing write (locked/
  unwritable backups\ folder, disk full) was completely silent, and its
  own tracking state (g_lastBackupContent/g_lastBackupTick) was updated
  as if it had succeeded anyway - suppressing any near-term retry.
- Fix: return value now checked; on failure, warns once (new
  g_backupFailWarned flag, same pattern as the existing autosave-
  failure warning) and deliberately does NOT update the tracking state,
  so the very next autosave retries immediately instead of waiting out
  a full interval believing a snapshot already succeeded.

v0.9.32 - File > New now takes a backup snapshot before discarding too:
- Jack found this directly while smoke-testing the v0.9.28 checklist:
  added an entry, File > New, confirmed the discard prompt, checked
  backups\ - nothing there.
- v0.9.28 gave Open/Recent Files/Restore from Backup an unconditional
  snapshot before discarding, but left File > New out on the reasoning
  "New already clears to a blank sheet, nothing to preserve" - backwards,
  since it's the data being discarded that needs protecting, not the new
  blank sheet.
- Fix: DoFileNew() now calls WriteBackupSnapshot() right after the
  discard prompt is confirmed, same as the other three paths.

v0.9.33 - Restore from Backup now reliably opens to the backups folder:
- Jack: "shouldnt it take me to backup folder? it didnt. last folder i
  used in open was desktop and went there instead."
- Documented Windows quirk: GetOpenFileNameW only honors lpstrInitialDir
  on the very first time a process ever shows that dialog; after that it
  reuses whatever folder was last navigated to in ANY prior call.
- Fix: pre-fill lpstrFile with the backups folder path (trailing
  backslash, no filename) before opening the dialog - a path in
  lpstrFile takes priority over the remembered folder, unlike
  lpstrInitialDir, so this reliably forces the dialog into backups\
  every time.

See CHANGELOG.md's [0.9.29]/[0.9.30]/[0.9.31]/[0.9.32]/[0.9.33] entries
for full detail. Full smoke-test pass (20 items, v0.9.24 through
v0.9.33) completed and confirmed by Jack on 2026-09-25 - see ROADMAP.md.
'@

$tempFile = [System.IO.Path]::GetTempFileName()
Set-Content -Path $tempFile -Value $commitMessage -Encoding UTF8

git commit -F $tempFile
$commitExitCode = $LASTEXITCODE
Remove-Item $tempFile -ErrorAction SilentlyContinue
if ($commitExitCode -ne 0) {
    Write-Host "FAILED at step: git commit (exit code $commitExitCode)" -ForegroundColor Red
    exit 1
}

# 4. Push.
git push
Test-LastExit "git push"

Write-Host "Done: v0.9.29-v0.9.33 committed and pushed." -ForegroundColor Green
