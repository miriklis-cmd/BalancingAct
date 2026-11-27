#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <microsoft.ui.xaml.window.h>   // IWindowNative: gets the HWND for the file dialogs
#include <commdlg.h>
#include <shobjidl.h>   // IFileOpenDialog (Restore from Backup opens in the backups folder)
#pragma comment(lib, "comdlg32.lib")
#include "src/data/AutosaveShared.h"          // shared data folder, who-owns-autosave lock, settings.txt keys (same code as the Win32 app)
#include "src/data/FishBalanceWin32IO.h"    // LoadFbdFileContent, WriteFileAtomicUtf8, WToUtf8 (same code as the Win32 app)
// The print job and the report renderer are the Win32 app's own source files, compiled INTO this file (unity-style)
// so the project needs no extra items or per-file precompiled-header settings. Do not also add them to the project.
// They come before "using namespace winrt" on purpose: nothing in them expects the winrt names.
#include "src/services/ReportRender.cpp"    // RenderReportPages, FreeRenderedPages
#include "src/services/PrintJob.cpp"        // PrintBreakdownJob, GetPreviewPageSpec
#include "src/services/CsvExport.cpp"       // ExportToCsv(path, entries, &err) - the Win32 app's own CSV code
#include "src/services/Email.cpp"           // EmailSupplier / EmailSupplierWith (mailto: or .eml draft), TodayDateString - the Win32 app's own code
#include "version.h"                           // APP_VERSION - the same number as the Win32 app (one shared version)
#include "src/data/UserSettings.h"         // step 14: Tools > Settings values in settings.txt (same keys as the Win32 app)
#include "src/services/GmailDraft.h"        // step 14: Gmail drafts. src/services/GmailDraft.cpp must be an item in the project (precompiled header: Not Using)
#pragma comment(lib, "gdi32.lib")      // CreateDIBSection, TextOutW, StartDocW ... (a WinUI project does not link it by default)
#pragma comment(lib, "winspool.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")      // ShellExecuteW for the mailto: draft

#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <memory>
#include <cwctype>
#include <ctime>
#include <set>
#include <chrono>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace
{
    // Step 14: the current Tools > Settings values. Loaded in Root_Loaded, changed only by the Settings dialog.
    UserSettings g_user;

    SimpleDate TodaySimple()
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        return SimpleDate{ st.wYear, st.wMonth, st.wDay };
    }

    std::wstring Lower(std::wstring s)
    {
        for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
        return s;
    }

    std::wstring IsoFromTm(const std::tm& lt)
    {
        wchar_t buf[16];
        swprintf(buf, 16, L"%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
        return buf;
    }

    std::wstring TodayIso()
    {
        std::time_t now = std::time(nullptr);
        std::tm lt{};
        localtime_s(&lt, &now);
        return IsoFromTm(lt);
    }

    // Date shown to the user: dd/mm/yyyy (Australian), stored as yyyy-mm-dd like the .fbd files.
    std::wstring DisplayDate(const std::wstring& iso)
    {
        int y = 0, m = 0, d = 0;
        if (swscanf_s(iso.c_str(), L"%d-%d-%d", &y, &m, &d) != 3)
            return iso;
        wchar_t buf[16];
        swprintf(buf, 16, L"%02d/%02d/%04d", d, m, y);
        return buf;
    }

    std::wstring ToStd(hstring const& h) { return std::wstring(h.c_str()); }

    std::wstring DateFromPicker(Controls::CalendarDatePicker const& p)
    {
        auto d = p.Date();
        if (!d)
            return TodayIso();
        std::time_t t = winrt::clock::to_time_t(d.Value());
        std::tm lt{};
        localtime_s(&lt, &t);
        return IsoFromTm(lt);
    }

    void SetPickerDate(Controls::CalendarDatePicker const& p, const std::wstring& iso)
    {
        int y = 0, m = 0, d = 0;
        std::tm lt{};
        if (swscanf_s(iso.c_str(), L"%d-%d-%d", &y, &m, &d) == 3)
        {
            lt.tm_year = y - 1900;
            lt.tm_mon = m - 1;
            lt.tm_mday = d;
            lt.tm_hour = 12; // midday: immune to daylight-saving edges
            lt.tm_isdst = -1;
        }
        else
        {
            std::time_t now = std::time(nullptr);
            localtime_s(&lt, &now);
        }
        std::time_t t = std::mktime(&lt);
        p.Date(Windows::Foundation::IReference<Windows::Foundation::DateTime>{ winrt::clock::from_time_t(t) });
    }

    // The text box inside an editable ComboBox (template part "EditableText"). Its Text is what the user actually
    // typed, even when the ComboBox itself has not (or will not) commit that text to its own Text property.
    Controls::TextBox FindEditableTextBox(DependencyObject const& root)
    {
        const int count = Media::VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < count; ++i)
        {
            DependencyObject child = Media::VisualTreeHelper::GetChild(root, i);
            if (auto tb = child.try_as<Controls::TextBox>())
                return tb;
            if (auto found = FindEditableTextBox(child))
                return found;
        }
        return nullptr;
    }

    // What is really in an editable Supplier / Species box right now: the inner text box when it has text (the ComboBox's
    // own Text can still be empty until the box loses focus or Enter is pressed), else the ComboBox's Text.
    // v0.24.11: used when saving the half-typed row, so a kill while the caret is still in the box keeps it.
    std::wstring TypedComboText(Controls::ComboBox const& combo)
    {
        try
        {
            if (auto inner = FindEditableTextBox(combo))
            {
                const std::wstring t = ToStd(inner.Text());
                if (!t.empty())
                    return t;
            }
        }
        catch (winrt::hresult_error const&) {}
        return ToStd(combo.Text());
    }

    // emails.txt lives in the shared data folder C:\BalancingAct\data, the same file the Win32 app uses
    // (Storage.cpp GetSharedDataDir). Created on first use; if that fails it falls back to beside this exe.
    // An emails.txt still sitting beside the exe is copied across once if the shared folder has none.
    std::wstring SharedFilePath(const wchar_t* fileName)
    {
        wchar_t exe[MAX_PATH] = {};
        const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring exeDir;
        if (n > 0 && n < MAX_PATH)
        {
            std::wstring path(exe);
            const size_t slash = path.find_last_of(L"\\/");
            if (slash != std::wstring::npos)
                exeDir = path.substr(0, slash);
        }
        const wchar_t* const kDir = L"C:\\BalancingAct\\data";
        CreateDirectoryW(L"C:\\BalancingAct", nullptr); // already existing is fine
        CreateDirectoryW(kDir, nullptr);
        const DWORD attrs = GetFileAttributesW(kDir);
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
            return exeDir.empty() ? std::wstring() : exeDir + L"\\" + fileName;
        const std::wstring shared = std::wstring(kDir) + L"\\" + fileName;
        if (!exeDir.empty() && GetFileAttributesW(shared.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            const std::wstring old = exeDir + L"\\" + fileName;
            if (GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES)
                CopyFileW(old.c_str(), shared.c_str(), TRUE);
        }
        return shared;
    }

    std::wstring EmailsFilePath() { return SharedFilePath(L"emails.txt"); }

    // Placeholder name lists so the drop-downs have something in them. In the real app these
    // come from the saved names; loading them is a later step.
    const wchar_t* const kSeedSuppliers[] = { L"Supplier 1", L"Supplier 2", L"Supplier 3" };
    const wchar_t* const kSeedSpecies[] = { L"Flathead", L"Snapper", L"Whiting", L"Garfish", L"Calamari" };

    // Entries table columns. Must match the header Grid in MainWindow.xaml.
    constexpr int kColCount = 8;
    struct Col { double width; bool star; };
    const Col kCols[kColCount] = {
        { 2.62, true }, { 1.86, true }, { 1.39, true }, { 1.5, true },
        { 1.38, true }, { 2.28, true }, { 2.37, true }, { 1.6, true } };

    // Font size for the text in list rows. The row cells are built in code, and a ListViewItem does not pass the
    // ListView's own FontSize down to them (only the header row inherits it), so every row sets this explicitly.
    double g_listFontSize = 14.0;

    Controls::Grid MakeRow(const std::wstring (&cells)[kColCount], bool flagged, bool bookStart)
    {
        Controls::Grid g;
        if (flagged) // unusual-price flag: same light red tint + red text as the Win32 list
            g.Background(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 255, 235, 235) });
        else if (bookStart) // first row of a book: light blue tint + bold, like the Win32 list
            g.Background(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 228, 238, 252) });
        for (int i = 0; i < kColCount; ++i)
        {
            Controls::ColumnDefinition cd;
            cd.Width(GridLength{ kCols[i].width, kCols[i].star ? GridUnitType::Star : GridUnitType::Pixel });
            g.ColumnDefinitions().Append(cd);

            Controls::TextBlock tb;
            tb.Text(hstring{ cells[i] });
            tb.FontSize(g_listFontSize);
            tb.VerticalAlignment(VerticalAlignment::Center);
            tb.TextTrimming(TextTrimming::CharacterEllipsis);
            if (flagged)
                tb.Foreground(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 200, 0, 0) });
            if (bookStart)
                tb.FontWeight(Windows::UI::Text::FontWeights::SemiBold());

            // Each cell is a Border so the columns get the same thin divider lines as the header.
            Controls::Border cell;
            cell.Padding(ThicknessHelper::FromLengths(16, 0, 16, 0));
            cell.BorderThickness(ThicknessHelper::FromLengths(0, 0, i < kColCount - 1 ? 1 : 0, 1));
            cell.BorderBrush(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 0xE3, 0xE8, 0xEF) });
            cell.Child(tb);
            Controls::Grid::SetColumn(cell, i);
            g.Children().Append(cell);
        }
        return g;
    }

    // Overview rows: Supplier | Kgs | Total, numbers right-aligned, bold for the GRAND TOTAL row.
    Controls::Grid MakeOverviewRow(const std::wstring (&cells)[3], bool bold)
    {
        const double widths[3] = { 3.0, 1.5, 2.0 };
        Controls::Grid g;
        for (int i = 0; i < 3; ++i)
        {
            Controls::ColumnDefinition cd;
            cd.Width(GridLength{ widths[i], GridUnitType::Star });
            g.ColumnDefinitions().Append(cd);

            Controls::TextBlock tb;
            tb.Text(hstring{ cells[i] });
            tb.FontSize(g_listFontSize);
            tb.VerticalAlignment(VerticalAlignment::Center);
            tb.HorizontalAlignment(i == 0 ? HorizontalAlignment::Left : HorizontalAlignment::Right);
            tb.TextTrimming(TextTrimming::CharacterEllipsis);
            if (bold)
                tb.FontWeight(Windows::UI::Text::FontWeights::SemiBold());

            Controls::Border cell;
            cell.Padding(ThicknessHelper::FromLengths(16, 0, 16, 0));
            cell.BorderThickness(ThicknessHelper::FromLengths(0, 0, i < 2 ? 1 : 0, 1));
            cell.BorderBrush(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 0xE3, 0xE8, 0xEF) });
            cell.Child(tb);
            Controls::Grid::SetColumn(cell, i);
            g.Children().Append(cell);
        }
        return g;
    }

    // IBufferByteAccess, declared here instead of including <robuffer.h>: that header declares a global
    // "Windows" namespace, which makes every "Windows::..." in this file ambiguous with winrt::Windows.
    struct __declspec(uuid("905a0fef-bc53-11df-8c49-001e4fc686da")) IBufferByteAccessLocal : ::IUnknown
    {
        virtual HRESULT __stdcall Buffer(BYTE** value) = 0;
    };

    // One rendered report page (24-bit BGR, top-down, DWORD-aligned rows) -> a BGRA bitmap the Image control can show.
    bool PageToBitmap(const RenderedPage& p, Media::Imaging::WriteableBitmap& out, std::wstring& error)
    {
        if (!p.bits || p.width <= 0 || p.height <= 0)
        {
            error = L"A report page had no image data.";
            return false;
        }
        Media::Imaging::WriteableBitmap wb{ p.width, p.height };
        auto access = wb.PixelBuffer().as<IBufferByteAccessLocal>();
        BYTE* dst = nullptr;
        if (FAILED(access->Buffer(&dst)) || !dst)
        {
            error = L"Could not get at the preview bitmap's pixels.";
            return false;
        }
        const size_t w = static_cast<size_t>(p.width);
        const size_t h = static_cast<size_t>(p.height);
        const size_t srcStride = (w * 3 + 3) & ~static_cast<size_t>(3);
        const BYTE* src = static_cast<const BYTE*>(p.bits);
        for (size_t y = 0; y < h; ++y)
        {
            const BYTE* s = src + y * srcStride;
            BYTE* d = dst + y * w * 4;
            for (size_t x = 0; x < w; ++x)
            {
                d[x * 4 + 0] = s[x * 3 + 0];
                d[x * 4 + 1] = s[x * 3 + 1];
                d[x * 4 + 2] = s[x * 3 + 2];
                d[x * 4 + 3] = 255;
            }
        }
        wb.Invalidate();
        out = wb;
        return true;
    }

    // Breakdown rows: Species | Price | Kgs | Total. kind 0 = supplier heading (one wide label),
    // 1 = price line, 2 = "<species> Total", 3 = "<supplier> Total". Widths must match the header in MainWindow.xaml.
    Controls::Grid MakeBreakdownRow(const std::wstring (&cells)[4], int kind)
    {
        const double widths[4] = { 3.0, 1.5, 1.5, 2.0 };
        const auto line = Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 0xE3, 0xE8, 0xEF) };
        Controls::Grid g;
        if (kind == 0)
            g.Background(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 0xEE, 0xF2, 0xF8) });
        else if (kind == 3)
            g.Background(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 0xF6, 0xF8, 0xFB) });
        for (int i = 0; i < 4; ++i)
        {
            Controls::ColumnDefinition cd;
            cd.Width(GridLength{ widths[i], GridUnitType::Star });
            g.ColumnDefinitions().Append(cd);
        }
        if (kind == 0)
        {
            Controls::TextBlock tb;
            tb.Text(hstring{ cells[0] });
            tb.FontSize(g_listFontSize); // the supplier heading row was the one row type that missed the list size
            tb.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            tb.VerticalAlignment(VerticalAlignment::Center);
            tb.Margin(ThicknessHelper::FromLengths(16, 0, 16, 0));
            Controls::Grid::SetColumnSpan(tb, 4);
            g.Children().Append(tb);
            return g;
        }
        for (int i = 0; i < 4; ++i)
        {
            Controls::TextBlock tb;
            tb.Text(hstring{ cells[i] });
            tb.FontSize(g_listFontSize);
            tb.VerticalAlignment(VerticalAlignment::Center);
            tb.HorizontalAlignment(i == 0 ? HorizontalAlignment::Left : HorizontalAlignment::Right);
            tb.TextTrimming(TextTrimming::CharacterEllipsis);
            if (kind >= 2)
                tb.FontWeight(Windows::UI::Text::FontWeights::SemiBold());

            Controls::Border cell;
            cell.Padding(ThicknessHelper::FromLengths(16, 0, 16, 0));
            cell.BorderThickness(ThicknessHelper::FromLengths(0, 0, i < 3 ? 1 : 0, 1));
            cell.BorderBrush(line);
            cell.Child(tb);
            Controls::Grid::SetColumn(cell, i);
            g.Children().Append(cell);
        }
        return g;
    }

    // By Species rows: Species | Kgs | Total | Avg | Highest | Lowest. Widths must match the header in
    // MainWindow.xaml; the GRAND TOTAL row is bold and only carries Kgs and Total (prices of different species
    // are not comparable, same as the Win32 list).
    Controls::Grid MakeSpeciesRow(const std::wstring (&cells)[6], bool bold)
    {
        const double widths[6] = { 2.5, 1.2, 1.6, 1.4, 1.5, 1.5 };
        Controls::Grid g;
        for (int i = 0; i < 6; ++i)
        {
            Controls::ColumnDefinition cd;
            cd.Width(GridLength{ widths[i], GridUnitType::Star });
            g.ColumnDefinitions().Append(cd);

            Controls::TextBlock tb;
            tb.Text(hstring{ cells[i] });
            tb.FontSize(g_listFontSize);
            tb.VerticalAlignment(VerticalAlignment::Center);
            tb.HorizontalAlignment(i == 0 ? HorizontalAlignment::Left : HorizontalAlignment::Right);
            tb.TextTrimming(TextTrimming::CharacterEllipsis);
            if (bold)
                tb.FontWeight(Windows::UI::Text::FontWeights::SemiBold());

            Controls::Border cell;
            cell.Padding(ThicknessHelper::FromLengths(16, 0, 16, 0));
            cell.BorderThickness(ThicknessHelper::FromLengths(0, 0, i < 5 ? 1 : 0, 1));
            cell.BorderBrush(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 0xE3, 0xE8, 0xEF) });
            cell.Child(tb);
            Controls::Grid::SetColumn(cell, i);
            g.Children().Append(cell);
        }
        return g;
    }

    // Books table rows: Book | Rows | Kgs | Entered | From the book | Difference. Widths must match the
    // header in MainWindow.xaml. A Difference other than $0.00 is red; totals are bold.
    Controls::Grid MakeBookRow(const std::wstring (&cells)[6], bool bold, bool bad)
    {
        const double widths[6] = { 2.3, 0.8, 1.1, 1.5, 1.8, 1.5 };
        Controls::Grid g;
        for (int i = 0; i < 6; ++i)
        {
            Controls::ColumnDefinition cd;
            cd.Width(GridLength{ widths[i], GridUnitType::Star });
            g.ColumnDefinitions().Append(cd);

            Controls::TextBlock tb;
            tb.Text(hstring{ cells[i] });
            tb.FontSize(g_listFontSize);
            tb.VerticalAlignment(VerticalAlignment::Center);
            tb.HorizontalAlignment(i == 0 ? HorizontalAlignment::Left : HorizontalAlignment::Right);
            tb.TextTrimming(TextTrimming::CharacterEllipsis);
            if (bold)
                tb.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            if (bad && i == 5)
                tb.Foreground(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 200, 0, 0) });

            Controls::Border cell;
            cell.Padding(ThicknessHelper::FromLengths(10, 0, 10, 0));
            cell.BorderThickness(ThicknessHelper::FromLengths(0, 0, i < 5 ? 1 : 0, 1));
            cell.BorderBrush(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 0xE3, 0xE8, 0xEF) });
            cell.Child(tb);
            Controls::Grid::SetColumn(cell, i);
            g.Children().Append(cell);
        }
        return g;
    }

    std::wstring BookBoxText(const std::wstring& name, wchar_t type)
    {
        if (name.empty())
            return L"(no book)";
        return name + (type == L'C' ? L" (Cash)" : (type == L'D' ? L" (Debtor)" : L""));
    }

    void FillCombo(Controls::ComboBox const& box, const std::vector<std::wstring>& names)
    {
        box.Items().Clear();
        for (const std::wstring& n : names)
            box.Items().Append(box_value(hstring{ n }));
    }

    // v0.24.12: an editable ComboBox that is not focused shows its SELECTED ITEM (or the placeholder), not its Text, so
    // text set from code stays invisible until the user clicks in (Jack's recovery test: Supplier / Species looked blank,
    // then showed the right text on click). To show a recovered draft, select the matching list item, adding the name to
    // the list first when it is a new one. Only when the box is still empty (never over text being typed).
    void ShowDraftInCombo(Controls::ComboBox const& combo, const std::wstring& want)
    {
        if (want.empty() || !combo)
            return;
        try
        {
            if (combo.SelectedIndex() >= 0)
                return;
            auto lower = [](std::wstring v) {
                for (wchar_t& ch : v)
                    ch = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
                return v;
            };
            const std::wstring wantLower = lower(want);
            auto items = combo.Items();
            for (uint32_t i = 0; i < items.Size(); ++i)
            {
                if (lower(ToStd(unbox_value_or<hstring>(items.GetAt(i), L""))) == wantLower)
                {
                    combo.SelectedIndex(static_cast<int32_t>(i));
                    return;
                }
            }
            items.Append(box_value(hstring{ want }));
            combo.SelectedIndex(static_cast<int32_t>(items.Size()) - 1);
        }
        catch (winrt::hresult_error const&) {}
    }
}

namespace winrt::FishBalanceWinUI::implementation
{
    void MainWindow::Root_Loaded(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_loadedOnce)
            return;
        m_loadedOnce = true;
        // ---- Step 14: Tools > Settings (same keys as the Win32 app's Tools > Settings window) ------------------------
        g_user = LoadUserSettings(autosave_shared::DataDir());
        g_outlierFence = OutlierFenceForSensitivity(g_user.outlierSensitivity);

        // List text size: the lists inherit FontSize, so this takes effect at once (the Win32 app needs a restart).
        // Column widths are fixed in the XAML, so at Large / Extra large a long name may be cut short.
        auto applyTextSize = [this]() {
            const double pct = static_cast<double>(ClampTextPercent(g_user.textPercent)) / 100.0;
            const double fs = 14.0 * pct;
            g_listFontSize = fs;                 // list rows (built in code) - they pick it up when the lists are rebuilt
            EntriesList().FontSize(fs);          // list headers inherit it straight away
            OvList().FontSize(fs);
            BooksList().FontSize(fs);
            BreakdownList().FontSize(fs);
            TabPage3().FontSize(fs);
        };
        applyTextSize();
        VersionText().Text(L"Fish Balance Manager (WinUI) v" APP_VERSION);

