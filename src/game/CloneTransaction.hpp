#pragma once

#include "BlockEntities.hpp"
#include "World.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace cppfm::clone_transaction {

enum class ApplyResult {
    Applied,
    AppliedWithNotificationFailure,
    Rejected,
    AnvilRejected,
    SnapshotFailed,
    ApplyFailedRolledBack,
    RollbackFailed,
};

using ReplaceEntity =
    std::function<bool(const BlockEntityStore::NbtReplacement&)>;
using AdmitAnvilProjection = std::function<bool(
    const std::vector<World::BlockUpdate>&,
    const std::vector<BlockEntityStore::NbtReplacement>&)>;

// A validated, zero-copy view over the legacy command inputs. Each entry
// binds the old/new block state and block-entity replacement at one position,
// so transaction code cannot accidentally pair unrelated vector elements.
// The input vectors must outlive the synchronous applyStagedClone call.
struct StagedChangeView {
    std::reference_wrapper<const World::BlockUpdate> originalBlock;
    std::reference_wrapper<const World::BlockUpdate> replacementBlock;
    std::reference_wrapper<const BlockEntityStore::NbtReplacement>
        replacementEntity;
    BlockEntityStore::NbtReplacement originalEntity;
};

class StagedCloneView {
public:
    static std::optional<StagedCloneView> fromLegacyVectors(
        const std::vector<World::BlockUpdate>& replacementBlocks,
        const std::vector<World::BlockUpdate>& originalBlocks,
        const std::vector<BlockEntityStore::NbtReplacement>&
            replacementEntities);

    const std::vector<StagedChangeView>& changes() const noexcept {
        return changes_;
    }

private:
    friend ApplyResult applyStagedClone(
        World& world, BlockEntityStore& entities,
        const std::vector<World::BlockUpdate>& blockUpdates,
        const std::vector<World::BlockUpdate>& originalBlockStates,
        const std::vector<BlockEntityStore::NbtReplacement>& entityUpdates,
        const ReplaceEntity& replaceEntity,
        const AdmitAnvilProjection& admitAnvilProjection);

    explicit StagedCloneView(std::vector<StagedChangeView> changes)
        : changes_(std::move(changes)) {}

    std::vector<StagedChangeView> changes_;
};

ApplyResult applyStagedClone(
    World& world, BlockEntityStore& entities,
    const std::vector<World::BlockUpdate>& blockUpdates,
    const std::vector<World::BlockUpdate>& originalBlockStates,
    const std::vector<BlockEntityStore::NbtReplacement>& entityUpdates,
    const ReplaceEntity& replaceEntity,
    const AdmitAnvilProjection& admitAnvilProjection);

} // namespace cppfm::clone_transaction
