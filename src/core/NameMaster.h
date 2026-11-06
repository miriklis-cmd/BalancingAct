// NameMaster.h - the master Supplier / Species name lists (core layer, no Windows calls).
//
// v0.13.0. Before this, the Supplier and Species drop-downs were rebuilt from whatever names the open sheet
// happened to contain, so every new sheet started empty and spelling variants ("Barra", "barra ", "Barramundi")
// crept in and had to be merged by hand. The master list is kept in names.txt beside the exe and holds:
//   - the canonical names, each with the date it was added (so Manage Names can show recent additions),
//   - aliases: "Barra" -> "Barramundi", remembered when names are merged so the old spelling is corrected
//     automatically the next time it is typed.
//
// Everything here is pure (strings in, strings out) so it is covered by tests/test_name_master.cpp. Reading and
// writing names.txt is src/data/Storage.cpp; the prompts are src/forms/ManageNamesWindow.cpp.
#pragma once

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <map>
#include <string>
#include <vector>

#include "FishBalanceCore.h"

// One kind of name (suppliers or species).
struct NameList {
    std::vector<std::wstring> names;               // canonical spellings, in the order they were added
    std::map<std::wstring, std::wstring> aliases;  // old spelling -> canonical name it was merged into
    std::map<std::wstring, std::wstring> added;    // canonical name -> "YYYY-MM-DD" (missing/empty = unknown)
};

struct NameMasterData {
    NameList suppliers;
    NameList species;
    bool fileExisted = false; // false until names.txt has been read or created (drives one-time seeding)
};

// Trims and collapses any run of white space inside the name to one space.
inline std::wstring CollapseNameSpaces(const std::wstring& s) {
    std::wstring out;
    bool pendingSpace = false;
    for (wchar_t c : s) {
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
            pendingSpace = !out.empty();
        } else {
            if (pendingSpace) out += L' ';
            pendingSpace = false;
            out += c;
        }
    }
    return out;
}

// Comparison key: lower case letters and digits only, so "Port Fish Co." == "port fish co" == "PortFishCo".
inline std::wstring NameKey(const std::wstring& s) {
    std::wstring k;
    for (wchar_t c : s) {
        if (std::iswalnum((wint_t)c)) k += (wchar_t)std::towlower((wint_t)c);
    }
    return k;
}

// Optimal-string-alignment distance (insert, delete, substitute, swap two neighbours each cost 1), so a
// transposition typo such as "Snpaer" counts as one slip.
inline int NameEditDistance(const std::wstring& a, const std::wstring& b) {
    const size_t n = a.size(), m = b.size();
    std::vector<std::vector<int>> d(n + 1, std::vector<int>(m + 1, 0));
    for (size_t i = 0; i <= n; i++) d[i][0] = (int)i;
    for (size_t j = 0; j <= m; j++) d[0][j] = (int)j;
    for (size_t i = 1; i <= n; i++) {
        for (size_t j = 1; j <= m; j++) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int best = std::min(std::min(d[i - 1][j] + 1, d[i][j - 1] + 1), d[i - 1][j - 1] + cost);
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) best = std::min(best, d[i - 2][j - 2] + 1);
            d[i][j] = best;
        }
    }
    return d[n][m];
}

inline bool NameListHas(const NameList& list, const std::wstring& name) {
    return std::find(list.names.begin(), list.names.end(), name) != list.names.end();
}

// Adds a canonical name (no-op if present). An alias with the same key is dropped: adding the name on purpose
// overrides an earlier merge. Returns true if the name was new.
inline bool NameListAdd(NameList& list, const std::wstring& name, const std::wstring& dateISO) {
    if (name.empty() || NameListHas(list, name)) return false;
    list.names.push_back(name);
    list.added[name] = dateISO;
    const std::wstring key = NameKey(name);
    for (auto it = list.aliases.begin(); it != list.aliases.end();) {
        if (NameKey(it->first) == key) it = list.aliases.erase(it); else ++it;
    }
    return true;
}