        auto openSettings = [this, applyTextSize]() -> winrt::fire_and_forget
        {
            // Copies of everything used after the first co_await live in this coroutine's own frame.
            MainWindow* const self = this;
            const auto applyText = applyTextSize;
            const UserSettings before = g_user;

            auto label = [](const wchar_t* text, double topMargin = 12.0) {
                Controls::TextBlock t;
                t.Text(text);
                t.TextWrapping(TextWrapping::Wrap);
                t.Margin(Thickness{ 0, topMargin, 0, 4 });
                return t;
            };
            auto note = [](const wchar_t* text) {
                Controls::TextBlock t;
                t.Text(text);
                t.TextWrapping(TextWrapping::Wrap);
                t.Opacity(0.7);
                t.FontSize(12);
                t.Margin(Thickness{ 0, 6, 0, 0 });
                return t;
            };
            auto radios = [](std::initializer_list<const wchar_t*> items, int selected) {
                Controls::RadioButtons rb;
                for (const wchar_t* i : items)
                    rb.Items().Append(box_value(hstring{ i }));
                rb.SelectedIndex(selected);
                return rb;
            };
            auto textBox = [](const std::wstring& text, int maxLen) {
                Controls::TextBox tb;
                tb.Text(hstring{ text });
                tb.MaxLength(maxLen);
                return tb;
            };
            auto page = []() {
                Controls::StackPanel p;
                p.Spacing(2);
                p.Padding(Thickness{ 4, 4, 12, 4 });
                return p;
            };
            // Tall enough to show a whole tab on a normal monitor, shorter on a laptop (then it scrolls).
            const double svHeight = (std::max)(280.0, (std::min)(620.0, RootGrid().ActualHeight() - 300.0));
            auto scroller = [svHeight](Controls::StackPanel const& content) {
                Controls::ScrollViewer sv;
                sv.Content(content);
                sv.VerticalScrollBarVisibility(Controls::ScrollBarVisibility::Auto);
                sv.HorizontalScrollBarVisibility(Controls::ScrollBarVisibility::Disabled);
                sv.Height(svHeight);
                return sv;
            };
            auto numberBox = [](double value, double lo, double hi) {
                Controls::NumberBox nb;
                nb.Minimum(lo);
                nb.Maximum(hi);
                nb.Value(value);
                nb.SmallChange(1);
                nb.SpinButtonPlacementMode(Controls::NumberBoxSpinButtonPlacementMode::Inline);
                nb.Width(150);
                nb.HorizontalAlignment(HorizontalAlignment::Left);
                return nb;
            };

            // ---- Email ----
            const int modeIdx = before.emailMode == EmailMode::Eml ? 1 : (before.emailMode == EmailMode::Gmail ? 2 : 0);
            auto rbMode = radios({ L"Open in my mail program (mailto) - plain text",
                                   L"Outlook draft with a price table (.eml) - needs classic Outlook",
                                   L"Gmail draft - saved in the Gmail Drafts folder, nothing is sent" }, modeIdx);
            auto tbAccount = textBox(before.gmailAccount, 150);
            tbAccount.Header(box_value(L"Gmail account (optional)"));
            Controls::Button forget;
            forget.Content(box_value(L"Forget this PC's Gmail sign-in"));
            forget.Margin(Thickness{ 0, 8, 0, 0 });
            forget.Click([self](Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                if (self->m_closed)
                    return;
                if (!GmailHasSavedSignIn())
                {
                    MessageBoxW(self->Hwnd(), L"This PC has no saved Gmail sign-in.", L"Gmail", MB_OK | MB_ICONINFORMATION);
                    return;
                }
                if (MessageBoxW(self->Hwnd(),
                        L"Forget this PC's saved Gmail sign-in? You will be asked to sign in to Google again the next time you "
                        L"create Gmail drafts. (Your Google account is not changed.)",
                        L"Gmail", MB_YESNO | MB_ICONQUESTION) == IDYES)
                    GmailForgetSignIn();
            });
            const EmailWording shown = ResolveEmailWording(before.emailSubject, before.emailIntro, before.emailSignoff, before.emailSignature);
            auto tbSubject = textBox(shown.subject, 150);
            tbSubject.Header(box_value(L"Subject  ({supplier} and {date} are filled in for each email)"));
            auto tbIntro = textBox(shown.intro, 150);
            tbIntro.Header(box_value(L"Opening line  (the date and a colon follow it)"));
            auto tbSignoff = textBox(shown.signoff, 150);
            tbSignoff.Header(box_value(L"Sign-off"));
            auto tbSignature = textBox(L"", 600); // v0.24.0: optional, several lines
            tbSignature.Header(box_value(L"Signature  (name, company, phone - up to 8 lines; empty = none)"));
            tbSignature.AcceptsReturn(true);   // BEFORE the text is set: a single-line TextBox cuts the text at the first line break
            tbSignature.TextWrapping(TextWrapping::Wrap);
            tbSignature.MinHeight(96);
            tbSignature.Text(hstring{ NewlinesToCrlf(shown.signature) });

            auto emailPage = page();
            emailPage.Children().Append(label(L"How should \"Email All Suppliers\" open each email?", 4));
            emailPage.Children().Append(rbMode);
            emailPage.Children().Append(tbAccount);
            emailPage.Children().Append(forget);
            emailPage.Children().Append(label(L"Wording (used by all three methods). Leave a box blank to use the standard wording."));
            emailPage.Children().Append(tbSubject);
            Controls::StackPanel gap1; gap1.Height(6); emailPage.Children().Append(gap1);
            emailPage.Children().Append(tbIntro);
            Controls::StackPanel gap2; gap2.Height(6); emailPage.Children().Append(gap2);
            emailPage.Children().Append(tbSignoff);
            Controls::StackPanel gap3; gap3.Height(6); emailPage.Children().Append(gap3);
            emailPage.Children().Append(tbSignature);
            emailPage.Children().Append(note(L"Emails carry the sheet date (the date on the entries), not the day you send them."));

            // ---- Backups & price flags ----
            auto nbMinutes = numberBox(ClampBackupMinutes(before.backupMinutes), 1, 60);
            auto nbKeep = numberBox(ClampBackupKeep(before.backupKeep), 10, 500);
            const int sensIdx = ClampOutlierSensitivity(before.outlierSensitivity);
            auto rbSens = radios({ L"Relaxed - only flag prices that are far from the others",
                                   L"Normal (recommended)",
                                   L"Strict - flag smaller differences too" }, sensIdx);
            auto backupPage = page();
            backupPage.Children().Append(label(L"Backups", 4));
            // Labels are separate text blocks: a NumberBox header wraps at the width of the box (150), which squashed them.
            backupPage.Children().Append(label(L"Take a backup snapshot at most every (minutes, only if something changed)", 6));
            backupPage.Children().Append(nbMinutes);
            backupPage.Children().Append(label(L"Keep the most recent (snapshots; older ones are deleted)", 14));
            backupPage.Children().Append(nbKeep);
            backupPage.Children().Append(label(L"Price flagging (possible typo in a price)", 20));
            backupPage.Children().Append(rbSens);
            backupPage.Children().Append(note(L"A price is only checked once there are at least 4 other prices for that species on the sheet. "
                                              L"Changing this re-checks every entry on the current sheet straight away."));

            // ---- Display & printing ----
            const int tp = ClampTextPercent(before.textPercent);
            auto rbText = radios({ L"Normal", L"Large (125%)", L"Extra large (150%)" }, tp == 125 ? 1 : (tp == 150 ? 2 : 0));
            const int ds = ClampDateStyle(before.dateStyle);
            auto rbDate = radios({ L"18 November 2026", L"18 Nov 2026", L"18/11/2026" }, ds);
            Controls::ComboBox cbPrinter;
            cbPrinter.Header(box_value(L"Printer"));
            cbPrinter.Width(360);
            cbPrinter.Items().Append(box_value(L"Windows default printer"));
            const std::vector<std::wstring> printers = ListInstalledPrinters();
            int printerSel = 0;
            for (size_t i = 0; i < printers.size(); ++i)
            {
                cbPrinter.Items().Append(box_value(hstring{ printers[i] }));
                if (printers[i] == before.printer)
                    printerSel = static_cast<int>(i) + 1;
            }
            cbPrinter.SelectedIndex(printerSel);
            Controls::ComboBox cbPaper;
            cbPaper.Header(box_value(L"Paper"));
            cbPaper.Width(200);
            for (const wchar_t* t : { L"Printer default", L"A4", L"A3" })
                cbPaper.Items().Append(box_value(hstring{ t }));
            cbPaper.SelectedIndex(ClampPrintPaper(before.printPaper));
            Controls::ComboBox cbOrient;
            cbOrient.Header(box_value(L"Orientation"));
            cbOrient.Width(200);
            for (const wchar_t* t : { L"Printer default", L"Portrait", L"Landscape" })
                cbOrient.Items().Append(box_value(hstring{ t }));
            cbOrient.SelectedIndex(ClampPrintOrientation(before.printOrientation));
            auto displayPage = page();
            displayPage.Children().Append(label(L"Text size in the lists", 4));
            displayPage.Children().Append(rbText);
            displayPage.Children().Append(note(L"Only the tables change size, as in the Win32 app. The tabs, buttons, entry form and status bar stay as they are."));
            displayPage.Children().Append(label(L"Date format in emails"));
            displayPage.Children().Append(rbDate);
            displayPage.Children().Append(label(L"Printing - where the Print dialog and Print Preview start", 20));
            displayPage.Children().Append(cbPrinter);
            Controls::StackPanel gap4; gap4.Height(8); displayPage.Children().Append(gap4);
            displayPage.Children().Append(cbPaper);
            Controls::StackPanel gap5; gap5.Height(8); displayPage.Children().Append(gap5);
            displayPage.Children().Append(cbOrient);
            displayPage.Children().Append(note(L"You can still change anything in the Print dialog. If the saved printer is no longer installed, the "
                                               L"Windows default printer is used. To make a PDF, choose \"Microsoft Print to PDF\" as the printer."));

            // ---- Data folder & about ----
            auto tbPath = textBox(autosave_shared::DataDir(), 500);
            tbPath.IsReadOnly(true);
            Controls::Button openFolder;
            openFolder.Content(box_value(L"Open folder"));
            openFolder.Margin(Thickness{ 0, 8, 0, 0 });
            openFolder.Click([self](Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                ShellExecuteW(nullptr, L"open", autosave_shared::DataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            });
            auto dataPage = page();
            dataPage.Children().Append(label(L"Your data folder (entries, backups, names, settings)", 4));
            dataPage.Children().Append(tbPath);
            dataPage.Children().Append(openFolder);
            dataPage.Children().Append(label(L"Fish Balance Manager (WinUI) v" APP_VERSION, 24));
            dataPage.Children().Append(note(L"These settings are saved in settings.txt in the data folder and are shared with the Win32 app."));

            Controls::Pivot pivot;
            const std::pair<const wchar_t*, Controls::StackPanel> tabs[4] = {
                { L"Email", emailPage }, { L"Backups & flags", backupPage }, { L"Display & printing", displayPage }, { L"Data & about", dataPage } };
            for (const auto& t : tabs)
            {
                Controls::PivotItem pi;
                pi.Header(box_value(hstring{ t.first }));
                pi.Content(scroller(t.second));
                pivot.Items().Append(pi);
            }
            pivot.MinWidth(680);
            pivot.Resources().Insert(box_value(L"PivotHeaderItemFontSize"), box_value(16.0)); // default 24 pushed the last tabs off the edge

            Controls::ContentDialog dlg;
            dlg.XamlRoot(RootGrid().XamlRoot());
            dlg.Title(box_value(L"Settings"));
            dlg.Content(pivot);
            dlg.PrimaryButtonText(L"OK");
            dlg.CloseButtonText(L"Cancel");
            dlg.DefaultButton(Controls::ContentDialogButton::Primary);
            dlg.Resources().Insert(box_value(L"ContentDialogMaxWidth"), box_value(780.0)); // default 548 cut the text boxes off at the right
            // The default height cap (756) was clipping the bottom of the tab (the last note line was cut off).
            dlg.Resources().Insert(box_value(L"ContentDialogMaxHeight"), box_value((std::max)(500.0, RootGrid().ActualHeight() - 20.0)));
            const Controls::ContentDialogResult result = co_await dlg.ShowAsync();
            if (self->m_closed || result != Controls::ContentDialogResult::Primary)
                co_return;

            // ---- OK: read everything back, clamp, save, apply ----
            UserSettings now;
            const int mi = rbMode.SelectedIndex();
            now.emailMode = mi == 1 ? EmailMode::Eml : (mi == 2 ? EmailMode::Gmail : EmailMode::Mailto);
            now.gmailAccount = TrimW(ToStd(tbAccount.Text()));
            // A box left at the standard wording (or blank) is stored as blank, like the Win32 window does.
            const EmailWording def;
            auto keepIfCustom = [](const std::wstring& raw, const std::wstring& fallback) {
                const std::wstring cleaned = CleanWordingLine(raw, fallback);
                return cleaned == fallback ? std::wstring() : cleaned;
            };
            now.emailSubject = keepIfCustom(ToStd(tbSubject.Text()), def.subject);
            now.emailIntro = keepIfCustom(ToStd(tbIntro.Text()), def.intro);
            now.emailSignoff = keepIfCustom(ToStd(tbSignoff.Text()), def.signoff);
            // v0.24.0: several lines, stored on one line ("\n" for a line break); empty = no signature.
            now.emailSignature = EncodeMultiline(CleanMultiline(ToStd(tbSignature.Text()), 8, 150));
            auto intOf = [](Controls::NumberBox const& nb, int fallback) {
                const double v = nb.Value();
                return std::isnan(v) ? fallback : static_cast<int>(v + 0.5);
            };
            now.backupMinutes = ClampBackupMinutes(intOf(nbMinutes, before.backupMinutes));
            now.backupKeep = ClampBackupKeep(intOf(nbKeep, before.backupKeep));
            now.outlierSensitivity = ClampOutlierSensitivity(rbSens.SelectedIndex());
            const int ti = rbText.SelectedIndex();
            now.textPercent = ti == 1 ? 125 : (ti == 2 ? 150 : 100);
            now.dateStyle = ClampDateStyle(rbDate.SelectedIndex());
            const int ps = cbPrinter.SelectedIndex();
            now.printer = (ps >= 1 && static_cast<size_t>(ps - 1) < printers.size()) ? printers[static_cast<size_t>(ps) - 1] : std::wstring();
            now.printPaper = ClampPrintPaper(cbPaper.SelectedIndex());
            now.printOrientation = ClampPrintOrientation(cbOrient.SelectedIndex());

            g_user = now;
            SaveUserSettings(autosave_shared::DataDir(), g_user);
            applyText();
            if (now.textPercent != before.textPercent)
            {
                // Rebuild the lists so the rows pick up the new size.
                self->RefreshList();
                self->RefreshOverview();
                self->RefreshBooks();
                self->RefreshBreakdown();
                self->RefreshBySpecies();
            }

            if (now.outlierSensitivity != before.outlierSensitivity)
            {
                g_outlierFence = OutlierFenceForSensitivity(now.outlierSensitivity);
                // Re-judge every entry on the sheet against the new fence, so the flags match straight away.
                std::vector<bool> flagsBefore;
                for (const Entry& e : self->m_entries)
                    flagsBefore.push_back(e.priceFlagged);
                std::vector<std::wstring> done;
                for (size_t i = 0; i < self->m_entries.size(); ++i)
                {
                    const std::wstring key = GroupKey(self->m_entries[i].product);
                    if (std::find(done.begin(), done.end(), key) != done.end())
                        continue;
                    done.push_back(key);
                    ReevaluateOutlierFlagsForSpecies(self->m_entries, self->m_entries[i].product);
                }
                bool changed = false;
                for (size_t i = 0; i < self->m_entries.size() && i < flagsBefore.size(); ++i)
                    if (self->m_entries[i].priceFlagged != flagsBefore[i])
                        changed = true;
                if (changed)
                {
                    self->MarkDirty();
                    self->RefreshList();
                    self->Recalculate();
                }
            }
            self->SetStatus(L"Settings saved.", false);
        };

        {
            Controls::MenuFlyoutItem settingsItem;
            settingsItem.Text(L"Settings...");
            settingsItem.Click([this, openSettings](Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                if (!m_closed)
                    openSettings();
            });
            ToolsMenu().Items().Append(settingsItem);
        }
        // ---- end of step 14 settings ----------------------------------------------------------------------------
        LoadNameMaster(); // v0.15.0: names.txt now also holds the supplier emails (old emails.txt migrates once)
        LoadSupplierEmails();

        // While the window is closing, XAML tears the controls down and can still fire events
        // (text cleared, selection changed). Touching a closed control throws, so stop reacting.
        // Ask before throwing away unsaved changes when the window is closed.
        this->AppWindow().Closing([this](Microsoft::UI::Windowing::AppWindow const&,
                                         Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args) {
            if (m_dirty && !m_closed && !m_exitConfirmed &&
                MessageBoxW(Hwnd(), L"You have unsaved changes. Exit without saving?",
                            L"Fish Balance Manager", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            {
                args.Cancel(true); // staying open: handlers keep working
                return;
            }
            SaveWindowPlacement(); // while the window still exists
            ShutdownAutosave(); // final autosave + settings.txt LASTFILE/DIRTY while the controls still exist
            // The close is going ahead. Stop reacting NOW: XAML tears the controls down after this point and
            // fires events while doing it (text cleared, selection changed), and touching a torn-down control
            // throws "object already closed" (0x80000013). The Closed event below is too late to set this.
            m_closed = true;
            // Belt and braces: the debounce timer cannot fire into a window that is being torn down.
            try { if (m_autosaveTimer) m_autosaveTimer.Stop(); }
            catch (winrt::hresult_error const&) {}
        });

        this->Closed([this](Windows::Foundation::IInspectable const&, WindowEventArgs const&) {
            m_closed = true;
        });

        // Start maximised, with the title bar tinted to match the mock-up.
        auto appWindow = this->AppWindow();
        RestoreWindowPlacement(); // where it was last closed (maximised if it was, or if that spot is no longer on a screen); first run: maximised
        LoadRecentFiles();
        RebuildRecentMenu();
        // App icon: app.ico sits next to the exe (add it to the project with Content = Yes).
        {
            wchar_t exePath[MAX_PATH] = {};
            if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
            {
                std::wstring dir(exePath);
                const size_t slash = dir.find_last_of(L"\\/");
                if (slash != std::wstring::npos)
                {
                    const std::wstring icon = dir.substr(0, slash + 1) + L"app.ico";
                    if (GetFileAttributesW(icon.c_str()) != INVALID_FILE_ATTRIBUTES)
                    {
                        appWindow.SetIcon(hstring{ icon });
                        // The taskbar button used to show a blank square: also give the window its big and small
                        // icons directly (WM_SETICON), loaded at the sizes Windows asks for.
                        const HWND hwnd = Hwnd();
                        if (hwnd)
                        {
                            HANDLE big = LoadImageW(nullptr, icon.c_str(), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE);
                            HANDLE smallIcon = LoadImageW(nullptr, icon.c_str(), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
                            if (big) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
                            if (smallIcon) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
                        }
                    }
                }
            }
        }
        if (Microsoft::UI::Windowing::AppWindowTitleBar::IsCustomizationSupported())
        {
            using ColorRef = Windows::Foundation::IReference<Windows::UI::Color>;
            const Windows::UI::Color bar = Microsoft::UI::ColorHelper::FromArgb(255, 0xEC, 0xF1, 0xF8);
            auto titleBar = appWindow.TitleBar();
            titleBar.BackgroundColor(ColorRef{ bar });
            titleBar.InactiveBackgroundColor(ColorRef{ bar });
            titleBar.ButtonBackgroundColor(ColorRef{ bar });
            titleBar.ButtonInactiveBackgroundColor(ColorRef{ bar });
        }
        SetPickerDate(DateBox(), TodayIso());

        // The editable Supplier/Species combo boxes mark Enter as handled themselves, so a normal
        // KeyDown handler never sees it. Listen with handledEventsToo = true instead.
        {
            auto weak = get_weak();
            Input::KeyEventHandler onKey{ [weak](Windows::Foundation::IInspectable const& s, Input::KeyRoutedEventArgs const& e) {
                if (auto self = weak.get())
                    self->Combo_EnterKey(s, e);
            } };
            SupplierBox().AddHandler(UIElement::KeyDownEvent(), box_value(onKey), true);
            SpeciesBox().AddHandler(UIElement::KeyDownEvent(), box_value(onKey), true);

            // An editable ComboBox can throw away typed text that is not an item in its drop-down (e.g. an alias like
            // "S/mlt") when focus leaves it. Keep the text, and silently swap a remembered alias / different capitals
            // for the real name (same rule as Add Entry and the Win32 box). (A TextSubmitted handler was tried in 10b-10g
            // and removed in 10h: marking it handled stopped the combo committing the text at all.)
            for (int pass = 0; pass < 2; ++pass)
            {
                const bool isSupplier = (pass == 0);
                Controls::ComboBox box = isSupplier ? SupplierBox() : SpeciesBox();
                // The debug log showed the typed text is ALREADY gone by the time the normal (bubbling) KeyDown or
                // LosingFocus events run for Tab - the combo clears it while handling the key. PreviewKeyDown
                // tunnels down first, so the text is still in the inner text box there.
                // Alias auto-complete (as the Win32 boxes): the combo already completes from the drop-down names; when
                // nothing in the list starts with what has been typed, an ALIAS might ("y/fin" -> its alias text,
                // with the rest selected so the next key overwrites it). Tab / Add then turn an exact alias into the
                // real name. Attached once the combo's template (and its inner text box) exists.
                box.Loaded([weak, isSupplier](Windows::Foundation::IInspectable const& s, RoutedEventArgs const&) {
                    auto self = weak.get();
                    if (!self || self->m_closed)
                        return;
                    try
                    {
                        auto combo = s.try_as<Controls::ComboBox>();
                        if (!combo)
                            return;
                        auto inner = FindEditableTextBox(combo);
                        if (!inner)
                            return;
                        auto prevLen = std::make_shared<size_t>(0);
                        auto busy = std::make_shared<bool>(false);
                        inner.TextChanged([weak, isSupplier, combo, prevLen, busy](Windows::Foundation::IInspectable const& ts,
                                                                                 Controls::TextChangedEventArgs const&) {
                            auto me = weak.get();
                            auto tb = ts.try_as<Controls::TextBox>();
                            if (!me || me->m_closed || !tb)
                                return;
                            me->ScheduleAutosave(); // v0.24.10: the typed supplier / species is part of the saved draft
                            try
                            {
                                const std::wstring text = ToStd(tb.Text());
                                const bool grew = text.size() > *prevLen;
                                *prevLen = text.size();
                                if (*busy || !grew || text.empty())
                                    return;
                                if (tb.SelectionLength() != 0 || static_cast<size_t>(tb.SelectionStart()) != text.size())
                                    return; // only when the caret is at the end of what was typed
                                // Something in the drop-down already starts with it? The combo's own completion handles that.
                                auto lower = [](std::wstring v) {
                                    for (wchar_t& ch : v)
                                        ch = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
                                    return v;
                                };
                                const std::wstring lowered = lower(text);
                                auto items = combo.Items();
                                for (uint32_t i = 0; i < items.Size(); ++i)
                                {
                                    const std::wstring name = ToStd(unbox_value_or<hstring>(items.GetAt(i), L""));
                                    if (name.size() >= text.size() && lower(name).compare(0, lowered.size(), lowered) == 0)
                                        return;
                                }
                                me->EnsureNamesSeeded();
                                const NameList& list = isSupplier ? me->m_names.suppliers : me->m_names.species;
                                const std::wstring alias = FindAliasStartingWith(list, text);
                                // v0.17.1 (as Win32): no name starts with the exact text, but one may start with the same
                                // LETTERS, ignoring spaces / capitals / punctuation ("jtm" -> "J T Moyce"). v0.17.2: an alias
                                // that starts with exactly what was typed ("s/m" -> "S/Mlt") wins over this.
                                if (alias.size() <= text.size())
                                {
                                    std::vector<std::wstring> names;
                                    for (uint32_t i = 0; i < items.Size(); ++i)
                                        names.push_back(ToStd(unbox_value_or<hstring>(items.GetAt(i), L"")));
                                    size_t consumed = 0;
                                    const std::wstring byKey = FindNameByKeyPrefix(names, text, consumed);
                                    if (!byKey.empty())
                                    {
                                        *busy = true;
                                        tb.Text(hstring{ byKey });
                                        tb.Select(static_cast<int32_t>(consumed), static_cast<int32_t>(byKey.size() - consumed));
                                        *prevLen = consumed; // Backspace on the highlighted tail is not mistaken for typing
                                        *busy = false;
                                        return;
                                    }
                                }
                                if (alias.size() <= text.size())
                                    return;
                                *busy = true;
                                tb.Text(hstring{ alias });
                                tb.Select(static_cast<int32_t>(text.size()), static_cast<int32_t>(alias.size() - text.size()));
                                *busy = false;
                            }
                            catch (winrt::hresult_error const&)
                            {
                                *busy = false;
                            }
                        });
                    }
                    catch (winrt::hresult_error const&) {}
                });

                box.PreviewKeyDown([weak, isSupplier](Windows::Foundation::IInspectable const& s, Input::KeyRoutedEventArgs const& e) {
                    if (e.Key() != Windows::System::VirtualKey::Tab)
                        return;
                    auto self = weak.get();
                    if (!self || self->m_closed)
                        return;
                    try
                    {
                        if (auto combo = s.try_as<Controls::ComboBox>())
                            self->ScheduleNameRestore(combo, isSupplier);
                    }
                    catch (winrt::hresult_error const&) {}
                });

                box.LosingFocus([weak, isSupplier](UIElement const& sender, Input::LosingFocusEventArgs const&) {
                    auto self = weak.get();
                    if (!self || self->m_closed)
                        return;
                    try
                    {
                        if (auto combo = sender.try_as<Controls::ComboBox>())
                            self->ScheduleNameRestore(combo, isSupplier);
                    }
                    catch (winrt::hresult_error const&) {}
                });
            }
        }
        SelectTab(0);
        UpdateTitle();
        RefreshNameLists();
        RefreshList();
        Recalculate();
        RecoverFromAutosave(); // last: shared autosave.fbd (same as Win32), unless another copy owns it
    }

    void MainWindow::Tab_Tapped(Windows::Foundation::IInspectable const& sender, Input::TappedRoutedEventArgs const&)
    {
        if (m_closed)
            return;
        auto fe = sender.try_as<FrameworkElement>();
        if (!fe)
            return;
        const int idx = _wtoi(unbox_value_or<hstring>(fe.Tag(), L"0").c_str());
        SelectTab(idx);
    }

    void MainWindow::SelectTab(int idx)
    {
        const Controls::Border sel[4] = { Sel0(), Sel1(), Sel2(), Sel3() };
        const Controls::TextBlock txt[4] = { TabText0(), TabText1(), TabText2(), TabText3() };
        const Shapes::Rectangle under[4] = { Under0(), Under1(), Under2(), Under3() };
        const UIElement pages[4] = { DataEntryPage(), TabPage1(), TabPage2(), TabPage3() };
        for (int i = 0; i < 4; ++i)
        {
            const bool on = (i == idx);
            sel[i].Visibility(on ? Visibility::Visible : Visibility::Collapsed);
            under[i].Visibility(on ? Visibility::Visible : Visibility::Collapsed);
            txt[i].Opacity(on ? 1.0 : 0.65);
            pages[i].Visibility(on ? Visibility::Visible : Visibility::Collapsed);
        }
    }

    HWND MainWindow::Hwnd()
    {
        HWND h = nullptr;
        if (auto native = this->try_as<::IWindowNative>())
            native->get_WindowHandle(&h);
        return h;
    }

    // Window title + status-bar file name, same wording as the Win32 app.
    void MainWindow::UpdateTitle()
    {
        if (m_closed || !FileText())
            return;
        std::wstring name = L"(unsaved)";
        if (!m_currentFile.empty())
        {
            const size_t pos = m_currentFile.find_last_of(L"\\/");
            name = (pos == std::wstring::npos) ? m_currentFile : m_currentFile.substr(pos + 1);
        }
        this->Title(hstring{ L"Fish Balance Manager - " + name });

        std::wstring text = m_finalizedDate.empty() ? (L"File: " + name)
                                                    : (L"FINALIZED (" + m_finalizedDate + L") - File: " + name);
        if (m_dirty)
            text += L"  (unsaved changes)";
        FileText().Text(hstring{ text });
        ScheduleAutosave(); // every state change (open / save / new / dirty) refreshes autosave.fbd shortly after
    }

    void MainWindow::MarkDirty()
    {
        if (m_suppressDirty || m_closed)
            return;
        ScheduleAutosave();
        if (!m_dirty)
        {
            m_dirty = true;
            UpdateTitle();
        }
    }

    bool MainWindow::ConfirmDiscard(const wchar_t* action)
    {
        if (!m_dirty)
            return true;
        const std::wstring msg = L"You have unsaved changes. Discard them and " + std::wstring(action) + L"?";
        return MessageBoxW(Hwnd(), msg.c_str(), L"Fish Balance Manager",
                           MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
    }

    // File > New: asks before throwing away unsaved work (same rule as Open / Exit), then a blank unsaved sheet.
    void MainWindow::New_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed)
            return;
        if (!ConfirmDiscard(L"start a new sheet"))
            return;
        StartNewSheet();
    }

    void MainWindow::Open_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed)
            return;
        if (!ConfirmDiscard(L"open another file"))
            return;
        wchar_t file[MAX_PATH] = L"";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = Hwnd();
        ofn.lpstrFilter = L"Fish Balance Files (*.fbd)\0*.fbd\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"fbd";
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
        if (GetOpenFileNameW(&ofn))
            OpenNamedFile(file);
    }

