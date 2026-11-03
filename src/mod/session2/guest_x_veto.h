#pragma once
// P8 session 9 (docs/research/pendientes/p8-s9-cinematica.md §7-§8), GUEST only, needs guest_seq_route=1:
// what a stage of ANOTHER character's sequencer manager X (the host's Kliff's, fed by option A) would do to THIS
// client is vetoed. In session 9 the host's camp-liberation cutscene, routed into X, ran its stage chart function
// Loading on the guest: its own field reload (0x778AB0 -> [P+0x15C] = 1), its GameLoadingStartReq, the server's
// LogoutCharacterToFieldServerAck and the title.
//
// [session2] guest_x_stage_veto = 0 | probe | 1 (default 0):
//   V1  mid hook 0x5F2A3A in ClientSequencerStage_StageChartProcessor's Loading handler 0x5F2A00, right after its
//       gate 0x204F730 (test al,al ; je 0x5F35D7). R14 = RCX = the processor; G = [proc+8]. When G is another
//       character's manager (gxv::judge: vtable 0x55C9720, not the current M nor owned by M's live owner or this
//       connection's own character, in guest_seq_play's XTable or owned by another character): RAX := 0, so the
//       native je takes the gate-refused path to the epilogue (nothing ran before but the read-only gate).
//   V0  inline hook at 0x778AB0 (the only writer of [P+0x15C] = 1), COUNT ONLY (review 2: neither P nor the request
//       names the stage that asked): a call inside an "X context" (gxv::ContextSlot) is counted and logged. The
//       context is set by guest_seq_route at 0xC07749 when it routes a 2981 to X and cleared at 0xC07776 (the nop
//       after call 0xA9E100), at every routing hook's entry, when stale (> 500 ms), and set around guest_seq_play's
//       own calls 0xA953C0(X) / 0xAA3E70(X).
// [session2] guest_x_camera_veto = 0 | probe | 1 (default probe): without guest_x_ui_veto, mid hooks at the entry of
//   the processor's camera / screen handlers (gxv::Fn, family Camera, §7.3), counting per handler. '1' is not
//   applied (each handler must be read whole first): it acts as probe and says so.
// [session2] guest_x_ui_veto = 0 | probe | 1 (default 0; p8-s10.md §4): one mid hook per dialogue / interface /
//   camera handler (gxv::Fn: SetQuestDialogAICategory, TimelineEvent, BlockInteraction, SetInteraction, InputBlock,
//   UIControl, the UI* slots, the camera slots), counting per handler how often it runs with G = X and with G = the
//   own M. With '1', a call for X of a handler whose native early way out was read whole (gxv::fn_info vetoable:
//   TimelineEvent, SetInteraction, InputBlock, UIControl, UIShowMinimap, UIStageGuide) and that is not in
//   guest_x_ui_veto_allow (below; by default SetInteraction is) is forced to that way out
//   (the refused path's register writes replayed, then rip; the whole hook .. way-out span hash-checked at install);
//   every other handler stays a probe, and the log says so.
// [session2] guest_x_ui_veto_allow = <handler names> (default SetInteraction; p8-impl-veto-interaccion.md): these
//   handlers are never vetoed for X (gxv::parse_allow / fn_act): still hooked and counted, their X calls logged
//   ALLOWED. SetInteraction adds / removes the NPCs' dynamic interaction entries (world state); allowed as a whole,
//   so its add and its remove (r8b = 1 / 0, counted apart) both run. Empty, 0 or none: nothing allowed (as before).
//
// Hooks: SEH reads, atomics, a bounded lock-free ring; no allocation, no lock, no log I/O, no game call.
// Lines 'GuestXVeto [...]' (rate-limited) from the diagnostics thread, a summary every 60 s.

#include <cstdint>
#include <string>

namespace cdmp::session2 {

// From guest_start. route_mode = guest_seq_route's raw value (both keys are refused unless it is 1). ui_allow =
// guest_x_ui_veto_allow's raw value.
void guest_x_veto_start(const std::string& stage_mode, const std::string& camera_mode, const std::string& ui_mode,
                        const std::string& ui_allow, const std::string& route_mode);
void guest_x_veto_tick() noexcept;  // the diagnostics thread, every 5 s

// The "X context" (V0). Thread-local; sp = an address in the setter's frame (only deeper calls are inside).
// guest_seq_route at 0xC07749 when a 2981 is ROUTED to X (sp = the handler's rsp there). Does nothing unless
// V0 and the clear hook at 0xC07776 are both in (a context without its clear point is never set).
void guest_x_context_routed_2981(uintptr_t x, uintptr_t sp) noexcept;
// The SEH filters of our own calls for X (guest_seq_play): before any unwind.
void guest_x_context_clear() noexcept;
// guest_seq_route_hook's entry, every point (review 3): a new handler starts; any context left on this thread is gone.
void guest_x_context_hook_entry() noexcept;
// This client's current M (a vf48 result of its field: guest_seq_play's frame hook, guest_seq_route's handlers) and
// its live owner key, for the own-manager check (review 4). The main thread; atomics only.
void guest_x_note_m(uintptr_t m, uint32_t m_key) noexcept;
// S46 phase 2 (shop_trade_probe's R0 on the guest): that M while it is fresh (this connection, noted <= 2 s ago); 0 else.
// Any thread; atomics only.
uintptr_t guest_x_current_m() noexcept;

// RAII around our own calls into the game for X. Saves and restores the previous context.
class GuestXContextScope {
public:
    explicit GuestXContextScope(uintptr_t x) noexcept;
    ~GuestXContextScope();
    GuestXContextScope(const GuestXContextScope&) = delete;
    GuestXContextScope& operator=(const GuestXContextScope&) = delete;

private:
    uint64_t prev_x_ = 0, prev_sp_ = 0, prev_us_ = 0;
};

}  // namespace cdmp::session2
