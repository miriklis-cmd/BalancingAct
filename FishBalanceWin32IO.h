// FishBalanceWin32IO.h
//
// Test-automation refactor (2026-09-30, v0.9.51): the Win32-specific file
// I/O half of the app - reading, atomically writing, and loading/decoding
// .fbd content - extracted out of main.cpp into its own header so a
// separate Windows integration-test executable (tests/win32_integration/)
// can exercise it against real temporary files, without linking any of the
// GUI/window code in main.cpp at all. This is NOT a portable header the way
// FishBalanceCore.h is - it uses <windows.h>/<cstdio> Win32 file APIs
// directly - but it also has ZERO dependency on any window, control, or
// global app state, so it's the natural boundary for "real file I/O
// integration tests" that don't need a GUI.
//
// Every low-level file operation (open/read/write/flush/close/move/delete)
// is routed through a small, overridable "ops" struct defaulting to the
// real Win32/CRT call - this is what lets an integration test inject a
// deterministic failure at a specific step (Phase 1 item 4: "inject
// deterministic open/write/flush/replace failures instead of relying on
// antivirus or disk-full conditions") instead of only being able to test
// the happy path. main.cpp itself never passes an override, so its
// behavior is completely unchanged by this refactor - every function here
// has the exact same signature and exact same default behavior it had
// inline in main.cpp before this version.
#pragma once

#include <windows.h>
#include <string>
#include <cstdio>
#include <functional>

#include "FishBalanceCore.h"

// ---------------------------------------------------------------------------
// File-open helper (moved from main.cpp unchanged)
// ---------------------------------------------------------------------------

// Every file this app opens goes through here. Under MSVC, this genuinely
// uses the safer _wfopen_s (it validates its arguments and reports errors
// through its return code, unlike plain _wfopen) rather than just
// suppressing MSVC's deprecation warning - an actual fix, not a silenced
// one. _wfopen_s is a Microsoft-only CRT extension not available on
// MinGW, which is why this is gated: MinGW keeps using the plain, already
// memory-safe standard _wfopen (every call site here passes a fixed
// literal mode string, never attacker-influenced data), and never raised
// this warning in the first place since it's specifically an MSVC CRT
// header annotation, not a general compiler diagnostic.
inline FILE* Win32IO_OpenFileW(const std::wstring& path, const wchar_t* mode) {
#ifdef _MSC_VER
    FILE* f = nullptr;
    errno_t err = _wfopen_s(&f, path.c_str(), mode);
    return (err == 0) ? f : nullptr;
#else
    return _wfopen(path.c_str(), mode);
#endif
}

// ---------------------------------------------------------------------------
// UTF-8 <-> UTF-16 conversion (moved from main.cpp unchanged, v0.9.51 - kept
// here rather than duplicated so the integration test suite, which links
// this header without main.cpp, has the exact same conversion behavior)
// ---------------------------------------------------------------------------

inline std::string WToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}

// Phase 1 / F4 audit remediation (2026-09-30): decodes with MB_ERR_INVALID_CHARS
// so a byte sequence that isn't valid UTF-8 is reported via outOk, rather
// than silently decoded with lossy U+FFFD replacement characters (the old,
// flag-less behavior) - which could quietly corrupt a Supplier/Species name,
// a Debtor/Cash figure, or a recorded file path with no visible sign
// anything had gone wrong. outOk is set true for empty input (an empty file
// is not a decode error) and for any fully-valid, non-empty input; false
// only when the bytes genuinely aren't valid UTF-8. outOk may be nullptr for
// callers that don't need to distinguish "empty" from "invalid" - they
// already treated invalid input as producing whatever garbage MultiByteToWideChar
// felt like producing, so an empty result on failure is a strictly safer
// regression than what they had before.
inline std::wstring Utf8ToW(const std::string& s, bool* outOk = nullptr) {
    if (outOk) *outOk = true;
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), nullptr, 0);
    if (len == 0) {
        if (outOk) *outOk = false;
        return {};
    }
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

