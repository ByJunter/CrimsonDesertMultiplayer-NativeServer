#pragma once
// The log's file names (log.cpp; docs/research/pendientes/p8-impl-freeze-watchdog.md §1). P8 session 10: a
// second launch of the game overwrote the guest's log of the freezes. Now every run writes its own file,
// <stem>_YYYYMMDD-HHMMSS[_N]<ext> (cdmp_20261010-183912.log), the newest kKeepLogs are kept, and <stem><ext>
// (cdmp.log) is a HARD LINK to the current one (no copy, no symlink), so whoever reads cdmp.log reads this run.
// Pure helpers (no Windows call) so the tests can run them.

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cdmp::log::names {

inline constexpr size_t kKeepLogs = 10;

// "YYYYMMDD-HHMMSS".
inline std::wstring stamp(unsigned year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second) {
    wchar_t b[24];
    auto put = [&](size_t at, unsigned v, int width) {
        for (int i = width - 1; i >= 0; --i) {
            b[at + static_cast<size_t>(i)] = static_cast<wchar_t>(L'0' + v % 10);
            v /= 10;
        }
    };
    put(0, year, 4);
    put(4, month, 2);
    put(6, day, 2);
    b[8] = L'-';
    put(9, hour, 2);
    put(11, minute, 2);
    put(13, second, 2);
    return std::wstring(b, 15);
}

// <stem>_<stamp>[_N]<ext>; n 0 or 1 = no suffix.
inline std::wstring dated(std::wstring_view stem, std::wstring_view stmp, unsigned n, std::wstring_view ext) {
    std::wstring s(stem);
    s += L'_';
    s += stmp;
    if (n > 1) s += L"_" + std::to_wstring(n);
    s += ext;
    return s;
}

// Is `name` (a file name, no folder) one of ours: <stem>_DDDDDDDD-DDDDDD[_N]<ext>? Not cdmp_packets_*.log.
inline bool is_dated(std::wstring_view name, std::wstring_view stem, std::wstring_view ext) {
    if (name.size() < stem.size() + 1 + 15 + ext.size()) return false;
    if (name.substr(0, stem.size()) != stem || name[stem.size()] != L'_') return false;
    if (name.substr(name.size() - ext.size()) != ext) return false;
    const std::wstring_view s = name.substr(stem.size() + 1, 15);
    for (size_t i = 0; i < 15; ++i) {
        const bool digit = s[i] >= L'0' && s[i] <= L'9';
        if (i == 8 ? s[i] != L'-' : !digit) return false;
    }
    const std::wstring_view rest = name.substr(stem.size() + 16, name.size() - stem.size() - 16 - ext.size());
    if (rest.empty()) return true;
    if (rest.size() < 2 || rest[0] != L'_') return false;
    return std::all_of(rest.begin() + 1, rest.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

// The sort key: stamp, then the _N suffix as a number (so _10 comes after _9).
inline std::pair<std::wstring, unsigned> key_of(std::wstring_view name, std::wstring_view stem, std::wstring_view ext) {
    const std::wstring st(name.substr(stem.size() + 1, 15));
    const std::wstring_view rest = name.substr(stem.size() + 16, name.size() - stem.size() - 16 - ext.size());
    unsigned n = 1;
    if (rest.size() > 1) n = static_cast<unsigned>(std::stoul(std::wstring(rest.substr(1))));
    return {st, n};
}

// Of the dated logs in `names` (others are ignored), the ones to delete so that `keep` remain (oldest first).
// `never` (this run's file) is never returned, even if its stamp sorts first (the clock was set back).
inline std::vector<std::wstring> to_delete(std::vector<std::wstring> names, std::wstring_view stem,
                                           std::wstring_view ext, size_t keep = kKeepLogs,
                                           std::wstring_view never = {}) {
    names.erase(std::remove_if(names.begin(), names.end(), [&](const std::wstring& n) { return !is_dated(n, stem, ext); }),
                names.end());
    std::sort(names.begin(), names.end(),
              [&](const std::wstring& a, const std::wstring& b) { return key_of(a, stem, ext) < key_of(b, stem, ext); });
    if (names.size() <= keep) return {};
    names.resize(names.size() - keep);
    if (!never.empty()) {
        names.erase(std::remove(names.begin(), names.end(), std::wstring(never)), names.end());
    }
    return names;
}

}  // namespace cdmp::log::names
