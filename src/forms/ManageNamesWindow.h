// ManageNamesWindow.h - the "Manage Supplier / Species Names" popup (forms layer).
// Moved out of main.cpp unchanged in v0.10.6 (split stage 5).
#pragma once

#include <string>

// Opens the popup (disables the main window until it closes).
void OpenManageNamesWindow();

// v0.13.0 entry-time name checks, called by CommitEntryForm (main.cpp) against the master lists (names.txt):
//  - ResolveEntryNames: snaps to the list's spelling (case/spacing), corrects a remembered alias, and asks
//    "did you mean...?" for a near match. Returns false if the user chose to go back and edit (nothing changed).
//    On success `supplier`/`product` hold the names to store and `note` gets a sentence per correction.
//  - RecordEntryNames: call once the entry is really committed; adds any name that is new to the lists, silently
//    (the sentence is appended to `note`).
bool ResolveEntryNames(std::wstring& supplier, std::wstring& product, std::wstring& note);
void RecordEntryNames(const std::wstring& supplier, const std::wstring& product, std::wstring& note);
