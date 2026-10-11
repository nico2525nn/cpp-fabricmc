// test_plan43.cpp — plan43 B1+B2 live-server tests (docs/VERIFICATION.md, W-01..W-07, W-12).
// Each case is spec-driven (Prismarine protocol.json 1.21.4 hand-built fixtures,
// NOT copied from server output — G-01 tautology guard).
// Stage-merge design: cases FAIL on pre-fix server (disconnect/misread), PASS post-fix.
// Run: ./build/test_plan43 ./build/cppfm
#include "TestClient.hpp"
#include "ServerProcess.hpp"
#include "Harness.hpp"
#include "../src/core/NBT.hpp"
#include "../src/game/Items.hpp"
#include "../src/generated/ItemIds.hpp"
#include "../src/proto/Ids.hpp"
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <thread>
#include <cmath>
#include <string_view>

using namespace cppfm;
using namespace cpptest;

static bool waitInitialChunks(TestClient& c, int ms=10000) {
    return c.waitFor([](const Packet& q) {
        return q.id == proto::pl::sc::ChunkBatchFinished;
    }, ms);
}

static bool waitChat(TestClient& c, const std::string& substr, int ms=5000){
    Packet p;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        for (const auto& line : c.chatLinesSnapshot())
            if (line.find(substr) != std::string::npos) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // also scan raw recent packets for the substring (fallback for unparsed formats)
    for (const auto& q : c.recentSnapshot()) {
        std::string raw(reinterpret_cast<const char*>(q.body.data()), q.body.size());
        if (raw.find(substr) != std::string::npos) return true;
    }
    return false;
}

static int lastInventoryItemCount(TestClient& c, std::uint32_t itemId,
                                  std::size_t slotIndex) {
    const auto packets = c.recentSnapshot();
    for (auto packet = packets.rbegin(); packet != packets.rend(); ++packet) {
        if (packet->id != proto::pl::sc::ContainerSetContent) continue;
        try {
            ReadBuffer input(packet->body);
            if (input.varint() != 0) continue;
            (void)input.varint(); // state id
            const auto slotCount = input.varint();
            if (slotCount < 0 ||
                static_cast<std::size_t>(slotCount) <= slotIndex)
                continue;
            for (std::int32_t index = 0; index < slotCount; ++index) {
                const auto stack = ItemStack::read(input);
                if (static_cast<std::size_t>(index) == slotIndex)
                    return stack.itemId == itemId ? stack.count : 0;
            }
        } catch (const std::exception&) {
        }
    }
    return -1;
}

static bool nbtContainsString(const nbt::Reader::Value& value,
                              std::string_view expected) {
    if (value.tag == nbt::String && value.str.find(expected) != std::string::npos)
        return true;
    for (const auto& child : value.list)
        if (nbtContainsString(child, expected)) return true;
    for (const auto& [name, child] : value.comp) {
        (void)name;
        if (nbtContainsString(child, expected)) return true;
    }
    return false;
}

static bool chunkContainsSignText(const Packet& packet, int x, int y, int z,
                                  std::string_view expected) {
    if (packet.id != proto::pl::sc::LevelChunkWithLight) return false;
    try {
        ReadBuffer input(packet.body);
        const auto chunkX = input.i32();
        const auto chunkZ = input.i32();
        if (chunkX != (x >> 4) || chunkZ != (z >> 4)) return false;

        nbt::Reader(input).skipRoot(); // heightmaps
        const auto sectionDataLength = input.varint();
        if (sectionDataLength < 0) return false;
        input.bytes(static_cast<std::size_t>(sectionDataLength));

        const auto blockEntityCount = input.varint();
        if (blockEntityCount < 0) return false;
        const auto localX = static_cast<std::uint8_t>((x % 16 + 16) % 16);
        const auto localZ = static_cast<std::uint8_t>((z % 16 + 16) % 16);
        for (std::int32_t index = 0; index < blockEntityCount; ++index) {
            const auto packedXZ = input.u8();
            const auto entityY = input.i16();
            const auto type = input.varint();
            auto updateTag = nbt::Reader(input).readNamedValue();
            if (type == 7 && (packedXZ >> 4) == localX &&
                (packedXZ & 0x0f) == localZ && entityY == y &&
                nbtContainsString(updateTag, expected))
                return true;
        }
    } catch (const std::exception&) {
        return false;
    }
    return false;
}

