// ControlIds.h - window/control/menu/timer IDs (forms layer).
// Moved out of main.cpp unchanged in v0.10.2 (split stage 2).
#pragma once

enum {
    ID_FILE_NEW = 1, ID_FILE_OPEN, ID_FILE_SAVE, ID_FILE_SAVEAS, ID_FILE_PRINT, ID_FILE_PRINT_PREVIEW,
    ID_FILE_EXPORT_CSV, ID_FILE_EMAIL_SUPPLIERS, ID_FILE_EXIT, ID_FILE_ABOUT, ID_EDIT_UNDO_DELETE,
    ID_TOOLS_MANAGE_NAMES, ID_FILE_RESTORE_BACKUP, ID_ENTRY_CLEAR_FLAG,
    ID_TAB = 100,
    ID_CMB_SUPPLIER = 200, ID_CMB_PRODUCT, ID_EDIT_KGS, ID_EDIT_PRICE, ID_BTN_ADD, ID_BTN_DELETE,
    ID_BTN_EDIT, ID_BTN_CANCEL_EDIT, ID_EDIT_FILTER, ID_DTP_DATE, ID_EDIT_NOTES, ID_BTN_DUPLICATE,
    ID_BTN_DUPLICATE_SUPSPEC, ID_BTN_START_BOOK,
    ID_LIST_ENTRIES, ID_EDIT_DEBTOR, ID_EDIT_CASH,
    ID_LIST_OVERVIEW, ID_LIST_BREAKDOWN, ID_BTN_PRINT_BREAKDOWN, ID_BTN_EMAIL_SUPPLIERS,
    ID_BTN_PRINT_PREVIEW, ID_LIST_BYSPECIES,
    ID_BTN_FINALIZE, // Finalize/Un-finalize Day (ROADMAP.md item 3) - one
                      // button, relabeled, same pattern as Add/Update Entry
    // Manage Names popup window controls
    ID_MNG_RADIO_SUPPLIER = 400, ID_MNG_RADIO_SPECIES, ID_MNG_LIST, ID_MNG_TARGET, ID_MNG_APPLY, ID_MNG_CLOSE,
    ID_MNG_EMAIL_EDIT, ID_MNG_EMAIL_SAVE,
    // Print Preview popup window controls
    ID_PREVIEW_PREV = 500, ID_PREVIEW_NEXT, ID_PREVIEW_PRINT, ID_PREVIEW_CLOSE,
    // Finalize Day date-prompt popup window controls
    ID_FIN_DTP = 600, ID_FIN_OK, ID_FIN_CANCEL,
    // v0.12.0 Start Book... popup menu items (ID_BOOK_PICK_BASE + n = continue an existing book)
    ID_BOOK_NEW_DEBTOR = 990, ID_BOOK_NEW_CASH = 991, ID_BOOK_NONE = 992, ID_BOOK_PICK_BASE = 1000,
    // Up to 8 Recent Files slots
    ID_RECENT_BASE = 900,
    // One-shot delayed retries for the combo box first-paint fix (see
    // WM_TIMER). v0.9.3/v0.9.11/v0.9.12 already tried a synchronous fix,
    // reordering WM_CREATE, and one 50ms delayed retry - still not fully
    // reliable per Jack's own v0.9.41 report ("doesn't always happen, but
    // see screenshot"), so v0.9.42 adds two further, later retries on the
    // theory that whatever theme-engine/relayout race causes this doesn't
    // always resolve within 50ms on every machine.
    ID_TIMER_FIRST_PAINT_FIX = 951,
    ID_TIMER_FIRST_PAINT_FIX2 = 953,
    ID_TIMER_FIRST_PAINT_FIX3 = 954,
    // Recurring backup-check tick (v0.9.29 fix) - see WM_TIMER. Independent
    // of AutosaveNow()/focus-loss, so an idle screen with unsaved data
    // still gets backed up on schedule, not just when the user happens to
    // tab between fields.
    ID_TIMER_BACKUP_CHECK = 952
};
