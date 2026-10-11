#pragma once

#include "../generated/BlockStates.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace cppfm::entity_support {

// This is deliberately a conservative, state-only approximation. It reports
// the greatest local Y reached by a block's collision shape; it does not
// describe the shape's X/Z footprint. Callers which need exact entity collision
// must use collision voxel shapes rather than treating this as vanilla parity.
inline std::optional<double> topCollisionSurfaceHeight(
    std::uint32_t state) noexcept {
    const auto* block = gen::blockByState(state);
    if (!block) return std::nullopt;

    const std::string_view name = block->name;
    const auto property = [block, state](std::string_view wanted) noexcept
        -> std::optional<std::string_view> {
        if (state < block->minState || state > block->maxState)
            return std::nullopt;

        // Decode one generated state property without allocating the vector
        // returned by gen::propsOf(). This is called from entity ticks.
        std::uint64_t stride = 1;
        for (std::size_t i = block->propCount; i > 0; --i) {
            const std::size_t runIndex =
                static_cast<std::size_t>(block->propsOff) + i - 1;
            if (runIndex >= gen::kBlockPropsRun.size())
                return std::nullopt;
            const auto propertyIndex = gen::kBlockPropsRun[runIndex];
            if (propertyIndex >= gen::kPropDefs.size())
                return std::nullopt;

            const auto& definition = gen::kPropDefs[propertyIndex];
            if (definition.numValues == 0 ||
                stride > std::numeric_limits<std::uint64_t>::max() /
                             definition.numValues)
                return std::nullopt;

            if (definition.name == wanted) {
                const auto stateOffset = state - block->minState;
                const auto valueIndex =
                    (stateOffset / stride) % definition.numValues;
                const std::size_t poolIndex =
                    static_cast<std::size_t>(definition.firstValue) +
                    valueIndex;
                if (poolIndex >= gen::kPropValuePool.size())
                    return std::nullopt;
                return gen::kPropValuePool[poolIndex];
            }
            stride *= definition.numValues;
        }
        return std::nullopt;
    };

    const auto endsWith = [](std::string_view value,
                             std::string_view suffix) noexcept {
        return value.size() >= suffix.size() &&
               value.substr(value.size() - suffix.size()) == suffix;
    };

    // Empty shapes and fluids must never act as a landing surface.
    if (name == "minecraft:air" || name == "minecraft:cave_air" ||
        name == "minecraft:void_air" || name == "minecraft:water" ||
        name == "minecraft:lava" || name == "minecraft:bubble_column" ||
        name == "minecraft:fire" || name == "minecraft:soul_fire" ||
        name == "minecraft:nether_portal" ||
        name == "minecraft:end_portal" ||
        name == "minecraft:structure_void" || name == "minecraft:light")
        return std::nullopt;

    if (endsWith(name, "_slab")) {
        const auto type = property("type");
        if (!type) return std::nullopt;
        if (*type == "bottom") return 0.5;
        if (*type == "top" || *type == "double") return 1.0;
        return std::nullopt;
    }

    if (name == "minecraft:snow") {
        const auto layers = property("layers");
        if (!layers || layers->empty()) return std::nullopt;
        unsigned layerCount = 0;
        for (const char digit : *layers) {
            if (digit < '0' || digit > '9') return std::nullopt;
            layerCount = layerCount * 10u +
                         static_cast<unsigned>(digit - '0');
        }
        if (layerCount < 1 || layerCount > 8) return std::nullopt;
        return static_cast<double>(layerCount) / 8.0;
    }

    // Thin horizontal collision shapes with stable, state-independent heights.
    if (endsWith(name, "_carpet")) return 1.0 / 16.0;
    if (name == "minecraft:farmland" || name == "minecraft:dirt_path")
        return 15.0 / 16.0;
    if (name == "minecraft:scaffolding") return 7.0 / 8.0;
    if (name == "minecraft:campfire" || name == "minecraft:soul_campfire")
        return 7.0 / 16.0;

    // Fences and walls have collision shapes reaching 1.5 blocks high. The
    // returned height is their shape maximum; this API cannot test footprint
    // overlap with the narrow post/arms.
    if (name == "minecraft:fence" || endsWith(name, "_fence") ||
        name == "minecraft:wall" || endsWith(name, "_wall"))
        return 1.5;

    if (endsWith(name, "_fence_gate")) {
        const auto open = property("open");
        if (!open || *open == "true") return std::nullopt;
        if (*open == "false") return 1.5;
        return std::nullopt;
    }

    // Common transparent blocks whose collision is a full cube. Other
    // transparent blocks (plants, panes, containers, etc.) default to none.
    if (name == "minecraft:glass" || name == "minecraft:tinted_glass" ||
        name == "minecraft:ice" || name == "minecraft:packed_ice" ||
        name == "minecraft:blue_ice" || name == "minecraft:barrier" ||
        endsWith(name, "_stained_glass") || endsWith(name, "_leaves"))
        return 1.0;

    // Do not let an opaque/light-blocking registry flag turn known partial or
    // dynamic shapes into a full cube. Unlisted opaque states still require the
    // registry's full-opacity signal below; unknown state ids already failed.
    if (endsWith(name, "_stairs") || endsWith(name, "_trapdoor") ||
        endsWith(name, "_door") || endsWith(name, "_pressure_plate") ||
        endsWith(name, "_button") || endsWith(name, "_rail") ||
        endsWith(name, "_pane") || endsWith(name, "_sign") ||
        endsWith(name, "_banner") || endsWith(name, "_head") ||
        endsWith(name, "_skull") ||
        endsWith(name, "_chest") || endsWith(name, "_shulker_box") ||
        name == "minecraft:anvil" || name == "minecraft:chipped_anvil" ||
        name == "minecraft:damaged_anvil" || name == "minecraft:bell" ||
        name == "minecraft:brewing_stand" || name == "minecraft:cake" ||
        name == "minecraft:cauldron" || endsWith(name, "_cauldron") ||
        name == "minecraft:composter" ||
        name == "minecraft:daylight_detector" ||
        name == "minecraft:decorated_pot" ||
        name == "minecraft:enchanting_table" ||
        name == "minecraft:grindstone" || name == "minecraft:hopper" ||
        name == "minecraft:lectern" || name == "minecraft:end_portal_frame" ||
        name == "minecraft:piston" || name == "minecraft:sticky_piston" ||
        name == "minecraft:piston_head" ||
        name == "minecraft:moving_piston" ||
        name == "minecraft:conduit" || name == "minecraft:heavy_core")
        return std::nullopt;

    if (block->transparent || block->filterLight != 15)
        return std::nullopt;
    return 1.0;
}

} // namespace cppfm::entity_support
