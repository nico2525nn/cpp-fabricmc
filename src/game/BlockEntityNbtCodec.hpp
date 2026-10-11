#pragma once

#include "BlockEntityData.hpp"
#include "BlockEntityPosition.hpp"
#include "../core/NBTValue.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cppfm {

// Pure conversion between one serialized block-entity record and its typed
// value. The caller owns publication, synchronization, indexing, raw-template
// retention, and resource accounting. Encoding starts from the caller's NBT
// template so unknown fields and unsupported inventory entries survive.
class BlockEntityNbtCodec final {
public:
    using DecodedRecord = std::pair<std::int64_t, BlockEntityOwner>;

    static std::optional<std::int64_t> entityKey(const nbt::Value& entity) {
        const auto* x = entity.get("x");
        const auto* y = entity.get("y");
        const auto* z = entity.get("z");
        if (!x || !y || !z || x->tag != nbt::Int || y->tag != nbt::Int ||
            z->tag != nbt::Int)
            return std::nullopt;
        if (!isPackedBlockPosition(x->i, y->i, z->i)) return std::nullopt;
        const auto key = posKey(x->i, y->i, z->i);
        return key;
    }

    static std::optional<DecodedRecord> decodeRecord(const nbt::Value& e) {
        const auto key = entityKey(e);
        const auto* idv = e.get("id");
        if (!key || !idv || idv->tag != nbt::String)
            return std::nullopt;
        const std::string& id = idv->str;
        auto owner = std::make_shared<BlockEntity>();
        BlockEntity& be = *owner;
        if (id == "minecraft:chest" || id == "minecraft:trapped_chest") {
            be.kind = BlockEntity::Kind::Chest;
            if (!readItems(e, be.chest.slots, ChestData::kSlots, "Items"))
                return std::nullopt;
        } else if (id == "minecraft:hopper" || id == "minecraft:dispenser" ||
                   id == "minecraft:dropper") {
            if (id == "minecraft:hopper")
                be.kind = BlockEntity::Kind::Hopper;
            else if (id == "minecraft:dropper")
                be.kind = BlockEntity::Kind::Dropper;
            else
                be.kind = BlockEntity::Kind::Dispenser;
            const int n = be.kind == BlockEntity::Kind::Hopper ? 5 : 9;
            if (!readItems(e, be.generic.slots, n, "Items"))
                return std::nullopt;
        } else if (id == "minecraft:barrel") {
            be.kind = BlockEntity::Kind::Barrel;
            if (!readItems(e, be.chest.slots, ChestData::kSlots, "Items"))
                return std::nullopt;
        } else if (id == "minecraft:shulker_box") {
            be.kind = BlockEntity::Kind::ShulkerBox;
            if (!readItems(e, be.chest.slots, ChestData::kSlots, "Items"))
                return std::nullopt;
        } else if (id == "minecraft:brewing_stand") {
            be.kind = BlockEntity::Kind::Brewing;
            if (!readItems(e, be.brewing.slots, BrewingData::kSlots, "Items"))
                return std::nullopt;
            if (const auto* brewTime = e.get("BrewTime")) {
                if (brewTime->tag != nbt::Short) return std::nullopt;
                be.brewing.brewTime = brewTime->s;
            }
            if (const auto* fuel = e.get("Fuel")) {
                if (fuel->tag != nbt::Byte) return std::nullopt;
                be.brewing.fuel = static_cast<std::int16_t>(fuel->b);
            }
        } else if (id == "minecraft:crafter") {
            be.kind = BlockEntity::Kind::Crafter;
            if (!readItems(e, be.crafter.slots, CrafterData::kSlots, "Items"))
                return std::nullopt;
            if (const auto* disabled = e.get("disabled_slots")) {
                if (disabled->tag == nbt::IntArray) {
                    for (const auto slot : disabled->intArray)
                        be.crafter.setSlotEnabled(slot, false);
                } else if (disabled->tag == nbt::List) {
                    // Be lenient with hand-authored/older fixtures that use
                    // an integer list instead of the vanilla int array.
                    for (const auto& value : disabled->list)
                        if (value.tag == nbt::Int)
                            be.crafter.setSlotEnabled(value.i, false);
                }
            }
            if (const auto* ticks = e.get("crafting_ticks_remaining")) {
                if (ticks->tag == nbt::Int)
                    be.crafter.craftingTicksRemaining = std::max(0, ticks->i);
                else if (ticks->tag == nbt::Short)
                    be.crafter.craftingTicksRemaining = std::max(
                        0, static_cast<int>(ticks->s));
            }
            if (const auto* triggered = e.get("triggered")) {
                if (triggered->tag == nbt::Byte)
                    be.crafter.triggered = triggered->b != 0;
                else if (triggered->tag == nbt::Int)
                    be.crafter.triggered = triggered->i != 0;
            }
        } else if (id == "minecraft:furnace" || id == "minecraft:smoker" ||
                   id == "minecraft:blast_furnace") {
            be.kind = BlockEntity::Kind::Furnace;
            if (!readItems(e, be.furnace.slots, 3, "Items"))
                return std::nullopt;
            if (const auto* burnTime = e.get("BurnTime")) {
                if (burnTime->tag != nbt::Short) return std::nullopt;
                be.furnace.burnTicks = burnTime->s;
            }
            if (const auto* cookTime = e.get("CookTime")) {
                if (cookTime->tag != nbt::Short) return std::nullopt;
                be.furnace.cookProgress = cookTime->s;
            }
            if (const auto* cookTimeTotal = e.get("CookTimeTotal")) {
                if (cookTimeTotal->tag != nbt::Short) return std::nullopt;
                be.furnace.cookTotal = cookTimeTotal->s;
            }
        } else if (id == "minecraft:sign" || id == "minecraft:hanging_sign") {
            be.kind = BlockEntity::Kind::Sign;
            readSignSide(e, "front_text", be.sign.front, be.sign.hasFront);
            readSignSide(e, "back_text", be.sign.back, be.sign.hasBack);
        } else {
            return std::nullopt;
        }
        return DecodedRecord{*key, std::move(owner)};
    }

