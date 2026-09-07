// Core safety regression tests: malformed input must be rejected at the
// boundary and valid values must retain their wire representation.

#include "../src/core/ByteBuffer.hpp"
#include "../src/core/Json.hpp"
#include "../src/core/NBT.hpp"
#include "../src/core/NBTValue.hpp"
#include "../src/core/Zlib.hpp"
#include "../src/game/RegionFile.hpp"
#include "../src/game/ServerProperties.hpp"
#include "../src/net/PacketDecoder.hpp"
#include "../src/net/PacketEncoder.hpp"
#include "../src/net/Rcon.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

using namespace cppfm;

namespace {

int passed = 0;
int failed = 0;

void check(bool condition, const char* description) {
    if (condition) {
        ++passed;
        std::printf("  PASS %s\n", description);
    } else {
        ++failed;
        std::printf("  FAIL %s\n", description);
    }
}

void expectThrow(const char* description, const std::function<void()>& action) {
    try {
        action();
        check(false, description);
    } catch (const std::exception&) {
        ++passed;
        std::printf("  PASS %s\n", description);
    } catch (...) {
        ++passed;
        std::printf("  PASS %s\n", description);
    }
}

std::vector<std::uint8_t> nestedNetworkNbt(std::size_t depth) {
    WriteBuffer out;
    out.u8(nbt::Compound);
    for (std::size_t i = 0; i < depth; ++i) {
        out.u8(nbt::Compound);
        out.u16(1);
        out.u8('x');
    }
    for (std::size_t i = 0; i <= depth; ++i) out.u8(nbt::End);
    return out.data;
}

void testByteBuffer() {
    std::printf("\n[ByteBuffer boundary checks]\n");
    WriteBuffer out;
    out.varint(-1);
    out.varlong(-1);
    ReadBuffer in(out.data);
    check(in.varint() == -1, "signed VarInt -1 roundtrip");
    check(in.varlong() == -1, "signed VarLong -1 roundtrip");

    expectThrow("VarInt rejects payload bits above bit 31", [] {
        const std::vector<std::uint8_t> bytes{0xff, 0xff, 0xff, 0xff, 0x10};
        ReadBuffer input(bytes);
        (void)input.varint();
    });
    expectThrow("VarLong rejects payload bits above bit 63", [] {
        const std::vector<std::uint8_t> bytes{
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x02};
        ReadBuffer input(bytes);
        (void)input.varlong();
    });

    WriteBuffer position;
    position.position(-12345, -2048, 54321);
    ReadBuffer positionIn(position.data);
    std::int32_t x = 0, y = 0, z = 0;
    positionIn.position(x, y, z);
    check(x == -12345 && y == -2048 && z == 54321, "signed packed position roundtrip");
}

void testJson() {
    std::printf("\n[JSON boundary checks]\n");
    const auto value = json::Value::parse(R"({"n":-12.5e2,"s":"ok","a":[true,null]})");
    check(value.isObj() && value.find("n") && value.find("n")->number == -1250.0,
          "valid JSON number and object parse");
    check(json::Value::parse(value.dump()).dump() == value.dump(),
          "JSON dump is parseable and stable");
    expectThrow("JSON rejects leading zero", [] { (void)json::Value::parse("01"); });
    expectThrow("JSON rejects incomplete exponent", [] { (void)json::Value::parse("1e"); });
    expectThrow("JSON rejects unescaped control character", [] {
        (void)json::Value::parse(std::string("\"bad\ntext\""));
    });
    expectThrow("JSON rejects an unpaired high surrogate", [] {
        (void)json::Value::parse(R"("\uD800")");
    });
    expectThrow("JSON rejects an unpaired low surrogate", [] {
        (void)json::Value::parse(R"("\uDC00")");
    });
    expectThrow("JSON rejects non-finite numeric input", [] {
        (void)json::Value::parse("1e309");
    });
    expectThrow("JSON rejects duplicate object keys", [] {
        (void)json::Value::parse(R"({"key":1,"key":2})");
    });
    expectThrow("JSON rejects excessive nesting", [] {
        std::string nested;
        nested.reserve(json::Value::kMaxDepth + 2);
        nested.append(json::Value::kMaxDepth + 1, '[');
        nested.append(json::Value::kMaxDepth + 1, ']');
        (void)json::Value::parse(nested);
    });
}

void testConfigurationAndEncoding() {
    std::printf("\n[configuration and encoder boundary checks]\n");
    ServerProperties properties;
    properties.props["View-Distance"] = "12";
    properties.props["bad-int"] = "12trailing";
    properties.props["bad-float"] = "nan";
    properties.props["bad-range"] = "1e309";
    check(properties.get<int>("view-distance", 6) == 12,
          "server properties lookup is ASCII case-insensitive");
    check(properties.get<int>("bad-int", 7) == 7,
          "server properties reject trailing integer data");
    check(properties.get<float>("bad-float", 3.5f) == 3.5f,
          "server properties reject non-finite floats");
    check(properties.get<double>("bad-range", 4.5) == 4.5,
          "server properties reject floating-point range errors");

    const std::vector<std::uint8_t> small{'p', 'a', 'y', 'l', 'o', 'a', 'd'};
    const auto encoded = PacketEncoder::encode(small, -1);
    check(PacketDecoder::decodeOuter(encoded, -1) == small,
          "packet encoder and decoder roundtrip an uncompressed body");
    expectThrow("packet encoder rejects compressed bodies above decoder budget", [] {
        const std::vector<std::uint8_t> large(PacketEncoder::kMaxDeclared + 1, 0x41);
        (void)PacketEncoder::encode(large, 0);
    });
}

void testNbt() {
    std::printf("\n[NBT boundary checks]\n");
    nbt::Value root = nbt::Value::makeCompound();
    auto list = nbt::Value::makeList(nbt::Int);
    list.list.push_back(nbt::Value::makeInt(7));
    list.list.push_back(nbt::Value::makeInt(9));
    root.set("values", list);
    WriteBuffer encoded;
    nbt::writeFileRoot(encoded, root, "root");
    ReadBuffer input(encoded.data);
    std::string rootName;
    nbt::Parser parser(input);
    const auto decoded = parser.readFileRoot(&rootName);
    const auto* values = decoded.get("values");
    check(rootName == "root" && values && values->tag == nbt::List &&
              values->elemType() == nbt::Int && values->list.size() == 2 &&
              values->list[1].i == 9,
          "named NBT list preserves element type and values");

    expectThrow("NBT rejects a negative list length", [] {
        WriteBuffer malformed;
        malformed.u8(nbt::List);
        malformed.u8(nbt::Int);
        malformed.i32(-1);
        ReadBuffer input(malformed.data);
        nbt::Reader reader(input);
        reader.skipRoot();
    });
    expectThrow("NBT rejects excessive nesting", [] {
        auto bytes = nestedNetworkNbt(nbt::kMaxNbtDepth + 1);
        ReadBuffer input(bytes);
        nbt::Reader reader(input);
        reader.skipRoot();
    });
    expectThrow("NBT rejects a negative file byte-array length", [] {
        WriteBuffer malformed;
        malformed.u8(nbt::Compound);
        malformed.u16(0);
        malformed.u8(nbt::ByteArray);
        malformed.u16(1);
        malformed.u8('x');
        malformed.i32(-1);
        malformed.u8(nbt::End);
        ReadBuffer input(malformed.data);
        nbt::Parser parser(input);
        (void)parser.readFileRoot();
    });
    expectThrow("NBT rejects End as a list element type", [] {
        (void)nbt::Value::makeList(nbt::End);
    });
}

void testCompressionAndPackets() {
    std::printf("\n[compression and packet boundary checks]\n");
    const std::vector<std::uint8_t> plain{'h', 'e', 'l', 'l', 'o'};
    std::vector<std::uint8_t> compressed;
    compressRaw(plain.data(), plain.size(), compressed);
    std::vector<std::uint8_t> decoded;
    decompressChecked(compressed.data(), compressed.size(), plain.size(), decoded);
    check(decoded == plain, "zlib exact-size roundtrip");

    auto trailing = compressed;
    trailing.push_back(0);
    expectThrow("zlib rejects trailing bytes", [&] {
        std::vector<std::uint8_t> ignored;
        decompressChecked(trailing.data(), trailing.size(), plain.size(), ignored);
    });
    expectThrow("zlib enforces the decompression budget", [&] {
        std::vector<std::uint8_t> ignored;
        decompressUnknown(compressed.data(), compressed.size(), ignored, 4);
    });

    WriteBuffer frame;
    frame.varint(0);
    frame.u8(0x2a);
    frame.u8(0x01);
    check(PacketDecoder::decodeFrame(frame.data, 256) ==
              std::vector<std::uint8_t>({0x2a, 0x01}),
          "uncompressed packet frame roundtrip");
    expectThrow("packet decoder rejects an outer length mismatch", [] {
        const std::vector<std::uint8_t> malformed{0x03, 0x2a};
        (void)PacketDecoder::decodeOuter(malformed, -1);
    });
    expectThrow("packet decoder rejects an uncompressed frame at threshold", [] {
        WriteBuffer malformed;
        malformed.varint(0);
        malformed.raw("1234", 4);
        (void)PacketDecoder::decodeFrame(malformed.data, 4);
    });
}

void testRegionFile() {
    std::printf("\n[Anvil region boundary checks]\n");
    const auto dir = std::filesystem::temp_directory_path() /
                     ("cppfm-core-safety-" + std::to_string(static_cast<long long>(getpid())));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    const auto path = dir / "region" / "r.0.0.mca";
    try {
        const std::vector<std::uint8_t> bytes{10, 0, 0, 3, 0, 0, 0};
        RegionFile region(path.string());
        region.store(0, 0, bytes);
        check(region.load(0, 0) == bytes, "Anvil region store/load roundtrip");
        expectThrow("Anvil rejects negative local coordinates", [&] {
            (void)region.load(-1, 0);
        });
        expectThrow("Anvil rejects local coordinates outside a region", [&] {
            region.store(32, 0, bytes);
        });
    } catch (const std::exception& e) {
        check(false, e.what());
    }
    std::filesystem::remove_all(dir, ec);
}

void testWhitelist() {
    std::printf("\n[whitelist persistence checks]\n");
    const auto dir = std::filesystem::temp_directory_path() /
                     ("cppfm-whitelist-safety-" +
                      std::to_string(static_cast<long long>(getpid())));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const auto path = dir / "whitelist.json";
    {
        std::ofstream file(path);
        file << R"json([{"name":"Alice\"Admin"},{"name":"Bob","uuid":"ignored"}])json";
    }
    Whitelist whitelist;
    whitelist.load(path.string());
    check(whitelist.enabled() && whitelist.contains("Alice\"Admin") &&
              whitelist.contains("Bob") && !whitelist.contains("uuid"),
          "whitelist parses names without treating metadata as users");
    whitelist.insert("Carol");
    check(whitelist.save(path.string()), "whitelist save reports success");
    Whitelist reloaded;
    reloaded.load(path.string());
    check(reloaded.contains("Alice\"Admin") && reloaded.contains("Carol"),
          "whitelist save/load preserves quoted names");
    {
        std::ofstream file(path, std::ios::trunc);
        file << "[malformed";
    }
    reloaded.load(path.string());
    check(reloaded.size() == 0 && !reloaded.contains("Carol"),
          "malformed whitelist is ignored instead of admitting stale users");
    std::filesystem::remove_all(dir, ec);
}

} // namespace

int main() {
    testByteBuffer();
    testJson();
    testConfigurationAndEncoding();
    testNbt();
    testCompressionAndPackets();
    testRegionFile();
    testWhitelist();
    std::printf("\n=== core_safety: %d PASS %d FAIL ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
