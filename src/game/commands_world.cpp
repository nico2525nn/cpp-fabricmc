#include "CommandModule.hpp"
#include "BlockEntityDrops.hpp"
#include "BlockStateTraits.hpp"
#include "BlockPredicate.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <functional>
#include <iterator>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cppfm {
namespace {

using block_predicate::ParsedBlockArgument;
using block_predicate::ParsedBlockPredicate;
using block_predicate::blockMatchesPredicate;
using block_predicate::parseBlockArgument;
using block_predicate::parseBlockPredicate;
using block_predicate::validateBlockPredicate;

} // namespace

void GameServer::initWorldCommands() {
    auto time = CommandNode::literal("time");
    addTimeSetSubcommands(time);
    addTimeQueryAndAddSubcommands(time);
    commands_.root->then(time);

    initGameRuleCommand();
    initForceLoadCommand();
    initSetBlockCommand();
    initWeatherCommand();

    auto worldBorder = CommandNode::literal("worldborder");
    addWorldBorderSizeSubcommand(worldBorder);
    addWorldBorderManagementSubcommands(worldBorder);
    commands_.root->then(worldBorder);

    initDifficultyCommand();
    initFillCommand();
    initCloneCommand();
    auto locate = CommandNode::literal("locate");
    initLocateStructureCommand(locate);
    initLocateBiomeCommand(locate);
    initLocatePoiCommand(locate);
    commands_.root->then(locate);
    initPlaceCommand();
    initSpreadPlayersCommand();
    initEnchantCommand();
    initSetWorldSpawnCommand();
    initDefaultGameModeCommand();
    initJigsawGenerateCommand();
}

void GameServer::addTimeSetSubcommands(const brigadier::NodePtr& time) {
    auto set = CommandNode::literal("set");
        // Named values are literals, not a greedy string argument. A string
        // parser also accepts numeric input, so registering it before the
        // integer branch made `/time set 6000` select the named-value handler
        // and fail without trying the valid numeric sibling.
        const auto addNamedTime = [this, &set](const char* name,
                                                std::int64_t ticks) {
            auto named = CommandNode::literal(name);
            named->executable = true;
            named->action = [this, name, ticks](CommandContext&) {
                setTimeOfDay(ticks);
                broadcastSystemText(msg::kGray + "Time set to " + name);
                return 1;
            };
            set->then(named);
        };
        addNamedTime("day", 1000);
        addNamedTime("noon", 6000);
        addNamedTime("night", 13000);
        addNamedTime("midnight", 18000);
        auto ticks = CommandNode::argument("ticks", args::integer(0, 24000));
        ticks->executable = true;
        ticks->action = [this](CommandContext& c) {
            setTimeOfDay(c.arg("ticks").asInt());
            Player* src = static_cast<Player*>(c.source.player);
            sendFeedback(src, "Set the time to " +
                         std::to_string(c.arg("ticks").asInt()));
            return 1;
        };
    set->then(ticks);
    time->then(set);
}

void GameServer::initGameRuleCommand() {
    auto& d = commands_;
    {
        auto gr = CommandNode::literal("gamerule");
        auto rule = CommandNode::argument("rule", args::stringWord());
        rule->executable = true;
        rule->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
            return gamerules_.allKeys();
        };
        rule->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string r = c.arg("rule").asStr();
            if (!gamerules_.contains(r)) { sendFeedback(src, (msg::kRed + "Unknown gamerule: " + r)); return 0; }
            std::string cur = gamerules_.get(r);
            sendFeedback(src, (msg::kGray + r + " = " + cur));
            return 1;
        };
        auto value = CommandNode::argument("value", args::stringWord());
        value->executable = true;
        value->suggestions = [this](brigadier::StringReader& reader, brigadier::ParseCtx&) {
            // W18 polish: suggest true/false for Boolean, numeric hints for Int Peek already-typed rule prefix: try to infer via token
            std::string token = reader.canRead() ? reader.readUnquotedString() : std::string();
            // fallback: offer both use last parsed rule if available; brigadier context would have it, but we approximate Offer boolean
            // choices; int rules also accept true/false as invalid but hint numbers
            std::vector<std::string> opts = {"true","false"};
            // also suggest common int values for int rules
            opts.push_back("0"); opts.push_back("1"); opts.push_back("10");
            return opts;
        };
        value->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string r = c.arg("rule").asStr();
            const std::string v = c.arg("value").asStr();
            std::string err;
            if (!gamerules_.setValidated(r, v, &err)) {
                sendFeedback(src, (msg::kRed + err));
                return 0;
            }
            broadcastSystemText((msg::kGray + "Gamerule " + r + " is now " + v));
            return 1;
        };
        rule->then(value);
        gr->then(rule);
        d.root->then(gr);
    }
}

void GameServer::initForceLoadCommand() {
    auto& d = commands_;
    {
        auto fl = CommandNode::literal("forceload");
        // query
        auto flQuery = CommandNode::literal("query");
        flQuery->executable = true;
        flQuery->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            World& targetWorld = worldForCommand(c.source);
            auto keys = targetWorld.forcedChunkKeys();
            if (keys.empty()) { sendFeedback(src, "No forced chunks"); return 0; }
            std::string out="Forced chunks:";
            for(auto k: keys){
                auto [cx, cz] = chunkKeyDecode(k);
                out += " [" + std::to_string(cx) + "," + std::to_string(cz) + "]";
            }
            sendFeedback(src, out);
            return (int)keys.size();
        };
        fl->then(flQuery);
        // remove all
        auto flRemove = CommandNode::literal("remove");
        auto flRemoveAll = CommandNode::literal("all");
        flRemoveAll->executable = true;
        flRemoveAll->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            World& targetWorld = worldForCommand(c.source);
            auto keys = targetWorld.forcedChunkKeys();
            for(auto k: keys){
                auto [cx, cz] = chunkKeyDecode(k);
                targetWorld.setChunkForced(cx,cz,false);
            }
            sendFeedback(src, "Removed all forced chunks (" + std::to_string(keys.size()) + ")");
            return (int)keys.size();
        };
        flRemove->then(flRemoveAll);
        // add <x> <z> and remove <x> <z>
        auto flAdd = CommandNode::literal("add");
        auto addX = CommandNode::argument("x", args::integer(INT32_MIN, INT32_MAX));
        auto addZ = CommandNode::argument("z", args::integer(INT32_MIN, INT32_MAX));
        addZ->executable = true;
        addZ->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            int cx = c.arg("x").asInt();
            int cz = c.arg("z").asInt();
            // vanilla forceload uses chunk coords directly; support block pos via >>4 fallback if large? keep chunk coords
            bool ok = worldForCommand(c.source).setChunkForced(cx,cz,true);
            if (!ok) { sendFeedback(src, "Chunk [" + std::to_string(cx)+","+std::to_string(cz)+"] already forced"); return 0; }
            sendFeedback(src, "Added chunk [" + std::to_string(cx)+","+std::to_string(cz)+"]");
            return 1;
        };
        auto remX = CommandNode::argument("x", args::integer(INT32_MIN, INT32_MAX));
        auto remZ = CommandNode::argument("z", args::integer(INT32_MIN, INT32_MAX));
        remZ->executable = true;
        remZ->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            int cx = c.arg("x").asInt();
            int cz = c.arg("z").asInt();
            bool ok = worldForCommand(c.source).setChunkForced(cx,cz,false);
            if (!ok) { sendFeedback(src, "Chunk [" + std::to_string(cx)+","+std::to_string(cz)+"] not forced"); return 0; }
            sendFeedback(src, "Removed chunk [" + std::to_string(cx)+","+std::to_string(cz)+"]");
            return 1;
        };
        addX->then(addZ);
        flAdd->then(addX);
        remX->then(remZ);
        flRemove->then(remX);
        fl->then(flAdd);
        fl->then(flRemove);
        // default executable query (bare /forceload)
        fl->executable = true;
        fl->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            auto keys = worldForCommand(c.source).forcedChunkKeys();
            sendFeedback(src, "Forced chunks: " + std::to_string(keys.size()));
            return (int)keys.size();
        };
        d.root->then(fl);
    }
}

