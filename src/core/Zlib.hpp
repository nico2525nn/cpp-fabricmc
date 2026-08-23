// Optional zlib (RFC1950/raw deflate) helpers for packet compression.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <stdexcept>
#include <zlib.h>

namespace cppfm {

inline void compressRaw(const std::uint8_t* src, std::size_t n,
                        std::vector<std::uint8_t>& out) {
    uLongf bound = compressBound(static_cast<uLong>(n));
    out.resize(bound);
    if (compress2(out.data(), &bound, src, static_cast<uLong>(n),
                  Z_DEFAULT_COMPRESSION) != Z_OK)
        throw std::runtime_error("zlib compress failed");
    out.resize(bound);
}

inline void decompressRaw(const std::uint8_t* src, std::size_t n,
                          std::size_t expected,
                          std::vector<std::uint8_t>& out) {
    out.resize(expected);
    uLongf dst = static_cast<uLongf>(expected);
    if (uncompress(out.data(), &dst, src, static_cast<uLong>(n)) != Z_OK ||
        dst != expected)
        throw std::runtime_error("zlib decompress failed");
    out.resize(dst);
}

} // namespace cppfm
