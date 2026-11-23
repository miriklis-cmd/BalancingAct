// ControlIds.h - window/control/menu/timer IDs (forms layer).
// Moved out of main.cpp unchanged in v0.10.2 (split stage 2).
#pragma once

enum {
    ID_FILE_NEW = 1, ID_FILE_OPEN, ID_FILE_SAVE, ID_FILE_SAVEAS, ID_FILE_PRINT, ID_FILE_PRINT_PREVIEW,
    ID_FILE_EXPORT_CSV, ID_FILE_EMAIL_SUPPLIERS, ID_FILE_EXIT, ID_FILE_ABOUT, ID_EDIT_UNDO_DELETE,
    ID_TOOLS_MANAGE_NAMES, ID_FILE_RESTORE_BACKUP, ID_ENTRY_CLEAR_FLAG,
    ID_TOOLS_SETTINGS, // v0.22.0 Tools > Settings (replaced the v0.20/0.21 Email method menu items)
    ID_TAB = 100,
    ID_CMB_SUPPLIER = 200, ID_CMB_PRODUCT, ID_EDIT_KGS, ID_EDIT_PRICE, ID_BTN_ADD, ID_BTN_DELETE,
    ID_BTN_EDIT, ID_BTN_CANCEL_EDIT, ID_EDIT_FILTER, ID_DTP_DATE, ID_EDIT_NOTES, ID_BTN_DUPLICATE,
    ID_BTN_DUPLICATE_SUPSPEC, ID_BTN_START_BOOK,
    ID_LIST_ENTRIES, ID_EDIT_DEBTOR, ID_EDIT_CASH,
    ID_LIST_OVERVIEW, ID_LIST_BREAKDOWN, ID_BTN_PRINT_BREAKDOWN, ID_BTN_EMAIL_SUPPLIERS,
    ID_BTN_PRINT_PREVIEW, ID_LIST_BYSPECIES,
    ID_EDIT_SUMFILTER_OV, ID_EDIT_SUMFILTER_BD, ID_EDIT_SUMFILTER_SP, // v0.19.0: shared filter on the summary tabs
    ID_BTN_FINALIZE, // Finalize/Un-finalize Day (ROADMAP.md item 3) - one
                      // button, relabeled, same pattern as Add/Update Entry
    // Manage Names popup window controls
    ID_MNG_RADIO_SUPPLIER = 400, ID_MNG_RADIO_SPECIES, ID_MNG_LIST, ID_MNG_NAME_EDIT, ID_MNG_RENAME, ID_MNG_CLOSE,
    ID_MNG_EMAIL_EDIT, ID_MNG_EMAIL_SAVE, ID_MNG_DUP_TICK, ID_MNG_MERGE, ID_MNG_LISTBTN, ID_MNG_UNDO, // v0.13.0
    ID_MNG_ALIAS_EDIT, ID_MNG_ALIAS_ADD, ID_MNG_ALIAS_REMOVE, // v0.14.0
    // Print Preview popup window controls
    ID_PREVIEW_PREV = 500, ID_PREVIEW_NEXT, ID_PREVIEW_PRINT, ID_PREVIEW_CLOSE,
    // Finalize Day date-prompt popup window controls
    ID_FIN_DTP = 600, ID_FIN_OK, ID_FIN_CANCEL,
    // Breakdown drill-down popup window controls (v0.12.6)
    ID_DRILL_LIST = 700, ID_DRILL_CLOSE,
    // v0.22.0 Settings window. The two radio groups must stay consecutive (CheckRadioButton uses the ranges).
    ID_SET_TAB = 1500, ID_SET_OK, ID_SET_CANCEL,
    ID_SET_EMAIL_MAILTO, ID_SET_EMAIL_EML, ID_SET_EMAIL_GMAIL,
    ID_SET_GMAIL_ACCOUNT, ID_SET_GMAIL_FORGET, ID_SET_SUBJECT, ID_SET_INTRO, ID_SET_SIGNOFF, ID_SET_SIGNATURE,
    ID_SET_BACKUP_MIN, ID_SET_BACKUP_KEEP,
    ID_SET_SENS_RELAXED, ID_SET_SENS_NORMAL, ID_SET_SENS_STRICT,
    ID_SET_DATA_PATH, ID_SET_OPEN_DATA,
    ID_SET_PRINTER, ID_SET_ORIENT, ID_SET_PAPER,                 // v0.23.0 (combo boxes)
    ID_SET_TEXT_100, ID_SET_TEXT_125, ID_SET_TEXT_150,           // consecutive radio group
    ID_SET_DATE_LONG, ID_SET_DATE_SHORT, ID_SET_DATE_NUM,        // consecutive radio group
    // v0.24.0 Email Suppliers notes dialog
    ID_NOTES_LIST = 1600, ID_NOTES_NOTSOLD, ID_NOTES_UP, ID_NOTES_DOWN, ID_NOTES_NEXT, ID_NOTES_SKIP, ID_NOTES_CREATE,
    // v0.12.0 Start Book... popup menu items (ID_BOOK_PICK_BASE + n = continue an existing book)
    ID_BOOK_NEW_DEBTOR = 990, ID_BOOK_NEW_CASH = 991, ID_BOOK_NONE = 992, ID_BOOK_PICK_BASE = 1000,
    // v0.16.0 right-click on the entries list: Move selected rows to a book (ID_MOVE_PICK_BASE + n = an existing book)
    ID_MOVE_NEW_DEBTOR = 993, ID_MOVE_NEW_CASH = 994, ID_MOVE_NONE = 995, ID_MOVE_PICK_BASE = 1100,
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
