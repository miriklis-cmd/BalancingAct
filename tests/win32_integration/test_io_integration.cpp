// Windows integration tests - file loading (Phase 1 item 3).
//
// Exercises FishBalanceWin32IO.h's LoadFbdFileContent()/ReadAllBytes()
// against REAL files in a real, uniquely-named temporary directory - not
// synthetic in-memory strings handed straight to ParseFbdContent() the way
// the portable tests/test_fbd_loader.cpp suite already does. This is
// deliberately a different layer: test_fbd_loader.cpp already covers every
// FbdErrorCode variation exhaustively at the parse level (no I/O at all);
// this file proves the Win32 file-I/O boundary above it - GetFileAttributesW/
// fopen/fseek/fread and the size cap - behaves correctly against a genuine
// file on disk, without linking any of main.cpp's GUI code.
//
// "Snapshot the live in-memory document before a failed load, assert it's
// unchanged afterward" and "assert autosave is not modified by failed
// loading" require access to main.cpp's own globals (g_entries, etc.), which
// are declared `static` (internal linkage) and therefore unreachable from
// this separate translation unit - those two properties are instead proven
// directly inside main.cpp's own FBM_BUILDING_TESTS block (see the
// "File loading failures leave the live document and autosave untouched"
// TEST_SUITE there), compiled into this same executable.
#include "../doctest_setup.h"
#include "../doctest.h"

// <windows.h> defines min/max as macros unless this is set first, which
// silently mangles any std::min/std::max call in FishBalanceCore.h into a
// broken expression before the compiler ever sees it as a function call -
// main.cpp defines this itself (see its own top-of-file comment), but this
// is a separate translation unit that doesn't include main.cpp, so it needs
// its own copy of the same fix.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>

#include "../../FishBalanceCore.h"
#include "../../FishBalanceWin32IO.h"

namespace {

// A fresh, uniquely-named, empty directory under the system temp path,
// removed (non-recursively - every test below writes at most one flat file
// into it) when the guard goes out of scope.
struct TempDir {
    std::wstring path;
    TempDir() {
        wchar_t base[MAX_PATH];
        GetTempPathW(MAX_PATH, base);
        wchar_t unique[MAX_PATH];
        GetTempFileNameW(base, L"fio", 0, unique);
        DeleteFileW(unique);
        CreateDirectoryW(unique, nullptr);
        path = unique;
    }
    ~TempDir() {
        // Best-effort: remove any file(s) this test wrote, then the directory.
        std::wstring pattern = path + L"\\*";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::wstring name = fd.cFileName;
                if (name == L"." || name == L"..") continue;
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    DeleteFileW((path + L"\\" + name).c_str());
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        RemoveDirectoryW(path.c_str());
    }
    std::wstring file(const wchar_t* name) const { return path + L"\\" + name; }
};

void WriteRawFile(const std::wstring& path, const std::string& bytes) {
    FILE* f = Win32IO_OpenFileW(path, L"wb");
    REQUIRE(f != nullptr);
    if (!bytes.empty()) {
        size_t written = fwrite(bytes.data(), 1, bytes.size(), f);
        REQUIRE(written == bytes.size());
    }
    fclose(f);
}

std::string Utf8(const std::wstring& w) { return WToUtf8(w); }

} // namespace

