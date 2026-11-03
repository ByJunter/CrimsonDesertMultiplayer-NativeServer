#pragma once
// Exceptions raised inside the GAME's code that the mod called (P8 review of
// the night of 10-10, I1: docs/research/pendientes/p8-revision-noche.md).
//
// The rule: the mod catches only its own steps (its writes to a buffer it just
// got, the game's lock-free helpers it verified). An exception inside game code
// that may hold a lock (a queue push, an allocator with a lock, an emitter that
// writes the thread's send context) is NOT handled: the filter writes one line
// straight to the log (no allocation, no lock: log::emergency) and returns
// EXCEPTION_CONTINUE_SEARCH, so the crash is real, where it happened, and the
// game's own handlers (or Windows' error reporting) see it. Before that, the
// caller's filter switches its own feature off, so that if something up the
// stack does handle it, the mod does not call that code again.

#include <cstdint>

#include "mod/game/fault_line.h"

struct _EXCEPTION_POINTERS;

namespace cdmp::game {

// The numbers of an exception record (all zero for a null pointer).
FaultFacts fault_facts(const _EXCEPTION_POINTERS* ep) noexcept;

// For an SEH filter around a call into the game's code: one ERR line
// "<who>: exception ... while <where>: NOT handled by the mod ..." and
// EXCEPTION_CONTINUE_SEARCH. Never allocates.
int game_fault_continue(const _EXCEPTION_POINTERS* ep, const char* who, const char* where) noexcept;

// How many such exceptions this process passed on (for describe lines).
uint32_t game_faults_passed() noexcept;

}  // namespace cdmp::game