void GameServer::initSetBlockCommand() {
    auto& d = commands_;
    {
        auto sb = CommandNode::literal("setblock");
        auto pos = CommandNode::argument("pos", args::blockPos());
        auto block = CommandNode::argument("block", args::blockStateArg());
        block->executable = true;
        block->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
            std::vector<std::string> v;
            v.reserve(gen::kBlocks.size());
            for (auto& e : gen::kBlocks) v.emplace_back(std::string(e.name));
            // add a few with state examples for tab testing
            v.push_back("minecraft:oak_stairs[facing=north,half=top]");
            v.push_back("minecraft:stone");
            return v;
        };
        block->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto p = c.arg("pos").asBlockPos();
            const auto parsed = parseBlockArgument(c.arg("block").asStr());
            if (parsed.hasBlockEntityNbt) {
                sendFeedback(src,
                    "Block-entity NBT is not supported by /setblock");
                return 0;
            }
            const auto dimension = commandDimension(c.source);
            World& targetWorld = worldFor(dimension);
            if (!targetWorld.verticalGeometry().containsY(p.y)) {
                sendFeedback(src, "Cannot set a block outside the world's build height");
                return 0;
            }
            if (!targetWorld.setBlock(p.x, p.y, p.z, parsed.state)) {
                sendFeedback(src, "Failed to set the block at " +
                             std::to_string(p.x) + ", " +
                             std::to_string(p.y) + ", " +
                             std::to_string(p.z));
                return 0;
            }
            broadcastBlockChangeFor(dimension, p.x, p.y, p.z, parsed.state);
            sendFeedback(src, "Changed the block at " + std::to_string(p.x) +
                         ", " + std::to_string(p.y) + ", " +
                         std::to_string(p.z));
            return 1;
        };
        pos->then(block);
        sb->then(pos);
        d.root->then(sb);
    }
}

void GameServer::initWeatherCommand() {
    auto weather = CommandNode::literal("weather");
    auto kind = CommandNode::argument("weatherKind", args::stringWord());
    kind->executable = true;
    kind->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&) {
        return std::vector<std::string>{"clear", "rain", "thunder"};
    };
    kind->action = [this](CommandContext& c) {
        const auto weatherName = c.arg("weatherKind").asStr();
        if (weatherName != "clear" && weatherName != "rain" &&
            weatherName != "thunder")
            throw std::runtime_error(
                "Unknown weather '" + weatherName +
                "' (expected clear, rain or thunder)");
        const auto duration = weatherName == "clear"
            ? 6000LL * 20 : (weatherName == "thunder" ? 3000LL : 6000LL) * 20;
        setWeather(weatherName == "clear" ? Weather::Clear : Weather::Rain,
                   duration);
        broadcastSystemText(msg::kGray + "Weather set to " + weatherName);
        return 1;
    };

    auto duration = CommandNode::argument(
        "weatherDuration", args::integer(0, 1000000));
    duration->executable = true;
    duration->action = [this](CommandContext& c) {
        Player* src = static_cast<Player*>(c.source.player);
        const auto weatherName = c.arg("weatherKind").asStr();
        if (weatherName != "clear" && weatherName != "rain" &&
            weatherName != "thunder")
            throw std::runtime_error(
                "Unknown weather '" + weatherName +
                "' (expected clear, rain or thunder)");
        const auto durationTicks =
            static_cast<std::int64_t>(c.arg("weatherDuration").asInt()) * 20;
        setWeather(weatherName == "clear" ? Weather::Clear : Weather::Rain,
                   durationTicks);
        sendFeedback(src, "Set weather to " + weatherName + " for " +
                         std::to_string(c.arg("weatherDuration").asInt()) +
                         "s");
        return 1;
    };
    kind->then(duration);
    weather->then(kind);
    commands_.root->then(weather);
}

void GameServer::addWorldBorderSizeSubcommand(
    const brigadier::NodePtr& wb) {
    auto size = CommandNode::literal("size");
    auto diameter = CommandNode::argument(
        "diameter", args::floatArg(1.f, 1000000.f));
    diameter->executable = true;
    diameter->action = [this](CommandContext& context) {
        auto* persistence = persistenceFor(0);
        if (!persistence) return 0;
        const auto border = persistence->worldBorderSnapshot();
        persistence->setWorldBorder(context.arg("diameter").asDouble(),
                                    border.centerX, border.centerZ);
        broadcastWorldBorder();
        return 1;
    };
    size->then(diameter);
    wb->then(size);
}

void GameServer::initDifficultyCommand() {
    auto& d = commands_;
    {
        auto diff = CommandNode::literal("difficulty");
        auto lvl = CommandNode::argument("level", args::stringWord());
        lvl->executable = true;
        lvl->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&) {
            return std::vector<std::string>{"peaceful", "easy", "normal",
                                            "hard"};
        };
        lvl->action = [this](CommandContext& c) {
            const std::string lv = c.arg("level").asStr();
            // anything else (e.g. "impossible") must error, not succeed.
            if (lv != "peaceful" && lv != "easy" && lv != "normal" && lv != "hard")
                throw std::runtime_error("Unknown difficulty '" + lv +
                    "' (expected peaceful, easy, normal or hard)");
            difficulty_ = lv;
            WriteBuffer b;
            b.i8(difficulty_ == "peaceful" ? 0 : difficulty_ == "easy" ? 1 :
                 difficulty_ == "hard" ? 3 : 2);
            b.boolean(false);
            broadcastPacketExcept(nullptr, proto::pl::sc::ChangeDifficulty, b);
            broadcastSystemText((msg::kGray + "Difficulty set to " + difficulty_));
            return 1;
        };
        diff->then(lvl);
        d.root->then(diff);
    }
}

