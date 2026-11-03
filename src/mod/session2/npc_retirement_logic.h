#pragma once
// S33: cancelación local por época/encarnación; sin memoria del juego.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
#include "mod/session2/observer_hold_acks_logic.h"

namespace cdmp::session2::nret {
// 15501 claves nativas: el límite admite toda la reserva, sin expulsar una
// cancelación antigua para hacer sitio. Lleno: el llamador NO retira la familia.
template <size_t N = 16384> class Retired {
    struct Entry { uint32_t key = 0; uint64_t generation = 0; };
    std::array<Entry, N> entries_{};
    size_t count_ = 0;
    uint32_t epoch_ = 0;
public:
    void clear() { count_ = 0; epoch_ = 0; }
    size_t size() const { return count_; }
    uint32_t epoch() const { return epoch_; }
    bool contains(uint32_t key, uint32_t epoch) const {
        if (epoch != epoch_) return false;
        for (size_t i = 0; i < count_; ++i) if (entries_[i].key == key) return true;
        return false;
    }
    // Token de operación >= nacimientos de TODOS los miembros de la familia.
    // La prevalidación asegura que un lleno no deje media familia cancelada.
    bool retire(uint32_t epoch, uint64_t generation, std::span<const uint32_t> keys) {
        if (!epoch || !generation || epoch < epoch_) return false;
        size_t extra = 0;
        for (size_t i = 0; i < keys.size(); ++i) {
            if ((keys[i] >> 28) != 0xB) return false;
            if (!contains(keys[i], epoch) &&
                std::find(keys.begin(), keys.begin() + i, keys[i]) == keys.begin() + i) ++extra;
        }
        if ((epoch == epoch_ ? count_ : 0) + extra > N) return false;
        if (epoch != epoch_) { count_ = 0; epoch_ = epoch; }
        for (const uint32_t key : keys) {
            size_t i = 0;
            while (i < count_ && entries_[i].key != key) ++i;
            if (i == count_) entries_[count_++] = {key, generation};
            else entries_[i].generation = std::max(entries_[i].generation, generation);
        }
        return true;
    }
    // Sólo nacimiento REAL posterior: un replay2005 o confirmación vieja no
    // revive la clave. Antes de llamar, la cola antigua debe haberse purgado.
    void created(uint32_t epoch, uint64_t generation, uint32_t key) {
        if (epoch < epoch_) return;
        if (epoch > epoch_) { count_ = 0; epoch_ = epoch; return; }
        for (size_t i = 0; i < count_; ++i) {
            if (entries_[i].key == key && generation > entries_[i].generation) {
                entries_[i] = entries_[--count_];
                return;
            }
        }
    }
};

enum class Filter : uint8_t { Unchanged, Changed, Drop, Malformed };
// No modifica el protocolo: compacta registros2005 completos y corrige len.
// 2516 SIEMPRE pasa: es la retirada que el cliente debe ver. Los otros Acks
// sólo se filtran si su esquema por clave ya está comprobado en oha::kHeldOps.
template <class IsRetired> Filter filter_packet(std::vector<uint8_t>& p, IsRetired retired) {
    if (p.size() < 5) return Filter::Unchanged;
    const uint16_t op = static_cast<uint16_t>(p[0] | (p[1] << 8));
    const size_t payload_size = static_cast<uint16_t>(p[3] | (p[4] << 8));
    if (payload_size != p.size() - 5) return Filter::Unchanged; // internos tienen otra cabecera
    if (op != 2005) {
        const auto key = oha::held_key_of(op, std::span<const uint8_t>(p).subspan(5));
        return key && retired(*key) ? Filter::Drop : Filter::Unchanged;
    }
    // Validación completa antes de escribir: una cola truncada no se reinterpreta.
    bool changed = false;
    for (size_t at = 5; at < p.size();) {
        if (p.size() - at < 2) return Filter::Malformed;
        const size_t len = static_cast<uint16_t>(p[at] | (p[at + 1] << 8));
        if (!len || len > p.size() - at - 2 || (p[at + 2] != 0 && len < 5)) return Filter::Malformed;
        if (p[at + 2] != 0 && retired(oha::le32(p.data() + at + 3))) changed = true;
        at += len + 2;
    }
    if (!changed) return Filter::Unchanged;
    size_t to = 5;
    bool has_actor = false;
    for (size_t at = 5; at < p.size();) {
        const size_t len = static_cast<uint16_t>(p[at] | (p[at + 1] << 8));
        const bool actor = p[at + 2] != 0;
        if (!actor || !retired(oha::le32(p.data() + at + 3))) {
            has_actor = has_actor || actor;
            std::memmove(p.data() + to, p.data() + at, len + 2);
            to += len + 2;
        }
        at += len + 2;
    }
    if (!has_actor) return Filter::Drop;
    p.resize(to);
    const uint16_t bytes = static_cast<uint16_t>(to - 5);
    std::memcpy(p.data() + 3, &bytes, sizeof bytes);
    return Filter::Changed;
}
} // namespace cdmp::session2::nret
