// Golden tests: our serializers must reproduce captured reference-server bytes
// exactly for chunk section data & heightmaps, plus protocol primitive vectors.
#include <cassert>
#include <cstdio>
#include <fstream>
#include "../src/core/ByteBuffer.hpp"
#include "../src/core/NBT.hpp"
#include "../src/game/World.hpp"
#include "../src/game/ChunkCodec.hpp"

using namespace cppfm;

static std::vector<std::uint8_t> readFile(const char* p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) { std::fprintf(stderr, "missing %s\n", p); std::exit(2); }
    return {std::istreambuf_iterator<char>(f), {}};
}

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { std::printf("  ok  %s\n", msg); } \
    else { std::printf("  FAIL %s\n", msg); ++g_fail; } } while (0)

static void testVarint() {
    std::printf("varint golden vectors\n");
    struct { std::int32_t v; const char* hex; } cases[] = {
        {0, "00"}, {1, "01"}, {127, "7f"}, {128, "80 01"}, {255, "ff 01"},
        {2147483647, "ff ff ff ff 07"}, {-1, "ff ff ff ff 0f"},
    };
    WriteBuffer wb;
    for (auto& c : cases) {
        wb.data.clear();
        wb.varint(c.v);
        CHECK([&]{ std::string s; for (auto b : wb.data) { char t[4]; snprintf(t,4,"%02x ",b); s+=t; } return s == std::string(c.hex) + " "; }(),
              "varint encode matches");
        ReadBuffer rb(wb.data);
        CHECK(rb.varint() == c.v, "varint roundtrip");
    }
}

static void testPosition() {
    std::printf("position packing vs captured spawn point\n");
    WriteBuffer b;
    b.position(0, -60, 0);
    // captured SetDefaultSpawn long: 00 00 00 00 00 00 0f c4
    const std::vector<std::uint8_t> expect{0,0,0,0,0,0,0x0f,0xc4};
    CHECK(b.data == expect, "spawn position long matches reference bytes");
}

static void testChunkGolden(const char* capturePath, std::int32_t cx, std::int32_t cz,
                            std::uint32_t biomeIdx) {
    std::printf("chunk golden: %s (%d,%d)\n", capturePath, cx, cz);
    auto cap = readFile(capturePath);
    ReadBuffer in(cap);
    const std::int32_t rx = in.i32();
    const std::int32_t rz = in.i32();
    CHECK(rx == cx && rz == cz, "captured coords match expectation");

    // reference heightmaps NBT
    const std::size_t hmStart = in.off;
    { nbt::Reader r(in); r.skipRoot(); }
    const std::size_t hmEnd = in.off;
    const std::size_t refHmLen = hmEnd - hmStart;

    const std::int32_t size = in.varint();
    std::size_t blobOff = in.off;

    World world("minecraft:plains", LevelType::Flat, 12345);
    world.generateChunkIfMissing(cx, cz);
    const Chunk* ch = world.tryGet(cx, cz);

    WriteBuffer myBlob;
    serializeSectionData(myBlob, ch, biomeIdx);
    const bool sizeOk = myBlob.data.size() == static_cast<std::size_t>(size);
    CHECK(sizeOk, ("section blob size equal (ours=" + std::to_string(myBlob.data.size()) +
           " vanilla=" + std::to_string(size) + ")").c_str());
    bool eq = myBlob.data.size() == static_cast<std::size_t>(size) &&
              std::equal(myBlob.data.begin(), myBlob.data.end(), cap.begin() + blobOff);
    if (!eq) {
        for (std::size_t i = 0; i < std::min(myBlob.data.size(), static_cast<std::size_t>(size)); ++i)
            if (myBlob.data[i] != cap[blobOff + i]) {
                std::printf("    first diff at +%zu: ours=%02x vanilla=%02x\n",
                            i, myBlob.data[i], cap[blobOff + i]);
                break;
            }
    }
    CHECK(eq, "section data BYTES identical to vanilla server output");

    // heightmaps byte equality
    WriteBuffer myHm;
    packHeightmapsNbt(myHm, ch);
    bool hmeq = myHm.data.size() == refHmLen &&
                std::equal(myHm.data.begin(), myHm.data.end(), cap.begin() + hmStart);
    if (!hmeq && myHm.data.size() == refHmLen) {
        for (std::size_t i = 0; i < refHmLen; ++i)
            if (myHm.data[i] != cap[hmStart + i]) {
                std::printf("    first hm diff at +%zu: ours=%02x vanilla=%02x\n",
                            i, myHm.data[i], cap[hmStart + i]);
                break;
            }
    }
    CHECK(hmeq, "heightmaps NBT BYTES identical to vanilla server output");
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "/tmp/opencode/captures";
    std::printf("=== cppfm golden tests ===\n");
    testVarint();
    testPosition();
    // plains chunk (0,-1): biome plains = index 40 in the synced registry order
    testChunkGolden((std::string(dir) + "/play_chunk_0.bin").c_str(), 0, -1, 40);
    // desert chunk from the second reference instance: desert=14, sand surface
    // (our flat world differs there, so only run structural size check via plains)
    std::printf("\n%s (%d failures)\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