// ---------------------------------------------------------------------------
// Read side: ReadBytesError / ReadAllBytes / ReadAllLines
// ---------------------------------------------------------------------------

// Phase 1 completeness follow-up (2026-09-30, v0.9.50): ReadAllBytes used to
// return a bare bool, so every caller (and the user, via a generic "could
// not open" message) had no way to tell a missing file from a locked one,
// from one too large, from an actual disk I/O failure. This is the read-side
// half of the structured diagnostic surface; FbdErrorCode in
// FishBalanceCore.h is the content/parse-side half, for problems found only
// after the bytes are already in hand.
enum class ReadBytesError {
    Ok = 0,
    NotFound,
    OpenFailure,
    SeekFailure,
    SizeQueryFailure,
    TooLarge,
    AllocationFailure,
    ShortRead,
    IoFailure,
};

static const long kMaxReadableFileBytes = 100L * 1024 * 1024; // 100MB - far beyond any realistic file this app writes

inline std::wstring DescribeReadBytesError(ReadBytesError err) {
    switch (err) {
        case ReadBytesError::Ok: return L"";
        case ReadBytesError::NotFound: return L"the file does not exist";
        case ReadBytesError::OpenFailure: return L"the file could not be opened (it may be locked by another program, or access may be denied)";
        case ReadBytesError::SeekFailure: return L"could not seek within the file";
        case ReadBytesError::SizeQueryFailure: return L"could not determine the file's size";
        case ReadBytesError::TooLarge: return L"the file is larger than the " + std::to_wstring(kMaxReadableFileBytes / (1024 * 1024)) + L"MB limit this program supports";
        case ReadBytesError::AllocationFailure: return L"not enough memory was available to read the file";
        case ReadBytesError::ShortRead: return L"the file could not be fully read - it may have been modified or truncated while being read";
        case ReadBytesError::IoFailure: return L"a read error occurred while reading the file";
    }
    return L"an unknown read error occurred"; // unreachable; keeps -Wall/-Wextra happy on an exhaustive-looking enum
}

// Test-automation refactor (v0.9.51): every low-level step ReadAllBytes
// takes, as an overridable port. Defaults reproduce the exact real behavior
// this function has always had; passing a non-null `ops` lets an
// integration test force a specific step to fail deterministically (e.g.
// "the seek succeeds but the read comes back short") without needing a
// real disk-full or antivirus-lock condition.
struct FileReadOps {
    std::function<DWORD(const std::wstring&)> getFileAttributes = [](const std::wstring& path) {
        return GetFileAttributesW(path.c_str());
    };
    std::function<FILE*(const std::wstring&)> open = [](const std::wstring& path) {
        return Win32IO_OpenFileW(path, L"rb");
    };
};

// Returns ReadBytesError::Ok (with the file's full content in outData) on
// success, or a specific failure code (with outData cleared) otherwise - see
// ReadBytesError/DescribeReadBytesError above. Phase 1 completeness follow-up
// (2026-09-30, v0.9.50): the byte-buffer allocation (outData.assign) is now
// wrapped so a std::bad_alloc/std::length_error from an attacker-controlled
// or corrupted size value is reported as ReadBytesError::AllocationFailure
// rather than crashing the process - deliberately catching only these two
// specific, expected exception types, never a broad catch(...) that could
// mask a genuine programming defect elsewhere in this function.
inline ReadBytesError ReadAllBytes(const std::wstring& path, std::string& outData, const FileReadOps* ops = nullptr) {
    static const FileReadOps kDefaultOps;
    const FileReadOps& io = ops ? *ops : kDefaultOps;

    outData.clear();
    DWORD attrs = io.getFileAttributes(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return ReadBytesError::NotFound;
    FILE* f = io.open(path);
    if (!f) return ReadBytesError::OpenFailure;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return ReadBytesError::SeekFailure; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return ReadBytesError::SizeQueryFailure; }
    if (sz > kMaxReadableFileBytes) { fclose(f); return ReadBytesError::TooLarge; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return ReadBytesError::SeekFailure; }
    if (sz > 0) {
        try {
            outData.assign((size_t)sz, '\0');
        } catch (const std::bad_alloc&) {
            fclose(f);
            return ReadBytesError::AllocationFailure;
        } catch (const std::length_error&) {
            fclose(f);
            return ReadBytesError::AllocationFailure;
        }
        size_t got = fread(&outData[0], 1, (size_t)sz, f);
        if (ferror(f)) {
            fclose(f);
            outData.clear();
            return ReadBytesError::IoFailure;
        }
        if (got != (size_t)sz) {
            fclose(f);
            outData.clear();
            return ReadBytesError::ShortRead;
        }
    }
    fclose(f);
    return ReadBytesError::Ok;
}