void GameServer::initFillCommand() {
    auto& d = commands_;
    {
        auto fill = CommandNode::literal("fill");
        auto from = CommandNode::argument("from", args::blockPos());
        auto to = CommandNode::argument("to", args::blockPos());
        auto block = CommandNode::argument("block", args::blockStateArg());
        const auto blockSuggestions = [](brigadier::StringReader&,
                                         brigadier::ParseCtx&) {
            std::vector<std::string> v;
            v.reserve(gen::kBlocks.size());
            for (auto& e : gen::kBlocks) v.emplace_back(std::string(e.name));
            return v;
        };
        block->suggestions = blockSuggestions;
        auto executeFill = [this](CommandContext& c, std::string modeName,
                                  std::optional<std::string> filterName) {
            Player* src = static_cast<Player*>(c.source.player);
            LootContext playerLootContext;
            const LootContext* lootContext = nullptr;
            if (src) {
                std::lock_guard playerLock(src->stateMtx);
                playerLootContext.hasEntityContext = true;
                playerLootContext.entityOnFire = src->fireTicks > 0;
                lootContext = &playerLootContext;
            }
            if (modeName.empty()) modeName = "replace";
            if (modeName != "replace" && modeName != "destroy" &&
                modeName != "keep" && modeName != "hollow" &&
                modeName != "outline") {
                sendFeedback(src, "Unknown /fill mode: " + modeName);
                return 0;
            }
            if (filterName && modeName != "replace") {
                sendFeedback(src,
                    "A block filter is only valid with the replace mode");
                return 0;
            }
            const auto replacement = parseBlockArgument(
                c.arg("block").asStr());
            if (replacement.hasBlockEntityNbt) {
                sendFeedback(src,
                    "Block-entity NBT is not supported by /fill");
                return 0;
            }
            std::optional<ParsedBlockPredicate> filterPredicate;
            if (filterName) {
                filterPredicate = parseBlockPredicate(*filterName);
                validateBlockPredicate(*filterPredicate, datapackManager_.tagManager);
            }

            const auto p1 = c.arg("from").asBlockPos();
            const auto p2 = c.arg("to").asBlockPos();
            const auto minX = std::min(p1.x, p2.x);
            const auto maxX = std::max(p1.x, p2.x);
            const auto minY = std::min(p1.y, p2.y);
            const auto maxY = std::max(p1.y, p2.y);
            const auto minZ = std::min(p1.z, p2.z);
            const auto maxZ = std::max(p1.z, p2.z);
            constexpr std::int64_t maxVolume = 32768;
            const auto widthX = static_cast<std::int64_t>(maxX) - minX + 1;
            const auto widthY = static_cast<std::int64_t>(maxY) - minY + 1;
            const auto widthZ = static_cast<std::int64_t>(maxZ) - minZ + 1;
            if (widthX > maxVolume || widthY > maxVolume ||
                widthZ > maxVolume)
                throw std::runtime_error("fill volume too large (max 32768)");
            const std::int64_t volume = widthX * widthY * widthZ;
            if (volume > maxVolume)
                throw std::runtime_error("fill volume too large (max 32768, got " +
                                         std::to_string(volume) + ")");
            const auto dimension = commandDimension(c.source);
            World& targetWorld = worldFor(dimension);
            const auto geometry = targetWorld.verticalGeometry();
            if (!geometry.containsY(minY) || !geometry.containsY(maxY)) {
                sendFeedback(src, "Cannot fill outside the world's build height");
                return 0;
            }

            for (std::int64_t chunkX = minX >> 4;
                 chunkX <= (maxX >> 4); ++chunkX) {
                for (std::int64_t chunkZ = minZ >> 4;
                     chunkZ <= (maxZ >> 4); ++chunkZ) {
                    if (!targetWorld.generateChunkIfMissing(
                            static_cast<std::int32_t>(chunkX),
                            static_cast<std::int32_t>(chunkZ))) {
                        sendFeedback(src,
                            "Could not fill blocks; a chunk could not be loaded");
                        return 0;
                    }
                }
            }

            std::vector<World::BlockUpdate> updates;
            std::vector<std::uint16_t> replacedStates;
            updates.reserve(static_cast<std::size_t>(volume));
            replacedStates.reserve(static_cast<std::size_t>(volume));
            for (std::int64_t y = minY; y <= maxY; ++y)
                for (std::int64_t z = minZ; z <= maxZ; ++z)
                    for (std::int64_t x = minX; x <= maxX; ++x) {
                        const auto blockX = static_cast<std::int32_t>(x);
                        const auto blockY = static_cast<std::int32_t>(y);
                        const auto blockZ = static_cast<std::int32_t>(z);
                        const auto previous = targetWorld.getBlock(
                            blockX, blockY, blockZ);
                        if (modeName == "keep" &&
                            !isAirBlockState(previous)) continue;
                        if (filterPredicate && !blockMatchesPredicate(
                                *filterPredicate, previous, datapackManager_.tagManager))
                            continue;
                        const bool boundary = blockX == minX ||
                            blockX == maxX || blockY == minY ||
                            blockY == maxY || blockZ == minZ ||
                            blockZ == maxZ;
                        if (modeName == "outline" && !boundary) continue;
                        const auto nextState = static_cast<std::uint16_t>(
                            modeName == "hollow" && !boundary
                                ? 0 : replacement.state);
                        if (previous == nextState) continue;
                        updates.push_back(
                            {blockX, blockY, blockZ, nextState});
                        replacedStates.push_back(previous);
                    }

            struct PlannedDrop {
                std::int32_t x;
                std::int32_t y;
                std::int32_t z;
                ItemStack stack;
            };
            std::vector<PlannedDrop> destroyDrops;
            // Vanilla's destroy mode removes blocks regardless of
            // doTileDrops, but only plans and spawns their loot while the
            // gamerule is enabled. Capture the rule once so every block in
            // this atomic fill observes the same drop policy.
            const bool collectDestroyDrops = modeName == "destroy" &&
                gameRules().getBool("doTileDrops");
            if (collectDestroyDrops) {
                const auto& blockEntities = blockEntitiesFor(dimension);
                static const std::unordered_map<std::string, std::string>
                    blockDropOverrides{
                        {"minecraft:grass_block", "minecraft:dirt"},
                        {"minecraft:stone", "minecraft:cobblestone"},
                    };
                std::unordered_map<
                    std::int64_t,
                    std::unordered_map<std::int64_t, nbt::Value>>
                    serializedEntitiesByChunk;
                const auto findSerializedEntity = [&blockEntities,
                                                   &serializedEntitiesByChunk](
                    const World::BlockUpdate& block) -> const nbt::Value* {
                    const auto chunk = chunkKey(block.x >> 4, block.z >> 4);
                    auto cached = serializedEntitiesByChunk.find(chunk);
                    if (cached == serializedEntitiesByChunk.end()) {
                        nbt::Value list = nbt::Value::makeList(nbt::Compound);
                        blockEntities.writeChunkNbt(block.x >> 4,
                                                    block.z >> 4, list);
                        std::unordered_map<std::int64_t, nbt::Value> entries;
                        entries.reserve(list.list.size());
                        for (auto& entity : list.list) {
                            const auto* x = entity.get("x");
                            const auto* y = entity.get("y");
                            const auto* z = entity.get("z");
                            if (!x || !y || !z || x->tag != nbt::Int ||
                                y->tag != nbt::Int || z->tag != nbt::Int)
                                continue;
                            entries.try_emplace(posKey(x->i, y->i, z->i),
                                                std::move(entity));
                        }
                        cached = serializedEntitiesByChunk.emplace(
                            chunk, std::move(entries)).first;
                    }
                    const auto entity = cached->second.find(
                        posKey(block.x, block.y, block.z));
                    return entity == cached->second.end()
                        ? nullptr : &entity->second;
                };
                const auto rejectUnsafeDrop = [&]() {
                    sendFeedback(src,
                        "Cannot use /fill destroy: block-entity data or item components cannot be represented safely as drops");
                    return 0;
                };
                for (std::size_t i = 0; i < updates.size(); ++i) {
                    const auto& update = updates[i];
                    const auto oldState = replacedStates[i];
                    const auto* oldBlock = gen::blockByState(oldState);
                    const auto appendDrop = [&](const ItemStack& stack) {
                        if (!stack.empty())
                            destroyDrops.push_back(
                                {update.x, update.y, update.z, stack});
                    };
                    bool shulkerContentsPackaged = false;

                    if (blockEntities.containsAt(update.x, update.y,
                                                 update.z)) {
                        // Snapshot serialized data before taking the entity
                        // state lock below. Shulker drops package that NBT as
                        // an item component; serializing the containing chunk
                        // while holding this entity's non-recursive mutex
                        // would deadlock on the same owner.
                        const auto* savedEntity =
                            findSerializedEntity(update);
                        const auto entity = blockEntities.getAt(
                            update.x, update.y, update.z);
                        // Unknown serialized owners (including decorated
                        // pots, whose one-stack/sherd payload is not a
                        // LootableContainer inventory) remain opaque. Never
                        // erase one through /fill destroy until its distinct
                        // item-drop semantics have an explicit transaction.
                        if (!entity || !savedEntity)
                            return rejectUnsafeDrop();
                        std::lock_guard entityLock(*entity->stateMtx);
                        const auto* pendingLoot =
                            savedEntity->get("LootTable");
                        if (pendingLoot &&
                            entity->kind != BlockEntity::Kind::ShulkerBox &&
                            !pendingLootBlockEntityMatchesKind(
                                *savedEntity, entity->kind))
                            return rejectUnsafeDrop();
                        const ItemStack* slots = nullptr;
                        std::size_t slotCount = 0;
                        const auto appendPendingLoot = [&]() {
                            if (slotCount == 0) return false;
                            const auto plan = planPendingContainerLootDrops(
                                *savedEntity, datapackManager_.lootTables, lootContext,
                                slotCount);
                            if (plan.status !=
                                PendingLootDropPlanStatus::Ready)
                                return false;
                            for (const auto& stack : plan.items)
                                appendDrop(stack);
                            return true;
                        };
                        switch (entity->kind) {
                        case BlockEntity::Kind::Chest:
                        case BlockEntity::Kind::Barrel:
                            slots = entity->chest.slots;
                            slotCount = ChestData::kSlots;
                            if (pendingLoot && !appendPendingLoot())
                                return rejectUnsafeDrop();
                            break;
                        case BlockEntity::Kind::Furnace:
                            slots = entity->furnace.slots;
                            slotCount = std::size(entity->furnace.slots);
                            break;
                        case BlockEntity::Kind::Hopper:
                        case BlockEntity::Kind::Dispenser:
                        case BlockEntity::Kind::Dropper:
                            slots = entity->generic.slots;
                            slotCount = entity->kind ==
                                    BlockEntity::Kind::Hopper
                                ? 5 : 9;
                            if (pendingLoot && !appendPendingLoot())
                                return rejectUnsafeDrop();
                            break;
                        case BlockEntity::Kind::Brewing:
                            slots = entity->brewing.slots;
                            slotCount = BrewingData::kSlots;
                            break;
                        case BlockEntity::Kind::Crafter:
                            slots = entity->crafter.slots;
                            slotCount = CrafterData::kSlots;
                            if (pendingLoot && !appendPendingLoot())
                                return rejectUnsafeDrop();
                            break;
                        case BlockEntity::Kind::ShulkerBox:
                            {
                                const bool hasTypedContents = std::any_of(
                                    std::begin(entity->chest.slots),
                                    std::end(entity->chest.slots),
                                    [](const ItemStack& stack) {
                                        return !stack.empty();
                                    });
                                const auto* savedItems = savedEntity
                                    ? savedEntity->get("Items") : nullptr;
                                const bool hasPendingLoot = savedEntity &&
                                    savedEntity->get("LootTable") != nullptr;
                                const bool hasSavedContents = savedItems &&
                                    savedItems->tag == nbt::List &&
                                    !savedItems->list.empty();
                                const bool hasCustomName = savedEntity &&
                                    savedEntity->get("CustomName") != nullptr;
                                if (hasTypedContents || hasSavedContents ||
                                    hasCustomName || hasPendingLoot) {
                                    if (!savedItems ||
                                        savedItems->tag != nbt::List ||
                                        (!savedItems->list.empty() &&
                                         savedItems->listElement !=
                                             nbt::Compound) ||
                                        ((hasTypedContents || hasSavedContents) &&
                                         savedItems->list.empty()) || !oldBlock)
                                        return rejectUnsafeDrop();
                                    auto shulker = makeShulkerBoxDropItem(
                                        oldBlock->name, *savedEntity);
                                    if (!shulker) return rejectUnsafeDrop();
                                    appendDrop(*shulker);
                                    shulkerContentsPackaged = true;
                                }
                            }
                            break;
                        case BlockEntity::Kind::MovingPiston:
                            return rejectUnsafeDrop();
                        case BlockEntity::Kind::Sign:
                            break;
                        }
                        for (std::size_t slot = 0; slot < slotCount; ++slot)
                            appendDrop(slots[slot]);
                    }

                    if (!oldBlock || oldState == 0) continue;
                    if (shulkerContentsPackaged) continue;
                    const std::string oldName(oldBlock->name);
                    if (!lootContext && datapackManager_.lootTables.blockTableRequiresEntityContext(
                                            oldName))
                        return rejectUnsafeDrop();
                    auto drops = datapackManager_.lootTables.evaluateWithContext(
                        oldName, ItemStack::air(), lootContext);
                    if (drops.empty() &&
                        datapackManager_.lootTables.findBlockTable(oldName) == nullptr &&
                        oldName != "minecraft:glass") {
                        const auto override = blockDropOverrides.find(oldName);
                        const auto& dropName = override == blockDropOverrides.end()
                            ? oldName : override->second;
                        const auto item = gen::itemIdByName().find(dropName);
                        if (item != gen::itemIdByName().end())
                            drops.push_back(ItemStack::of(item->second));
                    }
                    for (const auto& drop : drops) appendDrop(drop);
                }

                // Resolve and validate every drop before changing the world.
                // Unsupported opaque item data must not turn a destroy into
                // silent inventory loss after the blocks have already gone.
                try {
                    for (const auto& drop : destroyDrops) {
                        WriteBuffer encoded;
                        drop.stack.write(encoded);
                    }
                } catch (const std::exception&) {
                    return rejectUnsafeDrop();
                }
            }

            if (!updates.empty() && !targetWorld.setBlockBatch(updates)) {
                sendFeedback(src,
                             "Could not fill blocks; the world rejected the edit");
                return 0;
            }
            for (const auto& update : updates)
                broadcastBlockChangeFor(dimension, update.x, update.y,
                                        update.z, update.state);

            for (const auto& drop : destroyDrops)
                spawnItemDropFor(dimension, drop.x + 0.5, drop.y + 0.25,
                    drop.z + 0.5, drop.stack,
                    (nextRandom() / static_cast<double>(RAND_MAX) - 0.5) * 0.15,
                    0.12,
                    (nextRandom() / static_cast<double>(RAND_MAX) - 0.5) * 0.15);

            if (updates.empty()) {
                sendFeedback(src, "No blocks were filled");
                return 0;
            }
            sendFeedback(src, "Filled " + std::to_string(updates.size()) +
                             " blocks with " + replacement.name);
            return static_cast<int>(updates.size());
        };

        block->executable = true;
        block->action = [executeFill](CommandContext& c) {
            return executeFill(c, "replace", std::nullopt);
        };
        auto mode = CommandNode::argument("fillMode", args::stringWord());
        mode->suggestions = [](brigadier::StringReader&,
                               brigadier::ParseCtx&) {
            return std::vector<std::string>{
                "replace", "destroy", "keep", "hollow", "outline"};
        };
        mode->executable = true;
        mode->action = [executeFill](CommandContext& c) {
            return executeFill(c, c.arg("fillMode").asStr(), std::nullopt);
        };
        auto filter = CommandNode::argument(
            "replaceFilter", args::blockPredicateArg());
        filter->executable = true;
        filter->suggestions = blockSuggestions;
        filter->action = [executeFill](CommandContext& c) {
            return executeFill(c, c.arg("fillMode").asStr(),
                                c.arg("replaceFilter").asStr());
        };
        mode->then(filter);
        block->then(mode);
        to->then(block);
        from->then(to);
        fill->then(from);
        d.root->then(fill);
    }
}


