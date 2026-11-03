#pragma once

#include <chrono>
#include <cstdint>

namespace cdmp {

// Millisecond clock for networking. Uses steady_clock (QueryPerformanceCounter)
// rather than GetTickCount64, whose ~15.6 ms resolution made loopback RTT
// read as 0. Wraps every ~49 days; callers compare with unsigned subtraction.
inline uint32_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// Microseconds on the same clock, 64-bit (no wrap): durations of the network
// thread's loop (common/loop_health.h).
inline uint64_t now_us() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

}  // namespace cdmp
