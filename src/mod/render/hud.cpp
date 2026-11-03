#include "mod/render/hud.h"

#include <windows.h>

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <format>
#include <mutex>

#include "common/math3d.h"
#include "mod/game/camera_reader.h"
#include "mod/log.h"
#include "mod/render/dx12_hook.h"
#include "mod/role.h"

namespace cdmp::render {
namespace {

constexpr float kHeadHeight = 2.1f;  // marker above the character's feet (m)
constexpr ImU32 kAccent = IM_COL32(230, 70, 60, 255);
constexpr ImU32 kShadow = IM_COL32(0, 0, 0, 200);

std::mutex g_mutex;
HudData g_data;
HudOptions g_options;
std::atomic<uint8_t> g_notice{0};
std::atomic<uint64_t> g_notice_until{0};  // GetTickCount64 deadline

const char* notice_text(Notice n) {
    switch (n) {
        case Notice::FastTravelVetoed:
            return "Viaje rapido no disponible en cooperativo (invitado): ve andando o a caballo";
        case Notice::ReentrySlotKept:
            return "Reentrada: se juega tu partida del anfitrión (la de tu última salida), no la ranura elegida";
        default:
            return nullptr;
    }
}

void text_shadowed(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text) {
    dl->AddText(ImVec2(pos.x + 1, pos.y + 1), kShadow, text);
    dl->AddText(pos, color, text);
}

void draw_bar(ImDrawList* dl, ImVec2 center, float width, float fraction, ImU32 color) {
    const ImVec2 a(center.x - width / 2, center.y);
    const ImVec2 b(center.x + width / 2, center.y + 5);
    dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 160));
    dl->AddRectFilled(a, ImVec2(a.x + width * std::clamp(fraction, 0.0f, 1.0f), b.y), color);
}

void draw_remote_marker(ImDrawList* dl, const game::ScreenPoint& p, const HudData& d,
                        float distance) {
    const ImVec2 c(p.x, p.y);
    dl->AddCircleFilled(c, 7.0f, kShadow);
    dl->AddCircleFilled(c, 5.0f, kAccent);
    const std::string name = d.peer_name.empty() ? "Jugador 2" : d.peer_name;
    const ImVec2 size = ImGui::CalcTextSize(name.c_str());
    text_shadowed(dl, ImVec2(c.x - size.x / 2, c.y - 28), IM_COL32_WHITE, name.c_str());
    if (d.remote->health_max > 0) {
        draw_bar(dl, ImVec2(c.x, c.y - 12), 70, d.remote->health / d.remote->health_max,
                 IM_COL32(90, 200, 90, 255));
    }
    char dist[32];
    std::snprintf(dist, sizeof dist, "%.0f m", distance);
    const ImVec2 ds = ImGui::CalcTextSize(dist);
    text_shadowed(dl, ImVec2(c.x - ds.x / 2, c.y + 10), IM_COL32(220, 220, 220, 255), dist);
}

}  // namespace

void hud_configure(const HudOptions& options) {
    std::lock_guard lock(g_mutex);
    g_options = options;
}

void hud_update(HudData data) {
    std::lock_guard lock(g_mutex);
    g_data = std::move(data);
}

void hud_notice(Notice n, uint32_t ms) noexcept {
    g_notice_until.store(GetTickCount64() + ms, std::memory_order_relaxed);
    g_notice.store(static_cast<uint8_t>(n), std::memory_order_relaxed);
}

