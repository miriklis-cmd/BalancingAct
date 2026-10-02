// Windows integration tests - Save / Save As deterministic fault injection
// (Phase 1 item 4).
//
// Exercises FishBalanceWin32IO.h's WriteFileAtomicUtf8() against a REAL
// destination file in a real temporary directory, injecting a failure at
// each individual step (open/write/flush/close/replace) via its FileWriteOps
// port - rather than trying to reproduce a genuine disk-full or antivirus-
// lock condition, which can't be made to happen deterministically in an
// automated test. Every case proves the same two properties Jack's spec
// calls for: the real destination file is left byte-for-byte exactly as it
// was before the call (or absent, if it didn't exist), and no temp/partial
// file is left behind afterward.
//
// CoordinateSaveAs()'s own pure-function behavior (restoring g_currentFile/
// title state after a failed Save As) is already covered by the portable
// tests/test_recovery_and_finalize.cpp suite, which needs no real file I/O
// at all - this file is the layer below it: the actual bytes on disk.
#include "../doctest_setup.h"
#include "../doctest.h"

// See test_io_integration.cpp's identical comment - this translation unit
// doesn't include main.cpp (which defines this itself), so it needs its own
// copy of the same std::min/std::max-vs-windows.h-macro fix.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>

#include "../../FishBalanceWin32IO.h"

