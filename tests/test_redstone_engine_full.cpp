// test_redstone_engine_full — plan44 G-12: RedstoneEngine verification via public API.
// Policy: every assert drives World + RedstoneEngine (onBlockChanged/tick) and
// reads back block states. No constant-vs-constant checks.
// Coverage spans wire/comparators/repeaters/doors/rails/automation/QC,
// plus piston transaction, save-barrier, bounds, and concurrency behavior.
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <atomic>
#include <string>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "core/ByteBuffer.hpp"
#include "generated/BlockStates.hpp"
#include "generated/ItemIds.hpp"
#include "game/Items.hpp"
#include "game/World.hpp"
#include "game/BlockEntities.hpp"
#include "physics/Redstone.hpp"

using namespace cppfm;

static int g_pass = 0;
static int g_fail = 0;
static int g_total = 0;
static const char* curSection = "";
static constexpr std::size_t kTestBlockEntityNbtBudget = 2048;

static void CHECK(bool cond, const char* name) {
    ++g_total;
    if (cond) { ++g_pass; std::printf("  PASS [%s] %s\n", curSection, name); }
    else { ++g_fail; std::printf("  FAIL [%s] %s\n", curSection, name); }
}
static void CHECK_EQ_INT(int a, int b, const char* name) {
    char buf[512]; std::snprintf(buf, sizeof buf, "%s (expected %d got %d)", name, b, a);
    CHECK(a == b, buf);
}

struct Rig {
    World world;
    RedstoneEngine engine;
    BlockEntityStore bes;
    std::int64_t tick = 0;
    Rig() : world("minecraft:plains", LevelType::Flat, 0), engine(world) {
        engine.setTickRef(&tick);
        engine.setBlockEntityStore(&bes);
    }
    void place(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t st) {
        world.setBlock(x, y, z, st);
        engine.onBlockChanged(x, y, z);
    }
    void step(std::int64_t n = 1) { for (std::int64_t i = 0; i < n; ++i) engine.tick(++tick); }
    int wirePower(std::int32_t x, std::int32_t y, std::int32_t z) const {
        for (auto& [k, v] : gen::propsOf(world.getBlock(x, y, z)))
            if (k == "power") return std::atoi(std::string(v).c_str());
        return -999;
    }
    std::string prop(std::int32_t x, std::int32_t y, std::int32_t z, const char* key) const {
        for (auto& [k, v] : gen::propsOf(world.getBlock(x, y, z)))
            if (k == key) return std::string(v);
        return "<none>";
    }
};
static std::uint16_t stateByName(const char* n) {
    auto b = gen::blockByName(n);
    return b ? static_cast<std::uint16_t>(b->minState) : std::uint16_t{0};
}
static std::uint16_t wireWith(int power) {
    return (std::uint16_t)gen::stateWithPropsList("minecraft:redstone_wire", {{"power", std::to_string(power).c_str()}});
}

// 1. wire attenuation: 15 at source, -1 per block, dies past 15
static void test_wire() {
    curSection = "WIRE";
    std::printf("\n[1] WIRE attenuation (engine flood fill)\n");
    Rig rig;
    for (int i = 1; i <= 16; ++i) rig.world.setBlock(i, 2, 0, wireWith(0));
    rig.place(0, 2, 0, stateByName("minecraft:redstone_block"));
    CHECK_EQ_INT(rig.wirePower(1, 2, 0), 15, "wire d1 from source == 15");
    CHECK_EQ_INT(rig.wirePower(6, 2, 0), 10, "wire 15 -> 5 blocks -> 10 (-1/block)");
    CHECK_EQ_INT(rig.wirePower(16, 2, 0), 0, "wire past 15 blocks unpowered");
    rig.place(0, 2, 0, 0);
    CHECK_EQ_INT(rig.wirePower(1, 2, 0), 0, "removing source clears adjacent wire");
    CHECK_EQ_INT(rig.wirePower(6, 2, 0), 0, "removing source clears the whole component");
    rig.place(16, 2, 0, stateByName("minecraft:redstone_block"));
    CHECK_EQ_INT(rig.wirePower(15, 2, 0), 15, "replacement source powers from the opposite end");
    CHECK_EQ_INT(rig.wirePower(6, 2, 0), 6, "replacement source recomputes attenuation");
}

// comparator helpers: chest/hopper behind comparator facing north (rear z+1),
// readout wire in front (z-1)
static void fillChest(Rig& rig, std::uint32_t item, int countPerSlot, int slots) {
    rig.world.setBlock(0, 2, 1, stateByName("minecraft:chest"));
    auto owner = rig.bes.createShared(posKey(0, 2, 1),
                                     BlockEntity::Kind::Chest);
    std::lock_guard entityLock(*owner->stateMtx);
    BlockEntity& be = *owner;
    for (int i = 0; i < slots; ++i) be.chest.slots[i] = ItemStack::of(item, (std::int16_t)countPerSlot);
}
static void fillHopper(Rig& rig, std::uint32_t item, int countPerSlot, int slots) {
    rig.world.setBlock(0, 2, 1, stateByName("minecraft:hopper"));
    auto owner = rig.bes.createShared(posKey(0, 2, 1),
                                     BlockEntity::Kind::Hopper);
    std::lock_guard entityLock(*owner->stateMtx);
    BlockEntity& be = *owner;
    be.generic.slotCount = 5;
    for (int i = 0; i < slots; ++i) be.generic.slots[i] = ItemStack::of(item, (std::int16_t)countPerSlot);
}

