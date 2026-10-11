#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cppfm::lz4_block {

inline constexpr std::uint32_t kDefaultChecksumSeed = 0x9747b28cU;
inline constexpr std::uint32_t kChecksumMask = 0x0fffffffU;

namespace detail {

constexpr std::uint32_t kPrime1 = 2654435761U;
constexpr std::uint32_t kPrime2 = 2246822519U;
constexpr std::uint32_t kPrime3 = 3266489917U;
constexpr std::uint32_t kPrime4 = 668265263U;
constexpr std::uint32_t kPrime5 = 374761393U;

inline std::uint32_t readLittleEndian32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

inline std::uint32_t rotateLeft(std::uint32_t value, unsigned amount) noexcept {
    return (value << amount) | (value >> (32U - amount));
}

inline std::uint32_t round(std::uint32_t accumulator,
                           std::uint32_t input) noexcept {
    accumulator += input * kPrime2;
    accumulator = rotateLeft(accumulator, 13);
    return accumulator * kPrime1;
}

inline std::uint32_t xxhash32(const std::uint8_t* bytes, std::size_t size,
                              std::uint32_t seed = 0) {
    if (size != 0 && bytes == nullptr)
        throw std::invalid_argument("null LZ4Block checksum input");
    static constexpr std::uint8_t kEmptyInput = 0;
    if (bytes == nullptr) bytes = &kEmptyInput;

    const std::uint8_t* cursor = bytes;
    const std::uint8_t* const end = bytes + size;
    std::uint32_t hash = 0;
    if (size >= 16) {
        const std::uint8_t* const limit = end - 16;
        std::uint32_t v1 = seed + kPrime1 + kPrime2;
        std::uint32_t v2 = seed + kPrime2;
        std::uint32_t v3 = seed;
        std::uint32_t v4 = seed - kPrime1;
        do {
            v1 = round(v1, readLittleEndian32(cursor));
            cursor += 4;
            v2 = round(v2, readLittleEndian32(cursor));
            cursor += 4;
            v3 = round(v3, readLittleEndian32(cursor));
            cursor += 4;
            v4 = round(v4, readLittleEndian32(cursor));
            cursor += 4;
        } while (cursor <= limit);
        hash = rotateLeft(v1, 1) + rotateLeft(v2, 7) +
               rotateLeft(v3, 12) + rotateLeft(v4, 18);
    } else {
        hash = seed + kPrime5;
    }

    hash += static_cast<std::uint32_t>(size);
    while (static_cast<std::size_t>(end - cursor) >= 4) {
        hash += readLittleEndian32(cursor) * kPrime3;
        hash = rotateLeft(hash, 17) * kPrime4;
        cursor += 4;
    }
    while (cursor < end) {
        hash += static_cast<std::uint32_t>(*cursor) * kPrime5;
        hash = rotateLeft(hash, 11) * kPrime1;
        ++cursor;
    }
    hash ^= hash >> 15;
    hash *= kPrime2;
    hash ^= hash >> 13;
    hash *= kPrime3;
    hash ^= hash >> 16;
    return hash;
}

inline std::size_t readExtendedLength(const std::uint8_t* input,
                                     std::size_t inputSize,
                                     std::size_t& position,
                                     std::size_t initial) {
    std::size_t length = initial;
    if (initial != 15) return length;
    for (;;) {
        if (position >= inputSize)
            throw std::runtime_error("truncated LZ4Block sequence length");
        const std::uint8_t extra = input[position++];
        if (length > std::numeric_limits<std::size_t>::max() - extra)
            throw std::runtime_error("LZ4Block sequence length overflow");
        length += extra;
        if (extra != 255) return length;
    }
}

inline std::vector<std::uint8_t> decompressRawBlock(
    const std::uint8_t* input, std::size_t inputSize,
    std::size_t expectedSize) {
    if (inputSize != 0 && input == nullptr)
        throw std::invalid_argument("null LZ4Block payload");
    std::vector<std::uint8_t> output;
    output.reserve(expectedSize);
    std::size_t position = 0;
    while (position < inputSize) {
        const std::uint8_t token = input[position++];
        const auto literalLength = readExtendedLength(
            input, inputSize, position, static_cast<std::size_t>(token >> 4));
        if (literalLength > inputSize - position ||
            literalLength > expectedSize - output.size())
            throw std::runtime_error("invalid LZ4Block literal length");
        output.insert(output.end(), input + position,
                      input + position + literalLength);
        position += literalLength;

        // The final sequence in an LZ4 block consists of literals only.
        if (position == inputSize) break;
        if (inputSize - position < 2)
            throw std::runtime_error("truncated LZ4Block match offset");
        const std::size_t offset = static_cast<std::size_t>(input[position]) |
            (static_cast<std::size_t>(input[position + 1]) << 8);
        position += 2;
        if (offset == 0 || offset > output.size())
            throw std::runtime_error("invalid LZ4Block match offset");

        const auto encodedMatchLength = static_cast<std::size_t>(token & 0x0fU);
        const auto extraMatchLength = readExtendedLength(
            input, inputSize, position, encodedMatchLength);
        if (extraMatchLength > std::numeric_limits<std::size_t>::max() - 4)
            throw std::runtime_error("LZ4Block match length overflow");
        const auto matchLength = extraMatchLength + 4;
        if (matchLength > expectedSize - output.size())
            throw std::runtime_error("LZ4Block output exceeds declared size");
        for (std::size_t copied = 0; copied < matchLength; ++copied)
            output.push_back(output[output.size() - offset]);
    }
    if (output.size() != expectedSize)
        throw std::runtime_error("LZ4Block output size does not match header");
    return output;
}

} // namespace detail

