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
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
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

    const std::vector<std::uint8_t> maxFrameBody(PacketEncoder::kMaxFrame, 0x5A);
    const auto maxFrame = PacketEncoder::encode(maxFrameBody, -1);
    check(PacketDecoder::decodeOuter(maxFrame, -1) == maxFrameBody,
          "encoder and decoder round-trip the largest VarInt21 frame");
    throws("encoder rejects an uncompressed frame outside VarInt21", [] {
        std::vector<std::uint8_t> tooLarge(PacketEncoder::kMaxFrame + 1, 0x5A);
        (void)PacketEncoder::encode(tooLarge, -1);
    });
    const std::vector<std::uint8_t> maxDeclaredBody(PacketEncoder::kMaxDeclared, 0);
    const auto compressedMaxDeclared = PacketEncoder::encode(maxDeclaredBody, 1);
    check(PacketDecoder::decodeOuter(compressedMaxDeclared, 1) == maxDeclaredBody,
          "compressed declared-size limit remains inclusive at 2 MiB");

    const std::vector<std::uint8_t> continuedFramePrefix{0x80, 0x80, 0x80, 0x01};
    ReadBuffer framePrefix(continuedFramePrefix);
    bool framePrefixRejected = false;
    try {
        (void)PacketDecoder::readVarint21([&framePrefix] { return framePrefix.u8(); });
    } catch (const PacketDecoder::OversizeError&) {
        framePrefixRejected = true;
    }
    check(framePrefixRejected && framePrefix.off == 3 && framePrefix.u8() == 0x01,
          "Varint21 rejects after exactly three continued prefix bytes");
    bool outerPrefixRejected = false;
    try {
        (void)PacketDecoder::decodeOuter(continuedFramePrefix, -1);
    } catch (const PacketDecoder::OversizeError&) {
        outerPrefixRejected = true;
    }
    check(outerPrefixRejected, "complete outer decoder uses the strict Varint21 prefix contract");

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

    int idleSockets[2] = {-1, -1};
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, idleSockets) == 0,
          "socketpair available for first-byte frame deadline test");
    if (idleSockets[0] >= 0) {
        Connection connection(idleSockets[0]);
        std::thread delayedFrame([fd = idleSockets[1]] {
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            const std::uint8_t frame[] = {0x02, 0x01, 0x02};
            (void)::send(fd, frame, sizeof(frame), MSG_NOSIGNAL);
        });
        bool readSucceeded = false;
        try {
            readSucceeded = connection.readFrameWithFirstByteTimeout(
                                std::chrono::milliseconds(100)) ==
                            std::vector<std::uint8_t>({0x01, 0x02});
        } catch (...) {}
        delayedFrame.join();
        check(readSucceeded,
              "frame deadline starts at its first byte, not while the connection is idle");
        connection.close();
        ::close(idleSockets[1]);
    }

    int dripSockets[2] = {-1, -1};
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, dripSockets) == 0,
          "socketpair available for slow-drip frame deadline test");
    if (dripSockets[0] >= 0) {
        Connection connection(dripSockets[0]);
        std::thread slowPeer([fd = dripSockets[1]] {
            const std::uint8_t length = 0x05;
            (void)::send(fd, &length, 1, MSG_NOSIGNAL);
            const std::uint8_t byte = 0x41;
            for (int i = 0; i < 4; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(70));
                if (::send(fd, &byte, 1, MSG_NOSIGNAL) != 1) break;
            }
        });
        const auto readStart = std::chrono::steady_clock::now();
        bool timedOut = false;
        try {
            (void)connection.readFrameWithFirstByteTimeout(
                std::chrono::milliseconds(100));
        } catch (const SocketClosedError& error) {
            timedOut = error.timedOut;
        }
        const auto elapsed = std::chrono::steady_clock::now() - readStart;
        slowPeer.join();
        check(timedOut && elapsed < std::chrono::milliseconds(500),
              "slow-dripped frame is bounded by one absolute frame deadline");
        connection.close();
        ::close(dripSockets[1]);
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
              "stream frame reader accepts a non-minimal terminated Varint21 length");
        connection.close();
        ::close(nonMinimalSockets[1]);
    }

    int overWidthSockets[2] = {-1, -1};
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, overWidthSockets) == 0,
          "socketpair available for over-width stream prefix handling");
    if (overWidthSockets[0] >= 0) {
        Connection connection(overWidthSockets[0]);
        const std::uint8_t frameBytes[] = {
            0x80, 0x80, 0x80,                    // invalid Varint21 frame prefix
            0x01, 0x42};                         // next valid one-byte frame
        (void)::send(overWidthSockets[1], frameBytes, sizeof(frameBytes), MSG_NOSIGNAL);
        bool rejected = false;
        try {
            (void)connection.readFrameWithTimeout(std::chrono::milliseconds(100));
        } catch (const PacketDecoder::OversizeError&) {
            rejected = true;
        }
        check(rejected, "stream rejects a third continuation in the Varint21 frame prefix");
        const auto frame = connection.readFrameWithTimeout(std::chrono::milliseconds(100));
        check(frame == std::vector<std::uint8_t>{0x42},
              "stream consumes exactly the invalid three-byte prefix before session-level disconnect");
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
            0x80, 0x80, 0x80,                    // invalid Varint21 frame prefix
            0x01, 0x42};                         // next valid one-byte frame
        peerEncoder.crypt(encryptedFrames.data(), encryptedFrames.size(), encryptedFrames.data());
        (void)::send(encryptedSockets[1], encryptedFrames.data(), encryptedFrames.size(), MSG_NOSIGNAL);
        bool rejected = false;
        try {
            (void)connection.readFrameWithTimeout(std::chrono::milliseconds(100));
        } catch (const PacketDecoder::OversizeError&) {
            rejected = true;
        }
        check(rejected, "encrypted stream rejects a third continuation in the Varint21 frame prefix");
        const auto frame = connection.readFrameWithTimeout(std::chrono::milliseconds(100));
        check(frame == std::vector<std::uint8_t>{0x42},
              "encrypted stream consumes exactly three prefix bytes and preserves CFB8 alignment");
        connection.close();
        ::close(encryptedSockets[1]);
    }

    int prioritySockets[2] = {-1, -1};
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, prioritySockets) == 0,
          "socketpair available for output priority barrier test");
    if (prioritySockets[0] >= 0) {
        int sendBufferSize = 1024;
        (void)::setsockopt(prioritySockets[0], SOL_SOCKET, SO_SNDBUF,
                           &sendBufferSize, sizeof(sendBufferSize));
        Connection sender(prioritySockets[0]);
        Connection receiver(prioritySockets[1]);
        WriteBuffer bulkPayload;
        bulkPayload.data.assign(64u * 1024u, 0x5A);
        ++activeSimulationDispatchDepth;
        try {
            sender.sendPacketLowPriority(0x41, bulkPayload);
            sender.sendPacketLowPriority(0x42, bulkPayload);
            sender.sendPacketBarrier(0x43, WriteBuffer{});
            sender.sendPacketLowPriority(0x44, WriteBuffer{});
        } catch (...) {
            --activeSimulationDispatchDepth;
            sender.abort();
            receiver.abort();
            check(false, "output barrier test queues all frames");
            return;
        }
        --activeSimulationDispatchDepth;

        std::vector<std::uint8_t> ids;
        bool readAll = true;
        for (int i = 0; i < 4; ++i) {
            try {
                const auto body = receiver.readFrameWithTimeout(
                    std::chrono::seconds(3));
                if (body.empty()) readAll = false;
                else ids.push_back(body.front());
            } catch (...) {
                readAll = false;
                break;
            }
        }
        check(readAll && ids == std::vector<std::uint8_t>{0x41, 0x42, 0x43, 0x44},
              "state-transition barrier keeps older low-priority chunks ahead of Respawn");
        sender.close();
        receiver.close();
    }

    int abortSockets[2] = {-1, -1};
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, abortSockets) == 0,
          "socketpair available for aborting a blocked writer");
    if (abortSockets[0] >= 0) {
        int sendBufferSize = 1024;
        (void)::setsockopt(abortSockets[0], SOL_SOCKET, SO_SNDBUF,
                           &sendBufferSize, sizeof(sendBufferSize));
        Connection blockedSender(abortSockets[0]);
        WriteBuffer blockedPayload;
        blockedPayload.data.assign(1024u * 1024u, 0xA5);
        ++activeSimulationDispatchDepth;
        blockedSender.sendPacketLowPriority(0x45, blockedPayload);
        --activeSimulationDispatchDepth;
        const auto abortStart = std::chrono::steady_clock::now();
        blockedSender.abort();
        const auto abortElapsed = std::chrono::steady_clock::now() - abortStart;
        check(!blockedSender.isOpen() && abortElapsed < std::chrono::seconds(2),
              "abort wakes and joins a blocked writer before releasing its descriptor");
        ::close(abortSockets[1]);
    }

    int readSockets[2] = {-1, -1};
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, readSockets) == 0,
          "socketpair available for aborting a blocked reader");
    if (readSockets[0] >= 0) {
        Connection blockedReader(readSockets[0]);
        std::atomic<bool> readStarted{false};
        std::atomic<bool> readFailed{false};
        std::thread reader([&] {
            readStarted.store(true, std::memory_order_release);
            try {
                (void)blockedReader.readFrameWithTimeout(std::chrono::seconds(5));
            } catch (...) {
                readFailed.store(true, std::memory_order_release);
            }
        });
        while (!readStarted.load(std::memory_order_acquire))
            std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const auto readAbortStart = std::chrono::steady_clock::now();
        blockedReader.abort();
        const auto readAbortElapsed = std::chrono::steady_clock::now() - readAbortStart;
        reader.join();
        check(!blockedReader.isOpen() && readFailed.load(std::memory_order_acquire) &&
                  readAbortElapsed < std::chrono::seconds(2),
              "abort wakes and drains active readers before releasing its descriptor");
        ::close(readSockets[1]);
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

    SessionAdmissionGate admission(2);
    auto slot1 = admission.tryAcquire();
    auto slot2 = admission.tryAcquire();
    check(slot1.has_value() && slot2.has_value() && admission.active() == 2 &&
              !admission.tryAcquire(),
          "session admission enforces its concurrent-worker ceiling");
    slot1.reset();
    check(admission.active() == 1 && admission.tryAcquire().has_value(),
          "session admission slot release permits the next worker");
    slot2.reset();

    SessionAdmissionGate concurrentAdmission(8);
    std::atomic<int> attempted{0};
    std::atomic<int> admitted{0};
    std::atomic<bool> startAdmission{false};
    std::atomic<bool> releaseAdmission{false};
    std::vector<std::thread> contenders;
    contenders.reserve(32);
    for (int i = 0; i < 32; ++i) {
        contenders.emplace_back([&] {
            while (!startAdmission.load(std::memory_order_acquire))
                std::this_thread::yield();
            auto slot = concurrentAdmission.tryAcquire();
            if (slot) admitted.fetch_add(1, std::memory_order_relaxed);
            attempted.fetch_add(1, std::memory_order_release);
            if (slot)
                while (!releaseAdmission.load(std::memory_order_acquire))
                    std::this_thread::yield();
        });
    }
    startAdmission.store(true, std::memory_order_release);
    while (attempted.load(std::memory_order_acquire) != 32)
        std::this_thread::yield();
    const bool boundedAdmission = admitted.load(std::memory_order_relaxed) == 8 &&
                                  concurrentAdmission.active() == 8;
    releaseAdmission.store(true, std::memory_order_release);
    for (auto& contender : contenders) contender.join();
    check(boundedAdmission && concurrentAdmission.active() == 0,
          "concurrent session admissions never exceed the configured ceiling");

    check(cppfm::mojang_detail::base64Decode("AQID") ==
              std::vector<std::uint8_t>({1, 2, 3}),
          "strict profile-key Base64 decoder accepts canonical input");
    check(cppfm::mojang_detail::base64Decode("AQ=garbage").empty(),
          "profile-key Base64 decoder rejects malformed padding");
    check(cppfm::mojang_detail::base64Decode("AB==").empty(),
          "profile-key Base64 decoder rejects non-zero unused bits");
    const auto longKey = cppfm::mojang_detail::base64Decode(std::string(2048, 'z'));
    check(longKey.size() == 1536 && longKey[0] == 0xCF &&
              longKey[1] == 0x3C && longKey[2] == 0xF3 &&
              longKey[1535] == 0xF3,
          "profile-key Base64 decoder handles long key material without signed overflow");

    cppfm::mojang_detail::ProfilePublicKeyCache cache;
    std::atomic<int> fetchCount{0};
    const cppfm::mojang_detail::ProfilePublicKeys expected{{1, 2, 3, 4}};
    std::vector<cppfm::mojang_detail::ProfilePublicKeys> results(8);
    std::vector<std::thread> callers;
    for (std::size_t i = 0; i < results.size(); ++i) {
        callers.emplace_back([&, i] {
            results[i] = cache.get([&] {
                fetchCount.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                return expected;
            });
        });
    }
    for (auto& caller : callers) caller.join();
    check(fetchCount.load(std::memory_order_relaxed) == 1 &&
              std::all_of(results.begin(), results.end(),
                          [&](const auto& result) { return result == expected; }),
          "Mojang profile keys are fetched once for concurrent logins");

    cppfm::mojang_detail::ProfilePublicKeyCache staleCache(
        std::chrono::minutes(0), std::chrono::minutes(30), std::chrono::seconds(10));
    const auto firstFetch = staleCache.get([&] { return expected; });
    bool staleReturned = false;
    try {
        staleReturned = staleCache.get([]() -> cppfm::mojang_detail::ProfilePublicKeys {
            throw std::runtime_error("temporary key endpoint failure");
        }) == expected;
    } catch (...) {}
    check(firstFetch == expected && staleReturned,
          "Mojang profile-key cache uses recent trusted keys during endpoint failure");

    using Cache = cppfm::mojang_detail::ProfilePublicKeyCache;
    auto fakeNow = Cache::Clock::time_point{};
    Cache expiryCache(std::chrono::seconds(10), std::chrono::seconds(20),
                      std::chrono::seconds(100), [&] { return fakeNow; });
    int expiryFetches = 0;
    const auto initiallyTrusted = expiryCache.get([&] {
        ++expiryFetches;
        return expected;
    });
    fakeNow += std::chrono::seconds(11);
    const auto trustedWhileStale = expiryCache.get([&]() ->
        cppfm::mojang_detail::ProfilePublicKeys {
        ++expiryFetches;
        throw std::runtime_error("temporary key endpoint failure");
    });
    fakeNow += std::chrono::seconds(10);
    const auto afterStaleExpiry = expiryCache.get([&] {
        ++expiryFetches;
        return expected;
    });
    check(initiallyTrusted == expected && trustedWhileStale == expected &&
              afterStaleExpiry.empty() && expiryFetches == 2,
          "Mojang key cache stops trusting keys at the stale deadline");
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