void hud_draw(float width, float height) {
    static bool visible = true;
    static bool f8_was_down = false;
    static game::CameraReader camera;
    static bool camera_init = false;
    static ULONGLONG last_diag = 0;

    // GetAsyncKeyState is global: with two copies on one PC (CDMP_ROLE) only the one in front takes F8.
    const bool mine = role::current() == role::Role::None || [] {
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        return pid == GetCurrentProcessId();
    }();
    const bool f8_down = mine && (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (f8_down && !f8_was_down) visible = !visible;
    f8_was_down = f8_down;
    if (!visible) {
        // The panel is hidden (F8), a notice is not: its own small window while it lasts.
        if (GetTickCount64() < g_notice_until.load(std::memory_order_relaxed)) {
            if (const char* t = notice_text(static_cast<Notice>(g_notice.load(std::memory_order_relaxed)))) {
                ImGui::SetNextWindowPos(ImVec2(20, 20));
                ImGui::SetNextWindowBgAlpha(0.55f);
                ImGui::Begin("cdmp notice", nullptr,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
                ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", t);
                ImGui::End();
            }
        }
        return;
    }

    HudData d;
    HudOptions opt;
    {
        std::lock_guard lock(g_mutex);
        d = g_data;
        opt = g_options;
    }
    if (!camera_init) {
        camera.init();
        camera_init = true;
    }
    const auto cam = camera.read();

    // Is the camera position stored in world space or relative to the map
    // chunk like the player's physics position? Whichever is closer to us.
    std::optional<proto::Vec3> cam_world;
    bool cam_is_local = false;
    if (cam && d.local) {
        const float to_world = math::length(math::sub(cam->position, d.local->position));
        const float to_local = math::length(math::sub(cam->position, d.local->local_position));
        cam_is_local = to_local < to_world;
        cam_world = cam_is_local ? math::add(cam->position, d.local->chunk_origin) : cam->position;
    }

    // ---- status panel
    ImGui::SetNextWindowPos(ImVec2(20, 20));
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("cdmp", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.25f, 1), "Crimson Desert Multiplayer %s", CDMP_VERSION);
    ImGui::SameLine();
    ImGui::TextDisabled("[F8]");
    if (d.connected) {
        ImGui::Text("Red: conectado a '%s' (%.1f ms)", d.peer_name.c_str(), d.rtt_ms);
    } else {
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Red: buscando al otro jugador...");
    }
    if (!d.peer_problem.empty()) {
        // In English from the session; the essential part is the instruction.
        ImGui::TextColored(ImVec4(1, 0.35f, 0.3f, 1), "Versiones distintas del mod: actualizad los dos");
        ImGui::TextColored(ImVec4(1, 0.35f, 0.3f, 1), "%s", d.peer_problem.c_str());
    }
    if (d.local) {
        ImGui::Text("Tú: HP %.0f/%.0f  Stamina %.0f/%.0f", d.local->health, d.local->health_max,
                    d.local->stamina, d.local->stamina_max);
    } else {
        ImGui::TextDisabled("Tú: %s", d.local_status.c_str());
    }
    float remote_distance = 0;
    const bool apart = d.same_world && !*d.same_world;
    if (d.remote && d.local && apart) {
        ImGui::TextDisabled(d.s2_guest ? "Otro jugador: no está en tu mundo (no juegas en el del anfitrión)"
                                       : "Otro jugador: no está en tu mundo ahora (entrando, cargando o fuera)");
    } else if (d.remote && d.local) {
        remote_distance = math::length(math::sub(d.remote->pos, d.local->position));
        ImGui::Text("Otro jugador: HP %.0f/%.0f  a %.0f m", d.remote->health, d.remote->health_max,
                    remote_distance);
    }
    if (!d.mount_problem.empty()) {
        ImGui::TextColored(ImVec4(1, 0.35f, 0.3f, 1), "%s", d.mount_problem.c_str());
    }
    if (d.alone_after_remote) {
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Juegas EN SOLITARIO en tu PC (saliste al título).");
        // P8 re-entry: with [session2] rejoin the HUD says what the re-entry does instead of "restart the game".
        if (d.rejoin_text.empty()) {
            ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Para volver al mundo del anfitrión: cierra el juego y ábrelo.");
        }
    }
    if (!d.rejoin_text.empty()) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", d.rejoin_text.c_str());
    if (!cam) ImGui::TextDisabled("Cámara: %s", camera.last_failure().c_str());
    if (GetTickCount64() < g_notice_until.load(std::memory_order_relaxed)) {
        if (const char* t = notice_text(static_cast<Notice>(g_notice.load(std::memory_order_relaxed)))) {
            ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", t);
        }
    }
    ImGui::End();

    // ---- world markers
    if (!cam || !cam_world) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    if (d.remote && !apart) {
        const proto::Vec3 head{d.remote->pos.x, d.remote->pos.y + kHeadHeight, d.remote->pos.z};
        if (auto p = game::project(*cam, *cam_world, head, width, height)) {
            draw_remote_marker(dl, *p, d, remote_distance);
        }
    }

    std::optional<game::ScreenPoint> self;
    if (d.local) {
        const proto::Vec3 chest{d.local->position.x, d.local->position.y + 1.2f,
                                d.local->position.z};
        self = game::project(*cam, *cam_world, chest, width, height);
        if (opt.self_marker && self) {
            const ImVec2 c(self->x, self->y);
            dl->AddLine(ImVec2(c.x - 8, c.y), ImVec2(c.x + 8, c.y), IM_COL32(80, 220, 255, 255), 2);
            dl->AddLine(ImVec2(c.x, c.y - 8), ImVec2(c.x, c.y + 8), IM_COL32(80, 220, 255, 255), 2);
        }
    }

    // Numbers that let us verify the projection from the log alone: in third
    // person our own chest should land near the horizontal centre.
    // On the game's present thread: one line every 5 s, formatted into a stack
    // buffer and queued (the log is asynchronous, p8-impl-log-async.md: no I/O,
    // no wait for the disk here; P8 session 8's 5.3 s without a frame was this
    // line waiting behind a slow WriteFile). No std::string / nested format.
    const ULONGLONG now = GetTickCount64();
    if (now - last_diag >= 5000 && d.local) {
        last_diag = now;
        const uint64_t frames = status_frames_drawn();
        const char* space = cam_is_local ? "chunk-local" : "world";
        const char* axes = cam->axes_from_view ? "from view matrix" : "fallback (cross)";
        const float dist = math::length(math::sub(*cam_world, d.local->position));
        const float fov = 2.0f * std::atan(1.0f / cam->p11) * 57.2958f;
        if (self) {
            log::info("Overlay diag: frames {} | cam {} space, axes {} | cam->player {:.2f} m | self on screen ({:.0f}, "
                      "{:.0f}) of {:.0f}x{:.0f}, depth {:.2f} | fov {:.1f} deg",
                      frames, space, axes, dist, self->x, self->y, width, height, self->depth, fov);
        } else {
            log::info("Overlay diag: frames {} | cam {} space, axes {} | cam->player {:.2f} m | self on screen behind "
                      "camera | fov {:.1f} deg",
                      frames, space, axes, dist, fov);
        }
    }
}

}  // namespace cdmp::render
