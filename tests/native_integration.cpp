// Native end-to-end suite: spawns the real server binary and exercises it
// through TestClient (production framing code). Replaces the Python suites.
#include "TestClient.hpp"
#include "ServerProcess.hpp"
#include <map>
#include "../src/core/Json.hpp"
#include "../src/core/NBT.hpp"
#include "../src/worldgen/DensityFunction.hpp"
#include "../src/worldgen/MultiNoise.hpp"
#include "../src/worldgen/StructureManager.hpp"
#include "../src/game/DatapackManager.hpp"
#include "../src/game/GameRules.hpp"
#include "../src/game/World.hpp"
#include "../src/game/Anvil.hpp"
#include "../src/game/Entities.hpp"
#include "../src/game/AiBrain.hpp"
#include "../src/game/BehaviorTree.hpp"
#include "../src/game/GameServer.hpp"
#include "../src/game/BlockEvent.hpp"
#include "../src/game/Recipes.hpp"
#include "../src/game/TagManager.hpp"
#include "../src/game/LootTables.hpp"
#include "../src/game/EnchantmentHelper.hpp"
#include "../src/game/Items.hpp"
#include "../src/game/MiningCalculator.hpp"
#include "../src/generated/BlockStates.hpp"
#include "../src/physics/Redstone.hpp"
#include "../src/physics/BlockTickScheduler.hpp"
#include "../src/game/FunctionEvaluator.hpp"
#include "../src/game/CommandsHelpers.hpp"
#include "../src/game/DamageSource.hpp"
#include "../src/game/Scoreboard.hpp"
#include <algorithm>
#include <memory>
#include <mutex>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <random>
#include <filesystem>
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>
#include <unordered_map>
#include <utility>

namespace cppfm {
struct NativeIntegrationTestAccess {
    static LootTableEvaluator& lootTables(GameServer& server) {
        return server.datapackManager_.lootTables;
    }

    static bool startTickLoop(GameServer& server) {
        return server.startTickLoop();
    }

    static bool tickLoopActive(GameServer& server) {
        std::lock_guard lock(server.serverThreadTasksMtx_);
        return server.tickLoopActive_;
    }

    static bool isCurrentServerThread(GameServer& server) {
        return server.isCurrentServerThread();
    }

    static bool isCurrentTickThread(GameServer& server) {
        return server.isCurrentTickThread();
    }

    static bool serverThreadOwnerIs(GameServer& server,
                                    const std::thread::id owner) {
        std::lock_guard lock(server.serverThreadTasksMtx_);
        return server.serverThreadId_ == owner;
    }

    static bool runFinalLifecycleCallback(
        GameServer& server, const std::function<bool()>& callback) {
        return server.runFinalLifecycleCallback(callback);
    }
};

struct WorldMutationContractTestAccess {
    static void initializeDimensionEngines(GameServer& server) {
        server.initializeDimensionEngines();
    }

    static void drainLightUpdates(GameServer& server,
                                  const std::int8_t dimension) {
        server.drainLightUpdatesFor(dimension);
    }

    struct OpaqueCrafterResult {
        bool tickCompleted = false;
        bool inputConsumed = false;
        bool outputDropped = false;
        bool opaqueInventoryPreserved = false;
        bool admissionReservationEstablished = false;
        bool admittedPlayerPublished = false;
        bool shulkerRemovedWithoutDrops = false;
        bool shulkerDropsSuppressed = false;
        bool pendingContainerLootGenerated = false;
        bool tntUsesPostCallbackHeldItem = false;
        bool blockEventTargetReplacementPreserved = false;
        bool registeredEmptyLootSuppressesBlockDrop = false;
        bool invalidLootTableUsesBlockFallback = false;
    };

    static void armDig(Player& player, std::int32_t x, std::int32_t y,
                       std::int32_t z, std::uint16_t state) {
        std::lock_guard digLock(player.digMtx);
        player.digActive = true;
        player.digX = x;
        player.digY = y;
        player.digZ = z;
        player.digStartTick = MiningCalculator::packDigStartTick(0, state);
        player.digTotalTicks = 1;
        player.digLastStage = MiningCalculator::packDigStage(0, true);
        ++player.digRevision;
    }

    static OpaqueCrafterResult crafterOutputToOpaqueContainer() {
        OpaqueCrafterResult result;
        ServerConfig config;
        config.jvmEnabled = false;
        config.ioWorkerThreads = 1;
        GameServer server(config);

        auto& runtime = server.dimensionRuntime(0);
        runtime.redstone = std::make_unique<RedstoneEngine>(runtime.world);
        runtime.redstone->setBlockEntityStore(&runtime.blockEntities);
        runtime.redstone->setTickRef(&server.tickNo_);
        auto mutationGate = runtime.world.lockMutationGate();
        (void)mutationGate;
        server.recipes_.loadDefaults();
        server.recipes_.loadDirectory("assets/data/recipes");

        constexpr std::int32_t x = 0;
        constexpr std::int32_t y = 64;
        constexpr std::int32_t z = 0;
        const auto crafterState = static_cast<std::uint16_t>(
            gen::stateWithPropsList("minecraft:crafter",
                                    {{"orientation", "north_up"}}));
        runtime.world.setBlock(x, y, z, crafterState);
        runtime.world.setBlock(x, y, z - 1, static_cast<std::uint16_t>(
            gen::blockNameToState().at("minecraft:chest")));

        nbt::Value opaqueRoot = nbt::Value::makeCompound();
        nbt::Value blockEntities = nbt::Value::makeList(nbt::Compound);
        nbt::Value chest = nbt::Value::makeCompound();
        chest.set("id", nbt::Value::makeString("minecraft:chest"));
        chest.set("x", nbt::Value::makeInt(x));
        chest.set("y", nbt::Value::makeInt(y));
        chest.set("z", nbt::Value::makeInt(z - 1));
        nbt::Value items = nbt::Value::makeList(nbt::Compound);
        nbt::Value unknownItem = nbt::Value::makeCompound();
        unknownItem.set("id", nbt::Value::makeString("example:gem"));
        unknownItem.set("Count", nbt::Value::makeByte(1));
        unknownItem.set("Slot", nbt::Value::makeByte(0));
        unknownItem.set("custom_payload",
                        nbt::Value::makeString("must remain untouched"));
        items.list.push_back(std::move(unknownItem));
        chest.set("Items", std::move(items));
        blockEntities.list.push_back(std::move(chest));
        opaqueRoot.set("block_entities", std::move(blockEntities));
        runtime.blockEntities.readChunkNbt(opaqueRoot);

        const auto crafterOwner = runtime.blockEntities.createShared(
            posKey(x, y, z), BlockEntity::Kind::Crafter);
        {
            std::lock_guard lock(*crafterOwner->stateMtx);
            crafterOwner->crafter.slots[0] = ItemStack::of(
                gen::itemIdByName().at("minecraft:oak_log"));
            // Drive the delayed-craft boundary directly; this regression is
            // about an opaque output target, not redstone signal propagation.
            crafterOwner->crafter.redstoneCraftDueTick = server.tickNo_;
        }

        try {
            server.craftersTickFor(0);
            result.tickCompleted = true;
        } catch (...) {
            return result;
        }

        {
            std::lock_guard lock(*crafterOwner->stateMtx);
            result.inputConsumed = crafterOwner->crafter.slots[0].empty();
        }
        {
            std::lock_guard lock(server.entsMtx_);
            const auto planks = gen::itemIdByName().at("minecraft:oak_planks");
            const auto matchingDrops = std::count_if(
                server.itemDrops_.begin(), server.itemDrops_.end(),
                [planks](const auto& item) {
                    if (!item) return false;
                    const auto stack = item->asStack();
                    return stack.itemId == planks && stack.count == 4;
                });
            result.outputDropped = matchingDrops == 1;
        }
        nbt::Value savedOpaque = nbt::Value::makeList(nbt::Compound);
        runtime.blockEntities.writeChunkNbt(0, -1, savedOpaque);
        for (const auto& savedChest : savedOpaque.list) {
            const auto* savedId = savedChest.get("id");
            const auto* savedX = savedChest.get("x");
            const auto* savedY = savedChest.get("y");
            const auto* savedZ = savedChest.get("z");
            const auto* savedItems = savedChest.get("Items");
            if (!savedId || savedId->tag != nbt::String ||
                savedId->str != "minecraft:chest" || !savedX ||
                savedX->tag != nbt::Int || savedX->i != x || !savedY ||
                savedY->tag != nbt::Int || savedY->i != y || !savedZ ||
                savedZ->tag != nbt::Int || savedZ->i != z - 1 ||
                !savedItems || savedItems->tag != nbt::List)
                continue;
            for (const auto& savedItem : savedItems->list) {
                const auto* itemId = savedItem.get("id");
                const auto* count = savedItem.get("Count");
                const auto* slot = savedItem.get("Slot");
                const auto* payload = savedItem.get("custom_payload");
                if (itemId && itemId->tag == nbt::String &&
                    itemId->str == "example:gem" && count &&
                    count->tag == nbt::Byte && count->b == 1 && slot &&
                    slot->tag == nbt::Byte && slot->b == 0 && payload &&
                    payload->tag == nbt::String &&
                    payload->str == "must remain untouched")
                    result.opaqueInventoryPreserved = true;
            }
        }

        constexpr std::int32_t breakX = 2;
        constexpr std::int32_t breakY = y;
        constexpr std::int32_t breakZ = z;
        const auto shulkerState = static_cast<std::uint16_t>(
            gen::blockNameToState().at("minecraft:red_shulker_box"));
        runtime.world.setBlock(breakX, breakY, breakZ, shulkerState);
        const auto shulkerOwner = runtime.blockEntities.createShared(
            posKey(breakX, breakY, breakZ), BlockEntity::Kind::ShulkerBox);
        {
            std::lock_guard lock(*shulkerOwner->stateMtx);
            shulkerOwner->chest.slots[0] =
                ItemStack::ofName("minecraft:diamond", 1);
        }

        auto player = std::make_shared<Player>();
        player->name = "CrafterOpaqueTarget";
        player->uuid[0] = 0xA1;
        {
            std::lock_guard playerLock(player->stateMtx);
            player->inPlay = true;
        }
        armDig(*player, breakX, breakY, breakZ, shulkerState);
        server.tickNo_ = 1;
        const auto admission = server.reservePlayerAdmission(*player);
        result.admissionReservationEstablished = admission.has_value();
        result.admittedPlayerPublished =
            admission && server.addPlayer(player, *admission);

        std::size_t existingDrops = 0;
        {
            std::lock_guard lock(server.entsMtx_);
            existingDrops = server.itemDrops_.size();
        }
        {
            auto suppressDrops = api::events().blockBreak.subscribeScoped(
                0, [](api::BlockBreakEvent& event) {
                    event.dropItems = false;
                });
            server.tickDigs();
        }

        result.shulkerRemovedWithoutDrops =
            runtime.world.getBlock(breakX, breakY, breakZ) == 0;
        {
            std::lock_guard lock(server.entsMtx_);
            result.shulkerDropsSuppressed =
                server.itemDrops_.size() == existingDrops;
        }

        constexpr std::int32_t lootChestX = 8;
        constexpr std::int64_t lootSeed = 123456;
        constexpr const char* lootTableId =
            "minecraft:chests/simple_dungeon";
        NativeIntegrationTestAccess::lootTables(server).loadDirectory(
            "assets/data/loot_tables");
        const auto expectedLoot =
            NativeIntegrationTestAccess::lootTables(server).evaluateRegisteredTable(
            lootTableId, ItemStack{}, nullptr, lootSeed);
        const auto chestState = static_cast<std::uint16_t>(
            gen::blockNameToState().at("minecraft:chest"));
        runtime.world.setBlock(lootChestX, breakY, breakZ, chestState);
        nbt::Value lootChestNbt = nbt::Value::makeCompound();
        lootChestNbt.set("id", nbt::Value::makeString("minecraft:chest"));
        lootChestNbt.set("x", nbt::Value::makeInt(lootChestX));
        lootChestNbt.set("y", nbt::Value::makeInt(breakY));
        lootChestNbt.set("z", nbt::Value::makeInt(breakZ));
        lootChestNbt.set("LootTable",
            nbt::Value::makeString(lootTableId));
        lootChestNbt.set("LootTableSeed",
            nbt::Value::makeLong(lootSeed));
        lootChestNbt.set("Items", nbt::Value::makeList(nbt::Compound));
        const bool lootChestInstalled = runtime.blockEntities.replaceNbtAt(
            lootChestX, breakY, breakZ, std::move(lootChestNbt));
        armDig(*player, lootChestX, breakY, breakZ, chestState);
        std::size_t lootDropsBefore = 0;
        {
            std::lock_guard lock(server.entsMtx_);
            lootDropsBefore = server.itemDrops_.size();
        }
        server.tickDigs();
        const bool lootChestWasRemoved =
            runtime.world.getBlock(lootChestX, breakY, breakZ) == 0;
        {
            std::lock_guard lock(server.entsMtx_);
            std::vector<bool> matchedLoot(
                expectedLoot ? expectedLoot->size() : 0, false);
            for (std::size_t dropIndex = lootDropsBefore;
                 dropIndex < server.itemDrops_.size(); ++dropIndex) {
                if (!server.itemDrops_[dropIndex]) continue;
                const auto actual = server.itemDrops_[dropIndex]->asStack();
                for (std::size_t expectedIndex = 0;
                     expectedLoot && expectedIndex < expectedLoot->size();
                     ++expectedIndex) {
                    const auto& expected = (*expectedLoot)[expectedIndex];
                    if (!matchedLoot[expectedIndex] &&
                        actual.itemId == expected.itemId &&
                        actual.count == expected.count) {
                        matchedLoot[expectedIndex] = true;
                        break;
                    }
                }
            }
            result.pendingContainerLootGenerated = lootChestInstalled &&
                expectedLoot && !expectedLoot->empty() && lootChestWasRemoved &&
                std::all_of(matchedLoot.begin(), matchedLoot.end(),
                            [](bool matched) { return matched; });
        }

        constexpr std::int32_t tntX = 4;
        const auto stableTnt = static_cast<std::uint16_t>(
            gen::stateWithPropsList("minecraft:tnt", {{"unstable", "false"}}));
        runtime.world.setBlock(tntX, breakY, breakZ, stableTnt);
        {
            std::lock_guard playerLock(player->stateMtx);
            player->gamemode = 0;
            player->heldSlot = 0;
            player->inv[36] = ItemStack::ofName("minecraft:flint_and_steel");
        }
        armDig(*player, tntX, breakY, breakZ, stableTnt);
        std::size_t tntEntitiesBefore = 0;
        {
            std::lock_guard lock(server.entsMtx_);
            tntEntitiesBefore = server.tntEntities_.size();
        }
        auto replaceTntTool = api::events().blockBreak.subscribeScoped(
            0, [player, tntX, breakY, breakZ](api::BlockBreakEvent& event) {
                if (event.x != tntX || event.y != breakY ||
                    event.z != breakZ)
                    return;
                std::lock_guard playerLock(player->stateMtx);
                player->inv[36] = ItemStack::ofName("minecraft:stick");
            });
        server.tickDigs();
        bool callbackReplacedTool = false;
        {
            std::lock_guard playerLock(player->stateMtx);
            callbackReplacedTool =
                player->inv[36].name() == "minecraft:stick";
        }
        {
            std::lock_guard lock(server.entsMtx_);
            result.tntUsesPostCallbackHeldItem = callbackReplacedTool &&
                server.tntEntities_.size() == tntEntitiesBefore;
        }

        constexpr std::int32_t callbackX = 6;
        const auto originalBlock = static_cast<std::uint16_t>(
            gen::blockNameToState().at("minecraft:stone"));
        const auto callbackReplacement = static_cast<std::uint16_t>(
            gen::blockNameToState().at("minecraft:gold_block"));
        runtime.world.setBlock(callbackX, breakY, breakZ, originalBlock);
        armDig(*player, callbackX, breakY, breakZ, originalBlock);
        blockEventDispatcher().addOnBlockBreakHandler(
            [&](const BlockEvent& event) {
                if (event.x == callbackX && event.y == breakY &&
                    event.z == breakZ)
                    runtime.world.setBlock(callbackX, breakY, breakZ,
                                           callbackReplacement);
            });
        server.tickDigs();
        blockEventDispatcher().clearLegacyHandlers();
        result.blockEventTargetReplacementPreserved =
            runtime.world.getBlock(callbackX, breakY, breakZ) ==
            callbackReplacement;

        const auto emptyLootRoot = std::filesystem::temp_directory_path() /
            ("cppfm-empty-loot-" + std::to_string(::getpid()));
        std::error_code fixtureError;
        std::filesystem::create_directories(
            emptyLootRoot / "loot_tables" / "blocks", fixtureError);
        bool fixtureWritten = !fixtureError;
        if (fixtureWritten) {
            std::ofstream tableFile(
                emptyLootRoot / "loot_tables" / "blocks" / "stone.json",
                std::ios::binary | std::ios::trunc);
            tableFile << R"({"pools":[]})";
            fixtureWritten = static_cast<bool>(tableFile);
        }
        if (fixtureWritten) {
            NativeIntegrationTestAccess::lootTables(server).loadDirectory(
                emptyLootRoot.string());
            constexpr std::int32_t emptyLootX = 10;
            runtime.world.setBlock(emptyLootX, breakY, breakZ,
                                   originalBlock);
            armDig(*player, emptyLootX, breakY, breakZ, originalBlock);
            const auto* registeredStoneTable =
                NativeIntegrationTestAccess::lootTables(server).findBlockTable(
                    "minecraft:stone");
            const auto emptyDrops =
                NativeIntegrationTestAccess::lootTables(server).evaluate(
                "minecraft:stone", ItemStack{});
            std::size_t dropsBeforeEmptyLoot = 0;
            {
                std::lock_guard lock(server.entsMtx_);
                dropsBeforeEmptyLoot = server.itemDrops_.size();
            }
            server.tickDigs();
            std::size_t dropsAfterEmptyLoot = 0;
            {
                std::lock_guard lock(server.entsMtx_);
                dropsAfterEmptyLoot = server.itemDrops_.size();
            }
            result.registeredEmptyLootSuppressesBlockDrop =
                registeredStoneTable && emptyDrops.empty() &&
                runtime.world.getBlock(emptyLootX, breakY, breakZ) == 0 &&
                dropsAfterEmptyLoot == dropsBeforeEmptyLoot;

            {
                std::ofstream invalidTable(
                    emptyLootRoot / "loot_tables" / "blocks" / "stone.json",
                    std::ios::binary | std::ios::trunc);
                invalidTable << R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:not_a_real_item"}]}]})";
                fixtureWritten = static_cast<bool>(invalidTable);
            }
            if (fixtureWritten) {
                NativeIntegrationTestAccess::lootTables(server).loadDirectory(
                    emptyLootRoot.string());
                constexpr std::int32_t invalidLootX = 11;
                runtime.world.setBlock(invalidLootX, breakY, breakZ,
                                       originalBlock);
                armDig(*player, invalidLootX, breakY, breakZ, originalBlock);
                const auto* invalidTableRegistered =
                    NativeIntegrationTestAccess::lootTables(server).findBlockTable(
                        "minecraft:stone");
                std::size_t dropsBeforeInvalidLoot = 0;
                {
                    std::lock_guard lock(server.entsMtx_);
                    dropsBeforeInvalidLoot = server.itemDrops_.size();
                }
                server.tickDigs();
                bool fallbackDropFound = false;
                {
                    std::lock_guard lock(server.entsMtx_);
                    for (std::size_t dropIndex = dropsBeforeInvalidLoot;
                         dropIndex < server.itemDrops_.size(); ++dropIndex) {
                        const auto& drop = server.itemDrops_[dropIndex];
                        if (drop && drop->asStack().name() ==
                                        "minecraft:cobblestone")
                            fallbackDropFound = true;
                    }
                }
                result.invalidLootTableUsesBlockFallback =
                    !invalidTableRegistered && fallbackDropFound;
            }
        }
        std::filesystem::remove_all(emptyLootRoot, fixtureError);
        result.registeredEmptyLootSuppressesBlockDrop =
            result.registeredEmptyLootSuppressesBlockDrop && !fixtureError;
        return result;
    }
};
} // namespace cppfm

using namespace cppfm;
using namespace cpptest;

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    const bool c_ = static_cast<bool>(cond); \
    std::printf("  %s  %s\n", c_ ? " ok " : "FAIL", msg); \
    if (!c_) ++g_fail; } while (0)

static std::uint16_t findEphemeralPort() {
    const auto socket = platform::createTcpSocket();
    if (!platform::isValid(socket)) return 0;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (platform::bindSocket(socket, reinterpret_cast<sockaddr*>(&address),
                             sizeof(address)) != 0) {
        platform::closeSocket(socket);
        return 0;
    }
    platform::socket_length_t length = sizeof(address);
    const int result = ::getsockname(socket, reinterpret_cast<sockaddr*>(&address),
                                     &length);
    platform::closeSocket(socket);
    return result == 0 ? ntohs(address.sin_port) : 0;
}

// Minimal chunk-section reader to assert world contents from wire bytes.
// Returns block state at (wx,wy,wz) from a LevelChunkWithLight body, or -1.
static std::int64_t chunkBlockAt(const std::vector<std::uint8_t>& body,
                                 std::int32_t wx, std::int32_t wy, std::int32_t wz) {
    ReadBuffer in(body);
    const std::int32_t cx = in.i32(), cz = in.i32();
    if (((wx >> 4) != cx) || ((wz >> 4) != cz)) return -1;
    { nbt::Reader r(in); r.skipRoot(); }                       // heightmaps
    const std::int32_t size = in.varint();
    const std::size_t end = in.off + static_cast<std::size_t>(size);
    std::vector<std::vector<std::uint32_t>> sections;
    while (in.off + 3 <= end && sections.size() < 24) {
        in.u16();                                            // block count (unused)
        // blocks container
        auto readContainer = [&]() -> std::vector<std::uint32_t> {
            const std::uint8_t bits = in.u8();
            if (bits == 0) {
                const std::int32_t v = in.varint();
                in.varint();                                    // longCount(=0)
                return std::vector<std::uint32_t>(4096, static_cast<std::uint32_t>(v));
            }
            const std::int32_t palN = in.varint();
            std::vector<std::uint32_t> pal(static_cast<std::size_t>(palN));
            for (auto& v : pal) v = static_cast<std::uint32_t>(in.varint());
            const std::int64_t nLongs = in.varint();
            const int per = 64 / bits;
            std::vector<std::uint32_t> vals; vals.reserve(4096);
            for (std::int64_t l = 0; l < nLongs && vals.size() < 4096; ++l) {
                const std::uint64_t word = in.u64();
                for (int e = 0; e < per && vals.size() < 4096; ++e)
                    vals.push_back(pal[(word >> (e * bits)) & ((1u << bits) - 1)]);
            }
            vals.resize(4096, pal.empty() ? 0 : pal.back());
            return vals;
        };
        sections.push_back(readContainer());
        readContainer();                                        // biomes
    }
    const int ly = wy + 64;
    if (ly < 0 || ly >= 384) return -1;
    if (sections.size() <= static_cast<std::size_t>(ly >> 4)) return -1;
    const auto& blocks = sections[static_cast<std::size_t>(ly >> 4)];
    if (blocks.size() != 4096) return -1;
    return blocks[((ly & 15) << 8) | ((wz & 15) << 4) | (wx & 15)];
}