// last sc Abilities 0x3A flags byte (-1 if none yet)
static int lastAbilitiesFlags(TestClient& c) {
    const auto recent = c.recentSnapshot();
    for (auto it = recent.rbegin(); it != recent.rend(); ++it) {
        if (it->id == proto::pl::sc::Abilities && !it->body.empty())
            return static_cast<std::int8_t>(it->body[0]);
    }
    return -999;
}
static bool waitAbilitiesFlags(TestClient& c, int want, int ms=5000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (lastAbilitiesFlags(c) == want) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return lastAbilitiesFlags(c) == want;
}

static int lastGameEventMode(TestClient& c) {
    const auto recent = c.recentSnapshot();
    for (auto it = recent.rbegin(); it != recent.rend(); ++it) {
        if (it->id != proto::pl::sc::GameEvent || it->body.size() < 5)
            continue;
        ReadBuffer body(it->body);
        // Protocol 769 uses event reason 3 for change_game_mode; reason 4 is
        // the distinct game-won event.
        if (body.u8() == 3) return static_cast<int>(std::lround(body.f32()));
    }
    return -1;
}

static bool waitGameEventMode(TestClient& c, int want, int ms=5000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (lastGameEventMode(c) == want) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return lastGameEventMode(c) == want;
}

// --- W-03: signed command ----------------------------------------------------
static void tSigned(ServerProc& srv) {
    SECTION("W-03 chat_command_signed 0x06 (fixed 256B + msgCount + ack[3])");
    struct Case { const char* cmd; int n; const char* expect; };
    Case cases[] = { {"seed", 0, "Seed:"}, {"list", 1, "Players online"}, {"seed", 2, "Seed:"} };
    for (auto& k : cases) {
        TestClient c;
        CHECK(c.connect("127.0.0.1", srv.port) && c.join("P43Sig"), "W-03 join");
        CHECK(waitInitialChunks(c), "W-03 initial chunk batch complete");
        c.sendSignedCommand(k.cmd, k.n);
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        bool alive = c.alive();
        char msg[128]; snprintf(msg, sizeof(msg), "W-03 signed n=%d alive (no disconnect)", k.n);
        CHECK(alive, msg);
        snprintf(msg, sizeof(msg), "W-03 signed n=%d cmd executed (chat \"%s\")", k.n, k.expect);
        CHECK(waitChat(c, k.expect, 3000), msg);
        c.close();
    }
    // malformed: truncated signed body must not kill the server (policy: ignore, stay alive)
    {
        TestClient c;
        CHECK(c.connect("127.0.0.1", srv.port) && c.join("P43SigBad"), "W-03 malformed join");
        WriteBuffer b; b.string("seed"); b.i64(0); // truncated: no salt/signatures
        c.sendRawPlay(proto::pl::cs::ChatCommandSigned, b);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        TestClient d;
        bool serverAlive = d.connect("127.0.0.1", srv.port) && d.join("P43SigProbe");
        CHECK(serverAlive, "W-03 malformed signed does not kill server");
        c.close(); d.close();
    }
}

// --- W-04: tab complete ------------------------------------------------------
static void tTab(ServerProc& srv) {
    SECTION("W-04 tab_complete 0x0D -> CommandSuggestions 0x10");
    TestClient c;
    CHECK(c.connect("127.0.0.1", srv.port) && c.join("P43Tab"), "W-04 join");
    c.sendTabComplete(7, "/gam");
    TestClient::SuggestionsResp r;
    CHECK(c.waitSuggestions(7, r, 5000), "W-04 0x10 response echoes transactionId 7");
    CHECK(c.alive(), "W-04 alive after tab (no underrun disconnect) [liveness]");
    if (r.transactionId == 7) {
        CHECK(r.start == 1, "W-04 start==1 (token after '/')");
        CHECK(r.length == 3, "W-04 length==3 (\"gam\")");
        CHECK(!r.matches.empty(), "W-04 matches non-empty for \"/gam\"");
    } else {
        CHECK(false, "W-04 start==1 (no response)");
        CHECK(false, "W-04 length==3 (no response)");
        CHECK(false, "W-04 matches non-empty (no response)");
    }
    // empty-text edge: still answers 0x10 (possibly empty matches), no kick
    c.sendTabComplete(8, "");
    TestClient::SuggestionsResp r2;
    CHECK(c.waitSuggestions(8, r2, 5000), "W-04 empty text still answers 0x10");
    CHECK(c.alive(), "W-04 alive after empty tab [liveness]");
    c.close();
}