// 2. comparator: empty / partial-16-stack / full / subtract
static void test_comparator() {
    curSection = "COMPARATOR";
    std::printf("\n[2] COMPARATOR analog (container fill + subtract)\n");
    std::uint32_t dia = gen::itemIdByName().find("minecraft:diamond")->second;
    std::uint32_t pearl = gen::itemIdByName().find("minecraft:ender_pearl")->second;
    auto compState = [](const char* mode) {
        return (std::uint16_t)gen::stateWithPropsList("minecraft:comparator",
            {{"facing", "north"}, {"mode", mode}, {"powered", "false"}});
    };
    { // empty chest -> unpowered, front wire 0
        Rig rig;
        rig.place(0, 2, 0, compState("compare"));
        rig.world.setBlock(0, 2, 1, stateByName("minecraft:chest"));
        rig.bes.createShared(posKey(0, 2, 1), BlockEntity::Kind::Chest);
        rig.place(0, 2, -1, wireWith(0));
        rig.engine.onBlockChanged(0, 2, 0);
        rig.step(2);
        CHECK(rig.prop(0, 2, 0, "powered") == "false", "empty chest -> comparator unpowered");
        CHECK_EQ_INT(rig.wirePower(0, 2, -1), 0, "empty chest -> front wire 0");
    }
    { // hopper 16x ender_pearl (16-stack => slot fill 1.0, avg 0.2) -> 3
        Rig rig;
        rig.place(0, 2, 0, compState("compare"));
        fillHopper(rig, pearl, 16, 1);
        rig.place(0, 2, -1, wireWith(0));
        rig.engine.onBlockChanged(0, 2, 0);
        rig.step(2);
        CHECK_EQ_INT(rig.wirePower(0, 2, -1), 3, "hopper 16x ender_pearl (16-stack) => 3");
    }
    { // full chest 27x64 diamond -> 15
        Rig rig;
        rig.place(0, 2, 0, compState("compare"));
        fillChest(rig, dia, 64, 27);
        rig.place(0, 2, -1, wireWith(0));
        rig.engine.onBlockChanged(0, 2, 0);
        rig.step(2);
        CHECK_EQ_INT(rig.wirePower(0, 2, -1), 15, "full chest => 15");
    }
    { // subtract: full chest (15) minus side 15 -> comparator off.
        Rig rig;
        rig.place(0, 2, 0, compState("subtract"));
        fillChest(rig, dia, 64, 27);
        rig.place(0, 2, -1, wireWith(0));
        rig.place(1, 2, 0, wireWith(0));
        rig.place(2, 2, 0, stateByName("minecraft:redstone_block"));
        rig.engine.onBlockChanged(0, 2, 0);
        rig.step(2);
        CHECK(rig.prop(0, 2, 0, "powered") == "false", "subtract full(15) - side(15) => comparator off");
    }
}

// 3. repeater: delay 1-4 ticks (due = now + delay*2), lock blocks
static void test_repeater() {
    curSection = "REPEATER";
    std::printf("\n[3] REPEATER delay + lock\n");
    auto repRig = [](int delay, bool locked) {
        Rig* rig = new Rig();
        rig->tick = 1000;
        auto rep = (std::uint16_t)gen::stateWithPropsList("minecraft:repeater",
            {{"facing", "east"}, {"delay", std::to_string(delay).c_str()},
             {"locked", locked ? "true" : "false"}, {"powered", "false"}});
        rig->place(-2, 2, 0, stateByName("minecraft:redstone_block"));
        rig->place(-1, 2, 0, wireWith(0));
        rig->place(0, 2, 0, rep);
        return rig;
    };
    {
        Rig* rig = repRig(2, false);
        rig->step(3); // now=1003 < due 1004
        CHECK(rig->prop(0, 2, 0, "powered") == "false", "delay2 not yet on at +3t");
        rig->step(1); // now=1004 == due
        CHECK(rig->prop(0, 2, 0, "powered") == "true", "delay2 on at +4t (due=now+delay*2)");
        delete rig;
    }
    {
        Rig* rig = repRig(1, false);
        rig->step(2);
        CHECK(rig->prop(0, 2, 0, "powered") == "true", "delay1 on at +2t");
        delete rig;
    }
    {
        Rig* rig = repRig(1, true);
        rig->step(4);
        CHECK(rig->prop(0, 2, 0, "powered") == "false", "locked repeater stays off");
        delete rig;
    }
}

// 4. lamp + door: binary followers of adjacent power
static void test_lamp_door() {
    curSection = "LAMP_DOOR";
    std::printf("\n[4] LAMP + DOOR followers\n");
    {
        Rig rig;
        auto lamp = (std::uint16_t)gen::stateWithPropsList("minecraft:redstone_lamp", {{"lit", "false"}});
        rig.place(0, 2, 0, lamp);
        CHECK(rig.prop(0, 2, 0, "lit") == "false", "lamp unpowered stays dark");
        rig.place(1, 2, 0, stateByName("minecraft:redstone_block"));
        CHECK(rig.prop(0, 2, 0, "lit") == "true", "lamp adjacent to source lights");
    }
    {
        Rig rig;
        auto lower = (std::uint16_t)gen::stateWithPropsList("minecraft:oak_door",
            {{"facing", "north"}, {"half", "lower"}, {"hinge", "left"}, {"open", "false"}, {"powered", "false"}});
        auto upper = (std::uint16_t)gen::stateWithPropsList("minecraft:oak_door",
            {{"facing", "north"}, {"half", "upper"}, {"hinge", "left"}, {"open", "false"}, {"powered", "false"}});
        rig.world.setBlock(0, 2, 0, lower);
        rig.world.setBlock(0, 3, 0, upper);
        rig.engine.onBlockChanged(0, 2, 0);
        CHECK(rig.prop(0, 2, 0, "open") == "false", "door unpowered closed");
        rig.place(1, 2, 0, stateByName("minecraft:redstone_block"));
        bool bothOpen = rig.prop(0, 2, 0, "open") == "true" && rig.prop(0, 3, 0, "open") == "true";
        CHECK(bothOpen, "door powered: both halves open (vanilla)");
        rig.place(1, 2, 0, 0);
        CHECK(rig.prop(0, 2, 0, "open") == "false", "door unpowered again: closes");
    }
}