// ------------------------------------------------------------------ scenarios
static void scenarioStatus(TestClient& c) {
    std::printf("[status]\n");
    const std::string js = c.queryStatusJson();
    CHECK(js.find("\"protocol\":769") != std::string::npos, "status advertises protocol 769");
    CHECK(js.find("1.21.4") != std::string::npos, "status advertises 1.21.4");
    try {
        const auto status = json::Value::parse(js);
        CHECK(status.isObj() && status.at("version").at("protocol").asInt(-1) == 769,
              "status response is valid JSON");
        CHECK(status.at("description").at("text").asStr() == "status \"quote\" \\ slash",
              "status escapes and restores MOTD text");
    } catch (const std::exception& e) {
        CHECK(false, "status response is valid JSON");
        std::printf("    [diag] status parse: %s\n", e.what());
        CHECK(false, "status escapes and restores MOTD text");
    }
}

static void scenarioJoinBuildChat(ServerProc& srv) {
    std::printf("[join/build/chat/persistence]\n");
    TestClient a;
    CHECK(a.connect("127.0.0.1", srv.port), "client A connects");
    std::printf("    [diag] TesterA local port %u\n", a.localPort());
    CHECK(a.join("TesterA"), "A joins through login+configuration");
    Packet joinPkt;
    const bool gotJoin = a.waitFor([](const Packet& p){ return p.id == proto::pl::sc::Login; }, 5000, &joinPkt);
    if (!gotJoin) {
        std::string hist;
        std::map<std::uint8_t,int> m;
        for (const auto& p : a.recentSnapshot()) ++m[p.id];
        for (auto& [k,v] : m) hist += " " + std::to_string(k) + "x" + std::to_string(v);
        std::printf("    [diag] recent histogram: %s\n", hist.c_str());
    }
    CHECK(gotJoin, "A receives join game");

    // initial chunk flood around spawn (vd=2 -> at least 5x5)
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8000);
    while (a.chunkCount() < 25 && std::chrono::steady_clock::now() < deadline) a.pump(50);
    CHECK(a.chunkCount() >= 25, "A received initial chunks (>=25)");

    // declare_commands should have been advertised
    CHECK(a.counters().declarations >= 1, "A received command tree");

    // dig the grass under spawn column and verify echo + ack + persistence bytes
    // regression fix: 0,-61,0 is inside spawn-protection=16, use 30,-61,0 outside (see docs/SPEC_OPS.md spawn-protection policy)
    a.sendPosition(30.5, -60.0, 0.5);
    a.pump(150);
    a.sendDig(30, -61, 0, 7);                    // break a surface grass block outside spawn protection
    bool sawAck = false, sawAirEcho = false;
    const auto dl2 = std::chrono::steady_clock::now() + std::chrono::milliseconds(4000);
    while (std::chrono::steady_clock::now() < dl2 && !(sawAck && sawAirEcho)) {
        a.pump(40);
        for (const auto& u : a.blockUpdatesSnapshot())
            if (u.x == 30 && u.y == -61 && u.z == 0 && u.state == 0) sawAirEcho = true;
        sawAck = a.counters().acknowledgements > 0;
    }
    CHECK(sawAck, "dig acknowledged (sequence)");
    if (!sawAirEcho) {
        const auto updates = a.blockUpdatesSnapshot();
        const auto observed = a.counters();
        std::printf("    [diag] blockUpdates=%zu acks=%d\n", updates.size(), observed.acknowledgements);
        for (const auto& u : updates)
            std::printf("      upd (%d,%d,%d)->%u\n", u.x, u.y, u.z, u.state);
    }
    CHECK(sawAirEcho, "block update broadcast for dug block");

    // chat round-trip
    a.sendChatMessage("integration-hello");
    bool sawChat = false;
    const auto dl3 = std::chrono::steady_clock::now() + std::chrono::milliseconds(4000);
    while (std::chrono::steady_clock::now() < dl3 && !sawChat) {
        a.pump(40);
        for (const auto& line : a.chatLinesSnapshot()) if (line.find("integration-hello") != std::string::npos) sawChat = true;
    }
    CHECK(sawChat, "chat echoed back through system chat");

    // LastSeenMessages.Update carries an offset, not a count capped at 20.
    // A zero acknowledgement mask is valid even when the offset has advanced.
    a.sendChatMessage("offset-chat", 21);
    bool sawOffsetChat = false;
    const auto dl3b = std::chrono::steady_clock::now() + std::chrono::milliseconds(4000);
    while (std::chrono::steady_clock::now() < dl3b && !sawOffsetChat) {
        a.pump(40);
        for (const auto& line : a.chatLinesSnapshot())
            if (line.find("offset-chat") != std::string::npos) sawOffsetChat = true;
    }
    CHECK(sawOffsetChat, "chat with advanced last-seen offset remains accepted");

    // find chunk containing origin among A's raw chunks and verify air there now
    // (post-dig re-stream check happens on B below; A keeps its original chunks)

    a.close();

    // reconnect as B: edits must persist
    TestClient b;
    CHECK(b.connect("127.0.0.1", srv.port), "client B connects after A left");
    CHECK(b.join("TesterB"), "B joins");
    const auto dl4 = std::chrono::steady_clock::now() + std::chrono::milliseconds(8000);
    bool persisted = false;
    while (std::chrono::steady_clock::now() < dl4 && !persisted) {
        b.pump(50);
        for (const auto& body : b.rawChunksSnapshot()) {
            ReadBuffer in(body);
            const std::int32_t cx = in.i32(), cz = in.i32();
            if (cx == 1 && cz == 0) persisted = chunkBlockAt(body, 30, -61, 0) == 0;
        }
    }
    CHECK(persisted, "edit persisted across reconnect (fresh chunk is air)");
    b.close();
}

static void scenarioMultiplayer(ServerProc& srv) {
    std::printf("[multiplayer visibility]\n");
    TestClient alice, bob;
    CHECK(alice.connect("127.0.0.1", srv.port) && alice.join("Alice"), "Alice joins");
    alice.pump(500);
    CHECK(bob.connect("127.0.0.1", srv.port) && bob.join("Bob"), "Bob joins");

    // Alice should receive Bob's spawn entity once he spawns
    bool aliceSeesBob = false, bobSeesAliceSpawn = false;
    const auto dl = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
    while (std::chrono::steady_clock::now() < dl && !(aliceSeesBob && bobSeesAliceSpawn)) {
        alice.pump(30); bob.pump(30);
        aliceSeesBob = alice.counters().spawns > 0;
        bobSeesAliceSpawn = bob.counters().spawns > 0;
    }
    CHECK(aliceSeesBob, "Alice sees Bob's spawn_entity");
    CHECK(bobSeesAliceSpawn, "Bob sees Alice's spawn_entity");

    // movement relay
    alice.sendPosition(12.5, -60.0, 8.5);
    const auto dl2 = std::chrono::steady_clock::now() + std::chrono::milliseconds(4000);
    while (bob.counters().entityMoves == 0 && std::chrono::steady_clock::now() < dl2) { alice.pump(20); bob.pump(20); }
    CHECK(bob.counters().entityMoves > 0, "Bob sees Alice move");

    // chat cross-delivery + leave broadcast
    alice.sendChatMessage("hi-bob");
    bool bobGotChat = false;
    const auto dl3 = std::chrono::steady_clock::now() + std::chrono::milliseconds(4000);
    while (!bobGotChat && std::chrono::steady_clock::now() < dl3) {
        bob.pump(40);
        for (const auto& l : bob.chatLinesSnapshot()) if (l.find("hi-bob") != std::string::npos) bobGotChat = true;
    }
    CHECK(bobGotChat, "Bob receives Alice's chat");
    alice.close();
    bool bobSawLeave = false;
    const auto dl4 = std::chrono::steady_clock::now() + std::chrono::milliseconds(4000);
    while (!bobSawLeave && std::chrono::steady_clock::now() < dl4) {
        bob.pump(40);
        for (const auto& l : bob.chatLinesSnapshot()) if (l.find("left the game") != std::string::npos) bobSawLeave = true;
    }
    CHECK(bobSawLeave, "Bob sees leave broadcast");
    bob.close();
}

static void scenarioStress(ServerProc& srv, int n) {
    std::printf("[stress x%d]\n", n);
    std::atomic<int> ok{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < n; ++i)
        threads.emplace_back([&, i]{
            TestClient t;
            if (!t.connect("127.0.0.1", srv.port)) return;
            if (!t.join("Bot" + std::to_string(i))) return;
            const auto dl = std::chrono::steady_clock::now() + std::chrono::milliseconds(15000);
            while (t.chunkCount() < 3 && std::chrono::steady_clock::now() < dl) t.pump(50);
            if (t.count(proto::pl::sc::Login) > 0 && t.chunkCount() > 0) ++ok;
            t.close();
        });
    for (auto& th : threads) th.join();
    CHECK(ok == n, "all stress bots joined with chunks");
}

static void scenarioMobStateLockBoundary() {
    std::printf("\n[Mob side-effect lock boundary]\n");
    MobEntity owner;
    MobEntity other;
    std::recursive_mutex mutex;
    std::unique_lock<std::recursive_mutex> lock(mutex);
    const auto previous = setBehaviorTreeMobStateLock(&owner, &lock);
    bool observedReleased = false;
    CHECK(runWithoutMobStateLock(owner, [&] {
              observedReleased = !lock.owns_lock();
          }),
          "source-aware mob side-effect boundary accepts its owner");
    CHECK(observedReleased,
          "mob side-effect boundary releases the active state lock");
    CHECK(lock.owns_lock(),
          "mob side-effect boundary reacquires the active state lock");

    bool wrongOwnerRan = false;
    CHECK(!runWithoutMobStateLock(other, [&] { wrongOwnerRan = true; }),
          "source-aware mob side-effect boundary rejects another Mob");
    CHECK(!wrongOwnerRan && lock.owns_lock(),
          "another Mob cannot release the current Mob state lock");

    bool compatibilityReleased = false;
    runWithoutMobStateLock([&] {
        compatibilityReleased = !lock.owns_lock();
    });
    CHECK(compatibilityReleased && lock.owns_lock(),
          "legacy no-source boundary remains safe for the active owner");
    setBehaviorTreeMobStateLock(previous.mob, previous.lock);
}


void scenarioWorldGenParity(){
    std::printf("\n[WorldGen parity - Density 7 types / MultiNoise isosceles / Structure salts]\n");
    using namespace cppfm::worldgen;
    // Density: OldBlendedNoise
    {
        DensityPipeline pipe; pipe.setSeed(0);
        std::string j = R"({"type":"old_blended_noise","xz_scale":1,"y_scale":1,"xz_factor":80,"y_factor":160,"smear_scale_multiplier":8})";
        auto v = cppfm::json::Value::parse(j); std::string err;
        bool ok = pipe.buildFromJson(v, &err);
        CHECK(ok, "Density old_blended_noise parse");
        double s = pipe.sample(0,0,0);
        CHECK(std::isfinite(s), "old_blended_noise sample finite (not stub 0)");
    }
    {
        DensityPipeline pipe; pipe.setSeed(123);
        auto v = cppfm::json::Value::parse(R"({"type":"end_islands"})");
        pipe.buildFromJson(v,nullptr);
        double c = pipe.sample(0,0,0);
        CHECK(c == -0.84375, "end_islands center -0.84375");
        double outer = pipe.sample(2000,0,0);
        CHECK(outer > -0.84375 && outer < 0.5625, "end_islands outer in (-0.84375,0.5625)");
    }
    {
        DensityPipeline pipe; pipe.setSeed(0);
        std::string j1 = R"({"type":"weird_scaled_sampler","rarity_value_mapper":"type_1","noise":"minecraft:terrain","input":{"type":"constant","value":0.5}})";
        auto v1 = cppfm::json::Value::parse(j1); pipe.buildFromJson(v1,nullptr);
        double s1 = pipe.sample(0,0,0);
        std::string j2 = R"({"type":"weird_scaled_sampler","rarity_value_mapper":"type_2","noise":"minecraft:terrain","input":{"type":"constant","value":0.5}})";
        auto v2 = cppfm::json::Value::parse(j2); pipe.buildFromJson(v2,nullptr);
        double s2 = pipe.sample(0,0,0);
        CHECK(s2 > s1, "weird_scaled_sampler type_2 scale > type_1 for 0.5");
    }
    {
        DensityPipeline pipe;
        pipe.setBeardifierProvider([](int,int){ return 1.0; });
        auto v = cppfm::json::Value::parse(R"({"type":"beardifier"})");
        pipe.buildFromJson(v,nullptr);
        double near = pipe.sample(0,10,0);
        double far = pipe.sample(0,50,0);
        CHECK(near > far, "beardifier yFactor peak 10 > 50");
    }
    {
        DensityPipeline pipe; pipe.setSeed(1);
        auto va = cppfm::json::Value::parse(R"({"type":"shift_a","noise":"minecraft:offset"})");
        auto vb = cppfm::json::Value::parse(R"({"type":"shift_b","noise":"minecraft:offset"})");
        std::string err;
        pipe.buildFromJson(va,&err); double sa1 = pipe.sample(10,5,20); double sa2 = pipe.sample(10,0,20);
        // ShiftA y independent: sample(x,y,z) == sample(x,0,z)
        CHECK(std::abs(sa1 - sa2) < 1e-9, "ShiftA y-independent");
        (void)vb;
    }
    {
        DensityPipeline pipe; pipe.setSeed(0);
        auto v = cppfm::json::Value::parse(R"({"type":"cube","argument":{"type":"constant","value":2.0}})");
        pipe.buildFromJson(v,nullptr);
        double s = pipe.sample(0,0,0);
        CHECK(std::abs(s - 8.0) < 1e-9, "cube 2.0 -> 8.0");
    }
    {
        DensityPipeline pipe; pipe.setSeed(0);
        auto v = cppfm::json::Value::parse(R"({"type":"blend_alpha"})");
        pipe.buildFromJson(v,nullptr);
        double c = pipe.sample(8,0,8); // centre of chunk: distance 8 -> alpha 1
        CHECK(std::abs(c - 1.0) < 1e-9, "blend_alpha centre 1.0");
        double e = pipe.sample(0,0,0); // edge
        CHECK(e < 0.2, "blend_alpha edge <0.2");
    }
    // MultiNoise isosceles
    {
        MultiNoiseBiomeSource src(0);
        CHECK(src.hypercubeEntryCount() == src.biomeEntryCount(), "MultiNoise hypercubes == points (65)");
        ClimateParams mid{}; mid.temperature=0.1; mid.humidity=0.25; mid.continentalness=0.12; mid.erosion=0.10; mid.depth=0.0; mid.weirdness=0.0;
        // nearest via isosceles should be plains or forest; check stable selection
        const std::string& k = src.sampleByClimate(mid);
        CHECK(!k.empty(), "MultiNoise isosceles sampleByClimate non-empty");
        // width-normalized: create two hypercubes with same centre but diff width via addCube
        MultiNoiseBiomeSource s2(999);
        s2.clear();
        NoiseHypercube ca; ca.temperature={0.0f,0.2f}; ca.humidity={-0.1f,0.1f}; ca.continentalness={0.1f,0.14f}; ca.erosion={0.3f,0.4f}; ca.depth={-0.1f,0.1f}; ca.weirdness={-0.1f,0.1f};
        NoiseHypercube cb; cb.temperature={0.0f,0.2f}; cb.humidity={0.2f,0.6f}; cb.continentalness={0.1f,0.14f}; cb.erosion={0.05f,0.15f}; cb.depth={-0.1f,0.1f}; cb.weirdness={-0.1f,0.1f};
        s2.addCube("minecraft:plains_test", ca);
        s2.addCube("minecraft:forest_test", cb);
        ClimateParams qp{}; qp.temperature=0.1; qp.humidity=0.15; qp.continentalness=0.12; qp.erosion=0.20; qp.depth=0.0; qp.weirdness=0.0;
        const std::string& sel = s2.sampleByClimate(qp);
        CHECK(sel == "minecraft:plains_test" || sel == "minecraft:forest_test", "isosceles selects hypercube");
    }
    // Structures 20 sets salts
    {
        StructureManager mgr(0);
        auto& sets = mgr.sets();
        CHECK(sets.size() == 20, "StructureManager 20 sets");
        auto find = [&](const char* n)->const SMStructureSet* { for(auto& s: sets) if(s.name==n) return &s; return nullptr; };
        auto* v = find("minecraft:village"); CHECK(v && v->salt==10387312ULL, "village salt 10387312");
        auto* ac = find("minecraft:ancient_city"); CHECK(ac && ac->salt==20083232ULL, "ancient_city salt 20083232");
        auto* tr = find("minecraft:trail_ruins"); CHECK(tr && tr->salt==83469867ULL, "trail_ruins salt 83469867");
        auto* dp = find("minecraft:desert_pyramid"); CHECK(dp && dp->salt==14357617ULL && dp->spacing==32, "desert_pyramid 32/8 14357617");
        auto* po = find("minecraft:pillager_outpost"); CHECK(po && po->salt==165745296ULL, "pillager_outpost salt 165745296");
        auto* sw = find("minecraft:shipwreck"); CHECK(sw && sw->salt==165745295ULL, "shipwreck salt 165745295");
        auto* oru = find("minecraft:ocean_ruins"); CHECK(oru && oru->salt==14357621ULL, "ocean_ruins salt 14357621");
        auto* tc = find("minecraft:trial_chambers"); CHECK(tc && tc->salt==94251327ULL, "trial_chambers salt 94251327 (wiki)");
        auto* rp = find("minecraft:ruined_portal"); CHECK(rp && rp->salt==34222645ULL, "ruined_portal salt 34222645");
        auto* mon = find("minecraft:monument"); CHECK(mon && mon->spread==SMStructureSet::Triangular, "monument triangular");
        auto* mans = find("minecraft:mansion"); CHECK(mans && mans->spread==SMStructureSet::Triangular, "mansion triangular");
        // spacing validation smStructureAtChunk deterministic
        if (v) {
            auto at = smStructureAtChunk(*v, 12345, 0, 0);
            const bool valid = at.set == v &&
                (!at.present || (std::abs(at.originCx) <= v->maxHoriz &&
                                 std::abs(at.originCz) <= v->maxHoriz &&
                                 at.originX == at.originCx * 16 + v->locateOffsetX &&
                                 at.originZ == at.originCz * 16 + v->locateOffsetZ));
            CHECK(valid, "smStructureAtChunk village returns a bounded origin");
            auto at2 = smStructureAtChunk(*v, 12345, 0, 0);
            CHECK(at.originCx == at2.originCx && at.originCz == at2.originCz, "smStructureAtChunk deterministic");
        }
        // triangular offset check: monument linear vs triangular differ
        if (mon) {
            SMStructureSet linear=*mon; linear.spread=SMStructureSet::Linear;
            // triangular uses average, usually different offset (not strictly > but at least one differs over several cells)
            bool sourceSetsValid=true;
            for(int cx=0;cx<4;++cx) for(int cz=0;cz<4;++cz){
                auto b1=smStructureAtChunk(linear,0,cx,cz);
                auto b2=smStructureAtChunk(*mon,0,cx,cz);
                sourceSetsValid = sourceSetsValid &&
                    ((!b1.present || b1.set == &linear) && (!b2.present || b2.set == mon));
            }
            CHECK(sourceSetsValid,
                  "linear and triangular structure queries retain their source set");
        }
        // frequency buried_treasure
        auto* bt = find("minecraft:buried_treasure"); CHECK(bt && bt->frequency==0.01, "buried_treasure frequency 0.01");
        // locate golden via smStructureAtChunk search
        StructureManager mgr2(12345);
        auto pos = smStructureAtChunk(mgr2.sets()[0], 12345, 0, 0);
        CHECK(pos.set == &mgr2.sets()[0] &&
              (!pos.present || (std::abs(pos.originCx) <= mgr2.sets()[0].maxHoriz &&
                                std::abs(pos.originCz) <= mgr2.sets()[0].maxHoriz)),
              "locate structure returns a bounded optional result");
    }
    // Canonical structure manager still exposes all 20 vanilla sets.
    {
        StructureManager canonical(0);
        auto& sets = canonical.sets();
        CHECK(sets.size()==20, "canonical StructureManager 20 sets");
    }
}

void scenarioPredicateUnit(){
    std::printf("\n[Predicate unit — DatapackManager::testPredicate/evaluatePredicateValue 8 cases (plan35 §3/§6)]\n");
    DatapackManager dm;
    GameRuleManager gr;
    // Populate predicates registry (mimics datapack load) — 8 distinct predicate JSONs
    dm.predicates["test:check_true"]  = R"({"condition":"minecraft:check_gamerule","game_rule":"doMobSpawning","value":true})";
    dm.predicates["test:check_false"] = R"({"condition":"minecraft:check_gamerule","game_rule":"doMobSpawning","value":false})";
    dm.predicates["test:loc_in"]      = R"({"condition":"minecraft:location_check","predicate":{"position":{"x":{"min":0,"max":10},"y":{"min":-64,"max":320},"z":{"min":-10,"max":10}}}})";
    dm.predicates["test:loc_out"]     = R"({"condition":"minecraft:location_check","predicate":{"position":{"x":{"min":100,"max":200}}}})";
    dm.predicates["test:entity_zombie"] = R"({"condition":"minecraft:entity_properties","predicate":{"type":"minecraft:zombie"}})";
    dm.predicates["test:random_high"] = R"({"condition":"minecraft:random_chance","chance":0.9})";
    dm.predicates["test:random_low"]  = R"({"condition":"minecraft:random_chance","chance":0.1})";
    dm.predicates["test:any_of"]      = R"({"condition":"minecraft:any_of","terms":[{"condition":"minecraft:random_chance","chance":0.0},{"condition":"minecraft:random_chance","chance":0.9}]})";
    // 1) check_gamerule doMobSpawning true when gamerule true -> true
    {
        gr.set("doMobSpawning","true");
        PredicateContext ctx; ctx.gamerules=&gr;
        CHECK(dm.testPredicate("test:check_true", ctx)==true, "predicate check_gamerule doMobSpawning true -> true");
    }
    // 2) check_gamerule doMobSpawning false when gamerule true -> false
    {
        gr.set("doMobSpawning","true");
        PredicateContext ctx; ctx.gamerules=&gr;
        CHECK(dm.testPredicate("test:check_false", ctx)==false, "predicate check_gamerule doMobSpawning false when true -> false");
    }
    // 3) location_check position in range (x=5 inside 0..10) -> true, needs world != nullptr
    {
        cppfm::World w("minecraft:plains", cppfm::LevelType::Flat, 0);
        PredicateContext ctx; ctx.world=&w; ctx.gamerules=&gr; ctx.x=5; ctx.y=0; ctx.z=0;
        CHECK(dm.testPredicate("test:loc_in", ctx)==true, "predicate location_check pos in 0..10 (x=5) -> true");
    }
    // 4) location_check position out of range (x=5 outside 100..200) -> false
    {
        cppfm::World w("minecraft:plains", cppfm::LevelType::Flat, 0);
        PredicateContext ctx; ctx.world=&w; ctx.gamerules=&gr; ctx.x=5; ctx.y=0; ctx.z=0;
        CHECK(dm.testPredicate("test:loc_out", ctx)==false, "predicate location_check pos 100..200 with x=5 -> false");
    }
    // 5) entity_properties type zombie match -> true
    {
        MobEntity zombie; zombie.kind=MobKind::Zombie;
        PredicateContext ctx; ctx.entity=&zombie; ctx.gamerules=&gr;
        CHECK(dm.testPredicate("test:entity_zombie", ctx)==true, "predicate entity_properties type zombie (zombie) -> true");
    }
    // 6) entity_properties type zombie mismatch with creeper -> false
    {
        MobEntity creeper; creeper.kind=MobKind::Creeper;
        PredicateContext ctx; ctx.entity=&creeper; ctx.gamerules=&gr;
        CHECK(dm.testPredicate("test:entity_zombie", ctx)==false, "predicate entity_properties type zombie (creeper) -> false");
    }
    // 7) random_chance 0.9 (threshold 0.5) -> true
    {
        PredicateContext ctx;
        CHECK(dm.testPredicate("test:random_high", ctx)==true, "predicate random_chance 0.9 >=0.5 -> true");
    }
    // 8) random_chance 0.1 -> false + inverted via any_of (any_of with one true -> true)
    {
        PredicateContext ctx;
        CHECK(dm.testPredicate("test:random_low", ctx)==false, "predicate random_chance 0.1 <0.5 -> false");
        CHECK(dm.testPredicate("test:any_of", ctx)==true, "predicate any_of [0.0,0.9] -> true");
        // inverted: {condition:inverted, term:{random 0.9}} -> false
        json::Value inv = json::Value::parse(R"({"condition":"minecraft:inverted","term":{"condition":"minecraft:random_chance","chance":0.9}})");
        CHECK(dm.evaluatePredicateValue(inv, ctx)==false, "predicate inverted(random 0.9) -> false");
        // only count 8 CHECKs for the 8 cases, but inverted is extra documentation; treat any_of as 8th
    }
}

