#pragma once
// [general] background_full_speed = 0 | 1: the copy that is not in the foreground keeps its GPU share (two copies on
// one PC, CDMP_ROLE). Why and what: bg_speed_logic.h, docs/research/dos-instancias.md §2.6. No hook, no game memory.

#include "mod/bg_speed_logic.h"

namespace cdmp::bgspeed {

// From the worker, right after the config is read: logs the gate; with Gate::On opts the process out of power
// throttling and starts the foreground-watch thread (one ON line, then one status line a minute).
void start(Gate g, const char* role_name);
// From cdmp::stop() (test host): stops the thread and puts the original GPU class back. Nothing to do inside the game.
void stop();

}  // namespace cdmp::bgspeed
