#pragma once
// In-game overlay: hooks the real DXGI swap chain (below NVIDIA Streamline /
// DLSS frame generation) and draws an ImGui frame on top of every present.
//
// The hooks patch the vtables of DXGI's swap chain class, found by creating a
// throwaway D3D12 device + swap chain. Nothing in the game's own code is
// patched. Any failure inside the render path disables the overlay instead of
// taking the game down.

#include <cstdint>
#include <string>

namespace cdmp::render {

// Called once per frame between ImGui::NewFrame() and ImGui::Render(), on the
// game's present thread. width/height are the back buffer size.
using DrawFn = void (*)(float width, float height);

bool install(DrawFn draw);

struct Status {
    bool installed = false;
    bool disabled = false;          // turned off after an error
    uint64_t presents = 0;          // Present/Present1 calls seen
    uint64_t frames_drawn = 0;
    std::string queue_source;       // how we found the command queue
    std::string last_error;
};
Status status();
// Only the frame counter: one atomic, no lock and no string copy (for the present thread).
uint64_t status_frames_drawn() noexcept;
// Present/Present1 calls seen and the thread of the last one (freeze_watchdog's fallback source).
uint64_t status_presents() noexcept;
uint32_t present_thread_id() noexcept;

}  // namespace cdmp::render
