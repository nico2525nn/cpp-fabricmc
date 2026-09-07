// Optional zlib (RFC1950/raw deflate) helpers for packet compression.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <stdexcept>
#include <limits>
#include <zlib.h>

namespace cppfm {

inline constexpr std::size_t kMaxDecompressedBytes = 64u * 1024u * 1024u;

inline void validateZlibInput(const std::uint8_t* src, std::size_t n) {
    if (n != 0 && src == nullptr) throw std::invalid_argument("null zlib input");
    if (n > static_cast<std::size_t>(std::numeric_limits<uLong>::max()))
        throw std::length_error("zlib input is too large");
}

inline void compressRaw(const std::uint8_t* src, std::size_t n,
                        std::vector<std::uint8_t>& out) {
    validateZlibInput(src, n);
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
    validateZlibInput(src, n);
    if (expected > kMaxDecompressedBytes ||
        expected > static_cast<std::size_t>(std::numeric_limits<uLongf>::max()))
        throw std::length_error("zlib output is too large");
    out.resize(expected);
    uLongf dst = static_cast<uLongf>(expected);
    if (uncompress(out.data(), &dst, src, static_cast<uLong>(n)) != Z_OK ||
        dst != expected)
        throw std::runtime_error("zlib decompress failed");
    out.resize(dst);
}

inline void decompressChecked(const std::uint8_t* src, std::size_t n,
                              std::size_t expected,
                              std::vector<std::uint8_t>& out) {
    validateZlibInput(src, n);
    if (n > static_cast<std::size_t>(std::numeric_limits<uInt>::max()) ||
        expected > kMaxDecompressedBytes ||
        expected > static_cast<std::size_t>(std::numeric_limits<uInt>::max()))
        throw std::length_error("zlib stream exceeds decoder limits");
    if (expected == 0) throw std::runtime_error("zlib output must not be empty");
    out.resize(expected);
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) throw std::runtime_error("inflateInit failed");
    zs.next_in = const_cast<Bytef*>(src);
    zs.avail_in = static_cast<uInt>(n);
    zs.next_out = out.data();
    zs.avail_out = static_cast<uInt>(expected);
    const int ret = inflate(&zs, Z_FINISH);
    const std::size_t produced = static_cast<std::size_t>(zs.total_out);
    const bool fullyConsumed = (zs.avail_in == 0);
    inflateEnd(&zs);
    if (ret != Z_STREAM_END || !fullyConsumed || produced != expected)
        throw std::runtime_error("zlib decompress failed (size/consumption mismatch)");
    out.resize(produced);
}

// Inflate without knowing the output size (region files store raw zlib streams).
inline void decompressUnknown(const std::uint8_t* src, std::size_t n,
                              std::vector<std::uint8_t>& out,
                              std::size_t maxOutput = kMaxDecompressedBytes) {
    validateZlibInput(src, n);
    if (n > static_cast<std::size_t>(std::numeric_limits<uInt>::max()))
        throw std::length_error("zlib input is too large for a stream");
    if (maxOutput == 0 || maxOutput > kMaxDecompressedBytes)
        throw std::invalid_argument("invalid zlib output limit");
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) throw std::runtime_error("inflateInit failed");
    zs.next_in = const_cast<Bytef*>(src);
    zs.avail_in = static_cast<uInt>(n);
    out.clear();
    std::uint8_t buf[16384];
    int ret;
    do {
        zs.next_out = buf;
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&zs);
            throw std::runtime_error("zlib stream corrupt");
        }
        const std::size_t produced = sizeof(buf) - zs.avail_out;
        if (produced > maxOutput - out.size()) {
            inflateEnd(&zs);
            throw std::length_error("zlib output exceeds limit");
        }
        out.insert(out.end(), buf, buf + produced);
        if (ret == Z_OK && produced == 0 && zs.avail_in == 0) {
            inflateEnd(&zs);
            throw std::runtime_error("zlib stream made no progress");
        }
    } while (ret != Z_STREAM_END);
    inflateEnd(&zs);
}

} // namespace cppfm
