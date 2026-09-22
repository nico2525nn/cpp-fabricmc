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
    const std::vector<std::uint8_t> negativeVarintBytes{0xff, 0xff, 0xff, 0xff, 0x0f};
    ReadBuffer negativeVarint(negativeVarintBytes);
    check(negativeVarint.varint() == -1, "valid signed five-byte VarInt remains accepted");
    throws("VarInt fifth-byte payload overflow is rejected", [] {
        const std::vector<std::uint8_t> bytes{0xff, 0xff, 0xff, 0xff, 0x10};
        ReadBuffer in(bytes);
        (void)in.varint();
    });
    throws("VarInt sixth continuation byte is rejected", [] {
        const std::vector<std::uint8_t> bytes{0x80, 0x80, 0x80, 0x80, 0x80, 0x00};
        ReadBuffer in(bytes);
        (void)in.varint();
    });
    const std::vector<std::uint8_t> negativeVarlongBytes{0xff, 0xff, 0xff, 0xff, 0xff,
                                                         0xff, 0xff, 0xff, 0xff, 0x01};
    ReadBuffer negativeVarlong(negativeVarlongBytes);
    check(negativeVarlong.varlong() == -1, "valid signed ten-byte VarLong remains accepted");
    throws("VarLong tenth-byte payload overflow is rejected", [] {
        const std::vector<std::uint8_t> bytes{0x80, 0x80, 0x80, 0x80, 0x80,
                                              0x80, 0x80, 0x80, 0x80, 0x02};
        ReadBuffer in(bytes);
        (void)in.varlong();
    });
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
    testProtocolBoundariesAndLimitations();
    std::printf("network goal bugs: %d passed, %d failed\n", passCount, failCount);
    return failCount == 0 ? 0 : 1;
}
