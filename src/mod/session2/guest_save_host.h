#pragma once
// P8 session 25, the guest's own save on the host, PHASE A (host side only):
// docs/research/noche/progreso-invitado.md §3 and "Implementado (fase A)".
// [session2] guest_own_save = 0 (default) | probe | 1, host only; it needs
// save_guard=redirect installed and healthy (otherwise it is forced off with
// one log line). The pure part (modes, slot rules, header check, decisions,
// generations) is guest_save_logic.h.
//
//   staging   ...\Pearl Abyss\CD\cdmp_guest_save_staging\<host account>\
//             slot204.incoming\{lobby,save}.save: a sibling of the save
//             folder and of the sandbox, which the game never lists. Phase A
//             fills it at install from guest_own_save_debug_dir; phase B
//             fills the same folder from the tunnel and calls
//             guest_save_incoming_ready().
//   LoadSlot  entry hook on 0x186F570 (mgr, int* res, u32 slot, actor):
//             user 2's loads go to slot 204 when it is ready; with nothing
//             ready mode 1 refuses them all ([0x6CF7CA4], the manager's
//             "busy"; its UserLogin too: its login fails natively) instead
//             of loading the host's slot. The host's loads always pass.
//   load      in the shared 0x20688A0 entry hook (identity_probe.h), for
//             "slot204" and user 2 only, inside a LoadSlot call this hook
//             remapped (so on the server thread, inside the manager's busy
//             section): <sandbox>\<account>\slot204 -> staging slot204.old,
//             staging slot204.incoming -> slot204, then the slot is
//             registered with the game's own functions GetOrCreateSlot
//             0x2355EC0(S, "slot204", 0) and AddFile 0x235B100(slot,
//             "lobby"/"save"). After the load the file object's header
//             (+0x98) must be the file's and the save guard must have seen
//             the read; otherwise user 2's save is POISONED (no more loads or
//             saves of user 2 in this run).
//   save      entry hook on 0x186E450 (mgr, int* res, u32 slot, character,
//             opts*): user 2's saves go to slot 204 once both files of ITS
//             current login were verified; otherwise mode 1 refuses
//             ([0x6CF7CA0], nothing written). The host's own saves always
//             pass. After a save of slot 204 its two files are
//             exported to %LOCALAPPDATA%\cdmp\guest_saves\<guest>\gen<n>\
//             with a manifest (the last 5 kept), and the result handler runs.
//   probes    every mode but 0: who calls 0x186F570 / 0x186E450 / 0x1871930
//             (FlushPendingSaveData), for which user, slot, result, thread.
//
// Build 1.0.0.2976; every entry is checked against its bytes first.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mod/session2/guest_save_logic.h"

namespace cdmp::session2 {

struct GuestSaveOptions {
    gsv::Mode mode = gsv::Mode::Off;
    std::wstring debug_dir;       // phase A: a folder with lobby.save + save.save (another account's slot)
    std::string guest_id;         // the mailbox folder (phase B: the guest's SteamID64)
};

// After save_hooks_install and server_install, before the tunnel opens (worker thread). False = off (logged).
bool guest_save_install(const GuestSaveOptions& opt);
gsv::Mode guest_save_mode();  // what is installed (Off if forced off)

// ---- phase B's entry points (any thread)
// The staging folder (empty when the mode is off): write lobby.save and save.save into it, then call
// guest_save_incoming_ready(). It is outside the game's view; nothing reads it until it is ready.
std::wstring guest_save_incoming_dir();
// What a staged pair is (session 25): its guest (the mailbox folder), the guest's own slot N, its line
// (gsw::find_continuation) and its base (the save.save the guest offered), 64 hex digits each (empty = none). It
// becomes the exports' when the pair moves in: an upload staged while user 2 still plays the previous pair never
// renames that pair's saves.
struct GuestSaveLabel {
    std::string guest;
    std::optional<uint32_t> guest_slot;
    std::string lineage_root, lineage_base;
};
// The files in the staging folder are complete: checks them as the game would (header, sizes) and marks the slot
// ready for user 2's next load, labelled `label` (phase A's debug folder: no slot, no line). False (why says it) if
// they do not pass; the folder is left as it is.
bool guest_save_incoming_ready(const std::string& source, const GuestSaveLabel& label, std::string& why);
// The mailbox folder name of the current guest (phase B: its SteamID64).
void guest_save_set_guest_id(const std::string& id);
std::string guest_save_guest_id();
// Phase B (guest_save_tunnel.cpp, network thread): an upload is about to fill the staging folder. Under the folder
// lock: the staged mark is dropped (a load in between takes the live slot or nothing, never half a folder) and the
// folder is emptied (the mod's own files only). False (why) when the mode is off or failed in this run.
bool guest_save_incoming_begin(std::string& why);
// One file of the upload into the staging folder (after guest_save_incoming_begin, before _ready).
bool guest_save_incoming_write(gsv::FileKind kind, const std::vector<uint8_t>& bytes, std::string& why);
// The next export's generation is above this (only raised): the guest's base_gen and what it acknowledged, so the
// numbering never goes back after the mailbox folder was emptied (review of phase B, item 6).
void guest_save_set_generation_floor(uint32_t floor);
// %LOCALAPPDATA%\cdmp\guest_saves\<guest id as a folder name> (empty when the mode is off).
std::wstring guest_save_mailbox_folder(const std::string& guest_id);
// How many exports so far (the host's final save waits for one more), and the last game error the hooks saw on
// user 2's loads or saves (SAVE_STATUS; 0 = none).
uint64_t guest_save_exports();
uint32_t guest_save_last_error();
uint32_t guest_save_last_generation();

// After each export (server thread, right after the save): keep it short (queue the transfer).
struct GuestSaveExport {
    uint32_t generation = 0;
    std::wstring folder;  // ...\gen<n>
    std::string reason;
    uint64_t lobby_size = 0, save_size = 0;
    std::string lobby_sha256, save_sha256;
    std::string guest;                   // the live pair's guest (guest_save_incoming_ready's label)
    std::optional<uint32_t> guest_slot;  // its N
    std::string lineage_root, lineage_base;  // its line and base, 64 hex digits (empty = none)
    uint64_t time_unix = 0;
};
using GuestSaveResultFn = void (*)(const GuestSaveExport&) noexcept;
void guest_save_set_result_handler(GuestSaveResultFn fn);

// For SAVE_STATUS (phase B) and the log.
gsv::SlotState guest_save_state();
std::string guest_save_describe();

}  // namespace cdmp::session2
