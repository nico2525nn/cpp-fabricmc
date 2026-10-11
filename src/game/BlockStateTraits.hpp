#pragma once

#include <cstdint>
#include <string_view>
#include "../generated/BlockStates.hpp"

namespace cppfm {

inline bool isAirBlockState(std::uint16_t state) noexcept {
    const auto* block = gen::blockByState(state);
    return block && (block->name == "minecraft:air" ||
                     block->name == "minecraft:cave_air" ||
                     block->name == "minecraft:void_air");
}

// The generated registry contains state/property IDs and light opacity, but
// not Minecraft's VoxelShape/sturdy-face tables. Keep this predicate
// conservative for known partial/hazardous blocks; callers that need exact
// collision parity should eventually consume generated shape data instead.
inline bool hasSturdyTopFace(std::uint16_t state) {
    const auto* block = gen::blockByState(state);
    if (!block) return false;
    const auto name = block->name;
    if (isAirBlockState(state) || name == "minecraft:water" ||
        name == "minecraft:lava" || name == "minecraft:bubble_column" ||
        name == "minecraft:fire" || name == "minecraft:soul_fire")
        return false;

    if (name.ends_with("_slab")) {
        for (const auto& [property, value] : gen::propsOf(state))
            if (property == "type")
                return value == "top" || value == "double";
        return false;
    }
    if (name.find("stairs") != std::string_view::npos) {
        for (const auto& [property, value] : gen::propsOf(state))
            if (property == "half") return value == "top";
        return false;
    }

    // These blocks can have an opaque/light-blocking registry definition but
    // do not expose a full sturdy top face (notably fences and walls).
    if (name.ends_with("_fence") || name == "minecraft:fence" ||
        name.ends_with("_fence_gate") || name.ends_with("_wall") ||
        name == "minecraft:wall" || name.ends_with("_pane") ||
        name == "minecraft:iron_bars" || name.ends_with("_carpet") ||
        name.ends_with("_pressure_plate") || name.ends_with("_button") ||
        name.ends_with("_trapdoor") || name.ends_with("_door") ||
        name.ends_with("_rail") || name == "minecraft:rail" ||
        name.ends_with("_torch") || name == "minecraft:torch" ||
        name == "minecraft:chain" || name == "minecraft:ladder" ||
        name == "minecraft:chest" || name.ends_with("_chest") ||
        name == "minecraft:scaffolding" || name.ends_with("_campfire") ||
        name.ends_with("_sign") || name.ends_with("_hanging_sign") ||
        name.ends_with("_banner") || name.ends_with("_head") ||
        name.ends_with("_skull"))
        return false;

    // Most transparent states are plants, thin blocks, or fluids. Permit only
    // transparent full-cube materials with a known sturdy top surface.
    if (block->transparent) {
        return name == "minecraft:glass" ||
               name.ends_with("_stained_glass") ||
               name == "minecraft:ice" ||
               name == "minecraft:packed_ice" ||
               name == "minecraft:blue_ice" ||
               name == "minecraft:slime_block" ||
               name == "minecraft:honey_block" ||
               name == "minecraft:beacon" ||
               name == "minecraft:tinted_glass";
    }
    // filterLight is the registry's full-cube/opaque signal in this generated
    // table. Requiring it avoids treating opaque-but-partial containers and
    // utility blocks as door/portal support. This intentionally favors a
    // false negative over placing blocks on a non-sturdy collision shape.
    return block->filterLight == 15;
}

inline bool hasSafeLandingSupport(std::uint16_t state) {
    if (!hasSturdyTopFace(state)) return false;
    const auto* block = gen::blockByState(state);
    if (!block) return false;
    const auto name = block->name;
    return name != "minecraft:magma_block" &&
           name != "minecraft:cactus" &&
           name != "minecraft:powder_snow" &&
           name != "minecraft:pointed_dripstone";
}

} // namespace cppfm
