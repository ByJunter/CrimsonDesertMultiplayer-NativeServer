#include "save_sim.h"

#include <windows.h>

#include <cstdio>
#include <format>

using namespace cdmp::session2;
using gsw::AckResult;

namespace {

std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}

bool read_all(const std::wstring& path, std::vector<uint8_t>& out, std::string& why) {
    out.clear();
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) {
        why = "cannot read " + narrow(path);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0 || static_cast<uint64_t>(size) > gsv::kMaxFileSize) {
        std::fclose(f);
        why = narrow(path) + ": larger than 16 MB";
        return false;
    }
    out.resize(static_cast<size_t>(size));
    const size_t got = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (got != out.size()) {
        why = "cannot read " + narrow(path) + " whole";
        return false;
    }
    return true;
}

bool write_all(const std::wstring& path, const void* data, size_t size) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    const bool ok = size == 0 || std::fwrite(data, 1, size, f) == size;
    return std::fclose(f) == 0 && ok;
}

std::string utc_now() {
    SYSTEMTIME t{};
    GetSystemTime(&t);
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                       t.wSecond);
}

}  // namespace

bool SaveSim::load(uint32_t default_slot, std::string& why) {
    slot_ = o_.slot.value_or(default_slot);
    if (o_.guest_save.empty()) return true;
    if (!gsw::offer_slot_ok(slot_)) {
        why = std::format("--guest-slot {} is not 0-2 or 100-108 (3 and 206 are never offered)", slot_);
        return false;
    }
    if (!read_all(o_.guest_save + L"\\lobby.save", lobby_, why) || !read_all(o_.guest_save + L"\\save.save", save_, why)) {
        return false;
    }
    if (gsw::check_pair(lobby_, save_, why) != AckResult::Ok) return false;
    files_ = gsw::files_of(lobby_, save_);
    xfer_ = static_cast<uint32_t>(GetTickCount64() & 0xFFFFFF) << 4;
    loaded_ = true;
    std::printf("Guest save: %s ready as slot %u | lobby %u B sha256 %s | save %u B sha256 %s\n",
                narrow(o_.guest_save).c_str(), slot_, files_[0].size, cdmp::sha256::hex(files_[0].sha).c_str(),
                files_[1].size, cdmp::sha256::hex(files_[1].sha).c_str());
    return true;
}

std::vector<std::vector<uint8_t>> SaveSim::on_host_seen() {
    std::vector<std::vector<uint8_t>> out;
    if (!loaded_ || offered_) return out;
    offered_ = true;
    gsw::Offer o;
    o.xfer = ++xfer_;
    o.guest_id = o_.guest_id;
    o.slot = slot_;
    o.base_gen = o_.base_gen;
    o.files = files_;
    out.push_back(gsw::frame(gsw::encode_offer(o)));
    gsw::Outgoing chunks(o.xfer, lobby_, save_);
    std::vector<uint8_t> payload;
    while (chunks.next(payload)) out.push_back(gsw::frame(payload));
    std::printf("Guest save: SAVE_OFFER xfer %u (slot %u, guest id %llu, base gen %u) + %u chunks -> the host\n", o.xfer,
                o.slot, static_cast<unsigned long long>(o.guest_id), o.base_gen, chunks.chunks());
    return out;
}

bool SaveSim::store(const gsw::Result& r, std::string& why) {
    if (o_.results.empty()) return true;
    CreateDirectoryW(o_.results.c_str(), nullptr);
    const std::wstring dir = o_.results + std::format(L"\\gen{}", r.gen);
    CreateDirectoryW(dir.c_str(), nullptr);
    gsw::IncomingManifest m;
    m.gen = r.gen;
    m.host_id = status_ ? status_->host_id : 0;
    m.slot = r.slot;
    m.reason = r.reason;
    m.time_unix = r.time_unix;
    m.received_utc = utc_now();
    m.files = r.files;
    m.root = r.root;
    m.base = r.base;
    const std::string text = gsw::incoming_manifest_text(m);
    const auto& lobby = down_.file(gsw::Kind::Lobby);
    const auto& save = down_.file(gsw::Kind::Save);
    if (!write_all(dir + L"\\lobby.save", lobby.data(), lobby.size()) ||
        !write_all(dir + L"\\save.save", save.data(), save.size()) ||
        !write_all(dir + L"\\manifest.txt", text.data(), text.size())) {
        why = "cannot write " + narrow(dir);
        return false;
    }
    return true;
}