// Reads a whole text file (UTF-8) into a list of lines. Returns false (with
// an empty list) if the file doesn't exist, can't be fully read, or isn't
// valid UTF-8 - a missing file is expected on first run, the others mean
// something is genuinely wrong and the caller should not trust a partial
// result. Structured ReadBytesError/UTF-8-validity diagnostics aren't
// surfaced to the user for these companion files (settings.txt/recent.txt/
// emails.txt) the way they are for .fbd loading - see LoadFromFile - since
// none of their callers currently show the user anything beyond falling
// back to defaults; kept as a plain bool here rather than threading a
// diagnostic through call sites that would never display it.
inline bool ReadAllLines(const std::wstring& path, std::vector<std::wstring>& outLines) {
    outLines.clear();
    std::string data;
    if (ReadAllBytes(path, data) != ReadBytesError::Ok) return false;

    bool utf8Ok = true;
    std::wstring all;
    try {
        all = Utf8ToW(data, &utf8Ok);
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    if (!utf8Ok) return false;

    std::wstring cur;
    for (wchar_t c : all) {
        if (c == L'\n') {
            if (!cur.empty() && cur.back() == L'\r') cur.pop_back();
            outLines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) outLines.push_back(cur);
    return true;
}

// ---------------------------------------------------------------------------
// Write side: WriteFileAtomicUtf8
// ---------------------------------------------------------------------------

// Test-automation refactor (v0.9.51): every low-level step the atomic
// writer takes, as an overridable port - open, write, error-check, flush,
// close, the atomic rename, and the best-effort temp-file cleanup. Defaults
// reproduce the exact real behavior this function has always had. An
// integration test injects a specific failing step (e.g. "the write
// succeeds but the flush fails") to prove the destination is left
// completely untouched and no temp file is left behind, per Phase 1 item 4 -
// without needing a real disk-full or antivirus-lock condition.
struct FileWriteOps {
    std::function<FILE*(const std::wstring&)> open = [](const std::wstring& path) {
        return Win32IO_OpenFileW(path, L"wb");
    };
    std::function<size_t(const void*, size_t, FILE*)> write = [](const void* data, size_t size, FILE* f) {
        return fwrite(data, 1, size, f);
    };
    std::function<bool(FILE*)> hasError = [](FILE* f) { return ferror(f) != 0; };
    std::function<int(FILE*)> flush = [](FILE* f) { return fflush(f); };
    std::function<int(FILE*)> close = [](FILE* f) { return fclose(f); };
    std::function<bool(const std::wstring&, const std::wstring&)> moveReplace =
        [](const std::wstring& from, const std::wstring& to) {
            return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        };
    std::function<void(const std::wstring&)> remove = [](const std::wstring& path) { _wremove(path.c_str()); };
};

// Writes `utf8Content` to `path` as safely as this app can manage: write to
// a temporary file in the SAME directory first (so the final rename is on
// the same volume, which is required for it to be atomic rather than a
// slow, interruptible copy+delete), checking every write/flush/close along
// the way, and only atomically swap it into place if every step succeeded.
//
// If anything fails partway through - disk full, the destination locked by
// another program, a crash - the temporary file is cleaned up and the REAL
// destination is left completely untouched, exactly as it was before this
// call. See SecurityHardeningRegister.md for the full writeup.
//
// Returns true only if the entire operation succeeded. On failure, if
// outError is non-null, it's filled with a short, human-readable reason
// suitable for showing directly to the user.
inline bool WriteFileAtomicUtf8(const std::wstring& path, const std::string& utf8Content,
        std::wstring* outError = nullptr, const FileWriteOps* ops = nullptr) {
    static const FileWriteOps kDefaultOps;
    const FileWriteOps& io = ops ? *ops : kDefaultOps;

    auto fail = [&](const wchar_t* reason) {
        if (outError) *outError = reason;
        return false;
    };

    std::wstring tempPath = path + L".tmp";
    FILE* f = io.open(tempPath);
    if (!f) return fail(L"could not create a temporary file for saving (check the destination folder is writable)");

    bool writeOk = true;
    if (!utf8Content.empty()) {
        size_t written = io.write(utf8Content.data(), utf8Content.size(), f);
        if (written != utf8Content.size()) writeOk = false;
    }
    if (writeOk && io.hasError(f)) writeOk = false;
    // Flush the C library's own buffer to the OS now, rather than waiting
    // for fclose() to do it implicitly - lets us check the result
    // explicitly instead of only learning about a failure from fclose().
    if (writeOk && io.flush(f) != 0) writeOk = false;

    int closeResult = io.close(f);
    if (closeResult != 0) writeOk = false;

    if (!writeOk) {
        io.remove(tempPath); // best-effort cleanup; a leftover .tmp file is harmless either way
        return fail(L"writing the file failed partway through - the disk may be full, or the file is locked by another program");
    }

    // Atomically swap the fully-written temp file into place. If this fails,
    // the temp file is cleaned up and the REAL destination is untouched -
    // whatever was there before (if anything) is still exactly as it was.
    if (!io.moveReplace(tempPath, path)) {
        io.remove(tempPath);
        return fail(L"could not replace the destination file - it may be open in another program, or read-only");
    }
    return true;
}

// ---------------------------------------------------------------------------
// LoadFromFileContent: the platform-boundary half of loading a .fbd file -
// read bytes, decode UTF-8, parse. Does NOT touch any app global state
// (g_entries, g_currentFile, etc.) - that commit step stays in main.cpp's
// own LoadFromFile wrapper, which calls this and then applies the result.
// Separated out specifically so the integration test suite can exercise
// "read a real file from disk and get a structured result" without linking
// any GUI code at all.
// ---------------------------------------------------------------------------

struct LoadFbdFileResult {
    bool ok = false;
    FbdLoadResult parsed; // only meaningful when ok is true
    std::wstring errorMessage; // only meaningful when ok is false
};

inline LoadFbdFileResult LoadFbdFileContent(const std::wstring& path, const FileReadOps* readOps = nullptr) {
    LoadFbdFileResult out;
    std::string data;
    ReadBytesError readErr = ReadAllBytes(path, data, readOps);
    if (readErr != ReadBytesError::Ok) {
        out.errorMessage = DescribeReadBytesError(readErr);
        return out;
    }

    bool utf8Ok = true;
    std::wstring all;
    try {
        all = Utf8ToW(data, &utf8Ok);
    } catch (const std::bad_alloc&) {
        out.errorMessage = L"not enough memory was available to decode the file";
        return out;
    } catch (const std::length_error&) {
        out.errorMessage = L"not enough memory was available to decode the file";
        return out;
    }
    if (!utf8Ok) {
        out.errorMessage = L"the file is not valid UTF-8 text";
        return out;
    }

    FbdLoadResult result;
    try {
        result = ParseFbdContent(all);
    } catch (const std::bad_alloc&) {
        out.errorMessage = L"not enough memory was available to parse the file";
        return out;
    } catch (const std::length_error&) {
        out.errorMessage = L"not enough memory was available to parse the file";
        return out;
    }
    if (!result.ok) {
        out.errorMessage = result.errorMessage;
        if (result.errorLine > 0) out.errorMessage += L" (line " + std::to_wstring(result.errorLine) + L")";
        return out;
    }

    out.ok = true;
    out.parsed = result;
    return out;
}
