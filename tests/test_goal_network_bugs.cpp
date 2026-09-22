#include "../src/core/ByteBuffer.hpp"
#include "../src/core/Zlib.hpp"
#include "../src/net/Connection.hpp"
#include "../src/net/Crypto.hpp"
#include "../src/net/MojangAuth.hpp"
#include "../src/net/PacketDecoder.hpp"
#include "../src/net/PacketEncoder.hpp"
#include "../src/net/PacketBatcher.hpp"
#include "../src/net/RateLimiter.hpp"
#include "../src/proto/Ids.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace cppfm;

namespace {
int passCount = 0;
int failCount = 0;

void check(bool condition, const char* name) {
    if (condition) {
        std::printf("PASS %s\n", name);
        ++passCount;
    } else {
        std::printf("FAIL %s\n", name);
        ++failCount;
    }
}

void throws(const char* name, const std::function<void()>& fn) {
    bool raised = false;
    try {
        fn();
    } catch (...) {
        raised = true;
    }
    check(raised, name);
}

void testVarints() {
    const std::vector<std::uint8_t> int32MaxBytes{0xff, 0xff, 0xff, 0xff, 0x07};
    ReadBuffer int32MaxInput(int32MaxBytes);
    check(int32MaxInput.varint() == std::numeric_limits<std::int32_t>::max(),
          "literal VarInt decodes INT32_MAX");
    const std::vector<std::uint8_t> int32MinBytes{0x80, 0x80, 0x80, 0x80, 0x08};
    ReadBuffer int32MinInput(int32MinBytes);
    check(int32MinInput.varint() == std::numeric_limits<std::int32_t>::min(),
          "literal VarInt decodes INT32_MIN");
    WriteBuffer int32MinWriter;
    int32MinWriter.varint(std::numeric_limits<std::int32_t>::min());
    check(int32MinWriter.data == int32MinBytes,
          "VarInt writer emits the independent INT32_MIN vector");

    const std::vector<std::uint8_t> negativeVarintBytes{0xff, 0xff, 0xff, 0xff, 0x0f};
    ReadBuffer negativeVarint(negativeVarintBytes);
    check(negativeVarint.varint() == -1, "valid signed five-byte VarInt remains accepted");
    const std::vector<std::uint8_t> nonMinimalVarint{0x81, 0x00};
    ReadBuffer nonMinimalVarintInput(nonMinimalVarint);
    check(nonMinimalVarintInput.varint() == 1,
          "vanilla accepts non-minimal terminated VarInt");
    const std::vector<std::uint8_t> highVarintPayload{0x80, 0x80, 0x80, 0x80, 0x10};
    ReadBuffer highVarintPayloadInput(highVarintPayload);
    check(highVarintPayloadInput.varint() == 0,
          "vanilla VarInt truncates fifth-byte payload above bit 31");
    const std::vector<std::uint8_t> tooWideVarint{0x80, 0x80, 0x80, 0x80, 0x80, 0x00};
    ReadBuffer tooWideVarintInput(tooWideVarint);
    bool rejectedVarint = false;
    try {
        (void)tooWideVarintInput.varint();
    } catch (const std::runtime_error&) {
        rejectedVarint = true;
    }
    check(rejectedVarint && tooWideVarintInput.off == tooWideVarint.size(),
          "over-width VarInt rejects after consuming the sixth byte");
    const std::vector<std::uint8_t> negativeVarlongBytes{0xff, 0xff, 0xff, 0xff, 0xff,
                                                         0xff, 0xff, 0xff, 0xff, 0x01};
    ReadBuffer negativeVarlong(negativeVarlongBytes);
    check(negativeVarlong.varlong() == -1, "valid signed ten-byte VarLong remains accepted");
    const std::vector<std::uint8_t> int64MaxBytes{
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f};
    ReadBuffer int64MaxInput(int64MaxBytes);
    check(int64MaxInput.varlong() == std::numeric_limits<std::int64_t>::max(),
          "literal VarLong decodes INT64_MAX");
    const std::vector<std::uint8_t> int64MinBytes{
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01};
    ReadBuffer int64MinInput(int64MinBytes);
    check(int64MinInput.varlong() == std::numeric_limits<std::int64_t>::min(),
          "literal VarLong decodes INT64_MIN");
    WriteBuffer int64MinWriter;
    int64MinWriter.varlong(std::numeric_limits<std::int64_t>::min());
    check(int64MinWriter.data == int64MinBytes,
          "VarLong writer emits the independent INT64_MIN vector");
    const std::vector<std::uint8_t> nonMinimalVarlong{0x81, 0x00};
    ReadBuffer nonMinimalVarlongInput(nonMinimalVarlong);
    check(nonMinimalVarlongInput.varlong() == 1,
          "vanilla accepts non-minimal terminated VarLong");
    const std::vector<std::uint8_t> highVarlongPayload{
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x02};
    ReadBuffer highVarlongPayloadInput(highVarlongPayload);
    check(highVarlongPayloadInput.varlong() == 0,
          "vanilla VarLong truncates tenth-byte payload above bit 63");
    const std::vector<std::uint8_t> tooWideVarlong{
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00};
    ReadBuffer tooWideVarlongInput(tooWideVarlong);
    bool rejectedVarlong = false;
    try {
        (void)tooWideVarlongInput.varlong();
    } catch (const std::runtime_error&) {
        rejectedVarlong = true;
    }
    check(rejectedVarlong && tooWideVarlongInput.off == tooWideVarlong.size(),
          "over-width VarLong rejects after consuming the eleventh byte");

    const crypto::Bytes key(16, 0x29);
    crypto::AesCfb8 encoder;
    crypto::AesCfb8 decoder;
    encoder.initEncrypt(key);
    decoder.initDecrypt(key);
    auto encryptedOverWidth = tooWideVarint;
    encoder.crypt(encryptedOverWidth.data(), encryptedOverWidth.size(), encryptedOverWidth.data());
    std::size_t consumed = 0;
    bool rejectedEncrypted = false;
    try {
        (void)PacketDecoder::readVarintEncrypted(encryptedOverWidth.data(), encryptedOverWidth.size(),
                                                 decoder, consumed);
    } catch (const std::runtime_error&) {
        rejectedEncrypted = true;
    }
    check(rejectedEncrypted && consumed == encryptedOverWidth.size(),
          "encrypted over-width VarInt consumes the sixth byte before rejection");

    crypto::AesCfb8 highEncoder;
    crypto::AesCfb8 highDecoder;
    highEncoder.initEncrypt(key);
    highDecoder.initDecrypt(key);
    auto encryptedHighPayload = highVarintPayload;
    highEncoder.crypt(encryptedHighPayload.data(), encryptedHighPayload.size(),
                      encryptedHighPayload.data());
    std::size_t highConsumed = 0;
    const auto highDecoded = PacketDecoder::readVarintEncrypted(
        encryptedHighPayload.data(), encryptedHighPayload.size(), highDecoder, highConsumed);
    check(highDecoded == 0 && highConsumed == encryptedHighPayload.size(),
          "encrypted VarInt accepts vanilla fifth-byte payload truncation");
}

void testFixedWidthAndPositionVectors() {
    WriteBuffer fixed;
    fixed.u16(0x1234);
    fixed.i16(-2);
    fixed.u32(0x89abcdefu);
    fixed.i32(-2);
    fixed.u64(0x0123456789abcdefULL);
    fixed.i64(-2);
    const std::vector<std::uint8_t> fixedBytes{
        0x12, 0x34, 0xff, 0xfe,
        0x89, 0xab, 0xcd, 0xef, 0xff, 0xff, 0xff, 0xfe,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe};
    check(fixed.data == fixedBytes, "fixed-width signed and unsigned integers write big-endian bytes");
    ReadBuffer fixedInput(fixedBytes);
    const bool fixedRead = fixedInput.u16() == 0x1234 && fixedInput.i16() == -2 &&
                           fixedInput.u32() == 0x89abcdefu && fixedInput.i32() == -2 &&
                           fixedInput.u64() == 0x0123456789abcdefULL && fixedInput.i64() == -2 &&
                           fixedInput.remaining() == 0;
    check(fixedRead, "fixed-width signed and unsigned integers read independent big-endian bytes");

    struct PositionVector {
        std::int32_t x, y, z;
        std::vector<std::uint8_t> bytes;
        const char* name;
    };
    const std::vector<PositionVector> positions{
        {1, 0, 0, {0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00}, "Position x axis"},
        {0, 0, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00}, "Position z axis"},
        {0, 1, 0, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, "Position y axis"},
        {-33554432, 0, 0, {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, "Position x minimum"},
        {33554431, 0, 0, {0x7f, 0xff, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00}, "Position x maximum"},
        {0, 0, -33554432, {0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00}, "Position z minimum"},
        {0, 0, 33554431, {0x00, 0x00, 0x00, 0x1f, 0xff, 0xff, 0xf0, 0x00}, "Position z maximum"},
        {0, -2048, 0, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00}, "Position y minimum"},
        {0, 2047, 0, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff}, "Position y maximum"},
        {-12345, -2048, 54321,
         {0xff, 0xf3, 0xf1, 0xc0, 0x0d, 0x43, 0x18, 0x00},
         "Position mixed signed coordinates"},
    };
    for (const auto& vector : positions) {
        WriteBuffer written;
        written.position(vector.x, vector.y, vector.z);
        check(written.data == vector.bytes, vector.name);
        ReadBuffer input(vector.bytes);
        std::int32_t x = 0, y = 0, z = 0;
        input.position(x, y, z);
        check(x == vector.x && y == vector.y && z == vector.z,
              "Position reader decodes independent literal bytes");
    }
}

