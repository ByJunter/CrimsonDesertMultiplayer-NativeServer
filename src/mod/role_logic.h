#pragma once
// Two copies of the game on one PC (docs/research/dos-instancias.md). The environment variable CDMP_ROLE (host |
// guest) tells each process which one it is; absent, the mod behaves exactly as before (cdmp.ini, cdmp.log, the real
// save folder). With a role:
//   - the ini is bin64\cdmp.<role>.ini (cdmp.ini only when that one is absent, with a warning);
//   - every file the mod writes next to the asi carries the role right after its stem: cdmp.<role>.log (the alias)
//     and cdmp.<role>_<stamp>.log, cdmp_packets.<role>_<stamp>.log/.bin, cdmp_crash.<role>_<...>.dmp;
//   - only the FIRST process of a role runs the mod: the game starts a short-lived second CrimsonDesert.exe ~40 s
//     after launch (S28, u0-resultados-s28.md 8.8) that inherits CDMP_ROLE; a named mutex per role tells it apart and
//     it only writes cdmp.<role>.extra_<stamp>.log (who it is) and stays inert;
//   - the guest role sends every path under %LOCALAPPDATA%\Pearl Abyss\CD\save to ...\CD\save_cdmp_guest
//     (save_hooks.h, save_hooks_install_guest_role), in every guest-role process, from the start.
// Pure helpers (no Windows call) so the tests can run them.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cdmp::role {

enum class Role : uint8_t { None, Host, Guest };

inline constexpr wchar_t kEnvVar[] = L"CDMP_ROLE";
// The guest role's save folder, next to the real one under %LOCALAPPDATA%\Pearl Abyss\CD (outside save\, so outside
// Steam Auto-Cloud's folder and outside the host's cdmp_save_sandbox).
inline constexpr wchar_t kGuestSaveFolder[] = L"save_cdmp_guest";

// The variable's value: empty (or only blanks) = None; "host" / "guest" in any case, blanks around ignored;
// anything else = nullopt (the caller keeps the mod off: a typo must never run a guest on the real saves).
inline std::optional<Role> parse(std::wstring_view v) {
    auto blank = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; };
    while (!v.empty() && blank(v.front())) v.remove_prefix(1);
    while (!v.empty() && blank(v.back())) v.remove_suffix(1);
    if (v.empty()) return Role::None;
    std::wstring low(v);
    for (wchar_t& c : low) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    if (low == L"host") return Role::Host;
    if (low == L"guest") return Role::Guest;
    return std::nullopt;
}

inline const wchar_t* name(Role r) {
    switch (r) {
        case Role::Host: return L"host";
        case Role::Guest: return L"guest";
        case Role::None: break;
    }
    return L"";
}
inline const char* name_a(Role r) {
    switch (r) {
        case Role::Host: return "host";
        case Role::Guest: return "guest";
        case Role::None: break;
    }
    return "none";
}

// What goes right after a file's stem: "" | ".host" | ".guest".
inline std::wstring tag(Role r) { return r == Role::None ? std::wstring() : std::wstring(L".") + name(r); }

// `stem` + the role's tag: tagged(L"cdmp_packets", Host) = L"cdmp_packets.host".
inline std::wstring tagged(std::wstring_view stem, Role r) { return std::wstring(stem) + tag(r); }

// The log's alias (the dated files are <stem>_<stamp>[_N].log, log_names.h). `extra`: a second process of the role.
inline std::wstring log_name(Role r, bool extra = false) {
    return tagged(L"cdmp", r) + (extra && r != Role::None ? L".extra" : L"") + L".log";
}

// The ini to read. `role_ini_exists`: bin64\cdmp.<role>.ini is there; `base_exists`: bin64\cdmp.ini is.
struct IniChoice {
    std::wstring name;       // file name in bin64
    bool fell_back = false;  // cdmp.ini because cdmp.<role>.ini is absent (a warning)
};
inline IniChoice choose_ini(Role r, bool role_ini_exists, bool base_exists) {
    if (r == Role::None) return {L"cdmp.ini", false};
    const std::wstring own = tagged(L"cdmp", r) + L".ini";
    if (role_ini_exists || !base_exists) return {own, false};  // neither: the default is written as the role's own
    return {L"cdmp.ini", true};
}

// The named mutex that tells the first process of a role from the later ones (per logon session).
inline std::wstring mutex_name(Role r) { return std::wstring(L"Local\\cdmp_role_") + name(r); }

// %LOCALAPPDATA% -> the real save folder and the guest role's own.
inline std::wstring save_dir(std::wstring_view local_app_data) {
    return std::wstring(local_app_data) + L"\\Pearl Abyss\\CD\\save";
}
inline std::wstring guest_save_dir(std::wstring_view local_app_data) {
    return std::wstring(local_app_data) + L"\\Pearl Abyss\\CD\\" + kGuestSaveFolder;
}

}  // namespace cdmp::role
