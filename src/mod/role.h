#pragma once
// This process's role (role_logic.h, docs/research/dos-instancias.md): set once by the worker (mod.cpp) before any
// other thread of the mod starts, read by whoever names a file. None until then and without CDMP_ROLE.

#include <atomic>
#include <string>
#include <string_view>

#include "mod/role_logic.h"

namespace cdmp::role {

namespace detail {
inline std::atomic<uint8_t>& current_slot() {
    static std::atomic<uint8_t> r{static_cast<uint8_t>(Role::None)};
    return r;
}
}  // namespace detail

inline void set_current(Role r) { detail::current_slot().store(static_cast<uint8_t>(r), std::memory_order_release); }
inline Role current() { return static_cast<Role>(detail::current_slot().load(std::memory_order_acquire)); }
// `stem` with this process's tag (role_logic.h tagged).
inline std::wstring tagged(std::wstring_view stem) { return role::tagged(stem, current()); }

}  // namespace cdmp::role