void testCompressionAndFraming() {
    const std::vector<std::uint8_t> exact{'a', 'b', 'c', 'd', 'e'};
    const auto atThreshold = PacketEncoder::encode(exact, 5);
    ReadBuffer outerAt(atThreshold);
    (void)outerAt.varint();
    check(outerAt.varint() == 5, "compression boundary at threshold carries declared size");
    check(PacketDecoder::decodeOuter(atThreshold, 5) == exact,
          "compressed boundary round-trips through the shipped decoder");

    const std::vector<std::uint8_t> below{'a', 'b', 'c', 'd'};
    const auto belowThreshold = PacketEncoder::encode(below, 5);
    ReadBuffer outerBelow(belowThreshold);
    (void)outerBelow.varint();
    check(outerBelow.varint() == 0, "below-threshold frame uses dataLength zero");
    check(PacketDecoder::decodeOuter(belowThreshold, 5) == below,
          "uncompressed boundary round-trips through the shipped decoder");

    throws("empty compressed packet body is rejected", [] {
        (void)PacketDecoder::decodeFrame(std::vector<std::uint8_t>{0x00}, 256);
    });
    throws("outer length mismatch is rejected before dispatch", [] {
        (void)PacketDecoder::decodeOuter(std::vector<std::uint8_t>{0x02, 0x01}, -1);
    });
    throws("invalid negative compression threshold is rejected", [] {
        (void)PacketEncoder::encode(std::vector<std::uint8_t>{0x01}, -2);
    });
    throws("zlib trailing bytes are rejected", [&] {
        std::vector<std::uint8_t> z;
        compressRaw(exact.data(), exact.size(), z);
        z.push_back(0);
        std::vector<std::uint8_t> out;
        decompressChecked(z.data(), z.size(), exact.size(), out);
    });
}