// v0.14.0: alias editing from Manage Names.
enum class AliasAddResult { Added, Empty, SameAsName, IsAName, NoSuchName };

// Makes `alias` another spelling of `canonical`. Refuses an empty alias, one that is just the name again (same key),
// or one that is already a real name in the list (merge those instead). An existing alias with the same key is
// replaced, so this also re-points an alias.
inline AliasAddResult NameListAddAlias(NameList& list, const std::wstring& aliasText, const std::wstring& canonical) {
    const std::wstring alias = CollapseNameSpaces(aliasText);
    const std::wstring key = NameKey(alias);
    if (key.empty()) return AliasAddResult::Empty;
    if (!NameListHas(list, canonical)) return AliasAddResult::NoSuchName;
    if (key == NameKey(canonical)) return AliasAddResult::SameAsName;
    for (const std::wstring& n : list.names) {
        if (NameKey(n) == key) return AliasAddResult::IsAName;
    }
    for (auto it = list.aliases.begin(); it != list.aliases.end();) {
        if (NameKey(it->first) == key) it = list.aliases.erase(it); else ++it;
    }
    list.aliases[alias] = canonical;
    return AliasAddResult::Added;
}

// Removes the alias with this spelling (compared by key). Returns true if one was removed.
inline bool NameListRemoveAlias(NameList& list, const std::wstring& aliasText) {
    const std::wstring key = NameKey(aliasText);
    bool removed = false;
    for (auto it = list.aliases.begin(); it != list.aliases.end();) {
        if (!key.empty() && NameKey(it->first) == key) { it = list.aliases.erase(it); removed = true; }
        else ++it;
    }
    return removed;
}

