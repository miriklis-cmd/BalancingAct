// ManageNamesWindow.cpp - Tools > Manage Supplier / Species Names, plus the entry-time name checks.
//
// v0.13.0 rewrite (Jack's design, 2026-11-06). The old popup used one multi-select list for everything, so
// clicking a second supplier (to look at its email, say) silently selected both and the next Apply merged them.
// Now:
//   - the list is single-select: a click only ever SELECTS a name, and shows its name/email underneath;
//   - merging is a separate, deliberate act: tick the check boxes, press "Merge ticked...", choose which name to
//     keep in a confirmation that says how many entries will change;
//   - the names come from the master list (names.txt, src/core/NameMaster.h), not just the open sheet, so a
//     name keeps existing between sheets; names that are only on the open sheet are marked "not in list";
//   - a "Possible duplicates" strip offers to tick names that differ only by case, spacing or punctuation;
//   - a merge or rename is remembered as an alias (Barra -> Barramundi) and can be undone once.
//
// The second half of the file is what Add Entry calls (ResolveEntryNames / RecordEntryNames): snap to the list's
// spelling, correct a remembered alias, ask "did you mean...?" for a near match, and add a genuinely new name
// silently.
//
// Rules from ARCHITECTURE.md followed here: every dimension goes through S(); ListView_SetItemText is a
// multi-statement macro so its text always comes from a named variable; the popup disables the main window on
// creation and re-enables it in its own WM_DESTROY; Esc closes via subclassing because this is a plain window.
#include "ManageNamesWindow.h"

#include "../PlatformWin.h"
#include <algorithm>
#include <cwchar>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../core/FishBalanceCore.h"
#include "../core/FishBalanceControlLimits.h"
#include "../core/NameMaster.h"
#include "../data/AppData.h"
#include "../data/FishBalanceWin32IO.h"
#include "../data/Storage.h"
#include "ControlIds.h"
#include "UiState.h"
#include "MainWindowApi.h"
#include "MainWindowLayout.h"
#include "PopupPlacement.h"

namespace {

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

std::wstring TodayISO() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    SimpleDate d;
    d.year = st.wYear;
    d.month = st.wMonth;
    d.day = st.wDay;
    return FormatDateISO(d);
}

// First use: names.txt does not exist yet, so start the lists from whatever is on the open sheet. Afterwards the
// file exists and the sheet no longer decides what is in the lists.
void EnsureNameMasterSeeded() {
    if (g_nameMaster.fileExisted) return;
    AddSheetNamesToMaster(L"");
    SaveNameMaster(); // on failure fileExisted stays false and the next call simply tries again
}

// ---------------------------------------------------------------------------
// Manage Names window state (file-local)
// ---------------------------------------------------------------------------

struct ManageRow {
    std::wstring name;
    int entries = 0;          // how many entries on the OPEN sheet use it
    std::wstring email;       // suppliers only
    std::wstring aliases;     // "Blk Flat, Dusky Flathead" - other spellings that mean this name
    bool inMaster = false;
    bool recent = false;      // added to the master list in the last 7 days
};

struct UndoSnapshot {
    bool valid = false;
    std::vector<Entry> entries;
    NameMasterData master;
    std::map<std::wstring, std::wstring> emails;
    bool dirty = false;
};

HWND g_hRadioSupplier = nullptr, g_hRadioSpecies = nullptr;
HWND g_hDupLbl = nullptr, g_hDupBtn = nullptr, g_hHint = nullptr;
HWND g_hList = nullptr;
HWND g_hTickLbl = nullptr, g_hMergeBtn = nullptr;
HWND g_hNameLbl = nullptr, g_hNameEdit = nullptr, g_hRenameBtn = nullptr, g_hListBtn = nullptr;
HWND g_hEmailLbl = nullptr, g_hEmailEdit = nullptr, g_hEmailBtn = nullptr;
HWND g_hAliasLbl = nullptr, g_hAliasEdit = nullptr, g_hAliasAddBtn = nullptr, g_hAliasRemBtn = nullptr;
HWND g_hStatus = nullptr, g_hUndoBtn = nullptr, g_hCloseBtn = nullptr;

bool g_isSupplier = true;
std::vector<ManageRow> g_rows;
std::vector<std::wstring> g_dupGroup; // the names the "Tick these" button ticks
bool g_populating = false;            // true while the list is being rebuilt (ignore its change notifications)
bool g_settingText = false;           // true while the name/email boxes are being filled in by code
UndoSnapshot g_undo;

NameList& CurrentList() { return g_isSupplier ? g_nameMaster.suppliers : g_nameMaster.species; }

void SetStatus(const std::wstring& text) {
    if (g_hStatus) SetWindowTextW(g_hStatus, text.c_str());
}

std::wstring EntriesText(int n) { return std::to_wstring(n) + (n == 1 ? L" entry" : L" entries"); }

// Rows = every name in the master list + any name on the open sheet that is not in the list yet.
void BuildRows() {
    g_rows.clear();
    const NameList& list = CurrentList();
    std::map<std::wstring, int> counts;
    for (const Entry& e : g_entries) counts[g_isSupplier ? e.supplier : e.product]++;
    const std::wstring today = TodayISO();

    auto addRow = [&](const std::wstring& name, bool inMaster) {
        ManageRow r;
        r.name = name;
        auto c = counts.find(name);
        r.entries = (c == counts.end()) ? 0 : c->second;
        if (g_isSupplier) {
            auto em = g_supplierEmails.find(name);
            if (em != g_supplierEmails.end()) r.email = em->second;
        }
        r.inMaster = inMaster;
        {
            const std::vector<std::wstring> al = AliasesOf(list, name);
            for (size_t i = 0; i < al.size(); i++) r.aliases += (i ? L", " : L"") + al[i];
        }
        if (inMaster) {
            auto a = list.added.find(name);
            r.recent = a != list.added.end() && NameAddedRecently(a->second, today, 7);
        }
        g_rows.push_back(r);
    };
    std::set<std::wstring> seen;
    for (const std::wstring& n : list.names) {
        if (seen.insert(n).second) addRow(n, true);
    }
    for (const auto& kv : counts) {
        if (!kv.first.empty() && seen.insert(kv.first).second) addRow(kv.first, false);
    }
    std::sort(g_rows.begin(), g_rows.end(), [](const ManageRow& a, const ManageRow& b) {
        return std::make_pair(NameKey(a.name), a.name) < std::make_pair(NameKey(b.name), b.name);
    });
}

