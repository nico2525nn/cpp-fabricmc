#pragma once

#include <atomic>
#include <cstdint>

namespace cppfm::redstone_detail {

inline std::int64_t currentTick(
    const std::int64_t* plain,
    const std::atomic<std::int64_t>* atomic) noexcept {
    if (atomic) return atomic->load(std::memory_order_relaxed);
    return plain ? *plain : 0;
}

} // namespace cppfm::redstone_detail