// Decode lz4-java 1.8.0's LZ4Block stream (the codec used by Minecraft 1.21.4),
// not the unrelated LZ4 Frame format. The caller supplies an aggregate output
// cap so a hostile region cannot allocate without bound.
inline std::vector<std::uint8_t> decompress(const std::uint8_t* input,
                                            std::size_t inputSize,
                                            std::size_t maxOutputBytes) {
    static constexpr std::uint8_t kMagic[] = {
        'L', 'Z', '4', 'B', 'l', 'o', 'c', 'k'
    };
    constexpr std::size_t kHeaderSize = 8 + 1 + 4 + 4 + 4;
    constexpr std::uint8_t kRawMethod = 0x10;
    constexpr std::uint8_t kLz4Method = 0x20;
    if (inputSize != 0 && input == nullptr)
        throw std::invalid_argument("null LZ4Block stream");

    std::vector<std::uint8_t> output;
    std::size_t position = 0;
    bool foundEndMark = false;
    while (position < inputSize) {
        if (inputSize - position < kHeaderSize)
            throw std::runtime_error("truncated LZ4Block header");
        for (std::size_t i = 0; i < sizeof(kMagic); ++i) {
            if (input[position + i] != kMagic[i])
                throw std::runtime_error("invalid LZ4Block magic");
        }
        position += sizeof(kMagic);
        const std::uint8_t token = input[position++];
        const auto method = static_cast<std::uint8_t>(token & 0xf0U);
        const auto level = static_cast<unsigned>(token & 0x0fU);
        const auto compressedSize = static_cast<std::size_t>(
            detail::readLittleEndian32(input + position));
        position += 4;
        const auto decompressedSize = static_cast<std::size_t>(
            detail::readLittleEndian32(input + position));
        position += 4;
        const auto checksum = detail::readLittleEndian32(input + position);
        position += 4;

        if (compressedSize == 0 && decompressedSize == 0) {
            if (method != kRawMethod || checksum != 0 || position != inputSize)
                throw std::runtime_error("invalid LZ4Block end marker");
            foundEndMark = true;
            break;
        }
        if (compressedSize == 0 || decompressedSize == 0)
            throw std::runtime_error("invalid empty LZ4Block payload");
        const std::size_t maxBlockSize = std::size_t{1} << (10U + level);
        const std::size_t maxCompressedSize =
            maxBlockSize + maxBlockSize / 255U + 16U;
        if (decompressedSize > maxBlockSize ||
            compressedSize > maxCompressedSize ||
            compressedSize > inputSize - position ||
            decompressedSize > maxOutputBytes - output.size())
            throw std::runtime_error("LZ4Block length exceeds its bounds");

        std::vector<std::uint8_t> block;
        if (method == kRawMethod) {
            if (compressedSize != decompressedSize)
                throw std::runtime_error("invalid raw LZ4Block lengths");
            block.assign(input + position, input + position + compressedSize);
        } else if (method == kLz4Method) {
            block = detail::decompressRawBlock(
                input + position, compressedSize, decompressedSize);
        } else {
            throw std::runtime_error("unsupported LZ4Block method");
        }
        // lz4-java 1.8.0's StreamingXXHash32.asChecksum() exposes only the
        // low 28 bits, which are the bytes stored in LZ4Block headers.
        if ((detail::xxhash32(block.data(), block.size(),
                              kDefaultChecksumSeed) & kChecksumMask) !=
            checksum)
            throw std::runtime_error("LZ4Block checksum mismatch");
        output.insert(output.end(), block.begin(), block.end());
        position += compressedSize;
    }
    if (!foundEndMark)
        throw std::runtime_error("LZ4Block stream has no end marker");
    return output;
}

} // namespace cppfm::lz4_block