// 5. rails: straight / ascending / powered toggle
static void test_rails() {
    curSection = "RAILS";
    std::printf("\n[5] RAIL shapes + powered_rail\n");
    auto railNS = [] {
        return (std::uint16_t)gen::stateWithPropsList("minecraft:rail",
            {{"shape", "north_south"}, {"waterlogged", "false"}});
    };
    {
        Rig rig;
        rig.place(10, 2, 10, railNS());
        rig.place(11, 2, 10, railNS());
        rig.step(1);
        CHECK(rig.prop(10, 2, 10, "shape") == "east_west", "two rails in a row -> east_west");
    }
    {
        Rig rig;
        rig.place(40, 2, 40, railNS());
        rig.place(41, 3, 40, railNS());
        rig.engine.onBlockChanged(40, 2, 40); // diagonal neighbor: needs explicit update
        rig.step(1);
        CHECK(rig.prop(40, 2, 40, "shape") == "ascending_east", "rail with higher neighbor -> ascending_east");
        CHECK(rig.prop(41, 3, 40, "shape") == "ascending_east", "higher neighbor agrees on ascending_east");
    }
    {
        Rig rig;
        rig.place(50, 2, 50, railNS());
        rig.place(51, 2, 50, railNS());
        rig.place(50, 2, 51, railNS());
        rig.engine.onBlockChanged(50, 2, 50);
        CHECK(rig.prop(50, 2, 50, "shape") == "south_east",
              "same-level east+south neighbors form south_east curve");
        rig.place(50, 2, 51, 0);
        CHECK(rig.prop(50, 2, 50, "shape") == "east_west",
              "removing curve neighbor leaves aligned east_west connection");
    }
    {
        Rig rig;
        rig.place(60, 3, 60, railNS());
        rig.place(61, 2, 60, railNS());
        rig.engine.onBlockChanged(60, 3, 60);
        CHECK(rig.prop(60, 3, 60, "shape") == "ascending_west",
              "rail with lower east neighbor rises toward west");
        CHECK(rig.prop(61, 2, 60, "shape") == "ascending_west",
              "lower neighbor agrees on ascending_west");
    }
    {
        Rig rig;
        auto pr = (std::uint16_t)gen::stateWithPropsList("minecraft:powered_rail",
            {{"powered", "false"}, {"shape", "north_south"}, {"waterlogged", "false"}});
        rig.place(30, 2, 30, pr);
        CHECK(rig.prop(30, 2, 30, "powered") == "false", "powered_rail unpowered off");
        rig.place(31, 2, 30, stateByName("minecraft:redstone_block"));
        CHECK(rig.prop(30, 2, 30, "powered") == "true", "powered_rail adjacent to source on");
    }
    for (const char* railName : {"minecraft:powered_rail", "minecraft:detector_rail",
                                 "minecraft:activator_rail"}) {
        Rig rig;
        auto specialRail = (std::uint16_t)gen::stateWithPropsList(railName,
            {{"powered", "false"}, {"shape", "north_south"}, {"waterlogged", "false"}});
        rig.place(70, 2, 70, specialRail);
        rig.place(71, 2, 70, specialRail);
        rig.place(70, 2, 71, specialRail);
        rig.engine.onBlockChanged(70, 2, 70);
        std::string checkName = std::string(railName) + " rejects curves and keeps a straight shape";
        CHECK(rig.prop(70, 2, 70, "shape") == "east_west", checkName.c_str());
    }
}

// 6. dispenser / dropper / hopper: QC arming + container analog path
static void test_dispenser_hopper() {
    curSection = "DISP_HOPPER";
    std::printf("\n[6] DISPENSER / DROPPER / HOPPER engine outputs\n");
    {
        Rig rig;
        rig.place(5, 2, 5, stateByName("minecraft:dispenser"));
        CHECK(!rig.engine.isQuasiPowered(5, 2, 5), "dispenser initially unpowered");
        rig.place(5, 3, 5, stateByName("minecraft:stone"));
        rig.place(6, 3, 5, stateByName("minecraft:redstone_block"));
        CHECK(rig.engine.isQuasiPowered(5, 2, 5), "dispenser QC powered via y+1 (vanilla JE)");
    }
    {
        Rig rig;
        rig.place(7, 2, 7, stateByName("minecraft:dropper"));
        rig.place(7, 3, 7, stateByName("minecraft:stone"));
        rig.place(8, 3, 7, stateByName("minecraft:redstone_block"));
        CHECK(rig.engine.isQuasiPowered(7, 2, 7), "dropper QC powered via y+1 (vanilla JE)");
    }
    { // full hopper (5x64) behind comparator -> 15 (hopper analog path)
        Rig rig;
        std::uint32_t cob = gen::itemIdByName().find("minecraft:cobblestone")->second;
        auto comp = (std::uint16_t)gen::stateWithPropsList("minecraft:comparator",
            {{"facing", "north"}, {"mode", "compare"}, {"powered", "false"}});
        rig.place(0, 2, 0, comp);
        fillHopper(rig, cob, 64, 5);
        rig.place(0, 2, -1, wireWith(0));
        rig.engine.onBlockChanged(0, 2, 0);
        rig.step(2);
        CHECK_EQ_INT(rig.wirePower(0, 2, -1), 15, "full hopper (5x64) => comparator 15");
    }
}

// 7. QC: y+1 powering, direct-vs-QC contrast, removal
static void test_qc() {
    curSection = "QC";
    std::printf("\n[7] QUASI-CONNECTIVITY\n");
    Rig rig;
    auto piston = (std::uint16_t)gen::stateWithPropsList("minecraft:piston",
        {{"facing", "east"}, {"extended", "false"}});
    rig.place(0, 2, 0, piston);
    CHECK(!rig.engine.isQuasiPowered(0, 2, 0), "piston initially not QC powered");
    rig.place(0, 3, 0, stateByName("minecraft:stone"));
    rig.place(1, 3, 0, stateByName("minecraft:redstone_block"));
    CHECK(rig.engine.isQuasiPowered(0, 2, 0), "piston QC powered via y+1 stone");
    CHECK(!rig.engine.isPoweredHere(0, 2, 0), "piston not directly powered (QC-only contrast)");
    rig.place(1, 3, 0, 0);
    CHECK(!rig.engine.isQuasiPowered(0, 2, 0), "piston QC off after source removal");
}

