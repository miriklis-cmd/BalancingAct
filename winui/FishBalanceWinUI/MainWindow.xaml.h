#pragma once

#include "MainWindow.g.h"
#include <winrt/Microsoft.UI.Dispatching.h> // DispatcherQueueTimer member (autosave debounce)
#include "src/core/FishBalanceCore.h"
#include "src/core/NameMaster.h"   // master Supplier/Species lists with aliases (names.txt) - shared with the Win32 app
#include <map>
#include <string>
#include <vector>

namespace winrt::FishBalanceWinUI::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow()
        {
            // Xaml objects should not call InitializeComponent during construction.
            // See https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent
        }

        void Root_Loaded(Windows::Foundation::IInspectable const& sender,
                         Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void New_Click(Windows::Foundation::IInspectable const& sender,
                       Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Open_Click(Windows::Foundation::IInspectable const& sender,
                        Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Save_Click(Windows::Foundation::IInspectable const& sender,
                        Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void SaveAs_Click(Windows::Foundation::IInspectable const& sender,
                          Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Exit_Click(Windows::Foundation::IInspectable const& sender,
                        Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Tab_Tapped(Windows::Foundation::IInspectable const& sender,
                        Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& e);
        void Finalize_Click(Windows::Foundation::IInspectable const& sender,
                            Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Add_Click(Windows::Foundation::IInspectable const& sender,
                       Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Delete_Click(Windows::Foundation::IInspectable const& sender,
                          Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void UndoDelete_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void About_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ExportCsv_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ClearUndoDelete();                       // forget the one-level Undo Delete buffer (the sheet changed under it)
        void LoadRecentFiles();                       // recent.txt in the shared data folder (same file as the Win32 app)
        void RememberRecentFile(const std::wstring& path);
        void RebuildRecentMenu();
        void OpenRecent(const std::wstring& path);
        void RestoreWindowPlacement();                // WUI* keys in settings.txt (the Win32 app's X/Y/W/H are left alone)
        void SaveWindowPlacement();
        void Edit_Click(Windows::Foundation::IInspectable const& sender,
                        Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void EntriesList_DoubleTapped(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& e);
        void EntriesList_RightTapped(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const& e);
        void Combo_DropDownClosed(Windows::Foundation::IInspectable const& sender, Windows::Foundation::IInspectable const& e);
        void Combo_EnterKey(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void CancelEdit_Click(Windows::Foundation::IInspectable const& sender,
                              Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Duplicate_Click(Windows::Foundation::IInspectable const& sender,
                             Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Repeat_Click(Windows::Foundation::IInspectable const& sender,
                          Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Price_KeyDown(Windows::Foundation::IInspectable const& sender,
                           Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void Box_TextChanged(Windows::Foundation::IInspectable const& sender,
                             Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& e);
        void Filter_TextChanged(Windows::Foundation::IInspectable const& sender,
                                Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& e);
        void BreakdownList_DoubleTapped(Windows::Foundation::IInspectable const& sender,
                                        Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& e);
        void Print_Click(Windows::Foundation::IInspectable const& sender,
                         Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ManageNames_Click(Windows::Foundation::IInspectable const& sender,
                               Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void EmailAll_Click(Windows::Foundation::IInspectable const& sender,
                            Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void PrintPreview_Click(Windows::Foundation::IInspectable const& sender,
                                Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void StartBook_Click(Windows::Foundation::IInspectable const& sender,
                             Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void EntriesList_SelectionChanged(Windows::Foundation::IInspectable const& sender,
                                          Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);

    private:
        void SelectTab(int idx);
        HWND Hwnd();
        void UpdateTitle();
        void MarkDirty();
        bool ConfirmDiscard(const wchar_t* action);
        void OpenNamedFile(const std::wstring& path);
        void ApplyParsedFile(const FbdLoadResult& f, const std::wstring& path, bool dirty, const std::wstring& statusNote);
        // Autosave / recovery in the shared data folder (C:\BalancingAct\data), same autosave.fbd as the Win32 app.
        void RecoverFromAutosave();
        void ScheduleAutosave();
        void AutosaveNow();
        void ShutdownAutosave();
        FbdSaveState GatherSaveState(const std::wstring& finalizedDate);
        bool SaveTo(const std::wstring& path);
        void ApplyLockState();
        void WriteSnapshot(const wchar_t* label);
        winrt::fire_and_forget ShowFinalizeDialog();
        void FinalizeNow(const std::wstring& date);
        void UnfinalizeNow();
        void StartNewSheet();
        void DoSave(bool saveAs);
        void AddEntryFromForm();
        void FillFormFromEntry(const Entry& e, bool everything);
        void ClearForm();
        void EndEditMode();
        void SetStatus(const std::wstring& text, bool isError);
        void RefreshList();
        void RefreshOverview();
        void RefreshBooks();
        void RefreshBreakdown();
        void RefreshBySpecies();
        void LoadSupplierEmails();
        bool SaveSupplierEmails();
        winrt::fire_and_forget ShowManageNames();
        void LoadNameMaster();
        bool SaveNameMaster();
        void EnsureNamesSeeded();
        void ScheduleNameRestore(Microsoft::UI::Xaml::Controls::ComboBox const& combo, bool isSupplier);
        void RecordEntryNames(const std::wstring& supplier, const std::wstring& species);
        winrt::fire_and_forget AddEntryClicked();
        void DoEmailSuppliers();
        winrt::fire_and_forget ShowPrintPreview();
        winrt::fire_and_forget ShowAbout();
        winrt::fire_and_forget ShowPrintDialog();   // WinUI's own Print dialog (printer / copies / paper / orientation)
        winrt::fire_and_forget ShowDrillDown(BreakdownRowRef ref);
        void SetCurrentBook(const std::wstring& name, wchar_t type);
        std::wstring NextBookName(wchar_t type);
        void UpdateSelectionStatus();
        void EditSelected();
        void RefreshNameLists();
        void Recalculate();
        int RealIndexOfSelection();
        void MoveSelectedRowsToBook(const std::wstring& name, wchar_t type);

        std::vector<Entry> m_entries;   // the day's sheet - same Entry type as the Win32 app
        std::wstring m_currentBook;     // book new entries go into (empty = none)
        wchar_t m_currentBookType = 0;  // L'D', L'C' or 0
        NameMasterData m_names;         // names.txt: master lists, aliases, dates
        int m_nameGen = 0;              // bumped whenever the entry form is added/cleared; stale name-restore timers check it
        bool m_resolving = false;       // the "did you mean...?" check before an add is in progress
        std::map<std::wstring, std::wstring> m_supplierEmails; // supplier -> address, from emails.txt beside the exe
        std::vector<BreakdownRowRef> m_bkRefs; // Breakdown rows that can be drilled into, in list order
        std::vector<int> m_bkItemRef;          // per Breakdown list item: index into m_bkRefs, or -1 for a supplier heading
        std::vector<size_t> m_shown;    // list row -> index into m_entries (the filter hides some)
        int m_editIndex = -1;           // >= 0 while editing an existing entry
        bool m_loadedOnce = false;
        std::wstring m_currentFile;     // full path of the open .fbd; empty = unsaved sheet
        std::wstring m_finalizedDate;   // set when a finalized (history) file is open
        bool m_reconValid = false;      // Debtor and Cash parse and the entered total is computable
        bool m_diffOk = false;          // ... and Debtor + Cash balances the entered total
        bool m_allowMixedDates = false; // user said Yes to the Different Date warning for this sheet
        ULONGLONG m_ddClosedTick = 0;   // when a Supplier/Species dropdown last closed (see Combo_EnterKey)
        bool m_dirty = false;           // unsaved changes
        bool m_suppressDirty = false;   // true while a file is being loaded into the boxes
        std::wstring m_baseDebtor, m_baseCash; // Debtor/Cash text as last loaded/saved: WinUI raises TextChanged
                                               // AFTER Text() returns, so m_suppressDirty is already off by then
        bool m_exitConfirmed = false;   // File > Exit already asked about unsaved changes
        bool m_closed = false;          // set when the window starts closing; handlers then do nothing
        bool m_autosaveReady = false;   // startup recovery has run: autosave may write (never before - it would overwrite the real autosave with a blank sheet)
        bool m_autosaveOwner = false;   // this copy owns autosave.fbd (no other copy of either app was running first)
        bool m_autosaveFailWarned = false;
        bool m_shutdownDone = false;
        Entry m_undoEntry;               // the row last deleted (Edit > Undo Delete), one level, like the Win32 app
        int m_undoIndex = -1;
        bool m_hasUndo = false;
        std::vector<std::wstring> m_recent; // File > Recent Files, newest first (max kMaxRecentFiles)
        ULONGLONG m_lastBackupTick = 0; // rolling snapshot in the shared backups\ folder (same 3-minute rhythm as Win32)
        std::string m_lastBackupContent;
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_autosaveTimer{ nullptr };
    };
}

namespace winrt::FishBalanceWinUI::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
