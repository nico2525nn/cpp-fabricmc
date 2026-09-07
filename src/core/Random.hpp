// Process-wide gameplay RNG.
//
// The current gameplay implementation intentionally does not claim vanilla
// RNG parity. It does, however, need one well-defined access point because
// sessions and the tick loop can draw concurrently. An atomic LCG keeps the
// old non-negative integer contract without taking a process-wide mutex on
// every random-tick sample. A future vanilla-compatible RNG can replace this
// implementation without changing gameplay call sites.
#pragma once

#include <atomic>
#include <cstdint>

namespace cppfm {

inline std::atomic<std::uint64_t>& randomState() noexcept {
    static std::atomic<std::uint64_t> state{0x9E3779B97F4A7C15ULL};
    return state;
}

inline int nextRandom() noexcept {
    // Numerical Recipes' full-period 64-bit LCG.  The high bits have much
    // better quality than the low bits for callers that take a small modulo.
    auto& state = randomState();
    std::uint64_t current = state.load(std::memory_order_relaxed);
    for (;;) {
        const std::uint64_t next = current * 6364136223846793005ULL + 1442695040888963407ULL;
        if (state.compare_exchange_weak(current, next,
                                        std::memory_order_relaxed,
                                        std::memory_order_relaxed))
            return static_cast<int>((next >> 33) & 0x7FFFFFFFULL);
    }
}

inline void seedRandom(unsigned seed) noexcept {
    // Zero is valid input but would be less useful as a visible initial
    // state in diagnostics; map it to a non-zero deterministic state.
    randomState().store(seed == 0 ? 0x9E3779B97F4A7C15ULL : seed,
                        std::memory_order_relaxed);
}

} // namespace cppfm
