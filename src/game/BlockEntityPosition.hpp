#pragma once

#include <cstdint>

namespace cppfm {

inline constexpr std::int32_t kMinPackedBlockCoordinate = -(1 << 25);
inline constexpr std::int32_t kMaxPackedBlockCoordinate = (1 << 25) - 1;
inline constexpr std::int32_t kMinPackedBlockY = -(1 << 11);
inline constexpr std::int32_t kMaxPackedBlockY = (1 << 11) - 1;

inline constexpr bool isPackedBlockPosition(std::int32_t x, std::int32_t y,
                                            std::int32_t z) noexcept {
    return x >= kMinPackedBlockCoordinate &&
           x <= kMaxPackedBlockCoordinate &&
           z >= kMinPackedBlockCoordinate &&
           z <= kMaxPackedBlockCoordinate &&
           y >= kMinPackedBlockY && y <= kMaxPackedBlockY;
}

// Vanilla-style packed position: x:26 | z:26 | y:12 (signed fields).
inline std::int64_t posKey(std::int32_t x, std::int32_t y, std::int32_t z) {
    return (static_cast<std::int64_t>(static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(x)) & 0x3FFFFFFULL) << 38) |
           (static_cast<std::int64_t>(static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(z)) & 0x3FFFFFFULL) << 12) |
           static_cast<std::int64_t>(static_cast<std::uint64_t>(y) & 0xFFFULL);
}

inline std::int32_t posKeyUnpackX(std::int64_t k) {
    const std::uint64_t v = (static_cast<std::uint64_t>(k) >> 38) & 0x3FFFFFFULL;
    return static_cast<std::int32_t>(v >= (1ULL << 25) ? v - (1ULL << 26) : v);
}

inline std::int32_t posKeyUnpackZ(std::int64_t k) {
    const std::uint64_t v = (static_cast<std::uint64_t>(k) >> 12) & 0x3FFFFFFULL;
    return static_cast<std::int32_t>(v >= (1ULL << 25) ? v - (1ULL << 26) : v);
}

inline std::int32_t posKeyUnpackY(std::int64_t k) {
    const std::uint64_t v = static_cast<std::uint64_t>(k) & 0xFFFULL;
    return static_cast<std::int32_t>(v >= (1ULL << 11) ? v - (1ULL << 12) : v);
}

} // namespace cppfm