namespace {

struct TempDir {
    std::wstring path;
    TempDir() {
        wchar_t base[MAX_PATH];
        GetTempPathW(MAX_PATH, base);
        wchar_t unique[MAX_PATH];
        GetTempFileNameW(base, L"fsf", 0, unique);
        DeleteFileW(unique);
        CreateDirectoryW(unique, nullptr);
        path = unique;
    }
    ~TempDir() {
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

bool ReadWholeFile(const std::wstring& path, std::string& out) {
    return ReadAllBytes(path, out) == ReadBytesError::Ok;
}

bool FileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

} // namespace

TEST_SUITE("Windows integration - Save/Save As fault injection (Phase 1 item 4)") {

TEST_CASE("a pre-existing destination is untouched when the temp file can't be opened") {
    TempDir dir;
    std::wstring path = dir.file(L"target.fbd");
    // Establish real, pre-existing content via the real (non-injected) path.
    REQUIRE(WriteFileAtomicUtf8(path, "ORIGINAL CONTENT"));

    FileWriteOps ops;
    ops.open = [](const std::wstring&) -> FILE* { return nullptr; }; // simulate open failure

    std::wstring err;
    bool ok = WriteFileAtomicUtf8(path, "NEW CONTENT THAT SHOULD NEVER LAND", &err, &ops);
    CHECK_FALSE(ok);
    CHECK_FALSE(err.empty());

    std::string stillThere;
    REQUIRE(ReadWholeFile(path, stillThere));
    CHECK(stillThere == "ORIGINAL CONTENT");
    CHECK_FALSE(FileExists(path + L".tmp"));
}

TEST_CASE("no destination is created at all when the temp file can't be opened, for a brand-new file") {
    TempDir dir;
    std::wstring path = dir.file(L"never_existed.fbd");
    REQUIRE_FALSE(FileExists(path));

    FileWriteOps ops;
    ops.open = [](const std::wstring&) -> FILE* { return nullptr; };

    std::wstring err;
    bool ok = WriteFileAtomicUtf8(path, "SOMETHING", &err, &ops);
    CHECK_FALSE(ok);
    CHECK_FALSE(FileExists(path));
    CHECK_FALSE(FileExists(path + L".tmp"));
}

TEST_CASE("a short/failed write leaves the destination untouched and cleans up the temp file") {
    TempDir dir;
    std::wstring path = dir.file(L"target.fbd");
    REQUIRE(WriteFileAtomicUtf8(path, "ORIGINAL CONTENT"));

    FileWriteOps ops;
    // Real open/flush/close/remove, but the write itself always reports 0
    // bytes written - a short write, exactly like a disk-full condition
    // partway through, without needing to actually fill a real disk.
    ops.write = [](const void*, size_t, FILE*) -> size_t { return 0; };

    std::wstring err;
    bool ok = WriteFileAtomicUtf8(path, "NEW CONTENT THAT SHOULD NEVER LAND", &err, &ops);
    CHECK_FALSE(ok);
    CHECK_FALSE(err.empty());

    std::string stillThere;
    REQUIRE(ReadWholeFile(path, stillThere));
    CHECK(stillThere == "ORIGINAL CONTENT");
    CHECK_FALSE(FileExists(path + L".tmp"));
}

TEST_CASE("a reported write error (ferror) fails the save even if the byte count matched") {
    TempDir dir;
    std::wstring path = dir.file(L"target.fbd");
    REQUIRE(WriteFileAtomicUtf8(path, "ORIGINAL CONTENT"));

    FileWriteOps ops;
    ops.hasError = [](FILE*) { return true; }; // force the post-write error check to fail

    std::wstring err;
    bool ok = WriteFileAtomicUtf8(path, "NEW CONTENT", &err, &ops);
    CHECK_FALSE(ok);

    std::string stillThere;
    REQUIRE(ReadWholeFile(path, stillThere));
    CHECK(stillThere == "ORIGINAL CONTENT");
    CHECK_FALSE(FileExists(path + L".tmp"));
}

TEST_CASE("a flush failure fails the save and cleans up the temp file") {
    TempDir dir;
    std::wstring path = dir.file(L"target.fbd");
    REQUIRE(WriteFileAtomicUtf8(path, "ORIGINAL CONTENT"));

    FileWriteOps ops;
    ops.flush = [](FILE*) { return -1; }; // simulate fflush failure

    std::wstring err;
    bool ok = WriteFileAtomicUtf8(path, "NEW CONTENT", &err, &ops);
    CHECK_FALSE(ok);

    std::string stillThere;
    REQUIRE(ReadWholeFile(path, stillThere));
    CHECK(stillThere == "ORIGINAL CONTENT");
    CHECK_FALSE(FileExists(path + L".tmp"));
}

TEST_CASE("a replace (rename) failure leaves both the destination and the temp file exactly as they were") {
    TempDir dir;
    std::wstring path = dir.file(L"target.fbd");
    REQUIRE(WriteFileAtomicUtf8(path, "ORIGINAL CONTENT"));

    FileWriteOps ops;
    ops.moveReplace = [](const std::wstring&, const std::wstring&) { return false; }; // simulate MoveFileExW failure
    // remove() left as the real default, so the temp-file cleanup this
    // triggers is exercised for real, not just assumed.

    std::wstring err;
    bool ok = WriteFileAtomicUtf8(path, "NEW CONTENT THAT SHOULD NEVER LAND", &err, &ops);
    CHECK_FALSE(ok);
    CHECK_FALSE(err.empty());

    std::string stillThere;
    REQUIRE(ReadWholeFile(path, stillThere));
    CHECK(stillThere == "ORIGINAL CONTENT");
    CHECK_FALSE(FileExists(path + L".tmp")); // cleaned up even though the replace itself failed
}

TEST_CASE("every injected step succeeding (the control case) actually writes the new content") {
    TempDir dir;
    std::wstring path = dir.file(L"target.fbd");
    REQUIRE(WriteFileAtomicUtf8(path, "ORIGINAL CONTENT"));

    FileWriteOps ops; // every field left at its real default
    std::wstring err;
    bool ok = WriteFileAtomicUtf8(path, "REPLACED CONTENT", &err, &ops);
    CHECK(ok);
    CHECK(err.empty());

    std::string content;
    REQUIRE(ReadWholeFile(path, content));
    CHECK(content == "REPLACED CONTENT");
    CHECK_FALSE(FileExists(path + L".tmp"));
}

} // TEST_SUITE
