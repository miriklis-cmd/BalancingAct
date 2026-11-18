// SettingsWindow.h - the modal Tools > Settings window (forms layer), v0.22.0.
// Three tabs: Email (method, Gmail account/sign-in, wording), Backups & prices (backup rhythm, price-flag
// sensitivity), Data folder & about. OK saves everything to settings.txt; Cancel changes nothing.
#pragma once

// Opens the window (disables the main window until it closes). Does nothing if it is already open.
void OpenSettingsWindow();