void GameServer::initLocateStructureCommand(
    const brigadier::NodePtr& locate) {
        {
            auto structureLit = CommandNode::literal("structure");
            auto structArg = CommandNode::argument("locateStructureId", args::resourceLocation());
            structArg->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&){
                return std::vector<std::string>{
                    "minecraft:village","minecraft:ancient_city","minecraft:trail_ruins",
                    "minecraft:desert_pyramid","minecraft:jungle_temple","minecraft:swamp_hut",
                    "minecraft:igloo","minecraft:pillager_outpost","minecraft:monument",
                    "minecraft:mansion","minecraft:ruined_portal","minecraft:shipwreck",
                    "minecraft:ocean_ruins","minecraft:nether_complexes","minecraft:nether_fossil",
                    "minecraft:end_city","minecraft:trial_chambers","minecraft:buried_treasure",
                    "minecraft:mineshaft","minecraft:stronghold"
                };
            };
            structArg->executable = true;
            structArg->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                std::string req = c.arg("locateStructureId").asStr();
                if(req.find(':')==std::string::npos) req="minecraft:"+req;
                std::string shortName = req.substr(req.find(':')+1);
                // Reuse the world's already-loaded manager.  Constructing a
                // temporary manager here reloads every structure asset for
                // every command and can block the session long enough for the
                // keep-alive janitor to mistake a healthy client for an idle
                // one.  It also used to search the overworld from Nether/End.
                World& locateWorld = worldForCommand(c.source);
                const auto* manager = locateWorld.structureManager();
                if (!manager) {
                    sendFeedback(src, "Structure search is unavailable");
                    return 0;
                }
                const auto& sets = manager->sets();
                std::vector<const worldgen::SMStructureSet*> candidates;
                for(auto& s : sets){
                    if(s.name==req) candidates.push_back(&s);
                    else if(s.name.find(shortName)!=std::string::npos) candidates.push_back(&s);
                    // also handle aliases: desert_pyramid vs village etc
                    if(req=="minecraft:village" && s.name.find("village")!=std::string::npos) candidates.push_back(&s);
                }
                // dedup
                std::sort(candidates.begin(), candidates.end());
                candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
                if(candidates.empty()){
                    // try any set that contains shortName substring
                    for(auto& s: sets) if(s.name.find(shortName)!=std::string::npos) candidates.push_back(&s);
                }
                if(candidates.empty()){
                    sendFeedback(src, "Unknown structure: "+req);
                    return 0;
                }
                // Search spiral from source position (or 0,0 for console)
                int srcCx = src ? (int)std::floor(src->x/16.0) : 0;
                int srcCz = src ? (int)std::floor(src->z/16.0) : 0;
                int bestDist = INT32_MAX;
                int bestX=0,bestY=64,bestZ=0;
                std::string bestName;
                const int maxRadius = 100; // chunk radius (Yarn uses 100 chunk steps spiral)
                bool found=false;
                const int sourceX = src ? static_cast<int>(src->x) : 0;
                const int sourceZ = src ? static_cast<int>(src->z) : 0;
                auto consider = [&](const worldgen::SMStructureSet* set,
                                    const worldgen::SMStructureAt& at) {
                    if (!at.present) return;
                    // maxHoriz is the generation footprint around the origin;
                    // retain it in the search bound so a structure whose
                    // origin is just outside the current square is not lost.
                    if (std::abs(at.originCx - srcCx) > maxRadius + set->maxHoriz ||
                        std::abs(at.originCz - srcCz) > maxRadius + set->maxHoriz)
                        return;
                    if (!set->biomes.empty()) {
                        const std::string bio = locateWorld.sampledBiome(
                            at.originX + 8, 64, at.originZ + 8);
                        bool ok = false;
                        for (const auto& want : set->biomes)
                            if (bio.find(want) != std::string::npos) { ok = true; break; }
                        if (!ok) return;
                    }
                    const int dist = std::abs(at.originX - sourceX) +
                                     std::abs(at.originZ - sourceZ);
                    if (dist >= bestDist) return;
                    bestDist = dist;
                    bestX = at.originX;
                    bestZ = at.originZ;
                    bestName = set->name;
                    found = true;
                };

                // Enumerate the structure-set cells directly.  The previous
                // chunk spiral called smStructureAtChunk() for every chunk;
                // that function itself checks a 3x3 cell neighbourhood, and
                // strongholds regenerated all 128 ring positions per chunk.
                // Direct cell enumeration preserves the same hash candidates
                // while keeping /locate bounded and responsive.
                auto floorCell = [](int value, int spacing) -> std::int64_t {
                    return static_cast<std::int64_t>(std::floor(
                        static_cast<double>(value) / spacing));
                };
                for (const auto* set : candidates) {
                    if (set->concentric.enabled) {
                        for (int i = 0; i < set->concentric.count; ++i)
                            consider(set, worldgen::smConcentricStructureAtIndex(
                                             *set, cfg_.seed, i));
                        continue;
                    }
                    const int spacing = std::max(1, set->spacing);
                    const int bound = maxRadius + set->maxHoriz + 1;
                    const std::int64_t minCell = floorCell(srcCx - bound, spacing) - 1;
                    const std::int64_t maxCell = floorCell(srcCx + bound, spacing) + 1;
                    const std::int64_t minCellZ = floorCell(srcCz - bound, spacing) - 1;
                    const std::int64_t maxCellZ = floorCell(srcCz + bound, spacing) + 1;
                    for (std::int64_t cellX = minCell; cellX <= maxCell; ++cellX)
                        for (std::int64_t cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ)
                            consider(set, worldgen::smStructureAtCell(
                                             *set, cfg_.seed, cellX, cellZ));
                }
                if (found) {
                    // Prefer an already loaded surface.  A locate command is
                    // read-only and must not synchronously generate a distant
                    // structure chunk merely to calculate its display Y.
                    bestY = locateWorld.surfaceFeetYIfLoaded(bestX, bestZ)
                                .value_or(locateWorld.isFlat() ? -60
                                                               : locateWorld.seaLevel());
                    if (bestY < locateWorld.minY() ||
                        bestY >= locateWorld.maxY()) bestY = 64;
                }
                if(!found || bestDist==INT32_MAX){
                    sendFeedback(src, "Could not find structure "+req+" nearby (searched "+std::to_string(maxRadius*16)+" blocks)");
                    return 0;
                }
                // Feedback like Yarn: "The nearest minecraft:trial_chambers is at [x, y, z] (distance blocks away) (new chunks)"
                double dx = src ? (bestX - src->x) : bestX;
                double dz = src ? (bestZ - src->z) : bestZ;
                int distBlocks = (int)std::sqrt(dx*dx + dz*dz);
                std::string msg = "The nearest "+bestName+" is at ["+std::to_string(bestX)+", "+std::to_string(bestY)+", "+std::to_string(bestZ)+"] ("+std::to_string(distBlocks)+" blocks away)";
                sendFeedback(src, msg);
                return distBlocks;
            };
            structureLit->then(structArg);
            locate->then(structureLit);
        }
}

