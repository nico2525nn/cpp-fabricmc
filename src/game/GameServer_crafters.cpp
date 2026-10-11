#include "GameServer.hpp"
#include "BlockEntityDrops.hpp"
#include "BlockEntityKinds.hpp"
#include "EntitySupport.hpp"
#include "MetadataTypes.hpp"
#include "../physics/Fluids.hpp"
#include "../physics/Redstone.hpp"
#include "Constants.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/EntityIds.hpp"
#include "MenuInteraction.hpp"
#include "StairsHelper.hpp"
#include "EquipmentComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "MeleeHelper.hpp"
#include "CombatManager.hpp"
#include "HungerManager.hpp"
#include "MobSpawner.hpp"
#include "MenuLogic.hpp"
#include "PotionBrewing.hpp"
#include "GameServerHelpers.hpp"

namespace cppfm {
using namespace proto;

void GameServer::craftersTick() {
    craftersTickFor(0);
    craftersTickFor(-1);
    craftersTickFor(1);
}

void GameServer::craftersTickFor(std::int8_t dimension) {
    dimension = canonicalDimension(dimension);
    auto& blockEntities = blockEntitiesFor(dimension);
    auto& world = worldFor(dimension);
    auto& redstone = redstoneFor(dimension);

    struct TickIo final : MenuIo {
        BlockEntityStore& store;
        explicit TickIo(BlockEntityStore& s) : store(s) {}
        void dropFromPlayer(Player&, const ItemStack&, bool) override {}
        void blockEntityChanged(std::int64_t key) override {
            store.markDirty(key);
        }
        void itemCrafted(Player&, const ItemStack&) override {}
        void itemSmelted(Player&, const ItemStack&) override {}
    } io(blockEntities);

    auto setBooleanProperty = [&](std::int32_t x, std::int32_t y,
                                  std::int32_t z, std::string_view key,
                                  bool value) -> bool {
        const std::uint16_t oldState = world.getBlock(x, y, z);
        const auto* def = gen::blockByState(oldState);
        if (!def) return false;
        const auto oldProps = gen::propsOf(oldState);
        bool present = false;
        for (const auto& [name, ignored] : oldProps)
            if (name == key) { present = true; break; }
        if (!present) return false;
        const std::string_view valueName = value ? "true" : "false";
        std::vector<std::pair<std::string_view, std::string_view>> props;
        props.reserve(oldProps.size());
        for (const auto& [name, current] : oldProps)
            props.emplace_back(name, name == key ? valueName : current);
        const auto newState = static_cast<std::uint16_t>(
            gen::stateWithProps(*def, props));
        if (newState == oldState) return false;
        world.setBlock(x, y, z, newState);
        broadcastBlockChangeFor(dimension, x, y, z, newState);
        return true;
    };

    auto frontDirection = [&](std::uint16_t state) {
        std::string front = "north";
        for (const auto& [name, value] : gen::propsOf(state)) {
            if (name != "orientation") continue;
            const auto separator = value.find('_');
            front = std::string(value.substr(0, separator));
            break;
        }
        struct Direction { int x = 0, y = 0, z = -1; } result;
        if (front == "south") result = {0, 0, 1};
        else if (front == "east") result = {1, 0, 0};
        else if (front == "west") result = {-1, 0, 0};
        else if (front == "up") result = {0, 1, 0};
        else if (front == "down") result = {0, -1, 0};
        return result;
    };

    auto ensureContainer = [&](std::int32_t x, std::int32_t y,
                               std::int32_t z) -> BlockEntityStore::Owner {
        const auto key = posKey(x, y, z);
        if (auto existing = blockEntities.getShared(key)) return existing;
        const auto kind = blockEntityKindForBlockState(
            world.getBlock(x, y, z));
        if (!kind) return nullptr;
        // Unknown item IDs/components keep their saved inventory opaque. A
        // crafter facing such a container must treat it as a non-insertable
        // target and drop any remaining output instead of letting an
        // exception escape the server tick.
        return blockEntities.tryCreateShared(key, *kind);
    };

    // Store keys, not BlockEntity values: each crafter owns mutable stacks and
    // the map can be invalidated by a block replacement during a callback.
    std::vector<std::int64_t> snapshot;
    blockEntities.forEach([&](std::int64_t key, BlockEntity& be) {
        if (be.kind == BlockEntity::Kind::Crafter) snapshot.push_back(key);
    });

    for (const auto key : snapshot) {
        // Acquire the World mutation gate before any block-entity owner lock.
        // Container creation later in this tick re-enters the gate, and using
        // the same lock order as block edits and chunk snapshots avoids an
        // owner-lock -> World-gate cycle during concurrent chunk streaming.
        auto mutationGate = world.lockMutationGate();
        auto beOwner = blockEntities.getShared(key);
        if (!beOwner) continue;
        std::unique_lock entityLock(*beOwner->stateMtx);
        auto* be = beOwner.get();
        if (be->kind != BlockEntity::Kind::Crafter) continue;
        const int x = posKeyUnpackX(key), y = posKeyUnpackY(key), z = posKeyUnpackZ(key);
        const auto state = world.getBlock(x, y, z);
        const auto* def = gen::blockByState(state);
        if (!def || def->name != "minecraft:crafter") {
            blockEntities.remove(key);
            continue;
        }

        bool stateTriggered = false;
        bool stateCrafting = false;
        for (const auto& [name, value] : gen::propsOf(state)) {
            if (name == "triggered") stateTriggered = value == "true";
            else if (name == "crafting") stateCrafting = value == "true";
        }
        be->crafter.triggered = stateTriggered;
        const bool powered = redstone.isPoweredHere(x, y, z);
        if (powered && !stateTriggered && be->crafter.redstoneCraftDueTick < 0) {
            be->crafter.triggered = true;
            be->crafter.redstoneCraftDueTick = tickNo_ + 4;
            setBooleanProperty(x, y, z, "triggered", true);
            blockEntities.markDirty(key);
        } else if (!powered && stateTriggered) {
            be->crafter.triggered = false;
            setBooleanProperty(x, y, z, "triggered", false);
            blockEntities.markDirty(key);
        }

        if (be->crafter.craftingTicksRemaining > 0) {
            --be->crafter.craftingTicksRemaining;
            if (be->crafter.craftingTicksRemaining == 0 && stateCrafting)
                setBooleanProperty(x, y, z, "crafting", false);
            blockEntities.markDirty(key);
        }

        if (be->crafter.redstoneCraftDueTick < 0 ||
            tickNo_ < be->crafter.redstoneCraftDueTick)
            continue;
        be->crafter.redstoneCraftDueTick = -1;

        Menu menu;
        menu.type = MenuType::Crafter;
        menu.blockKey = key;
        menu.container = be->crafter.slots;
        menu.containerCount = CrafterData::kSlots;
        menu.crafterDisabledSlots = &be->crafter.disabledSlots;
        menu.blockEntity = be;
        menu.blockEntityOwner = beOwner;
        ItemStack output = ItemStack::air();
        CrafterMenuLogic logic;
        if (!logic.craftOnRedstone(menu, recipes_, output, io)) {
            broadcastSoundFor(dimension, "minecraft:block.crafter.fail",
                              x + .5, y + .5, z + .5, 1.f, 1.f, "block");
            continue;
        }

        be->crafter.craftingTicksRemaining = 6;
        setBooleanProperty(x, y, z, "crafting", true);
        blockEntities.markDirty(key);

        const auto direction = frontDirection(state);
        const int tx = x + direction.x, ty = y + direction.y,
                  tz = z + direction.z;
        ItemStack remaining = output;
        if (auto targetOwner = ensureContainer(tx, ty, tz)) {
            std::lock_guard targetLock(*targetOwner->stateMtx);
            auto* target = targetOwner.get();
            const auto before = remaining.count;
            // Reuse the same item-stack matching and side rules as container
            // automation, while keeping the source/output coordinates local.
            auto insertTarget = [&](BlockEntity& destination) {
                auto insertSlots = [&](ItemStack* slots, int count,
                                       bool skipDisabled) {
                    for (int pass = 0; pass < 2 && !remaining.empty(); ++pass)
                        for (int i = 0; i < count && !remaining.empty(); ++i) {
                            if (skipDisabled && destination.crafter.isSlotDisabled(i)) continue;
                            auto& slot = slots[i];
                            if (pass == 0) {
                                if (slot.empty() || !slot.sameItemData(remaining)) continue;
                                const int limit = maxStackForId(slot.itemId);
                                if (slot.count >= limit) continue;
                                const int moved = std::min<int>(remaining.count,
                                                                limit - slot.count);
                                slot.count = static_cast<std::int16_t>(slot.count + moved);
                                remaining.count = static_cast<std::int16_t>(remaining.count - moved);
                            } else if (slot.empty()) {
                                const int moved = std::min<int>(remaining.count,
                                                                maxStackForId(remaining.itemId));
                                slot = remaining;
                                slot.count = static_cast<std::int16_t>(moved);
                                remaining.count = static_cast<std::int16_t>(remaining.count - moved);
                            }
                        }
                };
                switch (destination.kind) {
                case BlockEntity::Kind::Chest:
                case BlockEntity::Kind::Barrel:
                case BlockEntity::Kind::ShulkerBox:
                    insertSlots(destination.chest.slots, ChestData::kSlots, false);
                    break;
                case BlockEntity::Kind::Hopper:
                    insertSlots(destination.generic.slots, 5, false);
                    break;
                case BlockEntity::Kind::Dispenser:
                case BlockEntity::Kind::Dropper:
                    insertSlots(destination.generic.slots, 9, false);
                    break;
                case BlockEntity::Kind::Crafter:
                    insertSlots(destination.crafter.slots, CrafterData::kSlots, true);
                    break;
                case BlockEntity::Kind::Furnace:
                    if (direction.y > 0)
                        insertSlots(destination.furnace.slots + FurnaceData::kInput, 1, false);
                    else if (direction.y == 0 && isFuelItem(remaining.itemId))
                        insertSlots(destination.furnace.slots + FurnaceData::kFuel, 1, false);
                    break;
                default:
                    break;
                }
            };
            insertTarget(*target);
            if (remaining.count != before)
                blockEntities.markDirty(posKey(tx, ty, tz));
        }
        if (!remaining.empty()) {
            spawnItemDropFor(dimension, x + .5 + direction.x * .7,
                             y + .5 + direction.y * .7,
                             z + .5 + direction.z * .7, remaining,
                             direction.x * .2, direction.y * .2,
                             direction.z * .2);
        }
        broadcastSoundFor(dimension, "minecraft:block.crafter.craft",
                          x + .5, y + .5, z + .5, 1.f, 1.f, "block");
    }
}

} // namespace cppfm
