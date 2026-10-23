#pragma once

#include "hft/instr/event.hpp"

#include <chrono>
#include <cstdint>

namespace hft::instr {

struct ClockReading {
    uint64_t ticks;
    uint32_t aux;
};

// Compile-time clock selection. Production x86 uses rdtscp. Tests and non-x86
// builds use steady_clock nanoseconds so the suite does not require a TSC.
#if defined(HFT_INSTR_FORCE_STEADY_CLOCK)
inline constexpr uint32_t kActiveClockSource = kClockSteady;
#elif defined(__x86_64__) || defined(__i386__)
inline constexpr uint32_t kActiveClockSource = kClockRdtscp;
#else
inline constexpr uint32_t kActiveClockSource = kClockSteady;
#endif

[[gnu::always_inline]] inline ClockReading read_clock() noexcept {
#if !defined(HFT_INSTR_FORCE_STEADY_CLOCK) && (defined(__x86_64__) || defined(__i386__))
    uint32_t lo = 0;
    uint32_t hi = 0;
    uint32_t aux = 0;
    // "memory" keeps the compiler from sliding the timestamp across nearby
    // loads and stores. rdtscp itself waits for earlier instructions to retire.
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux) : : "memory");
    ClockReading out;
    out.ticks = (uint64_t{hi} << 32) | lo;
    out.aux = aux;
    return out;
#else
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    ClockReading out;
    out.ticks = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    out.aux = 0;
    return out;
#endif
}

}  // namespace hft::instr
