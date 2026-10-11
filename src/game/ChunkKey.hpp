// Shared, dimension-qualified key for chunk I/O and encoded-body caches.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cppfm::chunk_io_detail {

struct DimensionChunkKey {
    std::int8_t dimension = 0;
    std::int32_t cx = 0;
    std::int32_t cz = 0;

    friend bool operator==(const DimensionChunkKey& lhs,
                           const DimensionChunkKey& rhs) noexcept {
        return lhs.dimension == rhs.dimension && lhs.cx == rhs.cx &&
               lhs.cz == rhs.cz;
    }
};

struct DimensionChunkKeyHash {
    std::size_t operator()(const DimensionChunkKey& key) const noexcept {
        // SplitMix-style finalisation keeps nearby signed coordinates from
        // producing long runs of equal low hash bits.
        std::uint64_t hash = static_cast<std::uint32_t>(key.cx);
        hash = (hash ^ (hash >> 30)) * 0xbf58476d1ce4e5b9ULL;
        hash ^= static_cast<std::uint32_t>(key.cz) + 0x9e3779b9U +
                (hash << 6) + (hash >> 2);
        hash ^= static_cast<std::uint8_t>(key.dimension) + 0x517cc1b7U +
                (hash << 6) + (hash >> 2);
        hash ^= hash >> 27;
        hash *= 0x94d049bb133111ebULL;
        hash ^= hash >> 31;
        return static_cast<std::size_t>(hash);
    }
};

inline std::int8_t canonicalDimension(std::int8_t dimension) noexcept {
    return dimension == -1 ? -1 : dimension == 1 ? 1 : 0;
}

inline DimensionChunkKey dimensionChunkKey(std::int8_t dimension,
                                           std::int32_t cx,
                                           std::int32_t cz) noexcept {
    return {canonicalDimension(dimension), cx, cz};
}

} // namespace cppfm::chunk_io_detail