// The aliases that point at `canonical`, in alphabetical order.
inline std::vector<std::wstring> AliasesOf(const NameList& list, const std::wstring& canonical) {
    std::vector<std::wstring> out;
    for (const auto& kv : list.aliases) {
        if (kv.second == canonical) out.push_back(kv.first);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Removes a canonical name and any alias that pointed at it.
inline void NameListRemove(NameList& list, const std::wstring& name) {
    list.names.erase(std::remove(list.names.begin(), list.names.end(), name), list.names.end());
    list.added.erase(name);
    for (auto it = list.aliases.begin(); it != list.aliases.end();) {
        if (it->second == name) it = list.aliases.erase(it); else ++it;
    }
}

// Folds every name in `sources` into `target`: the sources leave the list, the target is added if it was not
// there, and each source becomes an alias of the target (unless it differs from the target only by case,
// spacing or punctuation - snapping already handles that). Aliases that pointed at a source are re-pointed.
inline void NameListMerge(NameList& list, const std::vector<std::wstring>& sources, const std::wstring& target,
                          const std::wstring& dateISO) {
    const std::wstring targetKey = NameKey(target);
    for (const std::wstring& src : sources) {
        if (src == target) continue;
        list.names.erase(std::remove(list.names.begin(), list.names.end(), src), list.names.end());
        list.added.erase(src);
        for (auto& kv : list.aliases) {
            if (kv.second == src) kv.second = target;
        }
    }
    NameListAdd(list, target, dateISO);
    for (const std::wstring& src : sources) {
        if (src == target || NameKey(src) == targetKey) continue;
        list.aliases[src] = target;
    }
    // Drop aliases that would now map a name to itself.
    for (auto it = list.aliases.begin(); it != list.aliases.end();) {
        if (NameKey(it->first) == NameKey(it->second)) it = list.aliases.erase(it); else ++it;
    }
}

enum class NameMatch {
    Exact,   // already in the list, spelled exactly like this
    Snapped, // same name apart from case/spacing/punctuation - use the list's spelling
    Alias,   // an old spelling that was merged away - use what it was merged into
    Near,    // not in the list but close to something that is - ask
    New      // nothing similar - add it
};

struct NameResolution {
    NameMatch kind = NameMatch::New;
    std::wstring name;                // what to use (the cleaned input for Near/New/Exact)
    std::vector<std::wstring> similar;   // Near only: the closest existing names, best first (at most 3)
    std::vector<std::wstring> similarVia; // parallel to `similar`: the alias that matched ("" if the name itself matched)
};

// Decides what a typed name means against the list. Never modifies the list.
inline NameResolution ResolveName(const NameList& list, const std::wstring& typed) {
    NameResolution r;
    r.name = CollapseNameSpaces(typed);
    if (NameListHas(list, r.name)) { r.kind = NameMatch::Exact; return r; }
    const std::wstring key = NameKey(r.name);
    if (key.empty()) return r; // nothing to compare; treated as New

    for (const std::wstring& n : list.names) {
        if (NameKey(n) == key) { r.kind = NameMatch::Snapped; r.name = n; return r; }
    }
    for (const auto& kv : list.aliases) {
        if (NameKey(kv.first) == key && NameListHas(list, kv.second)) {
            r.kind = NameMatch::Alias;
            r.name = kv.second;
            return r;
        }
    }

    // Near: one is the start of the other (Barra / Barramundi), or only a letter or two apart.
    const int maxDist = key.size() >= 6 ? 2 : (key.size() >= 4 ? 1 : 0);
    // Aliases count too: "Blk flat" is the start of the alias "Blk Flathead", so it means Black Flathead.
    struct Hit { int score; int viaRank; std::wstring name; std::wstring via; };
    std::vector<Hit> hits;
    auto consider = [&](const std::wstring& candidateText, const std::wstring& canonical, const std::wstring& via) {
        const std::wstring nk = NameKey(candidateText);
        if (nk.empty()) return;
        int score = -1;
        const size_t shorter = std::min(nk.size(), key.size());
        if (shorter >= 3 && (nk.compare(0, key.size(), key) == 0 || key.compare(0, nk.size(), nk) == 0)) {
            score = (int)(std::max(nk.size(), key.size()) - shorter); // closer in length = closer match
        } else if (maxDist > 0) {
            const int dist = NameEditDistance(key, nk);
            if (dist <= maxDist) score = dist;
        }
        if (score >= 0) hits.push_back({ score, via.empty() ? 0 : 1, canonical, via });
    };
    for (const std::wstring& n : list.names) consider(n, n, std::wstring());
    for (const auto& kv : list.aliases) {
        if (NameListHas(list, kv.second)) consider(kv.first, kv.second, kv.first);
    }
    if (!hits.empty()) {
        std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
            if (a.score != b.score) return a.score < b.score;
            if (a.viaRank != b.viaRank) return a.viaRank < b.viaRank;
            return a.name < b.name;
        });
        r.kind = NameMatch::Near;
        for (const Hit& h : hits) {
            if (r.similar.size() >= 3) break;
            if (std::find(r.similar.begin(), r.similar.end(), h.name) != r.similar.end()) continue; // one entry per name
            r.similar.push_back(h.name);
            r.similarVia.push_back(h.via);
        }
    }
    return r;
}

// Auto-complete helper: the first alias (alphabetically, ignoring case) that starts with what has been typed, or
// "" if none. Only aliases whose target is still in the list count.
inline std::wstring FindAliasStartingWith(const NameList& list, const std::wstring& typed) {
    const std::wstring t = CollapseNameSpaces(typed);
    if (t.empty()) return std::wstring();
    std::wstring tl;
    for (wchar_t c : t) tl += (wchar_t)std::towlower((wint_t)c);
    std::wstring best, bestLower;
    for (const auto& kv : list.aliases) {
        if (!NameListHas(list, kv.second)) continue;
        std::wstring al;
        for (wchar_t c : kv.first) al += (wchar_t)std::towlower((wint_t)c);
        if (al.size() > tl.size() && al.compare(0, tl.size(), tl) == 0 && (best.empty() || al < bestLower)) {
            best = kv.first;
            bestLower = al;
        }
    }
    return best;
}

