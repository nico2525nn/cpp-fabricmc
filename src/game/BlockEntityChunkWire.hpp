#pragma once

#include "../core/NBTValue.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace cppfm {

struct BlockEntityWireType {
    std::string_view name;
    std::int32_t id;
};

// Minecraft 1.21.4's block-entity registry order.  The ids used by the
// chunk packet are registry ids, not block-state ids.  These values were
// checked against map_chunk packets emitted by an unmodified 1.21.4 server.
inline constexpr std::array<BlockEntityWireType, 45>
    kBlockEntityWireTypes{{
        {"minecraft:furnace", 0},
        {"minecraft:chest", 1},
        {"minecraft:trapped_chest", 2},
        {"minecraft:ender_chest", 3},
        {"minecraft:jukebox", 4},
        {"minecraft:dispenser", 5},
        {"minecraft:dropper", 6},
        {"minecraft:sign", 7},
        {"minecraft:hanging_sign", 8},
        {"minecraft:mob_spawner", 9},
        {"minecraft:creaking_heart", 10},
        {"minecraft:piston", 11},
        {"minecraft:brewing_stand", 12},
        {"minecraft:enchanting_table", 13},
        {"minecraft:end_portal", 14},
        {"minecraft:beacon", 15},
        {"minecraft:skull", 16},
        {"minecraft:daylight_detector", 17},
        {"minecraft:hopper", 18},
        {"minecraft:comparator", 19},
        {"minecraft:banner", 20},
        {"minecraft:structure_block", 21},
        {"minecraft:end_gateway", 22},
        {"minecraft:command_block", 23},
        {"minecraft:shulker_box", 24},
        {"minecraft:bed", 25},
        {"minecraft:conduit", 26},
        {"minecraft:barrel", 27},
        {"minecraft:smoker", 28},
        {"minecraft:blast_furnace", 29},
        {"minecraft:lectern", 30},
        {"minecraft:bell", 31},
        {"minecraft:jigsaw", 32},
        {"minecraft:campfire", 33},
        {"minecraft:beehive", 34},
        {"minecraft:sculk_sensor", 35},
        {"minecraft:calibrated_sculk_sensor", 36},
        {"minecraft:sculk_catalyst", 37},
        {"minecraft:sculk_shrieker", 38},
        {"minecraft:chiseled_bookshelf", 39},
        {"minecraft:brushable_block", 40},
        {"minecraft:decorated_pot", 41},
        {"minecraft:crafter", 42},
        {"minecraft:trial_spawner", 43},
        {"minecraft:vault", 44},
    }};

inline std::optional<std::int32_t> blockEntityWireTypeId(
    std::string_view name) noexcept {
    const auto found = std::find_if(
        kBlockEntityWireTypes.begin(), kBlockEntityWireTypes.end(),
        [name](const BlockEntityWireType& entry) {
            return entry.name == name;
        });
    if (found == kBlockEntityWireTypes.end()) return std::nullopt;
    return found->id;
}

inline bool nbtInteger(const nbt::Value* value,
                       std::int32_t& result) noexcept {
    if (!value) return false;
    std::int64_t wide = 0;
    switch (value->tag) {
    case nbt::Byte: wide = value->b; break;
    case nbt::Short: wide = value->s; break;
    case nbt::Int: wide = value->i; break;
    case nbt::Long: wide = value->l; break;
    default: return false;
    }
    if (wide < std::numeric_limits<std::int32_t>::min() ||
        wide > std::numeric_limits<std::int32_t>::max())
        return false;
    result = static_cast<std::int32_t>(wide);
    return true;
}

inline bool blockEntityFieldIsZero(const nbt::Value& value) noexcept {
    switch (value.tag) {
    case nbt::Byte: return value.b == 0;
    case nbt::Short: return value.s == 0;
    case nbt::Int: return value.i == 0;
    case nbt::Long: return value.l == 0;
    default: return false;
    }
}

inline bool inventoryItemsAreMenuOnly(std::string_view blockEntityId) {
    // These inventories are revealed through their container screen. Keep
    // `Items` for other block entities: campfire slots are observable in the
    // world and vanilla includes them in initial/update NBT.
    static constexpr std::array<std::string_view, 13> kMenuOnlyInventories{{
        "minecraft:chest", "minecraft:trapped_chest",
        "minecraft:ender_chest", "minecraft:barrel",
        "minecraft:shulker_box", "minecraft:furnace",
        "minecraft:smoker", "minecraft:blast_furnace",
        "minecraft:hopper", "minecraft:dispenser",
        "minecraft:dropper", "minecraft:brewing_stand",
        "minecraft:crafter",
    }};
    return std::find(kMenuOnlyInventories.begin(),
                     kMenuOnlyInventories.end(), blockEntityId) !=
           kMenuOnlyInventories.end();
}

