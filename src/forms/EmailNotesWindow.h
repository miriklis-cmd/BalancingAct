// EmailNotesWindow.h - "Email Suppliers - notes for each supplier" dialog (forms layer), v0.24.0.
// Shown every time before the supplier emails are made. One free-text box each for NOT SOLD / UP / DOWN per supplier.
// Starts blank each time. Same design as the WinUI dialog.
#pragma once
#include "../core/FishBalanceCore.h"
#include <map>
#include <string>
#include <vector>

enum class EmailNotesResult { Cancel, Skip, Create };

// Modal (runs its own message loop; the main window is disabled meanwhile). On Create, `notes` holds the text typed for each
// supplier (keyed by supplier name); on Skip / Cancel it is left empty.
EmailNotesResult ShowEmailNotesDialog(const std::vector<SupplierGroup>& data, std::map<std::wstring, EmailNotes>& notes);