void GameServer::initLocateBiomeCommand(const brigadier::NodePtr& locate) {
        {
            auto biomeLit = CommandNode::literal("biome");
            auto biomeArg = CommandNode::argument("locateBiomeId", args::resourceLocation());
            biomeArg->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&){
                return std::vector<std::string>{
                    "minecraft:plains","minecraft:desert","minecraft:forest","minecraft:taiga",
                    "minecraft:jungle","minecraft:swamp","minecraft:savanna","minecraft:dark_forest",
                    "minecraft:pale_garden","minecraft:snowy_plains","minecraft:deep_dark"
                };
            };
            biomeArg->executable = true;
            biomeArg->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                std::string req = c.arg("locateBiomeId").asStr();
                if(req.find(':')==std::string::npos) req="minecraft:"+req;
                std::string shortName = req.substr(req.find(':')+1);
                int srcX = src ? (int)src->x : 0;
                int srcZ = src ? (int)src->z : 0;
                World& targetWorld = worldForCommand(c.source);
                const int maxRadius = 6400; // blocks (Yarn locate biome radius)
                const int step = 16;
                int bestDist = INT32_MAX;
                int bestX=0,bestZ=0;
                bool found=false;
                // spiral search
                for(int r=0; r<=maxRadius && !found; r+=step){
                    for(int dx=-r; dx<=r && !found; dx+=step){
                        for(int dz=-r; dz<=r && !found; dz+=step){
                            if(r!=0 && std::abs(dx)!=r && std::abs(dz)!=r) continue;
                            int x = srcX + dx;
                            int z = srcZ + dz;
                            std::string bio = targetWorld.sampledBiome(x, 64, z);
                            if(bio.empty()) continue;
                            std::string bioShort = bio.substr(bio.find(':')+1);
                            bool match = (bio==req) || (bioShort==shortName) || (bio.find(shortName)!=std::string::npos);
                            if(match){
                                int dist = std::abs(dx)+std::abs(dz);
                                if(dist < bestDist){
                                    bestDist = dist;
                                    bestX = x; bestZ = z;
                                    found=true;
                                }
                            }
                        }
                    }
                    // early exit after first ring found to keep closest
                    if(found) break;
                }
                if(!found){
                    sendFeedback(src, "Could not find biome "+req+" nearby");
                    return 0;
                }
                int blocks = (int)std::sqrt((bestX-srcX)*(bestX-srcX)+(bestZ-srcZ)*(bestZ-srcZ));
                std::string msg = "The nearest "+req+" is at ["+std::to_string(bestX)+", ~, "+std::to_string(bestZ)+"] ("+std::to_string(blocks)+" blocks away)";
                sendFeedback(src, msg);
                return blocks;
            };
            biomeLit->then(biomeArg);
            locate->then(biomeLit);
        }
}