static void test_block_entity_batch_capacity() {
    curSection = "BE BATCH CAPACITY";
    std::printf("\n[8] block-entity batch capacity preflight\n");
    const auto first = posKey(0, 2, 0);
    const auto second = posKey(1, 2, 0);
    const auto otherChunk = posKey(32, 2, 0);

    {
        BlockEntityStore store(-64, 320, 2, 1024, 2,
                               kTestBlockEntityNbtBudget);
        const auto duplicate = store.tryCreateSharedBatch(
            {first, first}, BlockEntity::Kind::MovingPiston);
        CHECK(!duplicate && store.size() == 0,
              "duplicate keys reject the whole batch without records");

        const auto accepted = store.tryCreateSharedBatch(
            {first, second}, BlockEntity::Kind::MovingPiston);
        CHECK(accepted && accepted->size() == 2 && store.size() == 2 &&
                  store.getShared(first) == accepted->at(0) &&
                  store.getShared(second) == accepted->at(1),
              "a fitting batch publishes every owner");

        const auto originalOwner = store.getShared(first);
        const auto existing = store.tryCreateSharedBatch(
            {first, otherChunk}, BlockEntity::Kind::MovingPiston);
        CHECK(!existing && store.size() == 2 &&
                  store.getShared(first) == originalOwner &&
                  !store.getShared(otherChunk),
              "an existing typed key rejects without replacing or partially adding");
    }
    {
        BlockEntityStore store(-64, 320, 1, 1024, 2,
                               kTestBlockEntityNbtBudget);
        const auto overChunk = store.tryCreateSharedBatch(
            {first, second}, BlockEntity::Kind::MovingPiston);
        CHECK(!overChunk && store.size() == 0,
              "aggregate per-chunk cap rejects a multi-record batch atomically");
    }
    {
        BlockEntityStore store(-64, 320, 1, 1024, 1,
                               kTestBlockEntityNbtBudget);
        const auto overStore = store.tryCreateSharedBatch(
            {first, otherChunk}, BlockEntity::Kind::MovingPiston);
        CHECK(!overStore && store.size() == 0,
              "aggregate store cap rejects records across chunks atomically");
    }
    {
        nbt::Value root = nbt::Value::makeCompound();
        nbt::Value entries = nbt::Value::makeList(nbt::Compound);
        nbt::Value opaque = nbt::Value::makeCompound();
        opaque.set("id", nbt::Value::makeString("example:opaque_block_entity"));
        opaque.set("x", nbt::Value::makeInt(0));
        opaque.set("y", nbt::Value::makeInt(2));
        opaque.set("z", nbt::Value::makeInt(0));
        entries.list.push_back(std::move(opaque));
        root.set("block_entities", std::move(entries));

        BlockEntityStore store(-64, 320, 2, 16 * 1024, 2,
                               32 * 1024);
        store.readChunkNbt(root);
        const auto withOpaque = store.tryCreateSharedBatch(
            {first, second}, BlockEntity::Kind::MovingPiston);
        CHECK(!withOpaque && store.size() == 1 &&
                  store.containsAt(0, 2, 0) && !store.getShared(second),
              "an opaque raw key rejects the entire batch without promotion");
    }
}

struct BoundedPistonRig {
    World world;
    BlockEntityStore bes;
    RedstoneEngine engine;
    std::int64_t tick = 0;

    BoundedPistonRig(std::size_t maxChunkRecords,
                     std::size_t maxStoreRecords)
        : world("minecraft:plains", LevelType::Flat, 0),
          bes(-64, 320, maxChunkRecords, 1024, maxStoreRecords,
              kTestBlockEntityNbtBudget),
          engine(world) {
        engine.setTickRef(&tick);
        engine.setBlockEntityStore(&bes);
    }

    void step(std::int64_t count) {
        while (count-- > 0) engine.tick(++tick);
    }
};

static void test_piston_batch_capacity_rejection() {
    curSection = "PISTON BE CAPACITY";
    std::printf("\n[9] piston extension/retraction BE batch budget rejection\n");
    const auto fillerKey = posKey(100, 2, 0);
    {
        BoundedPistonRig rig(2, 2);
        const auto filler = rig.bes.createShared(
            fillerKey, BlockEntity::Kind::Chest);
        const auto piston = static_cast<std::uint16_t>(
            gen::stateWithPropsList("minecraft:piston",
                {{"facing", "east"}, {"extended", "false"}}));
        const auto stone = stateByName("minecraft:stone");
        rig.world.setBlock(0, 2, 0, piston);
        rig.world.setBlock(1, 2, 0, stone);
        rig.world.setBlock(2, 2, 0, stone);
        rig.world.setBlock(0, 3, 0, stateByName("minecraft:stone"));
        rig.world.setBlock(1, 3, 0, stateByName("minecraft:redstone_block"));
        rig.engine.onBlockChanged(0, 3, 0);
        rig.engine.onBlockChanged(1, 3, 0);
        rig.engine.onBlockChanged(0, 2, 0);
        CHECK(rig.engine.pendingCount() > 0,
              "powered piston has a scheduled extension before budget preflight");

        bool tickThrew = false;
        try { rig.step(5); }
        catch (...) { tickThrew = true; }
        CHECK(!tickThrew,
              "extension record-cap exhaustion does not throw from the tick");
        CHECK(rig.world.getBlock(0, 2, 0) == piston &&
                  rig.world.getBlock(1, 2, 0) == stone &&
                  rig.world.getBlock(2, 2, 0) == stone,
              "failed extension leaves piston and all pushed blocks unchanged");
        CHECK(rig.bes.size() == 1 && rig.bes.getShared(fillerKey) == filler &&
                  !rig.bes.getShared(posKey(1, 2, 0)) &&
                  !rig.bes.getShared(posKey(2, 2, 0)),
              "failed extension creates no partial moving-piston BE batch");
        CHECK(rig.engine.pendingCount() == 0,
              "failed extension leaves no delayed piston commit pending");
    }
    {
        BoundedPistonRig rig(1, 1);
        const auto filler = rig.bes.createShared(
            fillerKey, BlockEntity::Kind::Chest);
        const auto piston = static_cast<std::uint16_t>(
            gen::stateWithPropsList("minecraft:piston",
                {{"facing", "east"}, {"extended", "true"}}));
        const auto head = static_cast<std::uint16_t>(
            gen::stateWithPropsList("minecraft:piston_head",
                {{"facing", "east"}, {"type", "normal"},
                 {"short", "false"}}));
        rig.world.setBlock(0, 2, 0, piston);
        rig.world.setBlock(1, 2, 0, head);
        rig.engine.onBlockChanged(0, 2, 0);
        CHECK(rig.engine.pendingCount() > 0,
              "unpowered extended piston has a scheduled retraction");

        bool tickThrew = false;
        try { rig.step(5); }
        catch (...) { tickThrew = true; }
        CHECK(!tickThrew,
              "retraction record-cap exhaustion does not throw from the tick");
        CHECK(rig.world.getBlock(0, 2, 0) == piston &&
                  rig.world.getBlock(1, 2, 0) == head,
              "failed retraction leaves piston and head unchanged");
        CHECK(rig.bes.size() == 1 && rig.bes.getShared(fillerKey) == filler &&
                  !rig.bes.getShared(posKey(1, 2, 0)),
              "failed retraction creates no moving-head BE");
        CHECK(rig.engine.pendingCount() == 0,
              "failed retraction leaves no delayed piston commit pending");
    }
}