    void MainWindow::Save_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed)
            return;
        DoSave(false);
    }

    void MainWindow::SaveAs_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed)
            return;
        DoSave(true);
    }

    // Loads a .fbd with the same reader/parser the Win32 app uses. Nothing in the window
    // changes unless the whole file is accepted.
    void MainWindow::OpenNamedFile(const std::wstring& path)
    {
        LoadFbdFileResult res = LoadFbdFileContent(path);
        if (!res.ok)
        {
            const std::wstring msg = L"Could not open the selected file: " + res.errorMessage;
            MessageBoxW(Hwnd(), msg.c_str(), L"Fish Balance Manager", MB_OK | MB_ICONERROR);
            return;
        }
        ApplyParsedFile(res.parsed, path, false, L"");
        RememberRecentFile(path);
    }

    // Puts a parsed .fbd into the window. `path` becomes the open file (empty = unsaved sheet); `dirty` says whether
    // the sheet differs from what is on disk (true for work recovered from autosave).
    void MainWindow::ApplyParsedFile(const FbdLoadResult& f, const std::wstring& path, bool dirty, const std::wstring& statusNote)
    {
        ClearUndoDelete();
        m_emailedThisSheet = false;
        m_suppressDirty = true;
        EndEditMode();
        m_entries = f.entries;
        SetCurrentBook(f.draftBook, f.draftBookType); // the book new entries go into
        DebtorBox().Text(hstring{ f.debtor });
        CashBox().Text(hstring{ f.cash });
        m_baseDebtor = f.debtor;
        m_baseCash = f.cash;

        // The half-typed "Add Entry" form is saved in the file too - put it back.
        ClearForm();
        SupplierBox().Text(hstring{ f.draftSupplier });
        SpeciesBox().Text(hstring{ f.draftSpecies });
        KgsBox().Text(hstring{ f.draftKgs });
        PriceBox().Text(hstring{ f.draftPrice });
        NotesBox().Text(hstring{ f.draftNotes });
        if (!f.draftDate.empty())
            SetPickerDate(DateBox(), f.draftDate);
        FilterBox().Text(L"");
        m_suppressDirty = false;

        m_currentFile = path;
        m_finalizedDate = f.finalizedDate;
        m_allowMixedDates = false; // a different sheet - ask again if its dates get mixed
        m_dirty = dirty;
        ApplyLockState();

        RefreshNameLists();
        // v0.24.11: put the half-typed supplier / species back once more after the drop-down lists were rebuilt (a rebuild
        // can blank text that is not one of the listed names).
        if (!f.draftSupplier.empty())
            SupplierBox().Text(hstring{ f.draftSupplier });
        if (!f.draftSpecies.empty())
            SpeciesBox().Text(hstring{ f.draftSpecies });
        // v0.24.12: the saved text IS in the combo's Text, but an unfocused editable ComboBox shows its selected item, not
        // its Text (Jack: the boxes looked blank until he clicked in them). Select the matching list item now, and again
        // shortly after the window has drawn. Never over typed text, and a clear / new sheet (m_nameGen) cancels it.
        ShowDraftInCombo(SupplierBox(), f.draftSupplier);
        ShowDraftInCombo(SpeciesBox(), f.draftSpecies);
        if (!f.draftSupplier.empty() || !f.draftSpecies.empty())
        {
            const int gen = m_nameGen;
            const std::wstring wantSupplier = f.draftSupplier;
            const std::wstring wantSpecies = f.draftSpecies;
            auto weakSelf = get_weak();
            for (const int delayMs : { 300, 1500 })
            {
                auto timer = DispatcherQueue().CreateTimer();
                timer.Interval(std::chrono::milliseconds(delayMs));
                timer.IsRepeating(false);
                auto token = std::make_shared<winrt::event_token>();
                *token = timer.Tick([weakSelf, gen, wantSupplier, wantSpecies, timer, token](auto&&, auto&&) {
                    timer.Tick(*token);
                    auto self = weakSelf.get();
                    if (!self || self->m_closed || self->m_nameGen != gen)
                        return;
                    // Only while the box is still empty (not typed into since).
                    if (ToStd(self->SupplierBox().Text()).empty() || self->SupplierBox().SelectedIndex() < 0)
                        if (TypedComboText(self->SupplierBox()).empty() || TypedComboText(self->SupplierBox()) == wantSupplier)
                            ShowDraftInCombo(self->SupplierBox(), wantSupplier);
                    if (ToStd(self->SpeciesBox().Text()).empty() || self->SpeciesBox().SelectedIndex() < 0)
                        if (TypedComboText(self->SpeciesBox()).empty() || TypedComboText(self->SpeciesBox()) == wantSpecies)
                            ShowDraftInCombo(self->SpeciesBox(), wantSpecies);
                });
                timer.Start();
            }
        }
        RefreshList();
        Recalculate();
        UpdateTitle();
        std::wstring msg = statusNote.empty() ? (L"Opened - " + std::to_wstring(m_entries.size()) + L" entries") : statusNote;
        if (!m_finalizedDate.empty())
            msg += L" - FINALIZED " + DisplayDate(m_finalizedDate) + L", locked";
        if (f.skippedLines > 0)
            msg += L" (" + std::to_wstring(f.skippedLines) + L" unreadable lines skipped)";
        SetStatus(msg, false);
    }

    // ----------------------------------------------------------------------------------------------------------
    // Autosave / recovery / rolling backups in the SHARED data folder (C:\BalancingAct\data) - the same autosave.fbd
    // and backups\ folder the Win32 app uses, so work started in one app can be recovered by the other.
    // Only ONE running copy owns autosave.fbd (the first to start, see AutosaveShared.h); a second copy starts blank
    // and does not touch it.
    // ----------------------------------------------------------------------------------------------------------
    void MainWindow::RecoverFromAutosave()
    {
        const std::wstring dir = autosave_shared::DataDir();
        m_autosaveOwner = autosave_shared::TryAcquireOwnership(dir);
        if (!m_autosaveOwner)
        {
            m_autosaveReady = true; // ready, but not the owner: ScheduleAutosave / AutosaveNow do nothing
            MessageBoxW(Hwnd(),
                L"Another copy of Fish Balance Manager (this app or the Win32 one) is already open and is using the automatic "
                L"backup (autosave).\n\nThis window starts with a blank sheet and will NOT autosave or recover anything. "
                L"Save your work with File > Save As, or close the other copy first.",
                L"Another Copy Is Open", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const std::wstring path = dir + L"\\autosave.fbd";
        const DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
        {
            LoadFbdFileResult res = LoadFbdFileContent(path);
            if (!res.ok)
            {
                // Keep the unreadable file for a manual rescue instead of letting the next autosave overwrite it.
                const std::wstring kept = dir + L"\\autosave_unreadable_" + std::to_wstring(GetTickCount64()) + L".fbd";
                MoveFileExW(path.c_str(), kept.c_str(), 0);
                MessageBoxW(Hwnd(),
                    (L"The last automatic backup (autosave.fbd) could not be read: " + res.errorMessage +
                     L"\n\nIt has been kept as " + kept + L" so nothing is lost. Starting with a blank sheet.").c_str(),
                    L"Autosave Could Not Be Loaded", MB_OK | MB_ICONWARNING);
            }
            else
            {
                const FbdLoadResult& f = res.parsed;
                const bool hasContent = !f.entries.empty() || !f.debtor.empty() || !f.cash.empty() ||
                                        !f.draftSupplier.empty() || !f.draftSpecies.empty() ||
                                        !f.draftKgs.empty() || !f.draftPrice.empty();
                if (hasContent)
                {
                    // Same identity rule as the Win32 app (F1): link the recovered sheet to a named file only when the
                    // autosave's own SOURCE_FILE agrees with settings.txt's LASTFILE; otherwise it is an unsaved sheet.
                    const std::wstring lastFile = autosave_shared::ReadSetting(dir, L"LASTFILE");
                    const bool dirtyAtExit = autosave_shared::ReadSetting(dir, L"DIRTY") == L"1";
                    const AutosaveRecoveryDecision d = DecideAutosaveRecovery(false, f.hasSourceFile, f.sourceFile, lastFile);
                    const bool linked = d.outcome == AutosaveRecoveryOutcome::MatchedNamedFile;
                    // Unsaved sheet, or a named file whose app was closed with unsaved changes: counts as unsaved.
                    const bool dirty = !linked || dirtyAtExit;
                    ApplyParsedFile(f, linked ? d.resultingCurrentFile : std::wstring(), dirty,
                        L"Recovered your last session from autosave - " + std::to_wstring(f.entries.size()) + L" entries" +
                        (linked ? L"" : L" (not linked to a file: use File > Save As to keep it)"));
                }
            }
        }
        m_autosaveReady = true;
        ScheduleAutosave();
    }

    void MainWindow::ScheduleAutosave()
    {
        if (!m_autosaveReady || !m_autosaveOwner || m_closed)
            return;
        try
        {
            if (!m_autosaveTimer)
            {
                m_autosaveTimer = DispatcherQueue().CreateTimer();
                m_autosaveTimer.Interval(std::chrono::milliseconds(1500));
                m_autosaveTimer.IsRepeating(false);
                auto weak = get_weak();
                m_autosaveTimer.Tick([weak](auto&&, auto&&) {
                    if (auto self = weak.get())
                        if (!self->m_closed)
                            self->AutosaveNow();
                });
            }
            m_autosaveTimer.Stop(); // debounce: restart the countdown on every change
            m_autosaveTimer.Start();
        }
        catch (winrt::hresult_error const&) {}
    }

    // Writes autosave.fbd (with SOURCE_FILE= so the Win32 app can tell which file the sheet belongs to) and, every few
    // minutes when something changed, a snapshot into the shared backups\ folder (newest N kept, like the Win32 app (Settings: how often / how many)).
    void MainWindow::AutosaveNow()
    {
        if (!m_autosaveReady || !m_autosaveOwner || m_closed)
            return;
        try
        {
            // GatherSaveState also resets the "clean" Debtor/Cash baseline - an autosave is not a save, so put it back.
            const std::wstring keepD = m_baseDebtor, keepC = m_baseCash;
            FbdSaveState s = GatherSaveState(m_finalizedDate);
            m_baseDebtor = keepD;
            m_baseCash = keepC;
            s.includeSourceFile = true;
            s.sourceFile = m_currentFile;
            const std::string content = WToUtf8(BuildFbdText(s));

            const std::wstring dir = autosave_shared::DataDir();
            std::wstring err;
            if (WriteFileAtomicUtf8(dir + L"\\autosave.fbd", content, &err))
                m_autosaveFailWarned = false;
            else if (!m_autosaveFailWarned)
            {
                m_autosaveFailWarned = true;
                SetStatus(L"Warning: automatic backup (autosave.fbd) could not be saved: " + err + L" - use File > Save.", true);
            }

            const ULONGLONG now = GetTickCount64();
            if (m_lastBackupTick == 0 || now - m_lastBackupTick >= static_cast<ULONGLONG>(ClampBackupMinutes(g_user.backupMinutes)) * 60ULL * 1000ULL)
            {
                m_lastBackupTick = now;
                if (content != m_lastBackupContent)
                {
                    const std::wstring bdir = dir + L"\\backups";
                    CreateDirectoryW(bdir.c_str(), nullptr);
                    std::wstring label = L"unsaved";
                    if (!m_currentFile.empty())
                    {
                        const size_t slash = m_currentFile.find_last_of(L"\\/");
                        std::wstring n = (slash == std::wstring::npos) ? m_currentFile : m_currentFile.substr(slash + 1);
                        const size_t dot = n.find_last_of(L'.');
                        if (dot != std::wstring::npos)
                            n = n.substr(0, dot);
                        if (!n.empty())
                            label = n;
                    }
                    SYSTEMTIME st;
                    GetLocalTime(&st);
                    std::wstring bpath;
                    for (int counter = 0; counter < 1000; ++counter)
                    {
                        bpath = bdir + L"\\" + BuildBackupFileName(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                                                                 st.wSecond, st.wMilliseconds, label, counter);
                        if (GetFileAttributesW(bpath.c_str()) == INVALID_FILE_ATTRIBUTES)
                            break;
                    }
                    std::wstring ignored;
                    if (WriteFileAtomicUtf8(bpath, content, &ignored))
                    {
                        m_lastBackupContent = content;
                        // Prune: names start with a sortable timestamp, so oldest sort first. Keep the newest 50.
                        std::vector<std::wstring> names;
                        WIN32_FIND_DATAW fd{};
                        HANDLE h = FindFirstFileW((bdir + L"\\backup_*.fbd").c_str(), &fd);
                        if (h != INVALID_HANDLE_VALUE)
                        {
                            do
                            {
                                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                                    names.push_back(fd.cFileName);
                            } while (FindNextFileW(h, &fd));
                            FindClose(h);
                        }
                        const size_t keepN = static_cast<size_t>(ClampBackupKeep(g_user.backupKeep));
                        if (names.size() > keepN)
                        {
                            std::sort(names.begin(), names.end());
                            for (size_t i = 0; i < names.size() - keepN; ++i)
                                _wremove((bdir + L"\\" + names[i]).c_str());
                        }
                    }
                    else
                        m_lastBackupTick = 0; // retry at the next autosave
                }
            }
        }
        catch (winrt::hresult_error const&) {}
    }

    // Last autosave on the way out, plus LASTFILE / DIRTY in settings.txt so the next start (either app) knows whether
    // the recovered sheet has unsaved changes. Called once, when the exit is going ahead and the controls still exist.
    void MainWindow::ShutdownAutosave()
    {
        if (m_shutdownDone)
            return;
        m_shutdownDone = true;
        if (!m_autosaveReady || !m_autosaveOwner)
            return;
        AutosaveNow();
        const std::wstring dir = autosave_shared::DataDir();
        autosave_shared::WriteSetting(dir, L"LASTFILE", m_currentFile);
        autosave_shared::WriteSetting(dir, L"DIRTY", m_dirty ? L"1" : L"0");
    }

    // Everything a save writes, gathered from the window (mirrors main.cpp's BuildFbdSaveContent).
    FbdSaveState MainWindow::GatherSaveState(const std::wstring& finalizedDate)
    {
        FbdSaveState s;
        s.debtor = ToStd(DebtorBox().Text());
        s.cash = ToStd(CashBox().Text());
        m_baseDebtor = s.debtor; // what is about to be written is the new clean baseline
        m_baseCash = s.cash;
        s.finalizedDate = finalizedDate;
        s.draftSupplier = TypedComboText(SupplierBox());
        s.draftSpecies = TypedComboText(SpeciesBox());
        s.draftKgs = ToStd(KgsBox().Text());
        s.draftPrice = ToStd(PriceBox().Text());
        s.draftNotes = ToStd(NotesBox().Text());
        s.hasDraftDate = true;
        s.draftDate = DateFromPicker(DateBox());
        s.entries = m_entries;
        s.draftBook = m_currentBook;
        s.draftBookType = m_currentBookType;
        return s;
    }

    // Same content the Win32 app saves (BuildFbdText from the core), written atomically.
    bool MainWindow::SaveTo(const std::wstring& path)
    {
        const FbdSaveState s = GatherSaveState(m_finalizedDate);
        std::wstring err;
        if (!WriteFileAtomicUtf8(path, WToUtf8(BuildFbdText(s)), &err))
        {
            const std::wstring msg = L"Could not save the file: " + err;
            MessageBoxW(Hwnd(), msg.c_str(), L"Fish Balance Manager", MB_OK | MB_ICONERROR);
            return false;
        }
        return true;
    }

    void MainWindow::DoSave(bool saveAs)
    {
        std::wstring path = m_currentFile;
        if (saveAs || path.empty())
        {
            wchar_t file[MAX_PATH] = L"";
            if (!m_currentFile.empty())
                wcsncpy_s(file, MAX_PATH, m_currentFile.c_str(), _TRUNCATE);
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = Hwnd();
            ofn.lpstrFilter = L"Fish Balance Files (*.fbd)\0*.fbd\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrDefExt = L"fbd";
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
            if (!GetSaveFileNameW(&ofn))
                return;
            path = file;
        }
        if (!SaveTo(path))
            return;
        m_currentFile = path;
        m_dirty = false;
        UpdateTitle();
        RememberRecentFile(path);
        SetStatus(L"Saved.", false);
    }

    // Mirrors ApplyFinalizedLockState() in the Win32 app: a finalized day greys out every control
    // that could change the numbers, and the button flips to Un-finalize Day.
    void MainWindow::ApplyLockState()
    {
        if (m_closed || !FinalizeButton() || !FinalizedBar() || !FinalizedTitle() || !FinalizedMsg())
            return;
        const bool locked = !m_finalizedDate.empty();
        const bool unlocked = !locked;
        const bool editing = (m_editIndex >= 0);

        SupplierBox().IsEnabled(unlocked);
        SpeciesBox().IsEnabled(unlocked);
        KgsBox().IsEnabled(unlocked);
        PriceBox().IsEnabled(unlocked);
        NotesBox().IsEnabled(unlocked);
        DateBox().IsEnabled(unlocked);
        AddButton().IsEnabled(unlocked);
        DeleteButton().IsEnabled(unlocked);
        EditButton().IsEnabled(unlocked && !editing);
        CancelEditButton().IsEnabled(unlocked && editing);
        DuplicateButton().IsEnabled(unlocked);
        RepeatButton().IsEnabled(unlocked);
        DebtorBox().IsEnabled(unlocked);
        CashBox().IsEnabled(unlocked);
        StartBookButton().IsEnabled(unlocked);

        if (locked)
        {
            FinalizedTitle().Text(hstring{ L"This day was finalized on " + DisplayDate(m_finalizedDate) });
            FinalizedMsg().Text(L"Entries, Debtor and Cash are locked. Press Un-finalize Day to edit them.");
        }
        FinalizedBar().Visibility(locked ? Visibility::Visible : Visibility::Collapsed);

        FinalizeButton().Content(box_value(locked ? L"Un-finalize Day" : L"Finalize Day"));
        FinalizeButton().IsEnabled(true); // the one control that flips the lock
        UpdateTitle();
    }

    // Rolling-backup style snapshot (best effort, silent) in a backups\ folder next to the open
    // file - the Win32 app's WriteBackupSnapshot, simplified: same file name pattern, no pruning.
    void MainWindow::WriteSnapshot(const wchar_t* label)
    {
        if (m_currentFile.empty())
            return;
        const size_t slash = m_currentFile.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return;
        const std::wstring dir = m_currentFile.substr(0, slash) + L"\\backups";
        CreateDirectoryW(dir.c_str(), nullptr);
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t name[96];
        swprintf(name, 96, L"\\backup_%04d%02d%02d_%02d%02d%02d_%03d_%s.fbd", st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, label);
        std::wstring ignored;
        WriteFileAtomicUtf8(dir + name, WToUtf8(BuildFbdText(GatherSaveState(m_finalizedDate))), &ignored);
    }

    void MainWindow::Finalize_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed)
            return;
        if (!m_finalizedDate.empty())
        {
            UnfinalizeNow();
            return;
        }
        // Same two blocks as the Win32 app: no override.
        if (!m_reconValid)
        {
            MessageBoxW(Hwnd(),
                L"Debtor or Cash contains an entry that can't be understood as a number or sum - "
                L"a day can't be finalized until both fields contain a valid figure.",
                L"Invalid Entry", MB_OK | MB_ICONWARNING);
            return;
        }
        if (!m_diffOk)
        {
            MessageBoxW(Hwnd(),
                L"Debtor + Cash doesn't balance against the entered total yet - a day can't be "
                L"finalized until it balances exactly.",
                L"Not Balanced", MB_OK | MB_ICONWARNING);
            return;
        }
        if (m_currentFile.empty())
        {
            MessageBoxW(Hwnd(),
                L"Save this sheet first (File > Save As). The finalized record is written to a "
                L"history folder next to the saved file.",
                L"Save First", MB_OK | MB_ICONINFORMATION);
            return;
        }
        ShowFinalizeDialog();
    }

    // Date prompt (the Win32 app's small "Finalize Day" window). Defaults to today, or to the date in
    // the open file's name when it is an ISO date (e.g. 2026-09-20.fbd) - same rule as Win32.
    winrt::fire_and_forget MainWindow::ShowFinalizeDialog()
    {
        auto strong = get_strong();

        std::wstring defaultIso = TodayIso();
        {
            std::wstring base = m_currentFile;
            const size_t slash = base.find_last_of(L"\\/");
            if (slash != std::wstring::npos) base = base.substr(slash + 1);
            const size_t dot = base.find_last_of(L'.');
            if (dot != std::wstring::npos) base = base.substr(0, dot);
            int y = 0, m = 0, d = 0;
            if (swscanf_s(base.c_str(), L"%d-%d-%d", &y, &m, &d) == 3 && y >= 2000 && m >= 1 && m <= 12 && d >= 1 && d <= 31)
                defaultIso = base;
        }

        Controls::TextBlock label;
        label.Text(L"Lock in today's balanced entries as of this date:");
        label.TextWrapping(TextWrapping::Wrap);
        Controls::CalendarDatePicker picker;
        picker.DateFormat(L"{day.integer(2)}/{month.integer(2)}/{year.full}");
        SetPickerDate(picker, defaultIso);
        Controls::StackPanel panel;
        panel.Spacing(12);
        panel.Children().Append(label);
        panel.Children().Append(picker);

        Controls::ContentDialog dlg;
        dlg.XamlRoot(RootGrid().XamlRoot());
        dlg.Title(box_value(L"Finalize Day"));
        dlg.Content(panel);
        dlg.PrimaryButtonText(L"Finalize");
        dlg.CloseButtonText(L"Cancel");
        dlg.DefaultButton(Controls::ContentDialogButton::Primary);

        const Controls::ContentDialogResult result = co_await dlg.ShowAsync();
        if (m_closed || result != Controls::ContentDialogResult::Primary)
            co_return;
        FinalizeNow(DateFromPicker(picker));
    }

    // The persistence coordination is the SAME core function the Win32 app uses
    // (CoordinateFinalizeDay), with this window's real writes plugged in.
    void MainWindow::FinalizeNow(const std::wstring& date)
    {
        const size_t slash = m_currentFile.find_last_of(L"\\/");
        if (m_currentFile.empty() || slash == std::wstring::npos)
            return;
        const std::wstring dir = m_currentFile.substr(0, slash) + L"\\history";
        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring path = dir + L"\\" + date + L".fbd";

        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            const std::wstring warn = L"A history record for " + date + L" already exists (history\\" + date +
                                      L".fbd). Overwrite it with today's data?";
            if (MessageBoxW(Hwnd(), warn.c_str(), L"Already Finalized", MB_YESNO | MB_ICONWARNING) != IDYES)
                return;
        }

        FinalizePersistencePorts ports;
        ports.writeBackupSnapshot = [this]() { WriteSnapshot(L"finalize"); };
        ports.writeHistoryRecord = [this, path, date](std::wstring* outError) {
            return WriteFileAtomicUtf8(path, WToUtf8(BuildFbdText(GatherSaveState(date))), outError);
        };
        ports.hasNamedFile = !m_currentFile.empty();
        ports.writeNamedFileSync = [this, date](std::wstring* outError) {
            // Silent version of SaveTo: the outcome message below reports a failure.
            std::wstring err;
            const bool ok = WriteFileAtomicUtf8(m_currentFile, WToUtf8(BuildFbdText(GatherSaveState(date))), &err);
            if (!ok && outError) *outError = err;
            return ok;
        };

        const FinalizeOutcome outcome = CoordinateFinalizeDay(ports, date);
        if (outcome.kind == FinalizeResultKind::HistoryWriteFailed)
        {
            MessageBoxW(Hwnd(), outcome.message.c_str(), L"Error", MB_OK | MB_ICONERROR);
            return;
        }
        if (outcome.finalized)
        {
            m_finalizedDate = date;
            ApplyLockState();
        }
        m_dirty = outcome.dirtyAfter;
        UpdateTitle();

        const bool namedFileSynced = (outcome.kind == FinalizeResultKind::FinalizedClean);
        const std::wstring successMsg = outcome.message + L" Start a new entry sheet now?";
        const int r = MessageBoxW(Hwnd(), successMsg.c_str(), L"Day Finalized",
                                  namedFileSynced ? (MB_YESNO | MB_ICONQUESTION) : (MB_YESNO | MB_ICONWARNING));
        if (r == IDYES)
            StartNewSheet();
        else
            SetStatus(L"Day finalized.", false);
    }

    // Same clear-to-blank as File > New in the Win32 app: detaches from the finalized file so the
    // next day's sheet is a genuinely new, unlocked, unsaved sheet.
    void MainWindow::StartNewSheet()
    {
        ClearUndoDelete();
        m_emailedThisSheet = false;
        m_suppressDirty = true;
        EndEditMode();
        m_entries.clear();
        SetCurrentBook(L"", 0);
        DebtorBox().Text(L"");
        CashBox().Text(L"");
        m_baseDebtor.clear();
        m_baseCash.clear();
        ClearForm();
        FilterBox().Text(L"");
        m_suppressDirty = false;
        m_currentFile.clear();
        m_finalizedDate.clear();
        m_allowMixedDates = false; // a different sheet - ask again if its dates get mixed
        m_dirty = false;
        ApplyLockState();
        RefreshNameLists();
        RefreshList();
        Recalculate();
        UpdateTitle();
        SetStatus(L"New sheet.", false);
    }

    void MainWindow::UnfinalizeNow()
    {
        const std::wstring msg = L"This day was finalized on " + m_finalizedDate + L" - reopen it for editing?";
        if (MessageBoxW(Hwnd(), msg.c_str(), L"Un-finalize Day", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;

        WriteSnapshot(L"unfinalize"); // keep the locked-in state, as the Win32 app does
        m_finalizedDate.clear();
        ApplyLockState();
        m_dirty = true;
        if (!m_currentFile.empty())
        {
            if (SaveTo(m_currentFile))
                m_dirty = false;
        }
        UpdateTitle();
        SetStatus(L"Day re-opened for editing.", false);
    }

    void MainWindow::Exit_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed)
            return;
        // Window.Close() does not go through the AppWindow Closing handler above, so File > Exit has to ask
        // about unsaved changes itself (the window's X button does go through it).
        if (!ConfirmDiscard(L"exit"))
            return;
        m_exitConfirmed = true; // answered once - the Closing handler must not ask a second time
        ShutdownAutosave();
        // Not inside the menu item's own Click callback: closing the window from there makes XAML report
        // "Requested operation cannot be performed inside the callback or event handler" (0x80400001).
        // Queue it so the flyout finishes first.
        auto weak = get_weak();
        DispatcherQueue().TryEnqueue([weak]() {
            if (auto self = weak.get())
            {
                if (self->m_closed)
                    return;
                try { self->Close(); }
                catch (winrt::hresult_error const&) {} // already closing - nothing to do
            }
        });
    }

    void MainWindow::Add_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed)
            return;
        AddEntryClicked();
    }

    int MainWindow::RealIndexOfSelection()
    {
        int32_t row = EntriesList().SelectedIndex();
        if (row < 0 || static_cast<size_t>(row) >= m_shown.size())
            return -1;
        return static_cast<int>(m_shown[static_cast<size_t>(row)]);
    }

    void MainWindow::Delete_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed)
            return;
        if (m_editIndex >= 0)
        {
            SetStatus(L"Finish or cancel the edit first.", true);
            return;
        }
        if (EntriesList().SelectedItems().Size() > 1)
        {
            SetStatus(L"Select just one row to delete.", true);
            return;
        }
        int idx = RealIndexOfSelection();
        if (idx < 0)
        {
            SetStatus(L"Select a row to delete.", true);
            return;
        }
        const std::wstring deletedSpecies = m_entries[static_cast<size_t>(idx)].product;
        m_undoEntry = m_entries[static_cast<size_t>(idx)]; // Edit > Undo Delete (one level)
        m_undoIndex = idx;
        m_hasUndo = true;
        UndoDeleteItem().IsEnabled(true);
        m_entries.erase(m_entries.begin() + idx);
        ReevaluateOutlierFlagsForSpecies(m_entries, deletedSpecies);
        MarkDirty();
        SetStatus(L"Entry deleted. Edit > Undo Delete brings it back.", false);
        RefreshNameLists();
        RefreshList();
        Recalculate();
    }

    void MainWindow::Edit_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed)
            return;
        EditSelected();
    }

    // Double-click a row = Edit (same as the Win32 app's double-click; a flagged row gets the review first).
    void MainWindow::EntriesList_DoubleTapped(Windows::Foundation::IInspectable const&,
                                              Input::DoubleTappedRoutedEventArgs const&)
    {
        if (m_closed || !m_finalizedDate.empty() || EntriesList().SelectedIndex() < 0)
            return;
        EditSelected();
    }

    // Right-click a flagged row -> "Clear flag" (same as the Win32 context menu).
    // v0.16.0 (as Win32): Move the selected rows to a book (empty name = out of any book). Not on a finalized day.
    void MainWindow::MoveSelectedRowsToBook(const std::wstring& name, wchar_t type)
    {
        if (m_closed || !m_finalizedDate.empty())
            return;
        std::vector<size_t> picked;
        auto items = EntriesList().Items();
        auto selected = EntriesList().SelectedItems();
        for (uint32_t k = 0; k < selected.Size(); ++k)
        {
            uint32_t row = 0;
            if (items.IndexOf(selected.GetAt(k), row) && row < m_shown.size())
                picked.push_back(m_shown[row]);
        }
        const wchar_t cleanType = name.empty() ? static_cast<wchar_t>(0) : CleanBookType(type);
        int moved = 0;
        for (size_t idx : picked)
        {
            if (idx >= m_entries.size())
                continue;
            Entry& e = m_entries[idx];
            if (e.book == name && e.bookType == cleanType)
                continue;
            e.book = name;
            e.bookType = cleanType;
            ++moved;
        }
        if (moved == 0)
            return;
        MarkDirty();
        RefreshList();
        Recalculate();
        const std::wstring rows = std::to_wstring(moved) + (moved == 1 ? L" row" : L" rows");
        SetStatus(name.empty() ? L"Took " + rows + L" out of its book." : L"Moved " + rows + L" to " + name + L".", false);
    }

    void MainWindow::EntriesList_RightTapped(Windows::Foundation::IInspectable const&,
                                             Input::RightTappedRoutedEventArgs const& e)
    {
        if (m_closed)
            return;
        DependencyObject node = e.OriginalSource().try_as<DependencyObject>();
        Controls::ListViewItem item{ nullptr };
        while (node)
        {
            item = node.try_as<Controls::ListViewItem>();
            if (item)
                break;
            node = Media::VisualTreeHelper::GetParent(node);
        }
        if (!item)
            return;
        const int32_t row = EntriesList().IndexFromContainer(item);
        if (row < 0 || static_cast<size_t>(row) >= m_shown.size())
            return;
        const size_t real = m_shown[static_cast<size_t>(row)];
        // Right-clicking a row that is already selected keeps the whole selection (several rows can be moved to a
        // book at once); right-clicking any other row selects just that row.
        {
            bool alreadySelected = false;
            auto selected = EntriesList().SelectedItems();
            auto items = EntriesList().Items();
            for (uint32_t k = 0; k < selected.Size() && !alreadySelected; ++k)
            {
                uint32_t r = 0;
                if (items.IndexOf(selected.GetAt(k), r) && static_cast<int32_t>(r) == row)
                    alreadySelected = true;
            }
            if (!alreadySelected)
                EntriesList().SelectedIndex(row);
        }
        const bool flagged = m_entries[real].priceFlagged;
        const bool canMove = m_finalizedDate.empty();
        if (!flagged && !canMove)
            return;

        auto weak = get_weak();
        Controls::MenuFlyout flyout;
        if (flagged)
        {
            Controls::MenuFlyoutItem clear;
            clear.Text(L"Clear flag");
            clear.Click([weak, real](auto&&, auto&&) {
                if (auto self = weak.get())
                {
                    if (real < self->m_entries.size())
                    {
                        self->m_entries[real].priceFlagged = false;
                        self->RefreshList();
                        self->SetStatus(L"Flag cleared.", false);
                    }
                }
            });
            flyout.Items().Append(clear);
        }
        if (canMove)
        {
            if (flagged)
                flyout.Items().Append(Controls::MenuFlyoutSeparator{});
            const uint32_t n = EntriesList().SelectedItems().Size();
            Controls::MenuFlyoutSubItem sub;
            sub.Text(hstring{ L"Move " + std::to_wstring(n) + (n == 1 ? L" selected row to book" : L" selected rows to book") });
            auto addMove = [&](const std::wstring& text, std::wstring name, wchar_t type) {
                Controls::MenuFlyoutItem item;
                item.Text(hstring{ text });
                item.Click([weak, name, type](auto&&, auto&&) {
                    if (auto self = weak.get())
                        self->MoveSelectedRowsToBook(name, type);
                });
                sub.Items().Append(item);
            };
            const BooksResult br = ComputeBookSummaries(m_entries);
            for (size_t i = 0; i < br.books.size() && i < 40; ++i)
                addMove(br.books[i].name + L" (" + std::to_wstring(br.books[i].count) + L" rows)", br.books[i].name,
                        br.books[i].type);
            if (!br.books.empty())
                sub.Items().Append(Controls::MenuFlyoutSeparator{});
            addMove(L"New Debtor book (" + NextBookName(L'D') + L")", NextBookName(L'D'), L'D');
            addMove(L"New Cash book (" + NextBookName(L'C') + L")", NextBookName(L'C'), L'C');
            sub.Items().Append(Controls::MenuFlyoutSeparator{});
            addMove(L"No book", L"", 0);
            flyout.Items().Append(sub);
        }
        flyout.ShowAt(item);
        e.Handled(true);
    }

    void MainWindow::EditSelected()
    {
        if (EntriesList().SelectedItems().Size() > 1)
        {
            SetStatus(L"Select just one row to edit.", true);
            return;
        }
        int idx = RealIndexOfSelection();
        if (idx < 0)
        {
            SetStatus(L"Select a row to edit.", true);
            return;
        }
        if (m_entries[static_cast<size_t>(idx)].priceFlagged)
        {
            // Same review as the Win32 app's ReviewOrEditEntry: re-check against today's other
            // entries; Yes = go and edit it, No = clear the flag and leave the entry alone.
            Entry& fe = m_entries[static_cast<size_t>(idx)];
            const std::vector<double> baseline = GatherOtherPricesForSpecies(m_entries, fe.product, idx);
            OutlierRange range;
            const bool stillOutlier =
                ComputeOutlierRange(baseline, range) && (fe.price < range.low || fe.price > range.high);
            wchar_t msg[768];
            if (stillOutlier)
            {
                const bool tooHigh = fe.price > range.high;
                const double limit = tooHigh ? range.high : range.low;
                swprintf(msg, 768,
                         L"This entry is $%.2f/kg for %s.\n"
                         L"The dynamic limit calculated from the other %s entries on this sheet is %s $%.2f/kg.\n\n"
                         L"Still looks like a typo?\n\n"
                         L"Click Yes to edit it, No to leave it as-is.",
                         fe.price, fe.product.c_str(), fe.product.c_str(), tooHigh ? L"max" : L"min", limit);
            }
            else
            {
                swprintf(msg, 768,
                         L"This entry's price ($%.2f) was flagged earlier as unusual for %s, but no "
                         L"longer looks unusual against the other entries on this sheet. Click Yes to edit it anyway, "
                         L"No to clear the flag and leave it as-is.",
                         fe.price, fe.product.c_str());
            }
            if (MessageBoxW(Hwnd(), msg, L"Flagged Price", MB_YESNO | MB_ICONWARNING) == IDNO)
            {
                fe.priceFlagged = false;
                RefreshList();
                return;
            }
        }
        m_editIndex = idx;
        FillFormFromEntry(m_entries[static_cast<size_t>(idx)], true);
        AddButton().Content(box_value(L"Update Entry"));
        ApplyLockState();
        SetStatus(L"Editing - change the fields, then press Update Entry.", false);
        KgsBox().Focus(FocusState::Programmatic);
    }

    void MainWindow::CancelEdit_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed)
            return;
        EndEditMode();
        ClearForm();
        SetStatus(L"Edit cancelled.", false);
    }

    void MainWindow::EndEditMode()
    {
        m_editIndex = -1;
        AddButton().Content(box_value(L"Add Entry"));
        ApplyLockState();
    }

    // Win32 "Duplicate Last Entry": refill the whole form from the newest entry, ready to tweak.
    void MainWindow::Duplicate_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed)
            return;
        if (m_entries.empty())
        {
            SetStatus(L"There are no entries yet to duplicate.", true);
            return;
        }
        FillFormFromEntry(m_entries.back(), true);
        KgsBox().Focus(FocusState::Programmatic);
        KgsBox().SelectAll();
    }

    // Win32 "Duplicate Supplier & Species": refill only those two from the newest entry.
    void MainWindow::Repeat_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed)
            return;
        if (m_entries.empty())
        {
            SetStatus(L"There are no entries yet to repeat from.", true);
            return;
        }
        FillFormFromEntry(m_entries.back(), false);
        KgsBox().Focus(FocusState::Programmatic);
        KgsBox().SelectAll();
    }

    void MainWindow::FillFormFromEntry(const Entry& e, bool everything)
    {
        SupplierBox().Text(hstring{ e.supplier });
        SpeciesBox().Text(hstring{ e.product });
        if (everything)
        {
            KgsBox().Text(hstring{ KgsBoxText(e, FormatKg(e.kgs)) }); // shows the sum if one was typed
            PriceBox().Text(hstring{ FormatNum(e.price) });
            NotesBox().Text(hstring{ e.notes });
            SetPickerDate(DateBox(), e.date);
        }
    }

    void MainWindow::ClearForm()
    {
        m_nameGen++;
        SupplierBox().SelectedIndex(-1);
        SupplierBox().Text(L"");
        SpeciesBox().SelectedIndex(-1);
        SpeciesBox().Text(L"");
        KgsBox().Text(L"");
        PriceBox().Text(L"");
        NotesBox().Text(L"");
        SetPickerDate(DateBox(), TodayIso());
    }

    void MainWindow::Price_KeyDown(Windows::Foundation::IInspectable const&,
                                   Input::KeyRoutedEventArgs const& e)
    {
        if (m_closed)
            return;
        if (e.Key() == Windows::System::VirtualKey::Enter)
        {
            // Enter in any entry-form field adds the row (like the Win32 app) - except while a
            // Supplier/Species dropdown is open, where Enter just picks the highlighted item.
            if ((SupplierBox() && SupplierBox().IsDropDownOpen()) || (SpeciesBox() && SpeciesBox().IsDropDownOpen()))
                return;
            e.Handled(true);
            AddEntryClicked();
        }
    }

    // Reads what was really typed from the combo's inner text box, shows the resolved name (alias / capitals) at once,
    // and checks again shortly after WinUI's own focus-loss handling, which can blank text it does not recognise.
    // A later Add / clear of the form (m_nameGen) cancels the second check so it never brings cleared text back.
    void MainWindow::ScheduleNameRestore(Controls::ComboBox const& combo, bool isSupplier)
    {
        if (m_closed || !combo)
            return;
        try
        {
            auto inner = FindEditableTextBox(combo);
            const std::wstring typedRaw = inner ? ToStd(inner.Text()) : ToStd(combo.Text());
            const std::wstring typed = TrimW(typedRaw);
            if (typed.empty())
                return;
            EnsureNamesSeeded();
            const NameList& list = isSupplier ? m_names.suppliers : m_names.species;
            const NameResolution r = ResolveName(list, typed);
            std::wstring shown = typedRaw;
            if ((r.kind == NameMatch::Snapped || r.kind == NameMatch::Alias) && r.name != typed)
                shown = r.name;
            if (ToStd(combo.Text()) != shown)
                combo.Text(hstring{ shown });

            const int gen = m_nameGen;
            auto weak = get_weak();
            auto timer = DispatcherQueue().CreateTimer();
            timer.Interval(std::chrono::milliseconds(150));
            timer.IsRepeating(false);
            auto token = std::make_shared<winrt::event_token>();
            *token = timer.Tick([weak, combo, shown, gen, timer, token](auto&&, auto&&) {
                timer.Tick(*token); // remove this handler so the timer can be released
                auto self = weak.get();
                if (!self || self->m_closed || self->m_nameGen != gen)
                    return;
                try
                {
                    if (ToStd(combo.Text()) != shown)
                        combo.Text(hstring{ shown });
                }
                catch (winrt::hresult_error const&) {}
            });
            timer.Start();
        }
        catch (winrt::hresult_error const&) {}
    }

    void MainWindow::Combo_DropDownClosed(Windows::Foundation::IInspectable const&, Windows::Foundation::IInspectable const&)
    {
        m_ddClosedTick = GetTickCount64();
    }

    // Enter in Supplier/Species adds the row, unless that Enter was only picking a dropdown item.
    void MainWindow::Combo_EnterKey(Windows::Foundation::IInspectable const&, Input::KeyRoutedEventArgs const& e)
    {
        if (m_closed)
            return;
        if (e.Key() != Windows::System::VirtualKey::Enter)
            return;
        if ((SupplierBox() && SupplierBox().IsDropDownOpen()) || (SpeciesBox() && SpeciesBox().IsDropDownOpen()))
            return;
        if (GetTickCount64() - m_ddClosedTick < 250) // the dropdown closed on this very Enter
            return;
        // Run after the combo box has finished committing the typed text.
        auto weak = get_weak();
        DispatcherQueue().TryEnqueue([weak]() {
            if (auto self = weak.get())
                if (!self->m_closed)
                    self->AddEntryClicked();
        });
    }

    // v0.24.10: a half-typed Add Entry row (supplier / species / kgs / price / notes) is part of autosave.fbd, so typing in
    // those boxes must schedule an autosave too (it only ran after a sheet change before, so a kill lost the draft).
    void MainWindow::Draft_TextChanged(Windows::Foundation::IInspectable const&,
                                       Controls::TextChangedEventArgs const&)
    {
        if (m_closed)
            return;
        ScheduleAutosave();
    }

    void MainWindow::Box_TextChanged(Windows::Foundation::IInspectable const&,
                                     Controls::TextChangedEventArgs const&)
    {
        if (m_closed)
            return;
        // Only a real change counts: loading a file sets these boxes in code and the event arrives late.
        if (m_loadedOnce && (ToStd(DebtorBox().Text()) != m_baseDebtor || ToStd(CashBox().Text()) != m_baseCash))
            MarkDirty();
        Recalculate();
    }

    void MainWindow::Filter_TextChanged(Windows::Foundation::IInspectable const&,
                                        Controls::TextChangedEventArgs const&)
    {
        if (m_closed)
            return;
        RefreshList();
    }

    void MainWindow::SetStatus(const std::wstring& text, bool isError)
    {
        if (m_closed || !StatusMsg())
            return;
        StatusMsg().Text(hstring{ text });
        StatusMsg().Foreground(Media::SolidColorBrush{
            isError ? Microsoft::UI::Colors::Crimson() : Microsoft::UI::Colors::Gray() });
    }

    // Drop-down contents = placeholder names + every name already used on this sheet.
    // Whatever is currently typed/selected is kept.
    void MainWindow::RefreshNameLists()
    {
        const hstring curSupplier = SupplierBox().Text();
        const hstring curSpecies = SpeciesBox().Text();

        auto build = [this](bool supplier) {
            std::set<std::wstring> seen;
            std::vector<std::wstring> names;
            auto add = [&](const std::wstring& n) {
                if (!n.empty() && seen.insert(Lower(n)).second)
                    names.push_back(n);
            };
            if (m_names.fileExisted) // the master list (names.txt) when there is one, else the placeholders
                for (const std::wstring& n : (supplier ? m_names.suppliers.names : m_names.species.names)) add(n);
            else if (supplier) for (auto s : kSeedSuppliers) add(s);
            else               for (auto s : kSeedSpecies) add(s);
            for (const Entry& e : m_entries)
                add(supplier ? e.supplier : e.product);
            std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) {
                return Lower(a) < Lower(b);
            });
            return names;
        };

        FillCombo(SupplierBox(), build(true));
        FillCombo(SpeciesBox(), build(false));
        SupplierBox().Text(curSupplier);
        SpeciesBox().Text(curSpecies);
    }

    void MainWindow::AddEntryFromForm()
    {
        auto fail = [this](const std::wstring& message) { SetStatus(message, true); };

        const std::wstring supplier = TrimW(ToStd(SupplierBox().Text()));
        const std::wstring species = TrimW(ToStd(SpeciesBox().Text()));
        const std::wstring notes = TrimW(ToStd(NotesBox().Text()));

        if (supplier.empty() || species.empty())
        {
            fail(L"Supplier and Species are required.");
            return;
        }
        if (supplier.find(L'|') != std::wstring::npos || species.find(L'|') != std::wstring::npos ||
            notes.find(L'|') != std::wstring::npos)
        {
            fail(L"The | character isn't allowed in Supplier, Species or Notes.");
            return;
        }
        if (supplier.size() > kMaxSupplierSpeciesLength || species.size() > kMaxSupplierSpeciesLength ||
            notes.size() > kMaxNotesLength)
        {
            fail(L"Supplier, Species or Notes is too long.");
            return;
        }

        double kgs = 0.0;
        double price = 0.0;
        // Kgs accepts a sum like 24+24+24 (the same expression rules as Debtor/Cash); the entry
        // stores the total.
        const std::wstring kgsText = TrimW(ToStd(KgsBox().Text()));
        if (kgsText.empty())
        {
            fail(L"Kgs must be a number.");
            return;
        }
        const SumParseResult kgsSum = ParseSumExprStrict(kgsText, true); // Kgs also takes products like 8*30
        if (!kgsSum.ok)
        {
            fail(kgsSum.tooLarge ? L"Kgs total is too large."
                                 : L"Kgs has an invalid entry: \"" + kgsSum.errorTerm + L"\"");
            return;
        }
        kgs = kgsSum.value;
        if (!ParseDoubleW(ToStd(PriceBox().Text()), price))
        {
            fail(L"Price must be a number.");
            return;
        }
        NumericValidationResult kgsCheck = ValidateWeightKg(kgs);
        if (!kgsCheck.ok)
        {
            fail(kgsCheck.error);
            return;
        }
        NumericValidationResult priceCheck = ValidatePriceDollars(price);
        if (!priceCheck.ok)
        {
            fail(priceCheck.error);
            return;
        }

        const bool editing = (m_editIndex >= 0 && static_cast<size_t>(m_editIndex) < m_entries.size());
        if (!editing && m_entries.size() >= kMaxEntryRecords)
        {
            fail(L"This sheet is full.");
            return;
        }

        Entry entry;
        entry.supplier = supplier;
        entry.product = species;
        entry.kgs = kgs;
        entry.price = price;
        entry.kgsExpr = KgsExprToStore(kgsText);
        entry.date = DateFromPicker(DateBox());
        entry.notes = notes;
        if (editing)
        {
            // Editing a row never moves it to another book.
            entry.book = m_entries[static_cast<size_t>(m_editIndex)].book;
            entry.bookType = m_entries[static_cast<size_t>(m_editIndex)].bookType;
        }
        else
        {
            entry.book = m_currentBook;
            entry.bookType = m_currentBookType;
        }

        // Different-date warning (same rule as the Win32 app): a sheet is normally one day, so an
        // entry dated differently from everything already on it is confirmed first. Defaults to No.
        if (!m_allowMixedDates)
        {
            const int excl = editing ? m_editIndex : -1;
            if (IsNewDateOnSheet(m_entries, entry.date, excl))
            {
                const std::vector<std::wstring> sheetDates = DistinctSheetDates(m_entries, excl);
                std::wstring listed;
                for (size_t i = 0; i < sheetDates.size() && i < 3; ++i)
                {
                    if (i)
                        listed += L", ";
                    listed += DisplayDate(sheetDates[i]);
                }
                if (sheetDates.size() > 3)
                    listed += L" and " + std::to_wstring(sheetDates.size() - 3) + L" more";
                const std::wstring msg =
                    L"The entries already on this sheet are dated " + listed + L", but this entry is dated " +
                    DisplayDate(entry.date) + L".\n\n"
                    L"A sheet is normally one day's entries. Add this entry anyway?\n\n"
                    L"Yes = add it, and don't ask again for this session.\n"
                    L"No = don't add it (change the Date and try again).";
                if (MessageBoxW(Hwnd(), msg.c_str(), L"Different Date", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
                    return;
                m_allowMixedDates = true;
            }
        }

        // Unusual-price check (same rule as the Win32 app): this price vs the OTHER prices for the
        // species on the sheet. It only marks the entry - it never blocks it.
        {
            const std::vector<double> baseline =
                GatherOtherPricesForSpecies(m_entries, species, editing ? m_editIndex : -1);
            OutlierRange range;
            entry.priceFlagged = ComputeOutlierRange(baseline, range) &&
                                 (price < range.low || price > range.high);
        }

        if (editing)
        {
            m_entries[static_cast<size_t>(m_editIndex)] = entry;
            MarkDirty();
            EndEditMode();
            ClearForm();
            SetStatus(L"Entry updated.", false);
        }
        else
        {
            m_entries.push_back(entry);
            MarkDirty();
            // Same batch-entry behaviour as the Win32 app: Supplier and Date stay, the rest
            // clears, focus goes back to Supplier.
            SpeciesBox().SelectedIndex(-1);
            SpeciesBox().Text(L"");
            KgsBox().Text(L"");
            PriceBox().Text(L"");
            NotesBox().Text(L"");
            SetStatus(L"Entry added.", false);
        }

        // A new/edited price can change whether the OTHER entries for this species still look normal.
        ReevaluateOutlierFlagsForSpecies(m_entries, species);

        RecordEntryNames(supplier, species); // a genuinely new name joins the master list, silently
        RefreshNameLists();
        RefreshList();
        Recalculate();
        SupplierBox().Focus(FocusState::Programmatic);
    }

    // names.txt: the master Supplier/Species lists, in the shared data folder (same file and format as the Win32 app).
    void MainWindow::LoadNameMaster()
    {
        m_names = NameMasterData();
        const std::wstring path = SharedFilePath(L"names.txt");
        if (path.empty())
            return;
        std::vector<std::wstring> lines;
        const bool found = ReadAllLines(path, lines);
        if (found)
            ParseNameMasterLines(lines, m_names);
        m_names.fileExisted = found;

        // v0.15.0: one-time move of emails.txt (shared folder and the old exe-folder copy) into names.txt.
        // Addresses already in names.txt win. The old files are kept as emails.txt.migrated.
        std::vector<std::wstring> legacyPaths;
        legacyPaths.push_back(EmailsFilePath());
        {
            wchar_t exe[MAX_PATH] = {};
            const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
            if (n > 0 && n < MAX_PATH)
            {
                std::wstring p(exe);
                const size_t slash = p.find_last_of(L"\\/");
                if (slash != std::wstring::npos)
                {
                    const std::wstring beside = p.substr(0, slash) + L"\\emails.txt";
                    if (beside != legacyPaths[0])
                        legacyPaths.push_back(beside);
                }
            }
        }
        std::vector<std::wstring> migrated;
        for (const std::wstring& lp : legacyPaths)
        {
            if (lp.empty() || GetFileAttributesW(lp.c_str()) == INVALID_FILE_ATTRIBUTES)
                continue;
            std::vector<std::wstring> legacyLines;
            if (!ReadAllLines(lp, legacyLines))
                continue;
            MigrateLegacyEmails(m_names, legacyLines);
            migrated.push_back(lp);
        }
        LoadSupplierEmails(); // fill the view first so the save below keeps the migrated addresses
        if (!migrated.empty() && SaveNameMaster())
        {
            for (const std::wstring& lp : migrated)
            {
                const std::wstring dest = lp + L".migrated";
                if (GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES)
                    DeleteFileW(dest.c_str());
                MoveFileW(lp.c_str(), dest.c_str());
            }
        }
    }

    bool MainWindow::SaveNameMaster()
    {
        const std::wstring path = SharedFilePath(L"names.txt");
        if (path.empty())
            return false;
        SyncListEmails(m_names.suppliers, m_supplierEmails);
        std::string content;
        for (const std::wstring& l : BuildNameMasterLines(m_names))
            content += WToUtf8(l) + "\n";
        const bool ok = WriteFileAtomicUtf8(path, content);
        if (ok)
            m_names.fileExisted = true;
        return ok;
    }

    // First use with no names.txt: start the lists from the names on the open sheet (Win32 does the same).
    void MainWindow::EnsureNamesSeeded()
    {
        if (m_names.fileExisted)
            return;
        const std::wstring today = TodayIso();
        for (const Entry& e : m_entries)
        {
            NameListAdd(m_names.suppliers, e.supplier, today);
            NameListAdd(m_names.species, e.product, today);
        }
        SaveNameMaster(); // on failure fileExisted stays false and the next call simply tries again
    }

    void MainWindow::RecordEntryNames(const std::wstring& supplier, const std::wstring& species)
    {
        EnsureNamesSeeded();
        const std::wstring today = TodayIso();
        bool changed = false;
        if (NameListAdd(m_names.suppliers, supplier, today))
            changed = true;
        if (NameListAdd(m_names.species, species, today))
            changed = true;
        if (changed)
            SaveNameMaster();
    }

    // Add / Update Entry: first check Supplier and Species against the master lists (same rules as the Win32 app).
    // A different capital/spacing or a remembered alias is corrected silently; a near match ("Blk flat" for
    // "Black Flathead") is asked about; anything else is a new name and is added after the entry.
    winrt::fire_and_forget MainWindow::AddEntryClicked()
    {
        auto strong = get_strong();
        if (m_closed || m_resolving)
            co_return;
        m_nameGen++; // cancels any pending "keep what was typed" check
        m_resolving = true;
        EnsureNamesSeeded();
        for (int pass = 0; pass < 2; ++pass)
        {
            if (m_closed)
            {
                m_resolving = false;
                co_return;
            }
            const bool isSupplier = (pass == 0);
            Controls::ComboBox box = isSupplier ? SupplierBox() : SpeciesBox();
            const NameList& list = isSupplier ? m_names.suppliers : m_names.species;
            const std::wstring typed = TrimW(ToStd(box.Text()));
            if (typed.empty())
                continue;
            const NameResolution r = ResolveName(list, typed);
            if ((r.kind == NameMatch::Snapped || r.kind == NameMatch::Alias) && r.name != typed)
            {
                box.Text(hstring{ r.name });
                continue;
            }
            if (r.kind != NameMatch::Near || r.similar.empty())
                continue;

            const std::wstring kindText = isSupplier ? L"supplier" : L"species";
            std::wstring msg = L"\"" + typed + L"\" isn't in the " + kindText + L" list, but it looks like \"" + r.similar[0] + L"\".";
            if (!r.similarVia.empty() && !r.similarVia[0].empty())
                msg += L"\n(\"" + r.similarVia[0] + L"\" is another name for it.)";
            if (r.similar.size() > 1)
            {
                msg += L"\n\nOther close matches: ";
                for (size_t i = 1; i < r.similar.size(); ++i)
                    msg += (i > 1 ? L", " : L"") + r.similar[i];
            }
            Controls::ContentDialog dlg;
            dlg.XamlRoot(RootGrid().XamlRoot());
            dlg.Title(box_value(L"Did you mean...?"));
            dlg.Content(box_value(hstring{ msg }));
            dlg.PrimaryButtonText(hstring{ L"Use \"" + r.similar[0] + L"\"" });
            dlg.SecondaryButtonText(hstring{ L"Keep \"" + typed + L"\" (new)" });
            dlg.CloseButtonText(L"Cancel");
            dlg.DefaultButton(Controls::ContentDialogButton::Primary);
            const Controls::ContentDialogResult dr = co_await dlg.ShowAsync();
            if (m_closed || dr == Controls::ContentDialogResult::None)
            {
                m_resolving = false;
                co_return; // cancelled - nothing is added
            }
            if (dr == Controls::ContentDialogResult::Primary)
                box.Text(hstring{ r.similar[0] });
        }

        // v0.16.0 (as Win32): the very first entry on a sheet with no book asks which book it is in. Only when it
        // would otherwise be added (Kgs and Price filled in); Cancel goes back with nothing added.
        if (m_editIndex < 0 && m_entries.empty() && m_currentBook.empty() && m_finalizedDate.empty() &&
            !TrimW(ToStd(KgsBox().Text())).empty() && !TrimW(ToStd(PriceBox().Text())).empty() &&
            !TrimW(ToStd(SupplierBox().Text())).empty() && !TrimW(ToStd(SpeciesBox().Text())).empty())
        {
            Controls::RadioButtons choices;
            choices.Items().Append(box_value(hstring{ L"Start a Debtor book (" + NextBookName(L'D') + L")" }));
            choices.Items().Append(box_value(hstring{ L"Start a Cash book (" + NextBookName(L'C') + L")" }));
            choices.Items().Append(box_value(L"No book - carry on without one"));
            choices.SelectedIndex(0);
            Controls::StackPanel panel;
            Controls::TextBlock intro;
            intro.Text(L"Which book are you entering from? You can move rows between books later (right-click the rows).");
            intro.TextWrapping(TextWrapping::Wrap);
            panel.Children().Append(intro);
            panel.Children().Append(choices);
            panel.Spacing(12);
            Controls::ContentDialog dlg;
            dlg.XamlRoot(RootGrid().XamlRoot());
            dlg.Title(box_value(L"This entry isn't in a book yet"));
            dlg.Content(panel);
            dlg.PrimaryButtonText(L"OK");
            dlg.CloseButtonText(L"Cancel");
            dlg.DefaultButton(Controls::ContentDialogButton::Primary);
            const Controls::ContentDialogResult dr = co_await dlg.ShowAsync();
            if (m_closed || dr != Controls::ContentDialogResult::Primary)
            {
                m_resolving = false;
                co_return; // cancelled - nothing is added
            }
            const int pick = choices.SelectedIndex();
            if (pick == 0)
                SetCurrentBook(NextBookName(L'D'), L'D');
            else if (pick == 1)
                SetCurrentBook(NextBookName(L'C'), L'C');
        }
        m_resolving = false;
        AddEntryFromForm();
    }

    void MainWindow::RefreshList()
    {
        if (m_closed || !EntriesList() || !FilterBox() || !CountText())
            return;

        const std::wstring needle = Lower(TrimW(ToStd(FilterBox().Text())));
        m_shown.clear();
        EntriesList().Items().Clear();
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            const Entry& e = m_entries[i];
            if (!needle.empty())
            {
                const std::wstring hay = Lower(e.supplier + L" " + e.product + L" " + e.notes + L" " +
                                               e.date + L" " + DisplayDate(e.date));
                if (hay.find(needle) == std::wstring::npos)
                    continue;
            }
            m_shown.push_back(i);
            const std::wstring cells[kColCount] = { e.supplier, e.product, FormatKg(e.kgs),
                                                    (e.priceFlagged ? L"\u26A0 " : L"") + FormatMoney(e.price), FormatMoney(e.Total()),
                                                    DisplayDate(e.date), e.notes, e.book };
            EntriesList().Items().Append(MakeRow(cells, e.priceFlagged, IsBookStart(m_entries, i)));
        }

        std::wstring count = std::to_wstring(m_entries.size()) + (m_entries.size() == 1 ? L" entry" : L" entries");
        if (m_shown.size() != m_entries.size())
            count += L" (" + std::to_wstring(m_shown.size()) + L" shown)";
        CountText().Text(hstring{ count });
        RefreshOverview();
    }

    // Total Overview list: same aggregation as the Win32 app (ComputeGroupedTotals by supplier,
    // sorted, with a GRAND TOTAL row at the end). Always uses ALL entries, not the filtered view.
    void MainWindow::RefreshOverview()
    {
        if (m_closed || !OvList())
            return;
        OvList().Items().Clear();
        const GroupedTotalsResult totals = ComputeGroupedTotals(m_entries, true);
        for (const GroupedTotal& row : totals.rows)
        {
            const std::wstring cells[3] = { row.key, FormatKg(row.kgs), FormatMoney(row.amt) };
            OvList().Items().Append(MakeOverviewRow(cells, false));
        }
        const std::wstring grand[3] = { L"GRAND TOTAL", FormatKg(totals.grandKgs), FormatMoney(totals.grandAmt) };
        OvList().Items().Append(MakeOverviewRow(grand, true));
        RefreshBreakdown();
        RefreshBySpecies();
    }

    // Same rules as RecalcTotals() in the Win32 app: the entered total, the Debtor + Cash
    // book total, the difference, and fail-closed when anything cannot be trusted.
    void MainWindow::Recalculate()
    {
        // TextChanged can fire while the window is still being built (or torn down).
        if (m_closed || !DebtorBox() || !CashBox() || !BookText() || !EnteredText() || !DiffText() ||
            !BadgeText() || !BadgeCircle() || !OkIcon() || !BadIcon() || !ReasonText())
            return;

        RefreshBooks(); // depends on the Debtor/Cash boxes as well as the entries

        double entered = 0.0;
        bool overflow = false;
        for (const Entry& e : m_entries)
        {
            double term = 0.0;
            if (!SafeMultiplyFinite(e.kgs, e.price, term) || !SafeAccumulate(entered, term))
            {
                overflow = true;
                break;
            }
        }

        SumParseResult debtor = ParseSumExprStrict(ToStd(DebtorBox().Text()));
        SumParseResult cash = ParseSumExprStrict(ToStd(CashBox().Text()));
        const bool valid = debtor.ok && cash.ok && !overflow;
        m_reconValid = valid;
        m_diffOk = false;

        EnteredText().Text(hstring{ overflow ? std::wstring(L"--") : FormatMoney(entered) });

        auto setBadge = [this](const wchar_t* text, bool good) {
            const Windows::UI::Color colour =
                good ? Microsoft::UI::ColorHelper::FromArgb(255, 0x1F, 0x7F, 0x3C) : Microsoft::UI::Colors::Crimson();
            BadgeText().Text(text);
            BadgeText().Foreground(Media::SolidColorBrush{ colour });
            BadgeCircle().Fill(Media::SolidColorBrush{ colour });
            OkIcon().Visibility(good ? Visibility::Visible : Visibility::Collapsed);
            BadIcon().Visibility(good ? Visibility::Collapsed : Visibility::Visible);
        };

        if (!valid)
        {
            std::wstring reason;
            if (overflow)
            {
                reason = L"The entered total is too large to compute reliably.";
            }
            else
            {
                const std::wstring field = !debtor.ok ? L"Debtor" : L"Cash";
                const SumParseResult& bad = !debtor.ok ? debtor : cash;
                reason = bad.tooLarge ? field + L" total is over the maximum allowed."
                                      : field + L" has an invalid entry: \"" + bad.errorTerm + L"\"";
            }
            BookText().Text(L"--");
            DiffText().Text(L"--");
            if (OvBookText() && OvGrandText() && OvDiffText())
            {
                OvBookText().Text(L"--");
                OvGrandText().Text(hstring{ overflow ? std::wstring(L"--") : FormatMoney(entered) });
                OvDiffText().Text(hstring{ L"Cannot check - " + reason });
                OvDiffText().Foreground(Media::SolidColorBrush{ Microsoft::UI::Colors::Crimson() });
            }
            ReasonText().Text(hstring{ reason });
            setBadge(L"Cannot check", false);
            return;
        }

        const double book = debtor.value + cash.value;
        const double diff = book - entered;
        const bool balanced = std::isfinite(diff) && std::abs(diff) < 0.005;
        m_diffOk = balanced;

        BookText().Text(hstring{ FormatMoney(book) });
        DiffText().Text(hstring{ FormatMoney(diff) });
        ReasonText().Text(L"");
        if (OvBookText() && OvGrandText() && OvDiffText())
        {
            OvBookText().Text(hstring{ FormatMoney(book) });
            OvGrandText().Text(hstring{ FormatMoney(entered) });
            OvDiffText().Text(hstring{ FormatMoney(diff) + (balanced ? L"   (OK - balanced)" : L"   (OUT OF BALANCE)") });
            OvDiffText().Foreground(Media::SolidColorBrush{ balanced
                ? Microsoft::UI::ColorHelper::FromArgb(255, 0x1F, 0x7F, 0x3C) : Microsoft::UI::Colors::Crimson() });
        }
        setBadge(balanced ? L"Balanced" : L"Out of balance", balanced);
    }

    // ---- Books (build 5a): same behaviour as the Win32 app ----

    void MainWindow::SetCurrentBook(const std::wstring& name, wchar_t type)
    {
        m_currentBook = name;
        m_currentBookType = name.empty() ? static_cast<wchar_t>(0) : CleanBookType(type);
        if (!m_closed && BookBox())
            BookBox().Text(hstring{ BookBoxText(m_currentBook, m_currentBookType) });
    }

    // "Book3" / "Cash2": the first unused number for that type.
    std::wstring MainWindow::NextBookName(wchar_t type)
    {
        const std::wstring prefix = (type == L'C') ? L"Cash" : L"Book";
        const BooksResult br = ComputeBookSummaries(m_entries);
        for (int n = 1; n < 1000; ++n)
        {
            const std::wstring candidate = prefix + std::to_wstring(n);
            bool used = (candidate == m_currentBook);
            for (const BookSummary& b : br.books)
                if (b.name == candidate)
                    used = true;
            if (!used)
                return candidate;
        }
        return prefix;
    }

    // Start Book...: next Debtor book, next Cash book, an earlier book, or no book.
    void MainWindow::StartBook_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed || !m_finalizedDate.empty())
            return;
        const BooksResult br = ComputeBookSummaries(m_entries);
        auto weak = get_weak();

        auto addItem = [&](Controls::MenuFlyout const& fly, const std::wstring& text, std::wstring name, wchar_t type, bool checked) {
            Controls::ToggleMenuFlyoutItem item;
            item.Text(hstring{ text });
            item.IsChecked(checked);
            item.Click([weak, name, type](auto&&, auto&&) {
                if (auto self = weak.get())
                {
                    self->SetCurrentBook(name, type);
                    self->MarkDirty();
                    self->SetStatus(name.empty() ? std::wstring(L"New entries are not put in a book.")
                                                 : L"New entries go into " + name + L".", false);
                }
            });
            fly.Items().Append(item);
        };

        Controls::MenuFlyout fly;
        addItem(fly, L"Start next Debtor book (" + NextBookName(L'D') + L")", NextBookName(L'D'), L'D', false);
        addItem(fly, L"Start next Cash book (" + NextBookName(L'C') + L")", NextBookName(L'C'), L'C', false);
        if (!br.books.empty())
        {
            fly.Items().Append(Controls::MenuFlyoutSeparator{});
            for (size_t i = 0; i < br.books.size() && i < 40; ++i)
            {
                const BookSummary& b = br.books[i];
                addItem(fly, L"Add to " + b.name + L" (" + std::to_wstring(b.count) + L" rows)", b.name, b.type,
                        b.name == m_currentBook);
            }
        }
        fly.Items().Append(Controls::MenuFlyoutSeparator{});
        addItem(fly, L"No book (new entries are not put in a book)", L"", 0, m_currentBook.empty());
        fly.ShowAt(StartBookButton());
    }

    // Status bar: what the selected rows add up to (the old "select them in Excel and read the sum").
    void MainWindow::EntriesList_SelectionChanged(Windows::Foundation::IInspectable const&,
                                                  Controls::SelectionChangedEventArgs const&)
    {
        if (m_closed)
            return;
        UpdateSelectionStatus();
    }

    void MainWindow::UpdateSelectionStatus()
    {
        if (m_closed || !SelText() || !EntriesList())
            return;
        std::vector<int> picked;
        auto items = EntriesList().Items();
        auto selected = EntriesList().SelectedItems();
        for (uint32_t k = 0; k < selected.Size(); ++k)
        {
            uint32_t row = 0;
            if (items.IndexOf(selected.GetAt(k), row) && row < m_shown.size())
                picked.push_back(static_cast<int>(m_shown[row]));
        }
        std::wstring text;
        if (!picked.empty())
        {
            const SelectionTotals t = ComputeSelectionTotals(m_entries, picked);
            text = L"Selected: " + std::to_wstring(t.count) + (t.count == 1 ? L" row | " : L" rows | ") +
                   FormatKg(t.kgs) + L" kg | " + FormatMoney(t.amount);
        }
        SelText().Text(hstring{ text });
    }

    // Books table on Total Overview (same ComputeBookSummaries / ComputeBookChecks as the Win32 app).
    void MainWindow::RefreshBooks()
    {
        if (m_closed || !BooksList() || !DebtorBox() || !CashBox())
            return;
        BooksList().Items().Clear();
        const BooksResult books = ComputeBookSummaries(m_entries);
        const std::vector<BookCheck> checks =
            ComputeBookChecks(books, ToStd(DebtorBox().Text()), ToStd(CashBox().Text()));
        const std::wstring zero = FormatMoney(0.0);

        auto addRow = [this, &zero](const std::wstring& name, int rows, double kgs, double amount, bool haveFigure,
                                    double figure, double diff, bool bold) {
            const std::wstring figS = haveFigure ? FormatMoney(figure) : std::wstring(L"-");
            const std::wstring diffS = haveFigure ? FormatMoney(diff) : std::wstring(L"-");
            const std::wstring cells[6] = { name, std::to_wstring(rows), FormatKg(kgs), FormatMoney(amount), figS, diffS };
            BooksList().Items().Append(MakeBookRow(cells, bold, haveFigure && diffS != zero));
        };

        double dRows = 0, dKgs = 0, dAmt = 0, cRows = 0, cKgs = 0, cAmt = 0, dFig = 0, cFig = 0;
        bool dAll = true, cAll = true, anyD = false, anyC = false;
        for (const BookCheck& c : checks)
        {
            addRow(c.book.name + (c.book.type == L'C' ? L" (Cash)" : (c.book.type == L'D' ? L" (Debtor)" : L"")),
                   c.book.count, c.book.kgs, c.book.amount, c.haveBookFigure, c.bookFigure, c.difference, false);
            if (c.book.type == L'C')
            {
                anyC = true; cRows += c.book.count; cKgs += c.book.kgs; cAmt += c.book.amount;
                if (c.haveBookFigure) cFig += c.bookFigure; else cAll = false;
            }
            else
            {
                anyD = true; dRows += c.book.count; dKgs += c.book.kgs; dAmt += c.book.amount;
                if (c.haveBookFigure) dFig += c.bookFigure; else dAll = false;
            }
        }
        if (anyD)
            addRow(L"Total - Debtor books", static_cast<int>(dRows), dKgs, dAmt, dAll, dFig, dAmt - dFig, true);
        if (anyC)
            addRow(L"Total - Cash books", static_cast<int>(cRows), cKgs, cAmt, cAll, cFig, cAmt - cFig, true);
        if (books.unassignedCount > 0)
            addRow(L"Not in a book", books.unassignedCount, books.unassignedKgs, books.unassignedAmount, false, 0, 0, false);
    }
    // Breakdown tab: same BuildBreakdownData grouping the Win32 list, the print report and the CSV use.
    // m_bkRefs / m_bkItemRef remember what each row adds up, for the double-click drill-down.
    void MainWindow::RefreshBreakdown()
    {
        if (m_closed || !BreakdownList())
            return;
        BreakdownList().Items().Clear();
        m_bkRefs.clear();
        m_bkItemRef.clear();

        const std::vector<SupplierGroup> data = BuildBreakdownData(m_entries);
        const std::vector<BreakdownRowRef> refs = BuildBreakdownRowRefs(data);
        size_t next = 0; // refs are in exactly the order the rows are added below
        auto addRef = [&]() {
            m_bkItemRef.push_back(static_cast<int>(m_bkRefs.size()));
            m_bkRefs.push_back(refs.at(next++));
        };

        for (const SupplierGroup& sg : data)
        {
            const std::wstring head[4] = { sg.supplier, L"", L"", L"" };
            BreakdownList().Items().Append(MakeBreakdownRow(head, 0));
            m_bkItemRef.push_back(-1);
            for (const ProductGroup& pg : sg.products)
            {
                for (const PriceLine& pl : pg.prices)
                {
                    const std::wstring cells[4] = { pg.species, FormatMoney(pl.price), FormatKg(pl.kgs), FormatMoney(pl.amt) };
                    BreakdownList().Items().Append(MakeBreakdownRow(cells, 1));
                    addRef();
                }
                const std::wstring tot[4] = { pg.species + L" Total", L"", FormatKg(pg.totalKgs), FormatMoney(pg.totalAmt) };
                BreakdownList().Items().Append(MakeBreakdownRow(tot, 2));
                addRef();
            }
            const std::wstring stot[4] = { sg.supplier + L" Total", L"", FormatKg(sg.totalKgs), FormatMoney(sg.totalAmt) };
            BreakdownList().Items().Append(MakeBreakdownRow(stot, 3));
            addRef();
        }
    }

    void MainWindow::BreakdownList_DoubleTapped(Windows::Foundation::IInspectable const&,
                                                Input::DoubleTappedRoutedEventArgs const&)
    {
        if (m_closed)
            return;
        const int item = BreakdownList().SelectedIndex();
        if (item < 0 || static_cast<size_t>(item) >= m_bkItemRef.size() || m_bkItemRef[static_cast<size_t>(item)] < 0)
            return; // nothing selected, or a supplier heading
        ShowDrillDown(m_bkRefs.at(static_cast<size_t>(m_bkItemRef[static_cast<size_t>(item)])));
    }

    // Read-only list of the entries a Breakdown row adds up (same core ComputeDrillDown as the Win32
    // popup). Click a column header to sort (again to flip); Close / Esc closes it.
    winrt::fire_and_forget MainWindow::ShowDrillDown(BreakdownRowRef ref)
    {
        auto strong = get_strong();

        struct State
        {
            std::vector<Entry> entries; // snapshot: the dialog is modal, nothing edits the sheet meanwhile
            BreakdownRowRef ref;
            DrillSortKey key = DrillSortKey::Default;
            bool asc = true;
            Controls::ListView list{ nullptr };
            Controls::TextBlock total{ nullptr };
            std::vector<Controls::TextBlock> heads;
            std::function<void()> fill;
        };
        auto st = std::make_shared<State>();
        st->entries = m_entries;
        st->ref = ref;

        const wchar_t* const names[6] = { L"Supplier", L"Species", L"Date", L"Kgs", L"Price ($/kg)", L"Total ($)" };
        const DrillSortKey keys[6] = { DrillSortKey::Supplier, DrillSortKey::Species, DrillSortKey::Date,
                                       DrillSortKey::Kgs, DrillSortKey::Price, DrillSortKey::Total };
        const double widths[6] = { 2.0, 2.0, 1.6, 1.2, 1.4, 1.6 };

        auto makeRow = [&widths](const std::wstring (&cells)[6]) {
            Controls::Grid g;
            for (int i = 0; i < 6; ++i)
            {
                Controls::ColumnDefinition cd;
                cd.Width(GridLength{ widths[i], GridUnitType::Star });
                g.ColumnDefinitions().Append(cd);

                Controls::TextBlock tb;
                tb.Text(hstring{ cells[i] });
                tb.VerticalAlignment(VerticalAlignment::Center);
                tb.HorizontalAlignment(i >= 3 ? HorizontalAlignment::Right : HorizontalAlignment::Left);
                tb.TextTrimming(TextTrimming::CharacterEllipsis);

                Controls::Border cell;
                cell.Padding(ThicknessHelper::FromLengths(10, 0, 10, 0));
                cell.BorderThickness(ThicknessHelper::FromLengths(0, 0, i < 5 ? 1 : 0, 1));
                cell.BorderBrush(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 0xE3, 0xE8, 0xEF) });
                cell.Child(tb);
                Controls::Grid::SetColumn(cell, i);
                g.Children().Append(cell);
            }
            return g;
        };

        // Header: click a cell to sort by it.
        Controls::Grid head;
        head.Background(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 0xF0, 0xF3, 0xF8) });
        for (int i = 0; i < 6; ++i)
        {
            Controls::ColumnDefinition cd;
            cd.Width(GridLength{ widths[i], GridUnitType::Star });
            head.ColumnDefinitions().Append(cd);

            Controls::TextBlock tb;
            tb.Text(names[i]);
            tb.HorizontalAlignment(i >= 3 ? HorizontalAlignment::Right : HorizontalAlignment::Left);
            st->heads.push_back(tb);

            Controls::Border cell;
            cell.Background(Media::SolidColorBrush{ Microsoft::UI::Colors::Transparent() }); // so the whole cell takes the click
            cell.Padding(ThicknessHelper::FromLengths(10, 3, 10, 3));
            cell.BorderThickness(ThicknessHelper::FromLengths(0, 0, i < 5 ? 1 : 0, 1));
            cell.BorderBrush(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 0xE3, 0xE8, 0xEF) });
            cell.Child(tb);
            const DrillSortKey key = keys[i];
            cell.Tapped([st, key](auto&&, auto&&) {
                if (st->key == key)
                    st->asc = !st->asc;
                else
                {
                    st->key = key;
                    st->asc = true;
                }
                st->fill();
            });
            Controls::Grid::SetColumn(cell, i);
            head.Children().Append(cell);
        }

        st->list = Controls::ListView();
        st->list.SelectionMode(Controls::ListViewSelectionMode::None);
        st->list.BorderThickness(ThicknessHelper::FromUniformLength(1));
        st->list.BorderBrush(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 0xE3, 0xE8, 0xEF) });
        {
            st->list.ItemContainerStyle(Content().as<FrameworkElement>().Resources().Lookup(box_value(L"TightListItemStyle")).as<Style>());
        }
        st->total = Controls::TextBlock();
        st->total.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        st->total.TextWrapping(TextWrapping::Wrap);

        // The list is as tall as its rows (24 px each), up to what fits in the window, so short lists have no
        // empty gap and long ones scroll. 240 = title, header, total line, Close button and dialog padding.
        const double maxListHeight = (std::max)(150.0, RootGrid().ActualHeight() - 240.0);
        st->fill = [st, makeRow, names, keys, maxListHeight]() {
            DrillDownResult res = ComputeDrillDown(st->entries, st->ref);
            SortDrillDownIndices(st->entries, res.indices, st->key, st->asc);
            st->list.Items().Clear();
            for (size_t idx : res.indices)
            {
                const Entry& e = st->entries[idx];
                const std::wstring cells[6] = { e.supplier, e.product, e.date, FormatKg(e.kgs), FormatMoney(e.price),
                                                FormatMoney(e.Total()) };
                st->list.Items().Append(makeRow(cells));
            }
            st->list.Height((std::min)(maxListHeight, static_cast<double>(res.indices.size()) * 24.0 + 6.0));
            for (int i = 0; i < 6; ++i)
            {
                std::wstring t = names[i];
                if (st->key == keys[i])
                    t += st->asc ? L"  \u25B2" : L"  \u25BC";
                st->heads[static_cast<size_t>(i)].Text(hstring{ t });
            }
            // Same wording as the Win32 popup, so the figures can be compared with the Breakdown row by eye.
            st->total.Text(hstring{ L"Total:  " + std::to_wstring(res.indices.size()) +
                                    (res.indices.size() == 1 ? L" entry" : L" entries") + L"   |   " +
                                    FormatKg(res.kgs) + L" kg   |   " + FormatMoney(res.amt) });
        };
        st->fill();

        Controls::StackPanel panel;
        panel.Spacing(6);
        panel.MinWidth(760);
        panel.Children().Append(head);
        panel.Children().Append(st->list);
        panel.Children().Append(st->total);

        std::wstring title = ref.supplier;
        if (ref.kind != BreakdownRowKind::SupplierTotal)
            title += L" - " + ref.species;
        if (ref.kind == BreakdownRowKind::Price)
            title += L" at " + FormatMoney(ref.price) + L"/kg";

        Controls::ContentDialog dlg;
        dlg.XamlRoot(RootGrid().XamlRoot());
        dlg.Title(box_value(hstring{ title }));
        dlg.Content(panel);
        dlg.CloseButtonText(L"Close");
        dlg.DefaultButton(Controls::ContentDialogButton::Close);
        dlg.Resources().Insert(box_value(L"ContentDialogMaxWidth"), box_value(1000.0)); // default 548 is too narrow for six columns
        co_await dlg.ShowAsync();
        st->fill = nullptr; // break the State <-> lambda reference cycle
    }
    // By Species tab: same ComputeSpeciesStats as the Win32 list. Always uses ALL entries, not the filtered view.
    void MainWindow::RefreshBySpecies()
    {
        if (m_closed || !TabPage3())
            return;
        TabPage3().Items().Clear();
        const SpeciesStatsResult stats = ComputeSpeciesStats(m_entries);
        for (const SpeciesStat& row : stats.rows)
        {
            const std::wstring cells[6] = { row.species, FormatKg(row.kgs), FormatMoney(row.amt),
                                            FormatMoney(row.avgPrice), FormatMoney(row.maxPrice), FormatMoney(row.minPrice) };
            TabPage3().Items().Append(MakeSpeciesRow(cells, false));
        }
        const std::wstring grand[6] = { L"GRAND TOTAL", FormatKg(stats.grandKgs), FormatMoney(stats.grandAmt), L"", L"", L"" };
        TabPage3().Items().Append(MakeSpeciesRow(grand, true));
    }
    namespace
    {
        // Same picker as the Win32 app's PickFileInFolderModern: the classic dialog ignores the start folder after its first
        // use, IFileOpenDialog::SetFolder always opens where it is told. 1 = picked, 0 = cancelled, -1 = unusable.
        int PickBackupFile(HWND owner, const std::wstring& folder, std::wstring& outPath)
        {
            const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            const bool uninit = SUCCEEDED(hrInit); // already initialised another way: just use it
            int result = -1;
            IFileOpenDialog* dlg = nullptr;
            if (SUCCEEDED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))) && dlg)
            {
                COMDLG_FILTERSPEC filters[] = {
                    { L"Fish Balance Backups (*.fbd)", L"*.fbd" },
                    { L"All Files (*.*)", L"*.*" },
                };
                DWORD opts = 0;
                dlg->GetOptions(&opts);
                dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST);
                dlg->SetFileTypes(2, filters);
                dlg->SetFileTypeIndex(1);
                dlg->SetDefaultExtension(L"fbd");
                IShellItem* folderItem = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&folderItem))) && folderItem)
                {
                    dlg->SetFolder(folderItem);
                    folderItem->Release();
                }
                const HRESULT hr = dlg->Show(owner);
                if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
                    result = 0;
                else if (SUCCEEDED(hr))
                {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(dlg->GetResult(&item)) && item)
                    {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                        {
                            outPath = path;
                            CoTaskMemFree(path);
                            result = 1;
                        }
                        item->Release();
                    }
                }
                dlg->Release();
            }
            if (uninit)
                CoUninitialize();
            return result;
        }
    }

    // A safety copy of the sheet that is about to be replaced (best effort, silent): same folder and file-name pattern as the
    // rolling backups, label "before-restore". Nothing to copy when the sheet has no entries.
    void MainWindow::SnapshotBeforeReplace()
    {
        if (m_entries.empty())
            return;
        try
        {
            const std::wstring keepD = m_baseDebtor, keepC = m_baseCash; // GatherSaveState resets the "clean" baseline; put it back
            FbdSaveState s = GatherSaveState(m_finalizedDate);
            m_baseDebtor = keepD;
            m_baseCash = keepC;
            s.includeSourceFile = true;
            s.sourceFile = m_currentFile;
            const std::string content = WToUtf8(BuildFbdText(s));
            const std::wstring bdir = autosave_shared::DataDir() + L"\\backups";
            CreateDirectoryW(bdir.c_str(), nullptr);
            SYSTEMTIME st;
            GetLocalTime(&st);
            std::wstring bpath;
            for (int counter = 0; counter < 1000; ++counter)
            {
                bpath = bdir + L"\\" + BuildBackupFileName(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                                                          st.wMilliseconds, L"before-restore", counter);
                if (GetFileAttributesW(bpath.c_str()) == INVALID_FILE_ATTRIBUTES)
                    break;
            }
            std::wstring ignored;
            WriteFileAtomicUtf8(bpath, content, &ignored);
        }
        catch (...)
        {
        }
    }

    // File > Restore from Backup: pick one of the rolling backups (the picker opens in the backups folder) and load it as an
    // UNSAVED sheet (no file name), like the Win32 app - review it, then Save As to keep it. The sheet it replaces is first
    // copied into backups\ as "before-restore", so a wrong pick can be undone by restoring that one.
    void MainWindow::RestoreBackup_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed)
            return;
        const std::wstring dir = autosave_shared::DataDir() + L"\\backups";
        const DWORD attrs = GetFileAttributesW(dir.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
        {
            MessageBoxW(Hwnd(),
                        L"No backups folder exists yet. Rolling backups are taken automatically as you work (every few minutes "
                        L"when something changed) - there just isn't one yet.",
                        L"No Backups Yet", MB_OK | MB_ICONINFORMATION);
            return;
        }
        std::wstring file;
        const int r = PickBackupFile(Hwnd(), dir, file);
        if (r == 0)
            return; // cancelled
        if (r != 1 || file.empty())
        {
            MessageBoxW(Hwnd(), (L"The file picker could not be opened.\n\nThe backups are in:\n" + dir + L"\n\nYou can also use File > Open on one of those files.").c_str(),
                        L"Restore from Backup", MB_OK | MB_ICONWARNING);
            return;
        }
        if (!ConfirmDiscard(L"load this backup (nothing on disk is touched unless you Save afterwards)"))
            return;
        LoadFbdFileResult res = LoadFbdFileContent(file);
        if (!res.ok)
        {
            MessageBoxW(Hwnd(), (L"Could not load that backup: " + res.errorMessage + L".\n\nNothing has been changed.").c_str(),
                        L"Restore from Backup", MB_OK | MB_ICONERROR);
            return;
        }
        SnapshotBeforeReplace(); // only after the backup is known to load, so a bad pick leaves no extra file
        ApplyParsedFile(res.parsed, L"", true, L"Backup loaded - review it, then File > Save As to keep it.");
    }

    // ---- v0.23.2: Edit > Undo Delete, Recent Files, window position, Export to CSV, About ----------------------------

    void MainWindow::ClearUndoDelete()
    {
        m_hasUndo = false;
        m_undoIndex = -1;
        if (!m_closed)
            UndoDeleteItem().IsEnabled(false);
    }

    // Same as the Win32 app's UndoDelete(): puts the last deleted row back where it was.
    void MainWindow::UndoDelete_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed || !m_hasUndo)
            return;
        if (!m_finalizedDate.empty())
        {
            SetStatus(L"This day is finalized - un-finalize it first.", true);
            return;
        }
        int idx = m_undoIndex;
        if (idx < 0)
            idx = 0;
        if (idx > static_cast<int>(m_entries.size()))
            idx = static_cast<int>(m_entries.size());
        EndEditMode();
        const Entry restored = m_undoEntry;
        m_entries.insert(m_entries.begin() + idx, restored);
        ReevaluateOutlierFlagsForSpecies(m_entries, restored.product); // it can change whether its siblings still look normal
        ClearUndoDelete();
        MarkDirty();
        SetStatus(L"Entry restored.", false);
        RefreshNameLists();
        RefreshList();
        Recalculate();
    }

    namespace
    {
        const size_t kRecentMax = 8;
        std::wstring RecentFilesFile() { return autosave_shared::DataDir() + L"\\recent.txt"; }
    }

    // recent.txt lives in the shared data folder: the Win32 app and this one share one Recent Files list.
    void MainWindow::LoadRecentFiles()
    {
        m_recent.clear();
        std::vector<std::wstring> lines;
        ReadAllLines(RecentFilesFile(), lines);
        for (const auto& l : lines)
            if (!l.empty() && m_recent.size() < kRecentMax)
                m_recent.push_back(l);
    }

    void MainWindow::RememberRecentFile(const std::wstring& path)
    {
        if (path.empty())
            return;
        m_recent.erase(std::remove(m_recent.begin(), m_recent.end(), path), m_recent.end());
        m_recent.insert(m_recent.begin(), path);
        if (m_recent.size() > kRecentMax)
            m_recent.resize(kRecentMax);
        std::string content;
        for (const auto& p : m_recent)
            content += WToUtf8(p) + "\n";
        WriteFileAtomicUtf8(RecentFilesFile(), content); // best effort: losing this only loses the menu
        RebuildRecentMenu();
    }

    void MainWindow::RebuildRecentMenu()
    {
        if (m_closed)
            return;
        auto items = RecentMenu().Items();
        items.Clear();
        if (m_recent.empty())
        {
            Controls::MenuFlyoutItem none;
            none.Text(L"(none yet)");
            none.IsEnabled(false);
            items.Append(none);
            return;
        }
        auto weak = get_weak();
        for (const std::wstring& path : m_recent)
        {
            const size_t slash = path.find_last_of(L"\\/");
            const std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
            Controls::MenuFlyoutItem item;
            item.Text(hstring{ name });
            Controls::ToolTipService::SetToolTip(item, box_value(hstring{ path }));
            item.Click([weak, path](Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                if (auto self = weak.get())
                    self->OpenRecent(path);
            });
            items.Append(item);
        }
    }

    void MainWindow::OpenRecent(const std::wstring& path)
    {
        if (m_closed)
            return;
        if (!ConfirmDiscard(L"open another file"))
            return;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            MessageBoxW(Hwnd(), (L"That file no longer exists:\n\n" + path + L"\n\nIt will be removed from the list.").c_str(),
                        L"Recent Files", MB_OK | MB_ICONWARNING);
            m_recent.erase(std::remove(m_recent.begin(), m_recent.end(), path), m_recent.end());
            std::string content;
            for (const auto& p : m_recent)
                content += WToUtf8(p) + "\n";
            WriteFileAtomicUtf8(RecentFilesFile(), content);
            RebuildRecentMenu();
            return;
        }
        OpenNamedFile(path);
    }

    // The window's position and size are kept in settings.txt under their own WUI* keys, so this app and the Win32 app
    // (which uses X/Y/W/H) never move each other's window. Nothing saved, or a saved spot that is no longer on any
    // screen (a monitor was unplugged): start maximised, as before.
    void MainWindow::RestoreWindowPlacement()
    {
        auto appWindow = this->AppWindow();
        auto maximise = [&appWindow]() {
            if (auto presenter = appWindow.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>())
                presenter.Maximize();
        };
        const std::wstring dir = autosave_shared::DataDir();
        const std::wstring sx = autosave_shared::ReadSetting(dir, L"WUIX");
        const std::wstring sy = autosave_shared::ReadSetting(dir, L"WUIY");
        const std::wstring sw = autosave_shared::ReadSetting(dir, L"WUIW");
        const std::wstring sh = autosave_shared::ReadSetting(dir, L"WUIH");
        const std::wstring sm = autosave_shared::ReadSetting(dir, L"WUIMAX");
        const HWND hwnd = Hwnd();
        if (!hwnd || sx.empty() || sy.empty() || sw.empty() || sh.empty())
        {
            maximise();
            return;
        }
        RECT rc{};
        rc.left = _wtoi(sx.c_str());
        rc.top = _wtoi(sy.c_str());
        rc.right = rc.left + _wtoi(sw.c_str());
        rc.bottom = rc.top + _wtoi(sh.c_str());
        const POINT titleMiddle{ (rc.left + rc.right) / 2, rc.top + 20 };
        if (rc.right - rc.left < 400 || rc.bottom - rc.top < 300 || !MonitorFromPoint(titleMiddle, MONITOR_DEFAULTTONULL))
        {
            maximise();
            return;
        }
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        if (!GetWindowPlacement(hwnd, &wp))
        {
            maximise();
            return;
        }
        wp.rcNormalPosition = rc;
        wp.showCmd = (sm == L"1") ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        if (!SetWindowPlacement(hwnd, &wp))
            maximise();
    }

    void MainWindow::SaveWindowPlacement()
    {
        const HWND hwnd = Hwnd();
        if (!hwnd)
            return;
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        if (!GetWindowPlacement(hwnd, &wp))
            return;
        const std::wstring dir = autosave_shared::DataDir();
        const RECT rc = wp.rcNormalPosition; // the un-maximised rectangle, so "maximised" remembers where it restores to
        autosave_shared::WriteSetting(dir, L"WUIX", std::to_wstring(rc.left));
        autosave_shared::WriteSetting(dir, L"WUIY", std::to_wstring(rc.top));
        autosave_shared::WriteSetting(dir, L"WUIW", std::to_wstring(rc.right - rc.left));
        autosave_shared::WriteSetting(dir, L"WUIH", std::to_wstring(rc.bottom - rc.top));
        autosave_shared::WriteSetting(dir, L"WUIMAX", wp.showCmd == SW_SHOWMAXIMIZED ? L"1" : L"0");
    }

    // File > Export to CSV: the same file the Win32 app writes (entries plus every report), opens directly in Excel.
    void MainWindow::ExportCsv_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed)
            return;
        wchar_t file[MAX_PATH] = L"";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = Hwnd();
        ofn.lpstrFilter = L"CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"csv";
        ofn.Flags = OFN_OVERWRITEPROMPT;
        if (!GetSaveFileNameW(&ofn))
            return;
        std::wstring err;
        if (ExportToCsv(file, m_entries, &err))
            SetStatus(L"Exported - the file opens directly in Excel.", false);
        else
            MessageBoxW(Hwnd(), (L"Could not export the file: " + err).c_str(), L"Export to CSV", MB_OK | MB_ICONERROR);
    }

    void MainWindow::About_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_closed)
            ShowAbout();
    }

    winrt::fire_and_forget MainWindow::ShowAbout()
    {
        auto strong = get_strong();
        Controls::TextBlock text;
        text.TextWrapping(TextWrapping::Wrap);
        text.Text(
            L"A native replacement for the BALANCE_PivotTable spreadsheet.\n\n"
            L"1. Enter each delivery on the Data Entry tab and fill in the Debtor / Cash figures from the books to check they balance.\n"
            L"2. See a $ total per supplier on the Total Overview tab.\n"
            L"3. See supplier > species > price with weights on the Breakdown tab (double-click a row to list the entries behind it).\n"
            L"4. See Kgs / $ totals per species, with average, highest and lowest price, on the By Species tab.\n\n"
            L"To fix a mistake, double-click a row (or select it and click Edit Selected Row), correct it and click Update Entry. "
            L"Deleting a row asks first, and Edit > Undo Delete brings back the last one.\n\n"
            L"An unusually high or low price gets a warning marker automatically; double-click the row to review it, or right-click to clear the flag.\n\n"
            L"Use File > Print Breakdown... (or Print Preview...) to print; choose Microsoft Print to PDF for a PDF. File > Export to CSV... writes everything for Excel.\n\n"
            L"Your work is saved automatically in C:\\BalancingAct\\data, and a rolling set of backups is kept there too. "
            L"Use File > Save As... to keep a named copy; File > Recent Files lists your last few. "
            L"Once Debtor + Cash balance exactly, Finalize Day locks the sheet.\n\n"
            L"Tools > Manage Supplier / Species Names tidies the name lists and holds supplier emails; Tools > Settings has email, backup, text size and printer options.");
        Controls::ScrollViewer scroll;
        scroll.MaxHeight((std::max)(280.0, (std::min)(560.0, RootGrid().ActualHeight() - 260.0)));
        scroll.Content(text);
        Controls::ContentDialog dlg;
        dlg.XamlRoot(RootGrid().XamlRoot());
        dlg.Title(box_value(hstring{ L"Fish Balance Manager (WinUI) v" APP_VERSION }));
        dlg.Content(scroll);
        dlg.CloseButtonText(L"Close");
        dlg.DefaultButton(Controls::ContentDialogButton::Close);
        dlg.Resources().Insert(box_value(L"ContentDialogMaxWidth"), box_value(680.0));
        co_await dlg.ShowAsync();
    }

    // File > Print Breakdown / the Print buttons: WinUI's own Print dialog (the Windows dialog cannot be told which
    // printer to start on, and the classic one looks dated).
    void MainWindow::Print_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_closed)
            return;
        ShowPrintDialog();
    }

    // Printer (your saved one pre-selected, else the Windows default), copies, paper and orientation, then
    // PrintBreakdownJobDirect prints the same report as the Win32 app straight to that printer. "Microsoft Print to PDF"
    // is in the list for a PDF. Paper / orientation start from Tools > Settings > Display & printing.
    winrt::fire_and_forget MainWindow::ShowPrintDialog()
    {
        auto strong = get_strong();
        if (m_closed)
            co_return;
        const std::vector<std::wstring> printers = ListInstalledPrinters();
        if (printers.empty())
        {
            // No printers at all: let Windows explain.
            PrintBreakdownJob(Hwnd(), m_entries, ToStd(DebtorBox().Text()), ToStd(CashBox().Text()), PrintDefaultsFromSettings(g_user));
            co_return;
        }
        std::wstring start = g_user.printer;
        if (std::find(printers.begin(), printers.end(), start) == printers.end())
        {
            wchar_t def[256];
            DWORD sz = 256;
            start = GetDefaultPrinterW(def, &sz) ? std::wstring(def) : printers[0];
        }
        auto label = [](const wchar_t* text, double top) {
            Controls::TextBlock t;
            t.Text(text);
            t.Margin(Thickness{ 0, top, 0, 4 });
            return t;
        };
        Controls::ComboBox cbPrinter;
        cbPrinter.HorizontalAlignment(HorizontalAlignment::Stretch);
        int sel = 0;
        for (size_t i = 0; i < printers.size(); ++i)
        {
            cbPrinter.Items().Append(box_value(hstring{ printers[i] }));
            if (printers[i] == start)
                sel = static_cast<int>(i);
        }
        cbPrinter.SelectedIndex(sel);
        Controls::NumberBox nbCopies;
        nbCopies.Minimum(1);
        nbCopies.Maximum(99);
        nbCopies.Value(1);
        nbCopies.SpinButtonPlacementMode(Controls::NumberBoxSpinButtonPlacementMode::Inline);
        nbCopies.Width(140);
        nbCopies.HorizontalAlignment(HorizontalAlignment::Left);
        Controls::ComboBox cbPaper;
        for (const wchar_t* t : { L"Printer default", L"A4", L"A3" })
            cbPaper.Items().Append(box_value(hstring{ t }));
        cbPaper.SelectedIndex(ClampPrintPaper(g_user.printPaper));
        cbPaper.Width(200);
        Controls::ComboBox cbOrient;
        for (const wchar_t* t : { L"Printer default", L"Portrait", L"Landscape" })
            cbOrient.Items().Append(box_value(hstring{ t }));
        cbOrient.SelectedIndex(ClampPrintOrientation(g_user.printOrientation));
        cbOrient.Width(200);

        Controls::StackPanel panel;
        panel.MinWidth(420);
        panel.Children().Append(label(L"Printer", 0));
        panel.Children().Append(cbPrinter);
        panel.Children().Append(label(L"Copies", 14));
        panel.Children().Append(nbCopies);
        panel.Children().Append(label(L"Paper", 14));
        panel.Children().Append(cbPaper);
        panel.Children().Append(label(L"Orientation", 14));
        panel.Children().Append(cbOrient);

        Controls::ContentDialog dlg;
        dlg.XamlRoot(RootGrid().XamlRoot());
        dlg.Title(box_value(L"Print - Breakdown Report"));
        dlg.Content(panel);
        dlg.PrimaryButtonText(L"Print");
        dlg.CloseButtonText(L"Cancel");
        dlg.DefaultButton(Controls::ContentDialogButton::Primary);
        const Controls::ContentDialogResult result = co_await dlg.ShowAsync();
        if (m_closed || result != Controls::ContentDialogResult::Primary)
            co_return;

        const int pi = cbPrinter.SelectedIndex();
        if (pi < 0 || static_cast<size_t>(pi) >= printers.size())
            co_return;
        PrintChoice choice;
        choice.printer = printers[static_cast<size_t>(pi)];
        choice.paper = ClampPrintPaper(cbPaper.SelectedIndex());
        choice.orientation = ClampPrintOrientation(cbOrient.SelectedIndex());
        const double cv = nbCopies.Value();
        choice.copies = std::isnan(cv) ? 1 : (std::max)(1, (std::min)(99, static_cast<int>(cv + 0.5)));
        PrintBreakdownJobDirect(Hwnd(), m_entries, ToStd(DebtorBox().Text()), ToStd(CashBox().Text()), choice);
        SetStatus(L"Sent to " + choice.printer + L".", false);
    }

    void MainWindow::PrintPreview_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_closed)
            ShowPrintPreview();
    }

    // Print preview: the report pages are drawn by the same RenderReportPages the Win32 app uses (at the capped
    // preview resolution), then shown one at a time in a dialog. "Print..." closes the dialog and runs the job.
    winrt::fire_and_forget MainWindow::ShowPrintPreview()
    {
        auto strong = get_strong();

        PreviewPageSpec spec;
        std::wstring error;
        if (!GetPreviewPageSpec(spec, &error, PrintDefaultsFromSettings(g_user)))
        {
            MessageBoxW(Hwnd(), error.c_str(), L"Print Preview", MB_OK | MB_ICONERROR);
            co_return;
        }
        RenderReportResult result = RenderReportPages(m_entries, spec.widthPx, spec.heightPx, spec.dpiX, spec.dpiY,
                                                      ToStd(DebtorBox().Text()), ToStd(CashBox().Text()));
        if (!result.ok)
        {
            FreeRenderedPages(result.pages);
            MessageBoxW(Hwnd(), result.error.c_str(), L"Print Preview", MB_OK | MB_ICONERROR);
            co_return;
        }
        if (result.pages.empty())
        {
            MessageBoxW(Hwnd(), L"Nothing to preview yet.", L"Print Preview", MB_OK | MB_ICONINFORMATION);
            co_return;
        }

        struct PreviewState
        {
            std::vector<Media::Imaging::WriteableBitmap> pages;
            size_t index = 0;
            Controls::Image image{ nullptr };
            Controls::TextBlock label{ nullptr };
            Controls::Button prev{ nullptr };
            Controls::Button next{ nullptr };
            std::function<void()> show;
        };
        auto st = std::make_shared<PreviewState>();
        for (const RenderedPage& p : result.pages)
        {
            Media::Imaging::WriteableBitmap bmp{ nullptr };
            if (!PageToBitmap(p, bmp, error))
            {
                FreeRenderedPages(result.pages);
                MessageBoxW(Hwnd(), error.c_str(), L"Print Preview", MB_OK | MB_ICONERROR);
                co_return;
            }
            st->pages.push_back(bmp);
        }
        const bool pageCapHit = result.pageCapHit;
        FreeRenderedPages(result.pages); // the bitmaps above hold their own copy

        // Keep the page as tall as fits in the window (the dialog adds a title, the page buttons and its own buttons).
        const double imageHeight = (std::max)(300.0, RootGrid().ActualHeight() - 270.0);
        st->image = Controls::Image();
        st->image.Stretch(Media::Stretch::Uniform);
        st->image.Height(imageHeight);

        st->prev = Controls::Button();
        st->prev.Content(box_value(L"< Previous"));
        st->next = Controls::Button();
        st->next.Content(box_value(L"Next >"));
        st->label = Controls::TextBlock();
        st->label.VerticalAlignment(VerticalAlignment::Center);

        st->show = [st]() {
            st->image.Source(st->pages.at(st->index));
            st->label.Text(hstring{ L"Page " + std::to_wstring(st->index + 1) + L" of " + std::to_wstring(st->pages.size()) });
            st->prev.IsEnabled(st->index > 0);
            st->next.IsEnabled(st->index + 1 < st->pages.size());
        };
        st->prev.Click([st](auto&&, auto&&) {
            if (st->index > 0)
                --st->index;
            st->show();
        });
        st->next.Click([st](auto&&, auto&&) {
            if (st->index + 1 < st->pages.size())
                ++st->index;
            st->show();
        });
        st->show();

        Controls::StackPanel nav;
        nav.Orientation(Controls::Orientation::Horizontal);
        nav.Spacing(12);
        nav.HorizontalAlignment(HorizontalAlignment::Center);
        nav.Children().Append(st->prev);
        nav.Children().Append(st->label);
        nav.Children().Append(st->next);

        Controls::StackPanel panel;
        panel.Spacing(8);
        if (pageCapHit)
        {
            Controls::TextBlock warn;
            warn.Text(hstring{ L"This report has more than " + std::to_wstring(kReportMaxPages) + L" pages. Only the first " +
                               std::to_wstring(kReportMaxPages) + L" are shown here and will print." });
            warn.TextWrapping(TextWrapping::Wrap);
            warn.Foreground(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 200, 0, 0) });
            panel.Children().Append(warn);
        }
        panel.Children().Append(st->image);
        panel.Children().Append(nav);

        Controls::ContentDialog dlg;
        dlg.XamlRoot(RootGrid().XamlRoot());
        dlg.Title(box_value(L"Print Preview - Breakdown Report"));
        dlg.Content(panel);
        dlg.PrimaryButtonText(L"Print...");
        dlg.CloseButtonText(L"Close");
        dlg.DefaultButton(Controls::ContentDialogButton::Close);
        const Controls::ContentDialogResult dr = co_await dlg.ShowAsync();
        st->show = nullptr; // break the State <-> lambda reference cycle
        if (m_closed || dr != Controls::ContentDialogResult::Primary)
            co_return;
        ShowPrintDialog();
    }
    // Supplier -> email address view. Since v0.15.0 the addresses are stored on the supplier lines of names.txt
    // (S|name|date|email); this just refills the in-memory map from the loaded master list.
    void MainWindow::LoadSupplierEmails()
    {
        m_supplierEmails.clear();
        for (const auto& kv : m_names.suppliers.emails)
            if (!kv.second.empty())
                m_supplierEmails[kv.first] = kv.second;
    }

    void MainWindow::EmailAll_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_closed)
            DoEmailSuppliers();
    }

    // v0.24.0: Email Suppliers first shows a dialog with optional notes for each supplier (NOT SOLD / UP / DOWN, free text).
    // Skip - no notes goes straight to the emails as before. Nothing is remembered between runs: Jack emails once per sheet,
    // so the app warns before a second set for the same sheet (supplier prices must never appear to change).
    winrt::fire_and_forget MainWindow::DoEmailSuppliers()
    {
        auto strong = get_strong();
        if (m_closed)
            co_return;
        const std::vector<SupplierGroup> data = BuildBreakdownData(m_entries);
        if (data.empty())
        {
            MessageBoxW(Hwnd(), L"There are no entries to email yet.", L"Email Suppliers", MB_OK | MB_ICONINFORMATION);
            co_return;
        }
        if (m_emailedThisSheet)
        {
            if (MessageBoxW(Hwnd(),
                    L"You have already created supplier emails for this sheet.\n\n"
                    L"Sending a second set can look to suppliers as if prices have changed. Only continue if you really mean to "
                    L"send them again.\n\nCreate them again?",
                    L"Email Suppliers - already emailed", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                co_return;
        }

        struct NotesState
        {
            std::map<std::wstring, EmailNotes> notes;
            int current = -1;
            bool loading = false;
        };
        auto ns = std::make_shared<NotesState>();

        auto makeBox = [](const wchar_t* header, double minHeight) {
            Controls::TextBox tb;
            tb.Header(box_value(hstring{ header }));
            tb.AcceptsReturn(true);
            tb.TextWrapping(TextWrapping::Wrap);
            tb.MinHeight(minHeight);
            tb.MaxLength(4000);
            return tb;
        };
        Controls::TextBox tbNotSold = makeBox(L"NOT SOLD", 96);
        Controls::TextBox tbUp = makeBox(L"UP  (arrived over the manifest)", 80);
        Controls::TextBox tbDown = makeBox(L"DOWN  (short of the manifest)", 80);
        Controls::TextBlock title;
        title.FontSize(16);
        title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        title.TextWrapping(TextWrapping::Wrap);
        Controls::TextBlock hint;
        hint.Text(L"Type it exactly as the supplier should read it: bins x weight (17X25 GREN) or kg (21.4 kg GUMMY). "
                  L"The headings are added for you; a box left empty adds nothing.");
        hint.TextWrapping(TextWrapping::Wrap);
        hint.Opacity(0.75);
        Controls::Button nextBtn;
        nextBtn.Content(box_value(L"Next supplier"));
        nextBtn.HorizontalAlignment(HorizontalAlignment::Left);

        Controls::ListView list;
        list.Width(250);
        list.SelectionMode(Controls::ListViewSelectionMode::Single);
        std::vector<Controls::TextBlock> marks;
        for (const SupplierGroup& sg : data)
        {
            Controls::Grid row;
            Controls::ColumnDefinition c0, c1;
            c0.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            c1.Width(GridLengthHelper::Auto());
            row.ColumnDefinitions().Append(c0);
            row.ColumnDefinitions().Append(c1);
            Controls::TextBlock name;
            name.Text(hstring{ sg.supplier });
            name.TextTrimming(TextTrimming::CharacterEllipsis);
            Controls::TextBlock mark;
            mark.Opacity(0.8);
            mark.Margin(Thickness{ 8, 0, 0, 0 });
            Controls::Grid::SetColumn(mark, 1);
            row.Children().Append(name);
            row.Children().Append(mark);
            list.Items().Append(row);
            marks.push_back(mark);
        }

        auto anyNotes = [](const EmailNotes& n) {
            return !CleanNoteBlock(n.notSold).empty() || !CleanNoteBlock(n.up).empty() || !CleanNoteBlock(n.down).empty();
        };
        auto saveCurrent = [ns, tbNotSold, tbUp, tbDown, marks, &data, anyNotes]() {
            if (ns->current < 0 || ns->loading || static_cast<size_t>(ns->current) >= data.size())
                return;
            EmailNotes n;
            n.notSold = ToStd(tbNotSold.Text());
            n.up = ToStd(tbUp.Text());
            n.down = ToStd(tbDown.Text());
            ns->notes[data[static_cast<size_t>(ns->current)].supplier] = n;
            marks[static_cast<size_t>(ns->current)].Text(anyNotes(n) ? L"notes" : L"");
        };
        // `data` outlives the dialog (it is a local of this coroutine, which waits on ShowAsync below).
        for (auto* box : { &tbNotSold, &tbUp, &tbDown })
            box->TextChanged([saveCurrent](Windows::Foundation::IInspectable const&, Controls::TextChangedEventArgs const&) { saveCurrent(); });
        list.SelectionChanged([ns, list, tbNotSold, tbUp, tbDown, title, &data](Windows::Foundation::IInspectable const&, Controls::SelectionChangedEventArgs const&) {
            const int idx = list.SelectedIndex();
            if (idx < 0 || static_cast<size_t>(idx) >= data.size())
                return;
            ns->current = idx;
            const SupplierGroup& sg = data[static_cast<size_t>(idx)];
            size_t lines = 0;
            for (const auto& pg : sg.products)
                lines += pg.prices.size();
            ns->loading = true;
            title.Text(hstring{ sg.supplier + L"   (" + std::to_wstring(lines) + (lines == 1 ? L" price line, " : L" price lines, ") +
                                FormatKg(sg.totalKgs) + L" kg)" });
            auto it = ns->notes.find(sg.supplier);
            const EmailNotes n = (it != ns->notes.end()) ? it->second : EmailNotes();
            tbNotSold.Text(hstring{ n.notSold });
            tbUp.Text(hstring{ n.up });
            tbDown.Text(hstring{ n.down });
            ns->loading = false;
        });
        nextBtn.Click([list, count = data.size()](Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
            if (count > 0)
                list.SelectedIndex((list.SelectedIndex() + 1) % static_cast<int>(count));
        });

        Controls::Grid upDown;
        Controls::ColumnDefinition u0, u1;
        u0.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        u1.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        upDown.ColumnDefinitions().Append(u0);
        upDown.ColumnDefinitions().Append(u1);
        upDown.ColumnSpacing(12);
        Controls::Grid::SetColumn(tbDown, 1);
        upDown.Children().Append(tbUp);
        upDown.Children().Append(tbDown);

        Controls::StackPanel right;
        right.Spacing(12);
        right.Children().Append(title);
        right.Children().Append(tbNotSold);
        right.Children().Append(upDown);
        right.Children().Append(hint);
        right.Children().Append(nextBtn);

        // A two-column Grid (not a horizontal StackPanel): the right side gets exactly the width that is left, so the boxes and the
        // hint wrap inside the dialog instead of running off its right edge.
        Controls::Grid body;
        body.Width(820);
        body.ColumnSpacing(20);
        Controls::ColumnDefinition b0, b1;
        b0.Width(GridLengthHelper::Auto());
        b1.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        body.ColumnDefinitions().Append(b0);
        body.ColumnDefinitions().Append(b1);
        Controls::Grid::SetColumn(right, 1);
        body.Children().Append(list);
        body.Children().Append(right);
        Controls::ScrollViewer scroll;
        scroll.MaxHeight((std::max)(300.0, RootGrid().ActualHeight() - 260.0));
        scroll.Content(body);

        Controls::ContentDialog dlg;
        dlg.XamlRoot(RootGrid().XamlRoot());
        dlg.Title(box_value(L"Email Suppliers - notes for each supplier"));
        dlg.Content(scroll);
        dlg.PrimaryButtonText(L"Create emails");
        dlg.SecondaryButtonText(L"Skip - no notes");
        dlg.CloseButtonText(L"Cancel");
        dlg.DefaultButton(Controls::ContentDialogButton::Primary);
        dlg.Resources().Insert(box_value(L"ContentDialogMaxWidth"), box_value(960.0));
        // Skip - no notes throws away anything typed: ask first, and stay in the dialog on No.
        dlg.SecondaryButtonClick([strong, ns, saveCurrent, anyNotes](Controls::ContentDialog const&, Controls::ContentDialogButtonClickEventArgs const& args) {
            saveCurrent();
            bool any = false;
            for (const auto& kv : ns->notes)
                if (anyNotes(kv.second)) { any = true; break; }
            if (any && MessageBoxW(strong->Hwnd(), L"You have typed notes. Skip and throw them away?", L"Email Suppliers",
                                   MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
                args.Cancel(true);
        });
        list.SelectedIndex(0); // after the handlers are attached: fills the boxes for the first supplier

        const Controls::ContentDialogResult result = co_await dlg.ShowAsync();
        if (m_closed)
            co_return;
        if (result == Controls::ContentDialogResult::Primary)
        {
            saveCurrent();
            RunEmailSuppliers(ns->notes);
        }
        else if (result == Controls::ContentDialogResult::Secondary)
            RunEmailSuppliers(std::map<std::wstring, EmailNotes>());
    }

    // Same flow and wording as the Win32 app's DoEmailSuppliers: one mailto: draft per supplier in the default mail
    // app, one at a time with a confirmation between each (several mail clients mishandle a second compose request).
    // `notes` = the optional per-supplier notes from the dialog above (empty = none).
    void MainWindow::RunEmailSuppliers(const std::map<std::wstring, EmailNotes>& notes)
    {
        auto data = BuildBreakdownData(m_entries);
        if (data.empty())
        {
            MessageBoxW(Hwnd(), L"There are no entries to email yet.", L"Email Suppliers", MB_OK | MB_ICONINFORMATION);
            return;
        }
        const EmailWording wording = EmailWordingFromSettings(g_user, m_entries, TodaySimple()); // step 14: sheet date + your wording
        auto wordingFor = [&](const std::wstring& supplier) {
            EmailWording w = wording;
            auto n = notes.find(supplier);
            if (n != notes.end())
                w.notes = n->second;
            return w;
        };

        std::vector<std::wstring> withoutEmail;
        for (const SupplierGroup& sg : data)
        {
            auto it = m_supplierEmails.find(sg.supplier);
            if (it == m_supplierEmails.end() || it->second.empty())
                withoutEmail.push_back(sg.supplier);
        }

        if (g_user.emailMode == EmailMode::Gmail)
        {
            // Same flow as the Win32 app's DoEmailSuppliersGmail: one draft per supplier is CREATED in Gmail (nothing is sent,
            // no window opens per supplier, so no one-at-a-time confirmation).
            std::wstring gmsg = L"This will create " + std::to_wstring(data.size()) +
                (data.size() == 1 ? std::wstring(L" draft email") : std::wstring(L" draft emails")) + L" in Gmail" +
                (g_user.gmailAccount.empty() ? std::wstring() : L" (" + g_user.gmailAccount + L")") +
                L". Nothing is sent - you review and send each one from Gmail's Drafts folder.\n\n";
            if (!withoutEmail.empty())
            {
                gmsg += L"These suppliers have no email address on file, so their draft's To field will be blank:\n";
                for (const std::wstring& s : withoutEmail)
                    gmsg += L"  - " + s + L"\n";
                gmsg += L"\n";
            }
            gmsg += L"Continue?";
            if (MessageBoxW(Hwnd(), gmsg.c_str(), L"Email Suppliers - Gmail", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return;

            SYSTEMTIME gst;
            GetLocalTime(&gst);
            size_t made = 0;
            std::wstring problem;
            for (const SupplierGroup& sg : data)
            {
                auto it = m_supplierEmails.find(sg.supplier);
                const std::wstring email = (it != m_supplierEmails.end()) ? it->second : std::wstring();
                const EmailWording sw = wordingFor(sg.supplier);
                const std::wstring dateText = sw.dateText.empty() ? TodayDateString() : sw.dateText;
                EmlBuildResult built = BuildSupplierEml(sg, gst.wHour, dateText, email,
                                                        FormatEmailSubject(sw.subject, dateText, sg.supplier), false, sw);
                if (!built.ok)
                {
                    problem = sg.supplier + L": " + built.error;
                    break;
                }
                std::wstring err;
                if (GmailCreateDraft(Hwnd(), built.content, g_user.gmailAccount, &err) != GmailResult::Ok)
                {
                    problem = err;
                    break;
                }
                ++made;
                m_emailedThisSheet = true;
            }
            if (!problem.empty())
            {
                const std::wstring m = problem + L"\n\n" + std::to_wstring(made) + L" of " + std::to_wstring(data.size()) +
                    L" draft(s) were created before this stopped.\n\nTip: Tools > Settings > Email lets you switch back to mailto or the Outlook (.eml) option.";
                MessageBoxW(Hwnd(), m.c_str(), L"Gmail drafts", MB_OK | MB_ICONWARNING);
                return;
            }
            const std::wstring done = L"Created " + std::to_wstring(made) + (made == 1 ? std::wstring(L" draft") : std::wstring(L" drafts")) +
                L" in Gmail. Open Gmail's Drafts folder to review and send them.\n\nOpen Gmail Drafts now?";
            if (MessageBoxW(Hwnd(), done.c_str(), L"Gmail drafts", MB_YESNO | MB_ICONINFORMATION) == IDYES)
            {
                const std::wstring url = L"https://mail.google.com/mail/" +
                    (g_user.gmailAccount.empty() ? std::wstring() : L"?authuser=" + g_user.gmailAccount) + L"#drafts";
                ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            return;
        }

        std::wstring msg = L"This will open " + std::to_wstring(data.size()) +
            (data.size() == 1 ? std::wstring(L" email") : std::wstring(L" emails")) +
            (g_user.emailMode == EmailMode::Eml ? std::wstring(L" as Outlook drafts (with a price table)") : std::wstring(L" in your default email app")) +
            L", one supplier at a time - you'll be asked to confirm "
            L"before each one opens, so you can send or close each draft before the next appears.\n\n";
        if (!withoutEmail.empty())
        {
            msg += L"These suppliers have no email address on file, so their draft will open with "
                   L"the To field blank - fill it in before sending:\n";
            for (const std::wstring& s : withoutEmail)
                msg += L"  - " + s + L"\n";
            msg += L"\n";
        }
        msg += L"Continue?";
        if (MessageBoxW(Hwnd(), msg.c_str(), L"Email Suppliers", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;

        for (size_t i = 0; i < data.size(); i++)
        {
            SupplierGroup& sg = data[i];
            auto it = m_supplierEmails.find(sg.supplier);
            const std::wstring email = (it != m_supplierEmails.end()) ? it->second : std::wstring();

            std::wstring emailErr;
            if (!EmailSupplierWith(g_user.emailMode, email, sg, &emailErr, wordingFor(sg.supplier)))
            {
                const std::wstring failMsg = L"Couldn't open an email draft for " + sg.supplier + L":\n\n" + emailErr +
                    L"\n\nClick OK to try the next supplier, or Cancel to stop here.";
                if (MessageBoxW(Hwnd(), failMsg.c_str(), L"Email Couldn't Open", MB_OKCANCEL | MB_ICONWARNING) == IDCANCEL)
                    return;
                continue;
            }
            m_emailedThisSheet = true;

            if (i + 1 < data.size())
            {
                const std::wstring nextMsg = L"Opened a draft for " + sg.supplier + L".\n\n"
                    L"Once you've sent (or closed) it, click OK to open the next one for " + data[i + 1].supplier +
                    L", or Cancel to stop here.";
                if (MessageBoxW(Hwnd(), nextMsg.c_str(), L"Email Suppliers", MB_OKCANCEL | MB_ICONINFORMATION) == IDCANCEL)
                    return;
            }
        }
        MessageBoxW(Hwnd(), L"Done - all supplier emails have been opened.", L"Email Suppliers", MB_OK | MB_ICONINFORMATION);
    }
    bool MainWindow::SaveSupplierEmails()
    {
        return SaveNameMaster();
    }

    void MainWindow::ManageNames_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_closed)
            ShowManageNames();
    }

    // Tools > Manage Supplier / Species Names: the same design as the Win32 popup (ManageNamesWindow.cpp, v0.14.x).
    // Click a name to view or edit it; tick boxes only to merge. Names, aliases and dates live in names.txt in the shared
    // data folder (the same file the Win32 app uses); entries on the open sheet are changed too. One level of Undo.
    // A ContentDialog cannot open another ContentDialog, so the merge options are an inline panel, not a pop-up.
    winrt::fire_and_forget MainWindow::ShowManageNames()
    {
        auto strong = get_strong();
        EnsureNamesSeeded();

        struct Row
        {
            std::wstring name;
            int entries = 0;
            std::wstring email;
            std::wstring aliases;
            bool inMaster = false;
            bool recent = false;
            bool ticked = false;
        };
        struct UndoSnap
        {
            bool valid = false;
            std::vector<Entry> entries;
            NameMasterData master;
            std::map<std::wstring, std::wstring> emails;
            bool dirty = false;
        };
        struct State
        {
            bool isSupplier = true;
            bool populating = false;
            bool settingText = false;
            std::vector<Row> rows;
            std::vector<Controls::CheckBox> checks;
            std::vector<std::wstring> dupGroup;
            std::vector<std::wstring> mergeNames;
            std::vector<Controls::RadioButton> keepRadios;
            UndoSnap undo;

            Controls::StackPanel header{ nullptr };
            Controls::ListView list{ nullptr };
            Controls::TextBlock dupLabel{ nullptr };
            Controls::Button dupBtn{ nullptr };
            Controls::StackPanel dupPanel{ nullptr };
            Controls::TextBlock tickLabel{ nullptr };
            Controls::Button mergeBtn{ nullptr };
            Controls::StackPanel mergePanel{ nullptr };
            Controls::TextBlock mergeInfo{ nullptr };
            Controls::StackPanel mergeRadios{ nullptr };
            Controls::CheckBox mergeSame{ nullptr };
            Controls::Button mergeOk{ nullptr };
            Controls::Button mergeCancel{ nullptr };
            Controls::TextBox nameBox{ nullptr };
            Controls::Button renameBtn{ nullptr };
            Controls::Button listBtn{ nullptr };
            Controls::StackPanel emailPanel{ nullptr };
            Controls::TextBox emailBox{ nullptr };
            Controls::Button saveEmail{ nullptr };
            Controls::ComboBox aliasBox{ nullptr };
            Controls::Button aliasAdd{ nullptr };
            Controls::Button aliasRem{ nullptr };
            Controls::TextBlock status{ nullptr };
            Controls::Button undoBtn{ nullptr };

            std::function<void()> buildRows;
            std::function<void(const std::wstring&)> populate;
            std::function<void()> refresh;
            std::function<void()> updateRename;
            std::function<void()> takeSnapshot;
            std::function<void(const std::vector<std::wstring>&, const std::wstring&, bool, bool)> executeMerge;
            std::function<void()> beginMerge;
            std::function<void()> doMerge;
            std::function<void()> doTickDuplicates;
            std::function<void()> doRename;
            std::function<void()> doSaveEmail;
            std::function<void()> doListBtn;
            std::function<void()> doAddAlias;
            std::function<void()> doRemoveAlias;
            std::function<void()> doUndo;
            std::function<void(const std::wstring&)> setStatus;
        };
        auto st = std::make_shared<State>();

        // Column widths (pixels), shared by the header and the rows. The email column is collapsed for species.
        const double kWCheck = 34, kWName = 200, kWEntries = 62, kWEmail = 170, kWAlias = 170, kWNote = 70;

        auto makeCell = [](const std::wstring& text, double width, bool bold) {
            Controls::TextBlock tb;
            tb.Text(hstring{ text });
            tb.Width(width);
            tb.TextTrimming(TextTrimming::CharacterEllipsis);
            tb.VerticalAlignment(VerticalAlignment::Center);
            tb.Margin(ThicknessHelper::FromLengths(0, 0, 8, 0));
            if (bold)
                tb.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            return tb;
        };

        // ---- controls -----------------------------------------------------------------------------------------
        Controls::RadioButton rbSupplier;
        rbSupplier.Content(box_value(L"Suppliers"));
        rbSupplier.GroupName(L"ManageNamesKind");
        rbSupplier.IsChecked(true);
        Controls::RadioButton rbSpecies;
        rbSpecies.Content(box_value(L"Species"));
        rbSpecies.GroupName(L"ManageNamesKind");
        Controls::StackPanel kind;
        kind.Orientation(Controls::Orientation::Horizontal);
        kind.Spacing(16);
        kind.Children().Append(rbSupplier);
        kind.Children().Append(rbSpecies);

        st->dupLabel = Controls::TextBlock();
        st->dupLabel.TextWrapping(TextWrapping::Wrap);
        st->dupLabel.VerticalAlignment(VerticalAlignment::Center);
        st->dupBtn = Controls::Button();
        st->dupBtn.Content(box_value(L"Tick these"));
        st->dupPanel = Controls::StackPanel();
        st->dupPanel.Orientation(Controls::Orientation::Horizontal);
        st->dupPanel.Spacing(12);
        st->dupPanel.Children().Append(st->dupLabel);
        st->dupPanel.Children().Append(st->dupBtn);

        Controls::TextBlock hint;
        hint.Text(L"Click a name to view or edit it. Tick boxes only when you want to merge names.");
        hint.Opacity(0.8);

        st->header = Controls::StackPanel();
        st->header.Orientation(Controls::Orientation::Horizontal);
        st->header.Margin(ThicknessHelper::FromLengths(12, 0, 0, 0));
        st->list = Controls::ListView();
        st->list.SelectionMode(Controls::ListViewSelectionMode::Single);
        // Small laptop screens: shrink the list so the merge panel and the rows below it are reachable.
        // As tall as the window allows (it was a fixed 220 = about 3 rows); rows are also made more compact below.
        st->list.Height((std::max)(200.0, RootGrid().ActualHeight() - 600.0));
        st->list.Resources().Insert(box_value(L"ListViewItemMinHeight"), box_value(34.0));
        st->list.BorderThickness(ThicknessHelper::FromUniformLength(1));
        st->list.BorderBrush(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 0xE3, 0xE8, 0xEF) });

        st->tickLabel = Controls::TextBlock();
        st->tickLabel.VerticalAlignment(VerticalAlignment::Center);
        st->mergeBtn = Controls::Button();
        st->mergeBtn.Content(box_value(L"Merge ticked..."));
        Controls::StackPanel tickRow;
        tickRow.Orientation(Controls::Orientation::Horizontal);
        tickRow.Spacing(16);
        tickRow.Children().Append(st->tickLabel);
        tickRow.Children().Append(st->mergeBtn);

        // Inline merge options (hidden until Merge ticked is pressed).
        st->mergeInfo = Controls::TextBlock();
        st->mergeInfo.TextWrapping(TextWrapping::Wrap);
        st->mergeRadios = Controls::StackPanel();
        st->mergeSame = Controls::CheckBox();
        st->mergeSame.Content(box_value(L"They are the same thing spelled differently: also remove the other names from the list "
                                        L"and correct them automatically from now on"));
        st->mergeOk = Controls::Button();
        st->mergeOk.Content(box_value(L"Merge"));
        st->mergeOk.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
        st->mergeCancel = Controls::Button();
        st->mergeCancel.Content(box_value(L"Cancel"));
        Controls::StackPanel mergeBtns;
        mergeBtns.Orientation(Controls::Orientation::Horizontal);
        mergeBtns.Spacing(8);
        mergeBtns.Children().Append(st->mergeOk);
        mergeBtns.Children().Append(st->mergeCancel);
        st->mergePanel = Controls::StackPanel();
        st->mergePanel.Spacing(6);
        st->mergePanel.Padding(ThicknessHelper::FromUniformLength(10));
        st->mergePanel.BorderThickness(ThicknessHelper::FromUniformLength(1));
        st->mergePanel.BorderBrush(Media::SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, 0x9A, 0xB4, 0xD8) });
        st->mergePanel.Visibility(Visibility::Collapsed);
        st->mergePanel.Children().Append(st->mergeInfo);
        st->mergePanel.Children().Append(st->mergeRadios);
        st->mergePanel.Children().Append(st->mergeSame);
        st->mergePanel.Children().Append(mergeBtns);

        auto makeLabel = [](const wchar_t* text) {
            Controls::TextBlock t;
            t.Text(text);
            t.Width(110);
            t.VerticalAlignment(VerticalAlignment::Center);
            return t;
        };

        st->nameBox = Controls::TextBox();
        st->nameBox.MaxLength(static_cast<int32_t>(kMaxSupplierSpeciesLength));
        st->nameBox.Width(330);
        st->renameBtn = Controls::Button();
        st->renameBtn.Content(box_value(L"Rename"));
        st->listBtn = Controls::Button();
        st->listBtn.Content(box_value(L"Remove from list"));
        Controls::StackPanel nameRow;
        nameRow.Orientation(Controls::Orientation::Horizontal);
        nameRow.Spacing(8);
        nameRow.Children().Append(makeLabel(L"Name:"));
        nameRow.Children().Append(st->nameBox);
        nameRow.Children().Append(st->renameBtn);
        nameRow.Children().Append(st->listBtn);

        st->emailBox = Controls::TextBox();
        st->emailBox.MaxLength(static_cast<int32_t>(kMaxSupplierSpeciesLength));
        st->emailBox.Width(330);
        st->saveEmail = Controls::Button();
        st->saveEmail.Content(box_value(L"Save Email"));
        st->emailPanel = Controls::StackPanel();
        st->emailPanel.Orientation(Controls::Orientation::Horizontal);
        st->emailPanel.Spacing(8);
        st->emailPanel.Children().Append(makeLabel(L"Email:"));
        st->emailPanel.Children().Append(st->emailBox);
        st->emailPanel.Children().Append(st->saveEmail);

        st->aliasBox = Controls::ComboBox();
        st->aliasBox.IsEditable(true);
        st->aliasBox.Width(330);
        st->aliasAdd = Controls::Button();
        st->aliasAdd.Content(box_value(L"Add alias"));
        st->aliasRem = Controls::Button();
        st->aliasRem.Content(box_value(L"Remove alias"));
        Controls::StackPanel aliasRow;
        aliasRow.Orientation(Controls::Orientation::Horizontal);
        aliasRow.Spacing(8);
        aliasRow.Children().Append(makeLabel(L"Also known as:"));
        aliasRow.Children().Append(st->aliasBox);
        aliasRow.Children().Append(st->aliasAdd);
        aliasRow.Children().Append(st->aliasRem);

        st->status = Controls::TextBlock();
        st->status.TextWrapping(TextWrapping::Wrap);
        st->status.Opacity(0.85);
        st->undoBtn = Controls::Button();
        st->undoBtn.Content(box_value(L"Undo"));

        Controls::StackPanel panel;
        panel.Spacing(8);
        panel.MinWidth(700);
        panel.Children().Append(kind);
        panel.Children().Append(st->dupPanel);
        panel.Children().Append(hint);
        panel.Children().Append(st->header);
        panel.Children().Append(st->list);
        panel.Children().Append(tickRow);
        panel.Children().Append(st->mergePanel);
        panel.Children().Append(nameRow);
        panel.Children().Append(st->emailPanel);
        panel.Children().Append(aliasRow);
        panel.Children().Append(st->status);
        panel.Children().Append(st->undoBtn);

        // ---- helpers ------------------------------------------------------------------------------------------
        auto curList = [this, st]() -> NameList& { return st->isSupplier ? m_names.suppliers : m_names.species; };

        st->setStatus = [st](const std::wstring& text) { st->status.Text(hstring{ text }); };

        st->takeSnapshot = [this, st]() {
            ClearUndoDelete(); // a rename / merge rewrites the rows: an old Undo Delete would no longer fit
            st->undo.valid = true;
            st->undo.entries = m_entries;
            st->undo.master = m_names;
            st->undo.emails = m_supplierEmails;
            st->undo.dirty = m_dirty;
        };

        // Rows = every name in the master list + any name on the open sheet that is not in the list yet.
        st->buildRows = [this, st]() {
            st->rows.clear();
            const NameList& list = st->isSupplier ? m_names.suppliers : m_names.species;
            std::map<std::wstring, int> counts;
            for (const Entry& e : m_entries)
                counts[st->isSupplier ? e.supplier : e.product]++;
            const std::wstring today = TodayIso();
            std::set<std::wstring> seen;
            auto addRow = [&](const std::wstring& name, bool inMaster) {
                Row r;
                r.name = name;
                auto c = counts.find(name);
                r.entries = (c == counts.end()) ? 0 : c->second;
                if (st->isSupplier)
                {
                    auto em = m_supplierEmails.find(name);
                    if (em != m_supplierEmails.end())
                        r.email = em->second;
                }
                r.inMaster = inMaster;
                if (inMaster)
                {
                    auto a = list.added.find(name);
                    r.recent = a != list.added.end() && NameAddedRecently(a->second, today, 7);
                }
                const std::vector<std::wstring> al = AliasesOf(list, name);
                for (size_t i = 0; i < al.size(); ++i)
                    r.aliases += (i ? L", " : L"") + al[i];
                st->rows.push_back(r);
            };
            for (const std::wstring& n : list.names)
                if (seen.insert(n).second)
                    addRow(n, true);
            for (const auto& kv : counts)
                if (!kv.first.empty() && seen.insert(kv.first).second)
                    addRow(kv.first, false);
            std::sort(st->rows.begin(), st->rows.end(), [](const Row& a, const Row& b) {
                return std::make_pair(NameKey(a.name), a.name) < std::make_pair(NameKey(b.name), b.name);
            });
        };

        // Enables/disables and fills everything below and around the list from the selection and ticks.
        st->refresh = [this, st]() {
            const int32_t selRaw = st->list.SelectedIndex();
            const int sel = (selRaw >= 0 && static_cast<size_t>(selRaw) < st->rows.size()) ? static_cast<int>(selRaw) : -1;
            int ticked = 0;
            for (const Row& r : st->rows)
                if (r.ticked)
                    ++ticked;

            st->tickLabel.Text(hstring{ ticked == 0 ? std::wstring(L"Nothing ticked") : std::to_wstring(ticked) + L" ticked" });
            st->mergeBtn.Content(box_value(hstring{ ticked >= 2 ? L"Merge ticked (" + std::to_wstring(ticked) + L")..." : std::wstring(L"Merge ticked...") }));
            st->mergeBtn.IsEnabled(ticked >= 2);
            if (ticked < 2)
                st->mergePanel.Visibility(Visibility::Collapsed);

            st->settingText = true;
            st->nameBox.Text(hstring{ sel >= 0 ? st->rows[static_cast<size_t>(sel)].name : std::wstring() });
            st->emailBox.Text(hstring{ sel >= 0 ? st->rows[static_cast<size_t>(sel)].email : std::wstring() });
            st->settingText = false;
            st->nameBox.IsEnabled(sel >= 0);
            st->emailPanel.Visibility(st->isSupplier ? Visibility::Visible : Visibility::Collapsed);
            st->emailBox.IsEnabled(sel >= 0 && st->isSupplier);
            st->saveEmail.IsEnabled(sel >= 0 && st->isSupplier);

            if (sel < 0)
            {
                st->listBtn.Content(box_value(L"Remove from list"));
                st->listBtn.IsEnabled(false);
            }
            else if (!st->rows[static_cast<size_t>(sel)].inMaster)
            {
                st->listBtn.Content(box_value(L"Add to list"));
                st->listBtn.IsEnabled(true);
            }
            else
            {
                st->listBtn.Content(box_value(L"Remove from list"));
                st->listBtn.IsEnabled(st->rows[static_cast<size_t>(sel)].entries == 0); // a name this sheet uses can't just vanish
            }
            st->undoBtn.IsEnabled(st->undo.valid);

            // Aliases: the drop-down lists this name's aliases (pick one to remove it); type a new one to add it.
            st->aliasBox.Items().Clear();
            const bool aliasOk = sel >= 0 && st->rows[static_cast<size_t>(sel)].inMaster;
            bool hasAliases = false;
            if (aliasOk)
            {
                const NameList& list = st->isSupplier ? m_names.suppliers : m_names.species;
                for (const std::wstring& a : AliasesOf(list, st->rows[static_cast<size_t>(sel)].name))
                {
                    st->aliasBox.Items().Append(box_value(hstring{ a }));
                    hasAliases = true;
                }
            }
            st->aliasBox.Text(L"");
            st->aliasBox.IsEnabled(aliasOk);
            st->aliasAdd.IsEnabled(aliasOk);
            st->aliasRem.IsEnabled(aliasOk && hasAliases);
            st->updateRename();

            // Duplicates strip
            std::vector<std::wstring> names;
            for (const Row& r : st->rows)
                names.push_back(r.name);
            auto groups = FindDuplicateNameGroups(names);
            st->dupGroup = groups.empty() ? std::vector<std::wstring>() : groups[0];
            if (st->dupGroup.size() > 1)
            {
                std::wstring text = L"Possible duplicates:  ";
                for (size_t i = 0; i < st->dupGroup.size(); ++i)
                {
                    if (i)
                        text += L"  /  ";
                    text += st->dupGroup[i];
                }
                st->dupLabel.Text(hstring{ text });
                st->dupPanel.Visibility(Visibility::Visible);
            }
            else
            {
                st->dupPanel.Visibility(Visibility::Collapsed);
            }
        };

        st->updateRename = [st]() {
            const int32_t selRaw = st->list.SelectedIndex();
            bool can = false;
            if (selRaw >= 0 && static_cast<size_t>(selRaw) < st->rows.size())
            {
                const std::wstring typed = CollapseNameSpaces(ToStd(st->nameBox.Text()));
                can = !typed.empty() && typed != st->rows[static_cast<size_t>(selRaw)].name;
            }
            st->renameBtn.IsEnabled(can);
        };

        // Rebuilds the list. `selectName` (if still present) stays selected so the boxes below keep showing it.
        st->populate = [this, st, kWCheck, kWName, kWEntries, kWEmail, kWAlias, kWNote, makeCell](const std::wstring& selectName) {
            st->populating = true;
            st->list.Items().Clear();
            st->checks.clear();
            st->mergePanel.Visibility(Visibility::Collapsed);
            st->buildRows();

            const double emailW = st->isSupplier ? kWEmail : 0.0;
            st->header.Children().Clear();
            st->header.Children().Append(makeCell(L"", kWCheck, true));
            st->header.Children().Append(makeCell(L"Name", kWName, true));
            st->header.Children().Append(makeCell(L"Entries", kWEntries, true));
            if (st->isSupplier)
                st->header.Children().Append(makeCell(L"Email", emailW, true));
            st->header.Children().Append(makeCell(L"Also known as", kWAlias, true));
            st->header.Children().Append(makeCell(L"", kWNote, true));

            int selectIdx = -1;
            for (size_t i = 0; i < st->rows.size(); ++i)
            {
                const Row& r = st->rows[i];
                Controls::StackPanel line;
                line.Orientation(Controls::Orientation::Horizontal);

                Controls::CheckBox cb;
                cb.Width(kWCheck);
                cb.MinWidth(0);
                cb.MinHeight(24);
                cb.Checked([st, i](auto&&, auto&&) {
                    if (st->populating || i >= st->rows.size())
                        return;
                    st->rows[i].ticked = true;
                    st->mergePanel.Visibility(Visibility::Collapsed); // the choice list would be out of date
                    st->refresh();
                });
                cb.Unchecked([st, i](auto&&, auto&&) {
                    if (st->populating || i >= st->rows.size())
                        return;
                    st->rows[i].ticked = false;
                    st->mergePanel.Visibility(Visibility::Collapsed);
                    st->refresh();
                });
                st->checks.push_back(cb);
                line.Children().Append(cb);
                line.Children().Append(makeCell(r.name, kWName, false));
                line.Children().Append(makeCell(std::to_wstring(r.entries), kWEntries, false));
                if (st->isSupplier)
                    line.Children().Append(makeCell(r.email.empty() ? std::wstring(L"no email saved") : r.email, emailW, false));
                line.Children().Append(makeCell(r.aliases, kWAlias, false));
                line.Children().Append(makeCell(!r.inMaster ? std::wstring(L"not in list") : (r.recent ? std::wstring(L"new") : std::wstring()), kWNote, false));
                st->list.Items().Append(line);
                if (!selectName.empty() && r.name == selectName)
                    selectIdx = static_cast<int>(i);
            }
            st->list.SelectedIndex(selectIdx);
            if (selectIdx >= 0)
                st->list.ScrollIntoView(st->list.Items().GetAt(static_cast<uint32_t>(selectIdx)));
            st->populating = false;
            st->refresh();
        };

        // Folds `sources` into `target` on the open sheet, in the master list and (suppliers) in the email list.
        // alsoList false = fix the entries only: names stay in the list, no alias is recorded, emails are untouched.
        st->executeMerge = [this, st, curList](const std::vector<std::wstring>& sources, const std::wstring& target,
                                               bool isRename, bool alsoList) {
            st->takeSnapshot();
            bool emailsChanged = false;
            if (st->isSupplier && alsoList)
            {
                std::wstring keptEmail;
                auto t = m_supplierEmails.find(target);
                if (t != m_supplierEmails.end())
                    keptEmail = t->second;
                for (const std::wstring& s : sources)
                {
                    if (s == target)
                        continue;
                    auto it = m_supplierEmails.find(s);
                    if (keptEmail.empty() && it != m_supplierEmails.end())
                        keptEmail = it->second;
                    if (it != m_supplierEmails.end())
                    {
                        m_supplierEmails.erase(it);
                        emailsChanged = true;
                    }
                }
                if (!keptEmail.empty())
                {
                    auto t2 = m_supplierEmails.find(target);
                    if (t2 == m_supplierEmails.end() || t2->second != keptEmail)
                    {
                        m_supplierEmails[target] = keptEmail;
                        emailsChanged = true;
                    }
                }
            }

            int changed = 0;
            for (Entry& e : m_entries)
            {
                std::wstring& field = st->isSupplier ? e.supplier : e.product;
                if (field == target)
                    continue;
                for (const std::wstring& s : sources)
                {
                    if (field == s)
                    {
                        field = target;
                        ++changed;
                        break;
                    }
                }
            }

            if (alsoList)
                NameListMerge(curList(), sources, target, TodayIso());
            else
                NameListAdd(curList(), target, TodayIso()); // the kept name must be in the list (no-op if it is)
            const bool masterSaved = SaveNameMaster();
            const bool emailsSaved = emailsChanged ? SaveSupplierEmails() : true;

            if (changed > 0)
            {
                MarkDirty();
                EndEditMode(); // a half-edited row could still carry an old spelling
                RefreshList();
                Recalculate();
            }
            RefreshNameLists();
            st->populate(target);

            const std::wstring entriesText = std::to_wstring(changed) + (changed == 1 ? L" entry" : L" entries");
            std::wstring status;
            if (!alsoList)
                status = L"Changed " + entriesText + L" to \"" + target + L"\". The other names are still in the list.";
            else if (isRename)
                status = L"Renamed \"" + sources[0] + L"\" to \"" + target + L"\". " + entriesText + L" updated.";
            else
                status = L"Merged " + std::to_wstring(sources.size()) + L" names into \"" + target + L"\". " + entriesText + L" updated.";
            st->setStatus(status);
            if (!masterSaved || !emailsSaved)
                MessageBoxW(Hwnd(),
                            L"The change was made, but names.txt could not be written. It will be lost when the "
                            L"app closes unless a later save succeeds. Check that neither file is open in another program.",
                            L"Save Failed", MB_OK | MB_ICONWARNING);
        };

        // Shows the inline merge options for the ticked names.
        st->beginMerge = [st]() {
            st->mergeNames.clear();
            std::vector<int> counts;
            int best = 0;
            int total = 0;
            std::vector<std::wstring> distinctEmails;
            std::wstring emailLines;
            bool listMerge = true;
            const Row* first = nullptr;
            for (const Row& r : st->rows)
            {
                if (!r.ticked)
                    continue;
                if (!first)
                    first = &r;
                st->mergeNames.push_back(r.name);
                counts.push_back(r.entries);
                total += r.entries;
                if (counts.back() > counts[static_cast<size_t>(best)])
                    best = static_cast<int>(counts.size()) - 1;
                if (r.inMaster && NameKey(r.name) != NameKey(first->name))
                    listMerge = false; // two genuinely different names in the list: probably a real name typed by mistake
                if (st->isSupplier && !r.email.empty())
                {
                    if (std::find(distinctEmails.begin(), distinctEmails.end(), r.email) == distinctEmails.end())
                        distinctEmails.push_back(r.email);
                    emailLines += L"\n  " + r.name + L"  -  " + r.email;
                }
            }
            if (st->mergeNames.size() < 2)
                return;
            std::wstring info = L"Merge " + std::to_wstring(st->mergeNames.size()) +
                                L" names into one. Choose the name to keep; entries on this sheet using the other names are changed to it.";
            if (distinctEmails.size() > 1)
                info += L"\n\nThese suppliers have different email addresses:" + emailLines +
                        L"\n\nThe kept supplier's address is used (or the first one above, if it has none), when the "
                        L"box below is ticked. You can change it afterwards.";
            st->mergeInfo.Text(hstring{ info });
            st->mergeRadios.Children().Clear();
            st->keepRadios.clear();
            for (size_t i = 0; i < st->mergeNames.size(); ++i)
            {
                Controls::RadioButton rb;
                rb.GroupName(L"MergeKeepGroup");
                rb.Content(box_value(hstring{ st->mergeNames[i] + L"   (" + std::to_wstring(total - counts[i]) +
                                              (total - counts[i] == 1 ? L" entry" : L" entries") + L" would change)" }));
                rb.IsChecked(static_cast<int>(i) == best);
                st->mergeRadios.Children().Append(rb);
                st->keepRadios.push_back(rb);
            }
            st->mergeSame.IsChecked(listMerge);
            st->mergePanel.Visibility(Visibility::Visible);
        };

        st->doMerge = [st]() {
            if (st->mergeNames.size() < 2)
                return;
            size_t keep = 0;
            for (size_t i = 0; i < st->keepRadios.size(); ++i)
            {
                if (st->keepRadios[i].IsChecked().Value())
                {
                    keep = i;
                    break;
                }
            }
            const bool alsoList = st->mergeSame.IsChecked().Value();
            const std::vector<std::wstring> names = st->mergeNames;
            st->mergePanel.Visibility(Visibility::Collapsed);
            st->executeMerge(names, names[keep], false, alsoList);
        };

        st->doTickDuplicates = [st]() {
            st->populating = true; // the Checked handlers must not each refresh the whole window
            for (size_t i = 0; i < st->rows.size() && i < st->checks.size(); ++i)
            {
                const bool want = std::find(st->dupGroup.begin(), st->dupGroup.end(), st->rows[i].name) != st->dupGroup.end();
                st->rows[i].ticked = want;
                st->checks[i].IsChecked(want);
            }
            st->populating = false;
            st->refresh();
        };

        st->doRename = [this, st]() {
            const int32_t selRaw = st->list.SelectedIndex();
            if (selRaw < 0 || static_cast<size_t>(selRaw) >= st->rows.size())
                return;
            const std::wstring oldName = st->rows[static_cast<size_t>(selRaw)].name;
            const int oldEntries = st->rows[static_cast<size_t>(selRaw)].entries;
            const std::wstring newName = CollapseNameSpaces(ToStd(st->nameBox.Text()));
            if (newName.empty() || newName == oldName)
                return;
            if (newName.find(L'|') != std::wstring::npos)
            {
                MessageBoxW(Hwnd(), L"Names can't contain the '|' character - it's used internally as a separator when saving.",
                            L"Invalid Name", MB_OK | MB_ICONWARNING);
                return;
            }
            // Typing a name that is already in the list (even just in different capitals) means "merge into that one".
            std::wstring existing;
            const std::wstring newKey = NameKey(newName);
            for (const Row& r : st->rows)
            {
                if (r.name != oldName && (r.name == newName || (!newKey.empty() && NameKey(r.name) == newKey)))
                {
                    existing = r.name;
                    break;
                }
            }
            if (!existing.empty())
            {
                const std::wstring msg = L"\"" + existing + L"\" is already in the list.\n\nMerge \"" + oldName + L"\" into \"" +
                                         existing + L"\"? " + std::to_wstring(oldEntries) + (oldEntries == 1 ? L" entry" : L" entries") +
                                         L" will change.";
                if (MessageBoxW(Hwnd(), msg.c_str(), L"Merge Names", MB_YESNO | MB_ICONQUESTION) != IDYES)
                    return;
                st->executeMerge({ oldName }, existing, false, true);
                return;
            }
            st->executeMerge({ oldName }, newName, true, true);
        };

        st->doSaveEmail = [this, st]() {
            const int32_t selRaw = st->list.SelectedIndex();
            if (!st->isSupplier || selRaw < 0 || static_cast<size_t>(selRaw) >= st->rows.size())
                return;
            const std::wstring supplier = st->rows[static_cast<size_t>(selRaw)].name;
            const std::wstring email = TrimW(ToStd(st->emailBox.Text()));
            if (!email.empty())
            {
                if (email.find(L'|') != std::wstring::npos)
                {
                    MessageBoxW(Hwnd(), L"Email addresses can't contain the '|' character.", L"Invalid Email", MB_OK | MB_ICONWARNING);
                    return;
                }
                if (!LooksLikeEmail(email))
                {
                    MessageBoxW(Hwnd(), L"That doesn't look like a valid email address.", L"Invalid Email", MB_OK | MB_ICONWARNING);
                    return;
                }
            }
            if (email.empty())
                m_supplierEmails.erase(supplier);
            else
            {
                m_supplierEmails[supplier] = email;
                // the address is stored on the supplier's names.txt line, so the supplier must be in the list
                NameListAdd(m_names.suppliers, supplier, TodayIso());
            }
            const bool saved = SaveSupplierEmails();
            st->populate(supplier);
            st->setStatus(email.empty() ? L"Email cleared." : L"Email saved.");
            if (!saved)
                MessageBoxW(Hwnd(),
                            L"Could not save the email address to disk - the change is still in memory for this "
                            L"session, but will be lost when the app closes unless the save succeeds. Check that "
                            L"names.txt isn't open in another program and try again.",
                            L"Save Failed", MB_OK | MB_ICONWARNING);
        };

        st->doListBtn = [this, st, curList]() {
            const int32_t selRaw = st->list.SelectedIndex();
            if (selRaw < 0 || static_cast<size_t>(selRaw) >= st->rows.size())
                return;
            const Row row = st->rows[static_cast<size_t>(selRaw)];
            std::wstring status;
            bool removed = false;
            if (!row.inMaster)
            {
                NameListAdd(curList(), row.name, TodayIso());
                status = L"\"" + row.name + L"\" added to the list.";
            }
            else if (row.entries == 0)
            {
                NameListRemove(curList(), row.name);
                if (st->isSupplier)
                    m_supplierEmails.erase(row.name); // the address lives on the list line, so it goes too
                status = L"\"" + row.name + L"\" removed from the list.";
                removed = true;
            }
            else
                return;
            const bool saved = SaveNameMaster();
            RefreshNameLists();
            st->populate(removed ? std::wstring() : row.name);
            st->setStatus(status);
            if (!saved)
                MessageBoxW(Hwnd(), L"Could not save names.txt. The change is in memory only until a later save succeeds.",
                            L"Save Failed", MB_OK | MB_ICONWARNING);
        };

        st->doAddAlias = [this, st, curList]() {
            const int32_t selRaw = st->list.SelectedIndex();
            if (selRaw < 0 || static_cast<size_t>(selRaw) >= st->rows.size() || !st->rows[static_cast<size_t>(selRaw)].inMaster)
                return;
            const std::wstring name = st->rows[static_cast<size_t>(selRaw)].name;
            const std::wstring typed = CollapseNameSpaces(ToStd(st->aliasBox.Text()));
            std::wstring movedFrom;
            std::wstring replacedSpelling; // same spelling ignoring capitals/spaces/punctuation: replaced, not added
            for (const auto& kv : curList().aliases)
            {
                if (NameKey(kv.first) != NameKey(typed))
                    continue;
                if (kv.second != name)
                    movedFrom = kv.second;
                else if (kv.first != typed)
                    replacedSpelling = kv.first;
            }
            const UndoSnap previous = st->undo;
            st->takeSnapshot();
            const AliasAddResult res = NameListAddAlias(curList(), typed, name);
            if (res != AliasAddResult::Added)
            {
                st->undo = previous; // nothing changed - keep whatever Undo was there before
                switch (res)
                {
                case AliasAddResult::Empty: st->setStatus(L"Type the other spelling first, then press Add alias."); break;
                case AliasAddResult::SameAsName: st->setStatus(L"\"" + typed + L"\" is already this name (capitals and spacing are matched automatically)."); break;
                case AliasAddResult::IsAName: st->setStatus(L"\"" + typed + L"\" is already a name in the list - tick both and use Merge instead."); break;
                default: st->setStatus(L"That name is not in the list."); break;
                }
                return;
            }
            const bool saved = SaveNameMaster();
            st->populate(name);
            st->setStatus(L"\"" + typed + L"\" will now be read as \"" + name + L"\"." +
                          (movedFrom.empty() ? std::wstring() : L" (It used to mean \"" + movedFrom + L"\".)") +
                          (replacedSpelling.empty() ? std::wstring() : L" It replaced \"" + replacedSpelling + L"\" - spellings that differ only in capitals, spaces or punctuation count as the same."));
            if (!saved)
                MessageBoxW(Hwnd(), L"Could not save names.txt. The change is in memory only until a later save succeeds.",
                            L"Save Failed", MB_OK | MB_ICONWARNING);
        };

        st->doRemoveAlias = [this, st, curList]() {
            const int32_t selRaw = st->list.SelectedIndex();
            if (selRaw < 0 || static_cast<size_t>(selRaw) >= st->rows.size() || !st->rows[static_cast<size_t>(selRaw)].inMaster)
                return;
            const Row row = st->rows[static_cast<size_t>(selRaw)];
            const std::vector<std::wstring> mine = AliasesOf(curList(), row.name);
            if (mine.empty())
                return;
            const std::wstring typed = CollapseNameSpaces(ToStd(st->aliasBox.Text()));
            std::wstring target;
            if (typed.empty())
            {
                if (mine.size() == 1)
                    target = mine[0];
                else
                {
                    st->setStatus(L"Pick which one to remove from the drop-down box (" + row.aliases + L").");
                    return;
                }
            }
            else
            {
                for (const std::wstring& a : mine)
                    if (NameKey(a) == NameKey(typed))
                        target = a;
                if (target.empty())
                {
                    st->setStatus(L"\"" + typed + L"\" is not one of this name's other spellings (" + row.aliases + L").");
                    return;
                }
            }
            st->takeSnapshot();
            NameListRemoveAlias(curList(), target);
            const bool saved = SaveNameMaster();
            st->populate(row.name);
            st->setStatus(L"\"" + target + L"\" is no longer read as \"" + row.name + L"\".");
            if (!saved)
                MessageBoxW(Hwnd(), L"Could not save names.txt. The change is in memory only until a later save succeeds.",
                            L"Save Failed", MB_OK | MB_ICONWARNING);
        };

        st->doUndo = [this, st]() {
            if (!st->undo.valid)
                return;
            EndEditMode();
            ClearUndoDelete();
            m_entries = st->undo.entries;
            m_names = st->undo.master;
            m_supplierEmails = st->undo.emails;
            m_dirty = st->undo.dirty;
            st->undo.valid = false;
            const bool a = SaveNameMaster();
            const bool b = SaveSupplierEmails();
            UpdateTitle();
            RefreshNameLists();
            RefreshList();
            Recalculate();
            st->populate(std::wstring());
            st->setStatus(L"Undone.");
            if (!a || !b)
                MessageBoxW(Hwnd(), L"The change was undone on screen, but names.txt could not be written.",
                            L"Save Failed", MB_OK | MB_ICONWARNING);
        };

        // ---- wiring -------------------------------------------------------------------------------------------
        rbSupplier.Checked([st](auto&&, auto&&) {
            st->isSupplier = true;
            st->populate(std::wstring());
            st->setStatus(L"");
        });
        rbSpecies.Checked([st](auto&&, auto&&) {
            st->isSupplier = false;
            st->populate(std::wstring());
            st->setStatus(L"");
        });
        st->list.SelectionChanged([st](auto&&, auto&&) {
            if (!st->populating)
                st->refresh();
        });
        st->nameBox.TextChanged([st](auto&&, auto&&) {
            if (!st->settingText)
                st->updateRename();
        });
        st->dupBtn.Click([st](auto&&, auto&&) { st->doTickDuplicates(); });
        st->mergeBtn.Click([st](auto&&, auto&&) { st->beginMerge(); });
        st->mergeOk.Click([st](auto&&, auto&&) { st->doMerge(); });
        st->mergeCancel.Click([st](auto&&, auto&&) { st->mergePanel.Visibility(Visibility::Collapsed); });
        st->renameBtn.Click([st](auto&&, auto&&) { st->doRename(); });
        st->listBtn.Click([st](auto&&, auto&&) { st->doListBtn(); });
        st->saveEmail.Click([st](auto&&, auto&&) { st->doSaveEmail(); });
        st->aliasAdd.Click([st](auto&&, auto&&) { st->doAddAlias(); });
        st->aliasRem.Click([st](auto&&, auto&&) { st->doRemoveAlias(); });
        st->undoBtn.Click([st](auto&&, auto&&) { st->doUndo(); });

        st->populate(std::wstring());

        Controls::ContentDialog dlg;
        dlg.XamlRoot(RootGrid().XamlRoot());
        dlg.Title(box_value(L"Manage Supplier / Species Names"));
        // Scrolls when the window is short (laptop): the merge options used to be cut off with no way to reach them.
        Controls::ScrollViewer panelScroll;
        panelScroll.VerticalScrollBarVisibility(Controls::ScrollBarVisibility::Auto);
        panelScroll.HorizontalScrollBarVisibility(Controls::ScrollBarVisibility::Disabled);
        panelScroll.MaxHeight((std::max)(260.0, RootGrid().ActualHeight() - 170.0));
        panelScroll.Content(panel);
        dlg.Content(panelScroll);
        dlg.CloseButtonText(L"Close");
        dlg.DefaultButton(Controls::ContentDialogButton::Close);
        dlg.Resources().Insert(box_value(L"ContentDialogMaxWidth"), box_value(1100.0)); // wide enough for the list columns
        // The dialog's own default height cap (756) was what forced a scroll bar on big monitors; follow the window instead.
        dlg.Resources().Insert(box_value(L"ContentDialogMaxHeight"), box_value((std::max)(500.0, RootGrid().ActualHeight() - 20.0)));
        co_await dlg.ShowAsync();
        // Break the State <-> lambda reference cycles.
        st->buildRows = nullptr;
        st->populate = nullptr;
        st->refresh = nullptr;
        st->updateRename = nullptr;
        st->takeSnapshot = nullptr;
        st->executeMerge = nullptr;
        st->beginMerge = nullptr;
        st->doMerge = nullptr;
        st->doTickDuplicates = nullptr;
        st->doRename = nullptr;
        st->doSaveEmail = nullptr;
        st->doListBtn = nullptr;
        st->doAddAlias = nullptr;
        st->doRemoveAlias = nullptr;
        st->doUndo = nullptr;
        st->setStatus = nullptr;
    }
}