void GameServer::initLocatePoiCommand(const brigadier::NodePtr& locate) {
        {
            auto poiLit = CommandNode::literal("poi");
            auto poiArg = CommandNode::argument("locatePoiId", args::resourceLocation());
            poiArg->executable = true;
            poiArg->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                std::string req = c.arg("locatePoiId").asStr();
                sendFeedback(src, "POI locate not yet implemented for "+req);
                return 0;
            };
            poiLit->then(poiArg);
            locate->then(poiLit);
        }
}

void GameServer::initPlaceCommand() {
    auto& d = commands_;
    auto place = CommandNode::literal("place");
    auto featureAction = [this](CommandContext& c) {
        Player* source = static_cast<Player*>(c.source.player);
        std::string feature = c.arg("placeFeatureId").asStr();
        if (feature.find(':') == std::string::npos)
            feature = "minecraft:" + feature;
        sendFeedback(source, "Cannot place configured feature " + feature +
                             ": no vanilla configured-feature registry is loaded");
        return 0;
    };
    auto featureLiteral = CommandNode::literal("feature");
    auto featureArgument = CommandNode::argument("placeFeatureId",
                                                  args::resourceLocation());
    featureArgument->executable = true;
    featureArgument->action = [featureAction](CommandContext& c) {
        return featureAction(c);
    };
    auto featurePosition = CommandNode::argument("placeFeaturePos",
                                                  args::blockPos());
    featurePosition->executable = true;
    featurePosition->action = [featureAction](CommandContext& c) {
        return featureAction(c);
    };
    featureArgument->then(featurePosition);
    featureLiteral->then(featureArgument);
    place->then(featureLiteral);

    auto structureAction = [](CommandContext& c) {
        Player* source = static_cast<Player*>(c.source.player);
        sendFeedback(source,
                     "Vanilla structure templates are unavailable for /place structure");
        return 0;
    };
    auto structureLiteral = CommandNode::literal("structure");
    auto structureArgument = CommandNode::argument(
        "placeStructureId", args::resourceLocation());
    structureArgument->executable = true;
    structureArgument->action = [structureAction](CommandContext& c) {
        return structureAction(c);
    };
    auto structurePosition = CommandNode::argument("placeStructurePos",
                                                   args::blockPos());
    structurePosition->executable = true;
    structurePosition->action = [structureAction](CommandContext& c) {
        return structureAction(c);
    };
    structureArgument->then(structurePosition);
    structureLiteral->then(structureArgument);
    place->then(structureLiteral);

    // The command shape is present for dispatcher compatibility. The
    // structure-pool/template graph needed for vanilla jigsaw placement is not
    // available in this implementation.
    auto jigsawLiteral = CommandNode::literal("jigsaw");
    auto poolArgument = CommandNode::argument("jigsawPool",
                                               args::resourceLocation());
    auto targetArgument = CommandNode::argument("jigsawTarget",
                                                 args::resourceLocation());
    auto depthArgument = CommandNode::argument("jigsawDepth",
                                                args::integer(1, 7));
    depthArgument->executable = true;
    depthArgument->action = [this](CommandContext& c) {
        Player* source = static_cast<Player*>(c.source.player);
        const auto pool = c.arg("jigsawPool").asStr();
        const auto target = c.arg("jigsawTarget").asStr();
        (void)c.arg("jigsawDepth").asInt();
        sendFeedback(source, "Jigsaw place not yet implemented (pool=" + pool +
                     " target=" + target + ") — use /place structure instead");
        return 0;
    };
    targetArgument->then(depthArgument);
    poolArgument->then(targetArgument);
    jigsawLiteral->then(poolArgument);
    place->then(jigsawLiteral);
    d.root->then(place);
}

