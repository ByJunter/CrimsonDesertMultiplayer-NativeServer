#include "common/troc_script.h"

#include <charconv>
#include <cstring>
#include <format>
#include <sstream>

#include "common/troc_frame.h"

namespace cdmp::troc {
namespace {

std::vector<std::string> split(std::string_view line) {
    std::vector<std::string> out;
    std::istringstream in{std::string(line)};
    std::string t;
    while (in >> t) out.push_back(t);
    return out;
}

std::optional<uint64_t> number(const std::string& s) {
    uint64_t v = 0;
    const char* b = s.data();
    const char* e = s.data() + s.size();
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        b += 2;
        base = 16;
    }
    auto [p, ec] = std::from_chars(b, e, v, base);
    if (ec != std::errc{} || p != e || b == e) return std::nullopt;
    return v;
}

std::optional<int64_t> signed_number(const std::string& s) {
    if (!s.empty() && s[0] == '-') {
        auto v = number(s.substr(1));
        if (!v || *v > 0x8000000000000000ull) return std::nullopt;
        return -static_cast<int64_t>(*v);
    }
    auto v = number(s);
    if (!v || *v > 0x7FFFFFFFFFFFFFFFull) return std::nullopt;
    return static_cast<int64_t>(*v);
}

std::optional<uint8_t> type_size(const std::string& t) {
    if (t == "u8") return 1;
    if (t == "u16") return 2;
    if (t == "u32") return 4;
    if (t == "u64") return 8;
    return std::nullopt;
}

std::optional<std::vector<uint8_t>> hex(const std::string& s) {
    std::vector<uint8_t> out;
    if (s == "-") return out;
    if (s.size() % 2) return std::nullopt;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < s.size(); i += 2) {
        const int hi = nib(s[i]), lo = nib(s[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(static_cast<uint8_t>(hi << 4 | lo));
    }
    return out;
}

uint16_t rd16(std::span<const uint8_t> b, size_t o) { return static_cast<uint16_t>(b[o] | b[o + 1] << 8); }
uint32_t rd32(std::span<const uint8_t> b, size_t o) {
    return static_cast<uint32_t>(b[o]) | static_cast<uint32_t>(b[o + 1]) << 8 | static_cast<uint32_t>(b[o + 2]) << 16 |
           static_cast<uint32_t>(b[o + 3]) << 24;
}

// "4,5,6" -> bits 4, 5 and 6; only child types (4 mercenary/NPC, 5 mount, 6 pet).
std::optional<uint8_t> child_types(const std::string& s) {
    uint8_t mask = 0;
    size_t at = 0;
    while (true) {
        const size_t comma = s.find(',', at);
        auto v = number(s.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
        if (!v || *v < 4 || *v > 6) return std::nullopt;
        mask = static_cast<uint8_t>(mask | 1u << *v);
        if (comma == std::string::npos) return mask;
        at = comma + 1;
    }
}

// One CC block parsed at `m`, or null (CC::vf25 0x231FC90 order).
std::optional<CcBlock> cc_block_at(std::span<const uint8_t> r, size_t m) {
    static constexpr std::string_view kHead = "character", kTail = "app_xml";
    size_t o = m;
    CcBlock c;
    c.offset = m;
    bool any = false;
    for (int layer = 0; layer < 2; ++layer) {
        if (o >= r.size()) return std::nullopt;
        const uint8_t empty = r[o++];
        if (empty == 1) continue;
        if (empty != 0 || o + 14 > r.size()) return std::nullopt;
        const uint16_t a = rd16(r, o);
        const uint32_t h = rd32(r, o + 2);
        if (a > 0x2000 || h == 0 || h == 0xFFFFFFFFu) return std::nullopt;
        if (layer == 0) c.layer0_hash = h;
        any = true;
        o += 14;  // u16, u32 hash, u64 time
    }
    if (!any || o >= r.size()) return std::nullopt;
    const uint8_t flag = r[o++];
    if (flag > 1) return std::nullopt;
    o += flag + 16 + 1;  // (u8), 2 x u64, u8
    if (o + 4 > r.size()) return std::nullopt;
    const uint32_t n = rd32(r, o);
    o += 4;
    if (n < kHead.size() + kTail.size() || n > 1024 || n > r.size() - o) return std::nullopt;
    const std::string_view path(reinterpret_cast<const char*>(r.data() + o), n);
    if (!path.starts_with(kHead) || !path.ends_with(kTail)) return std::nullopt;
    for (char ch : path) {
        if (ch < 0x20 || ch > 0x7E) return std::nullopt;
    }
    o += n + 8;  // the path, u64
    if (o >= r.size() || r[o] > 1) return std::nullopt;
    c.restore = r[o];
    return c;
}

}  // namespace

std::optional<std::vector<std::span<const uint8_t>>> split_actor_records(std::span<const uint8_t> payload) {
    std::vector<std::span<const uint8_t>> out;
    size_t o = 0;
    while (o < payload.size()) {
        if (payload.size() - o < 2) return std::nullopt;
        const uint16_t n = rd16(payload, o);
        o += 2;
        if (n == 0 || n > payload.size() - o) return std::nullopt;
        out.push_back(payload.subspan(o, n));
        o += n;
    }
    return out;
}

std::optional<CcBlock> find_cc_block(std::span<const uint8_t> record) {
    std::optional<CcBlock> found;
    for (size_t m = kActorRecordHeader; m < record.size(); ++m) {
        auto c = cc_block_at(record, m);
        if (!c) continue;
        if (found) return std::nullopt;  // two parses: ambiguous
        found = c;
    }
    return found;
}

std::optional<ChildRecord> find_child_record(std::span<const uint8_t> payload, uint32_t owner, uint8_t type_mask) {
    auto recs = split_actor_records(payload);
    if (!recs) return std::nullopt;
    for (size_t i = 0; i < recs->size(); ++i) {
        const auto r = (*recs)[i];
        if (r.size() == kUserRecordSize || r.size() < kActorRecordHeader) continue;
        const uint8_t type = r[0];
        const uint32_t key = rd32(r, 1);
        if (type > 7 || !(type_mask >> type & 1) || key >> 28 != 0xB || rd32(r, 5) != owner) continue;
        ChildRecord c;
        c.index = i;
        c.count = recs->size();
        c.type = type;
        c.key = key;
        c.owner = owner;
        c.slot = r[9];
        c.record = r;
        return c;
    }
    return std::nullopt;
}

std::optional<Prepare2661> parse_prepare_2661(std::span<const uint8_t> p) {
    if (p.size() < kPrepareBindings || (p.size() - kPrepareBindings) % kPrepareBindingSize != 0) return std::nullopt;
    Prepare2661 r;
    r.owner = rd32(p, 5);
    r.seq = static_cast<uint64_t>(rd32(p, 9)) | static_cast<uint64_t>(rd32(p, 13)) << 32;
    r.hash = rd32(p, 27);
    for (size_t o = kPrepareBindings; o + kPrepareBindingSize <= p.size(); o += kPrepareBindingSize) {
        r.bindings.push_back({rd32(p, o), rd32(p, o + 4), p[o + 8]});
    }
    return r;
}

std::optional<Script> parse_script(std::string_view text, std::string* error) {
    Script s;
    int line_no = 0;
    ScriptStep* last_packet = nullptr;  // where `put` lines go
    auto bad = [&](const std::string& why) -> std::optional<Script> {
        if (error) *error = std::format("line {}: {}", line_no, why);
        return std::nullopt;
    };
    size_t at = 0;
    while (at <= text.size()) {
        const size_t nl = text.find('\n', at);
        std::string_view line = text.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at);
        at = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        ++line_no;
        if (const size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        const auto t = split(line);
        if (t.empty()) continue;
        const std::string& cmd = t[0];
        if (cmd == "end") break;
        if (cmd == "var") {
            if (t.size() != 4 || !type_size(t[2])) return bad("var NAME TYPE VALUE");
            auto v = number(t[3]);
            if (!v) return bad("bad value '" + t[3] + "'");
            s.vars[t[1]] = *v;
        } else if (cmd == "send" || cmd == "reply") {
            const bool reply = cmd == "reply";
            if (t.size() != 5) {
                return bad(reply ? "reply ON_OPCODE OPCODE SLOT HEX" : "send LABEL OPCODE SLOT HEX");
            }
            ScriptStep st;
            st.kind = reply ? ScriptStep::Kind::Reply : ScriptStep::Kind::Send;
            st.line = line_no;
            std::optional<uint64_t> op;
            std::optional<int64_t> slot;
            std::optional<std::vector<uint8_t>> payload;
            if (reply) {
                auto on = number(t[1]);
                op = number(t[2]);
                slot = signed_number(t[3]);
                payload = hex(t[4]);
                if (!on || *on > 0xFFFF) return bad("bad trigger opcode");
                st.on_opcode = static_cast<uint16_t>(*on);
                st.label = "reply to " + t[1];
            } else {
                st.label = t[1];
                op = number(t[2]);
                slot = signed_number(t[3]);
                payload = hex(t[4]);
            }
            if (!op || *op > 0xFFFF) return bad("bad opcode");
            if (*op == kOpLogoutSessionReq || *op == kOpLogoutSessionAck || *op == kOpLogoutUserAck) {
                return bad("2001-2003 only exist as internal messages: use logout_session");
            }
            if (!slot || *slot < -128 || *slot > 127) return bad("bad slot");
            if (!payload) return bad("bad hex payload");
            if (payload->size() + kHeaderSize > kMaxPacketSize) return bad("payload too large");
            st.opcode = static_cast<uint16_t>(*op);
            st.slot = static_cast<int8_t>(*slot);
            st.payload = std::move(*payload);
            s.steps.push_back(std::move(st));
            last_packet = &s.steps.back();
        } else if (cmd == "logout_session") {
            if (t.size() != 1) return bad("logout_session takes no arguments");
            ScriptStep st;
            st.kind = ScriptStep::Kind::Send;
            st.internal = true;
            st.label = "LogoutSessionToServerReq";
            st.opcode = kOpLogoutSessionReq;
            st.line = line_no;
            s.steps.push_back(std::move(st));
            last_packet = nullptr;  // nothing to put into a fixed message
        } else if (cmd == "put") {
            if (!last_packet) return bad("put must follow send or reply");
            if (t.size() != 4) return bad("put TYPE OFFSET NAME");
            auto size = type_size(t[1]);
            auto off = number(t[2]);
            if (!size || !off) return bad("bad put");
            if (*off + *size > last_packet->payload.size()) return bad("put outside the payload");
            last_packet->puts.push_back({*size, static_cast<uint16_t>(*off), t[3]});
        } else if (cmd == "learn") {
            if (t.size() != 5) return bad("learn NAME TYPE OPCODE OFFSET");
            auto size = type_size(t[2]);
            auto op = number(t[3]);
            auto off = number(t[4]);
            if (!size || !op || *op > 0xFFFF || !off || *off > 0xFFFF) return bad("bad learn");
            s.learns.push_back({t[1], *size, static_cast<uint16_t>(*op), static_cast<uint16_t>(*off)});
        } else if (cmd == "learn_child") {
            if (t.size() != 4 && t.size() != 5) return bad("learn_child NAME OPCODE OWNER_VAR [TYPES]");
            if (s.child) return bad("only one learn_child per script");
            auto op = number(t[2]);
            if (!op || *op > 0xFFFF) return bad("bad opcode");
            auto mask = t.size() == 5 ? child_types(t[4]) : std::optional<uint8_t>(uint8_t{0x70});
            if (!mask) return bad("TYPES is a list of 4, 5 and 6 (e.g. 6 or 4,5,6)");
            ScriptChildLearn c;
            c.var = t[1];
            c.opcode = static_cast<uint16_t>(*op);
            c.owner_var = t[3];
            c.type_mask = *mask;
            s.child = std::move(c);
        } else if (cmd == "learn_2661") {
            if (t.size() < 4 || t.size() > 6) return bad("learn_2661 SEQ NPC HASH [OWNER [NODE]]");
            ScriptPrepareLearn l{t[1], t[2], t[3], t.size() >= 5 ? t[4] : std::string("PLAYER_KEY"),
                                 t.size() == 6 ? t[5] : std::string()};
            // The two learned variables are their own: never the same, never one of the inputs.
            for (const std::string& in : {l.hash_var, l.owner_var, l.node_var}) {
                if (in == l.seq_var || in == l.npc_var) return bad("learn_2661: SEQ / NPC cannot be an input variable");
            }
            if (l.seq_var == l.npc_var) return bad("learn_2661 needs two variables (SEQ and NPC)");
            s.prepares.push_back(std::move(l));
        } else if (cmd == "learn_child_hash") {
            if (t.size() != 2) return bad("learn_child_hash NAME");
            if (!s.child) return bad("learn_child_hash must follow learn_child");
            if (!s.child->hash_var.empty()) return bad("only one learn_child_hash per script");
            if (t[1] == s.child->var) return bad("learn_child_hash needs its own variable");
            s.child->hash_var = t[1];
        } else if (cmd == "expect_learned") {
            if (t.size() != 2 && t.size() != 3) return bad("expect_learned NAME [TIMEOUT_MS]");
            ScriptStep st;
            st.kind = ScriptStep::Kind::ExpectLearned;
            st.var = t[1];
            st.ms = ScriptPlayer::kDefaultExpectMs;
            st.line = line_no;
            if (t.size() == 3) {
                auto ms = number(t[2]);
                if (!ms || *ms > 3600000) return bad("bad timeout");
                st.ms = static_cast<uint32_t>(*ms);
            }
            s.steps.push_back(std::move(st));
            last_packet = nullptr;
        } else if (cmd == "expect") {
            if (t.size() != 2 && t.size() != 3 && t.size() != 6) {
                return bad("expect OPCODE [TIMEOUT_MS] | expect OPCODE TIMEOUT_MS TYPE OFFSET NAME");
            }
            auto op = number(t[1]);
            if (!op || *op > 0xFFFF) return bad("bad opcode");
            ScriptStep st;
            st.kind = ScriptStep::Kind::Expect;
            st.opcode = static_cast<uint16_t>(*op);
            st.ms = ScriptPlayer::kDefaultExpectMs;
            st.line = line_no;
            if (t.size() >= 3) {
                auto ms = number(t[2]);
                if (!ms || *ms > 3600000) return bad("bad timeout");
                st.ms = static_cast<uint32_t>(*ms);
            }
            if (t.size() == 6) {
                auto size = type_size(t[3]);
                auto off = number(t[4]);
                if (!size || !off || *off > 0xFFFF) return bad("bad expect value (TYPE OFFSET NAME)");
                st.match_size = *size;
                st.match_offset = static_cast<uint16_t>(*off);
                st.var = t[5];
            }
            s.steps.push_back(std::move(st));
            last_packet = nullptr;
        } else if (cmd == "wait") {
            auto ms = t.size() == 2 ? number(t[1]) : std::nullopt;
            if (!ms || *ms > 3600000) return bad("wait MS");
            ScriptStep st;
            st.kind = ScriptStep::Kind::Wait;
            st.ms = static_cast<uint32_t>(*ms);
            st.line = line_no;
            s.steps.push_back(std::move(st));
            last_packet = nullptr;
        } else {
            return bad("unknown command '" + cmd + "'");
        }
    }
    auto is_learned = [&](const std::string& v) {
        bool yes = s.child && (s.child->var == v || s.child->hash_var == v);
        for (const auto& l : s.learns) yes = yes || l.var == v;
        for (const auto& l : s.prepares) yes = yes || l.seq_var == v || l.npc_var == v;
        return yes;
    };
    for (const auto& l : s.prepares) {
        for (const std::string* v : {&l.hash_var, &l.owner_var, &l.node_var}) {
            if (!v->empty() && s.vars.count(*v) == 0 && !is_learned(*v)) {
                return bad("learn_2661: variable '" + *v + "' is never declared or learned");
            }
        }
        // A sequencer id is a u64 and an actor key a u32: a put of another size is a slot mix-up.
        for (const auto& st : s.steps) {
            for (const auto& p : st.puts) {
                if ((p.var == l.seq_var && p.size != 8) || (p.var == l.npc_var && p.size != 4)) {
                    line_no = st.line;
                    return bad("learn_2661: '" + p.var + "' is put as " + std::to_string(p.size * 8) +
                               " bits (SEQ is a u64, NPC a u32)");
                }
            }
        }
    }
    if (s.child && s.vars.count(s.child->owner_var) == 0 && !is_learned(s.child->owner_var)) {
        return bad("learn_child: owner variable '" + s.child->owner_var + "' is never declared or learned");
    }
    // Every variable a put uses must exist (learned or declared); an
    // expect_learned waits for a learned one.
    for (const auto& st : s.steps) {
        for (const auto& p : st.puts) {
            if (s.vars.count(p.var) == 0 && !is_learned(p.var)) {
                line_no = st.line;
                return bad("variable '" + p.var + "' is never declared or learned");
            }
        }
        if (st.kind == ScriptStep::Kind::ExpectLearned && !is_learned(st.var)) {
            line_no = st.line;
            return bad("expect_learned: '" + st.var + "' is never learned");
        }
        if (st.kind == ScriptStep::Kind::Expect && st.match_size && s.vars.count(st.var) == 0 && !is_learned(st.var)) {
            line_no = st.line;
            return bad("expect: variable '" + st.var + "' is never declared or learned");
        }
    }
    return s;
}

std::optional<uint64_t> ScriptPlayer::var(const std::string& name) const {
    auto it = s_.vars.find(name);
    if (it == s_.vars.end()) return std::nullopt;
    return it->second;
}

void ScriptPlayer::fail(const std::string& why) {
    state_ = State::Failed;
    error_ = why;
}

bool ScriptPlayer::build(const ScriptStep& st, std::vector<uint8_t>& out) {
    if (st.internal) {
        out = make_logout_session_req();
        return true;
    }
    std::vector<uint8_t> payload = st.payload;
    for (const auto& p : st.puts) {
        auto v = var(p.var);
        if (!v) {
            fail(std::format("line {}: variable {} not learned yet", st.line, p.var));
            return false;
        }
        for (uint8_t i = 0; i < p.size; ++i) payload[p.offset + i] = static_cast<uint8_t>(*v >> (8 * i));
    }
    out = make(st.opcode, st.slot, payload);
    return true;
}

void ScriptPlayer::learn_var(const std::string& name, uint64_t value) {
    if (pinned_.count(name)) return;  // the command line wins
    s_.vars[name] = value;
    learned_.insert(name);
}

void ScriptPlayer::learn_child(std::span<const uint8_t> payload) {
    const ScriptChildLearn& c = *s_.child;
    if (child_learned_) return;  // only the first child counts
    auto owner = var(c.owner_var);
    if (!owner) return;
    if (!split_actor_records(payload)) {
        if (++malformed_child_acks_ <= 3) {
            notes_.push_back(std::format("Ack {} of {} bytes with a bad record length: ignored for {}", c.opcode,
                                         payload.size(), c.var));
        }
        return;
    }
    auto r = find_child_record(payload, static_cast<uint32_t>(*owner), c.type_mask);
    if (!r) return;
    child_learned_ = true;
    learn_var(c.var, r->key);
    std::string note = std::format("{} = 0x{:08X}: child 0x{:08X} type {} slot {} of owner 0x{:08X}, record {} of {} ({} bytes)",
                                   c.var, var(c.var).value_or(0), r->key, r->type, r->slot, r->owner, r->index + 1,
                                   r->count, r->record.size());
    if (!c.hash_var.empty()) {
        auto cc = find_cc_block(r->record);
        if (cc && cc->layer0_hash) {
            learn_var(c.hash_var, *cc->layer0_hash);
            note += std::format("; {} = 0x{:08X} (record's layer-0 hash 0x{:08X} at +0x{:X}, restore {})", c.hash_var,
                                var(c.hash_var).value_or(0), *cc->layer0_hash, cc->offset + 3, cc->restore);
        } else {
            const auto keep = var(c.hash_var);
            note += std::format("; {} not learned (no single CC block with a layer-0 action){}", c.hash_var,
                                keep ? std::format(", keeps 0x{:X}", *keep) : std::string());
        }
    }
    notes_.push_back(std::move(note));
}

void ScriptPlayer::learn_prepare(std::span<const uint8_t> payload) {
    const auto r = parse_prepare_2661(payload);
    if (!r) return;
    for (const auto& l : s_.prepares) {
        const auto owner = var(l.owner_var), hash = var(l.hash_var);
        if (!owner || !hash || r->owner != static_cast<uint32_t>(*owner) || r->hash != static_cast<uint32_t>(*hash)) {
            continue;
        }
        const uint32_t node = l.node_var.empty() ? 0 : static_cast<uint32_t>(var(l.node_var).value_or(0));
        const uint32_t npc = r->npc(node);
        learn_var(l.seq_var, r->seq);
        if (npc) learn_var(l.npc_var, npc);
        notes_.push_back(std::format("{} = 0x{:X}, {} = {} (2661 of owner 0x{:08X}, resource hash 0x{:08X}, {} "
                                     "bindings, {})",
                                     l.seq_var, r->seq, l.npc_var, npc ? std::format("0x{:08X}", npc) : std::string("not in it"),
                                     r->owner, r->hash, r->bindings.size(),
                                     node ? std::format("node 0x{:08X}", node) : std::string("the first NPC of type 4/5")));
    }
}

void ScriptPlayer::on_server_packet(std::span<const uint8_t> packet, uint32_t /*now_ms*/) {
    auto v = parse_wire(packet, Dir::ToGuestClient);  // also the 7-byte logout Acks
    if (!v) return;
    ++received_;
    for (const auto& l : s_.learns) {
        if (l.opcode != v->opcode || l.offset + l.size > v->payload.size()) continue;
        uint64_t x = 0;
        for (uint8_t i = 0; i < l.size; ++i) x |= static_cast<uint64_t>(v->payload[l.offset + i]) << (8 * i);
        learn_var(l.var, x);
    }
    if (s_.child && s_.child->opcode == v->opcode) learn_child(v->payload);
    if (!s_.prepares.empty() && v->opcode == kOpPrepareSequencerAck) learn_prepare(v->payload);
    ++seen_[v->opcode];
    // An expect with a value (up to the next send): only an Ack holding that variable's value counts.
    for (size_t i = pc_; i < s_.steps.size() && s_.steps[i].kind != ScriptStep::Kind::Send; ++i) {
        const ScriptStep& st = s_.steps[i];
        if (st.kind != ScriptStep::Kind::Expect || !st.match_size || st.opcode != v->opcode) continue;
        if (st.match_offset + st.match_size > v->payload.size()) continue;
        uint64_t x = 0;
        for (uint8_t b = 0; b < st.match_size; ++b) x |= static_cast<uint64_t>(v->payload[st.match_offset + b]) << (8 * b);
        const auto want = var(st.var);
        if (want && x == *want) ++matched_[i];
    }
    if (state_ != State::Running) return;
    for (const auto& st : s_.steps) {
        if (st.kind != ScriptStep::Kind::Reply || st.on_opcode != v->opcode) continue;
        std::vector<uint8_t> out;
        if (build(st, out)) replies_.push_back(std::move(out));
    }
}

std::vector<std::vector<uint8_t>> ScriptPlayer::tick(uint32_t now_ms) {
    std::vector<std::vector<uint8_t>> out;
    if (state_ != State::Running) return out;
    out.swap(replies_);
    if (!started_) {
        started_ = true;
        step_started_ms_ = now_ms;
    }
    while (pc_ < s_.steps.size() && state_ == State::Running) {
        const ScriptStep& st = s_.steps[pc_];
        if (st.kind == ScriptStep::Kind::Reply) {
            ++pc_;  // replies are standing handlers, not steps
        } else if (st.kind == ScriptStep::Kind::Send) {
            std::vector<uint8_t> p;
            if (!build(st, p)) break;
            out.push_back(std::move(p));
            ++sent_;
            seen_.clear();  // an expect after this send wants an answer to it
            matched_.clear();
            ++pc_;
            step_started_ms_ = now_ms;
        } else if (st.kind == ScriptStep::Kind::Expect && st.match_size) {
            auto it = matched_.find(pc_);
            if (it != matched_.end() && it->second > 0) {
                --it->second;
                ++pc_;
                step_started_ms_ = now_ms;
            } else if (now_ms - step_started_ms_ >= st.ms) {
                fail(std::format("line {}: no Ack {} holding {} (0x{:X}) at +{} within {} ms (Acks {} of any value: "
                                 "{})",
                                 st.line, st.opcode, st.var, var(st.var).value_or(0), st.match_offset, st.ms,
                                 st.opcode, seen_.count(st.opcode) ? seen_.at(st.opcode) : 0));
            } else {
                break;
            }
        } else if (st.kind == ScriptStep::Kind::Expect) {
            auto it = seen_.find(st.opcode);
            if (it != seen_.end() && it->second > 0) {
                --it->second;
                ++pc_;
                step_started_ms_ = now_ms;
            } else if (now_ms - step_started_ms_ >= st.ms) {
                fail(std::format("line {}: no Ack {} within {} ms", st.line, st.opcode, st.ms));
            } else {
                break;
            }
        } else if (st.kind == ScriptStep::Kind::ExpectLearned) {
            if (learned(st.var)) {
                ++pc_;
                step_started_ms_ = now_ms;
            } else if (now_ms - step_started_ms_ >= st.ms) {
                fail(std::format("line {}: {} not learned within {} ms", st.line, st.var, st.ms));
            } else {
                break;
            }
        } else {  // Wait
            if (now_ms - step_started_ms_ < st.ms) break;
            ++pc_;
            step_started_ms_ = now_ms;
        }
    }
    if (state_ == State::Running && pc_ >= s_.steps.size()) state_ = State::Done;
    return out;
}

std::string ScriptPlayer::describe() const {
    const char* st = state_ == State::Running ? "running" : state_ == State::Done ? "done" : "FAILED";
    std::string where = "-";
    if (pc_ < s_.steps.size()) {
        const auto& s = s_.steps[pc_];
        if (s.kind == ScriptStep::Kind::Expect) {
            where = s.match_size ? std::format("waiting for Ack {} holding {}", s.opcode, s.var)
                                 : std::format("waiting for Ack {}", s.opcode);
        }
        else if (s.kind == ScriptStep::Kind::ExpectLearned) where = std::format("waiting to learn {}", s.var);
        else if (s.kind == ScriptStep::Kind::Wait) where = std::format("waiting {} ms", s.ms);
        else where = s.label;
    }
    return std::format("script {} at step {}/{} ({}) | sent {} received {}{}", st, pc_, s_.steps.size(), where,
                       sent_, received_, error_.empty() ? "" : " | " + error_);
}

}  // namespace cdmp::troc
