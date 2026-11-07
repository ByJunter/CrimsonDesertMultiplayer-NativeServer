#pragma once

#include <cstdint>
#include <string>

namespace cdmp::session2 {

// Both roles, build 3527: another user's main character is not hidden by the
// mercenary clan's login rule (remote_visible_logic.h). Without it the players
// are in the same world, moving, and invisible to each other. A failed install
// is logged and leaves that as it is; other builds are a no-op success.
bool remote_visible_install();
std::string remote_visible_describe();
// Installed: the setter's code was checked and it is hooked.
bool remote_visible_ready() noexcept;
// focus_follow, the client's main thread: Status' own setter for the clan's bit, past this module's refusal.
// `in_pending` and `in_flags`: which of the two flag words it goes to (a hide: `in_flags`). False: it faulted.
bool remote_visible_set_hidden(uintptr_t status, bool hidden, bool in_pending, bool in_flags) noexcept;

}  // namespace cdmp::session2