void GameServer::initSpreadPlayersCommand() {
    auto& d = commands_;
        {
            auto sp = CommandNode::literal("spreadplayers");
            auto center = CommandNode::argument("spCenter", args::vec2Arg());
            auto spread = CommandNode::argument("spSpread", args::floatArg(0.f, 100000.f));
            auto maxRange = CommandNode::argument("spMaxRange", args::floatArg(1.f, 100000.f));
            auto respect = CommandNode::argument("spRespectTeams", args::boolean());
            auto targets = CommandNode::argument("spTargets", args::entity(false,false));
            targets->executable = true;
            targets->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                brigadier::Vec2f centerV;
                {
                    auto v = c.arg("spCenter");
                    if(auto* p = std::get_if<brigadier::Vec2f>(&v.v)) centerV = *p;
                    else if(auto* p3 = std::get_if<brigadier::Vec3d>(&v.v)) { centerV.x=(float)p3->x; centerV.y=(float)p3->z; }
                    else { centerV.x=0; centerV.y=0; }
                }
                float spreadDist = (float)c.arg("spSpread").asDouble();
                float maxR = (float)c.arg("spMaxRange").asDouble();
                bool respectTeams = c.arg("spRespectTeams").asBool();
                const auto sel = c.arg("spTargets").asSelector();
                World& targetWorld = worldForCommand(c.source);
                std::vector<Player*> players;
                for(auto& n: sel.playerNames) if(Player* p=findPlayer(*this,n)) players.push_back(p);
                if(players.empty()){
                    sendFeedback(src, "No targets for spreadplayers");
                    return 0;
                }
                if(spreadDist > maxR){
                    sendFeedback(src, "spreadDistance must not be greater than maxRange");
                    return 0;
                }
                double cx = centerV.x, cz = centerV.y;
                // Group by team if respectTeams
                struct Group { std::vector<Player*> members; };
                std::vector<Group> groups;
                if(respectTeams){
                    std::unordered_map<std::string, size_t> teamToIdx;
                    std::vector<Player*> noTeam;
                    for(auto* p: players){
                        std::string teamName;
                        for(auto& kv: teams.teams) if(kv.second.members.count(p->name)) { teamName=kv.first; break; }
                        if(teamName.empty()) noTeam.push_back(p);
                        else {
                            auto it = teamToIdx.find(teamName);
                            if(it==teamToIdx.end()){
                                size_t idx=groups.size();
                                teamToIdx[teamName]=idx;
                                groups.push_back({{p}});
                            } else groups[it->second].members.push_back(p);
                        }
                    }
                    for(auto* p: noTeam) groups.push_back({{p}});
                } else {
                    for(auto* p: players) groups.push_back({{p}});
                }
                // Random spread with simple rejection sampling
                struct Pos { double x,z; };
                std::vector<Pos> placed;
                placed.reserve(groups.size());
                seedRandom(static_cast<unsigned>(
                    std::chrono::steady_clock::now().time_since_epoch().count()) ^
                    static_cast<unsigned>(tickNo_));
                auto findY = [&](double x, double z)->double{
                    int ix=(int)std::floor(x), iz=(int)std::floor(z);
                    // scan from top down for solid
                    for(int y=targetWorld.maxY() - 1;
                        y>=targetWorld.minY(); --y){
                        uint16_t st = targetWorld.getBlock(ix,y,iz);
                        uint16_t above = targetWorld.getBlock(ix,y+1,iz);
                        uint16_t above2 = targetWorld.getBlock(ix,y+2,iz);
                        if(st!=0 && above==0 && above2==0) return y+1;
                    }
                    // fallback: surfaceFeetY
                    return targetWorld.surfaceFeetY(ix, iz);
                };
                for(size_t gi=0; gi<groups.size(); ++gi){
                    Pos pos{0,0};
                    bool ok=false;
                    for(int attempt=0; attempt<1000; ++attempt){
                        double rx = cx + ((double)nextRandom()/RAND_MAX*2.0-1.0)*maxR;
                        double rz = cz + ((double)nextRandom()/RAND_MAX*2.0-1.0)*maxR;
                        bool far=true;
                        for(auto& pr: placed){
                            double dx=rx-pr.x, dz=rz-pr.z;
                            if(std::sqrt(dx*dx+dz*dz) < spreadDist){ far=false; break; }
                        }
                        if(!far) continue;
                        pos={rx,rz};
                        ok=true; break;
                    }
                    if(!ok){
                        // fallback: just use random
                        pos={cx + ((double)nextRandom()/RAND_MAX*2.0-1.0)*maxR, cz + ((double)nextRandom()/RAND_MAX*2.0-1.0)*maxR};
                    }
                    placed.push_back(pos);
                    double y=findY(pos.x,pos.z);
                    for(auto* p: groups[gi].members){
                        teleportPlayer(*p, pos.x, y, pos.z);
                    }
                }
                int total = (int)players.size();
                sendFeedback(src, "Spread "+std::to_string(total)+" entities around "+std::to_string((int)cx)+", "+std::to_string((int)cz));
                broadcastSystemText("Teleported "+std::to_string(total)+" entities via spreadplayers");
                return total;
            };
            // vec2Arg returns Vec2f stored as Vec2f; need to ensure spread takes Vec2f
            respect->then(targets);
            maxRange->then(respect);
            spread->then(maxRange);
            center->then(spread);
            sp->then(center);
            d.root->then(sp);
        }
}

void GameServer::initEnchantCommand() {
    auto& d = commands_;
    {
        auto enchant = CommandNode::literal("enchant");
        auto targets = CommandNode::argument("targets", args::entity(false,false));
        auto ench = CommandNode::argument("enchantment", args::resourceLocation());
        ench->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&){
            std::vector<std::string> v;
            for(int i=0;i<42;++i){ std::string n=ItemStack::enchantNameById(i); if(!n.empty()) v.push_back(n); }
            // also bare names without namespace
            std::vector<std::string> extra;
            for(auto &s: v) { auto p=s.find(':'); if(p!=std::string::npos) extra.push_back(s.substr(p+1)); }
            v.insert(v.end(), extra.begin(), extra.end());
            return v;
        };
        // without level (default 1)
        ench->executable = true;
        ench->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("targets").asSelector();
            std::string enchName = c.arg("enchantment").asStr();
            if(enchName.find(':')==std::string::npos) enchName="minecraft:"+enchName;
            if(ItemStack::enchantIdByName(enchName)<0) throw std::runtime_error("Unknown enchantment: "+enchName);
            int level = 1;
            int enchanted = 0;
            for(auto &nm: sel.playerNames) if(Player* t=findPlayer(*this,nm)){
                if(t->heldSlot<0 || t->heldSlot>=9) continue;
                auto &held = t->inv[36 + t->heldSlot];
                if(held.empty()) continue;
                ItemStack::addEnchant(held, enchName, level);
                resendInventory(*t);
                ++enchanted;
            }
            if(enchanted==0) throw std::runtime_error("No target held an item to enchant");
            sendFeedback(src, "Enchanted "+std::to_string(enchanted)+" target(s) with "+enchName+" "+std::to_string(level));
            return enchanted;
        };
        auto lvlArg = CommandNode::argument("level", args::integer(1, 255));
        lvlArg->executable = true;
        lvlArg->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("targets").asSelector();
            std::string enchName = c.arg("enchantment").asStr();
            if(enchName.find(':')==std::string::npos) enchName="minecraft:"+enchName;
            if(ItemStack::enchantIdByName(enchName)<0) throw std::runtime_error("Unknown enchantment: "+enchName);
            int level = c.arg("level").asInt();
            int enchanted = 0;
            for(auto &nm: sel.playerNames) if(Player* t=findPlayer(*this,nm)){
                if(t->heldSlot<0 || t->heldSlot>=9) continue;
                auto &held = t->inv[36 + t->heldSlot];
                if(held.empty()) continue;
                ItemStack::addEnchant(held, enchName, level);
                resendInventory(*t);
                ++enchanted;
            }
            if(enchanted==0) throw std::runtime_error("No target held an item to enchant");
            sendFeedback(src, "Enchanted "+std::to_string(enchanted)+" target(s) with "+enchName+" "+std::to_string(level));
            return enchanted;
        };
        ench->then(lvlArg);
        targets->then(ench);
        enchant->then(targets);
        d.root->then(enchant);
    }
}

void GameServer::addTimeQueryAndAddSubcommands(
    const brigadier::NodePtr& time) {
    // /time query <daytime|gametime|day> and /time add <value>.
    auto query = CommandNode::literal("query");
        for (const char* q : {"daytime", "gametime", "day"}) {
            auto qlit = CommandNode::literal(q);
            qlit->executable = true;
            qlit->action = [this, q](CommandContext& c) {
                Player* src = static_cast<Player*>(c.source.player);
                std::int64_t v = std::string(q) == "daytime" ? dayTime() :
                                 std::string(q) == "day" ? (dayTime() / 24000) : tickNo_.load();
                sendFeedback(src, "The time is " + std::to_string(v));
                return static_cast<int>(v);
            };
            query->then(qlit);
        }
    time->then(query);
    auto add = CommandNode::literal("add");
        auto amt = CommandNode::argument("timeAdd", args::timeArg());
        amt->executable = true;
        amt->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            setTimeOfDay(dayTime() + c.arg("timeAdd").asI64());
            WriteBuffer t;
            t.i64(tickNo_); t.i64(dayTime()); t.boolean(true);
            broadcastPacketExcept(nullptr, proto::pl::sc::UpdateTime, t);
            sendFeedback(src, "Set the time to " + std::to_string(dayTime()));
            return 1;
        };
    add->then(amt);
    time->then(add);
}

