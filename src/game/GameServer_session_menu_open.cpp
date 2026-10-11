#include "GameServer.hpp"
#include "BlockEntityDrops.hpp"
#include "BlockEntityKinds.hpp"
#include "Messages.hpp"
#include "MenuInteraction.hpp"
#include "CostCalculator.hpp"
#include "../generated/BlockStates.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace cppfm {
using namespace proto;

void Session::openMenuAt(std::int32_t x, std::int32_t y, std::int32_t z,
                         std::uint16_t stateOfBlock) {
    // A UseItemOn packet carries the state observed by the client.  The
    // block may have changed before this handler runs (redstone, a tick, or
    // another player), so never construct a menu from a stale state.  Keep
    // the player lock out of the world/store lookup: the normal menu lock
    // order is block-entity -> player.
    std::int8_t dimension = 0;
    LootContext playerLootContext;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (!self_->inPlay || self_->dead) return;
        dimension = GameServer::canonicalDimension(self_->dimension);
        playerLootContext.hasEntityContext = true;
        playerLootContext.entityOnFire = self_->fireTicks > 0;
    }
    World& blockWorld = srv_.worldFor(dimension);
    const std::uint16_t currentState = blockWorld.getBlock(x, y, z);
    if (currentState != stateOfBlock) return;
    const gen::BlockDef* def = gen::blockByState(stateOfBlock);
    if (!def) return;
    const std::string name(def->name);

    auto menu = std::make_shared<Menu>();
    menu->windowId = ++menuWindowCounter_;
    menu->blockKey = posKey(x, y, z);
    const auto blockKey = *menu->blockKey;
    menu->blockNameAtOpen = name;
    auto& blockEntityStore = srv_.blockEntitiesFor(dimension);
    const auto openBlockEntity = [&](BlockEntity::Kind kind) -> BlockEntity* {
        auto mutationGate = blockWorld.lockMutationGate();
        if (blockWorld.getBlock(x, y, z) != stateOfBlock) return nullptr;
        const auto expectedKind = blockEntityKindForBlockState(stateOfBlock);
        if (!expectedKind || *expectedKind != kind) return nullptr;
        auto owner = blockEntityStore.getShared(blockKey);
        if (owner) {
            std::lock_guard entityLock(*owner->stateMtx);
            if (owner->kind != kind) {
                sendSystemText(msg::kRed +
                    "This container has data this server cannot safely edit.");
                return nullptr;
            }
        } else {
            owner = blockEntityStore.tryCreateShared(blockKey, kind);
        }
        if (!owner) {
            sendSystemText(msg::kRed +
                "This container has data this server cannot safely edit.");
            return nullptr;
        }
        std::size_t lootableSlots = 0;
        switch (kind) {
            case BlockEntity::Kind::Chest:
            case BlockEntity::Kind::Barrel:
            case BlockEntity::Kind::ShulkerBox:
                lootableSlots = 27;
                break;
            case BlockEntity::Kind::Hopper:
                lootableSlots = 5;
                break;
            case BlockEntity::Kind::Dispenser:
            case BlockEntity::Kind::Dropper:
            case BlockEntity::Kind::Crafter:
                lootableSlots = 9;
                break;
            default:
                break;
        }
        if (lootableSlots != 0) {
            const auto materialized = materializePendingLootAt(
                blockEntityStore, srv_.lootTables_, x, y, z, lootableSlots,
                &playerLootContext);
            if (materialized != PendingLootMaterialization::NoPendingLoot &&
                materialized != PendingLootMaterialization::Materialized) {
                sendSystemText(msg::kRed +
                    "This container's loot table is unavailable or invalid.");
                return nullptr;
            }
            if (materialized == PendingLootMaterialization::Materialized) {
                owner = blockEntityStore.getShared(blockKey);
                if (!owner) return nullptr;
                std::lock_guard entityLock(*owner->stateMtx);
                if (owner->kind != kind) return nullptr;
            }
        }
        menu->blockEntityOwner = owner;
        menu->blockEntityKindAtOpen = owner->kind;
        return owner.get();
    };

    if (name == "minecraft:ender_chest") {
        // B-14 EnderItems: per-player 27 slots, not per-block (vanilla EnderChest)
        menu->type = MenuType::Chest;
        menu->container = self_->enderItems.data();
        menu->containerCount = 27;
        menu->blockEntity = nullptr;
    } else if (name.find("chest") != std::string::npos &&
        name.find("ender") == std::string::npos) {
        auto* be = openBlockEntity(BlockEntity::Kind::Chest);
        if (!be) return;
        menu->type = MenuType::Chest;
        menu->container = be->chest.slots;
        menu->containerCount = ChestData::kSlots;
        menu->blockEntity = be;
    } else if (name == "minecraft:hopper" || name == "minecraft:dispenser" ||
               name == "minecraft:dropper") {
        const bool hopper = name == "minecraft:hopper";
        const auto kind = hopper ? BlockEntity::Kind::Hopper
            : (name == "minecraft:dropper" ? BlockEntity::Kind::Dropper
                                             : BlockEntity::Kind::Dispenser);
        auto* be = openBlockEntity(kind);
        if (!be) return;
        menu->type = hopper ? MenuType::Hopper : MenuType::Dispenser;
        menu->container = be->generic.slots;
        menu->containerCount = hopper ? 5 : 9;
        menu->blockEntity = be;
    } else if (name == "minecraft:furnace") {
        auto* be = openBlockEntity(BlockEntity::Kind::Furnace);
        if (!be) return;
        menu->type = MenuType::Furnace;
        menu->container = be->furnace.slots;
        menu->containerCount = 3;
        menu->blockEntity = be;
    } else if (name == "minecraft:blast_furnace") {
        auto* be = openBlockEntity(BlockEntity::Kind::Furnace);
        if (!be) return;
        menu->type = MenuType::BlastFurnace;
        menu->container = be->furnace.slots;
        menu->containerCount = 3;
        menu->blockEntity = be;
    } else if (name == "minecraft:smoker") {
        auto* be = openBlockEntity(BlockEntity::Kind::Furnace);
        if (!be) return;
        menu->type = MenuType::Smoker;
        menu->container = be->furnace.slots;
        menu->containerCount = 3;
        menu->blockEntity = be;
    } else if (name == "minecraft:crafting_table") {
        menu->type = MenuType::Crafting;
    } else if (name == "minecraft:enchanting_table") {
        menu->type = MenuType::Enchantment;
        menu->container = menu->extraSlots;
        menu->containerCount = 2;
    } else if (name == "minecraft:anvil" || name == "minecraft:chipped_anvil" ||
               name == "minecraft:damaged_anvil") {
        menu->type = MenuType::Anvil;
        menu->container = menu->extraSlots;
        menu->containerCount = 3;
    } else if (name == "minecraft:brewing_stand") {
        auto* be = openBlockEntity(BlockEntity::Kind::Brewing);
        if (!be) return;
        menu->type = MenuType::Brewing;
        menu->container = be->brewing.slots;
        menu->containerCount = 5;
        menu->blockEntity = be;
    } else if (name == "minecraft:stonecutter") {
        menu->type = MenuType::Stonecutter;
        menu->container = menu->extraSlots;
        menu->containerCount = 2;
    } else if (name == "minecraft:grindstone") {
        menu->type = MenuType::Grindstone;
        menu->container = menu->extraSlots;
        menu->containerCount = 3;
    } else if (name == "minecraft:smithing_table") {
        menu->type = MenuType::Smithing;
        menu->container = menu->extraSlots;
        menu->containerCount = 4;
    } else if (name == "minecraft:beacon") {
        menu->type = MenuType::Beacon;
        menu->container = menu->extraSlots;
        menu->containerCount = 1;
    } else if (name == "minecraft:loom") {
        menu->type = MenuType::Loom;
        menu->container = menu->extraSlots;
        menu->containerCount = 4;
    } else if (name == "minecraft:barrel") {
        auto* be = openBlockEntity(BlockEntity::Kind::Barrel);
        if (!be) return;
        menu->type = MenuType::Barrel;
        menu->container = be->chest.slots;
        menu->containerCount = 27;
        menu->blockEntity = be;
    } else if (name.find("shulker_box") != std::string::npos) {
        auto* be = openBlockEntity(BlockEntity::Kind::ShulkerBox);
        if (!be) return;
        menu->type = MenuType::ShulkerBox;
        menu->container = be->chest.slots;
        menu->containerCount = ChestData::kSlots;
        menu->blockEntity = be;
    } else if (name == "minecraft:crafter") {
        auto* be = openBlockEntity(BlockEntity::Kind::Crafter);
        if (!be) return;
        menu->type = MenuType::Crafter;
        menu->container = be->crafter.slots;
        menu->containerCount = 9;
        menu->crafterDisabledSlots = &be->crafter.disabledSlots;
        menu->blockEntity = be;
    } else if (name == "minecraft:cartography_table") {
        menu->type = MenuType::CartographyTable;
        menu->container = menu->extraSlots;
        menu->containerCount = 3;
    } else if (name == "minecraft:lectern") {
        menu->type = MenuType::Lectern;
        menu->container = menu->extraSlots;
        menu->containerCount = 1;
    } else return;

    if (!menuItemsSerializable(*menu)) {
        sendSystemText(
            "This menu contains item data this server cannot safely encode or edit.");
        return;
    }

    // Opening another screen first closes the previous handler.  Apart from
    // matching vanilla's screen lifecycle, this is important because the
    // previous handler may own a cursor item or transient input slots.  Do
    // this before publishing the new pointer, so no re-entrant packet can
    // observe two active menus.
    closeOpenMenu(true);
    menu->refreshCraftResult(srv_.recipes());

    std::shared_ptr<Menu> activeMenu;
    {
        std::lock_guard playerLock(self_->stateMtx);
        openMenu_ = std::move(menu);
        activeMenu = openMenu_;
    }

    {
        WriteBuffer b;
        b.varint(activeMenu->windowId);
        b.varint(activeMenu->openScreenTypeId());
        const char* title = "Container";
        switch(activeMenu->type) {
            case MenuType::Chest: title="Chest"; break;
            case MenuType::Furnace: title="Furnace"; break;
            case MenuType::BlastFurnace: title="Blast Furnace"; break;
            case MenuType::Smoker: title="Smoker"; break;
            case MenuType::Crafting: title="Crafting"; break;
            case MenuType::Enchantment: title="Enchanting Table"; break;
            case MenuType::Anvil: title="Anvil"; break;
            case MenuType::Brewing: title="Brewing Stand"; break;
            case MenuType::Stonecutter: title="Stonecutter"; break;
            case MenuType::Grindstone: title="Grindstone"; break;
            case MenuType::Smithing: title="Smithing Table"; break;
            case MenuType::Beacon: title="Beacon"; break;
            case MenuType::Loom: title="Loom"; break;
            case MenuType::Barrel: title="Barrel"; break;
            case MenuType::ShulkerBox: title="Shulker Box"; break;
            case MenuType::Hopper: title="Hopper"; break;
            case MenuType::Dispenser: title="Dispenser"; break;
            case MenuType::Crafter: title="Crafter"; break;
            case MenuType::CartographyTable: title="Cartography Table"; break;
            case MenuType::Lectern: title="Lectern"; break;
            case MenuType::Merchant: title="Villager"; break;
            default: title="Container"; break;
        }
        nbt::writeTextComponent(b, title);
        conn_->sendPacket(pl::sc::OpenScreen, b);
    }

    // sendMenuContent takes its own short model snapshot.  The active menu
    // is published before the first packet so a re-entrant client/JVM path
    // cannot click a screen that the session does not yet know about.
    sendMenuContent(*activeMenu);

    MenuType activeType = MenuType::Chest;
    std::int32_t activeWindowId = 0;
    ItemStack anvilLeft = ItemStack::air();
    ItemStack anvilRight = ItemStack::air();
    std::shared_ptr<BlockEntity> propertyOwner;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (openMenu_.get() != activeMenu.get()) return;
        activeType = activeMenu->type;
        activeWindowId = activeMenu->windowId;
        propertyOwner = activeMenu->blockEntityOwner;
        if (activeType == MenuType::Anvil) {
            anvilLeft = activeMenu->extraSlots[0];
            anvilRight = activeMenu->extraSlots[1];
        }
    }
    if (activeType == MenuType::Anvil) {
        ItemStack left = anvilLeft;
        ItemStack right = anvilRight;
        int cost = CostCalculator::anvilCost(left, right, "");
        WriteBuffer pb;
        pb.varint(activeWindowId);
        pb.i16(0);
        pb.i16(static_cast<std::int16_t>(cost < 0 ? 0 : cost));
        conn_->trySendPacket(pl::sc::ContainerSetData, pb);
    } else if (activeType == MenuType::Brewing) {
        int brewTime = 0;
        int fuel = 0;
        bool valid = false;
        if (propertyOwner && propertyOwner->stateMtx) {
            std::lock_guard entityLock(*propertyOwner->stateMtx);
            if (propertyOwner->kind == BlockEntity::Kind::Brewing) {
                brewTime = propertyOwner->brewing.brewTime;
                fuel = propertyOwner->brewing.fuel;
                valid = true;
            }
        }
        if (valid) {
            for (int prop = 0; prop < 2; ++prop) {
                WriteBuffer pb;
                pb.varint(activeWindowId);
                pb.i16(static_cast<std::int16_t>(prop));
                pb.i16(static_cast<std::int16_t>(prop == 0 ? brewTime : fuel));
                conn_->trySendPacket(pl::sc::ContainerSetData, pb);
            }
        }
    } else if (activeType == MenuType::Furnace ||
               activeType == MenuType::BlastFurnace ||
               activeType == MenuType::Smoker) {
        std::array<int, 4> props{};
        bool valid = false;
        if (propertyOwner && propertyOwner->stateMtx) {
            std::lock_guard entityLock(*propertyOwner->stateMtx);
            if (propertyOwner->kind == BlockEntity::Kind::Furnace) {
                const auto& f = propertyOwner->furnace;
                props = {f.cookProgress, f.cookTotal, f.burnTicks,
                         f.burnDuration};
                valid = true;
            }
        }
        if (valid) {
            for (int prop = 0; prop < 4; ++prop) {
                WriteBuffer pb;
                pb.varint(activeWindowId);
                pb.i16(static_cast<std::int16_t>(prop));
                pb.i16(static_cast<std::int16_t>(props[prop]));
                conn_->trySendPacket(pl::sc::ContainerSetData, pb);
            }
        }
    }
}

} // namespace cppfm
