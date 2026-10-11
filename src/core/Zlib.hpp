// Bounded zlib (RFC1950 and gzip) helpers for packets and Anvil regions.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <stdexcept>
#include <limits>
#include <string>
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
namespace zlib_detail {

class InflateStream final {
public:
    explicit InflateStream(int windowBits) {
        if (inflateInit2(&stream_, windowBits) != Z_OK)
            throw std::runtime_error("inflateInit2 failed");
        initialized_ = true;
    }

    InflateStream(const InflateStream&) = delete;
    InflateStream& operator=(const InflateStream&) = delete;

    ~InflateStream() {
        if (initialized_) inflateEnd(&stream_);
    }

    z_stream& get() noexcept { return stream_; }

    void reset(int windowBits, Bytef* nextInput, uInt remainingInput) {
        if (inflateReset2(&stream_, windowBits) != Z_OK)
            throw std::runtime_error("inflateReset2 failed");
        stream_.next_in = nextInput;
        stream_.avail_in = remainingInput;
    }

private:
    z_stream stream_{};
    bool initialized_ = false;
};

inline void decompressUnknown(const std::uint8_t* src, std::size_t n,
                              std::vector<std::uint8_t>& out,
                              std::size_t maxOutput, int windowBits,
                              bool allowConcatenatedMembers,
                              const char* formatName) {
    validateZlibInput(src, n);
    if (n > static_cast<std::size_t>(std::numeric_limits<uInt>::max()))
        throw std::length_error("zlib input is too large for a stream");
    if (maxOutput == 0 || maxOutput > kMaxDecompressedBytes)
        throw std::invalid_argument("invalid zlib output limit");
    InflateStream inflater(windowBits);
    auto& zs = inflater.get();
    zs.next_in = const_cast<Bytef*>(src);
    zs.avail_in = static_cast<uInt>(n);
    out.clear();
    std::uint8_t buf[16384];
    for (;;) {
        zs.next_out = buf;
        zs.avail_out = sizeof(buf);
        const int ret = inflate(&zs, Z_NO_FLUSH);
        const std::size_t produced = sizeof(buf) - zs.avail_out;
        if (out.size() > maxOutput || produced > maxOutput - out.size())
            throw std::length_error("zlib output exceeds limit");
        out.insert(out.end(), buf, buf + produced);

        if (ret == Z_STREAM_END) {
            if (allowConcatenatedMembers && zs.avail_in != 0) {
                Bytef* const nextInput = zs.next_in;
                const uInt remainingInput = zs.avail_in;
                inflater.reset(windowBits, nextInput, remainingInput);
                continue;
            }
            if (zs.avail_in != 0)
                throw std::runtime_error("zlib stream has trailing bytes");
            return;
        }
        if (ret != Z_OK && ret != Z_STREAM_END) {
            throw std::runtime_error(std::string(formatName) +
                                     " stream corrupt");
        }
        if (ret == Z_OK && produced == 0 && zs.avail_in == 0) {
            throw std::runtime_error(std::string(formatName) +
                                     " stream is truncated");
        }
    }
}

} // namespace zlib_detail

inline void decompressUnknown(const std::uint8_t* src, std::size_t n,
                              std::vector<std::uint8_t>& out,
                              std::size_t maxOutput = kMaxDecompressedBytes) {
    zlib_detail::decompressUnknown(src, n, out, maxOutput, MAX_WBITS, false,
                                   "zlib");
}

// Vanilla Anvil compression type 1 is an RFC 1952 gzip stream. Java's
// GZIPInputStream accepts concatenated members; retain that behavior while
// enforcing the same aggregate output bound as region NBT decoding.
inline void decompressGzipUnknown(
    const std::uint8_t* src, std::size_t n, std::vector<std::uint8_t>& out,
    std::size_t maxOutput = kMaxDecompressedBytes) {
    zlib_detail::decompressUnknown(src, n, out, maxOutput, MAX_WBITS + 16,
                                   true, "gzip");
}

} // namespace cppfm
