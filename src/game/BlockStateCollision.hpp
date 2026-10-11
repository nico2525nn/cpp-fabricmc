#pragma once

#include "../generated/BlockStates.hpp"

#include <string_view>

namespace cppfm::block_state_collision {

inline bool hasPropertyValue(std::uint32_t state, std::string_view property,
                             std::string_view expected) {
    for (const auto& [name, value] : gen::propsOf(state))
        if (name == property) return value == expected;
    return false;
}

// The generated registry has properties but not full VoxelShape data. Keep
// this deliberately one-sided: only known-empty shapes are classified empty;
// all other registered states retain conservative collision behavior.
inline bool hasKnownEmptyCollisionShape(std::uint32_t state) {
    const auto* block = gen::blockByState(state);
    if (!block) return false;

    const std::string_view name = block->name;
    if (name == "minecraft:air" || name == "minecraft:cave_air" ||
        name == "minecraft:void_air" || name == "minecraft:water" ||
        name == "minecraft:lava" || name == "minecraft:bubble_column")
        return true;

    constexpr std::string_view suffix = "_fence_gate";
    return name.starts_with("minecraft:") && name.ends_with(suffix) &&
           hasPropertyValue(state, "open", "true");
}

} // namespace cppfm::block_state_collision
