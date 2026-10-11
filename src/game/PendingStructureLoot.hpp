#pragma once

#include "BlockEntityDrops.hpp"
#include "BlockEntities.hpp"
#include "World.hpp"
#include "../worldgen/StructureManager.hpp"

#include <cstdint>
#include <limits>
#include <vector>

namespace cppfm {

struct PendingStructureLootDrainResult final {
    std::size_t persisted = 0;
    std::size_t discarded = 0;
    std::size_t deferred = 0;
    std::size_t missingChunks = 0;
    std::size_t publicationFailures = 0;
};

// Preload every chunk referenced by the queue without holding World's
// mutation gate. Queue entries remain visible to save-admission during these
// loads, so a generated side effect cannot be snapshotted before transfer.
inline bool loadChunksForPendingStructureLoot(
    World& world, worldgen::StructureManager& structures) {
    bool loadedAll = true;
    for (const auto& [cx, cz] : structures.pendingLootChunks()) {
        if (!world.hasChunk(cx, cz) &&
            !world.generateChunkIfMissing(cx, cz))
            loadedAll = false;
    }
    return loadedAll;
}

// Transfer due generated-container loot into persistent Block Entity NBT.
// The World mutation gate spans queue removal and the corresponding Block
// Entity publication: a concurrent chunk snapshot can therefore observe
// either the still-pending queue entry (admission rejects it), or the fully
// published LootTable value, never the gap between those states.
inline PendingStructureLootDrainResult drainPendingStructureLoot(
    World& world, BlockEntityStore& blockEntities,
    worldgen::StructureManager& structures, std::int64_t currentTick) {
    PendingStructureLootDrainResult result;
    std::vector<worldgen::StructureManager::PendingLoot> pending;
    auto mutationGate = world.lockMutationGate();
    structures.drainPendingLoot(pending, currentTick);

    const auto defer = [&](worldgen::StructureManager::PendingLoot&& loot) {
        constexpr std::int64_t kRetryDelayTicks = 20 * 60;
        const auto retryAfter =
            currentTick > std::numeric_limits<std::int64_t>::max() -
                              kRetryDelayTicks
                ? std::numeric_limits<std::int64_t>::max()
                : currentTick + kRetryDelayTicks;
        structures.requeuePendingLoot(std::move(loot), retryAfter);
        ++result.deferred;
    };

    for (auto& loot : pending) {
        const auto x = loot.pos[0];
        const auto y = loot.pos[1];
        const auto z = loot.pos[2];
        // getBlock() intentionally returns air for an absent chunk. That is
        // not evidence that a generated chest was replaced: keep the queue
        // authoritative until its owning chunk is resident again.
        if (!world.hasChunk(x >> 4, z >> 4)) {
            ++result.missingChunks;
            defer(std::move(loot));
            continue;
        }
        const auto* block = gen::blockByState(world.getBlock(x, y, z));
        if (!block || (block->name != "minecraft:chest" &&
                       block->name != "minecraft:trapped_chest")) {
            // The generated container was replaced or removed before its
            // deferred loot was applied; never resurrect stale Block Entity
            // state over the new block.
            ++result.discarded;
            continue;
        }

        bool persisted = false;
        try {
            persisted = persistPendingContainerLootAt(
                blockEntities, x, y, z, loot.lootTable);
        } catch (...) {
            // Keep the queue authoritative if staging the Block Entity
            // replacement fails (including allocation failure).
        }
        if (persisted) {
            ++result.persisted;
            continue;
        }

        ++result.publicationFailures;
        defer(std::move(loot));
    }
    return result;
}

} // namespace cppfm