void testEncryptionAndLifecycle() {
    crypto::Bytes key(16, 0x42);
    crypto::AesCfb8 encoder;
    crypto::AesCfb8 decoder;
    encoder.initEncrypt(key);
    decoder.initDecrypt(key);
    const std::vector<std::uint8_t> one{0x01, 0x11};
    const std::vector<std::uint8_t> two{0x02, 0x22, 0x33};
    const auto first = PacketEncoder::encode(one, -1, &encoder);
    const auto second = PacketEncoder::encode(two, -1, &encoder);
    check(PacketDecoder::decodeOuter(first, -1, &decoder) == one,
          "AES-CFB8 decrypts the first framed packet");
    check(PacketDecoder::decodeOuter(second, -1, &decoder) == two,
          "AES-CFB8 preserves state across adjacent framed packets");
    WriteBuffer varintWire;
    varintWire.varint(300);
    crypto::AesCfb8 varintEncoder;
    crypto::AesCfb8 varintDecoder;
    varintEncoder.initEncrypt(key);
    varintDecoder.initDecrypt(key);
    auto encryptedVarint = varintWire.data;
    varintEncoder.crypt(encryptedVarint.data(), encryptedVarint.size(), encryptedVarint.data());
    std::size_t consumed = 0;
    check(PacketDecoder::readVarintEncrypted(encryptedVarint.data(), encryptedVarint.size(),
                                             varintDecoder, consumed) == 300 && consumed == 2,
          "encrypted VarInt helper decrypts before parsing and reports consumption");
    throws("AES rejects a non-128-bit shared secret", [] {
        crypto::AesCfb8 bad;
        bad.initEncrypt(crypto::Bytes(15, 0));
    });

#ifndef _WIN32
    int sockets[2] = {-1, -1};
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
          "socketpair available for partial-frame lifecycle test");
    if (sockets[0] >= 0) {
        Connection connection(sockets[0]);
        const std::uint8_t length = 0x02;
        (void)::send(sockets[1], &length, 1, MSG_NOSIGNAL);
        bool timedOut = false;
        try {
            (void)connection.readFrameWithTimeout(std::chrono::milliseconds(40));
        } catch (const SocketClosedError& error) {
            timedOut = error.timedOut;
        }
        check(timedOut, "partial frame timeout covers bytes after the length prefix");
        connection.close();
        connection.close();
        ::close(sockets[1]);
    }

    int nonMinimalSockets[2] = {-1, -1};
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, nonMinimalSockets) == 0,
          "socketpair available for non-minimal frame-length compatibility");
    if (nonMinimalSockets[0] >= 0) {
        Connection connection(nonMinimalSockets[0]);
        const std::uint8_t frameBytes[] = {0x81, 0x00, 0x42};
        (void)::send(nonMinimalSockets[1], frameBytes, sizeof(frameBytes), MSG_NOSIGNAL);
        const auto frame = connection.readFrameWithTimeout(std::chrono::milliseconds(100));
        check(frame == std::vector<std::uint8_t>{0x42},
              "stream frame reader accepts vanilla non-minimal VarInt length");
        connection.close();
        ::close(nonMinimalSockets[1]);
    }

    int overWidthSockets[2] = {-1, -1};
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, overWidthSockets) == 0,
          "socketpair available for over-width stream prefix handling");
    if (overWidthSockets[0] >= 0) {
        Connection connection(overWidthSockets[0]);
        const std::uint8_t frameBytes[] = {
            0x80, 0x80, 0x80, 0x80, 0x80, 0x00, // over-width frame length
            0x01, 0x42};                         // next valid one-byte frame
        (void)::send(overWidthSockets[1], frameBytes, sizeof(frameBytes), MSG_NOSIGNAL);
        bool rejected = false;
        try {
            (void)connection.readFrameWithTimeout(std::chrono::milliseconds(100));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        check(rejected, "stream rejects a sixth-byte VarInt continuation");
        const auto frame = connection.readFrameWithTimeout(std::chrono::milliseconds(100));
        check(frame == std::vector<std::uint8_t>{0x42},
              "stream consumes sixth byte before rejecting over-width VarInt");
        connection.close();
        ::close(overWidthSockets[1]);
    }

    int encryptedSockets[2] = {-1, -1};
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, encryptedSockets) == 0,
          "socketpair available for encrypted over-width prefix handling");
    if (encryptedSockets[0] >= 0) {
        const crypto::Bytes secret(16, 0x35);
        Connection connection(encryptedSockets[0]);
        connection.enableEncryption(secret);
        crypto::AesCfb8 peerEncoder;
        peerEncoder.initEncrypt(secret);
        std::vector<std::uint8_t> encryptedFrames{
            0x80, 0x80, 0x80, 0x80, 0x80, 0x00, // over-width frame length
            0x01, 0x42};                         // next valid one-byte frame
        peerEncoder.crypt(encryptedFrames.data(), encryptedFrames.size(), encryptedFrames.data());
        (void)::send(encryptedSockets[1], encryptedFrames.data(), encryptedFrames.size(), MSG_NOSIGNAL);
        bool rejected = false;
        try {
            (void)connection.readFrameWithTimeout(std::chrono::milliseconds(100));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        check(rejected, "encrypted stream rejects a sixth-byte VarInt continuation");
        const auto frame = connection.readFrameWithTimeout(std::chrono::milliseconds(100));
        check(frame == std::vector<std::uint8_t>{0x42},
              "encrypted stream consumes sixth byte and preserves CFB8 alignment");
        connection.close();
        ::close(encryptedSockets[1]);
    }
#endif
}