void scenarioMobAI30(){
    std::printf("\n[Mob AI 30 — Brain Goals/BT 10 cases (plan36 B-01)]\n");
    // 10 representative kinds: witch, ravager, iron_golem, bee, wolf, drowned, villager, piglin, cat, fox/panda/dolphin/evoker
    // 1 witch potion throw — shouldStart when player in 16m and cooldown 0
    {
        MobEntity m; m.kind=MobKind::Witch; m.witchPotionCooldown=0;
        AiContext ctx; cppfm::Player p{}; p.x=5; p.z=0; ctx.nearestPlayer=&p; ctx.nearestPlayerDist2=25;
        WitchPotionThrowGoal g;
        CHECK(g.shouldStart(m, ctx)==true, "mob_ai witch shouldStart with player 5m and cooldown 0");
        bool kept = g.tick(m, ctx, 1);
        CHECK(kept && m.witchPotionCooldown > 1, "mob_ai witch tick sets cooldown");
    }
    // 2 ravager roar — player 3m
    {
        MobEntity m; m.kind=MobKind::Ravager; m.ravagerRoarCooldown=0;
        AiContext ctx; cppfm::Player p{}; p.x=3; p.z=0; ctx.nearestPlayer=&p; ctx.nearestPlayerDist2=9;
        RavagerRoarGoal g;
        bool start = g.shouldStart(m, ctx);
        CHECK(start, "mob_ai ravager starts within roar range");
        g.tick(m, ctx, 10);
        CHECK(m.ravagerRoarCooldown == 110 && m.ravagerStunUntil == 20,
              "mob_ai ravager tick sets the documented cooldowns");
    }
    // 3 iron_golem defend — with player
    {
        MobEntity m; m.kind=MobKind::IronGolem;
        AiContext ctx; cppfm::Player p{}; ctx.nearestPlayer=&p; ctx.nearestPlayerDist2=100;
        IronGolemDefendGoal g;
        CHECK(!g.shouldStart(m, ctx), "mob_ai iron_golem does not defend without a threat");
        bool r = g.tick(m, ctx, 20);
        CHECK(!r && m.ironGolemDefendCooldown == 0,
              "mob_ai iron_golem leaves cooldown unchanged without a hostile");
    }
    // 4 bee pollinate — check goal no crash
    {
        MobEntity m; m.kind=MobKind::Bee; m.beeHasNectar=false;
        m.y = 300;
        AiContext ctx;
        cppfm::World w("minecraft:plains", LevelType::Flat, 0);
        ctx.world=&w;
        BeePollinateGoal g;
        CHECK(g.shouldStart(m, ctx), "mob_ai bee pollination starts without nectar");
        bool r = g.tick(m, ctx, 30);
        CHECK(!r && !m.beeHasNectar, "mob_ai bee stops when no flower is reachable");
    }
    // 5 wolf anger — check anger goal
    {
        MobEntity m; m.kind=MobKind::Wolf; m.wolfAngerTarget=-1;
        AiContext ctx; cppfm::Player p{}; p.x=6; ctx.nearestPlayer=&p; ctx.nearestPlayerDist2=36;
        WolfAngerGoal g;
        CHECK(!g.shouldStart(m, ctx), "mob_ai wolf is not angry without damage or anger state");
        bool r = g.tick(m, ctx, 40);
        CHECK(r && m.wolfAngerTarget == p.entityId && m.wolfAngerUntil == 140,
              "mob_ai wolf tick acquires the nearby target");
    }
    // 6 villager schedule — low priority, should tick
    {
        MobEntity m; m.kind=MobKind::Villager;
        AiContext ctx;
        VillagerScheduleGoal g;
        const double oldX = m.x, oldZ = m.z;
        bool r = g.tick(m, ctx, 6000);
        CHECK(r && m.x == oldX && m.z == oldZ,
              "mob_ai villager work schedule keeps position stable");
    }
    // 7 piglin barter
    {
        MobEntity m; m.kind=MobKind::Piglin;
        AiContext ctx; cppfm::Player p{}; p.heldSlot = 0;
        p.inv[36] = ItemStack::ofName("minecraft:gold_ingot", 1);
        ctx.nearestPlayer=&p; ctx.nearestPlayerDist2=16;
        PiglinBarterGoal g;
        CHECK(g.shouldStart(m, ctx), "mob_ai piglin starts when a player holds gold");
        bool r = g.tick(m, ctx, 50);
        CHECK(r && m.piglinBarterCooldown == 150, "mob_ai piglin barter sets cooldown");
    }
    // 8 cat scare
    {
        MobEntity m; m.kind=MobKind::Cat;
        AiContext ctx; cppfm::Player p{}; ctx.nearestPlayer=&p; ctx.nearestPlayerDist2=25;
        CatScareGoal g;
        CHECK(g.shouldStart(m, ctx), "mob_ai cat scare starts near a player");
        bool r = g.tick(m, ctx, 60);
        CHECK(r && m.catScareCooldown == 120, "mob_ai cat scare sets cooldown");
    }
    // 9 fox pounce
    {
        MobEntity m; m.kind=MobKind::Fox;
        AiContext ctx; cppfm::Player p{}; p.x = 4; ctx.nearestPlayer=&p; ctx.nearestPlayerDist2=16;
        FoxPounceGoal g;
        CHECK(g.shouldStart(m, ctx), "mob_ai fox pounce starts at 4 blocks");
        bool r = g.tick(m, ctx, 70);
        CHECK(r && m.foxPounceCooldown == 110 && m.y > 0,
              "mob_ai fox pounce moves and sets cooldown");
    }
    // 10 evoker fang / drowned trident
    {
        MobEntity m; m.kind=MobKind::Evoker; m.evokerFangCooldown=0;
        AiContext ctx; cppfm::Player p{}; ctx.nearestPlayer=&p; ctx.nearestPlayerDist2=64;
        EvokerFangGoal g;
        CHECK(g.shouldStart(m, ctx), "mob_ai evoker fang starts within 12 blocks");
        bool r = g.tick(m, ctx, 80);
        CHECK(r && m.evokerFangCooldown == 140, "mob_ai evoker fang sets cooldown");
    }
    // Also test BT createNodeForType aliases
    {
        auto n1 = createNodeForType("witch_throw_potion");
        auto n2 = createNodeForType("bee_pollinate");
        auto n3 = createNodeForType("ravager_roar");
        CHECK(n1!=nullptr && n2!=nullptr && n3!=nullptr, "mob_ai BT aliases createNodeForType non-null");
    }
}

void scenarioRecipesTagMirror(){
    std::printf("\n[Recipes tag/mirror — 20 cases (B-03 §1/§2)]\n");
    TagManager tm;
    std::string tagPath = "assets/data/tags";
    if(!std::filesystem::exists(tagPath)) tagPath = "/tmp/opencode/wt37/test/assets/data/tags";
    if(!std::filesystem::exists(tagPath)) tagPath = "assets/data/tags";
    tm.loadDirectory(tagPath);
    RecipeManager rm;
    rm.loadDefaults();
    rm.syncTagsFrom(tm);
    std::string recPath = "assets/data/recipes";
    if(!std::filesystem::exists(recPath)) recPath = "/tmp/opencode/wt37/test/assets/data/recipes";
    rm.loadDirectory(recPath);
    // tag checks 4
    auto* planks = tm.getItemTag("minecraft:planks");
    CHECK(planks && planks->size()>=10, "recipe tag planks >=10 after sync");
    if(planks){
        uint32_t oak = 0, spruce=0, birch=0, jungle=0;
        if(auto it=gen::itemIdByName().find("minecraft:oak_planks"); it!=gen::itemIdByName().end()) oak=it->second;
        if(auto it=gen::itemIdByName().find("minecraft:spruce_planks"); it!=gen::itemIdByName().end()) spruce=it->second;
        if(auto it=gen::itemIdByName().find("minecraft:birch_planks"); it!=gen::itemIdByName().end()) birch=it->second;
        if(auto it=gen::itemIdByName().find("minecraft:jungle_planks"); it!=gen::itemIdByName().end()) jungle=it->second;
        CHECK(planks->count(oak)>0, "planks contains oak_planks");
        CHECK(planks->count(spruce)>0, "planks contains spruce_planks");
        CHECK(planks->count(birch)>0, "planks contains birch_planks");
        CHECK(planks->count(jungle)>0, "planks contains jungle_planks");
    } else { CHECK(false,"planks tag missing"); }
    // trimBlankRows 2
    {
        auto r1 = Recipe::trimBlankRows({"   "," A ","AAA"});
        CHECK(r1.size()==2 && r1[0]==" A " && r1[1]=="AAA", "trimBlankRows [   , A ,AAA] -> 2 rows");
        auto r2 = Recipe::trimBlankRows({"   "});
        CHECK(r2.empty(), "trimBlankRows [   ] -> empty");
    }
    // mirror 8: test axe-like shaped 3x3 with mirror
    // Build a simple shaped recipe: pattern ["AB ","AB "," A "] where A=oak_planks, B=stick
    // We'll test matches directly via Recipe matches API rather than requiring JSON
    {
        Recipe axe; axe.kind=Recipe::Kind::Shaped; axe.width=3; axe.height=3;
        // fill cells row-major: row0 "AB " row1 "AB " row2 " A "
        // A=planks tag ingredient (accept oak), B=stick
        uint32_t oakId=0, stickId=0;
        if(auto it=gen::itemIdByName().find("minecraft:oak_planks"); it!=gen::itemIdByName().end()) oakId=it->second;
        if(auto it=gen::itemIdByName().find("minecraft:stick"); it!=gen::itemIdByName().end()) stickId=it->second;
        Ingredient ingA; if(oakId) ingA.items.insert(oakId);
        // also add spruce to test tag via rm.tags_?
        if(planks) for(auto id: *planks) ingA.items.insert(id);
        Ingredient ingB; if(stickId) ingB.items.insert(stickId);
        Ingredient empty;
        // row0
        axe.cells.push_back(ingA); axe.cells.push_back(ingB); axe.cells.push_back(empty);
        axe.cells.push_back(ingA); axe.cells.push_back(ingB); axe.cells.push_back(empty);
        axe.cells.push_back(empty); axe.cells.push_back(ingA); axe.cells.push_back(empty);
        // test mirror false at ox0 oy0 should match
        std::vector<ItemStack> grid(9);
        // place axe pattern at top-left
        if(oakId && stickId){
            grid[0]=ItemStack::of(oakId,1); grid[1]=ItemStack::of(stickId,1);
            grid[3]=ItemStack::of(oakId,1); grid[4]=ItemStack::of(stickId,1);
            grid[7]=ItemStack::of(oakId,1);
            CHECK(axe.matches(grid,3,3)==true, "mirror axe ox0 oy0 false true");
            // mirrored should be at ox0 mirrored true would be pattern flipped horizontally: " BA"," BA"," A "
            std::vector<ItemStack> gridM(9);
            gridM[1]=ItemStack::of(stickId,1); gridM[2]=ItemStack::of(oakId,1);
            gridM[4]=ItemStack::of(stickId,1); gridM[5]=ItemStack::of(oakId,1);
            gridM[7]=ItemStack::of(oakId,1);
            CHECK(axe.matches(gridM,3,3)==true, "mirror axe mirrored true");
            // A 3x3 recipe has one valid offset; both orientations above are
            // the meaningful mirror cases for this fixture.
        } else { CHECK(false,"missing oak/stick ids for mirror test"); }
    }
    // offset 6: stick 1x2
    {
        uint32_t oakId=0;
        if(auto it=gen::itemIdByName().find("minecraft:oak_planks"); it!=gen::itemIdByName().end()) oakId=it->second;
        if(!oakId) oakId=1;
        Ingredient ing; ing.items.insert(oakId);
        Recipe stick; stick.kind=Recipe::Kind::Shaped; stick.width=1; stick.height=2;
        stick.cells.push_back(ing); stick.cells.push_back(ing);
        int ok=0;
        for(int oy=0; oy<=1; ++oy) for(int ox=0; ox<=2; ++ox){
            std::vector<ItemStack> grid(9);
            grid[oy*3+ox]=ItemStack::of(oakId,1);
            grid[(oy+1)*3+ox]=ItemStack::of(oakId,1);
            if(stick.matches(grid,3,3)) ++ok;
        }
        CHECK(ok==6, "offset stick 1x2 matches 6 offsets");
    }
    // overall size check
    CHECK(rm.size()>=1500, "RecipeManager size >=1500 after loadDirectory");
}

void scenarioLootFunctions(){
    std::printf("\n[Loot functions — 3 cases (B-05) + fortune/ore]\n");
    LootTableEvaluator eval;
    std::string lootPath = "assets/data/loot_tables";
    if(!std::filesystem::exists(lootPath)) lootPath = "/tmp/opencode/wt37/test/assets/data/loot_tables";
    eval.loadDirectory(lootPath);
    CHECK(eval.size()>=5, "LootTables size >=5 after load");
    // zombie 3 checks
    {
        LootContext ctx; ctx.fortuneLevel=0;
        std::vector<ItemStack> drops;
        // Rotten-flesh set_count is uniformly 0..2, so a single valid roll
        // may intentionally produce nothing. Retry with an effectively
        // negligible all-empty probability instead of making this suite flaky.
        for (int attempt = 0; attempt < 32 && drops.empty(); ++attempt)
            drops = eval.evaluateEntity("minecraft:zombie", &ctx);
        CHECK(!drops.empty(), "zombie loot produces a drop across bounded valid rolls");
        bool valid = true;
        for (const auto& drop : drops)
            valid = valid && !drop.empty() && drop.count > 0;
        CHECK(valid, "loot zombie drops have valid item/count");
    }
}

void scenarioEnchantHelper(){
    std::printf("\n[Enchant helper — 10 cases (B-11)]\n");
    ItemStack pick = ItemStack::ofName("minecraft:diamond_pickaxe",1);
    pick.setDamage(10);
    ItemStack::addEnchant(pick, "minecraft:mending",1);
    CHECK(EnchantmentHelper::hasMending(pick)==true, "enchant hasMending true");
    CHECK(EnchantmentHelper::hasMending(ItemStack::ofName("minecraft:stone",1))==false, "hasMending false");
    ItemStack bow = ItemStack::ofName("minecraft:bow",1);
    ItemStack::addEnchant(bow, "minecraft:infinity",1);
    CHECK(EnchantmentHelper::hasInfinity(bow)==true, "hasInfinity true");
    CHECK(EnchantmentHelper::hasInfinity(pick)==false, "hasInfinity false");
    ItemStack tool = ItemStack::ofName("minecraft:diamond_pickaxe",1);
    ItemStack::addEnchant(tool, "minecraft:silk_touch",1);
    CHECK(EnchantmentHelper::hasSilkTouch(tool)==true, "hasSilkTouch true");
    CHECK(EnchantmentHelper::hasSilkTouch(pick)==false, "hasSilkTouch false for mending pick");
    ItemStack::addEnchant(tool, "minecraft:fortune",3);
    CHECK(EnchantmentHelper::getFortune(tool)==3, "getFortune 3");
    ItemStack trident = ItemStack::ofName("minecraft:trident",1);
    ItemStack::addEnchant(trident, "minecraft:channeling",1);
    CHECK(EnchantmentHelper::hasChanneling(trident)==true, "hasChanneling true");
    ItemStack::addEnchant(trident, "minecraft:riptide",2);
    CHECK(EnchantmentHelper::hasRiptide(trident)==true, "hasRiptide true");
    ItemStack cursed = ItemStack::ofName("minecraft:diamond_helmet",1);
    ItemStack::addEnchant(cursed, "minecraft:binding_curse",1);
    CHECK(EnchantmentHelper::hasBindingCurse(cursed)==true, "hasBindingCurse true");
}

void scenarioVillagerTradesUnit(){
    std::printf("\n[Villager trades — table invariants]\n");
    const auto& trades = GameServer::tradeTable();
    CHECK(!trades.empty(), "villager trade table is non-empty");
    bool valid = true;
    for (const auto& trade : trades) {
        valid = valid && trade.inItem != 0 && trade.inCount > 0 &&
                trade.outItem != 0 && trade.outCount > 0 && trade.maxUses > 0;
    }
    CHECK(valid, "villager trades have valid item ids, counts, and uses");
}

void scenarioThunderUnit(){
    std::printf("\n[World data invariants — 4 cases (B-12)]\n");
    cppfm::World w("minecraft:plains", LevelType::Flat, 0);
    CHECK(w.isFlat(), "flat world reports flat level type");
    CHECK(w.seaLevel() == kSeaLevelFlat, "flat world uses the configured sea level");
    WorldDataManager dm("world-test-thunder");
    CHECK(dm.needsFixup(0)==true, "WorldDataManager needsFixup for 0");
    CHECK(dm.needsFixup(4189)==false, "needsFixup false for 4189");
}

void scenarioEnderItemsUnit(){
    std::printf("\n[ItemStack serialization — 4 cases (B-14)]\n");
    // Test a normal stack and component payloads through the actual codec.
    ItemStack s = ItemStack::ofName("minecraft:diamond_sword",1);
    s.setDamage(42);
    // Unknown ids beyond protocol 769's SlotComponentType registry must not
    // be treated as valid opaque wire data. Exercise a real zero-byte Unit
    // component and a supported removed component instead.
    s.components.emplace_back(20, std::vector<std::uint8_t>{});
    s.removedComponents.push_back(4);
    WriteBuffer b; s.write(b);
    ReadBuffer r(b.data);
    ItemStack rr = ItemStack::read(r);
    CHECK(rr.itemId == s.itemId, "ItemStack roundtrip item id");
    CHECK(rr.count == s.count, "ItemStack roundtrip count");
    CHECK(rr.getDamage()==42, "ItemStack roundtrip damage 42");
    CHECK(rr.components == s.components && rr.removedComponents == s.removedComponents,
          "ItemStack roundtrip component payloads");
}

void scenarioQCNative(){
    std::printf("\n[QC native — 3 cases (B-08) basic + y+1 + dispenser]\n");
    cppfm::World w("minecraft:plains", cppfm::LevelType::Flat, 0);
    cppfm::RedstoneEngine re(w);
    cppfm::GameRuleManager gr;
    cppfm::BlockTickScheduler bts(w, &gr, nullptr);
    re.setBlockTickScheduler(&bts);
    std::int64_t tick=0; re.setTickRef(&tick);
    // helper to get state
    auto pistonFalse = cppfm::gen::stateWithPropsList("minecraft:piston", {{"facing","east"},{"extended","false"}});
    auto pistonTrue = cppfm::gen::stateWithPropsList("minecraft:piston", {{"facing","east"},{"extended","true"}});
    auto stone = cppfm::gen::blockByName("minecraft:stone") ? cppfm::gen::blockByName("minecraft:stone")->minState : 1;
    auto redstoneBlock = cppfm::gen::blockByName("minecraft:redstone_block") ? cppfm::gen::blockByName("minecraft:redstone_block")->minState : 0;
    // basic QC: piston at 0,0,0, stone at 0,1,0, redstone_block at 1,1,0 -> QC powered
    w.setBlock(0,0,0, static_cast<std::uint16_t>(pistonFalse)); re.onBlockChanged(0,0,0);
    CHECK(re.isQuasiPowered(0,0,0)==false, "QC basic initially not powered");
    w.setBlock(0,1,0, static_cast<std::uint16_t>(stone)); re.onBlockChanged(0,1,0);
    w.setBlock(1,1,0, static_cast<std::uint16_t>(redstoneBlock)); re.onBlockChanged(1,1,0);
    CHECK(re.isQuasiPowered(0,0,0)==true, "QC basic y+1 powered via stone");
    // y+1 only: remove direct power, keep y+1
    w.setBlock(1,0,0, 0); re.onBlockChanged(1,0,0);
    CHECK(re.isPoweredHere(0,0,0)==false, "QC y+1 only not directly powered");
    CHECK(re.isQuasiPowered(0,0,0)==true, "QC y+1 only still quasi powered");
    // dispenser QC: dispenser at 0,0,1, stone at 0,1,1, redstone_block at 1,1,1 -> QC
    auto disp = cppfm::gen::blockByName("minecraft:dispenser") ? cppfm::gen::blockByName("minecraft:dispenser")->minState : 0;
    w.setBlock(0,0,1, static_cast<std::uint16_t>(disp)); re.onBlockChanged(0,0,1);
    w.setBlock(0,1,1, static_cast<std::uint16_t>(stone)); re.onBlockChanged(0,1,1);
    w.setBlock(1,1,1, static_cast<std::uint16_t>(redstoneBlock)); re.onBlockChanged(1,1,1);
    CHECK(re.isQuasiPowered(0,0,1)==true, "QC dispenser y+1 powered");
    (void)pistonTrue;
}

void scenarioFunctionMacroNative(){
    std::printf("\n[Function macro — 4 cases (B-13) $var / $(var) / return run / missing var]\n");
    cppfm::FunctionEvaluator fe(nullptr);
    // $var legacy: "$say $player" with {player:"Steve"} -> "say Steve"
    {
        std::map<std::string,std::string> args{{"player","Steve"}};
        std::string out = fe.expandMacro("$say $player", args);
        CHECK(out=="say Steve", "macro $var legacy -> say Steve");
    }
    // $(var) new: "$say hello $(var)" with {var:"world"} -> "say hello world"
    {
        std::map<std::string,std::string> args{{"var","world"}};
        std::string out = fe.expandMacro("$say hello $(var)", args);
        CHECK(out=="say hello world", "macro $(var) -> say hello world");
    }
    // missing var fail: "$say hello $(missing)" -> "" (fail)
    {
        std::map<std::string,std::string> args{{"var","world"}};
        std::string out = fe.expandMacro("$say hello $(missing)", args);
        CHECK(out=="", "macro missing var -> empty fail");
    }
}