// Persistence NBT contains identity and location fields that are not part of
// a block entity's client update tag.  Omit empty/default simulation fields so
// an empty furnace or chest is represented like vanilla's absent optional NBT.
inline std::optional<nbt::Value> blockEntityUpdateTag(
    const nbt::Value& stored) {
    if (stored.tag != nbt::Compound) return std::nullopt;
    nbt::Value update = stored;
    const auto* id = stored.get("id");
    const std::string_view blockEntityId =
        id && id->tag == nbt::String ? std::string_view(id->str)
                                     : std::string_view{};
    const bool menuOnlyInventory = inventoryItemsAreMenuOnly(blockEntityId);
    std::erase_if(update.comp, [menuOnlyInventory](const auto& field) {
        const auto& [name, value] = field;
        if (name == "id" || name == "x" || name == "y" || name == "z" ||
            name == "keepPacked")
            return true;
        // Container contents are synchronized by the menu protocol when the
        // player opens a container, not as chunk-visible block-entity state.
        if (name == "Items" && menuOnlyInventory) return true;
        if (name == "disabled_slots" && value.tag == nbt::IntArray &&
            value.intArray.empty())
            return true;
        return (name == "BurnTime" || name == "CookTime" ||
                name == "CookTimeTotal" || name == "BrewTime" ||
                name == "Fuel" || name == "crafting_ticks_remaining" ||
                name == "triggered") && blockEntityFieldIsZero(value);
    });
    if (update.comp.empty()) return std::nullopt;
    return update;
}

inline std::size_t writeChunkBlockEntities(
    WriteBuffer& out, std::int32_t chunkX, std::int32_t chunkZ,
    const nbt::Value* blockEntities) {
    if (blockEntities && blockEntities->tag != nbt::List)
        throw std::invalid_argument("chunk block entities must be an NBT list");

    struct Record {
        const nbt::Value* value;
        std::int32_t x;
        std::int32_t y;
        std::int32_t z;
        std::int32_t type;
    };
    std::vector<Record> records;
    if (blockEntities) records.reserve(blockEntities->list.size());
    if (blockEntities) {
        for (const auto& value : blockEntities->list) {
            if (value.tag != nbt::Compound) continue;
            const auto* id = value.get("id");
            std::int32_t x = 0, y = 0, z = 0;
            if (!id || id->tag != nbt::String ||
                !nbtInteger(value.get("x"), x) ||
                !nbtInteger(value.get("y"), y) ||
                !nbtInteger(value.get("z"), z) ||
                (x >> 4) != chunkX || (z >> 4) != chunkZ ||
                y < std::numeric_limits<std::int16_t>::min() ||
                y > std::numeric_limits<std::int16_t>::max())
                continue;
            const auto type = blockEntityWireTypeId(id->str);
            if (!type) continue;
            records.push_back({&value, x, y, z, *type});
        }
    }

    if (records.size() > static_cast<std::size_t>(
                             std::numeric_limits<std::int32_t>::max()))
        throw std::length_error("too many block entities in a chunk packet");
    out.varint(static_cast<std::int32_t>(records.size()));
    for (const auto& record : records) {
        out.u8(static_cast<std::uint8_t>(
            ((record.x & 15) << 4) | (record.z & 15)));
        out.i16(static_cast<std::int16_t>(record.y));
        out.varint(record.type);
        const auto update = blockEntityUpdateTag(*record.value);
        if (!update) {
            out.u8(nbt::End);
        } else {
            out.u8(nbt::Compound);
            nbt::writePayload(out, *update);
        }
    }
    return records.size();
}

inline bool writeBlockEntityUpdatePacketBody(
    WriteBuffer& out, std::int32_t x, std::int32_t y, std::int32_t z,
    const nbt::Value& stored) {
    const auto* id = stored.get("id");
    if (!id || id->tag != nbt::String) return false;
    const auto type = blockEntityWireTypeId(id->str);
    if (!type) return false;

    out.position(x, y, z);
    out.varint(*type);
    const auto update = blockEntityUpdateTag(stored);
    if (!update) {
        out.u8(nbt::End);
    } else {
        out.u8(nbt::Compound);
        nbt::writePayload(out, *update);
    }
    return true;
}

} // namespace cppfm