void GameServer::addWorldBorderManagementSubcommands(
    const brigadier::NodePtr& wb) {
    // /worldborder get|set|center|add.
    auto get = CommandNode::literal("get");
        get->executable = true;
        get->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto border = worldBorderSnapshot();
            sendFeedback(src, "The world border is currently " +
                         std::to_string(border.diameter) + " blocks wide");
            return static_cast<int>(border.diameter);
        };
        wb->then(get);
        auto set = CommandNode::literal("set");
        auto diam = CommandNode::argument("diameter", args::floatArg(1.f, 60000000.f));
        diam->executable = true;
        diam->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            auto* persistence = persistenceFor(0);
            if (!persistence) return 0;
            const auto border = persistence->worldBorderSnapshot();
            const double diameter = std::clamp(
                c.arg("diameter").asDouble(), 1.0,
                static_cast<double>(constants::kWorldBorderDiameter));
            persistence->setWorldBorder(diameter, border.centerX, border.centerZ);
            broadcastWorldBorder();
            sendFeedback(src, "Set world border to " + std::to_string(diameter) + " blocks wide");
            return 1;
        };
        set->then(diam);
        wb->then(set);
        auto center = CommandNode::literal("center");
        auto cx = CommandNode::argument("centerX", args::floatArg(-30000000.f, 30000000.f));
        auto cz = CommandNode::argument("centerZ", args::floatArg(-30000000.f, 30000000.f));
        cz->executable = true;
        cz->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            auto* persistence = persistenceFor(0);
            if (!persistence) return 0;
            const auto border = persistence->worldBorderSnapshot();
            const double centerX = c.arg("centerX").asDouble();
            const double centerZ = c.arg("centerZ").asDouble();
            persistence->setWorldBorder(border.diameter, centerX, centerZ);
            broadcastWorldBorder();
            sendFeedback(src, "Set world border center to " +
                         std::to_string(centerX) + ", " +
                         std::to_string(centerZ));
            return 1;
        };
        cx->then(cz);
        center->then(cx);
        wb->then(center);
        auto add = CommandNode::literal("add");
        auto delta = CommandNode::argument("delta", args::floatArg(-60000000.f, 60000000.f));
        delta->executable = true;
        delta->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            auto* persistence = persistenceFor(0);
            if (!persistence) return 0;
            const auto border = persistence->worldBorderSnapshot();
            const double diameter = std::clamp(
                border.diameter + c.arg("delta").asDouble(), 1.0,
                static_cast<double>(constants::kWorldBorderDiameter));
            persistence->setWorldBorder(diameter, border.centerX,
                                        border.centerZ);
            broadcastWorldBorder();
            sendFeedback(src, "Set world border to " +
                         std::to_string(diameter) + " blocks wide");
            return 1;
        };
        add->then(delta);
    wb->then(add);
}

void GameServer::initSetWorldSpawnCommand() {
    auto& d = commands_;
    {
        // /setworldspawn [<pos>] [<angle>] (Yarn SetWorldSpawn).
        auto sws = CommandNode::literal("setworldspawn");
        sws->executable = true;
        sws->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto dimension = commandDimension(c.source);
            auto& targetWorld = worldFor(dimension);
            targetWorld.setSpawnPoint({static_cast<std::int32_t>(src ? src->x : 0),
                                       static_cast<std::int32_t>(src ? src->y : -60),
                                       static_cast<std::int32_t>(src ? src->z : 0),
                                       src ? src->yaw : 0.f});
            // level.dat is intentionally a single overworld file in this
            // implementation; dimension-local spawn points remain live for
            // the server session until dimension persistence is added.
            if (dimension == 0) saveLevelData();
            WriteBuffer point;
            const auto spawn = targetWorld.spawnPoint();
            point.position(spawn.x, spawn.y, spawn.z);
            point.f32(spawn.angle);
            broadcastPacketExceptInDimension(dimension, nullptr,
                                              proto::pl::sc::SetDefaultSpawn,
                                              point);
            sendFeedback(src, "Set world spawn to current position");
            return 1;
        };
        auto pos = CommandNode::argument("swsPos", args::blockPos());
        pos->executable = true;
        pos->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto p = c.arg("swsPos").asBlockPos();
            const auto dimension = commandDimension(c.source);
            auto& targetWorld = worldFor(dimension);
            targetWorld.setSpawnPoint({p.x, p.y, p.z, src ? src->yaw : 0.f});
            if (dimension == 0) saveLevelData();
            WriteBuffer b;
            b.position(p.x, p.y, p.z);
            b.f32(targetWorld.spawnPoint().angle);
            broadcastPacketExceptInDimension(dimension, nullptr,
                                              proto::pl::sc::SetDefaultSpawn, b);
            sendFeedback(src, "Set world spawn to " + std::to_string(p.x) +
                         ", " + std::to_string(p.y) + ", " + std::to_string(p.z));
            return 1;
        };
        auto angle = CommandNode::argument("swsAngle", args::angleArg());
        angle->executable = true;
        angle->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto p = c.arg("swsPos").asBlockPos();
            const auto dimension = commandDimension(c.source);
            auto& targetWorld = worldFor(dimension);
            const auto spawnAngle = static_cast<float>(c.arg("swsAngle").asDouble());
            targetWorld.setSpawnPoint({p.x, p.y, p.z, spawnAngle});
            if (dimension == 0) saveLevelData();
            WriteBuffer b;
            b.position(p.x, p.y, p.z);
            b.f32(spawnAngle);
            broadcastPacketExceptInDimension(dimension, nullptr,
                                              proto::pl::sc::SetDefaultSpawn, b);
            sendFeedback(src, "Set world spawn to " + std::to_string(p.x) +
                         ", " + std::to_string(p.y) + ", " + std::to_string(p.z));
            return 1;
        };
        pos->then(angle);
        sws->then(pos);
        d.root->then(sws);
    }
}

void GameServer::initDefaultGameModeCommand() {
    auto& d = commands_;
    {
        // /defaultgamemode <survival|creative|adventure|spectator>
        auto dgm = CommandNode::literal("defaultgamemode");
        auto mode = CommandNode::argument("defaultMode", args::gamemodeArg());
        mode->executable = true;
        mode->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            std::string m = c.arg("defaultMode").asStr();
            for (auto& ch : m) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
            if (m != "survival" && m != "creative" && m != "adventure" && m != "spectator")
                throw std::runtime_error("Unknown gamemode '" + m + "'");
            const std::uint8_t mode = m == "survival" ? 0 : m == "creative" ? 1 :
                                      m == "adventure" ? 2 : 3;
            setDefaultGameMode(mode);
            saveLevelData();
            sendFeedback(src, "Set default gamemode to " + m);
            return 1;
        };
        dgm->then(mode);
        d.root->then(dgm);
    }
}

void GameServer::initJigsawGenerateCommand() {
    auto& d = commands_;
    {
        // Keep the vanilla command shape, but report the unsupported template
        // graph instead of claiming that blocks were generated.
        auto jig = CommandNode::literal("jigsaw");
        auto gen = CommandNode::literal("generate");
        auto rest = CommandNode::argument("jigsawArgs", args::stringGreedy());
        rest->executable = true;
        rest->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            sendFeedback(src, "Jigsaw generation is unavailable: template pools are not loaded (" +
                         c.arg("jigsawArgs").asStr() + ")");
            return 0;
        };
        gen->then(rest);
        jig->then(gen);
        d.root->then(jig);
    }
}


} // namespace cppfm