TEST_SUITE("Windows integration - file loading (Phase 1 item 3)") {

TEST_CASE("missing file is reported as not found, not silently empty") {
    TempDir dir;
    LoadFbdFileResult r = LoadFbdFileContent(dir.file(L"does_not_exist.fbd"));
    CHECK_FALSE(r.ok);
    CHECK(r.errorMessage.find(L"does not exist") != std::wstring::npos);
}

TEST_CASE("a genuinely oversized file is rejected before being fully read") {
    TempDir dir;
    std::wstring path = dir.file(L"oversized.fbd");
    // kMaxReadableFileBytes is 100MB (FishBalanceWin32IO.h) - write one byte
    // past it. This is a real file on a real disk, not a mocked size, so the
    // whole read path (GetFileAttributesW, fopen, fseek/ftell) is exercised
    // exactly as it would be for a genuinely oversized document.
    FILE* f = Win32IO_OpenFileW(path, L"wb");
    REQUIRE(f != nullptr);
    std::string chunk(1024 * 1024, 'A'); // 1MB chunk, written repeatedly
    for (int i = 0; i < 100; i++) {
        REQUIRE(fwrite(chunk.data(), 1, chunk.size(), f) == chunk.size());
    }
    REQUIRE(fwrite("X", 1, 1, f) == 1); // the one byte that pushes it over 100MB
    fclose(f);

    LoadFbdFileResult r = LoadFbdFileContent(path);
    CHECK_FALSE(r.ok);
    CHECK(r.errorMessage.find(L"100MB limit") != std::wstring::npos);
}

TEST_CASE("invalid UTF-8 is rejected rather than silently mangled") {
    TempDir dir;
    std::wstring path = dir.file(L"bad_utf8.fbd");
    // 0xFF is not a valid UTF-8 lead byte in any position. The literal is
    // split after \xFE so MSVC doesn't greedily consume the following 'b'
    // as part of the hex escape (\x takes as many hex digits as follow it,
    // and 'b' is a valid one - \xFEb overflows a char and fails to compile).
    std::string bytes = "DEBTOR=100\nCASH=0\nBEGIN\n\xFF\xFE" "broken\nEND\n";
    WriteRawFile(path, bytes);

    LoadFbdFileResult r = LoadFbdFileContent(path);
    CHECK_FALSE(r.ok);
    CHECK(r.errorMessage.find(L"UTF-8") != std::wstring::npos);
}

TEST_CASE("missing BEGIN/END markers are rejected") {
    TempDir dir;
    std::wstring path = dir.file(L"no_markers.fbd");
    WriteRawFile(path, Utf8(L"DEBTOR=100\nCASH=0\n"));

    LoadFbdFileResult r = LoadFbdFileContent(path);
    CHECK_FALSE(r.ok);
    CHECK(r.errorMessage.find(L"BEGIN") != std::wstring::npos);
}

TEST_CASE("a duplicate BEGIN is rejected") {
    TempDir dir;
    std::wstring path = dir.file(L"dup_begin.fbd");
    WriteRawFile(path, Utf8(L"DEBTOR=0\nCASH=0\nBEGIN\nBEGIN\nEND\n"));

    LoadFbdFileResult r = LoadFbdFileContent(path);
    CHECK_FALSE(r.ok);
}

TEST_CASE("a misordered END before BEGIN is rejected") {
    TempDir dir;
    std::wstring path = dir.file(L"end_before_begin.fbd");
    WriteRawFile(path, Utf8(L"DEBTOR=0\nCASH=0\nEND\nBEGIN\n"));

    LoadFbdFileResult r = LoadFbdFileContent(path);
    CHECK_FALSE(r.ok);
}

TEST_CASE("a malformed data row (wrong field count) is rejected") {
    TempDir dir;
    std::wstring path = dir.file(L"malformed_row.fbd");
    // A real row needs Supplier|Species|Kgs|Price|Date|Notes|Flag (7 fields)
    // - this one is missing several.
    WriteRawFile(path, Utf8(L"DEBTOR=0\nCASH=0\nBEGIN\nOnlyTwo|Fields\nEND\n"));

    LoadFbdFileResult r = LoadFbdFileContent(path);
    CHECK_FALSE(r.ok);
}

TEST_CASE("a non-numeric Kgs field is rejected") {
    TempDir dir;
    std::wstring path = dir.file(L"bad_number.fbd");
    WriteRawFile(path, Utf8(L"DEBTOR=0\nCASH=0\nBEGIN\nSupplier|Species|notANumber|5.0|2026-01-01||0\nEND\n"));

    LoadFbdFileResult r = LoadFbdFileContent(path);
    CHECK_FALSE(r.ok);
}

TEST_CASE("excessive records (over kMaxEntryRecords) are rejected") {
    TempDir dir;
    std::wstring path = dir.file(L"too_many_records.fbd");

    std::string content = "DEBTOR=0\nCASH=0\nBEGIN\n";
    // One row over the documented cap (FishBalanceCore.h's kMaxEntryRecords).
    // A real file with 100,001 short rows - large but well within what a
    // single WriteFile call handles comfortably in a test.
    std::string row = "Supplier|Species|1.0|1.0|2026-01-01||0\n";
    content.reserve(content.size() + row.size() * (kMaxEntryRecords + 1) + 8);
    for (size_t i = 0; i < kMaxEntryRecords + 1; i++) content += row;
    content += "END\n";
    WriteRawFile(path, content);

    LoadFbdFileResult r = LoadFbdFileContent(path);
    CHECK_FALSE(r.ok);
    CHECK(r.errorMessage.find(std::to_wstring(kMaxEntryRecords)) != std::wstring::npos);
}

TEST_CASE("a single line over kMaxLineLength is rejected") {
    TempDir dir;
    std::wstring path = dir.file(L"line_too_long.fbd");
    std::string longNotes(kMaxLineLength + 10, 'x');
    std::string content = "DEBTOR=0\nCASH=0\nBEGIN\nSupplier|Species|1.0|1.0|2026-01-01|" + longNotes + "|0\nEND\n";
    WriteRawFile(path, content);

    LoadFbdFileResult r = LoadFbdFileContent(path);
    CHECK_FALSE(r.ok);
}

TEST_CASE("stray content outside BEGIN/END is rejected") {
    TempDir dir;
    std::wstring path = dir.file(L"stray_content.fbd");
    WriteRawFile(path, Utf8(L"DEBTOR=0\nCASH=0\nsome unexpected line\nBEGIN\nEND\n"));

    LoadFbdFileResult r = LoadFbdFileContent(path);
    CHECK_FALSE(r.ok);
}

TEST_CASE("a valid file with no entries loads successfully") {
    TempDir dir;
    std::wstring path = dir.file(L"empty_but_valid.fbd");
    WriteRawFile(path, Utf8(L"DEBTOR=0\nCASH=0\nBEGIN\nEND\n"));

    LoadFbdFileResult r = LoadFbdFileContent(path);
    REQUIRE(r.ok);
    CHECK(r.parsed.entries.empty());
}

TEST_CASE("a valid file with entries round-trips through the real file system") {
    TempDir dir;
    std::wstring path = dir.file(L"valid.fbd");
    WriteRawFile(path, Utf8(
        L"DEBTOR=100\nCASH=50\nBEGIN\n"
        L"Acme Fish|Salmon|10.5000|8.0000|2026-09-30|fresh|0\n"
        L"END\n"));

    LoadFbdFileResult r = LoadFbdFileContent(path);
    REQUIRE(r.ok);
    REQUIRE(r.parsed.entries.size() == 1);
    CHECK(r.parsed.entries[0].supplier == L"Acme Fish");
    CHECK(r.parsed.entries[0].product == L"Salmon");
    CHECK(r.parsed.debtor == L"100");
    CHECK(r.parsed.cash == L"50");
}

} // TEST_SUITE
