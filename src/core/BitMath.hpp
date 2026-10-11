#pragma once

#include <bit>
#include <cstdint>

namespace cppfm {

// Number of bits needed for values in [0, valueCount). Handles zero and the
// full uint32_t range without a shift-width overflow.
inline int ceilLog2(std::uint32_t valueCount) noexcept {
    if (valueCount <= 1) return 0;
    return static_cast<int>(std::bit_width(valueCount - 1));
}

} // namespace cppfm
