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

// The registry has state properties but no VoxelShape data. Keep this list
// deliberately one-sided: only states whose empty collision shape is known
// are classified as empty; every other known state remains conservative.
inline bool hasKnownEmptyCollisionShape(std::uint32_t state) {
    const auto* block = gen::blockByState(state);
    if (!block) return false;

    const std::string_view name = block->name;
    if (name == "minecraft:air" || name == "minecraft:cave_air" ||
        name == "minecraft:void_air" || name == "minecraft:water" ||
        name == "minecraft:lava" || name == "minecraft:bubble_column")
        return true;

    constexpr std::string_view kFenceGateSuffix = "_fence_gate";
    return name.starts_with("minecraft:") && name.ends_with(kFenceGateSuffix) &&
           hasPropertyValue(state, "open", "true");
}

}  // namespace cppfm::block_state_collision
