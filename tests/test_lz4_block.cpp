#include "game/Lz4Block.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* description) {
    if (condition) {
        std::cout << "PASS: " << description << '\n';
    } else {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

void appendLe32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

void appendBlock(std::vector<std::uint8_t>& stream, std::uint8_t token,
                 const std::vector<std::uint8_t>& encoded,
                 const std::vector<std::uint8_t>& decoded) {
    static constexpr std::uint8_t magic[] = {
        'L', 'Z', '4', 'B', 'l', 'o', 'c', 'k'
    };
    stream.insert(stream.end(), std::begin(magic), std::end(magic));
    stream.push_back(token);
    appendLe32(stream, static_cast<std::uint32_t>(encoded.size()));
    appendLe32(stream, static_cast<std::uint32_t>(decoded.size()));
    appendLe32(stream, cppfm::lz4_block::detail::xxhash32(
        decoded.data(), decoded.size(),
        cppfm::lz4_block::kDefaultChecksumSeed) &
        cppfm::lz4_block::kChecksumMask);
    stream.insert(stream.end(), encoded.begin(), encoded.end());
}

void appendEndMark(std::vector<std::uint8_t>& stream) {
    static constexpr std::uint8_t magic[] = {
        'L', 'Z', '4', 'B', 'l', 'o', 'c', 'k'
    };
    stream.insert(stream.end(), std::begin(magic), std::end(magic));
    stream.push_back(0x10); // raw method, 1 KiB block-size level
    appendLe32(stream, 0);
    appendLe32(stream, 0);
    appendLe32(stream, 0);
}

template <typename Function>
bool throws(Function&& function) {
    try {
        function();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

} // namespace

int main() {
    using cppfm::lz4_block::decompress;
    using cppfm::lz4_block::detail::xxhash32;
    const std::vector<std::uint8_t> empty;
    const std::vector<std::uint8_t> abc{'a', 'b', 'c'};
    std::vector<std::uint8_t> extendedOutput(301, 'A');
    extendedOutput.reserve(306);
    extendedOutput.push_back('1');
    extendedOutput.push_back('2');
    extendedOutput.push_back('3');
    extendedOutput.push_back('4');
    extendedOutput.push_back('5');
    check(xxhash32(empty.data(), empty.size()) == 0x02cc5d05U,
          "XXHash32 empty seed-zero reference vector");
    check(xxhash32(abc.data(), abc.size()) == 0x32d153ffU,
          "XXHash32 abc seed-zero reference vector");
    const auto seededHash = xxhash32(
        abc.data(), abc.size(), cppfm::lz4_block::kDefaultChecksumSeed);
    check(seededHash == 0x4d4cb222U,
          "XXHash32 lz4-java 1.8.0 default-seed raw reference vector");
    check((seededHash & cppfm::lz4_block::kChecksumMask) == 0x0d4cb222U,
          "lz4-java 1.8.0 checksum uses the low 28 bits of XXHash32");

    // Golden streams emitted by lz4-java 1.8.0's default
    // LZ4BlockOutputStream constructor (64 KiB blocks). Their checksums and
    // framing are fixed bytes, independent of this test's XXHash helper.
    const std::vector<std::uint8_t> lz4JavaRawFixture{
        0x4c, 0x5a, 0x34, 0x42, 0x6c, 0x6f, 0x63, 0x6b,
        0x16, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x22, 0xb2, 0x4c, 0x0d, 0x61, 0x62, 0x63,
        0x4c, 0x5a, 0x34, 0x42, 0x6c, 0x6f, 0x63, 0x6b,
        0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00
    };
    check(decompress(lz4JavaRawFixture.data(), lz4JavaRawFixture.size(), 64) ==
              abc,
          "decodes independent lz4-java 1.8.0 raw-block fixture");

    const std::vector<std::uint8_t> lz4JavaCompressedFixture{
        0x4c, 0x5a, 0x34, 0x42, 0x6c, 0x6f, 0x63, 0x6b,
        0x26, 0x0c, 0x00, 0x00, 0x00, 0x32, 0x01, 0x00, 0x00,
        0xbb, 0x4d, 0x3c, 0x0b,
        0x1f, 0x41, 0x01, 0x00, 0xff, 0x1a,
        0x50, 0x31, 0x32, 0x33, 0x34, 0x35,
        0x4c, 0x5a, 0x34, 0x42, 0x6c, 0x6f, 0x63, 0x6b,
        0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00
    };
    check(decompress(lz4JavaCompressedFixture.data(),
                     lz4JavaCompressedFixture.size(), 512) == extendedOutput,
          "decodes independent lz4-java 1.8.0 compressed-block fixture");

    std::vector<std::uint8_t> rawStream;
    appendBlock(rawStream, 0x10, abc, abc);
    appendEndMark(rawStream);
    check(decompress(rawStream.data(), rawStream.size(), 64) == abc,
          "LZ4Block raw block decodes with checksum verification");

    const std::vector<std::uint8_t> repeated{
        'a', 'b', 'c', 'a', 'b', 'c', 'a', 'b', 'c', 'a', 'b', 'c',
        '1', '2', '3', '4', '5'
    };
    // Raw LZ4 block: three literals, a nine-byte distance-three match, then
    // five trailing literals. This exercises overlapping match expansion.
    const std::vector<std::uint8_t> compressed{
        0x35, 'a', 'b', 'c', 0x03, 0x00, 0x50, '1', '2', '3', '4', '5'
    };
    std::vector<std::uint8_t> compressedStream;
    appendBlock(compressedStream, 0x20, compressed, repeated);
    appendEndMark(compressedStream);
    check(decompress(compressedStream.data(), compressedStream.size(), 64) ==
              repeated,
          "LZ4Block compressed block expands overlapping matches");

    const std::vector<std::uint8_t> extendedMatch{
        0x1f, 'A', 0x01, 0x00, 0xff, 0x1a,
        0x50, '1', '2', '3', '4', '5'
    };
    std::vector<std::uint8_t> extendedMatchStream;
    appendBlock(extendedMatchStream, 0x20, extendedMatch, extendedOutput);
    appendEndMark(extendedMatchStream);
    check(decompress(extendedMatchStream.data(), extendedMatchStream.size(),
                     512) == extendedOutput,
          "LZ4Block handles extended overlapping match lengths");

    std::vector<std::uint8_t> longLiterals(20);
    for (std::size_t i = 0; i < longLiterals.size(); ++i)
        longLiterals[i] = static_cast<std::uint8_t>('a' + i);
    std::vector<std::uint8_t> extendedLiterals{0xf0, 5};
    extendedLiterals.insert(extendedLiterals.end(), longLiterals.begin(),
                            longLiterals.end());
    std::vector<std::uint8_t> extendedLiteralStream;
    appendBlock(extendedLiteralStream, 0x20, extendedLiterals, longLiterals);
    appendEndMark(extendedLiteralStream);
    check(decompress(extendedLiteralStream.data(),
                     extendedLiteralStream.size(), 64) == longLiterals,
          "LZ4Block handles extended literal lengths");

    auto badChecksum = rawStream;
    badChecksum[17] ^= 1;
    check(throws([&] { (void)decompress(badChecksum.data(),
                                      badChecksum.size(), 64); }),
          "LZ4Block rejects checksum mismatch");
    check(throws([&] { (void)decompress(rawStream.data(),
                                       rawStream.size(), 2); }),
          "LZ4Block respects aggregate output cap");
    check(throws([&] { (void)decompress(rawStream.data(),
                                       rawStream.size() - 1, 64); }),
          "LZ4Block rejects a missing end marker");
    check(throws([&] { (void)decompress(rawStream.data(), 7, 64); }),
          "LZ4Block rejects a truncated header");

    auto badOffset = compressedStream;
    // Stream header is 21 bytes; the LZ4 sequence's offset begins after its
    // token and three literal bytes.
    badOffset[21 + 1 + 3] = 0;
    badOffset[21 + 1 + 3 + 1] = 0;
    check(throws([&] { (void)decompress(badOffset.data(),
                                      badOffset.size(), 64); }),
          "LZ4Block rejects zero match offset");

    return failures == 0 ? 0 : 1;
}
