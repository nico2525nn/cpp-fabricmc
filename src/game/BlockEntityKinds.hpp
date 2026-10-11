#pragma once

#include "BlockEntityData.hpp"
#include "../generated/BlockStates.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace cppfm {

// Keep the mapping from block state to its persistent owner in one place.
// Block placement, automation and commands must agree on which inventory
// object is represented by a block; silently choosing different kinds can
// discard or reinterpret saved state.
inline std::optional<BlockEntity::Kind> blockEntityKindForBlockName(
    std::string_view name) noexcept {
    if (name.find("chest") != std::string_view::npos &&
        name.find("ender") == std::string_view::npos)
        return BlockEntity::Kind::Chest;
    if (name == "minecraft:barrel") return BlockEntity::Kind::Barrel;
    if (name.find("shulker_box") != std::string_view::npos)
        return BlockEntity::Kind::ShulkerBox;
    if (name == "minecraft:sign" || name == "minecraft:hanging_sign" ||
        name.ends_with("_sign"))
        return BlockEntity::Kind::Sign;
    if (name == "minecraft:hopper") return BlockEntity::Kind::Hopper;
    if (name == "minecraft:dispenser") return BlockEntity::Kind::Dispenser;
    if (name == "minecraft:dropper") return BlockEntity::Kind::Dropper;
    if (name == "minecraft:furnace" ||
        name == "minecraft:blast_furnace" ||
        name == "minecraft:smoker")
        return BlockEntity::Kind::Furnace;
    if (name == "minecraft:brewing_stand") return BlockEntity::Kind::Brewing;
    if (name == "minecraft:crafter") return BlockEntity::Kind::Crafter;
    return std::nullopt;
}

inline std::optional<BlockEntity::Kind> blockEntityKindForBlockState(
    std::uint16_t state) noexcept {
    const auto* block = gen::blockByState(state);
    return block ? blockEntityKindForBlockName(block->name) : std::nullopt;
}

} // namespace cppfm