static void test_piston_push_preserves_contiguous_blocks() {
    curSection = "PISTON MULTI-BLOCK PUSH";
    std::printf("\n[10] piston multi-block push preserves every block\n");

    BoundedPistonRig rig(8, 8);
    constexpr std::int32_t y = 2;
    const auto piston = static_cast<std::uint16_t>(
        gen::stateWithPropsList("minecraft:piston",
            {{"facing", "east"}, {"extended", "false"}}));
    const auto extendedPiston = static_cast<std::uint16_t>(
        gen::stateWithPropsList("minecraft:piston",
            {{"facing", "east"}, {"extended", "true"}}));
    const auto head = static_cast<std::uint16_t>(
        gen::stateWithPropsList("minecraft:piston_head",
            {{"facing", "east"}, {"type", "normal"},
             {"short", "false"}}));
    const auto stone = stateByName("minecraft:stone");
    const auto dirt = stateByName("minecraft:dirt");
    const auto gold = stateByName("minecraft:gold_block");
    const auto redstone = stateByName("minecraft:redstone_block");

    rig.world.setBlock(0, y, 0, piston);
    rig.world.setBlock(1, y, 0, stone);
    rig.world.setBlock(2, y, 0, dirt);
    rig.world.setBlock(3, y, 0, gold);
    rig.world.setBlock(0, y + 1, 0, redstone);
    rig.engine.onBlockChanged(0, y + 1, 0);
    rig.engine.onBlockChanged(0, y, 0);
    rig.step(6);

    CHECK(rig.world.getBlock(0, y, 0) == extendedPiston &&
              rig.world.getBlock(1, y, 0) == head,
          "multi-block extension leaves the piston and head in place");
    CHECK(rig.world.getBlock(2, y, 0) == stone &&
              rig.world.getBlock(3, y, 0) == dirt &&
              rig.world.getBlock(4, y, 0) == gold &&
              rig.world.getBlock(5, y, 0) == 0,
          "multi-block extension shifts every source block one cell without loss");
    CHECK(rig.engine.pendingCount() == 0,
          "multi-block extension drains its scheduled and delayed work");
}