bool SaveSim::on_host_frame(const std::vector<uint8_t>& packet, std::vector<std::vector<uint8_t>>& replies) {
    const auto payload = gsw::save_payload(packet);
    if (!payload) return false;
    const std::span<const uint8_t> p = *payload;
    auto result_ack = [&](uint32_t xfer, AckResult r, uint32_t gen) {
        replies.push_back(gsw::frame(gsw::encode_ack(gsw::kStateResultAck, gsw::Ack{xfer, static_cast<uint8_t>(r), gen})));
    };
    switch (p[0]) {
        case gsw::kStateAck:
            if (const auto a = gsw::decode_ack(p, gsw::kStateAck); a && a->xfer == xfer_) {
                ack_ = a->result;
                ack_gen_ = a->gen;
                std::printf("Guest save: SAVE_ACK xfer %u: %s%s\n", a->xfer, gsw::ack_result_name(a->result),
                            a->gen ? std::format(" (the host continues this save from its mailbox gen{})", a->gen).c_str()
                                   : "");
            }
            return true;
        case gsw::kStateStatus:
            if (const auto s = gsw::decode_status(p)) {
                if (!status_ || status_->mode != s->mode) {
                    std::printf("Guest save: SAVE_STATUS mode %u = %s | host %llu | last gen %u\n", s->mode,
                                gsw::host_mode_name(s->mode), static_cast<unsigned long long>(s->host_id), s->last_gen);
                }
                status_ = *s;
            }
            return true;
        case gsw::kStateResult:
            if (const auto r = gsw::decode_result(p)) {
                ++results_;
                meta_ = *r;
                if (const AckResult b = down_.begin(r->xfer, r->files); b != AckResult::Ok) {
                    result_ack(r->xfer, b, r->gen);
                } else {
                    std::printf("Guest save: SAVE_RESULT gen%u (xfer %u, %s, slot %u, line %s; lobby %u B, save %u B)\n",
                                r->gen, r->xfer, gsw::reason_name(r->reason), r->slot,
                                gsw::digest_zero(r->root) ? "none"
                                                          : (cdmp::sha256::hex(r->root).substr(0, 16) + "...").c_str(),
                                r->files[0].size, r->files[1].size);
                }
            }
            return true;
        case gsw::kStateChunk:
            if (const auto c = gsw::decode_chunk(p)) {
                AckResult err = AckResult::Ok;
                const auto step = down_.add(*c, err);
                if (step == gsw::Assembler::Step::Error) {
                    result_ack(c->xfer, err, meta_.gen);
                    ++failed_;
                } else if (step == gsw::Assembler::Step::Complete) {
                    std::string why;
                    AckResult v = down_.verify(why);
                    if (v == AckResult::Ok && !store(meta_, why)) v = AckResult::Io;
                    result_ack(meta_.xfer, v, meta_.gen);
                    if (v == AckResult::Ok) {
                        ++stored_;
                        last_gen_ = meta_.gen;
                        std::printf("Guest save: gen%u checked (SHA-256, header)%s; SAVE_RESULT_ACK ok\n", meta_.gen,
                                    o_.results.empty()
                                        ? ""
                                        : std::format(" and stored in {}\\gen{}", narrow(o_.results), meta_.gen).c_str());
                    } else {
                        ++failed_;
                        std::printf("Guest save: gen%u FAILED: %s; SAVE_RESULT_ACK %s\n", meta_.gen, why.c_str(),
                                    gsw::ack_result_name(static_cast<uint8_t>(v)));
                    }
                    down_.reset();
                }
            }
            return true;
        default: return true;
    }
}

void SaveSim::on_host_packet(uint16_t opcode, std::span<const uint8_t> payload) {
    if (opcode != gsw::kOpSaveToFileCurrentPlayerAck) return;
    ++n3016_;
    if (const auto a = gsw::parse_save_ack(payload)) {
        const bool saved = a->ok && a->result == 0;
        if (!saved) ++n3016_failed_;
        std::printf("Guest save: SaveToFileCurrentPlayerAck 3016 from the server: character 0x%08X slot %u -> %s (result "
                    "0x%X)\n",
                    a->key, a->slot, saved ? "SAVED" : "NOT saved", a->result);
    } else {
        std::printf("Guest save: SaveToFileCurrentPlayerAck 3016 of %zu bytes (not the 14 expected)\n", payload.size());
    }
}

std::string SaveSim::describe() const {
    return std::format("guest save: offered {} ack {} | results {} stored {} failed {} last gen {} | 3016 {}", offered_,
                       ack_ ? gsw::ack_result_name(*ack_) : "-", results_, stored_, failed_, last_gen_, n3016_);
}

std::vector<uint8_t> SaveSim::final_save_req() { return gsw::make_final_save_req(); }