int FindRow(const std::wstring& name) {
    for (size_t i = 0; i < g_rows.size(); i++) {
        if (g_rows[i].name == name) return (int)i;
    }
    return -1;
}

int SelectedRow() {
    if (!g_hList) return -1;
    int i = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    return (i >= 0 && i < (int)g_rows.size()) ? i : -1;
}

std::vector<int> TickedRows() {
    std::vector<int> out;
    const int n = (int)g_rows.size();
    for (int i = 0; i < n; i++) {
        if (ListView_GetCheckState(g_hList, i)) out.push_back(i);
    }
    return out;
}

// Enables/disables and fills everything below and around the list from the current selection and ticks.
void RefreshControls() {
    const int sel = SelectedRow();
    const std::vector<int> ticked = TickedRows();

    // Tick summary + Merge button
    std::wstring tickText = ticked.empty() ? L"Nothing ticked" : std::to_wstring(ticked.size()) + L" ticked";
    SetWindowTextW(g_hTickLbl, tickText.c_str());
    std::wstring mergeText = ticked.size() >= 2 ? L"Merge ticked (" + std::to_wstring(ticked.size()) + L")..." : L"Merge ticked...";
    SetWindowTextW(g_hMergeBtn, mergeText.c_str());
    EnableWindow(g_hMergeBtn, ticked.size() >= 2 ? TRUE : FALSE);

    // Name / email boxes follow the selection
    g_settingText = true;
    if (sel >= 0) {
        SetWindowTextW(g_hNameEdit, g_rows[sel].name.c_str());
        SetWindowTextW(g_hEmailEdit, g_rows[sel].email.c_str());
    } else {
        SetWindowTextW(g_hNameEdit, L"");
        SetWindowTextW(g_hEmailEdit, L"");
    }
    g_settingText = false;
    EnableWindow(g_hNameEdit, sel >= 0 ? TRUE : FALSE);
    EnableWindow(g_hEmailEdit, (sel >= 0 && g_isSupplier) ? TRUE : FALSE);
    EnableWindow(g_hEmailBtn, (sel >= 0 && g_isSupplier) ? TRUE : FALSE);
    ShowWindow(g_hEmailLbl, g_isSupplier ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hEmailEdit, g_isSupplier ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hEmailBtn, g_isSupplier ? SW_SHOW : SW_HIDE);

    // Add to / Remove from list
    if (sel < 0) {
        SetWindowTextW(g_hListBtn, L"Remove from list");
        EnableWindow(g_hListBtn, FALSE);
    } else if (!g_rows[sel].inMaster) {
        SetWindowTextW(g_hListBtn, L"Add to list");
        EnableWindow(g_hListBtn, TRUE);
    } else {
        SetWindowTextW(g_hListBtn, L"Remove from list");
        EnableWindow(g_hListBtn, g_rows[sel].entries == 0 ? TRUE : FALSE); // a name this sheet uses can't just vanish
    }
    EnableWindow(g_hUndoBtn, g_undo.valid ? TRUE : FALSE);

    // Aliases: only for a name that is in the list. Add needs a typed alias; Remove needs a name that has aliases.
    const bool aliasOk = sel >= 0 && g_rows[sel].inMaster;
    SendMessageW(g_hAliasEdit, CB_RESETCONTENT, 0, 0);
    SetWindowTextW(g_hAliasEdit, L"");
    if (aliasOk) {
        for (const std::wstring& a : AliasesOf(CurrentList(), g_rows[sel].name)) {
            SendMessageW(g_hAliasEdit, CB_ADDSTRING, 0, (LPARAM)a.c_str());
        }
    }
    EnableWindow(g_hAliasEdit, aliasOk ? TRUE : FALSE);
    EnableWindow(g_hAliasAddBtn, aliasOk ? TRUE : FALSE);
    EnableWindow(g_hAliasRemBtn, (aliasOk && !g_rows[sel].aliases.empty()) ? TRUE : FALSE);

    // Rename is available once the box holds a different, non-empty name
    bool canRename = false;
    if (sel >= 0) {
        wchar_t buf[kMaxSupplierSpeciesLength + 2];
        GetWindowTextW(g_hNameEdit, buf, (int)(kMaxSupplierSpeciesLength + 2));
        const std::wstring typed = CollapseNameSpaces(buf);
        canRename = !typed.empty() && typed != g_rows[sel].name;
    }
    EnableWindow(g_hRenameBtn, canRename ? TRUE : FALSE);

    // Duplicates strip
    std::vector<std::wstring> names;
    for (const ManageRow& r : g_rows) names.push_back(r.name);
    auto groups = FindDuplicateNameGroups(names);
    g_dupGroup = groups.empty() ? std::vector<std::wstring>() : groups[0];
    if (g_dupGroup.size() > 1) {
        std::wstring text = L"Possible duplicates:  ";
        for (size_t i = 0; i < g_dupGroup.size(); i++) {
            if (i) text += L"  /  ";
            text += g_dupGroup[i];
        }
        SetWindowTextW(g_hDupLbl, text.c_str());
        ShowWindow(g_hDupLbl, SW_SHOW);
        ShowWindow(g_hDupBtn, SW_SHOW);
    } else {
        ShowWindow(g_hDupLbl, SW_HIDE);
        ShowWindow(g_hDupBtn, SW_HIDE);
    }
}

