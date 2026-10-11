#pragma once

#include "BlockEntities.hpp"
#include "LootTables.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "../generated/ItemIds.hpp"

namespace cppfm {

inline std::optional<nbt::Value> serializedBlockEntityAt(
    BlockEntityStore& store, std::int32_t x, std::int32_t y,
    std::int32_t z) {
    nbt::Value entities = nbt::Value::makeList(nbt::Compound);
    if (!store.writeChunkNbt(x >> 4, z >> 4, entities))
        return std::nullopt;
    for (auto& entity : entities.list) {
        if (entity.tag != nbt::Compound) return std::nullopt;
        const auto* savedX = entity.get("x");
        const auto* savedY = entity.get("y");
        const auto* savedZ = entity.get("z");
        if (savedX && savedX->tag == nbt::Int &&
            savedY && savedY->tag == nbt::Int &&
            savedZ && savedZ->tag == nbt::Int &&
            savedX->i == x && savedY->i == y && savedZ->i == z)
            return std::move(entity);
    }
    return std::nullopt;
}

enum class PendingLootMaterialization {
    NoPendingLoot,
    Materialized,
    UnsupportedTable,
    InvalidData,
    Failed
};

enum class PendingLootDropPlanStatus {
    NoPendingLoot,
    Ready,
    UnsupportedTable,
    InvalidData
};

struct PendingLootDropPlan final {
    PendingLootDropPlanStatus status =
        PendingLootDropPlanStatus::InvalidData;
    std::vector<ItemStack> items;
};

// This destructive planner is for LootableContainerBlockEntity-style
// inventories. Decorated pots also implement LootableInventory, but their
// one-stack payload and sherd data need a distinct drop transaction.
inline std::optional<std::size_t> pendingLootInventorySlots(
    std::string_view id) noexcept {
    if (id == "minecraft:chest" || id == "minecraft:trapped_chest" ||
        id == "minecraft:barrel")
        return ChestData::kSlots;
    if (id == "minecraft:hopper") return 5;
    if (id == "minecraft:dispenser" || id == "minecraft:dropper" ||
        id == "minecraft:crafter")
        return 9;
    return std::nullopt;
}

inline bool pendingLootBlockEntityMatchesKind(
    const nbt::Value& savedBlockEntity, BlockEntity::Kind kind) noexcept {
    if (savedBlockEntity.tag != nbt::Compound) return false;
    const auto* id = savedBlockEntity.get("id");
    if (!id || id->tag != nbt::String) return false;
    switch (kind) {
        case BlockEntity::Kind::Chest:
            return id->str == "minecraft:chest" ||
                   id->str == "minecraft:trapped_chest";
        case BlockEntity::Kind::Barrel:
            return id->str == "minecraft:barrel";
        case BlockEntity::Kind::Hopper:
            return id->str == "minecraft:hopper";
        case BlockEntity::Kind::Dispenser:
            return id->str == "minecraft:dispenser";
        case BlockEntity::Kind::Dropper:
            return id->str == "minecraft:dropper";
        case BlockEntity::Kind::Crafter:
            return id->str == "minecraft:crafter";
        default:
            return false;
    }
}

// Plan the contents of an unopened chest for a destructive operation without
// mutating its live Block Entity. Callers can commit the block change and
// spawn these drops only after the complete operation has been admitted.
inline PendingLootDropPlan planPendingContainerLootDrops(
    const nbt::Value& savedBlockEntity,
    const LootTableEvaluator& lootTables,
    const LootContext* context = nullptr,
    std::size_t expectedInventorySlots = 0) {
    if (savedBlockEntity.tag != nbt::Compound)
        return {};
    const auto* id = savedBlockEntity.get("id");
    const auto* lootTable = savedBlockEntity.get("LootTable");
    const auto* seedTag = savedBlockEntity.get("LootTableSeed");
    const auto* items = savedBlockEntity.get("Items");
    if (!lootTable) {
        if (seedTag) return {};
        return {PendingLootDropPlanStatus::NoPendingLoot, {}};
    }
    if (!id || id->tag != nbt::String ||
        lootTable->tag != nbt::String ||
        !item_component_nbt::isResourceIdentifier(lootTable->str) ||
        (seedTag && seedTag->tag != nbt::Long) ||
        (items && (items->tag != nbt::List ||
                   (!items->list.empty() &&
                    items->listElement != nbt::Compound) ||
                   !items->list.empty())))
        return {};
    const auto inventorySlots = pendingLootInventorySlots(id->str);
    if (!inventorySlots ||
        (expectedInventorySlots != 0 &&
         expectedInventorySlots != *inventorySlots))
        return {};

    std::optional<std::int64_t> seed;
    if (seedTag && seedTag->l != 0) seed = seedTag->l;
    auto generated = lootTables.evaluateRegisteredTable(
        lootTable->str, ItemStack{}, context, seed);
    if (!generated)
        return {PendingLootDropPlanStatus::UnsupportedTable, {}};
    if (generated->size() > *inventorySlots) return {};
    for (const auto& stack : *generated)
        if (!stack.empty() && !stack.canWriteToProtocol()) return {};
    return {PendingLootDropPlanStatus::Ready, std::move(*generated)};
}

// Transfer generated-structure loot metadata from the transient worldgen queue
// into the chest's normal chunk NBT. A queue alone is lost on process restart;
// retaining the table in the block-entity template lets chunk save/load and
// the existing lazy materializer own its lifetime.
inline bool persistPendingContainerLootAt(
    BlockEntityStore& store, std::int32_t x, std::int32_t y,
    std::int32_t z, std::string_view tableId) {
    if (!item_component_nbt::isResourceIdentifier(tableId)) return false;

    nbt::Value entities = nbt::Value::makeList(nbt::Compound);
    if (!store.writeChunkNbt(x >> 4, z >> 4, entities)) return false;

    std::optional<nbt::Value> chest;
    for (auto& entity : entities.list) {
        if (entity.tag != nbt::Compound) return false;
        const auto* savedX = entity.get("x");
        const auto* savedY = entity.get("y");
        const auto* savedZ = entity.get("z");
        if (savedX && savedX->tag == nbt::Int &&
            savedY && savedY->tag == nbt::Int &&
            savedZ && savedZ->tag == nbt::Int &&
            savedX->i == x && savedY->i == y && savedZ->i == z) {
            chest = std::move(entity);
            break;
        }
    }

    if (chest) {
        const auto* id = chest->get("id");
        if (!id || id->tag != nbt::String ||
            (id->str != "minecraft:chest" &&
             id->str != "minecraft:trapped_chest"))
            return false;
        const auto* existingTable = chest->get("LootTable");
        if (existingTable) {
            return existingTable->tag == nbt::String &&
                   existingTable->str == tableId;
        }
        const auto* items = chest->get("Items");
        if (items && (items->tag != nbt::List || !items->list.empty()))
            return false;
    } else {
        chest = nbt::Value::makeCompound();
        chest->set("id", nbt::Value::makeString("minecraft:chest"));
        chest->set("x", nbt::Value::makeInt(x));
        chest->set("y", nbt::Value::makeInt(y));
        chest->set("z", nbt::Value::makeInt(z));
        chest->set("Items", nbt::Value::makeList(nbt::Compound));
    }

    chest->set("LootTable", nbt::Value::makeString(std::string(tableId)));
    return store.replaceNbtAt(x, y, z, std::move(chest));
}

inline PendingLootMaterialization materializePendingLootAt(
    BlockEntityStore& store, const LootTableEvaluator& lootTables,
    std::int32_t x, std::int32_t y, std::int32_t z,
    std::size_t inventorySlots, const LootContext* context = nullptr) {
    const auto saved = serializedBlockEntityAt(store, x, y, z);
    if (!saved) return PendingLootMaterialization::Failed;
    const auto* lootTable = saved->get("LootTable");
    if (!lootTable) return PendingLootMaterialization::NoPendingLoot;
    const auto* seedTag = saved->get("LootTableSeed");
    if (lootTable->tag != nbt::String ||
        !item_component_nbt::isResourceIdentifier(lootTable->str) ||
        (seedTag && seedTag->tag != nbt::Long) ||
        inventorySlots == 0 || inventorySlots > ChestData::kSlots)
        return PendingLootMaterialization::InvalidData;

    std::optional<std::int64_t> seed;
    if (seedTag && seedTag->l != 0) seed = seedTag->l;
    const auto generated = lootTables.evaluateRegisteredTable(
        lootTable->str, ItemStack{}, context, seed);
    if (!generated) return PendingLootMaterialization::UnsupportedTable;
    if (generated->size() > inventorySlots)
        return PendingLootMaterialization::InvalidData;

    auto updated = *saved;
    updated.comp.erase(
        std::remove_if(updated.comp.begin(), updated.comp.end(),
            [](const auto& field) {
                return field.first == "LootTable" ||
                       field.first == "LootTableSeed";
            }),
        updated.comp.end());
    auto items = nbt::Value::makeList(nbt::Compound);
    items.list.reserve(generated->size());
    for (std::size_t slot = 0; slot < generated->size(); ++slot) {
        const auto& stack = (*generated)[slot];
        if (stack.empty()) continue;
        if (!stack.canWriteToProtocol())
            return PendingLootMaterialization::InvalidData;
        items.list.push_back(stack.toNbt(static_cast<std::int32_t>(slot)));
    }
    updated.set("Items", std::move(items));
    if (!store.replaceNbtAt(x, y, z, std::move(updated)))
        return PendingLootMaterialization::Failed;
    return PendingLootMaterialization::Materialized;
}

inline std::optional<ItemStack> makeShulkerBoxDropItem(
    std::string_view blockName, const nbt::Value& savedBlockEntity) {
    constexpr std::string_view kSuffix = "_shulker_box";
    if (!blockName.starts_with("minecraft:") ||
        (blockName != "minecraft:shulker_box" &&
         !blockName.ends_with(kSuffix)) ||
        savedBlockEntity.tag != nbt::Compound)
        return std::nullopt;

    // Only standard shulker metadata is accepted here. Unknown mod-owned
    // fields remain a fail-closed case; vanilla Lock is intentionally not an
    // item component, while pending loot is represented by container_loot.
    std::unordered_set<std::string_view> seenFields;
    for (const auto& [key, _] : savedBlockEntity.comp) {
        if (key != "id" && key != "x" && key != "y" && key != "z" &&
            key != "keepPacked" && key != "Items" && key != "CustomName" &&
            key != "Lock" && key != "LootTable" &&
            key != "LootTableSeed")
            return std::nullopt;
        if (!seenFields.emplace(key).second) return std::nullopt;
    }

    const auto* id = savedBlockEntity.get("id");
    const auto* x = savedBlockEntity.get("x");
    const auto* y = savedBlockEntity.get("y");
    const auto* z = savedBlockEntity.get("z");
    const auto* keepPacked = savedBlockEntity.get("keepPacked");
    const auto* savedItems = savedBlockEntity.get("Items");
    const auto* customName = savedBlockEntity.get("CustomName");
    const auto* lock = savedBlockEntity.get("Lock");
    const auto* lootTable = savedBlockEntity.get("LootTable");
    const auto* lootTableSeed = savedBlockEntity.get("LootTableSeed");
    const bool hasPendingLoot = lootTable != nullptr;
    if (!id || id->tag != nbt::String || id->str != "minecraft:shulker_box" ||
        !x || x->tag != nbt::Int || !y || y->tag != nbt::Int ||
        !z || z->tag != nbt::Int || !keepPacked ||
        keepPacked->tag != nbt::Byte || keepPacked->b != 0 ||
        (savedItems && savedItems->tag != nbt::List) ||
        (savedItems && savedItems->list.size() > 27) ||
        (savedItems && !savedItems->list.empty() &&
         savedItems->listElement != nbt::Compound) ||
        (customName && customName->tag != nbt::String) ||
        (lock && lock->tag != nbt::String) ||
        (lootTable && (lootTable->tag != nbt::String ||
                       !item_component_nbt::isResourceIdentifier(
                           lootTable->str))) ||
        (lootTableSeed && !lootTable) ||
        (lootTableSeed && lootTableSeed->tag != nbt::Long))
        return std::nullopt;

    const auto itemId = gen::itemIdByName().find(std::string(blockName));
    if (itemId == gen::itemIdByName().end()) return std::nullopt;
    ItemStack result = ItemStack::of(itemId->second);
    if (hasPendingLoot) {
        const auto componentId = item_component_nbt::componentId(
            "minecraft:container_loot");
        auto loot = nbt::Value::makeCompound();
        loot.set("loot_table", *lootTable);
        if (lootTableSeed && lootTableSeed->l != 0)
            loot.set("seed", *lootTableSeed);
        const auto component = item_component_nbt::protocolComponentFromNbt(
            "minecraft:container_loot", loot);
        if (!componentId || !component || component->id != *componentId)
            return std::nullopt;
        result.components.emplace_back(component->id, component->payload);
    } else if (savedItems && !savedItems->list.empty()) {
        const auto componentId = item_component_nbt::componentId(
            "minecraft:container");
        if (!componentId) return std::nullopt;

        std::unordered_set<std::int32_t> seenSlots;
        nbt::Value contents = nbt::Value::makeList(nbt::Compound);
        contents.list.reserve(savedItems->list.size());
        for (const auto& savedStack : savedItems->list) {
            if (savedStack.tag != nbt::Compound) return std::nullopt;
            const auto* slotTag = savedStack.get("Slot");
            std::optional<std::int32_t> slot;
            if (slotTag) {
                if (slotTag->tag == nbt::Byte) slot = slotTag->b;
                else if (slotTag->tag == nbt::Short) slot = slotTag->s;
                else if (slotTag->tag == nbt::Int) slot = slotTag->i;
            }
            if (!slot || *slot < 0 || *slot >= 27 ||
                !seenSlots.insert(*slot).second)
                return std::nullopt;

            nbt::Value stackNbt = savedStack;
            stackNbt.comp.erase(std::remove_if(
                stackNbt.comp.begin(), stackNbt.comp.end(),
                [](const auto& field) { return field.first == "Slot"; }),
                stackNbt.comp.end());
            nbt::Value entry = nbt::Value::makeCompound();
            entry.set("slot", nbt::Value::makeInt(*slot));
            entry.set("item", std::move(stackNbt));
            contents.list.push_back(std::move(entry));
        }

        const auto component = item_component_nbt::protocolComponentFromNbt(
            "minecraft:container", contents);
        if (!component || component->id != *componentId)
            return std::nullopt;
        result.components.emplace_back(component->id, component->payload);
    }
    if (customName) {
        const auto customNameComponentId = item_component_nbt::componentId(
            "minecraft:custom_name");
        const auto customNameComponent =
            item_component_nbt::protocolComponentFromNbt(
                "minecraft:custom_name", *customName);
        if (!customNameComponentId || !customNameComponent ||
            customNameComponent->id != *customNameComponentId)
            return std::nullopt;
        result.components.emplace_back(customNameComponent->id,
                                       customNameComponent->payload);
    }
    return result;
}

struct ShulkerBoxItemData {
    std::array<ItemStack, ChestData::kSlots> slots{};
    std::optional<nbt::Value> customName;
    std::optional<nbt::Value> containerLoot;
};

inline std::optional<ShulkerBoxItemData> shulkerBoxDataFromItem(
    const ItemStack& item) {
    if (!item.canWriteToProtocol()) return std::nullopt;
    const auto containerComponentId = item_component_nbt::componentId(
        "minecraft:container");
    const auto customNameComponentId = item_component_nbt::componentId(
        "minecraft:custom_name");
    const auto containerLootComponentId = item_component_nbt::componentId(
        "minecraft:container_loot");
    if (!containerComponentId || !customNameComponentId ||
        !containerLootComponentId ||
        !item.removedComponents.empty())
        return std::nullopt;

    for (const auto& [componentId, _] : item.components)
        if (componentId != *containerComponentId &&
            componentId != *customNameComponentId &&
            componentId != *containerLootComponentId)
            return std::nullopt;

    ShulkerBoxItemData data;
    const auto component = std::find_if(
        item.components.begin(), item.components.end(),
        [containerComponentId](const auto& entry) {
            return entry.first == *containerComponentId;
        });
    if (component != item.components.end()) {
        const auto contents =
            item_component_nbt::containerComponentProtocolToNbt(
                component->second, 0);
        if (!contents || contents->tag != nbt::List ||
            (!contents->list.empty() &&
             contents->listElement != nbt::Compound))
            return std::nullopt;

        std::unordered_set<std::int32_t> seenSlots;
        for (const auto& entry : contents->list) {
            if (entry.tag != nbt::Compound) return std::nullopt;
            const auto* slotTag = entry.get("slot");
            const auto* savedItem = entry.get("item");
            if (!slotTag || slotTag->tag != nbt::Int || !savedItem ||
                slotTag->i < 0 || slotTag->i >= ChestData::kSlots ||
                !seenSlots.insert(slotTag->i).second)
                return std::nullopt;
            auto stack = ItemStack::fromNbt(*savedItem);
            if (!stack || stack->empty() || !stack->canWriteToProtocol())
                return std::nullopt;
            data.slots[static_cast<std::size_t>(slotTag->i)] =
                std::move(*stack);
        }
    }

    const auto nameComponent = std::find_if(
        item.components.begin(), item.components.end(),
        [customNameComponentId](const auto& entry) {
            return entry.first == *customNameComponentId;
        });
    if (nameComponent != item.components.end()) {
        const auto savedItem = item.toNbt(-1);
        const auto* components = savedItem.get("components");
        const auto* customName = components &&
                                         components->tag == nbt::Compound
                                     ? components->get("minecraft:custom_name")
                                     : nullptr;
        if (!customName || customName->tag != nbt::String)
            return std::nullopt;
        data.customName = *customName;
    }
    const auto lootComponent = std::find_if(
        item.components.begin(), item.components.end(),
        [containerLootComponentId](const auto& entry) {
            return entry.first == *containerLootComponentId;
    });
    if (lootComponent != item.components.end()) {
        if (std::any_of(data.slots.begin(), data.slots.end(),
                        [](const ItemStack& stack) {
                            return !stack.empty();
                        }))
            return std::nullopt;
        const auto savedItem = item.toNbt(-1);
        const auto* components = savedItem.get("components");
        const auto* containerLoot = components &&
                                            components->tag == nbt::Compound
                                        ? components->get(
                                              "minecraft:container_loot")
                                        : nullptr;
        if (!containerLoot ||
            !item_component_nbt::validContainerLoot(*containerLoot))
            return std::nullopt;
        data.containerLoot = *containerLoot;
    }
    return data;
}

inline std::optional<std::array<ItemStack, ChestData::kSlots>>
shulkerBoxContentsFromItem(const ItemStack& item) {
    auto data = shulkerBoxDataFromItem(item);
    if (!data) return std::nullopt;
    return std::move(data->slots);
}

inline nbt::Value shulkerBoxBlockEntityNbt(
    const ShulkerBoxItemData& data, std::int32_t x, std::int32_t y,
    std::int32_t z) {
    nbt::Value entity = nbt::Value::makeCompound();
    entity.set("id", nbt::Value::makeString("minecraft:shulker_box"));
    entity.set("x", nbt::Value::makeInt(x));
    entity.set("y", nbt::Value::makeInt(y));
    entity.set("z", nbt::Value::makeInt(z));
    entity.set("keepPacked", nbt::Value::makeByte(0));
    nbt::Value items = nbt::Value::makeList(nbt::Compound);
    for (std::size_t slot = 0; slot < data.slots.size(); ++slot)
        if (!data.slots[slot].empty())
            items.list.push_back(data.slots[slot].toNbt(
                static_cast<std::int32_t>(slot)));
    entity.set("Items", std::move(items));
    if (data.customName)
        entity.set("CustomName", *data.customName);
    if (data.containerLoot) {
        const auto* lootTable = data.containerLoot->get("loot_table");
        const auto* seed = data.containerLoot->get("seed");
        if (lootTable) entity.set("LootTable", *lootTable);
        if (seed) entity.set("LootTableSeed", *seed);
    }
    return entity;
}

} // namespace cppfm