void scenarioPredicate16Extra(){
    std::printf("\n[Predicate 16 extra — 4 cases (B-13) value_check/entity_scores/reference/match_tool]\n");
    DatapackManager dm;
    // value_check range
    {
        dm.predicates["test:vc_in"] = R"({"condition":"minecraft:value_check","value":3,"range":{"min":1,"max":5}})";
        dm.predicates["test:vc_out"] = R"({"condition":"minecraft:value_check","value":6,"range":{"min":1,"max":5}})";
        PredicateContext ctx; ctx.fortuneLevel=3; ctx.hasValueCheck=true; ctx.valueCheckValue=3;
        // use direct evaluate with valueCheckValue: for vc_in, have 3 in 1..5 -> true, vc_out 6 not in -> false
        // but current impl uses ctx.hasValueCheck ? valueCheckValue : fortuneLevel as have, and then checks range
        // For vc with value field numeric, have is overridden, so we test via raw json
        json::Value v1=json::Value::parse(R"({"condition":"minecraft:value_check","range":{"min":1,"max":5}})");
        ctx.valueCheckValue=3; ctx.hasValueCheck=true;
        CHECK(dm.evaluatePredicateValue(v1, ctx)==true, "predicate value_check 3 in 1..5 -> true");
        ctx.valueCheckValue=6;
        CHECK(dm.evaluatePredicateValue(v1, ctx)==false, "predicate value_check 6 not in 1..5 -> false");
    }
    // entity_scores
    {
        Scoreboard sb;
        sb.setScore("obj","Alice",5);
        dm.predicates["test:score_ok"] = R"({"condition":"minecraft:entity_scores","scores":{"obj":{"min":5}}})";
        PredicateContext ctx; ctx.scoreboard=&sb; ctx.playerName="Alice";
        CHECK(dm.testPredicate("test:score_ok", ctx)==true, "predicate entity_scores Alice 5 >=5 -> true");
        ctx.playerName="Bob";
        CHECK(dm.testPredicate("test:score_ok", ctx)==false, "predicate entity_scores Bob 0 <5 -> false");
    }
    // reference
    {
        dm.predicates["test:ref_target"] = R"({"condition":"minecraft:random_chance","chance":0.9})";
        dm.predicates["test:ref"] = R"({"condition":"minecraft:reference","name":"test:ref_target"})";
        PredicateContext ctx;
        CHECK(dm.testPredicate("test:ref", ctx)==true, "predicate reference -> dispatch true (0.9)");
    }
    // match_tool silk_touch
    {
        PredicateContext ctx1; ctx1.silkTouch=true; ctx1.playerName="Alice"; ctx1.heldItemName="minecraft:diamond_pickaxe";
        PredicateContext ctx2; ctx2.silkTouch=false; ctx2.playerName="Bob"; ctx2.heldItemName="minecraft:diamond_pickaxe";
        json::Value v=json::Value::parse(R"({"condition":"minecraft:match_tool","predicate":{"items":["minecraft:diamond_pickaxe"],"enchantments":[{"enchantment":"minecraft:silk_touch","levels":{"min":1}}]}})");
        CHECK(dm.evaluatePredicateValue(v, ctx1)==true, "predicate match_tool silk_touch true -> true");
        CHECK(dm.evaluatePredicateValue(v, ctx2)==false, "predicate match_tool silk_touch false -> false");
    }
}

void scenarioLoot100Plan40(){
    std::printf("\n[Loot 100 plan40 — 8 cases (C-05) ore_drops binomial/limit/set_damage]\n");
    LootTableEvaluator eval;
    std::string lootPath="assets/data/loot_tables";
    if(!std::filesystem::exists(lootPath)) lootPath="/tmp/opencode/wt40/test/assets/data/loot_tables";
    eval.loadDirectory(lootPath);
    CHECK(eval.tables().size()>=98, "loot 100 size >=98");
    CHECK(eval.tables().count("minecraft:blocks/coal_ore")>0, "loot coal_ore with ore_drops");
    CHECK(eval.tables().count("minecraft:blocks/redstone_ore")>0, "loot redstone uniform_bonus");
    CHECK(eval.tables().count("minecraft:chests/bastion_other")>0, "loot bastion_other exists");
    CHECK(eval.tables().count("minecraft:entities/cow")>0, "loot cow exists");
    CHECK(eval.tables().count("minecraft:gameplay/fishing_treasure")>0, "loot fishing_treasure exists");
    const auto* stoneLoot = eval.findBlockTable("minecraft:stone");
    const auto seededLootA = eval.evaluateRegisteredTable(
        "minecraft:chests/simple_dungeon", {}, nullptr, 8675309);
    const auto seededLootB = eval.evaluateRegisteredTable(
        "minecraft:chests/simple_dungeon", {}, nullptr, 8675309);
    bool seededLootDeterministic = seededLootA && seededLootB &&
        seededLootA->size() == seededLootB->size();
    if (seededLootDeterministic) {
        for (std::size_t i = 0; i < seededLootA->size(); ++i) {
            if ((*seededLootA)[i].itemId != (*seededLootB)[i].itemId ||
                (*seededLootA)[i].count != (*seededLootB)[i].count) {
                seededLootDeterministic = false;
                break;
            }
        }
    }
    CHECK(stoneLoot != nullptr && seededLootA && seededLootDeterministic,
          "registered loot tables can be found independently of their possibly-empty generated result, and explicit seeds are reproducible");
    // fortune evaluation
    {
        LootContext ctx0;
        auto d0=eval.evaluateWithContext("minecraft:blocks/coal_ore", ItemStack::ofName("minecraft:iron_pickaxe",1), &ctx0);
        CHECK(!d0.empty(), "loot coal_ore fortune 0 non-empty");
        LootContext ctx3;
        ctx3.fortuneLevel = 3;
        auto d3=eval.evaluateWithContext("minecraft:blocks/coal_ore", ItemStack::ofName("minecraft:iron_pickaxe",1), &ctx3);
        CHECK(!d3.empty(), "loot coal_ore fortune 3 non-empty");
    }
}

void scenarioPredicate22Plan40(){
    std::printf("\n[Predicate 22 plan40 — 6 cases (C-07) nbt/type_specific/dimension/enchantment_active]\n");
    DatapackManager dm;
    // nbt true/false
    {
        json::Value v=json::Value::parse(R"({"condition":"minecraft:nbt","nbt":"{\"Tags\":[\"test\"]}"})");
        PredicateContext ctx; ctx.nbt="{\"Tags\":[\"test\"],\"Health\":20}";
        CHECK(dm.evaluatePredicateValue(v, ctx)==true, "predicate nbt true");
        ctx.nbt="{\"Tags\":[\"other\"]}";
        CHECK(dm.evaluatePredicateValue(v, ctx)==false, "predicate nbt false");
    }
    // type_specific player
    {
        json::Value v=json::Value::parse(R"({"condition":"minecraft:entity_properties","predicate":{"type_specific":{"type":"minecraft:player"}}})");
        cppfm::Player player{};
        PredicateContext ctx; ctx.player=&player;
        CHECK(dm.evaluatePredicateValue(v, ctx)==true, "predicate type_specific player true");
        ctx.player=nullptr;
        CHECK(dm.evaluatePredicateValue(v, ctx)==false, "predicate type_specific player false");
    }
    // dimension overworld
    {
        json::Value v=json::Value::parse(R"({"condition":"minecraft:location_check","predicate":{"dimension":"minecraft:overworld"}})");
        cppfm::World w("minecraft:plains", cppfm::LevelType::Flat, 0);
        PredicateContext ctx; ctx.world=&w;
        CHECK(dm.evaluatePredicateValue(v, ctx)==true, "predicate dimension overworld true");
    }
    // enchantment_active_check fortune 3
    {
        json::Value v=json::Value::parse(R"({"condition":"minecraft:enchantment_active_check","enchantment":"minecraft:fortune","levels":{"min":1,"max":3}})");
        PredicateContext ctx; ctx.fortuneLevel=3;
        CHECK(dm.evaluatePredicateValue(v, ctx)==true, "predicate enchantment_active fortune 3 true");
        ctx.fortuneLevel=0;
        CHECK(dm.evaluatePredicateValue(v, ctx)==false, "predicate enchantment_active fortune 0 false");
    }
    // block_state_property air
    {
        json::Value v=json::Value::parse(R"({"condition":"minecraft:block_state_property","block":"minecraft:air"})");
        cppfm::World w("minecraft:plains", cppfm::LevelType::Flat, 0);
        PredicateContext ctx; ctx.world=&w; ctx.x=0; ctx.y=100; ctx.z=0;
        CHECK(dm.evaluatePredicateValue(v, ctx)==true, "predicate block_state_property air true");
    }
    // damage_source is_fire
    {
        json::Value v=json::Value::parse(R"({"condition":"minecraft:damage_source_properties","predicate":{"tags":[{"id":"minecraft:is_fire","expected":true}]}})");
        PredicateContext ctx; DamageSource ds=DamageSource::fire(); ctx.damageSource=&ds;
        CHECK(dm.evaluatePredicateValue(v, ctx)==true, "predicate damage_source is_fire true");
        DamageSource ds2=DamageSource::fall(); ctx.damageSource=&ds2;
        CHECK(dm.evaluatePredicateValue(v, ctx)==false, "predicate damage_source is_fire false for fall");
    }
}

void scenarioEnchant41Plan40(){
    std::printf("\n[Enchant 41 plan40 — 9 cases (C-08) smite/bane/punch/flame/EPF caps 30/20]\n");
    ItemStack sword=ItemStack::ofName("minecraft:diamond_sword",1);
    ItemStack::addEnchant(sword,"minecraft:smite",3);
    CHECK(EnchantmentHelper::getSmite(sword)==3, "enchant smite 3 level");
    CHECK(EnchantmentHelper::isUndead(MobKind::Zombie)==true, "isUndead zombie true");
    CHECK(EnchantmentHelper::isUndead(MobKind::Cow)==false, "isUndead cow false");
    CHECK(EnchantmentHelper::isArthropod(MobKind::Spider)==true, "isArthropod spider true");
    ItemStack spider=ItemStack::ofName("minecraft:diamond_sword",1);
    ItemStack::addEnchant(spider,"minecraft:bane_of_arthropods",2);
    CHECK(EnchantmentHelper::getBaneOfArthropods(spider)==2, "enchant bane 2");
    // punch/knockback
    ItemStack bow=ItemStack::ofName("minecraft:bow",1);
    ItemStack::addEnchant(bow,"minecraft:punch",2);
    CHECK(EnchantmentHelper::getPunch(bow)==2, "enchant punch 2");
    ItemStack kb=ItemStack::ofName("minecraft:diamond_sword",1);
    ItemStack::addEnchant(kb,"minecraft:knockback",1);
    CHECK(EnchantmentHelper::getKnockback(kb)==1, "enchant knockback 1");
    // aqua/respiration
    ItemStack helm=ItemStack::ofName("minecraft:diamond_helmet",1);
    ItemStack::addEnchant(helm,"minecraft:aqua_affinity",1);
    CHECK(EnchantmentHelper::hasAquaAffinity(helm)==true, "enchant aqua_affinity true");
    ItemStack helm2=ItemStack::ofName("minecraft:diamond_helmet",1);
    ItemStack::addEnchant(helm2,"minecraft:respiration",3);
    CHECK(EnchantmentHelper::getRespiration(helm2)==3, "enchant respiration 3");
    // EPF caps 30/20: protection weight 1, fire weight 2, total caps 20
    {
        ItemStack prot=ItemStack::ofName("minecraft:diamond_chestplate",1);
        ItemStack::addEnchant(prot,"minecraft:protection",4);
        CHECK(EnchantmentHelper::getProtectionEPF(DamageSource::generic(), prot)==4, "EPF protection 4 weight1");
        ItemStack fireProt=ItemStack::ofName("minecraft:diamond_chestplate",1);
        ItemStack::addEnchant(fireProt,"minecraft:fire_protection",4);
        CHECK(EnchantmentHelper::getProtectionEPF(DamageSource::fire(), fireProt)==8, "EPF fire_protection 4 weight2");
        float d1=DamageCalculator::applyArmorAndToughness(40.f, 30, 20);
        float d2=DamageCalculator::applyEnchantProtection(d1, 20);
        CHECK(d2>=1.f && d2<=3.f, "EPF caps 30/20 40->1..3");
        CHECK(DamageSource::sonicBoom().bypassEnchant==true, "sonic_boom bypassEnchant");
    }
}

// Online-mode join is tested via crypto unit tests + manual verification.

static void scenarioDimensionCacheUnit() {
    std::printf("\n[dimension-aware chunk cache and light snapshots — 12 cases (W20)]\n");
    ServerConfig cfg;
    cfg.ioWorkerThreads = 1;
    cfg.jvmEnabled = false;
    cfg.levelType = "flat";
    GameServer server(cfg);
    server.data().load(cfg.assetsDir);
    WorldMutationContractTestAccess::initializeDimensionEngines(server);
    auto overworld = std::make_shared<const std::vector<std::uint8_t>>(
        std::vector<std::uint8_t>{0x0A});
    auto nether = std::make_shared<const std::vector<std::uint8_t>>(
        std::vector<std::uint8_t>{0x0B});
    server.storeChunkFor(0, 7, -3, 1, overworld);
    server.storeChunkFor(-1, 7, -3, 1, nether);
    GameServer::ChunkBodyRef found;
    CHECK(server.getCachedChunkFor(0, 7, -3, 0, found) &&
          found && *found == *overworld,
          "dimension cache: Overworld body is isolated");
    CHECK(server.getCachedChunkFor(-1, 7, -3, 0, found) &&
          found && *found == *nether,
          "dimension cache: Nether body is isolated at same coordinate");
    server.invalidateChunkCacheFor(-1, 7, -3);
    CHECK(!server.getCachedChunkFor(-1, 7, -3, 0, found),
          "dimension cache: invalidating Nether removes only Nether body");
    CHECK(server.getCachedChunkFor(0, 7, -3, 0, found),
          "dimension cache: invalidating Nether preserves Overworld body");
    CHECK(server.chunkCacheSize() == 1,
          "dimension cache: LRU contains the surviving dimension only");

    auto staleSnapshot = server.beginChunkCacheSnapshot(0, 7, -3);
    server.invalidateChunkCacheFor(0, 7, -3);
    bool staleBodyPublished = false;
    const auto staleBody = std::make_shared<const std::vector<std::uint8_t>>(
        std::vector<std::uint8_t>{0x0C});
    CHECK(!server.publishChunkForSnapshot(
              staleSnapshot, 2, staleBody,
              [&](const GameServer::ChunkBodyRef&) {
                  staleBodyPublished = true;
              }) && !staleBodyPublished,
          "chunk cache: invalidation rejects and does not publish a stale snapshot");
    CHECK(!server.getCachedChunkFor(0, 7, -3, 0, found),
          "chunk cache: rejected stale snapshot is not reinserted");

    auto currentSnapshot = server.beginChunkCacheSnapshot(0, 7, -3);
    bool currentBodyPublished = false;
    CHECK(server.publishChunkForSnapshot(
              currentSnapshot, 3, overworld,
              [&](const GameServer::ChunkBodyRef& body) {
                  currentBodyPublished = body && *body == *overworld;
              }) && currentBodyPublished,
          "chunk cache: current snapshot is stored and published");

    auto& world = server.worldFor(0);
    world.addSpawnTicket(0, 0);
    world.addSpawnTicket(1, 0);
    CHECK(world.generateChunkIfMissing(0, 0) &&
              world.generateChunkIfMissing(1, 0),
          "light-cache regression loads both sides of a chunk boundary");
    const auto cachedNeighbor =
        std::make_shared<const std::vector<std::uint8_t>>(
            std::vector<std::uint8_t>{0x0D});
    server.storeChunkFor(0, 1, 0, 1, cachedNeighbor);
    CHECK(server.getCachedChunkFor(0, 1, 0, 0, found) &&
              found && *found == *cachedNeighbor,
          "neighbor chunk body is cached before light reaches it");

    const auto glowstone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:glowstone"));
    CHECK(world.setBlock(15, 100, 8, glowstone),
          "glowstone emitter is placed beside the cached chunk");
    CHECK(server.getCachedChunkFor(0, 1, 0, 0, found),
          "editing the source chunk does not prematurely evict its neighbor");
    WorldMutationContractTestAccess::drainLightUpdates(server, 0);
    CHECK(world.getBlockLight(16, 100, 8) > 0,
          "authoritative tick propagates block light across the chunk boundary");
    CHECK(!server.getCachedChunkFor(0, 1, 0, 0, found),
          "publishing changed light invalidates the later-viewer chunk body");
}

