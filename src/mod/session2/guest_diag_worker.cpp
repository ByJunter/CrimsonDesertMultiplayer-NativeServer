#include "mod/session2/guest_diag_worker.h"

#include <atomic>
#include <format>

#include "common/clock.h"
#include "common/kick_worker.h"
#include "mod/log.h"
#include "mod/session2/guest.h"
#include "mod/session2/guest_actor_diag.h"
#include "mod/session2/guest_bodyanim_diag.h"
#include "mod/session2/guest_s3.h"
#include "mod/session2/guest_scene_diag.h"
#include "mod/session2/guest_sched_load.h"
#include "mod/session2/guest_seq_play.h"
#include "mod/session2/guest_seq_foreign.h"
#include "mod/session2/guest_seq_present.h"
#include "mod/session2/guest_seq_restore.h"
#include "mod/session2/guest_seq_route.h"
#include "mod/session2/guest_stage_diag.h"
#include "mod/session2/guest_x_interaction.h"
#include "mod/session2/guest_x_veto.h"
#include "mod/session2/load_trip.h"
#include "mod/session2/ride_filter.h"

namespace cdmp::session2 {
namespace {

// Never destroyed: a static destructor waiting for the thread under the loader lock could hang.
KickWorker& g_worker = *new KickWorker;
std::atomic<uint64_t> g_last_us{0}, g_max_us{0};
uint64_t g_slow_lines = 0;  // worker thread only
constexpr uint64_t kSlowUs = 1'000'000, kSlowLines = 64;

double ms(uint64_t us) { return static_cast<double>(us) / 1000.0; }

void run_once() {
    const uint64_t t0 = now_us();
    guest_actor_diag_tick();  // noexcept
    const uint64_t t1 = now_us();
    guest_s3_tick();          // noexcept
    guest_stage_diag_tick();  // noexcept; P8 phase 2 (pendientes/p8-observador-escenas.md Fase 2)
    guest_scene_diag_tick();  // noexcept; P8 S1, NPC scenes G1-G4 (pendientes/p8-s1-escenas-npc.md §5.2)
    guest_seq_route_tick();   // noexcept; P8 S1 option A, routing lines and G5 (pendientes/p8-impl-opcion-a.md)
    guest_seq_foreign_tick(); // noexcept; S44 the other player's binding and its probes (s25/s44-escenas-del-anfitrion.md)
    guest_seq_present_tick(); // noexcept; S45 foreign scenes on this client's camera / input (s25/s45-escenas-ajenas.md)
    guest_bodyanim_diag_tick();  // noexcept; P8 session 13, BodyAnim / CC+0x70 of the G5 NPCs (p8-impl-secuenciador-bodyanim.md)
    guest_seq_play_tick();    // noexcept; P8 guest_seq_play lines and summary (pendientes/p8-impl-escenas-play.md)
    guest_local_load_watch();  // noexcept; B1, guest_local_after_remote=stuck (pendientes/p8-s9-cinematica.md §8)
    guest_x_veto_tick();      // noexcept; P8 session 9 veto of X's reload / camera (pendientes/p8-s9-cinematica.md)
    guest_x_interaction_tick();  // noexcept; P8 session 13 C1, the shopkeepers' prompt (pendientes/p8-impl-tenderos-c1.md)
    guest_sched_load_tick();  // noexcept; P8 guest_schedule_load lines and summary (pendientes/p8-impl-horarios.md)
    guest_seq_restore_tick(); // noexcept; P8 seq_actor_restore lines and summary (pendientes/p8-impl-seq-restore.md)
    load_trip_tick();         // noexcept; P8 S2 (pendientes/p8-2322-observador.md §6)
    ride_filter_tick();       // noexcept; P8 session 9 D1 lines and summary (pendientes/p8-impl-s9-movimiento.md)
    const uint64_t t2 = now_us();
    try {
        if (guest_active()) log::info("{}", guest_describe());
    } catch (...) {
    }
    const uint64_t t3 = now_us();
    const uint64_t total = t3 - t0;
    g_last_us.store(total, std::memory_order_relaxed);
    if (total > g_max_us.load(std::memory_order_relaxed)) g_max_us.store(total, std::memory_order_relaxed);
    if (total > kSlowUs && g_slow_lines++ < kSlowLines) {
        log::warn("GuestDiag worker: one run took {:.1f} ms (actor diag {:.1f} ms, S3 {:.1f} ms, status line {:.1f} "
                  "ms); off the network thread, which is not held by it",
                  ms(total), ms(t1 - t0), ms(t2 - t1), ms(t3 - t2));
    }
}

}  // namespace

void guest_diag_worker_start() {
    if (g_worker.running()) return;
    const bool ok = g_worker.start(run_once, KickWorker::Config{});
    if (ok) {
        log::info("GuestDiag worker ON: the guest's diagnostics (wait list, restore lines, S3 history, status line) "
                  "run on their own thread (below normal priority) every 5 s, kicked by the network thread's report "
                  "and never waited for by it");
    } else {
        log::error("GuestDiag worker: the thread could not be created; the guest's 5 s diagnostics are OFF (the "
                   "network thread no longer runs them)");
    }
}

void guest_diag_kick() noexcept { g_worker.kick(); }

std::string guest_diag_worker_describe() {
    if (!g_worker.running()) return {};
    return std::format(" | diag worker: runs {} (timed {}), last {:.1f} ms, max {:.1f} ms", g_worker.runs(),
                       g_worker.timed_runs(), ms(g_last_us.load()), ms(g_max_us.load()));
}

}  // namespace cdmp::session2
