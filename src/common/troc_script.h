#pragma once
// Option D: plays a scripted conversation with the host's game server as its
// second user (stages E2-E4 of docs/OPCION-D-EMULADOR.md), from cdmp_peer
// standing in for the guest. Pure logic: the caller feeds the server's
// packets (Ack/Nak written to session 2) and the clock; the player hands back
// the Req to send. Unit-tested in tests/optd_tests.cpp.
//
// Script: one command per line, '#' starts a comment, numbers are decimal or
// 0x-hex. Opcodes are numbers (they belong to one game build; scripts are
// generated from a capture of that build: tools/scripts/make_troc_script.py).
//
//   var NAME u8|u16|u32|u64 VALUE     a variable with a starting value
//   send LABEL OPCODE SLOT HEX         the next Req: payload in hex ("-" = empty)
//   put u8|u16|u32|u64 OFFSET NAME     write a variable into that payload
//   learn NAME u8|u16|u32|u64 OPCODE OFFSET
//                                      whenever an Ack OPCODE arrives (now or later),
//                                      read the variable from its payload
//   expect OPCODE [TIMEOUT_MS]         do not go on until that Ack arrives (default 10 s)
//   expect OPCODE TIMEOUT_MS u8|u16|u32|u64 OFFSET NAME
//                                      the same, but only an Ack whose payload holds the
//                                      variable NAME at OFFSET counts (e.g. a 2322 naming
//                                      this peer's own character, not a copy of the host's)
//   wait MS                            pause
//   reply ON_OPCODE OPCODE SLOT HEX    whenever ON_OPCODE arrives, send this Req
//                                      (its own put/learn lines may follow, as for send)
//   logout_session                     the native goodbye LogoutSessionToServerReq 2001,
//                                      as the game's client sends it: D1 07 FF (3 bytes,
//                                      no length field); its Acks 2003/2002 can be expected
//   learn_child NAME OPCODE OWNER [TYPES]
//                                      D-53r: in every ActorLoadDataAck OPCODE (2005), the
//                                      first record (u16 length + record, from payload +0)
//                                      of a child of OWNER (a variable: owner u32 @5 == it,
//                                      type u8 @0 in TYPES, default 4,5,6, key u32 @1 with
//                                      key>>28 == 0xB) gives NAME = its key. Only the first
//                                      child counts; a malformed 2005 is ignored whole.
//   learn_child_hash NAME              with learn_child: NAME = the layer-0 action hash of
//                                      that child's CharacterControl block, found by its
//                                      serialized grammar (not a fixed offset); learned only
//                                      when exactly one such block parses (else NAME keeps
//                                      its `var` value)
//   expect_learned NAME [TIMEOUT_MS]   do not go on until NAME has been learned (default 10 s)
//   learn_2661 SEQ NPC HASH [OWNER [NODE]]
//                                      P8 U2 (research/s25/tiendas-u2.md): in every PrepareSequencerAck 2661
//                                      whose owner (u32 @5) is the variable OWNER (default PLAYER_KEY) and whose
//                                      resource hash (u32 @27) is the variable HASH: SEQ = its sequencer id (u64
//                                      @9) and NPC = the shopkeeper: the actor of the binding whose node key is the
//                                      variable NODE, or without NODE the first binding of an NPC (key>>28 == 0xB)
//                                      of type 4 or 5 (an actor the scene needs). The bindings: 11 bytes each from
//                                      @91 {u32 node, u32 actor, u8 type, 2 x u8}. The last such 2661 wins. SEQ may
//                                      only be put as u64, NPC as u32.
//   end                                stop here (the rest is ignored)
//
// A variable given on the command line (set_var) is pinned: no learn overwrites it.

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cdmp::troc {

struct ScriptPut {
    uint8_t size = 4;  // bytes
    uint16_t offset = 0;
    std::string var;
};

struct ScriptStep {
    enum class Kind : uint8_t { Send, Expect, Wait, Reply, ExpectLearned };
    Kind kind = Kind::Send;
    std::string label;
    uint16_t opcode = 0;     // Send/Reply: the Req; Expect: the Ack
    std::string var;         // ExpectLearned: the variable; Expect: the value its Ack must hold (empty = any)
    uint8_t match_size = 0;  // Expect with a value: bytes, at match_offset of the Ack's payload
    uint16_t match_offset = 0;
    uint16_t on_opcode = 0;  // Reply: the Ack that triggers it
    int8_t slot = -1;
    std::vector<uint8_t> payload;
    std::vector<ScriptPut> puts;
    uint32_t ms = 0;  // Expect: timeout; Wait: pause
    bool internal = false;  // Send: an internal message (logout_session), not opcode|slot|len|payload
    int line = 0;
};

struct ScriptLearn {
    std::string var;
    uint8_t size = 4;
    uint16_t opcode = 0;
    uint16_t offset = 0;
};

// learn_child / learn_child_hash (one per script).
struct ScriptChildLearn {
    std::string var;        // the child's key
    uint16_t opcode = 0;    // ActorLoadDataAck
    std::string owner_var;  // the owner's user key (USER_KEY)
    uint8_t type_mask = 0;  // bit t = record type t accepted
    std::string hash_var;   // empty: no learn_child_hash
};

// learn_2661 (P8 U2).
struct ScriptPrepareLearn {
    std::string seq_var, npc_var, hash_var, owner_var, node_var;  // node_var empty: the first NPC of type 4/5
};

struct Script {
    std::map<std::string, uint64_t> vars;
    std::vector<ScriptStep> steps;
    std::vector<ScriptLearn> learns;
    std::optional<ScriptChildLearn> child;
    std::vector<ScriptPrepareLearn> prepares;
};

// ---- PrepareSequencerAck 2661 (P8 U2; research/s25/tiendas-u2.md, observer_seq_copy_logic.h parse_prepare)
// u8 kind @0 | u32 actor @1 | the play data, packed, from @5: owner u32 @5, sequencer id u64 @9, type u8 @17, u8 @18,
// u64 @19, the resource's file-name hash u32 @27, ... up to @91; then 11-byte bindings {u32 node, u32 actor, u8 type,
// 2 x u8}.
inline constexpr uint16_t kOpPrepareSequencerAck = 2661;
inline constexpr size_t kPrepareBindings = 91, kPrepareBindingSize = 11;
struct Prepare2661 {
    struct Binding {
        uint32_t node = 0, actor = 0;
        uint8_t type = 0;
    };
    uint32_t owner = 0;
    uint64_t seq = 0;
    uint32_t hash = 0;
    std::vector<Binding> bindings;
    // The shopkeeper: the NPC (key>>28 == 0xB) bound at `node`, or with node 0 the first NPC of type 4 or 5; 0 = none.
    uint32_t npc(uint32_t node = 0) const {
        for (const Binding& b : bindings) {
            if (b.actor >> 28 != 0xB) continue;
            if (node ? b.node == node : (b.type == 4 || b.type == 5)) return b.actor;
        }
        return 0;
    }
};
std::optional<Prepare2661> parse_prepare_2661(std::span<const uint8_t> payload);

// ---- ActorLoadDataAck records (D-53r; pendientes/companeros-publicacion.md section 4.4)
// Payload = repeated {u16 length, record}. A user record is 9 bytes (type 0,
// u32 user key, u32 0). Any other record starts with u8 type, u32 key @1,
// u32 owner (user key) @5, u8 slot @9.
inline constexpr size_t kUserRecordSize = 9;
inline constexpr size_t kActorRecordHeader = 10;

// The records, in order; null if any length is 0 or runs past the payload.
std::optional<std::vector<std::span<const uint8_t>>> split_actor_records(std::span<const uint8_t> payload);

// The serialized CharacterControl block of a record (CC::vf25 0x231FC90):
// per layer u8 empty (0: u16, u32 hash, u64 time), u8 flag (+u8), 2 x u64,
// u8, u32 length + "character/...app_xml", u64, u8 restore (0/1).
struct CcBlock {
    size_t offset = 0;                     // of the first layer byte, in the record
    std::optional<uint32_t> layer0_hash;   // null: layer 0 empty
    uint8_t restore = 0;
};
// Only when exactly one offset parses as such a block (the anchor is the
// app_xml path; two parses = ambiguous = none).
std::optional<CcBlock> find_cc_block(std::span<const uint8_t> record);

struct ChildRecord {
    size_t index = 0;  // among the payload's records
    size_t count = 0;  // records in the payload
    uint8_t type = 0;
    uint32_t key = 0;
    uint32_t owner = 0;
    uint8_t slot = 0;
    std::span<const uint8_t> record;
};
// The first record of the payload that is a child of `owner`: type in
// type_mask, key class 0xB (key>>28), owner field == owner. Null for none or
// a malformed payload.
std::optional<ChildRecord> find_child_record(std::span<const uint8_t> payload, uint32_t owner, uint8_t type_mask);

// Null and a message ("line N: ...") for anything malformed.
std::optional<Script> parse_script(std::string_view text, std::string* error);

class ScriptPlayer {
public:
    enum class State { Running, Done, Failed };
    static constexpr uint32_t kDefaultExpectMs = 10000;

    explicit ScriptPlayer(Script script) : s_(std::move(script)) {}

    // Overrides a variable (command line: --var NAME=VALUE).
    void set_var(const std::string& name, uint64_t value) {
        s_.vars[name] = value;
        pinned_.insert(name);
    }

    // A packet the server wrote to session 2: learns variables, satisfies
    // the current expect, queues replies.
    void on_server_packet(std::span<const uint8_t> packet, uint32_t now_ms);

    // The Req to send now, in order (often none).
    std::vector<std::vector<uint8_t>> tick(uint32_t now_ms);

    State state() const { return state_; }
    const std::string& error() const { return error_; }
    size_t step() const { return pc_; }
    size_t steps() const { return s_.steps.size(); }
    std::string describe() const;
    std::optional<uint64_t> var(const std::string& name) const;
    uint64_t sent() const { return sent_; }
    uint64_t received() const { return received_; }
    // Whether a learn (or the command line) has given that variable a value.
    bool learned(const std::string& name) const { return learned_.count(name) != 0 || pinned_.count(name) != 0; }
    // One-line notes for the console (a child learned, a malformed 2005...), once.
    std::vector<std::string> take_notes() { return std::exchange(notes_, {}); }

private:
    bool build(const ScriptStep& st, std::vector<uint8_t>& out);
    void fail(const std::string& why);
    void learn_var(const std::string& name, uint64_t value);
    void learn_child(std::span<const uint8_t> payload);
    void learn_prepare(std::span<const uint8_t> payload);

    Script s_;
    State state_ = State::Running;
    std::string error_;
    size_t pc_ = 0;
    bool started_ = false;
    uint32_t step_started_ms_ = 0;
    std::map<uint16_t, uint64_t> seen_;  // Ack opcode -> times seen since the step began
    std::map<size_t, uint64_t> matched_;  // expect-with-a-value step -> fitting Acks seen since the last send
    std::vector<std::vector<uint8_t>> replies_;
    uint64_t sent_ = 0, received_ = 0;
    std::set<std::string> pinned_, learned_;
    std::vector<std::string> notes_;
    bool child_learned_ = false;
    uint32_t malformed_child_acks_ = 0;
};

}  // namespace cdmp::troc
