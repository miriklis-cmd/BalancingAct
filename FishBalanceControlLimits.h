// FishBalanceControlLimits.h
//
// Test-automation refactor (2026-09-30, v0.9.51): the single place that
// applies this app's documented field-length limits (FishBalanceCore.h's
// kMaxSupplierSpeciesLength etc.) to real Win32 EDIT/COMBOBOX controls via
// EM_LIMITTEXT/CB_LIMITTEXT. Extracted out of main.cpp's WM_CREATE handler
// so the Windows integration test suite can call the EXACT same function
// against its own throwaway hidden controls and verify the limits are
// actually enforced by the control itself (Phase 1 item 5's "Windows
// control test") - not just re-reading the constant and trusting it was
// wired up correctly.
//
// Deliberately takes plain HWNDs, not the app's global control-handle
// variables, so it has zero dependency on main.cpp and can be included by
// a standalone test executable that never links any GUI/window code from
// main.cpp at all beyond what it creates for itself.
#pragma once

#include <windows.h>
#include "FishBalanceCore.h"

// Any handle may be nullptr (a caller that only wants to apply the limit to
// a subset of these controls, e.g. the Manage Names dialog's two fields,
// passes nullptr for the rest) - each SendMessageW is skipped for a null
// handle rather than crashing.
struct FieldLengthLimitTargets {
    HWND supplierCombo = nullptr;   // CB_LIMITTEXT, kMaxSupplierSpeciesLength
    HWND speciesCombo = nullptr;    // CB_LIMITTEXT, kMaxSupplierSpeciesLength
    HWND kgsEdit = nullptr;         // EM_LIMITTEXT, kMaxDraftKgsPriceTextLength
    HWND priceEdit = nullptr;       // EM_LIMITTEXT, kMaxDraftKgsPriceTextLength
    HWND notesEdit = nullptr;       // EM_LIMITTEXT, kMaxNotesLength
    HWND debtorEdit = nullptr;      // EM_LIMITTEXT, kMaxDebtorCashExprLength
    HWND cashEdit = nullptr;        // EM_LIMITTEXT, kMaxDebtorCashExprLength
    HWND manageTargetEdit = nullptr;    // EM_LIMITTEXT, kMaxSupplierSpeciesLength (Manage Names rename/merge field)
    HWND manageEmailEdit = nullptr;     // EM_LIMITTEXT, kMaxSupplierSpeciesLength (Manage Names email field)
};

inline void ApplyFieldLengthLimits(const FieldLengthLimitTargets& t) {
    if (t.supplierCombo) SendMessageW(t.supplierCombo, CB_LIMITTEXT, (WPARAM)kMaxSupplierSpeciesLength, 0);
    if (t.speciesCombo) SendMessageW(t.speciesCombo, CB_LIMITTEXT, (WPARAM)kMaxSupplierSpeciesLength, 0);
    if (t.kgsEdit) SendMessageW(t.kgsEdit, EM_LIMITTEXT, (WPARAM)kMaxDraftKgsPriceTextLength, 0);
    if (t.priceEdit) SendMessageW(t.priceEdit, EM_LIMITTEXT, (WPARAM)kMaxDraftKgsPriceTextLength, 0);
    if (t.notesEdit) SendMessageW(t.notesEdit, EM_LIMITTEXT, (WPARAM)kMaxNotesLength, 0);
    if (t.debtorEdit) SendMessageW(t.debtorEdit, EM_LIMITTEXT, (WPARAM)kMaxDebtorCashExprLength, 0);
    if (t.cashEdit) SendMessageW(t.cashEdit, EM_LIMITTEXT, (WPARAM)kMaxDebtorCashExprLength, 0);
    if (t.manageTargetEdit) SendMessageW(t.manageTargetEdit, EM_LIMITTEXT, (WPARAM)kMaxSupplierSpeciesLength, 0);
    if (t.manageEmailEdit) SendMessageW(t.manageEmailEdit, EM_LIMITTEXT, (WPARAM)kMaxSupplierSpeciesLength, 0);
}