void testLimiterAndAuthEdges() {
    RateLimiter limiter(10.0, 1.0, 1000);
    check(!limiter.consume(std::numeric_limits<double>::quiet_NaN(), 1000),
          "NaN bandwidth input fails closed");
    check(!limiter.consume(-1.0, 1000), "negative bandwidth input fails closed");
    check(limiter.consume(10.0, 1000), "valid token-bucket boundary remains accepted");

    AcceptGate gate(1);
    check(gate.allow(1000), "accept gate allows first connection");
    check(!gate.allow(1001), "accept gate enforces per-window cap");
    check(gate.allow(0), "accept gate recovers after wall-clock rollback");

    check(cppfm::mojang_detail::base64Decode("AQID") ==
              std::vector<std::uint8_t>({1, 2, 3}),
          "strict profile-key Base64 decoder accepts canonical input");
    check(cppfm::mojang_detail::base64Decode("AQ=garbage").empty(),
          "profile-key Base64 decoder rejects malformed padding");
    check(cppfm::mojang_detail::base64Decode("AB==").empty(),
          "profile-key Base64 decoder rejects non-zero unused bits");
}

void testProtocolBoundariesAndLimitations() {
    check(proto::pl::sc::BundleDelimiter == 0x00,
          "bundle delimiter uses the protocol 769 id");
    check(proto::pl::sc::MultiBlockChange == 0x4E,
          "multi-block change uses the protocol 769 id");
    WriteBuffer record;
    record.varint((7 << 12) | (3 << 8) | (5 << 4) | 2);
    ReadBuffer recordIn(record.data);
    check(recordIn.varint() == 0x7352,
          "multi-block record preserves x/z/y axis ordering");
    check(proto::pl::sc::KeepAlive == 0x27 && proto::pl::cs::KeepAlive == 0x1A,
          "play keepalive directions remain distinct");
    check(proto::pl::sc::CookieRequest == 0x16 && proto::pl::cs::CookieResponse == 0x13,
          "play cookie request/response ids remain distinct");
    check(proto::cf::sc::AddResourcePack == 0x09 &&
              proto::pl::cs::ResourcePackReceive == 0x2F,
          "configuration resource-pack offer and play acknowledgement ids are distinct");
    const packet_batch_detail::PositionKey collisionA{0, 1, 0};
    const packet_batch_detail::PositionKey collisionB{0, 0, 1 << 21};
    check(collisionA < collisionB || collisionB < collisionA,
          "batch dedup keys distinguish coordinates that collided under XOR packing");
    const packet_batch_detail::SectionKey sectionA{2, 0, 0};
    const packet_batch_detail::SectionKey sectionB{1, 9, 0};
    check(sectionB < sectionA,
          "batch section keys provide deterministic ordering instead of hash iteration");
}
}

int main() {
    testVarints();
    testCompressionAndFraming();
    testEncryptionAndLifecycle();
    testLimiterAndAuthEdges();
    testFixedWidthAndPositionVectors();
    testProtocolBoundariesAndLimitations();
    std::printf("network goal bugs: %d passed, %d failed\n", passCount, failCount);
    return failCount == 0 ? 0 : 1;
}