static void test_pending_piston_conflicts_preserve_new_edits() {
    curSection = "PISTON CONFLICT CANCELLATION";
    std::printf("\n[11] delayed piston edits cancel without overwriting newer blocks\n");

    const auto stone = stateByName("minecraft:stone");
    const auto sentinel = stateByName("minecraft:diamond_block");
    const auto movingPiston = stateByName("minecraft:moving_piston");
    const auto redstoneBlock = stateByName("minecraft:redstone_block");
    const auto piston = static_cast<std::uint16_t>(
        gen::stateWithPropsList("minecraft:piston",
            {{"facing", "east"}, {"extended", "false"}}));
    const auto extendedPiston = static_cast<std::uint16_t>(
        gen::stateWithPropsList("minecraft:piston",
            {{"facing", "east"}, {"extended", "true"}}));

    const auto startPush = [&](Rig& rig) {
        rig.world.setBlock(0, 2, 0, piston);
        rig.world.setBlock(1, 2, 0, stone);
        rig.world.setBlock(0, 3, 0, redstoneBlock);
        rig.engine.onBlockChanged(0, 3, 0);
        rig.engine.onBlockChanged(0, 2, 0);
        rig.step(3);
        return rig.engine.pendingCount() > 0 &&
            rig.world.getBlock(0, 2, 0) == extendedPiston &&
            rig.world.getBlock(1, 2, 0) == movingPiston &&
            rig.bes.hasCurrentKind(posKey(1, 2, 0),
                                   BlockEntity::Kind::MovingPiston);
    };

    {
        Rig rig;
        const bool animationStarted = startPush(rig);
        CHECK(animationStarted,
              "normal piston push stages its moving block before the delayed commit");
        if (animationStarted) {
            rig.world.setBlock(2, 2, 0, sentinel);
            rig.step(3);
            CHECK(rig.world.getBlock(2, 2, 0) == sentinel,
                  "a new block in the piston landing cell survives delayed completion");
            CHECK(rig.world.getBlock(1, 2, 0) == stone &&
                      rig.world.getBlock(0, 2, 0) == piston,
                  "cancelling a conflicted push restores only its still-owned source and piston");
            CHECK(!rig.bes.getShared(posKey(1, 2, 0)) &&
                      rig.engine.pendingCount() == 0,
                  "cancelled push releases its transient owner and pending record");
        }
    }

    {
        Rig rig;
        const bool animationStarted = startPush(rig);
        CHECK(animationStarted,
              "owner-retype fixture starts a normal piston push");
        if (animationStarted) {
            const auto chest = stateByName("minecraft:chest");
            const auto diamond = gen::itemIdByName().find("minecraft:diamond");
            const auto owner = rig.bes.getShared(posKey(1, 2, 0));
            const auto diamondId = diamond == gen::itemIdByName().end()
                ? std::uint16_t{0} : diamond->second;
            bool retyped = false;
            if (chest != 0 && diamondId != 0 && owner) {
                // Reproduce BlockEntityStore's in-place identity-preserving
                // type change while a delayed piston still holds that owner.
                rig.world.setBlock(1, 2, 0, chest);
                const auto chestOwner = rig.bes.tryCreateShared(
                    posKey(1, 2, 0), BlockEntity::Kind::Chest);
                if (chestOwner == owner) {
                    std::lock_guard ownerLock(*owner->stateMtx);
                    owner->chest.slots[0] =
                        ItemStack::of(diamondId);
                    retyped = owner->kind == BlockEntity::Kind::Chest;
                }
            }
            CHECK(retyped,
                  "moving-piston owner can be retyped in place with new chest data");
            rig.step(3);
            bool chestDataSurvived = false;
            if (owner && diamondId != 0) {
                std::lock_guard ownerLock(*owner->stateMtx);
                chestDataSurvived = owner->kind == BlockEntity::Kind::Chest &&
                    owner->chest.slots[0].itemId == diamondId &&
                    owner->chest.slots[0].count == 1;
            }
            CHECK(rig.world.getBlock(1, 2, 0) == chest &&
                      rig.bes.getShared(posKey(1, 2, 0)) == owner &&
                      chestDataSurvived,
                  "stale piston completion preserves an in-place retyped chest owner and its contents");
            CHECK(rig.engine.pendingCount() == 0,
                  "owner-retype conflict cancels the delayed piston record");
        }
    }

    {
        Rig rig;
        const bool animationStarted = startPush(rig);
        CHECK(animationStarted,
              "placeholder-conflict fixture starts a normal piston push");
        if (animationStarted) {
            rig.world.setBlock(1, 2, 0, sentinel);
            // The production block-replacement hook removes the transient
            // owner when a moving placeholder is replaced.
            rig.bes.remove(posKey(1, 2, 0));
            rig.engine.flushPendingPistons(0, 0);
            CHECK(rig.world.getBlock(1, 2, 0) == sentinel,
                  "save-barrier cancellation preserves a replacement at the moving placeholder");
            CHECK(rig.world.getBlock(0, 2, 0) == piston &&
                      rig.engine.pendingCount() == 0,
                  "save barrier drains stale work without committing or retaining the piston action");
            CHECK(!rig.bes.getShared(posKey(1, 2, 0)),
                  "replaced moving placeholder has no stale block-entity owner");
        }
    }

    {
        Rig rig;
        const auto stickyPiston = static_cast<std::uint16_t>(
            gen::stateWithPropsList("minecraft:sticky_piston",
                {{"facing", "east"}, {"extended", "true"}}));
        const auto stickyHead = static_cast<std::uint16_t>(
            gen::stateWithPropsList("minecraft:piston_head",
                {{"facing", "east"}, {"type", "sticky"},
                 {"short", "false"}}));
        rig.world.setBlock(0, 2, 0, stickyPiston);
        rig.world.setBlock(1, 2, 0, stickyHead);
        rig.world.setBlock(2, 2, 0, stone);
        rig.engine.onBlockChanged(0, 2, 0);
        rig.step(3);
        const bool retractionStarted = rig.engine.pendingCount() > 0 &&
            rig.world.getBlock(1, 2, 0) == movingPiston &&
            rig.bes.hasCurrentKind(posKey(1, 2, 0),
                                   BlockEntity::Kind::MovingPiston);
        CHECK(retractionStarted,
              "sticky retraction stages a moving head while retaining the pull source");
        if (retractionStarted) {
            rig.world.setBlock(1, 2, 0, sentinel);
            rig.bes.remove(posKey(1, 2, 0));
            rig.step(3);
            CHECK(rig.world.getBlock(1, 2, 0) == sentinel,
                  "sticky retraction does not overwrite an edit at the pull destination");
            CHECK(rig.world.getBlock(2, 2, 0) == stone &&
                      rig.engine.pendingCount() == 0,
                  "cancelled sticky pull leaves its source untouched and removes stale work");
            CHECK(!rig.bes.getShared(posKey(1, 2, 0)),
                  "cancelled sticky retraction retains no obsolete moving-head owner");
        }
    }
}