    // Returns false for transient moving-piston values, which have a distinct
    // initial-chunk projection and must not enter durable chunk NBT.
    static bool encodeRecord(nbt::Value& e, const BlockEntityState& be,
                             std::int32_t x, std::int32_t y,
                             std::int32_t z) {
        bool serialized = false;
        switch (be.kind) {
        case BlockEntity::Kind::Chest:
            setExpectedId(e, "minecraft:chest",
                          {"minecraft:chest", "minecraft:trapped_chest"});
            writeItems(e, be.chest.slots, ChestData::kSlots, "Items");
            serialized = true;
            break;
        case BlockEntity::Kind::Hopper:
            setExpectedId(e, "minecraft:hopper", {"minecraft:hopper"});
            writeItems(e, be.generic.slots, 5, "Items");
            serialized = true;
            break;
        case BlockEntity::Kind::Dispenser:
            setExpectedId(e, "minecraft:dispenser", {"minecraft:dispenser"});
            writeItems(e, be.generic.slots, 9, "Items");
            serialized = true;
            break;
        case BlockEntity::Kind::Dropper:
            setExpectedId(e, "minecraft:dropper", {"minecraft:dropper"});
            writeItems(e, be.generic.slots, 9, "Items");
            serialized = true;
            break;
        case BlockEntity::Kind::Barrel:
            setExpectedId(e, "minecraft:barrel", {"minecraft:barrel"});
            writeItems(e, be.chest.slots, ChestData::kSlots, "Items");
            serialized = true;
            break;
        case BlockEntity::Kind::ShulkerBox:
            setExpectedId(e, "minecraft:shulker_box",
                          {"minecraft:shulker_box"});
            writeItems(e, be.chest.slots, ChestData::kSlots, "Items");
            serialized = true;
            break;
        case BlockEntity::Kind::Brewing:
            setExpectedId(e, "minecraft:brewing_stand",
                          {"minecraft:brewing_stand"});
            writeItems(e, be.brewing.slots, BrewingData::kSlots, "Items");
            e.set("BrewTime", nbt::Value::makeShort(be.brewing.brewTime));
            e.set("Fuel", nbt::Value::makeByte(
                              static_cast<std::int8_t>(be.brewing.fuel)));
            serialized = true;
            break;
        case BlockEntity::Kind::Crafter: {
            setExpectedId(e, "minecraft:crafter", {"minecraft:crafter"});
            writeItems(e, be.crafter.slots, CrafterData::kSlots, "Items");
            nbt::Value disabled;
            disabled.tag = nbt::IntArray;
            for (int slot = 0; slot < CrafterData::kSlots; ++slot)
                if (be.crafter.isSlotDisabled(slot))
                    disabled.intArray.push_back(slot);
            e.set("disabled_slots", std::move(disabled));
            e.set("crafting_ticks_remaining",
                  nbt::Value::makeInt(be.crafter.craftingTicksRemaining));
            e.set("triggered",
                  nbt::Value::makeByte(be.crafter.triggered ? 1 : 0));
            serialized = true;
            break;
        }
        case BlockEntity::Kind::MovingPiston:
            return false;
        case BlockEntity::Kind::Sign:
            setExpectedId(e, "minecraft:sign",
                          {"minecraft:sign", "minecraft:hanging_sign"});
            if (!e.get("is_waxed"))
                e.set("is_waxed", nbt::Value::makeByte(0));
            writeSignSide(e, "front_text", be.sign.front);
            writeSignSide(e, "back_text", be.sign.back);
            serialized = true;
            break;
        case BlockEntity::Kind::Furnace:
            setExpectedId(e, "minecraft:furnace",
                          {"minecraft:furnace", "minecraft:smoker",
                           "minecraft:blast_furnace"});
            writeFurnaceItems(e, be.furnace);
            e.set("BurnTime", nbt::Value::makeShort(be.furnace.burnTicks));
            e.set("CookTime", nbt::Value::makeShort(be.furnace.cookProgress));
            e.set("CookTimeTotal", nbt::Value::makeShort(be.furnace.cookTotal));
            serialized = true;
            break;
        }
        // There is deliberately no default switch arm: adding a new Kind
        // warns at compile time, while invalid values are still rejected here.
        if (!serialized) return false;
        e.set("keepPacked", nbt::Value::makeByte(0));
        e.set("x", nbt::Value::makeInt(x));
        e.set("y", nbt::Value::makeInt(y));
        e.set("z", nbt::Value::makeInt(z));
        return true;
    }

