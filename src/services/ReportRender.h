// ReportRender.h - renders the Breakdown report to bitmaps (services layer).
//
// v0.10.5 (main.cpp split stage 4b). Shared by Print Preview and real
// printing so the two can never show different content. The only change from
// the old in-main.cpp version: RenderReportPages() is given the Debtor and
// Cash text as parameters instead of reading the edit boxes itself.
#pragma once

#include "../PlatformWin.h"
#include <functional>
#include <string>
#include <vector>

// A rendered report page: a device-independent bitmap (DIB), so the exact
// same rendering can be shown on screen (Print Preview) and sent to a real
// printer (StretchDIBits) without redrawing or risking the two diverging.
struct RenderedPage {
    HBITMAP bitmap = nullptr;
    void* bits = nullptr; // owned by the DIB section behind `bitmap`, not separately freed
    int width = 0, height = 0;
};

// Minimal RAII wrappers for owned GDI handles (Phase 2 / F11) - deliberately
// narrow (only what this file's printing/preview code needs), not a
// general-purpose GDI abstraction. Each guarantees DeleteObject/DeleteDC is
// called exactly once, including on an early-return error path, without a
// cleanup call at every return site.
struct UniqueFont {
    HFONT h = nullptr;
    UniqueFont() = default;
    explicit UniqueFont(HFONT f) : h(f) {}
    ~UniqueFont() { reset(); }
    UniqueFont(const UniqueFont&) = delete;
    UniqueFont& operator=(const UniqueFont&) = delete;
    UniqueFont(UniqueFont&& o) noexcept : h(o.h) { o.h = nullptr; }
    UniqueFont& operator=(UniqueFont&& o) noexcept { if (this != &o) { reset(); h = o.h; o.h = nullptr; } return *this; }
    void reset() { if (h) { DeleteObject(h); h = nullptr; } }
    HFONT get() const { return h; }
    explicit operator bool() const { return h != nullptr; }
};

struct UniquePen {
    HPEN h = nullptr;
    UniquePen() = default;
    explicit UniquePen(HPEN p) : h(p) {}
    ~UniquePen() { reset(); }
    UniquePen(const UniquePen&) = delete;
    UniquePen& operator=(const UniquePen&) = delete;
    UniquePen(UniquePen&& o) noexcept : h(o.h) { o.h = nullptr; }
    UniquePen& operator=(UniquePen&& o) noexcept { if (this != &o) { reset(); h = o.h; o.h = nullptr; } return *this; }
    void reset() { if (h) { DeleteObject(h); h = nullptr; } }
    HPEN get() const { return h; }
    explicit operator bool() const { return h != nullptr; }
};

struct UniqueDC {
    HDC h = nullptr;
    UniqueDC() = default;
    explicit UniqueDC(HDC d) : h(d) {}
    ~UniqueDC() { reset(); }
    UniqueDC(const UniqueDC&) = delete;
    UniqueDC& operator=(const UniqueDC&) = delete;
    UniqueDC(UniqueDC&& o) noexcept : h(o.h) { o.h = nullptr; }
    UniqueDC& operator=(UniqueDC&& o) noexcept { if (this != &o) { reset(); h = o.h; o.h = nullptr; } return *this; }
    void reset(HDC d = nullptr) { if (h) DeleteDC(h); h = d; }
    HDC get() const { return h; }
    explicit operator bool() const { return h != nullptr; }
};

// Selects `obj` into `dc` and restores the previous object on destruction -
// pairs with the Unique* wrappers above so a caller never has to remember
// to deselect a font/pen from a DC before deleting it. Only ever touches
// the DC's current selection, not the lifetime of `dc` or `obj` themselves.
struct ScopedSelect {
    HDC dc = nullptr;
    HGDIOBJ old = nullptr;
    ScopedSelect(HDC d, HGDIOBJ obj) : dc(d) {
        if (dc && obj) old = SelectObject(dc, obj);
    }
    ~ScopedSelect() { if (dc && old) SelectObject(dc, old); }
    ScopedSelect(const ScopedSelect&) = delete;
    ScopedSelect& operator=(const ScopedSelect&) = delete;
};

struct RenderReportResult {
    std::vector<RenderedPage> pages; // empty when a per-page callback consumed every page (printing)
    bool ok = false;
    bool pageCapHit = false;
    std::wstring error; // meaningful only when ok == false
};

void FreeRenderedPages(std::vector<RenderedPage>& pages);

// Renders the Breakdown report (current sheet, g_entries) into DIB pages.
// debtorText/cashText are the raw contents of the Debtor and Cash boxes.
// See ReportRender.cpp for the full behaviour of `onPageReady`.
RenderReportResult RenderReportPages(int pageWidthPx, int pageHeightPx, int dpiX, int dpiY,
                                      const std::wstring& debtorText, const std::wstring& cashText,
                                      const std::function<bool(RenderedPage&)>& onPageReady = {});