// Groups of names that differ only by case, spacing or punctuation (each group has 2+ names, sorted).
inline std::vector<std::vector<std::wstring>> FindDuplicateNameGroups(const std::vector<std::wstring>& names) {
    std::map<std::wstring, std::vector<std::wstring>> byKey;
    for (const std::wstring& n : names) {
        const std::wstring k = NameKey(n);
        if (!k.empty()) byKey[k].push_back(n);
    }
    std::vector<std::vector<std::wstring>> out;
    for (auto& kv : byKey) {
        if (kv.second.size() > 1) {
            std::sort(kv.second.begin(), kv.second.end());
            out.push_back(kv.second);
        }
    }
    return out;
}

// Days since 1970-01-01 for a valid date (proleptic Gregorian); used to ask "added within the last N days?".
inline long long NameDateToDays(const SimpleDate& d) {
    long long y = d.year;
    const long long m = d.month;
    y -= (m <= 2) ? 1 : 0;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = y - era * 400;
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d.day - 1;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// True if `addedISO` is a valid date within `days` days up to and including `todayISO`.
inline bool NameAddedRecently(const std::wstring& addedISO, const std::wstring& todayISO, int days) {
    SimpleDate a, t;
    if (!ParseISODate(addedISO, a) || !ParseISODate(todayISO, t)) return false;
    const long long diff = NameDateToDays(t) - NameDateToDays(a);
    return diff >= 0 && diff < days;
}

// ---------------------------------------------------------------------------
// names.txt text. One record per line, '|' separated (names can never contain '|'):
//   S|supplier|date    P|species|date    AS|old supplier|canonical    AP|old species|canonical
// ---------------------------------------------------------------------------

inline std::vector<std::wstring> BuildNameMasterLines(const NameMasterData& d) {
    std::vector<std::wstring> lines;
    auto emit = [&lines](const NameList& l, const wchar_t* tag, const wchar_t* aliasTag) {
        for (const std::wstring& n : l.names) {
            auto it = l.added.find(n);
            lines.push_back(std::wstring(tag) + L"|" + n + L"|" + (it != l.added.end() ? it->second : std::wstring()));
        }
        for (const auto& kv : l.aliases) lines.push_back(std::wstring(aliasTag) + L"|" + kv.first + L"|" + kv.second);
    };
    emit(d.suppliers, L"S", L"AS");
    emit(d.species, L"P", L"AP");
    return lines;
}

// Parses names.txt lines. Like emails.txt, a line this app could not have written is skipped, not fatal.
inline void ParseNameMasterLines(const std::vector<std::wstring>& lines, NameMasterData& out) {
    out.suppliers = NameList();
    out.species = NameList();
    for (const std::wstring& line : lines) {
        const size_t p1 = line.find(L'|');
        if (p1 == std::wstring::npos) continue;
        const std::wstring tag = line.substr(0, p1);
        const size_t p2 = line.find(L'|', p1 + 1);
        const std::wstring a = line.substr(p1 + 1, p2 == std::wstring::npos ? std::wstring::npos : p2 - p1 - 1);
        const std::wstring b = p2 == std::wstring::npos ? std::wstring() : line.substr(p2 + 1);
        if (a.empty() || a.size() > kMaxSupplierSpeciesLength || b.size() > kMaxSupplierSpeciesLength) continue;
        if (b.find(L'|') != std::wstring::npos) continue;
        NameList* list = nullptr;
        bool isAlias = false;
        if (tag == L"S") list = &out.suppliers;
        else if (tag == L"P") list = &out.species;
        else if (tag == L"AS") { list = &out.suppliers; isAlias = true; }
        else if (tag == L"AP") { list = &out.species; isAlias = true; }
        else continue;
        if (isAlias) {
            if (!b.empty()) list->aliases[a] = b;
        } else if (!NameListHas(*list, a)) {
            list->names.push_back(a);
            list->added[a] = b;
        }
    }
    // An alias whose target is not in the list is meaningless; drop it rather than keep a dangling pointer.
    for (NameList* l : { &out.suppliers, &out.species }) {
        for (auto it = l->aliases.begin(); it != l->aliases.end();) {
            if (!NameListHas(*l, it->second)) it = l->aliases.erase(it); else ++it;
        }
    }
}