    static int inventorySlotCount(std::string_view id) noexcept {
        if (id == "minecraft:chest" || id == "minecraft:trapped_chest" ||
            id == "minecraft:barrel" || id == "minecraft:shulker_box")
            return ChestData::kSlots;
        if (id == "minecraft:hopper" || id == "minecraft:brewing_stand")
            return 5;
        if (id == "minecraft:dispenser" || id == "minecraft:dropper" ||
            id == "minecraft:crafter")
            return 9;
        if (id == "minecraft:furnace" || id == "minecraft:smoker" ||
            id == "minecraft:blast_furnace")
            return 3;
        return 0;
    }

    static void validateInventoryPayloads(const nbt::Value& root) {
        for (const auto& [name, value] : root.comp) {
            if (name != "block_entities" && name != "TileEntities") continue;
            if (value.tag != nbt::List) continue; // readChunkNbt reports this.
            for (const auto& entity : value.list) {
                if (entity.tag != nbt::Compound) continue;
                const auto* id = entity.get("id");
                if (!id || id->tag != nbt::String) continue;
                const int slotCount = inventorySlotCount(id->str);
                if (slotCount == 0) continue;
                const auto* items = entity.get("Items");
                if (!items) continue;
                if (items->tag != nbt::List)
                    throw std::runtime_error(
                        "known block-entity Items field is not an NBT list");
                if (!items->list.empty() &&
                    items->listElement != nbt::Compound)
                    throw std::runtime_error(
                        "block-entity Items list does not contain compounds");

                std::unordered_set<int> seenSlots;
                seenSlots.reserve(items->list.size());
                for (const auto& item : items->list) {
                    if (item.tag != nbt::Compound)
                        throw std::runtime_error(
                            "block-entity Items list contains a non-compound record");
                    const auto* slotTag = item.get("Slot");
                    if (!slotTag) continue;
                    const int slot = slotTag->tag == nbt::Byte ? slotTag->b
                                   : slotTag->tag == nbt::Short ? slotTag->s
                                   : slotTag->tag == nbt::Int ? slotTag->i : -1;
                    if (slot < 0 || slot >= slotCount) continue;
                    if (!seenSlots.insert(slot).second)
                        throw std::runtime_error(
                            "duplicate saved item in block-entity slot " +
                            std::to_string(slot));
                }
            }
        }
    }

private:
    static void setExpectedId(
        nbt::Value& e, std::string_view fallback,
        std::initializer_list<std::string_view> supportedIds) {
        const auto* current = e.get("id");
        if (current && current->tag == nbt::String &&
            std::find(supportedIds.begin(), supportedIds.end(), current->str) !=
                supportedIds.end())
            return;
        e.set("id", nbt::Value::makeString(std::string(fallback)));
    }

    static void readSignSide(const nbt::Value& e, const char* side,
                             std::string out[4], bool& has) {
        const auto* t = e.get(side);
        if (!t || t->tag != nbt::Compound) return;
        const auto* messages = t->get("messages");
        if (!messages || messages->tag != nbt::List) return;
        for (int i = 0; i < 4 && i < static_cast<int>(messages->list.size()); ++i)
            if (messages->list[i].tag == nbt::String)
                out[i] = messages->list[i].str;
        has = true;
    }