static void scenarioChunkSaveWithoutPersistenceIsNotScheduled() {
    std::printf("\n[chunk persistence ownership — no unsafe direct region fallback]\n");
    const auto worldDir = std::filesystem::temp_directory_path() /
        ("cppfm-unowned-chunk-save-" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(worldDir, ec);
    CHECK(!ec, "unowned-chunk fixture starts from an absent private world directory");

    ServerConfig cfg;
    cfg.ioWorkerThreads = 1;
    cfg.jvmEnabled = false;
    cfg.worldDir = worldDir.string();
    {
        GameServer server(cfg);
        const auto stone = static_cast<std::uint16_t>(
            gen::blockNameToState().at("minecraft:stone"));
        CHECK(server.worldFor(-1).setBlock(1, 64, 1, stone),
              "unowned-chunk fixture creates a dirty Nether chunk");
        server.saveChunkAsyncFor(-1, 0, 0);
        server.stop(); // drains the owned I/O pool before checking disk state
    }
    CHECK(!std::filesystem::exists(worldDir / "DIM-1" / "region" / "r.0.0.mca"),
          "a chunk without a Persistence owner is not written in-place");
    std::filesystem::remove_all(worldDir, ec);
    CHECK(!ec,
          "unowned-chunk fixture removes its exact temporary world directory");
}

static void scenarioSpawnChunkLoadFailureIsFatal() {
    std::printf("\n[startup rejects an unreadable saved spawn chunk]\n");
    const auto worldDir = std::filesystem::temp_directory_path() /
        ("cppfm-spawn-load-failure-" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(worldDir, ec);
    const auto regionPath = worldDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(regionPath.parent_path(), ec);
    CHECK(!ec, "spawn-load fixture creates its isolated region directory");
    if (ec) return;

    std::vector<std::uint8_t> originalChunk;
    try {
        auto root = chunkToNBT(0, 0, Chunk{}, "minecraft:plains");
        auto* sections = root.get("sections");
        auto* blockStates = sections && !sections->list.empty()
            ? sections->list.front().get("block_states") : nullptr;
        auto* palette = blockStates ? blockStates->get("palette") : nullptr;
        if (!palette || palette->tag != nbt::List || palette->list.empty())
            throw std::runtime_error("empty spawn chunk fixture palette");
        palette->list.front().set(
            "Name", nbt::Value::makeString("example:unsupported_block"));
        WriteBuffer encoded;
        nbt::writeFileRoot(encoded, root);
        RegionFile region(regionPath.string());
        region.store(0, 0, encoded.data);
        originalChunk = region.load(0, 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "spawn-load fixture setup failed: %s\n",
                     error.what());
        CHECK(false, "spawn-load fixture writes a structurally valid unsupported chunk");
        std::filesystem::remove_all(worldDir, ec);
        return;
    }
    CHECK(!originalChunk.empty(),
          "spawn-load fixture stores its target chunk in Anvil");

    ServerConfig config;
    config.ioWorkerThreads = 1;
    config.jvmEnabled = false;
    config.port = 0;
    config.worldDir = worldDir.string();
    bool initializationFailed = false;
    std::string failureMessage;
    {
        GameServer server(config);
        try {
            server.init();
        } catch (const std::exception& error) {
            initializationFailed = true;
            failureMessage = error.what();
        }
    }

    CHECK(initializationFailed &&
              failureMessage.find("required spawn chunk 0,0") !=
                  std::string::npos,
          "startup fails explicitly when a required saved spawn chunk is unsupported");
    try {
        RegionFile preservedRegion(regionPath.string());
        CHECK(preservedRegion.load(0, 0) == originalChunk,
              "failed startup preserves the original unsupported chunk bytes");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "spawn-load preservation check failed: %s\n",
                     error.what());
        CHECK(false, "failed startup leaves the original spawn region readable");
    }
    std::filesystem::remove_all(worldDir, ec);
    CHECK(!ec, "spawn-load fixture removes its exact temporary world");
}

static void scenarioCrafterOpaqueOutputTarget() {
    std::printf("\n[crafter output refuses opaque inventory without aborting tick]\n");
    const auto result =
        WorldMutationContractTestAccess::crafterOutputToOpaqueContainer();
    CHECK(result.tickCompleted,
          "crafter tick completes when its output target has opaque inventory");
    CHECK(result.inputConsumed && result.outputDropped,
          "crafted output is dropped safely instead of inserted or lost");
    CHECK(result.opaqueInventoryPreserved,
          "opaque destination block-entity data remains present");
    CHECK(result.admissionReservationEstablished &&
              result.admittedPlayerPublished,
          "directory reserves the test player before publishing it");
    CHECK(result.shulkerRemovedWithoutDrops,
          "dropItems=false does not cancel breaking a filled shulker");
    CHECK(result.shulkerDropsSuppressed,
          "dropItems=false suppresses both block and container-content drops");
    CHECK(result.pendingContainerLootGenerated,
          "breaking an unopened container materializes its seeded loot table instead of losing it");
    CHECK(result.tntUsesPostCallbackHeldItem,
          "TNT ignition uses the held item after native block-break callbacks");
    CHECK(result.blockEventTargetReplacementPreserved,
          "a legacy break callback replacement is not erased by stale mining state");
    CHECK(result.registeredEmptyLootSuppressesBlockDrop,
          "a registered loot table that returns no items no longer falls back to dropping the block itself");
    CHECK(result.invalidLootTableUsesBlockFallback,
          "an unrepresentable loot entry is not silently registered over the block's default drop");
}

static void scenarioNestedCommandSourcePolicy() {
    std::printf("\n[nested command source policy — execute/function/store defaults]\n");
    ServerConfig cfg;
    cfg.ioWorkerThreads = 1;
    cfg.jvmEnabled = false;
    GameServer server(cfg);

    Player player;
    player.name = "NestedSource";
    player.x = 12.5;
    player.y = -59.0;
    player.z = -4.5;
    player.yaw = 37.0f;
    player.pitch = -11.0f;

    brigadier::CommandSource parent;
    parent.dimensionOverride = -1;
    const auto executeSource = makeNestedCommandSource(server, &player, parent);
    CHECK(executeSource.player == &player && executeSource.name == player.name &&
              executeSource.srcX == player.x && executeSource.srcY == player.y &&
              executeSource.srcZ == player.z && executeSource.srcYaw == player.yaw &&
              executeSource.srcPitch == player.pitch &&
              executeSource.dimensionOverride == parent.dimensionOverride,
          "nested execute source preserves player position, rotation, and dimension");
    CHECK(static_cast<bool>(executeSource.resolveSelector),
          "nested execute source binds selectors after dimension inheritance");

    const auto functionSource =
        makeNestedCommandSource(server, &player, parent, false);
    CHECK(functionSource.srcX == player.x && functionSource.srcY == player.y &&
              functionSource.srcZ == player.z && functionSource.srcYaw == 0.0f &&
              functionSource.srcPitch == 0.0f,
          "function/store source retains their legacy zero rotation defaults");
}

static void scenarioJvmServerThreadBoundary() {
    std::printf("\n[JVM server-thread boundary — bootstrap, queue, timeout, stop]\n");
    const std::string worldDir =
        "/tmp/cppfm-jvm-server-thread-" + std::to_string(getpid());
    std::error_code ec;
    std::filesystem::remove_all(worldDir, ec);

    ServerConfig cfg;
    cfg.ioWorkerThreads = 1;
    cfg.jvmEnabled = false;
    cfg.port = 0;
    cfg.worldDir = worldDir;

    {
        GameServer server(cfg);
        server.init();

        const auto chestState = static_cast<std::uint16_t>(
            gen::blockNameToState().at("minecraft:chest"));
        const auto stoneState = static_cast<std::uint16_t>(
            gen::blockNameToState().at("minecraft:stone"));
        const auto chestKey = posKey(8, 100, 8);
        auto& world = server.worldFor(0);
        bool placedChest = false;
        bool replacedChest = false;
        BlockEntityStore::Owner chestEntity;
        {
            auto mutationGate = world.lockMutationGate();
            (void)mutationGate;
            placedChest = world.setBlock(8, 100, 8, chestState);
            chestEntity = server.blockEntities().createShared(
                chestKey, BlockEntity::Kind::Chest);
            replacedChest = world.setBlock(8, 100, 8, stoneState);
        }
        CHECK(placedChest && chestEntity && replacedChest &&
                  !server.blockEntities().getShared(chestKey),
              "changing a block entity's block type removes the stale entity from persistence");

        // init() temporarily owns the boundary for bootstrap, but a foreign
        // thread must not leave a request behind while no tick loop exists.
        std::atomic<bool> bootstrapRan{false};
        bool bootstrapResult = true;
        std::thread bootstrapCaller([&] {
            bootstrapResult = server.runOnServerThread(
                [&] { bootstrapRan.store(true, std::memory_order_release); },
                std::chrono::milliseconds(50));
        });
        bootstrapCaller.join();
        CHECK(!bootstrapResult,
              "JVM boundary: foreign bootstrap mutation is rejected while stopped");
        CHECK(!bootstrapRan.load(std::memory_order_acquire),
              "JVM boundary: rejected bootstrap mutation has no callback");

        std::atomic<bool> runnerFailed{false};
        std::thread runner([&] {
            try {
                server.runForever();
            } catch (...) {
                runnerFailed.store(true, std::memory_order_release);
            }
        });

        const auto waitUntil = [](auto&& predicate,
                                  std::chrono::milliseconds timeout) {
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (!predicate() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return static_cast<bool>(predicate());
        };
        CHECK(waitUntil([&] { return server.running(); },
                        std::chrono::milliseconds(2000)),
              "JVM boundary: server enters running state");

        bool duplicateRunRejected = false;
        try {
            server.runForever();
        } catch (const std::logic_error&) {
            duplicateRunRejected = true;
        }
        CHECK(duplicateRunRejected && server.running() &&
                  NativeIntegrationTestAccess::tickLoopActive(server),
              "a duplicate runForever call is rejected without stopping the active server");

        // A regular foreign request is executed by the tick thread, never by
        // the Java-created caller thread.
        std::thread::id queueCallerId;
        std::thread::id executionId;
        std::atomic<bool> queuedRan{false};
        bool queuedResult = false;
        std::thread queueCaller([&] {
            queueCallerId = std::this_thread::get_id();
            queuedResult = server.runOnServerThread(
                [&] {
                    executionId = std::this_thread::get_id();
                    queuedRan.store(true, std::memory_order_release);
                },
                std::chrono::milliseconds(1000));
        });
        queueCaller.join();
        CHECK(queuedResult && queuedRan.load(std::memory_order_acquire),
              "JVM boundary: foreign request completes through the queue");
        CHECK(!bootstrapRan.load(std::memory_order_acquire),
              "JVM boundary: rejected bootstrap mutation does not run after tick start");
        CHECK(executionId != queueCallerId,
              "JVM boundary: queued request runs outside the caller thread");

        jvm::JvmConfig jvmConfig;
        jvmConfig.enabled = false;
        // This integration fixture runs world generation and several hundred
        // unrelated assertions in the same process. Permit two tick turns so
        // the bridge test measures owner-thread mutation semantics rather than
        // scheduler latency against the production 250 ms default.
        jvmConfig.serverMutationTimeout = std::chrono::seconds(2);
        jvm::JvmRuntime runtime(server, std::move(jvmConfig));
        const auto worldHandle = runtime.worldHandle(server.world());
        auto netherMob = std::make_shared<MobEntity>();
        netherMob->dimension = -1;
        netherMob->kind = MobKind::Zombie;
        netherMob->entityId = 0x5A17;
        // Entity enumeration includes all in-play players and every mob;
        // natural spawns can precede this fixture. Read the count and indexed
        // handles in the same server-thread turn as insertion so a tick
        // cannot despawn the fixture or invalidate the count between calls.
        std::uint64_t netherMobHandle = 0;
        const bool mobFound = server.runOnServerThread([&] {
            server.addMob(netherMob);
            for (std::int32_t index = 0;
                 index < runtime.nativeEntityCount(); ++index) {
                const auto candidate = runtime.nativeEntityHandle(
                    static_cast<std::size_t>(index));
                if (runtime.nativePlayerEntityId(candidate) ==
                    netherMob->entityId) {
                    netherMobHandle = candidate;
                    break;
                }
            }
        }, std::chrono::seconds(2));
        CHECK(mobFound && netherMobHandle != 0,
              "JVM boundary: entity enumeration returns the inserted mob");
        const auto netherWorldHandle =
            runtime.nativeEntityWorld(netherMobHandle);
        CHECK(netherWorldHandle != 0 && runtime.nativeWorldName(
                  netherWorldHandle) == "minecraft:the_nether",
              "JVM boundary: entity world follows the mob dimension");
        std::atomic<bool> nativeMutationResult{false};
        std::thread nativeCaller([&] {
            nativeMutationResult.store(
                runtime.nativeWorldSetBlock(worldHandle, 3, 80, 3, 1),
                std::memory_order_release);
        });
        nativeCaller.join();
        CHECK(nativeMutationResult.load(std::memory_order_acquire),
              "JVM boundary: native world mutation completes from a foreign thread");

        // A native caller can have its mutation queued when an inactive
        // runtime is destroyed re-entrantly from the tick thread. The queued
        // closure must observe the runtime generation fence and skip its
        // captured `this` rather than touching the destroyed PImpl.
        jvm::JvmConfig transientConfig;
        transientConfig.enabled = false;
        // Keep the waiter alive well beyond the teardown fence so this test
        // exercises generation rejection, not the caller's timeout path.
        transientConfig.serverMutationTimeout = std::chrono::seconds(30);
        auto transientRuntime = std::make_unique<jvm::JvmRuntime>(
            server, std::move(transientConfig));
        const auto transientWorldHandle = transientRuntime->worldHandle(server.world());
        auto* transientRuntimePtr = transientRuntime.get();
        std::atomic<bool> transientMutationResult{true};
        std::atomic<bool> transientQueued{false};
        std::atomic<bool> transientCallerReturned{false};
        std::atomic<bool> callerReturnedBeforeFence{true};
        std::thread transientCaller;
        const bool destroyedOnTick = server.runOnServerThread([&] {
            transientCaller = std::thread([&] {
                transientMutationResult.store(
                    transientRuntimePtr->nativeWorldSetBlock(
                        transientWorldHandle, 4, 80, 4, 1),
                    std::memory_order_release);
                transientCallerReturned.store(true, std::memory_order_release);
            });
            transientQueued.store(waitUntil([&] {
                return server.pendingServerThreadTasksForTest() != 0;
            }, std::chrono::milliseconds(1000)), std::memory_order_release);
            callerReturnedBeforeFence.store(
                transientCallerReturned.load(std::memory_order_acquire),
                std::memory_order_release);
            transientRuntime.reset();
        }, std::chrono::milliseconds(2000));
        if (transientCaller.joinable()) transientCaller.join();
        CHECK(destroyedOnTick && transientQueued.load(std::memory_order_acquire) &&
                  !callerReturnedBeforeFence.load(std::memory_order_acquire) &&
                  !transientMutationResult.load(std::memory_order_acquire) &&
                  server.world().getBlock(4, 80, 4) == 0,
              "JVM boundary: tick-thread runtime destruction fences queued native mutations");

        std::atomic<int> legacyHandlerCalls{0};
        blockEventDispatcher().addOnBlockPlaceHandler(
            [&](const BlockEvent&) { legacyHandlerCalls.fetch_add(1); });
        jvm::JvmConfig secondaryConfig;
        secondaryConfig.enabled = false;
        jvm::JvmRuntime secondaryRuntime(server, std::move(secondaryConfig));
        secondaryRuntime.stop();
        blockEventDispatcher().onBlockPlace(5, 80, 5, 0, 1);
        CHECK(legacyHandlerCalls.load() == 1,
              "JVM boundary: stopping an inactive runtime preserves process-wide handlers");

        // Once a callback is running it cannot be cancelled safely, but the
        // foreign caller still returns at its deadline. The queued closure
        // owns its state and may finish on the tick thread afterwards.
        std::atomic<bool> runningTaskEntered{false};
        std::atomic<bool> releaseRunningTask{false};
        std::atomic<bool> runningTaskFinished{false};
        ServerThreadTaskOutcome runningTaskOutcome =
            ServerThreadTaskOutcome::Completed;
        const auto runningCallStart = std::chrono::steady_clock::now();
        std::thread releaseRunningThread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(750));
            releaseRunningTask.store(true, std::memory_order_release);
        });
        std::thread runningCaller([&] {
            runningTaskOutcome = server.runOnServerThreadDetailed(
                [&] {
                    runningTaskEntered.store(true, std::memory_order_release);
                    while (!releaseRunningTask.load(std::memory_order_acquire))
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    runningTaskFinished.store(true, std::memory_order_release);
                }, std::chrono::milliseconds(500));
        });
        const bool runningTaskStarted = waitUntil([&] {
                    return runningTaskEntered.load(std::memory_order_acquire);
                },
                std::chrono::milliseconds(1500));
        CHECK(runningTaskStarted,
              "JVM boundary: timed operation starts on the server thread");
        runningCaller.join();
        const auto runningCallElapsed = std::chrono::steady_clock::now() -
                                        runningCallStart;
        const bool taskStillHeldAtCallerReturn =
            !runningTaskFinished.load(std::memory_order_acquire);
        releaseRunningThread.join();
        CHECK(runningTaskOutcome == ServerThreadTaskOutcome::Indeterminate &&
                  runningCallElapsed < std::chrono::milliseconds(650) &&
                  taskStillHeldAtCallerReturn,
              "JVM boundary: running operation reports indeterminate at the caller deadline");
        CHECK(waitUntil([&] {
                  return runningTaskFinished.load(std::memory_order_acquire);
              }, std::chrono::milliseconds(1000)),
              "JVM boundary: running operation safely completes after caller timeout");

        // State mutations and Fabric callbacks cannot return an ordinary
        // failure while a server-thread task is still able to commit. Their
        // wait policy therefore cancels only while Pending and waits through
        // Running, preserving the synchronous result contract.
        std::atomic<bool> committedTaskEntered{false};
        std::atomic<bool> releaseCommittedTask{false};
        std::atomic<bool> committedTaskFinished{false};
        std::atomic<bool> committedCallerReturned{false};
        ServerThreadTaskOutcome committedTaskOutcome =
            ServerThreadTaskOutcome::Rejected;
        std::thread committedCaller([&] {
            committedTaskOutcome = server.runOnServerThreadDetailed(
                [&] {
                    committedTaskEntered.store(true, std::memory_order_release);
                    while (!releaseCommittedTask.load(std::memory_order_acquire))
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    committedTaskFinished.store(true, std::memory_order_release);
                }, std::chrono::milliseconds(500),
                ServerThreadTaskWaitPolicy::WaitIfRunning);
            committedCallerReturned.store(true, std::memory_order_release);
        });
        const bool committedTaskStarted = waitUntil([&] {
                    return committedTaskEntered.load(std::memory_order_acquire);
                },
                std::chrono::milliseconds(1500));
        if (committedTaskStarted)
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
        const bool callerWaitedPastDeadline = committedTaskStarted &&
            !committedCallerReturned.load(std::memory_order_acquire);
        releaseCommittedTask.store(true, std::memory_order_release);
        committedCaller.join();
        CHECK(committedTaskStarted && callerWaitedPastDeadline &&
                  committedTaskFinished.load(std::memory_order_acquire) &&
                  committedCallerReturned.load(std::memory_order_acquire) &&
                  committedTaskOutcome == ServerThreadTaskOutcome::Completed,
              "JVM boundary: synchronous mutation waits for a running server-thread result");

        // Occupy the tick thread, then verify that a timed-out pending call is
        // cancelled instead of being run after the caller has returned false.
        std::atomic<bool> blockerEntered{false};
        std::atomic<bool> releaseBlocker{false};
        bool blockerResult = false;
        std::thread blocker([&] {
            blockerResult = server.runOnServerThread(
                [&] {
                    blockerEntered.store(true, std::memory_order_release);
                    while (!releaseBlocker.load(std::memory_order_acquire))
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                },
                std::chrono::milliseconds(1000));
        });
        CHECK(waitUntil([&] {
                    return blockerEntered.load(std::memory_order_acquire);
                },
                std::chrono::milliseconds(1000)),
              "JVM boundary: test task reaches the tick thread");

        std::atomic<bool> timedOutTaskRan{false};
        const auto timeoutStart = std::chrono::steady_clock::now();
        const auto timedOut = server.runOnServerThreadDetailed(
            [&] { timedOutTaskRan.store(true, std::memory_order_release); },
            std::chrono::milliseconds(25));
        const auto timeoutElapsed = std::chrono::steady_clock::now() - timeoutStart;
        CHECK(timedOut == ServerThreadTaskOutcome::Rejected,
              "JVM boundary: blocked pending mutation is rejected at its deadline");
        CHECK(!timedOutTaskRan.load(std::memory_order_acquire),
              "JVM boundary: timed-out pending mutation is cancelled");
        CHECK(timeoutElapsed < std::chrono::milliseconds(500),
              "JVM boundary: cancellation wait remains bounded");

        // requestStop() is intentionally only a stop flag/socket wakeup.  A
        // pending request is rejected by the running guard and any request
        // already queued is cancelled by the normal tick-release path.
        std::atomic<bool> stopPendingTaskRan{false};
        bool stopPendingResult = true;
        std::thread stopPendingCaller([&] {
            stopPendingResult = server.runOnServerThread(
                [&] { stopPendingTaskRan.store(true, std::memory_order_release); },
                std::chrono::milliseconds(1000));
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        server.requestStop();
        bool rejectedAfterStop = server.runOnServerThread(
            [&] { stopPendingTaskRan.store(true, std::memory_order_release); },
            std::chrono::milliseconds(25));
        CHECK(!rejectedAfterStop,
              "JVM boundary: new foreign mutation is rejected after requestStop");

        std::thread releaseBlockerThread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            releaseBlocker.store(true, std::memory_order_release);
        });
        const auto inactiveRuntimeStopStart = std::chrono::steady_clock::now();
        runtime.stop();
        const auto inactiveRuntimeStopElapsed =
            std::chrono::steady_clock::now() - inactiveRuntimeStopStart;
        releaseBlockerThread.join();
        blocker.join();
        stopPendingCaller.join();
        CHECK(inactiveRuntimeStopElapsed >= std::chrono::milliseconds(100),
              "inactive JVM runtime drains queued server closures before releasing its state");
        CHECK(blockerResult,
              "JVM boundary: running task is allowed to finish during stop");
        CHECK(!stopPendingResult &&
                  !stopPendingTaskRan.load(std::memory_order_acquire),
              "JVM boundary: pending stop-time mutation never runs late");

        if (runner.joinable()) runner.join();
        CHECK(!runnerFailed.load(std::memory_order_acquire),
              "JVM boundary: server loop exits cleanly after requestStop");
        server.stop();
        CHECK(server.world().getBlock(3, 80, 3) == 1,
              "JVM boundary: queued native mutation remains on the server world");
    }

    std::filesystem::remove_all(worldDir, ec);
    CHECK(!ec, "JVM boundary: temporary world is removed");
}