void SetColumnWidths() {
    // Species has no email, so that column collapses to nothing.
    ListView_SetColumnWidth(g_hList, 2, g_isSupplier ? S(190) : 0);
}

// Rebuilds the list. `selectName` (if still present) stays selected so the boxes below keep showing it.
void PopulateList(const std::wstring& selectName) {
    g_populating = true;
    ListView_DeleteAllItems(g_hList);
    BuildRows();
    for (size_t i = 0; i < g_rows.size(); i++) {
        const ManageRow& r = g_rows[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = (int)i;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(r.name.c_str());
        ListView_InsertItem(g_hList, &item);
        std::wstring cnt = std::to_wstring(r.entries);
        ListView_SetItemText(g_hList, (int)i, 1, const_cast<LPWSTR>(cnt.c_str()));
        std::wstring email = r.email.empty() && g_isSupplier ? L"no email saved" : r.email;
        ListView_SetItemText(g_hList, (int)i, 2, const_cast<LPWSTR>(email.c_str()));
        std::wstring note = !r.inMaster ? L"not in list" : (r.recent ? L"new" : L"");
        ListView_SetItemText(g_hList, (int)i, 3, const_cast<LPWSTR>(r.aliases.c_str()));
        ListView_SetItemText(g_hList, (int)i, 4, const_cast<LPWSTR>(note.c_str()));
    }
    SetColumnWidths();
    int idx = selectName.empty() ? -1 : FindRow(selectName);
    if (idx >= 0) {
        ListView_SetItemState(g_hList, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g_hList, idx, FALSE);
    }
    g_populating = false;
    RefreshControls();
}

// ---------------------------------------------------------------------------
// Choosing which name survives a merge
// ---------------------------------------------------------------------------

typedef HRESULT(WINAPI* TaskDialogIndirectFn)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);

