#pragma once
// P8 session 25, weather (docs/research/noche/clima-sincronizar.md §5, section "Implementado"). Build 1.0.0.2976.
//
// The guest shows the host's weather through the game's own "Weather Viewer" mode (weather_sync_logic.h has the
// verified addresses): byte X+0x31 of the scene weather system X makes the guest's simulators and mixer stand still
// and the update 0x3DC4730 use the values written into X's holder.
//
// [session2] weather_sync = 0 (default) | probe | selftest | 1, both PCs (each role does its part):
//   probe     HOST and GUEST, read only (guarded reads, no hook, no call, no write): every 5 s one
//             'WeatherSync [probe host|guest]' line: X by both paths (same object?), its vtable, X+0x31, the
//             holder and A/B/C, X+0x88 (WeatherComponents), the named vector, X+0x124/+0x128, the local sample
//             A+0x198.., float sanity, the server tick and tick % 60000; a 'WeatherSync ... X changed' line on any
//             change of X / holder / A / B / C (sampled at 5 Hz), a 'target changed' line with tick % 60000 when
//             A+0x12C/+0x130/+0x134 change, and the B block's raw words once a minute.
//   selftest  HOST only, one PC (P1): the probe, plus an inline hook at the entry of 0x3DC4730 (installed once the
//             local player has been in the world for 3 s). 15 s after the world is ready, for 30 s, on the game's
//             thread right before the update: X+0x31 = 1 and A+0x12C = 1.0 every frame (the rest as it is); then
//             X+0x31 and A+0x12C/+0x160/+0x164/+0x168/+0x16C go back to what they were. Aborted (and restored) if
//             the world leaves, X changes or a read/write fails. Lines 'WeatherSync selftest: ...'.
//   1         HOST: the probe's 5 Hz read becomes a sample; the network thread sends it as tunnel control state 12
//             (weather_sync_logic.h) only to a guest that announced caps 0x04 and while user 2 is in the world.
//             GUEST: announces caps 0x04; keeps the newest samples (lock-free double buffer); the hook at 0x3DC4730
//             (as above) writes the interpolated host values in viewer mode while a sample is fresh (< 1 s) and the
//             gates pass (playing on the host, in the world 3 s, within weather_sync_radius_m of the host, with a
//             margin); else it puts back the holder words it kept at its first write and X+0x31, and the guest's own
//             weather returns that frame. A failed guarded access stops the writes until 300 clean frames or the next
//             opening of the gate (10 at most per run); a new X evicts the records of the old one. Lines
//             'WeatherSync [1 guest] ...'.
// [session2] weather_sync_radius_m = 2000 (GUEST, mode 1).
//
// Threads: one worker per PC (reads only, logs); the guest's hook writes only on the thread updating X, only an X
// it is being called for; the host's link is only touched by the network thread.

#include <cstdint>
#include <span>
#include <string>

#include "common/troc_link.h"

namespace cdmp::session2 {

// mod.cpp's worker, once per role (guest = the P8 guest's PC). Nothing for 0.
void weather_sync_start(const std::string& raw, bool guest, uint32_t radius_m);
// mod.cpp's network thread, guest: the caps flag to announce (0x04 with weather_sync=1, else 0).
uint8_t weather_sync_guest_caps();
// guest.cpp guest_deliver (the mod's network thread): true = a state-12 frame, taken out of the stream.
bool weather_sync_guest_on_control(std::span<const uint8_t> packet) noexcept;
// mod.cpp's network thread, host, after session2::tunnel_tick: the newest sample to the guest (mode 1).
void weather_sync_host_tick(troc::TrocLink& link, uint32_t now_ms);

}  // namespace cdmp::session2
