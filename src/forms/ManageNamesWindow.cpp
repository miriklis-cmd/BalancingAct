// ManageNamesWindow.cpp - moved unchanged out of main.cpp in v0.10.6 (split stage 5).
#include "ManageNamesWindow.h"

#include "../PlatformWin.h"
#include <algorithm>
#include <cwchar>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../core/FishBalanceCore.h"
#include "../core/FishBalanceControlLimits.h"
#include "../data/AppData.h"
#include "../data/FishBalanceWin32IO.h"
#include "../data/Storage.h"
#include "../services/ReportRender.h"
#include "ControlIds.h"
#include "UiState.h"
#include "MainWindowApi.h"

// ---------------------------------------------------------------------------
// Manage Supplier / Species Names popup window
//
// A small owner-disabled popup (not a real Win32 dialog resource, just a
// plain window like the rest of the app) that lists every distinct
// Supplier or Species currently in use, and lets the user rename/merge one
// or more of them at once - e.g. selecting both "Spanner" and "dam spanner"
// and renaming to "Spanner" folds them into a single entry everywhere.
// ---------------------------------------------------------------------------

void PopulateManageList() {
    SendMessageW(g_hManageList, LB_RESETCONTENT, 0, 0);
    g_manageValues.clear();

    std::vector<std::wstring> vals;
    std::map<std::wstring, int> counts;
    for (auto& e : g_entries) {
        const std::wstring& v = g_manageIsSupplier ? e.supplier : e.product;
        if (counts.find(v) == counts.end()) { counts[v] = 0; vals.push_back(v); }
        counts[v]++;
    }
    std::sort(vals.begin(), vals.end());
    for (auto& v : vals) {
        g_manageValues.push_back(v);
        std::wstring label = v + L"  (" + std::to_wstring(counts[v]) + (counts[v] == 1 ? L" entry)" : L" entries)");
        if (g_manageIsSupplier) {
            auto it = g_supplierEmails.find(v);
            if (it != g_supplierEmails.end() && !it->second.empty()) label += L"  - " + it->second;
        }
        SendMessageW(g_hManageList, LB_ADDSTRING, 0, (LPARAM)label.c_str());
    }
}

// Shows/hides and refreshes the Email field based on the current mode
// (Supplier vs Species) and how many items are selected in the list -
// editing an email only makes sense for a single selected supplier.
void UpdateManageEmailControls() {
    if (!g_hManageEmailLbl) return; // not created yet

    if (!g_manageIsSupplier) {
        ShowWindow(g_hManageEmailLbl, SW_HIDE);
        ShowWindow(g_hManageEmailEdit, SW_HIDE);
        ShowWindow(g_hManageEmailBtn, SW_HIDE);
        return;
    }
    ShowWindow(g_hManageEmailLbl, SW_SHOW);
    ShowWindow(g_hManageEmailEdit, SW_SHOW);
    ShowWindow(g_hManageEmailBtn, SW_SHOW);

    int selCount = (int)SendMessageW(g_hManageList, LB_GETSELCOUNT, 0, 0);
    if (selCount == 1) {
        int idx[1] = { -1 };
        SendMessageW(g_hManageList, LB_GETSELITEMS, 1, (LPARAM)idx);
        if (idx[0] >= 0 && idx[0] < (int)g_manageValues.size()) {
            std::wstring sup = g_manageValues[idx[0]];
            auto it = g_supplierEmails.find(sup);
            SetWindowTextW(g_hManageEmailEdit, it != g_supplierEmails.end() ? it->second.c_str() : L"");
            EnableWindow(g_hManageEmailEdit, TRUE);
            EnableWindow(g_hManageEmailBtn, TRUE);
            return;
        }
    }
    SetWindowTextW(g_hManageEmailEdit, L"");
    EnableWindow(g_hManageEmailEdit, FALSE);
    EnableWindow(g_hManageEmailBtn, FALSE);
}