static void test_piston_build_height_and_flush_dirty_chunks() {
    curSection = "PISTON BOUNDS / SAVE BARRIER";
    std::printf("\n[12] piston build-height rejection and cross-chunk flush dirtiness\n");

    const auto stone = stateByName("minecraft:stone");
    const auto redstoneBlock = stateByName("minecraft:redstone_block");
    auto rejectBoundaryPush = [&](int y, int pushedY, const char* facing,
                                  const char* boundary) {
        BoundedPistonRig rig(4, 4);
        const auto piston = static_cast<std::uint16_t>(
            gen::stateWithPropsList("minecraft:piston",
                {{"facing", facing}, {"extended", "false"}}));
        rig.world.setBlock(0, y, 0, piston);
        rig.world.setBlock(0, pushedY, 0, stone);
        rig.world.setBlock(1, y, 0, redstoneBlock);
        rig.engine.onBlockChanged(1, y, 0);
        rig.engine.onBlockChanged(0, y, 0);
        rig.step(6);

        const std::string prefix = std::string(boundary) + " boundary: ";
        CHECK(rig.world.getBlock(0, y, 0) == piston,
              (prefix + "piston remains unextended").c_str());
        CHECK(rig.world.getBlock(0, pushedY, 0) == stone,
              (prefix + "source block is not lost").c_str());
        CHECK(rig.bes.size() == 0,
              (prefix + "no moving-piston BE is published").c_str());
        CHECK(rig.engine.pendingCount() == 0,
              (prefix + "no partial delayed commit remains").c_str());
    };

    // The source itself is legal, but its pushed destination would be Y=320.
    rejectBoundaryPush(318, 319, "up", "upper build-height");
    // The source itself is legal, but its pushed destination would be Y=-65.
    rejectBoundaryPush(-63, -64, "down", "lower build-height");

    {
        BoundedPistonRig rig(8, 8);
        std::vector<std::pair<std::int32_t, std::int32_t>> notifiedDirty;
        std::vector<std::pair<std::int32_t, std::int32_t>> worldEditNotifications;
        rig.engine.setPistonChunkDirtyFn(
            [&](std::int32_t cx, std::int32_t cz) {
                notifiedDirty.emplace_back(cx, cz);
            });
        rig.world.setOnEdit([&](std::int32_t cx, std::int32_t cz) {
            worldEditNotifications.emplace_back(cx, cz);
        });

        const auto piston = static_cast<std::uint16_t>(
            gen::stateWithPropsList("minecraft:piston",
                {{"facing", "east"}, {"extended", "false"}}));
        const auto extendedPiston = static_cast<std::uint16_t>(
            gen::stateWithPropsList("minecraft:piston",
                {{"facing", "east"}, {"extended", "true"}}));
        rig.world.setBlock(14, 2, 0, piston);
        rig.world.setBlock(15, 2, 0, stone);
        rig.world.setBlock(14, 3, 0, redstoneBlock);
        rig.engine.onBlockChanged(14, 3, 0);
        rig.engine.onBlockChanged(14, 2, 0);
        rig.step(3);

        CHECK(rig.engine.pendingCount() > 0,
              "cross-chunk piston has a delayed final commit");
        CHECK(rig.world.getBlock(14, 2, 0) == extendedPiston,
              "cross-chunk piston enters its normal extension phase");
        notifiedDirty.clear();
        worldEditNotifications.clear();

        // Simulate final persistence shutdown: its World edit hook has already
        // been detached when the save barrier forces this cross-chunk commit.
        rig.world.setOnEdit({});
        rig.engine.flushPendingPistons(0, 0);

        const auto head = static_cast<std::uint16_t>(
            gen::stateWithPropsList("minecraft:piston_head",
                {{"facing", "east"}, {"type", "normal"},
                 {"short", "false"}}));
        CHECK(rig.world.getBlock(15, 2, 0) == head &&
                  rig.world.getBlock(16, 2, 0) == stone,
              "save-barrier flush commits the block across the chunk edge");
        CHECK(worldEditNotifications.empty(),
              "test has detached World edit hook during final commit");
        CHECK(notifiedDirty.size() == 2 &&
                  std::find(notifiedDirty.begin(), notifiedDirty.end(),
                            std::pair<std::int32_t, std::int32_t>{0, 0}) !=
                      notifiedDirty.end() &&
                  std::find(notifiedDirty.begin(), notifiedDirty.end(),
                            std::pair<std::int32_t, std::int32_t>{1, 0}) !=
                      notifiedDirty.end(),
              "explicit piston dirty callback reports both mutated chunks");
        CHECK(rig.engine.pendingCount() == 0,
              "successful save-barrier commit is fully drained");
    }
}

static void test_sticky_piston_retraction_overlapping_sources() {
    curSection = "STICKY PISTON RETRACTION ORDER";
    std::printf("\n[14] sticky retraction clears all sources before overlapping destinations\n");

    Rig rig;
    constexpr int y = 2;
    const auto stickyPiston = static_cast<std::uint16_t>(
        gen::stateWithPropsList("minecraft:sticky_piston",
            {{"facing", "east"}, {"extended", "true"}}));
    const auto stickyHead = static_cast<std::uint16_t>(
        gen::stateWithPropsList("minecraft:piston_head",
            {{"facing", "east"}, {"type", "sticky"},
             {"short", "false"}}));
    const auto slime = stateByName("minecraft:slime_block");
    const auto stone = stateByName("minecraft:stone");
    rig.world.setBlock(0, y, 0, stickyPiston);
    rig.world.setBlock(1, y, 0, stickyHead);

    // Two slime branches rejoin around a pair of ordinary blocks. BFS reaches
    // the farther stone first; its destination is the nearer stone's source.
    for (const auto [x, blockY] : std::vector<std::pair<int, int>>{
             {2, y}, {3, y}, {2, y + 1}, {1, y + 1},
             {3, y + 1}, {1, y + 2}, {3, y + 2},
             {1, y + 3}, {3, y + 3}, {1, y + 4}})
        rig.world.setBlock(x, blockY, 0, slime);
    rig.world.setBlock(3, y + 4, 0, stone);
    rig.world.setBlock(2, y + 4, 0, stone);

    rig.engine.onBlockChanged(0, y, 0);
    rig.step(3);
    const bool staged = rig.engine.pendingCount() > 0;
    CHECK(staged, "sticky piston stages retraction of a branched pull group");
    if (!staged) return;

    rig.step(3);
    CHECK(rig.world.getBlock(1, y + 4, 0) == stone &&
              rig.world.getBlock(2, y + 4, 0) == stone &&
              rig.world.getBlock(3, y + 4, 0) == 0,
          "overlapping pull destinations preserve both stones without source-clear loss");
    CHECK(rig.engine.pendingCount() == 0,
          "branched sticky retraction drains its delayed commit");
}