// --- W-12: finish-ack contamination ------------------------------------------
static void tFinish(ServerProc& srv) {
    SECTION("W-12 finish-ack absorbs settings/pong/pack/known-packs");
    TestClient c;
    CHECK(c.connect("127.0.0.1", srv.port), "W-12 connect");
    CHECK(c.joinWithFinishContamination("P43Fin"), "W-12 contaminated finish still joins play");
    if (c.alive()) {
        c.sendChatCommand("list");
        CHECK(waitChat(c, "Players"),
              "W-12 play usable after contaminated finish");
    } else {
        CHECK(false, "W-12 play usable after contaminated finish");
    }
    c.close();
}

// --- W-01: movement flags ----------------------------------------------------
static void tMoveFlags(ServerProc& srv) {
    SECTION("W-01 MovementFlags u8 bit0=onGround (16 combos alive)");
    TestClient c;
    CHECK(c.connect("127.0.0.1", srv.port) && c.join("P43Mov"), "W-01 join");
    const auto startPosition = c.positionSnapshot();
    double bx = startPosition.x, by = startPosition.y, bz = startPosition.z;
    const std::uint8_t flags[] = {0x00, 0x01, 0x02, 0x03};
    for (auto f : flags) {
        c.sendMovePlayerFlags(bx, by, bz, f);
        c.sendMovePlayerPosRotFlags(bx, by, bz, 0.f, 0.f, f);
        c.sendMovePlayerRotFlags(0.f, 0.f, f);
        c.sendFlyingFlags(f);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    CHECK(c.alive(), "W-01 16 combos (4 kinds x 4 flags) no kick [liveness]");
    CHECK(c.count(proto::pl::sc::Disconnect) == 0, "W-01 no Disconnect sent");
    c.close();
}

static void tFallDamage(ServerProc& srv) {
    SECTION("W-01 fall damage via flags=0x02 (airborne+collision, NOT ground)");
    TestClient c;
    CHECK(c.connect("127.0.0.1", srv.port) && c.join("P43Fall"), "W-01 fall join");
    c.sendChatCommand("gamemode survival");
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    const auto startPosition = c.positionSnapshot();
    double bx = startPosition.x, by = startPosition.y, bz = startPosition.z;
    // rise 20 blocks as airborne (client-authoritative positions)
    for (int i = 1; i <= 10; ++i) {
        c.sendMovePlayerFlags(bx, by + i * 2.0, bz, 0x00);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    // fall back with flags=0x02: spec = airborne (bit0=0). Buggy boolean() reads it as ground.
    for (int i = 9; i >= 0; --i) {
        c.sendMovePlayerFlags(bx, by + i * 2.0, bz, 0x02);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    c.sendMovePlayerFlags(bx, by, bz, 0x01); // land
    bool hurt = c.waitFor([](const Packet& q){ return q.id == proto::pl::sc::DamageEvent; }, 5000);
    CHECK(hurt, "W-01 20-block 0x02 fall deals fall damage (DamageEvent)");
    CHECK(c.alive(), "W-01 alive after fall [liveness]");
    c.close();
}

// --- W-02: use_entity --------------------------------------------------------
static std::int32_t summonHorseNear(TestClient& c, const char* who) {
    // NOTE: /summon takes NO position args (server spawns at player+(2,1,2));
    // extra tokens fail brigadier parse, so send the bare command.
    (void)who;
    // Snapshot the entity-id high-water mark before summoning. A newly joined
    // client's initial entity stream can still contain older horses after
    // join() returns; treating any post-snapshot horse packet as ours makes
    // this test race against that tail of the stream.
    std::int32_t highestSeenEntityId = 0;
    for (const auto& s : c.spawns())
        if (s.eid > highestSeenEntityId) highestSeenEntityId = s.eid;
    c.sendChatCommand("summon minecraft:horse");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(6000);
    while (std::chrono::steady_clock::now() < deadline) {
        auto ss = c.spawns();
        std::int32_t found = -1;
        for (auto& s : ss)
            if (s.type == 63 && s.eid > highestSeenEntityId) found = s.eid;
        if (found >= 0) return found;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return -1;
}

static bool hasPassengersFor(TestClient& c, std::int32_t vehicleId) {
    for (const auto& packet : c.recentSnapshot()) {
        if (packet.id != proto::pl::sc::SetPassengers) continue;
        try {
            ReadBuffer input(packet.body);
            if (input.varint() != vehicleId) continue;
            const auto passengerCount = input.varint();
            if (passengerCount > 0) return true;
        } catch (const std::exception&) {
            // Ignore unrelated malformed/truncated packet captures.
        }
    }
    return false;
}

static bool waitPassengersFor(TestClient& c, std::int32_t vehicleId,
                              int ms) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (hasPassengersFor(c, vehicleId)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return hasPassengersFor(c, vehicleId);
}

static bool waitCountAtLeast(TestClient& c, std::uint8_t packetId,
                             std::size_t wanted, int ms) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (c.count(packetId) >= wanted) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return c.count(packetId) >= wanted;
}

static int latestPassengerCountFor(TestClient& c, std::int32_t vehicleId) {
    const auto packets = c.recentSnapshot();
    for (auto packet = packets.rbegin(); packet != packets.rend(); ++packet) {
        if (packet->id != proto::pl::sc::SetPassengers) continue;
        try {
            ReadBuffer input(packet->body);
            if (input.varint() == vehicleId) return input.varint();
        } catch (const std::exception&) {
        }
    }
    return -1;
}

static bool waitPassengerCount(TestClient& c, std::int32_t vehicleId,
                              int wanted, int ms) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (latestPassengerCountFor(c, vehicleId) == wanted) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return latestPassengerCountFor(c, vehicleId) == wanted;
}

static std::size_t horseTamingEventCount(TestClient& c,
                                         std::int32_t horseId) {
    std::size_t count = 0;
    for (const auto& packet : c.recentSnapshot()) {
        if (packet.id != proto::pl::sc::EntityEvent) continue;
        try {
            ReadBuffer input(packet.body);
            if (input.i32() != horseId) continue;
            const auto status = input.i8();
            if (status == 6 || status == 7) ++count;
        } catch (const std::exception&) {
        }
    }
    return count;
}

static bool waitHorseTamingOutcome(TestClient& c, std::int32_t horseId,
                                   std::size_t previousCount,
                                   std::int8_t& status, int ms) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto packets = c.recentSnapshot();
        std::size_t count = 0;
        for (const auto& packet : packets) {
            if (packet.id != proto::pl::sc::EntityEvent) continue;
            try {
                ReadBuffer input(packet.body);
                if (input.i32() != horseId) continue;
                const auto event = input.i8();
                if (event == 6 || event == 7) ++count;
            } catch (const std::exception&) {
            }
        }
        if (count > previousCount) {
            for (auto packet = packets.rbegin(); packet != packets.rend();
                 ++packet) {
                if (packet->id != proto::pl::sc::EntityEvent) continue;
                try {
                    ReadBuffer input(packet->body);
                    if (input.i32() != horseId) continue;
                    const auto event = input.i8();
                    if (event == 6 || event == 7) {
                        status = event;
                        return true;
                    }
                } catch (const std::exception&) {
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

static bool hasHorseTameMetadata(TestClient& c, std::int32_t horseId) {
    for (const auto& packet : c.recentSnapshot()) {
        if (packet.id != proto::pl::sc::SetEntityMetadata) continue;
        try {
            ReadBuffer input(packet.body);
            if (input.varint() != horseId || input.u8() != 17 ||
                input.varint() != 0)
                continue;
            if ((input.u8() & 0x02U) != 0) return true;
        } catch (const std::exception&) {
        }
    }
    return false;
}

static bool waitHorseTameMetadata(TestClient& c, std::int32_t horseId,
                                  int ms) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (hasHorseTameMetadata(c, horseId)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return hasHorseTameMetadata(c, horseId);
}
// HurtAnimation 0x25 for a specific eid appeared? (fresh horse: any match is ours)
static bool waitHurtFor(TestClient& c, std::int32_t eid, std::size_t, int ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            for (const auto& q : c.recentSnapshot()) {
                if (q.id != proto::pl::sc::HurtAnimation) continue;
                try {
                    ReadBuffer in(q.body);
                    if (in.varint() == eid) return true;
                } catch (...) {}
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

static void tUseEntity(ServerProc& srv) {
    SECTION("W-02 use_entity hand/sneak wire parse and untamed-horse interaction");
    // A freshly summoned adult horse is untamed in this server. Vanilla does
    // not open its inventory on secondary-use; with an empty hand, interaction
    // falls through to mounting. Fresh client + horse isolate each combo.
    struct Combo { int mouse, hand; bool sneak; const char* label; };
    Combo combos[] = {
        {0,0,false,"m0/h0/s0"}, {0,1,false,"m0/h1/s0"},
        {0,0,true,"m0/h0/s1"},  {0,1,true,"m0/h1/s1"},
        {2,0,false,"m2/h0/s0"}, {2,1,false,"m2/h1/s0"},
        {2,0,true,"m2/h0/s1"},  {2,1,true,"m2/h1/s1"},
    };
    for (auto& k : combos) {
        TestClient c;
        char msg[160];
        snprintf(msg, sizeof(msg), "W-02 %s join", k.label);
        char nm[32]; snprintf(nm, sizeof(nm), "P43U%d", (int)(k.mouse * 4 + k.hand * 2 + (k.sneak ? 1 : 0)));
        const bool joined = c.connect("127.0.0.1", srv.port) && c.join(nm);
        CHECK(joined, msg);
        if (!joined) { c.close(); continue; }
        const bool chunksReady = waitInitialChunks(c);
        snprintf(msg, sizeof(msg), "W-02 %s initial chunk batch complete", k.label);
        CHECK(chunksReady, msg);
        if (!chunksReady) { c.close(); continue; }
        std::int32_t eid = summonHorseNear(c, k.label);
        snprintf(msg, sizeof(msg), "W-02 %s horse summoned", k.label);
        CHECK(eid >= 0, msg);
        if (eid >= 0) {
            c.sendUseEntityFull(eid, k.mouse, k.hand, k.sneak);
            const bool mounted = waitPassengersFor(c, eid, 3000);
            snprintf(msg, sizeof(msg), "W-02 %s untamed horse mounts", k.label);
            CHECK(mounted, msg);
            snprintf(msg, sizeof(msg), "W-02 %s untamed horse never opens inventory", k.label);
            CHECK(c.count(proto::pl::sc::OpenHorseWindow) == 0, msg);
        }
        CHECK(c.alive(), "W-02 alive [liveness]");
        c.close();
    }
    // ATTACK x2 (mouse=1, no hand, trailing sneak bool): must land (HurtAnimation
    // for our eid) and never open a window — pre/post stable, guards misparse.
    for (int s = 0; s < 2; ++s) {
        TestClient c;
        char nm[32]; snprintf(nm, sizeof(nm), "P43A%d", s);
        const bool joined = c.connect("127.0.0.1", srv.port) && c.join(nm);
        CHECK(joined, "W-02 atk join");
        if (!joined) { c.close(); continue; }
        const bool chunksReady = waitInitialChunks(c);
        char msg[160];
        snprintf(msg, sizeof(msg), "W-02 atk%d initial chunk batch complete", s);
        CHECK(chunksReady, msg);
        if (!chunksReady) { c.close(); continue; }
        std::int32_t eid = summonHorseNear(c, s ? "atk1" : "atk0");
        snprintf(msg, sizeof(msg), "W-02 atk%d horse summoned", s);
        CHECK(eid >= 0, msg);
        if (eid >= 0) {
            std::size_t w0 = c.count(proto::pl::sc::OpenHorseWindow);
            for (int i = 0; i < 4; ++i) {
                c.sendUseEntityFull(eid, 1, 0, s != 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            snprintf(msg, sizeof(msg), "W-02 atk%d lands (HurtAnimation eid)", s);
            CHECK(waitHurtFor(c, eid, 0, 3000), msg);
            snprintf(msg, sizeof(msg), "W-02 atk%d no window", s);
            CHECK(c.count(proto::pl::sc::OpenHorseWindow) == w0, msg);
        }
        CHECK(c.alive(), "W-02 atk alive [liveness]");
        c.close();
    }

    // Spectator interaction must be rejected before either the mount fallback
    // or the horse-window path can mutate state or emit those packets.
    TestClient spectator;
    const bool spectatorJoined =
        spectator.connect("127.0.0.1", srv.port) &&
        spectator.join("P43SpecHorse");
    CHECK(spectatorJoined, "W-02 spectator joins");
    if (spectatorJoined) {
        CHECK(waitInitialChunks(spectator),
              "W-02 spectator initial chunk batch complete");
        const auto eid = summonHorseNear(spectator, "spectator");
        CHECK(eid >= 0, "W-02 spectator test horse summoned");
        if (eid >= 0) {
            spectator.sendChatCommand("gamemode spectator");
            CHECK(waitGameEventMode(spectator, 3),
                  "W-02 player changes to spectator before interaction");
            spectator.sendUseEntityFull(eid, 0, 0, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            CHECK(!hasPassengersFor(spectator, eid),
                  "W-02 spectator cannot mount a non-player mob");
            CHECK(spectator.count(proto::pl::sc::OpenHorseWindow) == 0,
                  "W-02 spectator cannot open mob inventory");
        }
        spectator.close();
    }
}

static void tHorseTaming(ServerProc& srv) {
    SECTION("W-02 mounted horse taming: fail/buck, golden-carrot temper, success");
    TestClient c;
    const bool joined = c.connect("127.0.0.1", srv.port) &&
                        c.join("P43HorseTame");
    CHECK(joined, "W-02 taming client joins");
    if (!joined) return;
    CHECK(waitInitialChunks(c), "W-02 taming client initial chunks complete");

    const auto horseId = summonHorseNear(c, "taming");
    CHECK(horseId >= 0, "W-02 taming horse summoned");
    if (horseId < 0) return;

    // At temper zero, the first vanilla 1/50 mounted attempt must fail,
    // publish the buck event, and clear the passenger link.
    const auto outcomesBeforeFirstMount = horseTamingEventCount(c, horseId);
    c.sendUseEntityFull(horseId, 0, 0, false);
    CHECK(waitPassengersFor(c, horseId, 3000),
          "W-02 untamed horse accepts the initial mount");
    std::int8_t firstOutcome = -1;
    const bool firstAttemptResolved = waitHorseTamingOutcome(
        c, horseId, outcomesBeforeFirstMount, firstOutcome, 15000);
    CHECK(firstAttemptResolved && firstOutcome == 6,
          "W-02 zero-temper mount produces vanilla failed-taming status 6");
    if (!firstAttemptResolved || firstOutcome != 6) {
        CHECK(c.alive(), "W-02 server remains live after initial taming attempt");
        c.close();
        return;
    }
    CHECK(waitPassengerCount(c, horseId, 0, 2000),
          "W-02 failed taming bucks and clears the passenger list");

    c.sendChatCommand("give @s minecraft:golden_carrot 19");
    CHECK(waitChat(c, "Given 19 x minecraft:golden_carrot", 5000),
          "W-02 test player receives 19 golden carrots");
    const auto goldenCarrotId =
        gen::itemIdByName().at("minecraft:golden_carrot");
    CHECK(lastInventoryItemCount(c, goldenCarrotId, 36) == 19,
          "W-02 golden carrots are in the selected main-hand slot");
    const auto inventoryUpdatesBeforeFeed =
        c.count(proto::pl::sc::ContainerSetContent);
    for (int i = 0; i < 19; ++i) {
        c.sendUseEntityFull(horseId, 0, 0, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(waitCountAtLeast(c, proto::pl::sc::ContainerSetContent,
                           inventoryUpdatesBeforeFeed + 19, 5000),
          "W-02 all 19 golden-carrot feeds are accepted and consumed");
    CHECK(lastInventoryItemCount(c, goldenCarrotId, 36) == 0,
          "W-02 selected main-hand stack reaches zero after feeding");
    CHECK(latestPassengerCountFor(c, horseId) == 0,
          "W-02 feeding raises temper without mounting as a fallback");

    const auto outcomesBeforeSecondMount = horseTamingEventCount(c, horseId);
    c.sendUseEntityFull(horseId, 0, 0, false);
    CHECK(waitPassengerCount(c, horseId, 1, 3000),
          "W-02 player remounts after tempering the horse to its maximum");
    std::int8_t secondOutcome = -1;
    const bool secondAttemptResolved = waitHorseTamingOutcome(
        c, horseId, outcomesBeforeSecondMount, secondOutcome, 15000);
    CHECK(secondAttemptResolved && secondOutcome == 7,
          "W-02 maximum-temper mount tames horse and publishes status 7");
    CHECK(secondAttemptResolved && secondOutcome == 7 &&
              waitHorseTameMetadata(c, horseId, 2000),
          "W-02 tame flag is synchronized in horse metadata");

    if (secondAttemptResolved && secondOutcome == 7) {
        const auto windowsBefore =
            c.count(proto::pl::sc::OpenHorseWindow);
        c.sendUseEntityFull(horseId, 0, 0, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        CHECK(c.count(proto::pl::sc::OpenHorseWindow) == windowsBefore,
              "W-02 secondary-use while riding a tamed horse does not open inventory");
    }
    CHECK(c.alive(), "W-02 liveness after failed and successful taming outcomes");
    c.close();
}

// --- W-06: abilities ---------------------------------------------------------
static void tAbilities(ServerProc& srv) {
    SECTION("W-06 abilities 0x3A gamemode-linked + cs 0x26");
    TestClient c;
    CHECK(c.connect("127.0.0.1", srv.port) && c.join("P43Abil"), "W-06 join");
    CHECK(waitAbilitiesFlags(c, 0x00, 5000),
          "W-06 vanilla-default survival join flags 0x00");
    c.sendChatCommand("gamemode survival");
    CHECK(waitAbilitiesFlags(c, 0x00, 5000), "W-06 survival flags 0x00 (no invuln/fly/creative)");
    c.sendChatCommand("gamemode creative");
    CHECK(waitAbilitiesFlags(c, 0x01 | 0x04 | 0x08, 5000), "W-06 creative flags back to 0x0D");
    // cs 0x26: survival flying claim must not crash/kick; server strips unpermitted flight
    c.sendChatCommand("gamemode survival");
    CHECK(waitAbilitiesFlags(c, 0x00, 5000), "W-06 survival re-confirmed");
    c.sendAbilitiesFlags(0x02); // claim flying without permission
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    CHECK(c.alive(), "W-06 abilities 0x26 received without kick [liveness]");

    TestClient target;
    const bool targetJoined = target.connect("127.0.0.1", srv.port) &&
                              target.join("P43Target");
    CHECK(targetJoined, "W-06 target client joins");
    if (targetJoined) {
        CHECK(waitInitialChunks(target), "W-06 target receives initial chunks");
        c.sendChatCommand("gamemode creative P43Target");
        CHECK(waitGameEventMode(target, 1),
              "W-06 targeted creative sends change_game_mode event reason 3");
        CHECK(waitAbilitiesFlags(target, 0x01 | 0x04 | 0x08),
              "W-06 targeted creative sends matching Abilities flags");
        c.sendChatCommand("gamemode survival P43Target");
        CHECK(waitGameEventMode(target, 0),
              "W-06 targeted survival sends change_game_mode event reason 3");
        CHECK(waitAbilitiesFlags(target, 0x00),
              "W-06 targeted survival clears creative Abilities flags");
        CHECK(target.alive(), "W-06 target remains connected after mode changes [liveness]");
        target.close();
    }
    c.close();
}

// --- W-07: sign --------------------------------------------------------------
static void tSign(ServerProc& srv) {
    SECTION("W-07 update_sign 0x39 -> BlockEntityData 0x07 + relogin persist");
    TestClient c;
    CHECK(c.connect("127.0.0.1", srv.port) && c.join("P43Sign"), "W-07 join");
    CHECK(waitInitialChunks(c), "W-07 initial chunk batch complete");
    const auto position = c.positionSnapshot();
    int sx = (int)std::floor(position.x) + 2, sy = (int)std::floor(position.y), sz = (int)std::floor(position.z);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "setblock %d %d %d minecraft:oak_sign", sx, sy, sz);
    c.sendChatCommand(cmd);
    // placement proof: setblock chat feedback (BlockUpdate may arrive batched as
    // MultiBlockChange, so the feedback string is the robust signal here)
    char msg[128]; snprintf(msg, sizeof(msg), "W-07 sign placed (setblock %d,%d,%d ack)", sx, sy, sz);
    CHECK(waitChat(c, "Changed the block", 8000), msg);
    const std::string lines[4] = {"P43-L1", "P43-L2", "P43-L3", "P43-L4"};
    c.sendSignUpdate(sx, sy, sz, true, lines);
    Packet p;
    bool got07 = c.waitFor([](const Packet& q){ return q.id == proto::pl::sc::BlockEntityData; }, 5000, &p);
    CHECK(got07, "W-07 sign edit re-sends BlockEntityData 0x07");
    if (got07) {
        std::string raw(reinterpret_cast<const char*>(p.body.data()), p.body.size());
        CHECK(raw.find("P43-L1") != std::string::npos, "W-07 0x07 carries line 1 text");
    } else CHECK(false, "W-07 0x07 carries line 1 text");
    c.close();
    // Relogin must receive the persisted sign in the initial chunk's block-
    // entity records. A separate BlockEntityData packet is not required when
    // the entity is serialized directly into LevelChunkWithLight.
    TestClient d;
    CHECK(d.connect("127.0.0.1", srv.port) && d.join("P43Sign"), "W-07 relogin join");
    CHECK(waitInitialChunks(d), "W-07 relogin initial chunk batch complete");
    bool persist = false;
    for (const auto& packet : d.recentSnapshot()) {
        if (chunkContainsSignText(packet, sx, sy, sz, "P43-L1")) {
            persist = true;
            break;
        }
    }
    CHECK(persist, "W-07 relogin chunk contains persisted sign text at its position");
    d.close();
}

int main(int argc, char** argv) {
    const char* bin = argc > 1 ? argv[1] : "./build/cppfm";
    ServerProcessOptions serverOptions;
    serverOptions.portBase = 27000;
    serverOptions.portSpan = 2000;
    serverOptions.viewDistance = 4;
    serverOptions.readyTimeoutMs = 8000;
    serverOptions.worldPrefix = "/tmp/plan43-";
    serverOptions.operatorNames = {
        "P43Fin", "P43Tab", "P43Mov", "P43Fall", "P43Abil", "P43Sig",
        "P43Sign", "P43S0", "P43S1", "P43S2", "P43A0", "P43A1",
        "P43HorseTame", "P43SpecHorse",
        "P43U0", "P43U1", "P43U2", "P43U3", "P43U4", "P43U5",
        "P43U6", "P43U7", "P43U8", "P43U9", "P43U10", "P43U11"
    };
    ServerProc srv;
    if (!srv.start(bin, serverOptions)) { std::printf("FAIL server start\n"); return 2; }
    tSigned(srv);
    tTab(srv);
    tFinish(srv);
    tMoveFlags(srv);
    tFallDamage(srv);
    tUseEntity(srv);
    tHorseTaming(srv);
    tAbilities(srv);
    tSign(srv);
    CHECK(srv.stop(), "owned server process and temporary world cleaned up");
    std::printf("\n=== TEST_PLAN43: %d PASS %d FAIL ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