// Returns the index (into `names`) of the name to keep, or -1 for Cancel. Uses a Task Dialog with one radio button
// per name where available (needs the comctl32 v6 manifest, Vista or later); otherwise - or with many names - a
// plain Yes/No question about the most-used name.
int ChooseKeepName(HWND owner, const std::vector<std::wstring>& names, const std::vector<int>& counts, int defaultIdx,
                   const std::wstring& extra) {
    int total = 0;
    for (int c : counts) total += c;

    TaskDialogIndirectFn fn = nullptr;
    if (HMODULE h = GetModuleHandleW(L"comctl32.dll")) {
        fn = reinterpret_cast<TaskDialogIndirectFn>(reinterpret_cast<void*>(GetProcAddress(h, "TaskDialogIndirect")));
    }
    if (fn && names.size() <= 8) {
        const int kRadioBase = 1000;
        std::vector<std::wstring> labels;
        std::vector<TASKDIALOG_BUTTON> radios;
        for (size_t i = 0; i < names.size(); i++) {
            labels.push_back(names[i] + L"   (" + EntriesText(total - counts[i]) + L" change)");
        }
        for (size_t i = 0; i < names.size(); i++) {
            TASKDIALOG_BUTTON b;
            b.nButtonID = kRadioBase + (int)i;
            b.pszButtonText = labels[i].c_str();
            radios.push_back(b);
        }
        TASKDIALOG_BUTTON buttons[2];
        buttons[0].nButtonID = IDOK;
        buttons[0].pszButtonText = L"Merge";
        buttons[1].nButtonID = IDCANCEL;
        buttons[1].pszButtonText = L"Cancel";

        const std::wstring instruction = L"Merge " + std::to_wstring(names.size()) + L" names into one?";
        const std::wstring content = L"Choose the name to keep. The others are replaced everywhere they appear, "
                                     L"and the old spellings are corrected automatically from now on." +
                                     (extra.empty() ? std::wstring() : L"\n\n" + extra);
        TASKDIALOGCONFIG cfg{};
        cfg.cbSize = sizeof(cfg);
        cfg.hwndParent = owner;
        cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
        cfg.pszWindowTitle = L"Merge Names";
        cfg.pszMainInstruction = instruction.c_str();
        cfg.pszContent = content.c_str();
        cfg.cButtons = 2;
        cfg.pButtons = buttons;
        cfg.nDefaultButton = IDOK;
        cfg.cRadioButtons = (UINT)radios.size();
        cfg.pRadioButtons = radios.data();
        cfg.nDefaultRadioButton = kRadioBase + defaultIdx;
        int button = 0, radio = 0;
        if (SUCCEEDED(fn(&cfg, &button, &radio, nullptr))) {
            if (button != IDOK) return -1;
            const int idx = radio - kRadioBase;
            return (idx >= 0 && idx < (int)names.size()) ? idx : defaultIdx;
        }
        // Task Dialog failed to show: fall through to the plain question.
    }

    std::wstring msg = L"Merge " + std::to_wstring(names.size()) + L" names into \"" + names[defaultIdx] + L"\"?\n\n" +
                       EntriesText(total - counts[defaultIdx]) + L" will change. The other names are replaced "
                       L"everywhere they appear.";
    if (!extra.empty()) msg += L"\n\n" + extra;
    return MessageBoxW(owner, msg.c_str(), L"Merge Names", MB_YESNO | MB_ICONQUESTION) == IDYES ? defaultIdx : -1;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void TakeUndoSnapshot() {
    g_undo.valid = true;
    g_undo.entries = g_entries;
    g_undo.master = g_nameMaster;
    g_undo.emails = g_supplierEmails;
    g_undo.dirty = g_dirty;
}

// Folds `sources` into `target` on the open sheet, in the master list and (suppliers) in the email list.
void ExecuteMerge(const std::vector<std::wstring>& sources, const std::wstring& target, bool isRename = false) {
    TakeUndoSnapshot();

    // Email: the kept supplier's own address wins; if it has none, the first address found on the others.
    bool emailsChanged = false;
    std::wstring keptEmail;
    if (g_isSupplier) {
        auto t = g_supplierEmails.find(target);
        if (t != g_supplierEmails.end()) keptEmail = t->second;
        for (const std::wstring& s : sources) {
            if (s == target) continue;
            auto it = g_supplierEmails.find(s);
            if (keptEmail.empty() && it != g_supplierEmails.end()) keptEmail = it->second;
            if (it != g_supplierEmails.end()) { g_supplierEmails.erase(it); emailsChanged = true; }
        }
        if (!keptEmail.empty()) {
            auto t2 = g_supplierEmails.find(target);
            if (t2 == g_supplierEmails.end() || t2->second != keptEmail) {
                g_supplierEmails[target] = keptEmail;
                emailsChanged = true;
            }
        }
    }

    int changed = 0;
    for (Entry& e : g_entries) {
        std::wstring& field = g_isSupplier ? e.supplier : e.product;
        if (field == target) continue;
        for (const std::wstring& s : sources) {
            if (field == s) { field = target; changed++; break; }
        }
    }

    NameListMerge(CurrentList(), sources, target, TodayISO());
    const bool masterSaved = SaveNameMaster();
    const bool emailsSaved = emailsChanged ? SaveSupplierEmails() : true;

    if (g_editIndex != -1) CancelEdit();
    // A pending Undo Delete buffer may hold a copy of a row carrying an old spelling; undoing it afterwards would
    // bring the variant back (same rule as the old Apply button), so any actual change drops it.
    if (changed > 0) { ClearUndoState(); g_dirty = true; }
    RefreshAll();
    PopulateList(target);

    std::wstring status = isRename
        ? L"Renamed \"" + sources[0] + L"\" to \"" + target + L"\". " + EntriesText(changed) + L" updated."
        : L"Merged " + std::to_wstring(sources.size()) + L" names into \"" + target + L"\". " + EntriesText(changed) + L" updated.";
    SetStatus(status);
    if (!masterSaved || !emailsSaved) {
        MessageBoxW(g_hManageWnd,
                    L"The change was made, but names.txt or emails.txt could not be written. It will be lost when "
                    L"the app closes unless a later save succeeds. Check that neither file is open in another "
                    L"program.",
                    L"Save Failed", MB_OK | MB_ICONWARNING);
    }
}

void DoMergeTicked() {
    const std::vector<int> ticked = TickedRows();
    if (ticked.size() < 2) return;
    std::vector<std::wstring> names;
    std::vector<int> counts;
    int best = 0;
    for (size_t i = 0; i < ticked.size(); i++) {
        names.push_back(g_rows[ticked[i]].name);
        counts.push_back(g_rows[ticked[i]].entries);
        if (counts[i] > counts[best]) best = (int)i;
    }

    std::wstring extra;
    if (g_isSupplier) {
        std::vector<std::wstring> distinct;
        std::wstring lines;
        for (int ri : ticked) {
            const ManageRow& r = g_rows[ri];
            if (r.email.empty()) continue;
            if (std::find(distinct.begin(), distinct.end(), r.email) == distinct.end()) distinct.push_back(r.email);
            lines += L"\n  " + r.name + L"  -  " + r.email;
        }
        if (distinct.size() > 1) {
            extra = L"These suppliers have different email addresses:" + lines +
                    L"\n\nThe kept supplier's address is used (or the first one above, if it has none). You can "
                    L"change it afterwards.";
        }
    }
    const int keep = ChooseKeepName(g_hManageWnd, names, counts, best, extra);
    if (keep < 0) return; // cancelled - nothing has changed
    ExecuteMerge(names, names[keep]);
}

void DoTickDuplicates() {
    const int n = (int)g_rows.size();
    for (int i = 0; i < n; i++) {
        const bool want = std::find(g_dupGroup.begin(), g_dupGroup.end(), g_rows[i].name) != g_dupGroup.end();
        ListView_SetCheckState(g_hList, i, want ? TRUE : FALSE);
    }
    RefreshControls();
}

void DoRename() {
    const int sel = SelectedRow();
    if (sel < 0) return;
    wchar_t buf[kMaxSupplierSpeciesLength + 2];
    GetWindowTextW(g_hNameEdit, buf, (int)(kMaxSupplierSpeciesLength + 2));
    const std::wstring newName = CollapseNameSpaces(buf);
    const std::wstring oldName = g_rows[sel].name;
    if (newName.empty() || newName == oldName) return;
    if (newName.find(L'|') != std::wstring::npos) {
        MessageBoxW(g_hManageWnd, L"Names can't contain the '|' character - it's used internally as a separator when saving.",
                    L"Invalid Name", MB_OK | MB_ICONWARNING);
        return;
    }

    // Typing a name that is already in the list (even just in different capitals) means "merge into that one".
    std::wstring existing;
    const std::wstring newKey = NameKey(newName);
    for (const ManageRow& r : g_rows) {
        if (r.name != oldName && (r.name == newName || (!newKey.empty() && NameKey(r.name) == newKey))) {
            existing = r.name;
            break;
        }
    }
    if (!existing.empty()) {
        const std::wstring msg = L"\"" + existing + L"\" is already in the list.\n\nMerge \"" + oldName + L"\" into \"" +
                                 existing + L"\"? " + EntriesText(g_rows[sel].entries) + L" will change.";
        if (MessageBoxW(g_hManageWnd, msg.c_str(), L"Merge Names", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        ExecuteMerge({ oldName }, existing);
        return;
    }
    ExecuteMerge({ oldName }, newName, true);
}

void DoSaveEmail() {
    const int sel = SelectedRow();
    if (sel < 0 || !g_isSupplier) return;
    const std::wstring supplier = g_rows[sel].name;
    wchar_t buf[kMaxSupplierSpeciesLength + 2];
    GetWindowTextW(g_hEmailEdit, buf, (int)(kMaxSupplierSpeciesLength + 2));
    const std::wstring email = TrimW(buf);

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
    const bool saved = SaveSupplierEmails();
    PopulateList(supplier);
    if (saved) {
        SetStatus(email.empty() ? L"Email cleared for \"" + supplier + L"\"." : L"Email saved for \"" + supplier + L"\".");
    } else {
        MessageBoxW(g_hManageWnd,
                    L"Could not save the email address to disk - the change is still in memory for this session, but "
                    L"will be lost when the app closes unless the save succeeds. Check that emails.txt isn't open in "
                    L"another program and try again.",
                    L"Save Failed", MB_OK | MB_ICONWARNING);
    }
}

void DoAddRemoveListName() {
    const int sel = SelectedRow();
    if (sel < 0) return;
    const std::wstring name = g_rows[sel].name;
    std::wstring status;
    bool removed = false;
    if (!g_rows[sel].inMaster) {
        NameListAdd(CurrentList(), name, TodayISO());
        status = L"\"" + name + L"\" added to the list.";
    } else if (g_rows[sel].entries == 0) {
        NameListRemove(CurrentList(), name);
        status = L"\"" + name + L"\" removed from the list.";
        removed = true;
    } else {
        return;
    }
    const bool saved = SaveNameMaster();
    PopulateList(removed ? std::wstring() : name);
    SetStatus(status);
    if (!saved) {
        MessageBoxW(g_hManageWnd, L"Could not save names.txt. The change is in memory only until a later save succeeds.",
                    L"Save Failed", MB_OK | MB_ICONWARNING);
    }
}

void DoAddAlias() {
    const int sel = SelectedRow();
    if (sel < 0 || !g_rows[sel].inMaster) return;
    const std::wstring name = g_rows[sel].name;
    wchar_t buf[kMaxSupplierSpeciesLength + 2];
    GetWindowTextW(g_hAliasEdit, buf, (int)(kMaxSupplierSpeciesLength + 2));
    const std::wstring typed = CollapseNameSpaces(buf);
    // Would this replace an alias that points somewhere else? Say so in the status line afterwards.
    std::wstring movedFrom;
    {
        const NameList& cur = CurrentList();
        for (const auto& kv : cur.aliases) {
            if (NameKey(kv.first) == NameKey(typed) && kv.second != name) movedFrom = kv.second;
        }
    }
    const UndoSnapshot previousUndo = g_undo;
    TakeUndoSnapshot();
    const AliasAddResult res = NameListAddAlias(CurrentList(), typed, name);
    if (res != AliasAddResult::Added) {
        g_undo = previousUndo; // nothing changed - keep whatever Undo was there before
        switch (res) {
        case AliasAddResult::Empty: SetStatus(L"Type the other spelling first, then press Add alias."); break;
        case AliasAddResult::SameAsName: SetStatus(L"\"" + typed + L"\" is already this name (capitals and spacing are matched automatically)."); break;
        case AliasAddResult::IsAName: SetStatus(L"\"" + typed + L"\" is already a name in the list - tick both and use Merge instead."); break;
        default: SetStatus(L"That name is not in the list."); break;
        }
        RefreshControls();
        return;
    }
    const bool saved = SaveNameMaster();
    PopulateList(name);
    SetStatus(L"\"" + typed + L"\" will now be read as \"" + name + L"\"." +
              (movedFrom.empty() ? L"" : L" (It used to mean \"" + movedFrom + L"\".)"));
    if (!saved) {
        MessageBoxW(g_hManageWnd, L"Could not save names.txt. The change is in memory only until a later save succeeds.",
                    L"Save Failed", MB_OK | MB_ICONWARNING);
    }
}

// Removes the alias typed in the box if it belongs to the selected name; with an empty box, removes the name's only
// alias, or asks which one when there are several.
void DoRemoveAlias() {
    const int sel = SelectedRow();
    if (sel < 0 || !g_rows[sel].inMaster) return;
    const std::wstring name = g_rows[sel].name;
    const std::vector<std::wstring> mine = AliasesOf(CurrentList(), name);
    if (mine.empty()) return;
    wchar_t buf[kMaxSupplierSpeciesLength + 2];
    GetWindowTextW(g_hAliasEdit, buf, (int)(kMaxSupplierSpeciesLength + 2));
    const std::wstring typed = CollapseNameSpaces(buf);
    std::wstring target;
    if (typed.empty()) {
        if (mine.size() == 1) target = mine[0];
        else { SetStatus(L"Pick which one to remove from the drop-down box (" + g_rows[sel].aliases + L")."); return; }
    } else {
        for (const std::wstring& a : mine) {
            if (NameKey(a) == NameKey(typed)) target = a;
        }
        if (target.empty()) { SetStatus(L"\"" + typed + L"\" is not one of this name's other spellings (" + g_rows[sel].aliases + L")."); return; }
    }
    TakeUndoSnapshot();
    NameListRemoveAlias(CurrentList(), target);
    const bool saved = SaveNameMaster();
    PopulateList(name);
    SetStatus(L"\"" + target + L"\" is no longer read as \"" + name + L"\".");
    if (!saved) {
        MessageBoxW(g_hManageWnd, L"Could not save names.txt. The change is in memory only until a later save succeeds.",
                    L"Save Failed", MB_OK | MB_ICONWARNING);
    }
}

void DoUndo() {
    if (!g_undo.valid) return;
    if (g_editIndex != -1) CancelEdit();
    g_entries = g_undo.entries;
    g_nameMaster = g_undo.master;
    g_supplierEmails = g_undo.emails;
    g_dirty = g_undo.dirty;
    g_undo.valid = false;
    ClearUndoState();
    const bool a = SaveNameMaster();
    const bool b = SaveSupplierEmails();
    RefreshAll();
    PopulateList(L"");
    SetStatus(L"Undone.");
    if (!a || !b) {
        MessageBoxW(g_hManageWnd, L"The merge was undone on screen, but names.txt or emails.txt could not be written.",
                    L"Save Failed", MB_OK | MB_ICONWARNING);
    }
}

// Esc closes the popup from any control; Enter in the Name / Email boxes presses Rename / Save Email. This is a
// plain window, not a dialog, so none of that comes for free (same approach as DrillDownWindow.cpp).
LRESULT CALLBACK ManageChildSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        DestroyWindow(GetParent(hwnd));
        return 0; // hwnd is destroyed with its parent - nothing may touch it after this
    }
    if ((msg == WM_KEYDOWN || msg == WM_CHAR) && wParam == VK_RETURN) {
        const int id = GetDlgCtrlID(hwnd);
        if (msg == WM_KEYDOWN) {
            if (id == ID_MNG_NAME_EDIT) PostMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(ID_MNG_RENAME, BN_CLICKED), (LPARAM)g_hRenameBtn);
            if (id == ID_MNG_EMAIL_EDIT) PostMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(ID_MNG_EMAIL_SAVE, BN_CLICKED), (LPARAM)g_hEmailBtn);
        }
        if (id == ID_MNG_NAME_EDIT || id == ID_MNG_EMAIL_EDIT) return 0; // swallow the Enter so there is no beep
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, ManageChildSubclassProc, 1);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK ManageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hRadioSupplier = MakeControl(L"BUTTON", L"Suppliers", WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_GROUP, ID_MNG_RADIO_SUPPLIER, hwnd);
        g_hRadioSpecies = MakeControl(L"BUTTON", L"Species", WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE, ID_MNG_RADIO_SPECIES, hwnd);
        // SS_NOPREFIX everywhere a name can appear: "Jenkins & Son" must show the '&' literally.
        g_hDupLbl = MakeControl(L"STATIC", L"", WS_VISIBLE | SS_NOPREFIX | SS_ENDELLIPSIS, 0, hwnd);
        g_hDupBtn = MakeControl(L"BUTTON", L"Tick these", WS_VISIBLE | WS_TABSTOP, ID_MNG_DUP_TICK, hwnd);
        g_hHint = MakeControl(L"STATIC", L"Click a name to view or edit it. Tick boxes only when you want to merge names.",
                              WS_VISIBLE | SS_NOPREFIX, 0, hwnd);
        g_hList = MakeControl(L"SysListView32", L"",
                              WS_VISIBLE | WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                              ID_MNG_LIST, hwnd);
        g_hTickLbl = MakeControl(L"STATIC", L"", WS_VISIBLE | SS_NOPREFIX, 0, hwnd);
        g_hMergeBtn = MakeControl(L"BUTTON", L"Merge ticked...", WS_VISIBLE | WS_TABSTOP, ID_MNG_MERGE, hwnd);
        g_hNameLbl = MakeControl(L"STATIC", L"Name:", WS_VISIBLE, 0, hwnd);
        g_hNameEdit = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_MNG_NAME_EDIT, hwnd);
        g_hRenameBtn = MakeControl(L"BUTTON", L"Rename", WS_VISIBLE | WS_TABSTOP, ID_MNG_RENAME, hwnd);
        g_hListBtn = MakeControl(L"BUTTON", L"Remove from list", WS_VISIBLE | WS_TABSTOP, ID_MNG_LISTBTN, hwnd);
        g_hEmailLbl = MakeControl(L"STATIC", L"Email:", WS_VISIBLE, 0, hwnd);
        g_hEmailEdit = MakeControl(L"EDIT", L"", WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_MNG_EMAIL_EDIT, hwnd);
        g_hEmailBtn = MakeControl(L"BUTTON", L"Save Email", WS_VISIBLE | WS_TABSTOP, ID_MNG_EMAIL_SAVE, hwnd);
        g_hAliasLbl = MakeControl(L"STATIC", L"Also known as:", WS_VISIBLE | SS_NOPREFIX, 0, hwnd);
        // A drop-down box: pick one of the name's existing aliases (to remove it) or type a new one (to add it).
        g_hAliasEdit = MakeControl(L"COMBOBOX", L"", WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL, ID_MNG_ALIAS_EDIT, hwnd);
        g_hAliasAddBtn = MakeControl(L"BUTTON", L"Add alias", WS_VISIBLE | WS_TABSTOP, ID_MNG_ALIAS_ADD, hwnd);
        g_hAliasRemBtn = MakeControl(L"BUTTON", L"Remove alias", WS_VISIBLE | WS_TABSTOP, ID_MNG_ALIAS_REMOVE, hwnd);
        g_hStatus = MakeControl(L"STATIC", L"", WS_VISIBLE | SS_NOPREFIX | SS_ENDELLIPSIS, 0, hwnd);
        g_hUndoBtn = MakeControl(L"BUTTON", L"Undo", WS_VISIBLE | WS_TABSTOP, ID_MNG_UNDO, hwnd);
        g_hCloseBtn = MakeControl(L"BUTTON", L"Close", WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, ID_MNG_CLOSE, hwnd);
        if (!g_hRadioSupplier || !g_hRadioSpecies || !g_hDupLbl || !g_hDupBtn || !g_hHint || !g_hList || !g_hTickLbl ||
            !g_hMergeBtn || !g_hNameLbl || !g_hNameEdit || !g_hRenameBtn || !g_hListBtn || !g_hEmailLbl ||
            !g_hEmailEdit || !g_hEmailBtn || !g_hAliasLbl || !g_hAliasEdit || !g_hAliasAddBtn || !g_hAliasRemBtn || !g_hStatus || !g_hUndoBtn || !g_hCloseBtn) {
            SetLastError(ERROR_INVALID_WINDOW_HANDLE);
            return -1;
        }

        // Names and emails share the 255-character bound used everywhere else (see ApplyFieldLengthLimits).
        {
            FieldLengthLimitTargets limits;
            limits.manageTargetEdit = g_hNameEdit;
            limits.manageEmailEdit = g_hEmailEdit;
            SendMessageW(g_hAliasEdit, CB_LIMITTEXT, (WPARAM)kMaxSupplierSpeciesLength, 0);
            ApplyFieldLengthLimits(limits);
        }

        ListView_SetExtendedListViewStyle(g_hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES);
        AddColumn(g_hList, 0, L"Name", 230);
        AddColumn(g_hList, 1, L"Entries", 65);
        AddColumn(g_hList, 2, L"Email", 210);
        AddColumn(g_hList, 3, L"Also known as", 170);
        AddColumn(g_hList, 4, L"", 75);

        SetWindowSubclass(g_hList, ManageChildSubclassProc, 1, 0);
        SetWindowSubclass(g_hNameEdit, ManageChildSubclassProc, 1, 0);
        SetWindowSubclass(g_hEmailEdit, ManageChildSubclassProc, 1, 0);
        SetWindowSubclass(g_hCloseBtn, ManageChildSubclassProc, 1, 0);

        g_isSupplier = true;
        g_undo = UndoSnapshot();
        SendMessageW(g_hRadioSupplier, BM_SETCHECK, BST_CHECKED, 0);
        SetStatus(L"");
        PopulateList(L"");
        return 0;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        const int w = rc.right;
        MoveWindow(g_hRadioSupplier, S(15), S(12), S(110), S(28), TRUE);
        MoveWindow(g_hRadioSpecies, S(125), S(12), S(110), S(28), TRUE);
        MoveWindow(g_hDupLbl, S(15), S(48), w - S(30) - S(115), S(22), TRUE);
        MoveWindow(g_hDupBtn, w - S(15) - S(105), S(46), S(105), S(26), TRUE);
        MoveWindow(g_hHint, S(15), S(76), w - S(30), S(20), TRUE);
        const int listBottom = std::max((int)rc.bottom - S(263), S(160));
        MoveWindow(g_hList, S(15), S(100), w - S(30), listBottom - S(100), TRUE);
        MoveWindow(g_hTickLbl, S(15), listBottom + S(12), S(200), S(22), TRUE);
        MoveWindow(g_hMergeBtn, w - S(15) - S(150), listBottom + S(8), S(150), S(28), TRUE);

        const int y1 = listBottom + S(50);
        const int btnRight = w - S(15);
        MoveWindow(g_hNameLbl, S(15), y1 + S(3), S(50), S(22), TRUE);
        MoveWindow(g_hListBtn, btnRight - S(120), y1 - S(1), S(120), S(28), TRUE);
        MoveWindow(g_hRenameBtn, btnRight - S(120) - S(8) - S(85), y1 - S(1), S(85), S(28), TRUE);
        MoveWindow(g_hNameEdit, S(115), y1, btnRight - S(120) - S(8) - S(85) - S(8) - S(115), S(24), TRUE);
        const int y2 = y1 + S(38);
        MoveWindow(g_hEmailLbl, S(15), y2 + S(3), S(50), S(22), TRUE);
        MoveWindow(g_hEmailBtn, btnRight - S(120), y2 - S(1), S(120), S(28), TRUE);
        MoveWindow(g_hEmailEdit, S(115), y2, btnRight - S(120) - S(8) - S(115), S(24), TRUE);
        const int y3 = y2 + S(38);
        MoveWindow(g_hAliasLbl, S(15), y3 + S(3), S(100), S(22), TRUE);
        MoveWindow(g_hAliasRemBtn, btnRight - S(120), y3 - S(1), S(120), S(28), TRUE);
        MoveWindow(g_hAliasAddBtn, btnRight - S(120) - S(8) - S(85), y3 - S(1), S(85), S(28), TRUE);
        MoveWindow(g_hAliasEdit, S(115), y3, btnRight - S(120) - S(8) - S(85) - S(8) - S(115), S(220), TRUE); // height = dropdown list

        MoveWindow(g_hStatus, S(15), rc.bottom - S(46), w - S(30) - S(190), S(22), TRUE);
        MoveWindow(g_hUndoBtn, w - S(15) - S(85) - S(8) - S(85), rc.bottom - S(50), S(85), S(30), TRUE);
        MoveWindow(g_hCloseBtn, w - S(15) - S(85), rc.bottom - S(50), S(85), S(30), TRUE);
        return 0;
    }
    case WM_NOTIFY: {
        LPNMHDR hdr = (LPNMHDR)lParam;
        if (hdr->idFrom == ID_MNG_LIST && hdr->code == LVN_ITEMCHANGED && !g_populating) {
            LPNMLISTVIEW nm = (LPNMLISTVIEW)lParam;
            if ((nm->uChanged & LVIF_STATE) && ((nm->uOldState ^ nm->uNewState) & (LVIS_SELECTED | LVIS_STATEIMAGEMASK))) {
                RefreshControls();
            }
            return 0;
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if ((id == ID_MNG_RADIO_SUPPLIER || id == ID_MNG_RADIO_SPECIES) && code == BN_CLICKED) {
            g_isSupplier = (id == ID_MNG_RADIO_SUPPLIER);
            SetStatus(L"");
            PopulateList(L"");
            return 0;
        }
        if (id == ID_MNG_NAME_EDIT && code == EN_CHANGE) {
            if (!g_settingText) {
                const int sel = SelectedRow();
                bool canRename = false;
                if (sel >= 0) {
                    wchar_t buf[kMaxSupplierSpeciesLength + 2];
                    GetWindowTextW(g_hNameEdit, buf, (int)(kMaxSupplierSpeciesLength + 2));
                    const std::wstring typed = CollapseNameSpaces(buf);
                    canRename = !typed.empty() && typed != g_rows[sel].name;
                }
                EnableWindow(g_hRenameBtn, canRename ? TRUE : FALSE);
            }
            return 0;
        }
        if (code == BN_CLICKED) {
            if (id == ID_MNG_MERGE) { DoMergeTicked(); return 0; }
            if (id == ID_MNG_DUP_TICK) { DoTickDuplicates(); return 0; }
            if (id == ID_MNG_RENAME) { DoRename(); return 0; }
            if (id == ID_MNG_EMAIL_SAVE) { DoSaveEmail(); return 0; }
            if (id == ID_MNG_LISTBTN) { DoAddRemoveListName(); return 0; }
            if (id == ID_MNG_UNDO) { DoUndo(); return 0; }
            if (id == ID_MNG_ALIAS_ADD) { DoAddAlias(); return 0; }
            if (id == ID_MNG_ALIAS_REMOVE) { DoRemoveAlias(); return 0; }
            if (id == ID_MNG_CLOSE) { DestroyWindow(hwnd); return 0; }
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_undo = UndoSnapshot(); // an Undo is only offered while the window that made the change is open
        g_hList = nullptr;
        if (g_hMainWnd) EnableWindow(g_hMainWnd, TRUE);
        g_hManageWnd = nullptr;
        SetForegroundWindow(g_hMainWnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Entry-time checks
// ---------------------------------------------------------------------------

// Resolves one typed name. Returns false if the user chose to go back and edit. `note` collects what was done.
bool ResolveOneName(HWND owner, const wchar_t* kindLower, const wchar_t* kindTitle, NameList& list, std::wstring& name,
                    std::wstring& note) {
    const std::wstring typed = name;
    NameResolution r = ResolveName(list, typed);
    switch (r.kind) {
    case NameMatch::Exact:
    case NameMatch::New:
        name = r.name;
        break;
    case NameMatch::Snapped:
    case NameMatch::Alias:
        name = r.name;
        if (r.name != CollapseNameSpaces(typed)) {
            note += std::wstring(kindTitle) + L" \"" + CollapseNameSpaces(typed) + L"\" corrected to \"" + r.name + L"\". ";
        }
        break;
    case NameMatch::Near: {
        std::wstring msg = L"\"" + r.name + L"\" isn't in the " + kindLower + L" list.\n\nDid you mean \"" + r.similar[0] + L"\"?";
        if (r.similar.size() > 1) {
            msg += L"\n\n(Other close names: ";
            for (size_t i = 1; i < r.similar.size(); i++) {
                if (i > 1) msg += L", ";
                msg += L"\"" + r.similar[i] + L"\"";
            }
            msg += L")";
        }
        if (!r.similarVia.empty() && !r.similarVia[0].empty()) {
            msg += L"\n\n(\"" + r.similarVia[0] + L"\" is another name for it.)";
        }
        msg += L"\n\nYes = use \"" + r.similar[0] + L"\"\nNo = add \"" + r.name + L"\" as a new " + kindLower +
               L"\nCancel = go back and edit";
        const std::wstring title = std::wstring(L"New ") + kindTitle + L"?";
        const int a = MessageBoxW(owner, msg.c_str(), title.c_str(), MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);
        if (a == IDCANCEL) return false;
        if (a == IDYES) {
            name = r.similar[0];
            note += std::wstring(kindTitle) + L" \"" + r.name + L"\" changed to \"" + name + L"\". ";
        } else {
            name = r.name;
        }
        break;
    }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Public: entry-time checks (called by CommitEntryForm in main.cpp)
// ---------------------------------------------------------------------------

bool ResolveEntryNames(std::wstring& supplier, std::wstring& product, std::wstring& note) {
    EnsureNameMasterSeeded();
    const std::wstring origSupplier = supplier, origProduct = product;
    std::wstring s = supplier, p = product;
    std::wstring n;
    if (!ResolveOneName(g_hMainWnd, L"supplier", L"Supplier", g_nameMaster.suppliers, s, n)) {
        SetFocus(hCmbSupplier);
        return false;
    }
    if (!ResolveOneName(g_hMainWnd, L"species", L"Species", g_nameMaster.species, p, n)) {
        SetFocus(hCmbProduct);
        return false;
    }
    supplier = s;
    product = p;
    note += n;
    // Show the list's spelling in the form too (Supplier stays filled for the next row).
    if (s != origSupplier) { SetWindowTextW(hCmbSupplier, s.c_str()); g_prevSupplierLen = (int)s.size(); }
    if (p != origProduct) { SetWindowTextW(hCmbProduct, p.c_str()); g_prevProductLen = (int)p.size(); }
    return true;
}

void RecordEntryNames(const std::wstring& supplier, const std::wstring& product, std::wstring& note) {
    const std::wstring today = TodayISO();
    bool changed = false;
    if (NameListAdd(g_nameMaster.suppliers, supplier, today)) {
        note += L"New supplier \"" + supplier + L"\" added to the list. ";
        changed = true;
    }
    if (NameListAdd(g_nameMaster.species, product, today)) {
        note += L"New species \"" + product + L"\" added to the list. ";
        changed = true;
    }
    if (changed && !SaveNameMaster()) note += L"(names.txt could not be saved.) ";
}

void OpenManageNamesWindow() {
    if (g_hManageWnd) {
        SetForegroundWindow(g_hManageWnd);
        return;
    }
    EnsureNameMasterSeeded();

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
    int w = S(760), h = S(690);
    int x = mrc.left + ((mrc.right - mrc.left) - w) / 2;
    int y = mrc.top + ((mrc.bottom - mrc.top) - h) / 2;
    FitPopupToWorkArea(g_hMainWnd, x, y, w, h); // never taller/wider than the screen

    g_hManageWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ManageNamesWindow", L"Manage Supplier / Species Names",
                                    WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h,
                                    g_hMainWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_hManageWnd) {
        MessageBoxW(g_hMainWnd, L"The Manage Names window could not be opened.", L"Manage Names", MB_OK | MB_ICONWARNING);
        return;
    }
    EnableWindow(g_hMainWnd, FALSE);
    ShowWindow(g_hManageWnd, SW_SHOW);
    UpdateWindow(g_hManageWnd);
}