// Enables Apply only when there's actually something it could do: at least
// one name selected in the list, and a non-empty target to rename/merge
// them to. Previously the button was always clickable and just showed a
// MessageBox error if either condition wasn't met - this reflects that
// state up front instead.
void UpdateManageApplyButton() {
    if (!g_hManageApplyBtn) return; // not created yet
    int selCount = (int)SendMessageW(g_hManageList, LB_GETSELCOUNT, 0, 0);
    wchar_t buf[256];
    GetWindowTextW(g_hManageTarget, buf, 256);
    bool hasTarget = !TrimW(buf).empty();
    EnableWindow(g_hManageApplyBtn, (selCount > 0 && hasTarget) ? TRUE : FALSE);
}

void ManageSaveEmail() {
    int selCount = (int)SendMessageW(g_hManageList, LB_GETSELCOUNT, 0, 0);
    if (selCount != 1) {
        MessageBoxW(g_hManageWnd, L"Select exactly one supplier to set its email address.", L"Select One Supplier", MB_OK | MB_ICONINFORMATION);
        return;
    }
    int idx[1] = { -1 };
    SendMessageW(g_hManageList, LB_GETSELITEMS, 1, (LPARAM)idx);
    if (idx[0] < 0 || idx[0] >= (int)g_manageValues.size()) return;
    std::wstring supplier = g_manageValues[idx[0]];

    wchar_t buf[256];
    GetWindowTextW(g_hManageEmailEdit, buf, 256);
    std::wstring email = TrimW(buf);

    if (!email.empty()) {
        if (email.find(L'|') != std::wstring::npos) {
            MessageBoxW(g_hManageWnd, L"Email addresses can't contain the '|' character.", L"Invalid Email", MB_OK | MB_ICONWARNING);
            return;
        }
        if (!LooksLikeEmail(email)) {
            MessageBoxW(g_hManageWnd, L"That doesn't look like a valid email address.", L"Invalid Email", MB_OK | MB_ICONWARNING);
            return;
        }
    }

    if (email.empty()) g_supplierEmails.erase(supplier);
    else g_supplierEmails[supplier] = email;
    bool saved = SaveSupplierEmails();

    PopulateManageList();
    // Re-select the same supplier so the email field stays populated.
    for (size_t i = 0; i < g_manageValues.size(); i++) {
        if (g_manageValues[i] == supplier) { SendMessageW(g_hManageList, LB_SETSEL, TRUE, (LPARAM)i); break; }
    }
    UpdateManageEmailControls();
    if (saved) {
        MessageBoxW(g_hManageWnd, email.empty() ? L"Email cleared." : L"Email saved.", L"Done", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(g_hManageWnd,
            L"Could not save the email address to disk - the change is still in memory for this "
            L"session, but will be lost when the app closes unless the save succeeds. Check that "
            L"emails.txt isn't open in another program and try again.",
            L"Save Failed", MB_OK | MB_ICONWARNING);
    }
}

void ManageApply() {
    int selCount = (int)SendMessageW(g_hManageList, LB_GETSELCOUNT, 0, 0);
    if (selCount <= 0) {
        MessageBoxW(g_hManageWnd, L"Select one or more names in the list to rename/merge.", L"Nothing Selected", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::vector<int> idxs((size_t)selCount);
    SendMessageW(g_hManageList, LB_GETSELITEMS, (WPARAM)selCount, (LPARAM)idxs.data());

    wchar_t buf[256];
    GetWindowTextW(g_hManageTarget, buf, 256);
    std::wstring target = TrimW(buf);
    if (target.empty()) {
        MessageBoxW(g_hManageWnd, L"Enter the name to rename/merge the selected items to.", L"Missing Target", MB_OK | MB_ICONWARNING);
        return;
    }
    if (target.find(L'|') != std::wstring::npos) {
        MessageBoxW(g_hManageWnd, L"Names can't contain the '|' character - it's used internally as a separator when saving.", L"Invalid Name", MB_OK | MB_ICONWARNING);
        return;
    }

    std::vector<std::wstring> sourceNames;
    for (int idx : idxs)
        if (idx >= 0 && idx < (int)g_manageValues.size()) sourceNames.push_back(g_manageValues[idx]);

    // Supplier email addresses are keyed by supplier name, so a
    // rename/merge of SUPPLIER names (not species - g_supplierEmails has
    // no species concept) needs to migrate any saved email(s) too,
    // otherwise the address becomes silently orphaned under a name that
    // no longer exists anywhere. This is resolved BEFORE touching
    // g_entries at all below, so if the user cancels a genuine conflict,
    // nothing has changed yet - a clean abort, not a half-applied rename.
    //
    // Conflict handling: gather every DISTINCT saved email among the
    // target's own existing email (if any) and each selected source's
    // email. Zero or one distinct value found means no real conflict -
    // proceed automatically. Exactly two distinct values (by far the most
    // common real conflict - merging two suppliers who each have a
    // different saved address) - ask which to keep. Three or more
    // distinct values is rare enough that a full picker UI wasn't judged
    // worth building; the first one found is kept, but the user is told a
    // conflict existed rather than that being silent.
    std::wstring resolvedEmail;
    bool haveResolvedEmail = false;
    if (g_manageIsSupplier) {
        std::vector<std::wstring> candidates;
        auto addCandidate = [&](const std::wstring& email) {
            if (email.empty()) return;
            for (auto& c : candidates) if (c == email) return; // already have it
            candidates.push_back(email);
        };
        auto targetIt = g_supplierEmails.find(target);
        if (targetIt != g_supplierEmails.end()) addCandidate(targetIt->second);
        for (auto& src : sourceNames) {
            if (src == target) continue;
            auto it = g_supplierEmails.find(src);
            if (it != g_supplierEmails.end()) addCandidate(it->second);
        }

        if (candidates.size() == 1) {
            resolvedEmail = candidates[0];
            haveResolvedEmail = true;
        } else if (candidates.size() == 2) {
            std::wstring msg = L"The suppliers being merged have different saved email addresses:\n\n"
                                L"Yes = keep " + candidates[0] + L"\n"
                                L"No = keep " + candidates[1] + L"\n"
                                L"Cancel = don't merge yet, so you can check the addresses first";
            int r = MessageBoxW(g_hManageWnd, msg.c_str(), L"Which Email Should Be Kept?",
                                 MB_YESNOCANCEL | MB_ICONQUESTION);
            if (r == IDCANCEL) return; // nothing has been changed yet - clean abort
            resolvedEmail = (r == IDYES) ? candidates[0] : candidates[1];
            haveResolvedEmail = true;
        } else if (candidates.size() > 2) {
            resolvedEmail = candidates[0];
            haveResolvedEmail = true;
            std::wstring msg = L"Note: the suppliers being merged have " + std::to_wstring(candidates.size()) +
                                L" different saved email addresses. Kept: " + resolvedEmail +
                                L"\n\nYou can fix this afterward via Manage Names if it kept the wrong one.";
            MessageBoxW(g_hManageWnd, msg.c_str(), L"Multiple Emails Found", MB_OK | MB_ICONWARNING);
        }
    }

    int changed = 0;
    for (auto& e : g_entries) {
        std::wstring& field = g_manageIsSupplier ? e.supplier : e.product;
        for (auto& src : sourceNames) {
            if (field == src && field != target) { field = target; changed++; break; }
        }
    }

    bool emailsChanged = false;
    if (g_manageIsSupplier && haveResolvedEmail) {
        auto targetIt = g_supplierEmails.find(target);
        if (targetIt == g_supplierEmails.end() || targetIt->second != resolvedEmail) {
            g_supplierEmails[target] = resolvedEmail;
            emailsChanged = true;
        }
        for (auto& src : sourceNames) {
            if (src == target) continue;
            if (g_supplierEmails.erase(src) > 0) emailsChanged = true;
        }
    }
    bool emailsSaved = true;
    if (emailsChanged) emailsSaved = SaveSupplierEmails();

    if (g_editIndex != -1) CancelEdit();
    // A pending Undo Delete buffer holds a COPY of a row from before this
    // rename - if it happened to carry one of the old names, undoing it
    // after this point would silently reintroduce the spelling variant
    // this rename/merge was meant to eliminate. Simplest safe rule: any
    // actual rename invalidates the buffer, rather than trying to track
    // whether that specific row was affected.
    if (changed > 0) { ClearUndoState(); g_dirty = true; } // v0.9.30
    RefreshAll();
    PopulateManageList();
    SetWindowTextW(g_hManageTarget, L"");
    UpdateManageApplyButton();
    UpdateManageEmailControls(); // selection was lost when the list repopulated above

    std::wstring msg = L"Updated " + std::to_wstring(changed) + (changed == 1 ? L" entry." : L" entries.");
    if (emailsChanged && !emailsSaved) {
        msg += L"\n\nWarning: a saved email address was migrated in memory but could not be "
               L"written to emails.txt - it will be lost if the app closes before this succeeds. "
               L"Check emails.txt isn't open in another program.";
    }
    MessageBoxW(g_hManageWnd, msg.c_str(), L"Done", MB_OK | (emailsChanged && !emailsSaved ? MB_ICONWARNING : MB_ICONINFORMATION));
}

LRESULT CALLBACK ManageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hManageRadioSupplier = MakeControl(L"BUTTON", L"Supplier", WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP, ID_MNG_RADIO_SUPPLIER, hwnd);
        g_hManageRadioSpecies = MakeControl(L"BUTTON", L"Species", WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, ID_MNG_RADIO_SPECIES, hwnd);
        g_hManageHint = MakeControl(L"STATIC", L"Select one or more names to merge/rename, or select a single Supplier to set its email address.",
                                     WS_VISIBLE, 0, hwnd);
        g_hManageList = MakeControl(L"LISTBOX", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_MULTIPLESEL | LBS_NOTIFY, ID_MNG_LIST, hwnd);
        g_hManageEmailLbl = MakeControl(L"STATIC", L"Email for selected supplier:", WS_VISIBLE, 0, hwnd);
        g_hManageEmailEdit = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP, ID_MNG_EMAIL_EDIT, hwnd);
        g_hManageEmailBtn = MakeControl(L"BUTTON", L"Save Email", WS_VISIBLE | WS_TABSTOP, ID_MNG_EMAIL_SAVE, hwnd);
        g_hManageTargetLbl = MakeControl(L"STATIC", L"Rename / merge selected to:", WS_VISIBLE, 0, hwnd);
        g_hManageTarget = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP, ID_MNG_TARGET, hwnd);
        g_hManageApplyBtn = MakeControl(L"BUTTON", L"Apply", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_DEFPUSHBUTTON, ID_MNG_APPLY, hwnd);
        g_hManageCloseBtn = MakeControl(L"BUTTON", L"Close", WS_VISIBLE | WS_TABSTOP, ID_MNG_CLOSE, hwnd);
        // Phase 1 completeness follow-up (2026-09-30, v0.9.50): g_hManageTarget
        // is a Supplier/Species NAME field like any other - without this, a
        // name longer than the 255-wchar GetWindowTextW buffer used to read it
        // (ManageApply/UpdateManageApplyButton) would be silently truncated at
        // read time instead of being visibly capped at entry time. The buffer
        // itself is unchanged (256 wchar = 255 usable chars, already exactly
        // kMaxSupplierSpeciesLength), so this makes the existing bound an
        // explicit UI limit rather than an accidental one. g_hManageEmailEdit
        // gets the same bound for consistency, even though Jack's spec didn't
        // separately name an email-address limit - same buffer, same risk.
        {
            FieldLengthLimitTargets limits;
            limits.manageTargetEdit = g_hManageTarget;
            limits.manageEmailEdit = g_hManageEmailEdit;
            ApplyFieldLengthLimits(limits);
        }

        SendMessageW(g_hManageRadioSupplier, BM_SETCHECK, BST_CHECKED, 0);
        g_manageIsSupplier = true;
        PopulateManageList();
        UpdateManageEmailControls();
        UpdateManageApplyButton();
        return 0;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        MoveWindow(g_hManageRadioSupplier, S(15), S(15), S(100), S(24), TRUE);
        MoveWindow(g_hManageRadioSpecies, S(125), S(15), S(100), S(24), TRUE);
        MoveWindow(g_hManageHint, S(15), S(45), rc.right - S(30), S(38), TRUE);

        int listBottom = rc.bottom - S(200);
        if (listBottom < S(130)) listBottom = S(130);
        MoveWindow(g_hManageList, S(15), S(90), rc.right - S(30), listBottom - S(90), TRUE);

        int emailY = listBottom + S(12);
        MoveWindow(g_hManageEmailLbl, S(15), emailY, S(200), S(22), TRUE);
        MoveWindow(g_hManageEmailEdit, S(15), emailY + S(22), rc.right - S(30) - S(95), S(22), TRUE);
        MoveWindow(g_hManageEmailBtn, rc.right - S(100), emailY + S(21), S(85), S(24), TRUE);

        int y2 = emailY + S(64);
        MoveWindow(g_hManageTargetLbl, S(15), y2, rc.right - S(30), S(22), TRUE);
        MoveWindow(g_hManageTarget, S(15), y2 + S(22), rc.right - S(30), S(22), TRUE);
        MoveWindow(g_hManageApplyBtn, rc.right - S(190), y2 + S(52), S(85), S(28), TRUE);
        MoveWindow(g_hManageCloseBtn, rc.right - S(100), y2 + S(52), S(85), S(28), TRUE);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if ((id == ID_MNG_RADIO_SUPPLIER || id == ID_MNG_RADIO_SPECIES) && code == BN_CLICKED) {
            g_manageIsSupplier = (id == ID_MNG_RADIO_SUPPLIER);
            PopulateManageList();
            UpdateManageEmailControls();
            UpdateManageApplyButton();
            return 0;
        }
        if (id == ID_MNG_LIST && code == LBN_SELCHANGE) {
            UpdateManageEmailControls();
            UpdateManageApplyButton();
            return 0;
        }
        if (id == ID_MNG_TARGET && code == EN_CHANGE) {
            UpdateManageApplyButton();
            return 0;
        }
        if (id == ID_MNG_EMAIL_SAVE) { ManageSaveEmail(); return 0; }
        if (id == ID_MNG_APPLY) { ManageApply(); return 0; }
        if (id == ID_MNG_CLOSE) { DestroyWindow(hwnd); return 0; }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_hMainWnd) EnableWindow(g_hMainWnd, TRUE);
        g_hManageWnd = nullptr;
        SetForegroundWindow(g_hMainWnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void OpenManageNamesWindow() {
    if (g_hManageWnd) {
        SetForegroundWindow(g_hManageWnd);
        return;
    }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ManageWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"ManageNamesWindow";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    RECT mrc;
    GetWindowRect(g_hMainWnd, &mrc);
    int w = S(460), h = S(540);
    int x = mrc.left + ((mrc.right - mrc.left) - w) / 2;
    int y = mrc.top + ((mrc.bottom - mrc.top) - h) / 2;

    g_hManageWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ManageNamesWindow", L"Manage Supplier / Species Names",
                                    WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h,
                                    g_hMainWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    EnableWindow(g_hMainWnd, FALSE);
    ShowWindow(g_hManageWnd, SW_SHOW);
    UpdateWindow(g_hManageWnd);
}
