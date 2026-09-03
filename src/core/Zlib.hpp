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

// plan46 §1 W-14(b): strict network-path inflate. Unlike decompressRaw
// (uncompress() ignores trailing garbage), this verifies that the deflate
// stream consumes EXACTLY the supplied input and produces EXACTLY the
// declared size — over-long frames with trailing junk are rejected.
inline void decompressChecked(const std::uint8_t* src, std::size_t n,
                              std::size_t expected,
                              std::vector<std::uint8_t>& out) {
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
                              std::vector<std::uint8_t>& out) {
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
        out.insert(out.end(), buf, buf + sizeof(buf) - zs.avail_out);
    } while (ret != Z_STREAM_END);
    inflateEnd(&zs);
}

} // namespace cppfm