static void test_piston_notification_failure_keeps_delayed_commit() {
    curSection = "PISTON NOTIFICATION FAILURE";
    std::printf("\n[15] notification failures do not orphan committed piston animation\n");

    Rig rig;
    constexpr std::int32_t y = 2;
    const auto piston = static_cast<std::uint16_t>(
        gen::stateWithPropsList("minecraft:piston",
            {{"facing", "east"}, {"extended", "false"}}));
    const auto redstoneBlock = stateByName("minecraft:redstone_block");
    const auto stone = stateByName("minecraft:stone");
    const auto movingPiston = stateByName("minecraft:moving_piston");
    const auto extendedPiston = static_cast<std::uint16_t>(
        gen::stateWithPropsList("minecraft:piston",
            {{"facing", "east"}, {"extended", "true"}}));
    const auto head = static_cast<std::uint16_t>(
        gen::stateWithPropsList("minecraft:piston_head",
            {{"facing", "east"}, {"type", "normal"},
             {"short", "false"}}));

    rig.engine.setPistonChunkDirtyFn(
        [](std::int32_t, std::int32_t) {
            throw std::runtime_error("fixture dirty callback failure");
        });
    rig.engine.setBroadcastFn(
        [](std::int32_t, std::int32_t, std::int32_t, std::uint16_t) {
            throw std::runtime_error("fixture broadcast failure");
        });
    rig.world.setBlock(0, y, 0, piston);
    rig.world.setBlock(1, y, 0, stone);
    rig.world.setBlock(0, y + 1, 0, redstoneBlock);
    rig.engine.onBlockChanged(0, y + 1, 0);
    rig.engine.onBlockChanged(0, y, 0);

    bool tickThrew = false;
    try { rig.step(3); }
    catch (...) { tickThrew = true; }
    CHECK(!tickThrew,
          "throwing dirty and broadcast notifications are isolated from the tick");
    CHECK(rig.engine.pendingCount() == 1 &&
              rig.world.getBlock(0, y, 0) == extendedPiston &&
              rig.world.getBlock(1, y, 0) == movingPiston,
          "committed extension retains its delayed record despite notification failures");

    try { rig.step(3); }
    catch (...) { tickThrew = true; }
    CHECK(!tickThrew && rig.engine.pendingCount() == 0 &&
              rig.world.getBlock(0, y, 0) == extendedPiston &&
              rig.world.getBlock(1, y, 0) == head &&
              rig.world.getBlock(2, y, 0) == stone,
          "saved delayed record completes the piston move despite later notification failures");
}

static void test_piston_flush_drains_scheduled_cross_chunk_action() {
    curSection = "SCHEDULED PISTON SAVE BARRIER";
    std::printf("\n[13] save barrier drains a not-yet-due cross-chunk piston action\n");

    BoundedPistonRig rig(4, 4);
    std::vector<std::pair<std::int32_t, std::int32_t>> dirtyChunks;
    rig.engine.setPistonChunkDirtyFn(
        [&](std::int32_t chunkX, std::int32_t chunkZ) {
            dirtyChunks.emplace_back(chunkX, chunkZ);
        });

    constexpr std::int32_t pistonX = 14;
    constexpr std::int32_t y = 2;
    const auto piston = static_cast<std::uint16_t>(
        gen::stateWithPropsList("minecraft:piston",
            {{"facing", "east"}, {"extended", "false"}}));
    const auto extendedPiston = static_cast<std::uint16_t>(
        gen::stateWithPropsList("minecraft:piston",
            {{"facing", "east"}, {"extended", "true"}}));
    const auto head = static_cast<std::uint16_t>(
        gen::stateWithPropsList("minecraft:piston_head",
            {{"facing", "east"}, {"type", "normal"},
             {"short", "false"}}));
    const auto stone = stateByName("minecraft:stone");

    rig.world.setBlock(pistonX, y, 0, piston);
    rig.world.setBlock(pistonX + 1, y, 0, stone);
    rig.world.setBlock(pistonX, y + 1, 0,
                       stateByName("minecraft:redstone_block"));
    rig.engine.onBlockChanged(pistonX, y + 1, 0);
    rig.engine.onBlockChanged(pistonX, y, 0);

    CHECK(rig.engine.pendingCount() > 0 &&
              rig.world.getBlock(pistonX, y, 0) == piston &&
              rig.world.getBlock(pistonX + 1, y, 0) == stone,
          "action remains scheduled and unchanged before its due tick");

    // Chunk (1,0) contains the pushed block's destination, while the piston
    // itself and its scheduled event are in chunk (0,0).
    rig.engine.flushPendingPistons(1, 0);

    CHECK(rig.world.getBlock(pistonX, y, 0) == extendedPiston &&
              rig.world.getBlock(pistonX + 1, y, 0) == head &&
              rig.world.getBlock(pistonX + 2, y, 0) == stone,
          "destination-chunk barrier completes the cross-chunk piston move");
    CHECK(rig.engine.pendingCount() == 0 && rig.bes.size() == 0,
          "no scheduled action or piston animation remains for the snapshot");
    CHECK(std::find(dirtyChunks.begin(), dirtyChunks.end(),
                    std::pair<std::int32_t, std::int32_t>{0, 0}) !=
                  dirtyChunks.end() &&
              std::find(dirtyChunks.begin(), dirtyChunks.end(),
                        std::pair<std::int32_t, std::int32_t>{1, 0}) !=
                  dirtyChunks.end(),
          "cross-chunk start and destination are both reported dirty");
}

static void test_concurrent_public_notifications() {
    curSection = "CONCURRENCY";
    std::printf("\n[14] concurrent callback notifications and redstone ticks\n");
    Rig rig;
    constexpr int notificationCount = 250;
    std::atomic<bool> go{false};

    std::thread callbackThread([&] {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        for (int i = 0; i < notificationCount; ++i)
            rig.engine.onBlockChanged(0, 2, 0);
    });
    std::thread tickThread([&] {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        for (std::int64_t now = 1; now <= notificationCount; ++now)
            rig.engine.tick(now);
    });
    go.store(true, std::memory_order_release);
    callbackThread.join();
    tickThread.join();

    CHECK_EQ_INT(static_cast<int>(rig.engine.pendingCount()), 0,
                 "concurrent onBlockChanged/tick drains all notifications");
}

int main() {
    std::printf("=== test_redstone_engine_full — plan44 G-12 (engine via public API) ===\n");
    test_wire();
    test_comparator();
    test_repeater();
    test_lamp_door();
    test_rails();
    test_dispenser_hopper();
    test_qc();
    test_block_entity_batch_capacity();
    test_piston_batch_capacity_rejection();
    test_piston_push_preserves_contiguous_blocks();
    test_pending_piston_conflicts_preserve_new_edits();
    test_piston_build_height_and_flush_dirty_chunks();
    test_piston_flush_drains_scheduled_cross_chunk_action();
    test_sticky_piston_retraction_overlapping_sources();
    test_piston_notification_failure_keeps_delayed_commit();
    test_concurrent_public_notifications();
    std::printf("\n=== REDSTONE_ENGINE_FULL: %d PASS %d FAIL %d TOTAL ===\n", g_pass, g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