    static void writeSignSide(nbt::Value& e, const char* key,
                              const std::string lines[4]) {
        nbt::Value side = nbt::Value::makeCompound();
        if (const auto* existing = e.get(key);
            existing && existing->tag == nbt::Compound)
            side = *existing;
        nbt::Value messages = nbt::Value::makeList(nbt::String, 4);
        for (int i = 0; i < 4; ++i)
            messages.list.push_back(nbt::Value::makeString(lines[i]));

        // Keep server-filtered sign text when the visible messages have not
        // changed. BlockEntityState intentionally stores only the visible
        // text; after an edit there is no new filter result, so mirror the new
        // messages rather than preserving stale filtered content.
        bool messagesUnchanged = false;
        if (const auto* original = side.get("messages");
            original && original->tag == nbt::List &&
            original->list.size() == 4) {
            messagesUnchanged = true;
            for (std::size_t i = 0; i < original->list.size(); ++i) {
                if (original->list[i].tag != nbt::String ||
                    original->list[i].str != lines[i]) {
                    messagesUnchanged = false;
                    break;
                }
            }
        }
        if (!messagesUnchanged && side.get("filtered_messages"))
            side.set("filtered_messages", messages);
        side.set("messages", std::move(messages));
        if (!side.get("color"))
            side.set("color", nbt::Value::makeString("black"));
        if (!side.get("has_glowing_text"))
            side.set("has_glowing_text", nbt::Value::makeByte(0));
        e.set(key, std::move(side));
    }

    static void writeItems(nbt::Value& e, const ItemStack* slots, int count,
                           const char* listName) {
        std::vector<std::optional<nbt::Value>> originalBySlot(
            static_cast<std::size_t>(count));
        std::vector<nbt::Value> malformedEntries;
        if (const auto* original = e.get(listName);
            original && original->tag == nbt::List) {
            malformedEntries.reserve(original->list.size());
            for (const auto& item : original->list) {
                const auto* slotTag = item.get("Slot");
                int slot = -1;
                if (slotTag) {
                    if (slotTag->tag == nbt::Byte) slot = slotTag->b;
                    else if (slotTag->tag == nbt::Short) slot = slotTag->s;
                    else if (slotTag->tag == nbt::Int) slot = slotTag->i;
                }
                if (slot >= 0 && slot < count) {
                    auto& saved = originalBySlot[static_cast<std::size_t>(slot)];
                    if (!saved) saved = item;
                    else malformedEntries.push_back(item);
                } else {
                    malformedEntries.push_back(item);
                }
            }
        }

        nbt::Value arr = nbt::Value::makeList(nbt::Compound);
        for (int i = 0; i < count; ++i) {
            const auto& stack = slots[i];
            auto& original = originalBySlot[static_cast<std::size_t>(i)];
            if (stack.empty()) {
                // Items absent from the built-in registry cannot be edited by
                // this server, so retain their original record instead of
                // deleting modded/unknown inventory contents on save.
                if (original) {
                    const auto* id = original->get("id");
                    if (!id || id->tag != nbt::String ||
                        !gen::itemIdByName().count(id->str))
                        arr.list.push_back(std::move(*original));
                }
                continue;
            }
            arr.list.push_back(stack.toNbt(i));
        }
        for (auto& item : malformedEntries)
            arr.list.push_back(std::move(item));
        e.set(listName, std::move(arr));
    }

    static void writeFurnaceItems(nbt::Value& e, const FurnaceData& furnace) {
        writeItems(e, furnace.slots, 3, "Items");
    }

    static bool readItems(const nbt::Value& e, ItemStack* slots, int count,
                          const char* listName) {
        const nbt::Value* arr = e.get(listName);
        if (!arr) return true;
        if (arr->tag != nbt::List) return false;
        std::vector<bool> seenSlots(static_cast<std::size_t>(count), false);
        for (const auto& item : arr->list) {
            const auto* slotValue = item.get("Slot");
            if (!slotValue) continue;
            const int slot = slotValue->tag == nbt::Byte ? slotValue->b
                           : slotValue->tag == nbt::Short ? slotValue->s
                           : slotValue->tag == nbt::Int ? slotValue->i : -1;
            if (slot < 0 || slot >= count) continue;
            if (seenSlots[static_cast<std::size_t>(slot)])
                throw std::runtime_error(
                    "duplicate saved item in block-entity slot " +
                    std::to_string(slot));
            seenSlots[static_cast<std::size_t>(slot)] = true;
            auto stack = ItemStack::fromNbt(item);
            if (stack) {
                // This server cannot send opaque component payloads to the
                // vanilla client. Keep the complete block-entity record raw
                // instead of exposing the unsupported item as an empty slot.
                if (stack->persistedNbt &&
                    stack->persistedNbt->hasOpaqueComponents)
                    return false;
                slots[slot] = std::move(*stack);
                continue;
            }
            const auto* itemId = item.get("id");
            if (!itemId || itemId->tag != nbt::String ||
                !gen::itemIdByName().contains(itemId->str))
                return false;
            // A recognized item that fails validation is not an empty slot.
            // Reject the detached block-entity load so its source chunk stays
            // authoritative; otherwise the next save would erase that record.
            throw std::runtime_error(
                "invalid saved item stack in block-entity slot " +
                std::to_string(slot));
        }
        return true;
    }
};

} // namespace cppfm