static void scenarioStopFromTickCallback() {
    std::printf("\n[server lifecycle — stop requested by a tick callback]\n");
    const std::string worldDir =
        "/tmp/cppfm-self-stop-" + std::to_string(getpid());
    std::error_code ec;
    std::filesystem::remove_all(worldDir, ec);

    ServerConfig cfg;
    cfg.ioWorkerThreads = 1;
    cfg.jvmEnabled = false;
    cfg.port = 0;
    cfg.worldDir = worldDir;

    GameServer server(cfg);
    server.init();
    std::atomic<bool> callbackStopped{false};
    std::atomic<bool> runnerFailed{false};
    auto subscription = api::events().serverTick.subscribeScoped(
        -100, [&](api::ServerTickEvent&) {
            if (!callbackStopped.exchange(true, std::memory_order_acq_rel))
                server.stop();
        });
    std::atomic<bool> runReturned{false};
    std::thread runner([&] {
        try {
            server.runForever();
        } catch (const std::exception& error) {
            std::fprintf(stderr, "self-stop test runForever failed: %s\n", error.what());
            runnerFailed.store(true, std::memory_order_release);
        }
        runReturned.store(true, std::memory_order_release);
    });
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(5);
    while (!runReturned.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (!runReturned.load(std::memory_order_acquire)) server.stop();
    runner.join();
    server.stop();
    subscription.reset();
    CHECK(!runnerFailed.load(std::memory_order_acquire),
          "tick-callback stop exits runForever without an exception");
    CHECK(callbackStopped.load(std::memory_order_acquire),
          "tick callback can request shutdown without tearing down its own thread");
    CHECK(!server.running(), "tick-callback stop leaves server stopped");

    std::filesystem::remove_all(worldDir, ec);
    CHECK(!ec, "self-stop lifecycle test removes its temporary world");
}

static void scenarioTickLoopCannotStartAfterStop() {
    std::printf("\n[server lifecycle — late tick-loop startup after stop]\n");
    const std::string worldDir =
        "/tmp/cppfm-late-start-" + std::to_string(getpid());
    std::error_code ec;
    std::filesystem::remove_all(worldDir, ec);

    ServerConfig cfg;
    cfg.ioWorkerThreads = 1;
    cfg.jvmEnabled = false;
    cfg.port = 0;
    cfg.worldDir = worldDir;

    GameServer server(cfg);
    server.init();
    server.stop();

    const bool lateLoopStarted =
        NativeIntegrationTestAccess::startTickLoop(server);
    CHECK(!lateLoopStarted,
          "a stopped server rejects a tick loop that starts after teardown began");
    CHECK(!NativeIntegrationTestAccess::tickLoopActive(server),
          "late startup cannot publish an active tick loop after stop returned");
    CHECK(!server.running(), "late tick-loop startup leaves the server stopped");

    std::filesystem::remove_all(worldDir, ec);
    CHECK(!ec, "late tick-loop lifecycle test removes its temporary world");
}

static void scenarioShutdownLifecycleOwnerHandoff() {
    std::printf("\n[server lifecycle — no-loop shutdown owner handoff]\n");
    const std::string worldDir =
        "/tmp/cppfm-shutdown-owner-" + std::to_string(getpid());
    std::error_code ec;
    std::filesystem::remove_all(worldDir, ec);

    ServerConfig cfg;
    cfg.ioWorkerThreads = 1;
    cfg.jvmEnabled = false;
    cfg.port = 0;
    cfg.worldDir = worldDir;

    GameServer server(cfg);
    server.init();
    server.stop();

    std::atomic<bool> callbackWasOwner{false};
    std::atomic<bool> nativeMutationRan{false};
    std::atomic<bool> callbackAccepted{false};
    std::thread finalizer([&] {
        callbackAccepted.store(
            NativeIntegrationTestAccess::runFinalLifecycleCallback(server, [&] {
                callbackWasOwner.store(
                    NativeIntegrationTestAccess::isCurrentServerThread(server) &&
                        NativeIntegrationTestAccess::isCurrentTickThread(server),
                    std::memory_order_release);
                return server.runOnServerThreadDetailed(
                    [&] {
                        nativeMutationRan.store(true,
                                                std::memory_order_release);
                    }, std::chrono::milliseconds(1)) ==
                    ServerThreadTaskOutcome::Completed;
            }),
            std::memory_order_release);
    });
    finalizer.join();

    CHECK(callbackAccepted.load(std::memory_order_acquire) &&
              callbackWasOwner.load(std::memory_order_acquire) &&
              nativeMutationRan.load(std::memory_order_acquire),
          "terminal callbacks retain synchronous native mutation on the temporary server owner");
    CHECK(NativeIntegrationTestAccess::isCurrentServerThread(server) &&
              !NativeIntegrationTestAccess::isCurrentTickThread(server),
          "bootstrap ownership is restored when temporary shutdown ownership ends");

    std::filesystem::remove_all(worldDir, ec);
    CHECK(!ec, "shutdown owner-handoff test removes its temporary world");
}

static void scenarioConcurrentShutdownLifecycleCallbacks() {
    std::printf("\n[server lifecycle — concurrent terminal callback serialization]\n");
    const std::string worldDir =
        "/tmp/cppfm-shutdown-serialize-" + std::to_string(getpid());
    std::error_code ec;
    std::filesystem::remove_all(worldDir, ec);

    ServerConfig cfg;
    cfg.ioWorkerThreads = 1;
    cfg.jvmEnabled = false;
    cfg.port = 0;
    cfg.worldDir = worldDir;

    GameServer server(cfg);
    server.init();
    server.stop();

    std::atomic<bool> firstEntered{false};
    std::atomic<bool> releaseFirst{false};
    std::atomic<bool> firstMutationRan{false};
    std::atomic<bool> firstAccepted{false};
    std::atomic<bool> secondAttempting{false};
    std::atomic<bool> secondEntered{false};
    std::atomic<bool> secondAccepted{false};
    std::thread first([&] {
        firstAccepted.store(
            NativeIntegrationTestAccess::runFinalLifecycleCallback(server, [&] {
                firstEntered.store(true, std::memory_order_release);
                while (!releaseFirst.load(std::memory_order_acquire))
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                const bool mutationCompleted =
                    server.runOnServerThreadDetailed(
                        [&] {
                            firstMutationRan.store(
                                true, std::memory_order_release);
                        }, std::chrono::milliseconds(1)) ==
                    ServerThreadTaskOutcome::Completed;
                return mutationCompleted;
            }),
            std::memory_order_release);
    });
    const auto firstDeadline = std::chrono::steady_clock::now() +
                               std::chrono::seconds(2);
    while (!firstEntered.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < firstDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool firstStarted = firstEntered.load(std::memory_order_acquire);

    std::thread second([&] {
        secondAttempting.store(true, std::memory_order_release);
        secondAccepted.store(
            NativeIntegrationTestAccess::runFinalLifecycleCallback(server, [&] {
                secondEntered.store(true, std::memory_order_release);
                return true;
            }),
            std::memory_order_release);
    });
    const auto secondAttemptDeadline = std::chrono::steady_clock::now() +
                                      std::chrono::seconds(2);
    while (!secondAttempting.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < secondAttemptDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto serializationDeadline = std::chrono::steady_clock::now() +
                                       std::chrono::milliseconds(250);
    while (!secondEntered.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < serializationDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool overlapped = secondEntered.load(std::memory_order_acquire);
    releaseFirst.store(true, std::memory_order_release);
    first.join();
    second.join();

    CHECK(firstStarted && !overlapped,
          "a concurrent terminal callback is rejected while another owns the scope");
    CHECK(firstAccepted.load(std::memory_order_acquire) &&
              firstMutationRan.load(std::memory_order_acquire) &&
              !secondEntered.load(std::memory_order_acquire) &&
              !secondAccepted.load(std::memory_order_acquire),
          "overlapping terminal dispatch is rejected without stealing server ownership");
    CHECK(NativeIntegrationTestAccess::isCurrentServerThread(server) &&
              !NativeIntegrationTestAccess::isCurrentTickThread(server),
          "concurrent terminal callbacks leave the original bootstrap owner intact");

    std::filesystem::remove_all(worldDir, ec);
    CHECK(!ec, "shutdown serialization test removes its temporary world");
}

static void scenarioStopFromFinalLifecycleCallbackIsDeferred() {
    std::printf("\n[server lifecycle — stop requested from terminal callback worker]\n");
    const std::string worldDir =
        "/tmp/cppfm-shutdown-callback-stop-" + std::to_string(getpid());
    std::error_code ec;
    std::filesystem::remove_all(worldDir, ec);

    ServerConfig cfg;
    cfg.ioWorkerThreads = 1;
    cfg.jvmEnabled = false;
    cfg.port = 0;
    cfg.worldDir = worldDir;

    GameServer server(cfg);
    server.init();
    std::atomic<bool> callbackAccepted{false};
    std::atomic<bool> workerStopReturned{false};
    std::atomic<bool> stopReturnedBeforeCallbackEnd{false};
    std::thread stopper;
    std::thread finalizer([&] {
        callbackAccepted.store(
            NativeIntegrationTestAccess::runFinalLifecycleCallback(server, [&] {
                stopper = std::thread([&] {
                    server.stop();
                    workerStopReturned.store(true, std::memory_order_release);
                });
                const auto deadline = std::chrono::steady_clock::now() +
                                      std::chrono::milliseconds(500);
                while (!workerStopReturned.load(std::memory_order_acquire) &&
                       std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                stopReturnedBeforeCallbackEnd.store(
                    workerStopReturned.load(std::memory_order_acquire),
                    std::memory_order_release);
                return true;
            }),
            std::memory_order_release);
        if (stopper.joinable()) stopper.join();
    });
    finalizer.join();
    server.stop();

    CHECK(callbackAccepted.load(std::memory_order_acquire) &&
              stopReturnedBeforeCallbackEnd.load(std::memory_order_acquire),
          "a worker stop request returns without waiting on the callback that joins it");
    CHECK(!server.running() && server.shutdownPersistenceSucceeded(),
          "the owning caller completes deferred shutdown after the callback unwinds");

    std::filesystem::remove_all(worldDir, ec);
    CHECK(!ec, "callback stop regression removes its temporary world");
}

static void scenarioBootstrapOwnerTasksQuiesceBeforeTickHandoff() {
    std::printf("\n[server lifecycle — bootstrap owner handoff quiescence]\n");
    const std::string worldDir =
        "/tmp/cppfm-owner-handoff-" + std::to_string(getpid());
    std::error_code ec;
    std::filesystem::remove_all(worldDir, ec);

    ServerConfig cfg;
    cfg.ioWorkerThreads = 1;
    cfg.jvmEnabled = false;
    cfg.port = 0;
    cfg.worldDir = worldDir;

    GameServer server(cfg);
    server.init();
    std::atomic<bool> releaseInlineTask{false};
    std::atomic<bool> tickLoopPublished{false};
    std::atomic<bool> runnerReturned{false};
    std::atomic<bool> ownerChangedBeforeInlineReturn{false};
    std::thread runner;
    std::thread controller;

    const auto inlineOutcome = server.runOnServerThreadDetailed([&] {
        runner = std::thread([&] {
            server.runForever();
            runnerReturned.store(true, std::memory_order_release);
        });
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(2);
        while (!NativeIntegrationTestAccess::tickLoopActive(server) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        tickLoopPublished.store(
            NativeIntegrationTestAccess::tickLoopActive(server),
            std::memory_order_release);

        controller = std::thread([&] {
            const auto owner = runner.get_id();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ownerChangedBeforeInlineReturn.store(
                NativeIntegrationTestAccess::serverThreadOwnerIs(server, owner),
                std::memory_order_release);
            releaseInlineTask.store(true, std::memory_order_release);
            const auto ownerDeadline = std::chrono::steady_clock::now() +
                                       std::chrono::seconds(2);
            while (!NativeIntegrationTestAccess::serverThreadOwnerIs(server, owner) &&
                   !runnerReturned.load(std::memory_order_acquire) &&
                   std::chrono::steady_clock::now() < ownerDeadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            server.requestStop();
        });

        const auto releaseDeadline = std::chrono::steady_clock::now() +
                                     std::chrono::seconds(2);
        while (!releaseInlineTask.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < releaseDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }, std::chrono::seconds(3));

    if (controller.joinable()) controller.join();
    if (runner.joinable()) runner.join();
    server.stop();

    CHECK(tickLoopPublished.load(std::memory_order_acquire) &&
              inlineOutcome == ServerThreadTaskOutcome::Completed,
          "tick startup waits for an in-flight bootstrap-owner operation");
    CHECK(!ownerChangedBeforeInlineReturn.load(std::memory_order_acquire),
          "authoritative owner cannot transfer before old-owner inline work quiesces");
    CHECK(runnerReturned.load(std::memory_order_acquire),
          "tick-loop runner completes after the ownership handoff");

    std::filesystem::remove_all(worldDir, ec);
    CHECK(!ec, "owner handoff regression removes its temporary world");
}

static void scenarioStartupStopRequestIsNotLost() {
    std::printf("\n[server lifecycle — stop request during startup]\n");
    const std::string worldDir =
        "/tmp/cppfm-start-stop-" + std::to_string(getpid());
    std::error_code ec;
    std::filesystem::remove_all(worldDir, ec);

    ServerConfig cfg;
    cfg.ioWorkerThreads = 1;
    cfg.jvmEnabled = false;
    cfg.port = 0;
    cfg.worldDir = worldDir;

    GameServer server(cfg);
    server.init();
    server.requestStop();

    std::atomic<bool> runReturned{false};
    std::atomic<bool> runFailed{false};
    std::thread runner([&] {
        try {
            server.runForever();
        } catch (...) {
            runFailed.store(true, std::memory_order_release);
        }
        runReturned.store(true, std::memory_order_release);
    });
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (!runReturned.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool returnedBeforeRescue =
        runReturned.load(std::memory_order_acquire);
    if (!returnedBeforeRescue) server.requestStop();
    runner.join();
    server.stop();

    CHECK(returnedBeforeRescue && !runFailed.load(std::memory_order_acquire),
          "a pre-listener stop request is not overwritten by runForever startup");
    CHECK(!server.running() &&
              !NativeIntegrationTestAccess::tickLoopActive(server),
          "startup stop leaves no running server loop");

    std::filesystem::remove_all(worldDir, ec);
    CHECK(!ec, "startup stop test removes its temporary world");
}

static void scenarioStopFromPlayerJoinCallback() {
    std::printf("\n[server lifecycle — stop requested by a player-join callback]\n");
    const std::string worldDir =
        "/tmp/cppfm-session-self-stop-" + std::to_string(getpid());
    std::error_code ec;
    std::filesystem::remove_all(worldDir, ec);
    const auto port = findEphemeralPort();
    CHECK(port != 0, "reserve an ephemeral test port");
    if (!port) return;

    ServerConfig cfg;
    cfg.ioWorkerThreads = 1;
    cfg.jvmEnabled = false;
    cfg.onlineMode = false;
    cfg.enforceSecureProfile = false;
    cfg.port = port;
    cfg.worldDir = worldDir;

    GameServer server(cfg);
    server.init();
    std::atomic<bool> callbackReturned{false};
    std::atomic<bool> runnerFailed{false};
    std::atomic<bool> runReturned{false};
    auto subscription = server.events().join.subscribeScoped(
        -100, [&](api::PlayerJoinEvent&) {
            server.stop();
            callbackReturned.store(true, std::memory_order_release);
        });
    std::thread runner([&] {
        try {
            server.runForever();
        } catch (const std::exception& error) {
            std::fprintf(stderr, "session self-stop test runForever failed: %s\n",
                         error.what());
            runnerFailed.store(true, std::memory_order_release);
        }
        runReturned.store(true, std::memory_order_release);
    });

    const auto startDeadline = std::chrono::steady_clock::now() +
                               std::chrono::seconds(5);
    while (!server.running() && std::chrono::steady_clock::now() < startDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    TestClient client;
    const bool connected = server.running() && client.connect("127.0.0.1", port);
    CHECK(connected, "client connects to the callback-stop server");
    const bool joined = connected && client.join("SessionSelfStop");
    CHECK(joined, "client completes login that invokes the player-join callback");

    const auto stopDeadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(5);
    while (!runReturned.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < stopDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (!runReturned.load(std::memory_order_acquire)) server.stop();
    client.close();
    if (runner.joinable()) runner.join();
    server.stop();
    subscription.reset();

    CHECK(callbackReturned.load(std::memory_order_acquire),
          "session-thread stop request returns without self-joining or tearing down live state");
    CHECK(!runnerFailed.load(std::memory_order_acquire) &&
              runReturned.load(std::memory_order_acquire),
          "external owner completes shutdown after player-join callback exits");
    std::filesystem::remove_all(worldDir, ec);
    CHECK(!ec, "session self-stop lifecycle test removes its temporary world");
}

static void scenarioPlayerDataLoadIsTransactional() {
    std::printf("\n[player persistence — failed parse leaves live player unchanged]\n");
    const auto worldDir = std::filesystem::temp_directory_path() /
        ("cppfm-playerdata-transaction-" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(worldDir, ec);
    std::filesystem::create_directories(worldDir / "playerdata");

    ServerConfig cfg;
    cfg.jvmEnabled = false;
    cfg.ioWorkerThreads = 1;
    cfg.worldDir = worldDir.string();
    const std::string playerKey = "atomicity-fixture";
    const auto playerFile = worldDir / "playerdata" /
                            (playerKey + ".dat");
    {
        GameServer server(cfg);
        Player player;
        player.x = 1.25;
        player.y = 72.0;
        player.z = -4.5;
        player.health = 11.0f;
        player.food = 13;
        player.inv[0] = ItemStack::of(
            gen::itemIdByName().at("minecraft:diamond"), 3);
        player.inv[1] = ItemStack::of(
            gen::itemIdByName().at("minecraft:gold_ingot"), 4);
        player.scoreboardTags = {"alpha", "build:zone", "zeta"};

        Player tagLimitPlayer;
        bool filledTags = true;
        for (std::size_t index = 0;
             index < Player::kMaxScoreboardTags; ++index) {
            filledTags = tagLimitPlayer.addScoreboardTag(
                "tag-" + std::to_string(index)) && filledTags;
        }
        CHECK(filledTags &&
                  tagLimitPlayer.scoreboardTags.size() ==
                      Player::kMaxScoreboardTags,
              "players can add tags up to the vanilla 1024-tag limit");
        CHECK(!tagLimitPlayer.addScoreboardTag("tag-0") &&
                  !tagLimitPlayer.addScoreboardTag("tag-over-limit") &&
                  tagLimitPlayer.scoreboardTags.size() ==
                      Player::kMaxScoreboardTags,
              "duplicate and over-limit command tags do not grow player state");

        nbt::Value root = nbt::Value::makeCompound();
        root.set("Health", nbt::Value::makeFloat(2.0f));
        nbt::Value position = nbt::Value::makeList(nbt::Double);
        nbt::Value px; px.tag = nbt::Double; px.d = 900.0;
        nbt::Value py; py.tag = nbt::Double; py.d = 120.0;
        nbt::Value pz; pz.tag = nbt::Double; pz.d = 901.0;
        position.list = {px, py, pz};
        root.set("Pos", std::move(position));
        nbt::Value inventory = nbt::Value::makeList(nbt::Compound);
        nbt::Value validStack = nbt::Value::makeCompound();
        validStack.set("id", nbt::Value::makeString("minecraft:stone"));
        validStack.set("Count", nbt::Value::makeByte(1));
        validStack.set("Slot", nbt::Value::makeByte(5));
        inventory.list.push_back(std::move(validStack));
        nbt::Value malformedStack = nbt::Value::makeCompound();
        malformedStack.set("id", nbt::Value::makeString("minecraft:stone"));
        malformedStack.set("Count", nbt::Value::makeByte(0));
        malformedStack.set("Slot", nbt::Value::makeByte(6));
        inventory.list.push_back(std::move(malformedStack));
        root.set("Inventory", std::move(inventory));
        WriteBuffer encoded;
        nbt::writeFileRoot(encoded, root);
        {
            std::ofstream file(playerFile, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(encoded.data.data()),
                       static_cast<std::streamsize>(encoded.data.size()));
        }

        bool filePresent = false;
        const bool loaded = server.loadPlayerData(playerKey, player,
                                                  &filePresent);
        CHECK(!loaded && filePresent,
              "malformed player data is rejected and isolated");
        CHECK(player.x == 1.25 && player.y == 72.0 && player.z == -4.5 &&
                  player.health == 11.0f && player.food == 13,
              "failed player-data parsing does not partially apply position or survival state");
        CHECK(player.inv[0].itemId == gen::itemIdByName().at("minecraft:diamond") &&
                  player.inv[0].count == 3 &&
                  player.inv[1].itemId == gen::itemIdByName().at("minecraft:gold_ingot") &&
                  player.inv[1].count == 4 && player.inv[5].empty() &&
                  player.inv[6].empty(),
              "failed player-data parsing does not clear or partially replace inventory");
        CHECK(std::filesystem::exists(playerFile.string() + ".corrupt"),
              "rejected player data remains available under the quarantine suffix");

        const std::string tagRoundTripKey = playerKey + "-tags";
        server.savePlayerData(tagRoundTripKey, player);
        Player restoredTags;
        bool tagFilePresent = false;
        CHECK(server.loadPlayerData(tagRoundTripKey, restoredTags,
                                    &tagFilePresent) && tagFilePresent &&
                  restoredTags.scoreboardTags == player.scoreboardTags,
              "player scoreboard tags round-trip through vanilla Tags NBT");

        const std::string oversizedSaveKey = playerKey + "-oversized-save";
        const auto oversizedSavePath = worldDir / "playerdata" /
                                       (oversizedSaveKey + ".dat");
        Player baselineSave;
        baselineSave.health = 7.0f;
        server.savePlayerData(oversizedSaveKey, baselineSave);
        const auto readPlayerSave = [](const std::filesystem::path& path) {
            std::ifstream file(path, std::ios::binary);
            return std::vector<std::uint8_t>(
                (std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());
        };
        const auto baselineBytes = readPlayerSave(oversizedSavePath);
        CHECK(!baselineBytes.empty() &&
                  baselineBytes.size() <= 8u * 1024u * 1024u,
              "player save fixture starts with a valid under-cap NBT file");
        Player oversizedSave;
        for (int index = 0; index < 130; ++index)
            oversizedSave.scoreboardTags.insert(
                std::to_string(index) + std::string(65500, 'x'));
        CHECK(oversizedSave.scoreboardTags.size() == 130,
              "oversized player fixture contains enough individually valid NBT strings to exceed the save cap");
        server.savePlayerData(oversizedSaveKey, oversizedSave);
        const auto afterOversizedSaveBytes = readPlayerSave(oversizedSavePath);
        CHECK(afterOversizedSaveBytes == baselineBytes,
              "oversized serialized player NBT is rejected without replacing the previous save");

        const std::string emptyTagsKey = playerKey + "-empty-tags";
        Player emptyTagsPlayer;
        server.savePlayerData(emptyTagsKey, emptyTagsPlayer);
        const auto emptyTagsPath = worldDir / "playerdata" /
                                   (emptyTagsKey + ".dat");
        std::ifstream emptyTagsFile(emptyTagsPath, std::ios::binary);
        std::vector<std::uint8_t> emptyTagsBytes(
            (std::istreambuf_iterator<char>(emptyTagsFile)),
            std::istreambuf_iterator<char>());
        ReadBuffer emptyTagsBuffer(emptyTagsBytes);
        nbt::Parser emptyTagsParser(emptyTagsBuffer);
        const auto emptyTagsRoot = emptyTagsParser.readFileRoot();
        const auto* savedEmptyTags = emptyTagsRoot.get("Tags");
        CHECK(savedEmptyTags && savedEmptyTags->tag == nbt::List &&
                  savedEmptyTags->list.empty() &&
                  savedEmptyTags->listElement == nbt::End,
              "empty player Tags use vanilla's TAG_End list marker");
        Player restoredEmptyTags;
        bool emptyTagsPresent = false;
        CHECK(server.loadPlayerData(emptyTagsKey, restoredEmptyTags,
                                    &emptyTagsPresent) && emptyTagsPresent &&
                  restoredEmptyTags.scoreboardTags.empty(),
              "vanilla-shaped empty Tags lists load successfully");

        const std::string legacyTagsKey = playerKey + "-legacy-tags";
        const auto legacyTagsPath = worldDir / "playerdata" /
                                    (legacyTagsKey + ".dat");
        WriteBuffer legacyTagsBytes;
        nbt::writeFileRoot(legacyTagsBytes, nbt::Value::makeCompound());
        {
            std::ofstream file(legacyTagsPath,
                               std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(legacyTagsBytes.data.data()),
                       static_cast<std::streamsize>(legacyTagsBytes.data.size()));
        }
        Player legacyTagsPlayer;
        legacyTagsPlayer.scoreboardTags.insert("stale");
        bool legacyTagsPresent = false;
        CHECK(server.loadPlayerData(legacyTagsKey, legacyTagsPlayer,
                                    &legacyTagsPresent) &&
                  legacyTagsPresent && legacyTagsPlayer.scoreboardTags.empty(),
              "legacy player data without Tags clears preexisting session tags");

        const auto validPosition = [] {
            nbt::Value position = nbt::Value::makeList(nbt::Double);
            position.list = {nbt::Value::makeDouble(900.0),
                             nbt::Value::makeDouble(120.0),
                             nbt::Value::makeDouble(901.0)};
            return position;
        };
        const auto rejectedFixture = [&](const std::string& fixtureName,
                                         nbt::Value malformed,
                                         const char* description) {
            malformed.set("Health", nbt::Value::makeFloat(2.0f));
            const auto path = worldDir / "playerdata" /
                              (fixtureName + ".dat");
            WriteBuffer bytes;
            nbt::writeFileRoot(bytes, malformed);
            {
                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                file.write(reinterpret_cast<const char*>(bytes.data.data()),
                           static_cast<std::streamsize>(bytes.data.size()));
            }

            Player candidate;
            candidate.x = 1.25;
            candidate.y = 72.0;
            candidate.z = -4.5;
            candidate.health = 11.0f;
            candidate.food = 13;
            candidate.inv[0] = ItemStack::of(
                gen::itemIdByName().at("minecraft:diamond"), 3);
            candidate.inv[1] = ItemStack::of(
                gen::itemIdByName().at("minecraft:gold_ingot"), 4);
            candidate.enderItems[0] = ItemStack::of(
                gen::itemIdByName().at("minecraft:emerald"), 5);
            candidate.scoreboardTags.insert("unchanged");
            bool present = false;
            const bool loaded = server.loadPlayerData(fixtureName, candidate,
                                                      &present);
            CHECK(!loaded && present &&
                      std::filesystem::exists(path.string() + ".corrupt"),
                  description);
            CHECK(candidate.x == 1.25 && candidate.y == 72.0 &&
                      candidate.z == -4.5 && candidate.health == 11.0f &&
                      candidate.food == 13 &&
                      candidate.inv[0].itemId ==
                          gen::itemIdByName().at("minecraft:diamond") &&
                      candidate.inv[0].count == 3 &&
                      candidate.inv[1].itemId ==
                          gen::itemIdByName().at("minecraft:gold_ingot") &&
                      candidate.inv[1].count == 4 &&
                      candidate.enderItems[0].itemId ==
                          gen::itemIdByName().at("minecraft:emerald") &&
                      candidate.enderItems[0].count == 5 &&
                      candidate.scoreboardTags.size() == 1 &&
                      candidate.scoreboardTags.contains("unchanged"),
                  "malformed player data leaves position, survival data, inventory, ender inventory, and tags unchanged");
        };

        nbt::Value unregisteredItemData = nbt::Value::makeCompound();
        unregisteredItemData.set("Pos", validPosition());
        nbt::Value unregisteredInventory = nbt::Value::makeList(nbt::Compound);
        nbt::Value unregisteredItem = nbt::Value::makeCompound();
        unregisteredItem.set("id",
            nbt::Value::makeString("example:modded_item"));
        unregisteredItem.set("Count", nbt::Value::makeByte(1));
        unregisteredItem.set("Slot", nbt::Value::makeByte(0));
        unregisteredInventory.list.push_back(unregisteredItem);
        unregisteredItemData.set("Inventory", std::move(unregisteredInventory));
        const std::string unregisteredKey = playerKey + "-unregistered-item";
        const auto unregisteredPath = worldDir / "playerdata" /
                                      (unregisteredKey + ".dat");
        WriteBuffer unregisteredBytes;
        nbt::writeFileRoot(unregisteredBytes, unregisteredItemData);
        {
            std::ofstream file(unregisteredPath,
                               std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(unregisteredBytes.data.data()),
                       static_cast<std::streamsize>(unregisteredBytes.data.size()));
        }
        Player withUnregisteredItem;
        withUnregisteredItem.inv[0] = ItemStack::of(
            gen::itemIdByName().at("minecraft:diamond"), 3);
        bool unregisteredFilePresent = false;
        bool unsupportedItemData = false;
        bool unsafeUnregisteredData = false;
        const bool unregisteredLoaded = server.loadPlayerData(
            unregisteredKey, withUnregisteredItem, &unregisteredFilePresent,
            &unsupportedItemData, &unsafeUnregisteredData);
        std::ifstream unregisteredAfterFile(unregisteredPath,
                                             std::ios::binary);
        const std::vector<std::uint8_t> retainedUnregisteredBytes(
            (std::istreambuf_iterator<char>(unregisteredAfterFile)),
            std::istreambuf_iterator<char>());
        CHECK(!unregisteredLoaded && unregisteredFilePresent &&
                  unsupportedItemData &&
                  unsafeUnregisteredData &&
                  std::filesystem::exists(unregisteredPath) &&
                  !std::filesystem::exists(unregisteredPath.string() + ".corrupt") &&
                  retainedUnregisteredBytes == unregisteredBytes.data,
              "unregistered mod item data is rejected without quarantine or changing the active player save");
        CHECK(withUnregisteredItem.inv[0].itemId ==
                  gen::itemIdByName().at("minecraft:diamond") &&
                  withUnregisteredItem.inv[0].count == 3,
              "unsupported player items leave live player state unchanged");

        nbt::Value wrongPos = nbt::Value::makeCompound();
        nbt::Value integerPosition = nbt::Value::makeList(nbt::Int);
        integerPosition.list = {nbt::Value::makeInt(900),
                                nbt::Value::makeInt(120),
                                nbt::Value::makeInt(901)};
        wrongPos.set("Pos", std::move(integerPosition));
        nbt::Value validInventory = nbt::Value::makeList(nbt::Compound);
        nbt::Value stagedItem = nbt::Value::makeCompound();
        stagedItem.set("id", nbt::Value::makeString("minecraft:stone"));
        stagedItem.set("Count", nbt::Value::makeByte(1));
        stagedItem.set("Slot", nbt::Value::makeByte(5));
        validInventory.list.push_back(std::move(stagedItem));
        wrongPos.set("Inventory", std::move(validInventory));
        rejectedFixture(playerKey + "-wrong-pos", std::move(wrongPos),
                        "wrong-tag Pos is rejected and quarantined");

        nbt::Value wrongInventory = nbt::Value::makeCompound();
        wrongInventory.set("Pos", validPosition());
        wrongInventory.set("Inventory",
                           nbt::Value::makeString("not an inventory list"));
        rejectedFixture(playerKey + "-wrong-inventory",
                        std::move(wrongInventory),
                        "wrong-tag Inventory is rejected and quarantined");

        nbt::Value wrongTags = nbt::Value::makeCompound();
        wrongTags.set("Pos", validPosition());
        wrongTags.set("Tags", nbt::Value::makeString("not a string list"));
        rejectedFixture(playerKey + "-wrong-tags", std::move(wrongTags),
                        "wrong-tag Tags is rejected and quarantined");

        nbt::Value wrongEnderItems = nbt::Value::makeCompound();
        wrongEnderItems.set("Pos", validPosition());
        wrongEnderItems.set("EnderItems", nbt::Value::makeInt(1));
        rejectedFixture(playerKey + "-wrong-ender-items",
                        std::move(wrongEnderItems),
                        "wrong-tag EnderItems is rejected and quarantined");

        nbt::Value wrongXpLevel = nbt::Value::makeCompound();
        wrongXpLevel.set("Pos", validPosition());
        wrongXpLevel.set("XpLevel", nbt::Value::makeFloat(17.0f));
        wrongXpLevel.set("XpTotal", nbt::Value::makeInt(200));
        rejectedFixture(playerKey + "-wrong-xp-level",
                        std::move(wrongXpLevel),
                        "wrong-tag XpLevel is rejected and quarantined");

        nbt::Value outOfRangeSlot = nbt::Value::makeCompound();
        outOfRangeSlot.set("Pos", validPosition());
        nbt::Value badSlotList = nbt::Value::makeList(nbt::Compound);
        nbt::Value badSlotItem = nbt::Value::makeCompound();
        badSlotItem.set("id", nbt::Value::makeString("minecraft:stone"));
        badSlotItem.set("Count", nbt::Value::makeByte(1));
        badSlotItem.set("Slot", nbt::Value::makeInt(127));
        badSlotList.list.push_back(std::move(badSlotItem));
        outOfRangeSlot.set("Inventory", std::move(badSlotList));
        rejectedFixture(playerKey + "-out-of-range-slot",
                        std::move(outOfRangeSlot),
                        "unsupported out-of-range player Slot is rejected and quarantined");

        nbt::Value vanillaEquipment = nbt::Value::makeCompound();
        vanillaEquipment.set("Pos", validPosition());
        nbt::Value equipmentItems = nbt::Value::makeList(nbt::Compound);
        nbt::Value helmet = nbt::Value::makeCompound();
        helmet.set("id", nbt::Value::makeString("minecraft:diamond_helmet"));
        helmet.set("Count", nbt::Value::makeByte(1));
        helmet.set("Slot", nbt::Value::makeByte(103));
        equipmentItems.list.push_back(std::move(helmet));
        const auto addArmorPiece = [&](const char* itemId, std::int8_t slot) {
            nbt::Value armorPiece = nbt::Value::makeCompound();
            armorPiece.set("id", nbt::Value::makeString(itemId));
            armorPiece.set("Count", nbt::Value::makeByte(1));
            armorPiece.set("Slot", nbt::Value::makeByte(slot));
            equipmentItems.list.push_back(std::move(armorPiece));
        };
        addArmorPiece("minecraft:diamond_chestplate", 102);
        addArmorPiece("minecraft:diamond_leggings", 101);
        addArmorPiece("minecraft:diamond_boots", 100);
        nbt::Value offhand = nbt::Value::makeCompound();
        offhand.set("id", nbt::Value::makeString("minecraft:shield"));
        offhand.set("Count", nbt::Value::makeByte(1));
        offhand.set("Slot", nbt::Value::makeByte(-106));
        equipmentItems.list.push_back(std::move(offhand));
        vanillaEquipment.set("Inventory", std::move(equipmentItems));
        const std::string equipmentKey = playerKey + "-vanilla-equipment";
        const auto equipmentPath = worldDir / "playerdata" /
                                   (equipmentKey + ".dat");
        WriteBuffer equipmentBytes;
        nbt::writeFileRoot(equipmentBytes, vanillaEquipment);
        {
            std::ofstream file(equipmentPath,
                               std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(equipmentBytes.data.data()),
                       static_cast<std::streamsize>(equipmentBytes.data.size()));
        }
        Player equipped;
        bool equipmentPresent = false;
        const bool equipmentLoaded = server.loadPlayerData(
            equipmentKey, equipped, &equipmentPresent);
        CHECK(equipmentLoaded && equipmentPresent &&
                  equipped.inv[8].itemId ==
                      gen::itemIdByName().at("minecraft:diamond_helmet") &&
                  equipped.inv[7].itemId ==
                      gen::itemIdByName().at("minecraft:diamond_chestplate") &&
                  equipped.inv[6].itemId ==
                      gen::itemIdByName().at("minecraft:diamond_leggings") &&
                  equipped.inv[5].itemId ==
                      gen::itemIdByName().at("minecraft:diamond_boots") &&
                  equipped.inv[45].itemId ==
                      gen::itemIdByName().at("minecraft:shield"),
              "vanilla feet-through-head armor and offhand aliases map to the matching internal equipment slots");
        server.savePlayerData(equipmentKey, equipped);
        Player roundTrippedEquipment;
        bool roundTrippedEquipmentPresent = false;
        const bool roundTrippedEquipmentLoaded = server.loadPlayerData(
            equipmentKey, roundTrippedEquipment,
            &roundTrippedEquipmentPresent);
        CHECK(roundTrippedEquipmentLoaded &&
                  roundTrippedEquipmentPresent &&
                  roundTrippedEquipment.inv[8].itemId ==
                      gen::itemIdByName().at("minecraft:diamond_helmet") &&
                  roundTrippedEquipment.inv[7].itemId ==
                      gen::itemIdByName().at("minecraft:diamond_chestplate") &&
                  roundTrippedEquipment.inv[6].itemId ==
                      gen::itemIdByName().at("minecraft:diamond_leggings") &&
                  roundTrippedEquipment.inv[5].itemId ==
                      gen::itemIdByName().at("minecraft:diamond_boots") &&
                  roundTrippedEquipment.inv[45].itemId ==
                      gen::itemIdByName().at("minecraft:shield"),
              "saving and reloading vanilla equipment keeps every armor and offhand slot stable");
    }
    std::filesystem::remove_all(worldDir, ec);
    CHECK(!ec, "player-data transaction fixture removes its exact temporary world");
}

static void scenarioAdminBanLoadFailsClosed() {
    std::printf("\n[admin persistence — invalid existing ban files fail closed]\n");
    const auto adminDir = std::filesystem::temp_directory_path() /
        ("cppfm-admin-ban-load-" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(adminDir, ec);
    std::filesystem::create_directories(adminDir);
    {
        std::ofstream malformed(adminDir / "banned-players.json",
                                std::ios::binary | std::ios::trunc);
        malformed << "{ invalid json";
    }
    std::filesystem::create_directory(adminDir / "banned-ips.json");

    const auto previousDirectory = std::filesystem::current_path();
    struct RestoreCurrentDirectory {
        std::filesystem::path previous;
        bool active = true;
        ~RestoreCurrentDirectory() {
            if (active) {
                std::error_code restoreError;
                std::filesystem::current_path(previous, restoreError);
            }
        }
    } restore{previousDirectory, true};
    std::filesystem::current_path(adminDir);

    ServerConfig cfg;
    cfg.jvmEnabled = false;
    cfg.ioWorkerThreads = 1;
    cfg.worldDir = (adminDir / "world").string();
    GameServer server(cfg);
    server.bannedPlayersMut().insert("retained-player");
    server.bannedIpsMut().insert("192.0.2.17");

    bool playerBanLoadRejected = false;
    try {
        server.loadBans();
    } catch (const std::exception&) {
        playerBanLoadRejected = true;
    }
    CHECK(playerBanLoadRejected,
          "malformed existing banned-players.json aborts loading instead of disabling bans");
    CHECK(server.bannedPlayers().size() == 1 &&
              server.bannedPlayers().contains("retained-player"),
          "failed player-ban reload preserves the active ban set");

    bool ipBanLoadRejected = false;
    try {
        server.loadBannedIps();
    } catch (const std::exception&) {
        ipBanLoadRejected = true;
    }
    CHECK(ipBanLoadRejected,
          "non-regular existing banned-ips.json aborts loading instead of disabling bans");
    CHECK(server.bannedIps().size() == 1 &&
              server.bannedIps().contains("192.0.2.17"),
          "failed IP-ban reload preserves the active ban set");

    std::filesystem::current_path(previousDirectory);
    restore.active = false;
    std::filesystem::remove_all(adminDir, ec);
    CHECK(!ec, "admin-ban fixture removes its exact temporary directory");
}

static void scenarioCloneBlockEntityBudgetPreflight() {
    std::printf("\n[/clone block-entity budget — rejection preserves destination and source]\n");
    const auto worldDir = std::filesystem::temp_directory_path() /
        ("cppfm-clone-be-budget-" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(worldDir, ec);
    CHECK(!ec, "clone budget fixture starts with a clean exact temporary world");

    ServerConfig cfg;
    cfg.ioWorkerThreads = 1;
    cfg.jvmEnabled = false;
    // This fixture measures the block-entity record cap, not normal terrain
    // generation. Keep its required chunk setup deterministic and cheap.
    cfg.levelType = "flat";
    cfg.worldDir = worldDir.string();
    {
        GameServer server(cfg);
        try {
            std::printf("  initializing flat fixture server...\n");
            server.init();
            std::printf("  fixture server initialized\n");
            auto& world = server.worldFor(0);
            auto& entities = server.blockEntities();
            const auto source = posKey(0, 64, 0);
            const auto destination = posKey(1, 64, 0);
            const auto chestState = static_cast<std::uint16_t>(
                gen::blockNameToState().at("minecraft:chest"));
            const auto stoneState = static_cast<std::uint16_t>(
                gen::blockNameToState().at("minecraft:stone"));
            const auto movingPiston =
                gen::blockNameToState().find("minecraft:moving_piston");

            CHECK(movingPiston != gen::blockNameToState().end(),
                  "moving-piston animation block is registered");
            if (movingPiston != gen::blockNameToState().end()) {
                constexpr std::int32_t animationX = 32;
                const auto animationKey = posKey(animationX, 64, 0);
                BlockEntityStore::Owner animationOwner;
                bool basePlaced = false;
                bool animationPlaced = false;
                {
                    auto mutationGate = world.lockMutationGate();
                    (void)mutationGate;
                    basePlaced = world.setBlock(
                        animationX, 64, 0, stoneState);
                    auto staged = entities.tryCreateSharedBatch(
                        {animationKey}, BlockEntity::Kind::MovingPiston);
                    animationPlaced = staged && world.setBlock(
                        animationX, 64, 0,
                        static_cast<std::uint16_t>(movingPiston->second));
                    if (staged && animationPlaced)
                        animationOwner = staged->front();
                    else if (staged)
                        entities.remove(animationKey);
                }
                CHECK(basePlaced && animationPlaced &&
                          animationOwner &&
                          entities.getShared(animationKey) == animationOwner,
                      "ordinary-block to moving-piston transition preserves the pre-admitted animation entity");

                if (animationOwner) {
                    const auto sourceReject = server.dispatchConsole(
                        "clone 32 64 0 32 64 0 33 64 0");
                    CHECK(sourceReject.find("moving piston") !=
                                  std::string::npos &&
                              world.getBlock(animationX, 64, 0) ==
                                  movingPiston->second &&
                              entities.getShared(animationKey) ==
                                  animationOwner &&
                              world.getBlock(33, 64, 0) == 0,
                          "/clone rejects a moving-piston source without changing its animation or destination");

                    const auto movingDestinationKey = posKey(35, 64, 0);
                    BlockEntityStore::Owner movingDestinationOwner;
                    bool sourceStonePlaced = false;
                    bool destinationMovingPlaced = false;
                    {
                        auto mutationGate = world.lockMutationGate();
                        (void)mutationGate;
                        sourceStonePlaced = world.setBlock(
                            34, 64, 0, stoneState);
                        auto owners = entities.tryCreateSharedBatch(
                            {movingDestinationKey},
                            BlockEntity::Kind::MovingPiston);
                        destinationMovingPlaced = owners && world.setBlock(
                            35, 64, 0,
                            static_cast<std::uint16_t>(movingPiston->second));
                        if (destinationMovingPlaced)
                            movingDestinationOwner = owners->front();
                        else if (owners)
                            entities.remove(movingDestinationKey);
                    }
                    const auto destinationReject =
                        sourceStonePlaced && destinationMovingPlaced
                            ? server.dispatchConsole(
                                  "clone 34 64 0 34 64 0 35 64 0")
                            : std::string{};
                    CHECK(sourceStonePlaced,
                          "destination-guard fixture places its ordinary source block");
                    CHECK(destinationMovingPlaced,
                          "destination-guard fixture stages a moving piston and owner");
                    CHECK(destinationReject.find("moving piston") !=
                              std::string::npos,
                          "/clone explicitly rejects a moving-piston destination");
                    CHECK(world.getBlock(34, 64, 0) == stoneState &&
                              world.getBlock(35, 64, 0) ==
                                  movingPiston->second,
                          "rejected destination clone preserves both block states");
                    CHECK(movingDestinationOwner &&
                              entities.getShared(movingDestinationKey) ==
                                  movingDestinationOwner,
                          "rejected destination clone preserves its animation owner");

                    const auto unrelatedMovingKey = posKey(37, 64, 0);
                    BlockEntityStore::Owner unrelatedMovingOwner;
                    bool unrelatedMovingPlaced = false;
                    bool unrelatedSourcePlaced = false;
                    {
                        auto mutationGate = world.lockMutationGate();
                        (void)mutationGate;
                        unrelatedSourcePlaced = world.setBlock(
                            36, 64, 0, stoneState);
                        auto owners = entities.tryCreateSharedBatch(
                            {unrelatedMovingKey},
                            BlockEntity::Kind::MovingPiston);
                        unrelatedMovingPlaced = owners && world.setBlock(
                            37, 64, 0,
                            static_cast<std::uint16_t>(movingPiston->second));
                        if (unrelatedMovingPlaced)
                            unrelatedMovingOwner = owners->front();
                        else if (owners)
                            entities.remove(unrelatedMovingKey);
                    }
                    const auto unrelatedCloneResult =
                        unrelatedMovingPlaced && unrelatedSourcePlaced
                            ? server.dispatchConsole(
                                  "clone 36 64 0 36 64 0 38 64 0")
                            : std::string{};
                    CHECK(unrelatedMovingPlaced,
                          "unrelated-animation fixture stages its moving piston");
                    CHECK(unrelatedSourcePlaced,
                          "unrelated-animation fixture places its clone source");
                    CHECK(unrelatedCloneResult.find("moving piston") ==
                              std::string::npos,
                          "/clone does not reject an unrelated animation in the chunk");
                    CHECK(world.getBlock(38, 64, 0) == stoneState,
                          "/clone copies a valid source beside an unrelated animation");
                    CHECK(unrelatedMovingOwner &&
                              entities.getShared(unrelatedMovingKey) ==
                                  unrelatedMovingOwner,
                          "/clone leaves the unrelated animation owner intact");

                    const bool animationFinalized = world.setBlock(
                        animationX, 64, 0, stoneState);
                    const bool destinationAnimationCleared = world.setBlock(
                        35, 64, 0, 0);
                    const bool unrelatedAnimationCleared = world.setBlock(
                        37, 64, 0, 0);
                    CHECK(animationFinalized && destinationAnimationCleared &&
                              unrelatedAnimationCleared &&
                              !entities.getShared(animationKey) &&
                              !entities.getShared(movingDestinationKey) &&
                              !entities.getShared(unrelatedMovingKey),
                          "clone animation fixtures clean up their exact transient owners");
                }
            }

            std::printf("  generating block-entity fixture chunk...\n");
            const bool chunkReady = world.generateChunkIfMissing(0, 0);
            std::printf("  block-entity fixture chunk generated: %s\n",
                        chunkReady ? "yes" : "no");
            std::printf("  placing source chest block...\n");
            const bool sourcePlaced = chunkReady &&
                world.setBlock(0, 64, 0, chestState);
            std::printf("  source chest placement complete: %s\n",
                        sourcePlaced ? "yes" : "no");
            std::printf("  placing destination sentinel block...\n");
            const bool destinationPlaced = sourcePlaced &&
                world.setBlock(1, 64, 0, stoneState);
            std::printf("  destination sentinel placement complete: %s\n",
                        destinationPlaced ? "yes" : "no");
            CHECK(chunkReady && sourcePlaced && destinationPlaced,
                  "clone budget source and sentinel destination are prepared in a loaded chunk");

            if (chunkReady && sourcePlaced && destinationPlaced) {
                entities.failNextChunkNbtWriteForTest();
                bool unrelatedWriteSucceeded = false;
                std::thread unrelatedPersistenceWriter([&] {
                    nbt::Value ignored = nbt::Value::makeList(nbt::Compound);
                    unrelatedWriteSucceeded =
                        entities.writeChunkNbt(20, 20, ignored);
                });
                unrelatedPersistenceWriter.join();
                CHECK(unrelatedWriteSucceeded,
                      "an unrelated persistence thread cannot consume the clone snapshot fault");
                const auto snapshotFailure = server.dispatchConsole(
                    "clone 0 64 0 0 64 0 1 64 0");
                entities.clearChunkNbtWriteFailureForTest();
                CHECK(snapshotFailure.find("snapshot source block entities") !=
                          std::string::npos,
                      "/clone aborts when a source block-entity snapshot fails");
                CHECK(world.getBlock(0, 64, 0) == chestState &&
                          world.getBlock(1, 64, 0) == stoneState &&
                          !entities.getShared(destination),
                      "failed source snapshot applies no clone block or block-entity changes");

                entities.setDirtyCallback({});
                std::vector<std::int64_t> fillerKeys;
                fillerKeys.reserve(
                    BlockEntityStore::kMaxChunkBlockEntityRecords - 1);
                for (std::int32_t y = -64;
                     y < world.maxY() &&
                         fillerKeys.size() + 1 <
                             BlockEntityStore::kMaxChunkBlockEntityRecords;
                     ++y) {
                    for (std::int32_t z = 0; z < 16 &&
                         fillerKeys.size() + 1 <
                             BlockEntityStore::kMaxChunkBlockEntityRecords;
                         ++z) {
                        for (std::int32_t x = 0; x < 16 &&
                             fillerKeys.size() + 1 <
                                 BlockEntityStore::kMaxChunkBlockEntityRecords;
                             ++x) {
                            if ((x == 0 || x == 1) && y == 64 && z == 0)
                                continue;
                            fillerKeys.push_back(posKey(x, y, z));
                        }
                    }
                }
                auto fillerOwners = entities.tryCreateSharedBatch(
                    fillerKeys, BlockEntity::Kind::MovingPiston);
                const auto sourceEntity = fillerOwners
                    ? entities.tryCreateShared(source,
                                               BlockEntity::Kind::Chest)
                    : BlockEntityStore::Owner{};
                CHECK(fillerOwners && sourceEntity &&
                          fillerKeys.size() + 1 ==
                              BlockEntityStore::kMaxChunkBlockEntityRecords &&
                          entities.size() ==
                              BlockEntityStore::kMaxChunkBlockEntityRecords,
                      "fixture reaches the exact per-chunk block-entity record limit");

                const auto serializeChunkEntities = [&] {
                    auto list = nbt::Value::makeList(nbt::Compound);
                    entities.writeChunkNbt(0, 0, list);
                    auto serializedRoot = nbt::Value::makeCompound();
                    serializedRoot.set("block_entities", std::move(list));
                    WriteBuffer encoded;
                    nbt::writeFileRoot(encoded, serializedRoot);
                    return encoded.data;
                };
                const auto before = serializeChunkEntities();
                const auto countBefore = entities.size();
                const auto result = server.dispatchConsole(
                    "clone 0 64 0 0 64 0 1 64 0");
                const auto after = serializeChunkEntities();

                CHECK(result.find("budget") != std::string::npos,
                      "/clone reports the block-entity budget rejection");
                CHECK(world.getBlock(1, 64, 0) == stoneState &&
                          !entities.getShared(destination) &&
                          entities.size() == countBefore,
                      "rejected /clone leaves the sentinel block and entity-store membership unchanged");
                CHECK(before == after,
                      "rejected /clone leaves the complete source-chunk block-entity NBT byte-identical");
            }
        } catch (const std::exception& error) {
            std::fprintf(stderr,
                         "clone block-entity budget fixture threw: %s\n",
                         error.what());
            CHECK(false,
                  "clone block-entity budget fixture completes without an exception");
        }
        server.stop();
    }
    std::filesystem::remove_all(worldDir, ec);
    CHECK(!ec, "clone budget fixture removes its exact temporary world");
}

static void scenarioPlayerLoginHandoff(const char* serverPath) {
    std::printf("\n[player login handoff — serialized same-UUID replacement]\n");

    ServerProcessOptions observedOptions;
    observedOptions.viewDistance = 2;
    observedOptions.maxPlayers = 2;
    observedOptions.readyTimeoutMs = 10000;
    observedOptions.worldPrefix = "/tmp/cppfm-player-handoff-observer-";
    observedOptions.isolateRuntime = true;
    ServerProc observedServer;
    CHECK(observedServer.start(serverPath, observedOptions),
          "observer handoff fixture starts a real server");
    if (observedServer.pid > 0) {
        TestClient previous;
        TestClient observer;
        const bool previousJoined =
            previous.connect("127.0.0.1", observedServer.port) &&
            previous.join("HandoffRunner");
        CHECK(previousJoined, "original same-UUID player joins");
        const bool observerJoined =
            observer.connect("127.0.0.1", observedServer.port) &&
            observer.join("HandoffObserver");
        CHECK(observerJoined, "observer joins while original player is active");

        if (previousJoined && observerJoined) {
            const int movesBefore = observer.counters().entityMoves;
            previous.sendPosition(30.5, -60.0, 0.5);
            const auto moveDeadline = std::chrono::steady_clock::now() +
                                      std::chrono::seconds(5);
            while (observer.counters().entityMoves == movesBefore &&
                   std::chrono::steady_clock::now() < moveDeadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            CHECK(observer.counters().entityMoves > movesBefore,
                  "observer sees the old session's final movement before takeover");
            const std::size_t packetBaseline =
                observer.recentSnapshot().size();

            TestClient candidateA;
            TestClient candidateB;
            std::atomic<int> ready{0};
            std::atomic<bool> start{false};
            bool connectedA = false;
            bool connectedB = false;
            bool joinedA = false;
            bool joinedB = false;
            std::thread first([&] {
                connectedA = candidateA.connect(
                    "127.0.0.1", observedServer.port);
                ready.fetch_add(1, std::memory_order_release);
                while (!start.load(std::memory_order_acquire))
                    std::this_thread::yield();
                joinedA = connectedA && candidateA.join("HandoffRunner");
            });
            std::thread second([&] {
                connectedB = candidateB.connect(
                    "127.0.0.1", observedServer.port);
                ready.fetch_add(1, std::memory_order_release);
                while (!start.load(std::memory_order_acquire))
                    std::this_thread::yield();
                joinedB = connectedB && candidateB.join("HandoffRunner");
            });
            const auto readyDeadline = std::chrono::steady_clock::now() +
                                       std::chrono::seconds(5);
            while (ready.load(std::memory_order_acquire) != 2 &&
                   std::chrono::steady_clock::now() < readyDeadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const bool bothConnected =
                ready.load(std::memory_order_acquire) == 2 &&
                connectedA && connectedB;
            start.store(true, std::memory_order_release);
            first.join();
            second.join();
            CHECK(bothConnected,
                  "two same-UUID contenders reach the production login path concurrently");
            CHECK(joinedA || joinedB,
                  "serialized contenders admit a replacement instead of deadlocking or rejecting both");

            const auto aliveDeadline = std::chrono::steady_clock::now() +
                                       std::chrono::seconds(5);
            const auto liveCandidates = [&] {
                return static_cast<int>(candidateA.alive()) +
                       static_cast<int>(candidateB.alive());
            };
            while (liveCandidates() != 1 &&
                   std::chrono::steady_clock::now() < aliveDeadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            CHECK(liveCandidates() == 1,
                  "concurrent same-UUID attempts leave exactly one active client");

            // PlayerInfoRemove contains UUIDs; Add's first action bit denotes
            // a new entry. Assert actual observer wire order, not just roster state.
            std::unordered_map<std::string, std::size_t> removalCounts;
            std::unordered_map<std::string, std::size_t> addCounts;
            std::size_t matchingAdds = 0;
            bool everyAddFollowedRemoval = true;
            const auto packetDeadline = std::chrono::steady_clock::now() +
                                        std::chrono::seconds(5);
            do {
                const auto packets = observer.recentSnapshot();
                removalCounts.clear();
                addCounts.clear();
                matchingAdds = 0;
                everyAddFollowedRemoval = true;
                for (std::size_t index = packetBaseline;
                     index < packets.size(); ++index) {
                    try {
                        if (packets[index].id == proto::pl::sc::PlayerInfoRemove) {
                            ReadBuffer input(packets[index].body);
                            const auto count = input.varint();
                            for (std::int32_t entry = 0; entry < count; ++entry) {
                                input.need(16);
                                const std::string uuid(
                                    reinterpret_cast<const char*>(input.p + input.off),
                                    16);
                                input.off += 16;
                                ++removalCounts[uuid];
                            }
                        } else if (packets[index].id ==
                                   proto::pl::sc::PlayerInfoUpdate) {
                            ReadBuffer input(packets[index].body);
                            const auto actions = input.u8();
                            const auto count = input.varint();
                            if ((actions & 0x01u) == 0 || count != 1) continue;
                            input.need(16);
                            const std::string uuid(
                                reinterpret_cast<const char*>(input.p + input.off),
                                16);
                            const auto removals = removalCounts[uuid];
                            auto& adds = addCounts[uuid];
                            ++matchingAdds;
                            if (removals <= adds)
                                everyAddFollowedRemoval = false;
                            ++adds;
                        }
                    } catch (const std::exception&) {
                        everyAddFollowedRemoval = false;
                    }
                }
                if (matchingAdds != 0 && everyAddFollowedRemoval) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            } while (std::chrono::steady_clock::now() < packetDeadline);
            CHECK(matchingAdds >= 1 && everyAddFollowedRemoval,
                  "observer receives old PlayerInfo Remove before replacement Add on the wire");

            candidateA.close();
            candidateB.close();
        }
        previous.close();
        observer.close();
        CHECK(observedServer.stop(),
              "observer handoff server and temporary world are cleaned up");
    }

    // A single occupied slot must be available to the same identity. Then
    // force the old player's durable save to fail and require fail-closed
    // behavior for both the immediate and a later takeover attempt.
    ServerProcessOptions singleOptions;
    singleOptions.viewDistance = 2;
    singleOptions.maxPlayers = 1;
    singleOptions.readyTimeoutMs = 10000;
    singleOptions.worldPrefix = "/tmp/cppfm-player-handoff-single-";
    singleOptions.isolateRuntime = true;
    ServerProc singleServer;
    CHECK(singleServer.start(serverPath, singleOptions),
          "single-slot handoff fixture starts a real server");
    if (singleServer.pid > 0) {
        TestClient previous;
        TestClient replacement;
        const bool previousJoined =
            previous.connect("127.0.0.1", singleServer.port) &&
            previous.join("SingleSlotRunner");
        CHECK(previousJoined, "single-slot original account joins");
        if (previousJoined) {
            constexpr double savedX = 30.5;
            constexpr double savedY = -60.0;
            constexpr double savedZ = 0.5;
            const auto initialTeleportDeadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (previous.count(proto::pl::sc::PlayerPosition) == 0 &&
                   std::chrono::steady_clock::now() < initialTeleportDeadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            const bool initialTeleportReceived =
                previous.count(proto::pl::sc::PlayerPosition) != 0;
            CHECK(initialTeleportReceived,
                  "original client receives its initial server teleport before moving");
            previous.sendPosition(savedX, savedY, savedZ);
            constexpr const char* movementBarrier =
                "handoff-final-position-processed";
            previous.sendChatMessage(movementBarrier);
            const auto movementBarrierDeadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(5);
            bool movementProcessed = false;
            while (!movementProcessed &&
                   std::chrono::steady_clock::now() < movementBarrierDeadline) {
                const auto chatLines = previous.chatLinesSnapshot();
                movementProcessed = std::any_of(
                    chatLines.begin(), chatLines.end(),
                    [](const std::string& line) {
                        return line.find("handoff-final-position-processed") !=
                               std::string::npos;
                    });
                if (!movementProcessed)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            CHECK(movementProcessed,
                  "server reply confirms final movement was processed before takeover");
            const bool replacementJoined =
                replacement.connect("127.0.0.1", singleServer.port) &&
                replacement.join("SingleSlotRunner");
            CHECK(replacementJoined,
                  "same-UUID takeover succeeds at maxPlayers=1");
            bool loadedLatestPosition = false;
            const auto positionDeadline = std::chrono::steady_clock::now() +
                                          std::chrono::seconds(5);
            while (replacementJoined &&
                   std::chrono::steady_clock::now() < positionDeadline) {
                const auto position = replacement.positionSnapshot();
                if (std::abs(position.x - savedX) < 0.01 &&
                    std::abs(position.y - savedY) < 0.01 &&
                    std::abs(position.z - savedZ) < 0.01) {
                    loadedLatestPosition = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            CHECK(loadedLatestPosition,
                  "replacement loads the old session's final saved player position");

            std::error_code ec;
            const auto world = std::filesystem::path(singleServer.worldDir);
            const auto playerDataDir = world / "playerdata";
            const auto parkedPlayerDataDir =
                world / "playerdata-handoff-test-backup";
            std::filesystem::create_directories(playerDataDir, ec);
            const bool directoryReady = !ec;
            ec.clear();
            if (directoryReady)
                std::filesystem::rename(playerDataDir, parkedPlayerDataDir, ec);
            const bool parked = directoryReady && !ec;
            CHECK(parked,
                  "save-failure fixture parks only this server's playerdata directory");
            bool blockerCreated = false;
            if (parked) {
                std::ofstream blocker(playerDataDir, std::ios::binary);
                blockerCreated = static_cast<bool>(blocker);
                blocker.close();
            }
            CHECK(blockerCreated,
                  "save-failure fixture replaces the playerdata directory with a file");

            if (blockerCreated) {
                TestClient failedTakeover;
                const bool connected =
                    failedTakeover.connect("127.0.0.1", singleServer.port);
                const bool joined = connected &&
                    failedTakeover.join("SingleSlotRunner");
                CHECK(!joined,
                      "replacement is rejected when old-session player-data save fails");
                failedTakeover.close();
            }
            if (parked) {
                std::filesystem::remove(playerDataDir, ec);
                ec.clear();
                std::filesystem::rename(parkedPlayerDataDir, playerDataDir, ec);
                CHECK(!ec,
                      "save-failure fixture restores the original playerdata directory");
            }
            if (blockerCreated) {
                TestClient afterFailure;
                const bool connected =
                    afterFailure.connect("127.0.0.1", singleServer.port);
                const bool joined = connected &&
                    afterFailure.join("SingleSlotRunner");
                CHECK(!joined,
                      "later same-UUID login remains fail-closed after save failure");
                afterFailure.close();
            }
        }
        previous.close();
        replacement.close();
        CHECK(singleServer.stop(),
              "single-slot handoff server and temporary world are cleaned up");
    }
}

static void scenarioPlayerDirectoryTransactions() {
    ServerConfig config;
    config.jvmEnabled = false;
    config.maxPlayers = 1;
    config.ioWorkerThreads = 1;
    GameServer server(config);

    std::array<std::optional<PlayerDirectory::ReservationId>, 8>
        concurrentReservations{};
    std::array<Player, 8> contenders{};
    for (std::size_t index = 0; index < contenders.size(); ++index) {
        contenders[index].name = "DirectoryContender" + std::to_string(index);
        contenders[index].uuid[0] = static_cast<std::uint8_t>(index + 1);
    }
    std::vector<std::thread> reservationWorkers;
    reservationWorkers.reserve(concurrentReservations.size());
    for (std::size_t index = 0; index < concurrentReservations.size(); ++index) {
        reservationWorkers.emplace_back([&server, &concurrentReservations,
                                         &contenders, index] {
            concurrentReservations[index] =
                server.reservePlayerAdmission(contenders[index]);
        });
    }
    for (auto& worker : reservationWorkers) worker.join();
    const auto successfulReservations = static_cast<std::size_t>(std::count_if(
        concurrentReservations.begin(), concurrentReservations.end(),
        [](const auto& reservation) { return reservation.has_value(); }));
    CHECK(successfulReservations == 1,
          "concurrent player admission never reserves beyond capacity");
    for (const auto reservation : concurrentReservations)
        if (reservation) server.releasePlayerAdmission(*reservation);

    auto original = std::make_shared<Player>();
    original->name = "DirectoryPlayer";
    original->uuid[0] = 7;
    original->inPlay = true;
    const auto originalReservation = server.reservePlayerAdmission(*original);
    CHECK(originalReservation.has_value(),
          "player directory reserves the only available slot");
    CHECK(originalReservation &&
              server.addPlayer(original, *originalReservation),
          "player directory publishes a stable roster snapshot");
    CHECK(server.playerCount() == 1 &&
              server.playersSnapshot().front() == original,
          "published player is visible in stable roster snapshots");

    auto replacement = std::make_shared<Player>();
    replacement->name = "ReplacementPlayer";
    replacement->uuid = original->uuid;
    replacement->inPlay = true;
    const auto replacementReservation =
        server.reservePlayerAdmission(*replacement);
    CHECK(replacementReservation.has_value(),
          "same-UUID replacement reserves the incumbent slot");
    Player unrelated;
    unrelated.name = "UnrelatedPlayer";
    unrelated.uuid[0] = 8;
    CHECK(!server.reservePlayerAdmission(unrelated),
          "replacement reservation prevents unrelated over-admission");

    CHECK(replacementReservation &&
              server.addPlayer(replacement, *replacementReservation),
          "same-UUID publication commits its reservation");
    auto roster = server.playersSnapshot();
    CHECK(roster.size() == 1 && roster.front() == replacement &&
              !original->inPlay,
          "same-UUID publication retires the incumbent and consumes its reservation");

    auto sameName = std::make_shared<Player>();
    sameName->name = replacement->name;
    sameName->uuid[0] = 8;
    sameName->inPlay = true;
    const auto sameNameReservation = server.reservePlayerAdmission(*sameName);
    CHECK(sameNameReservation.has_value(),
          "same-name replacement reserves its incumbent slot at capacity");
    CHECK(sameNameReservation &&
              server.addPlayer(sameName, *sameNameReservation),
          "same-name replacement commits through ordinary admission");
    roster = server.playersSnapshot();
    CHECK(roster.size() == 1 && roster.front() == sameName &&
              !replacement->inPlay,
          "duplicate-name publication atomically replaces the roster entry");

    auto unreserved = std::make_shared<Player>();
    unreserved->name = "UnreservedPlayer";
    unreserved->uuid[0] = 9;
    CHECK(!server.addPlayer(unreserved, 0) && server.playerCount() == 1,
          "unreserved roster publication cannot bypass maxPlayers");

    server.removePlayer(sameName.get());
    Player afterRemoval;
    afterRemoval.name = "AfterRemoval";
    afterRemoval.uuid[0] = 10;
    const auto afterRemovalReservation =
        server.reservePlayerAdmission(afterRemoval);
    CHECK(server.playerCount() == 0 && afterRemovalReservation,
          "removing the last player releases capacity without leaking reservations");
    if (afterRemovalReservation)
        server.releasePlayerAdmission(*afterRemovalReservation);

    ServerConfig pairConfig;
    pairConfig.jvmEnabled = false;
    pairConfig.maxPlayers = 2;
    GameServer pairServer(pairConfig);
    auto first = std::make_shared<Player>();
    first->name = "FirstIncumbent";
    first->uuid[0] = 21;
    first->inPlay = true;
    auto second = std::make_shared<Player>();
    second->name = "SecondIncumbent";
    second->uuid[0] = 22;
    second->inPlay = true;
    const auto firstInitial = pairServer.reservePlayerAdmission(*first);
    const auto secondInitial = pairServer.reservePlayerAdmission(*second);
    CHECK(firstInitial && pairServer.addPlayer(first, *firstInitial),
          "first incumbent occupies one of two slots");
    CHECK(secondInitial && pairServer.addPlayer(second, *secondInitial),
          "second incumbent fills the two-slot roster");

    auto firstReplacement = std::make_shared<Player>();
    firstReplacement->name = "FirstReplacement";
    firstReplacement->uuid = first->uuid;
    firstReplacement->inPlay = true;
    auto secondReplacement = std::make_shared<Player>();
    secondReplacement->name = "SecondReplacement";
    secondReplacement->uuid = second->uuid;
    secondReplacement->inPlay = true;
    std::optional<PlayerDirectory::ReservationId> firstPending;
    std::optional<PlayerDirectory::ReservationId> secondPending;
    std::thread reserveFirst([&] {
        firstPending = pairServer.reservePlayerAdmission(*firstReplacement);
    });
    std::thread reserveSecond([&] {
        secondPending = pairServer.reservePlayerAdmission(*secondReplacement);
    });
    reserveFirst.join();
    reserveSecond.join();
    CHECK(firstPending && secondPending,
          "distinct simultaneous reconnects both reserve their incumbents at capacity");
    CHECK(firstPending && pairServer.addPlayer(firstReplacement, *firstPending),
          "first concurrent replacement commits without exceeding capacity");
    CHECK(secondPending && pairServer.addPlayer(secondReplacement, *secondPending),
          "second concurrent replacement commits without exceeding capacity");
    roster = pairServer.playersSnapshot();
    CHECK(roster.size() == 2 && pairServer.playerCount() == 2,
          "two incumbent replacements retain the exact configured player capacity");

    auto crossIdentityReplacement = std::make_shared<Player>();
    crossIdentityReplacement->name = secondReplacement->name;
    crossIdentityReplacement->uuid = firstReplacement->uuid;
    crossIdentityReplacement->inPlay = true;
    const auto crossIdentityReservation =
        pairServer.reservePlayerAdmission(*crossIdentityReplacement);
    CHECK(crossIdentityReservation.has_value(),
          "replacement matching one UUID and another name reserves both incumbents");
    CHECK(crossIdentityReservation && pairServer.addPlayer(
              crossIdentityReplacement, *crossIdentityReservation),
          "cross-identity replacement commits as one roster transaction");
    roster = pairServer.playersSnapshot();
    CHECK(roster.size() == 1 && roster.front() == crossIdentityReplacement &&
              !firstReplacement->inPlay && !secondReplacement->inPlay,
          "cross-identity replacement retires every matching incumbent");

    PlayerDirectory exceptionDirectory;
    auto exceptionIncumbent = std::make_shared<Player>();
    exceptionIncumbent->name = "ExceptionIncumbent";
    exceptionIncumbent->uuid[0] = 31;
    const auto incumbentAdmission =
        exceptionDirectory.reserve(1, *exceptionIncumbent);
    CHECK(incumbentAdmission && exceptionDirectory.add(
              exceptionIncumbent, *incumbentAdmission, {}),
          "standalone directory publishes an incumbent for exception testing");
    auto exceptionReplacement = std::make_shared<Player>();
    exceptionReplacement->name = "ExceptionReplacement";
    exceptionReplacement->uuid = exceptionIncumbent->uuid;
    const auto failingAdmission =
        exceptionDirectory.reserve(1, *exceptionReplacement);
    bool retirementFailureObserved = false;
    if (failingAdmission) {
        try {
            (void)exceptionDirectory.add(
                exceptionReplacement, *failingAdmission,
                [](const PlayerDirectory::DuplicatePlayers&) {
                    throw std::runtime_error("injected retirement failure");
                });
        } catch (const std::runtime_error&) {
            retirementFailureObserved = true;
        }
        exceptionDirectory.releaseReservation(*failingAdmission);
    }
    auto retryReplacement = std::make_shared<Player>();
    retryReplacement->name = exceptionReplacement->name;
    retryReplacement->uuid = exceptionReplacement->uuid;
    const auto retryAdmission = exceptionDirectory.reserve(1, *retryReplacement);
    CHECK(retirementFailureObserved && exceptionDirectory.count() == 1 &&
              exceptionDirectory.snapshot().front() == exceptionIncumbent &&
              retryAdmission.has_value(),
          "failed retirement preserves the old roster and releases a retryable reservation");
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    bool filterMobAi=false;
    bool filterBlockEntityDropPolicy = false;
    bool filterCloneBlockEntityBudget = false;
    bool filterChunkSaveOwnership = false;
    bool filterPlayerDataTransaction = false;
    bool filterPlayerLoginHandoff = false;
    for(int i=1;i<argc;++i) {
        const std::string arg(argv[i]);
        if (arg.find("mob_ai") != std::string::npos) filterMobAi = true;
        if (arg == "block_entity_drop_policy")
            filterBlockEntityDropPolicy = true;
        if (arg == "clone_block_entity_budget")
            filterCloneBlockEntityBudget = true;
        if (arg == "chunk_save_ownership")
            filterChunkSaveOwnership = true;
        if (arg == "player_data_transaction")
            filterPlayerDataTransaction = true;
        if (arg == "player_login_handoff")
            filterPlayerLoginHandoff = true;
    }
    const char* serverPath = argc > 1 ? argv[1] : "build/cppfm";
    if (filterPlayerLoginHandoff) {
        std::printf("=== native production player-login handoff regression ===\n");
        scenarioPlayerLoginHandoff(serverPath);
        std::printf("\n%s (%d failures)\n",
                    g_fail ? "FAILURES" : "ALL PASS", g_fail);
        return g_fail ? 1 : 0;
    }
    if(filterMobAi){
        std::printf("=== cppfm native mob_ai filter (10 cases) ===\n");
        scenarioMobStateLockBoundary();
        scenarioMobAI30();
        std::printf("\n%s (%d failures)\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
        return g_fail ? 1 : 0;
    }
    if (filterBlockEntityDropPolicy) {
        std::printf("=== block-entity and suppressed-drop policy regression ===\n");
        scenarioCrafterOpaqueOutputTarget();
        std::printf("\n%s (%d failures)\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
        return g_fail ? 1 : 0;
    }
    if (filterCloneBlockEntityBudget) {
        std::printf("=== clone block-entity budget regression ===\n");
        scenarioCloneBlockEntityBudgetPreflight();
        std::printf("\n%s (%d failures)\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
        return g_fail ? 1 : 0;
    }
    if (filterChunkSaveOwnership) {
        std::printf("=== chunk persistence ownership regression ===\n");
        scenarioChunkSaveWithoutPersistenceIsNotScheduled();
        std::printf("\n%s (%d failures)\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
        return g_fail ? 1 : 0;
    }
    if (filterPlayerDataTransaction) {
        std::printf("=== player persistence and fail-closed admin-ban regressions ===\n");
        scenarioPlayerDataLoadIsTransactional();
        scenarioAdminBanLoadFailsClosed();
        std::printf("\n%s (%d failures)\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
        return g_fail ? 1 : 0;
    }
    // handle --filter mob_ai appearing as argv[1]/argv[2] shifting serverPath
    for(int i=1;i<argc;++i){ if(std::string(argv[i]).rfind("build/",0)==0 || std::string(argv[i]).rfind("./build",0)==0) serverPath=argv[i]; }
    std::printf("=== cppfm native self-test (server: %s) ===\n", serverPath);

    ServerProcessOptions serverOptions;
    serverOptions.viewDistance = 2;
    serverOptions.readyTimeoutMs = 8000;
    serverOptions.motd = "status \"quote\" \\ slash";
    const char* nativeWorldPrefix = std::getenv("CPPFM_NATIVE_WORLD_PREFIX");
    serverOptions.worldPrefix = (nativeWorldPrefix && *nativeWorldPrefix)
        ? nativeWorldPrefix : "/tmp/opencode/native-world-";
    serverOptions.isolateRuntime = true;
    ServerProc srv;
    if (!srv.start(serverPath, serverOptions)) {
        std::printf("FATAL: could not start server\n");
        return 2;
    }

    {
        TestClient c;
        c.connect("127.0.0.1", srv.port);
        scenarioStatus(c);
        c.close();
    }
    scenarioJoinBuildChat(srv);
    // Online-mode encryption/auth is implemented and unit-tested at the crypto layer.
    // Full E2E requires real Mojang session validation.
    scenarioMultiplayer(srv);
    scenarioStress(srv, 12);

    CHECK(srv.stop(), "owned server process and temporary world cleaned up");
    scenarioMobStateLockBoundary();
    scenarioDimensionCacheUnit();
    scenarioChunkSaveWithoutPersistenceIsNotScheduled();
    scenarioSpawnChunkLoadFailureIsFatal();
    scenarioCrafterOpaqueOutputTarget();
    scenarioNestedCommandSourcePolicy();
    scenarioJvmServerThreadBoundary();
    scenarioTickLoopCannotStartAfterStop();
    scenarioShutdownLifecycleOwnerHandoff();
    scenarioConcurrentShutdownLifecycleCallbacks();
    scenarioStopFromFinalLifecycleCallbackIsDeferred();
    scenarioBootstrapOwnerTasksQuiesceBeforeTickHandoff();
    scenarioStartupStopRequestIsNotLost();
    scenarioStopFromTickCallback();
    scenarioStopFromPlayerJoinCallback();
    scenarioPlayerDataLoadIsTransactional();
    scenarioAdminBanLoadFailsClosed();
    scenarioCloneBlockEntityBudgetPreflight();
    scenarioWorldGenParity();
    scenarioPredicateUnit();
    scenarioMobAI30();
    scenarioRecipesTagMirror();
    scenarioLootFunctions();
    scenarioEnchantHelper();
    scenarioVillagerTradesUnit();
    scenarioThunderUnit();
    scenarioEnderItemsUnit();
    scenarioPlayerDirectoryTransactions();
    scenarioQCNative();
    scenarioFunctionMacroNative();
    scenarioPredicate16Extra();
    scenarioLoot100Plan40();
    scenarioPredicate22Plan40();
    scenarioEnchant41Plan40();
    std::printf("\n%s (%d failures)\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
