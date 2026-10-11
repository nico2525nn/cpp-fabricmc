#include "CommandModule.hpp"
#include "BlockEntityChunkWire.hpp"
#include "BlockPredicate.hpp"
#include "BlockStateTraits.hpp"
#include "CloneTransaction.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cppfm {
namespace {
enum class CloneMode { Normal, Force, Move };

CloneMode parseCloneMode(std::string_view mode) {
    if (mode == "force") return CloneMode::Force;
    if (mode == "move") return CloneMode::Move;
    return CloneMode::Normal;
}

using block_predicate::ParsedBlockPredicate;
using block_predicate::blockMatchesPredicate;
using block_predicate::parseBlockPredicate;
using block_predicate::validateBlockPredicate;
} // namespace

void GameServer::initCloneCommand() {
    auto& d = commands_;
    {
        auto clone = CommandNode::literal("clone");
        auto from = CommandNode::argument("from", args::blockPos());
        auto to = CommandNode::argument("to", args::blockPos());
        auto target = CommandNode::argument("target", args::blockPos());
        auto doClone = [&](CommandContext& c, bool masked, bool filtered,
                           std::string filter, CloneMode mode) -> int {
            Player* src=static_cast<Player*>(c.source.player);
            const auto dimension = commandDimension(c.source);
            World& targetWorld = worldFor(dimension);
            auto f=c.arg("from").asBlockPos(); auto t=c.arg("to").asBlockPos(); auto dst=c.arg("target").asBlockPos();
            int minX=std::min(f.x,t.x), maxX=std::max(f.x,t.x);
            int minY=std::min(f.y,t.y), maxY=std::max(f.y,t.y);
            int minZ=std::min(f.z,t.z), maxZ=std::max(f.z,t.z);
            constexpr std::int64_t maxVolume = 32768;
            const auto widthX = static_cast<std::int64_t>(maxX) - minX + 1;
            const auto widthY = static_cast<std::int64_t>(maxY) - minY + 1;
            const auto widthZ = static_cast<std::int64_t>(maxZ) - minZ + 1;
            if (widthX > maxVolume || widthY > maxVolume ||
                widthZ > maxVolume)
                throw std::runtime_error("Volume too large (max 32768)");
            const std::int64_t vol = widthX * widthY * widthZ;
            if (vol > maxVolume)
                throw std::runtime_error("Volume too large " +
                                         std::to_string(vol));
            const auto geometry = targetWorld.verticalGeometry();
            const auto destinationMaxY =
                static_cast<std::int64_t>(dst.y) + widthY - 1;
            const auto destinationMaxX =
                static_cast<std::int64_t>(dst.x) + widthX - 1;
            const auto destinationMaxZ =
                static_cast<std::int64_t>(dst.z) + widthZ - 1;
            constexpr auto minCoordinate =
                static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min());
            constexpr auto maxCoordinate =
                static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());
            if (destinationMaxX > maxCoordinate ||
                destinationMaxZ > maxCoordinate ||
                destinationMaxX < minCoordinate ||
                destinationMaxZ < minCoordinate) {
                sendFeedback(src, "Cannot clone beyond the coordinate range");
                return 0;
            }
            if (!geometry.containsY(minY) || !geometry.containsY(maxY) ||
                !geometry.containsY(dst.y) ||
                destinationMaxY >= geometry.maxY()) {
                sendFeedback(src, "Cannot clone outside the world's build height");
                return 0;
            }
            const bool overlaps =
                static_cast<std::int64_t>(minX) <= destinationMaxX &&
                static_cast<std::int64_t>(dst.x) <= maxX &&
                static_cast<std::int64_t>(minY) <= destinationMaxY &&
                static_cast<std::int64_t>(dst.y) <= maxY &&
                static_cast<std::int64_t>(minZ) <= destinationMaxZ &&
                static_cast<std::int64_t>(dst.z) <= maxZ;
            if (overlaps && mode == CloneMode::Normal) {
                sendFeedback(src, "Source and destination cannot overlap");
                return 0;
            }

            const auto loadChunkRectangle = [&](std::int64_t firstX,
                                                std::int64_t firstZ,
                                                std::int64_t lastX,
                                                std::int64_t lastZ) {
                for (std::int64_t cx = firstX >> 4; cx <= (lastX >> 4); ++cx)
                    for (std::int64_t cz = firstZ >> 4;
                         cz <= (lastZ >> 4); ++cz)
                        if (!targetWorld.generateChunkIfMissing(
                                static_cast<std::int32_t>(cx),
                                static_cast<std::int32_t>(cz)))
                            return false;
                return true;
            };
            if (!loadChunkRectangle(minX, minZ, maxX, maxZ) ||
                !loadChunkRectangle(dst.x, dst.z, destinationMaxX,
                                    destinationMaxZ)) {
                sendFeedback(src, "Could not load every chunk for clone");
                return 0;
            }

            // Keep the chunk/entity snapshot, budget preflight, and later
            // writes in one world mutation order. Otherwise a concurrent
            // chunk publication or block edit could invalidate the capacity
            // decision between checking and applying the clone.
            auto mutationGate = targetWorld.lockMutationGate();

            auto& entityStore = blockEntitiesFor(dimension);
            const auto movingPistonState =
                gen::blockNameToState().find("minecraft:moving_piston");
            const auto hasMovingPistonAt = [&](std::int32_t x,
                                                std::int32_t y,
                                                std::int32_t z) {
                const auto state = targetWorld.getBlock(x, y, z);
                return (movingPistonState != gen::blockNameToState().end() &&
                        state == movingPistonState->second) ||
                    entityStore.hasCurrentKind(
                        posKey(x, y, z), BlockEntity::Kind::MovingPiston);
            };
            std::unordered_map<std::int64_t, nbt::Value> sourceEntities;
            for (std::int64_t cx = (static_cast<std::int64_t>(minX) >> 4);
                 cx <= (static_cast<std::int64_t>(maxX) >> 4); ++cx) {
                for (std::int64_t cz = (static_cast<std::int64_t>(minZ) >> 4);
                     cz <= (static_cast<std::int64_t>(maxZ) >> 4); ++cz) {
                    auto entities = nbt::Value::makeList(nbt::Compound);
                    if (!entityStore.writeChunkNbt(
                            static_cast<std::int32_t>(cx),
                            static_cast<std::int32_t>(cz), entities)) {
                        sendFeedback(src,
                            "Could not snapshot source block entities; no changes were applied");
                        return 0;
                    }
                    for (auto& entity : entities.list) {
                        const auto* x = entity.get("x");
                        const auto* y = entity.get("y");
                        const auto* z = entity.get("z");
                        if (!x || x->tag != nbt::Int ||
                            !y || y->tag != nbt::Int ||
                            !z || z->tag != nbt::Int ||
                            x->i < minX || x->i > maxX ||
                            y->i < minY || y->i > maxY ||
                            z->i < minZ || z->i > maxZ)
                            continue;
                        sourceEntities.insert_or_assign(
                            posKey(x->i, y->i, z->i), std::move(entity));
                    }
                }
            }

            std::optional<ParsedBlockPredicate> filterPredicate;
            if (filtered) {
                filterPredicate = parseBlockPredicate(filter);
                validateBlockPredicate(*filterPredicate, datapackManager_.tagManager);
            }
            // copy to tmp to handle overlap
            struct Entry {
                int x, y, z;
                std::uint16_t state;
                std::optional<nbt::Value> blockEntity;
            };
            std::vector<Entry> tmp; tmp.reserve((size_t)vol);
            for (std::int64_t y = minY; y <= maxY; ++y)
                for (std::int64_t z = minZ; z <= maxZ; ++z)
                    for (std::int64_t x = minX; x <= maxX; ++x) {
                        const auto ix = static_cast<std::int32_t>(x);
                        const auto iy = static_cast<std::int32_t>(y);
                        const auto iz = static_cast<std::int32_t>(z);
                        const auto state = targetWorld.getBlock(ix, iy, iz);
                        if (masked && isAirBlockState(state)) continue;
                        if (filterPredicate && !blockMatchesPredicate(
                                *filterPredicate, state, datapackManager_.tagManager))
                            continue;
                        if (hasMovingPistonAt(ix, iy, iz)) {
                            sendFeedback(src,
                                "Cannot clone a moving piston animation");
                            return 0;
                        }
                        std::optional<nbt::Value> blockEntity;
                        if (const auto entity = sourceEntities.find(
                                posKey(ix, iy, iz));
                            entity != sourceEntities.end())
                            blockEntity = entity->second;
                        tmp.push_back({ix, iy, iz, state,
                                       std::move(blockEntity)});
                    }

            std::vector<World::BlockUpdate> blockUpdates;
            std::vector<BlockEntityStore::NbtReplacement> entityReplacements;
            std::unordered_set<std::int64_t> destinationPositions;
            destinationPositions.reserve(tmp.size());
            const auto expectedWrites = tmp.size() *
                (mode == CloneMode::Move ? std::size_t{2} : std::size_t{1});
            blockUpdates.reserve(expectedWrites);
            entityReplacements.reserve(expectedWrites);
            for (const auto& entry : tmp) {
                const auto dx = static_cast<std::int32_t>(
                    static_cast<std::int64_t>(dst.x) + entry.x - minX);
                const auto dy = static_cast<std::int32_t>(
                    static_cast<std::int64_t>(dst.y) + entry.y - minY);
                const auto dz = static_cast<std::int32_t>(
                    static_cast<std::int64_t>(dst.z) + entry.z - minZ);
                if (hasMovingPistonAt(dx, dy, dz)) {
                    sendFeedback(src,
                        "Cannot clone over a moving piston animation");
                    return 0;
                }
                blockUpdates.push_back({dx, dy, dz, entry.state});
                destinationPositions.insert(posKey(dx, dy, dz));
                entityReplacements.push_back(
                    {dx, dy, dz, entry.blockEntity});
            }
            std::vector<std::size_t> clearedSources;
            if(mode == CloneMode::Move){
                clearedSources.reserve(tmp.size());
                for (std::size_t i = 0; i < tmp.size(); ++i) {
                    const auto& e = tmp[i];
                    // An overlapping source cell is retained only when a
                    // selected source entry actually writes that destination
                    // cell. With masked/filtered clones, geometric overlap
                    // alone is insufficient: a skipped source cell does not
                    // overwrite the old block that must be moved away.
                    if (destinationPositions.contains(
                            posKey(e.x, e.y, e.z)))
                        continue;
                    clearedSources.push_back(i);
                    blockUpdates.push_back({e.x, e.y, e.z, 0});
                    entityReplacements.push_back(
                        {e.x, e.y, e.z, std::nullopt});
                }
            }

            std::vector<World::BlockUpdate> originalBlockStates;
            originalBlockStates.reserve(blockUpdates.size());
            for (const auto& update : blockUpdates) {
                originalBlockStates.push_back({
                    update.x, update.y, update.z,
                    targetWorld.getBlock(update.x, update.y, update.z)});
            }
            const auto result = clone_transaction::applyStagedClone(
                targetWorld, entityStore, blockUpdates, originalBlockStates,
                entityReplacements,
                [&](const BlockEntityStore::NbtReplacement& replacement) {
                    return entityStore.replaceNbtAt(
                        replacement.x, replacement.y, replacement.z,
                        replacement.entity);
                },
                [this, dimension](
                    const std::vector<World::BlockUpdate>& projectedBlocks,
                    const std::vector<BlockEntityStore::NbtReplacement>&
                        projectedEntities) {
                    return blockMutationCandidatesFitAnvilEntries(
                        dimension, projectedBlocks, projectedEntities);
                });
            const bool cloneCommitted =
                result == clone_transaction::ApplyResult::Applied ||
                result == clone_transaction::ApplyResult::
                    AppliedWithNotificationFailure;
            if (!cloneCommitted) {
                if (result ==
                    clone_transaction::ApplyResult::Rejected) {
                    sendFeedback(src,
                        "Clone rejected: block-entity save budget would be exceeded");
                } else if (result ==
                    clone_transaction::ApplyResult::AnvilRejected) {
                    sendFeedback(src,
                        "Clone rejected: projected chunk cannot be stored in an Anvil entry (255-sector limit or serialization failure)");
                } else if (result ==
                           clone_transaction::ApplyResult::SnapshotFailed) {
                    sendFeedback(src,
                        "Could not stage clone block-entity snapshots; no changes were applied");
                } else if (result ==
                           clone_transaction::ApplyResult::RollbackFailed) {
                    sendFeedback(src,
                        "Clone failed and rollback was incomplete; inspect the affected chunks");
                } else {
                    sendFeedback(src,
                        "Could not apply clone; the attempted changes were rolled back");
                }
                return 0;
            }

            for (const auto& entry : tmp) {
                const auto dx = static_cast<std::int32_t>(
                    static_cast<std::int64_t>(dst.x) + entry.x - minX);
                const auto dy = static_cast<std::int32_t>(
                    static_cast<std::int64_t>(dst.y) + entry.y - minY);
                const auto dz = static_cast<std::int32_t>(
                    static_cast<std::int64_t>(dst.z) + entry.z - minZ);
                broadcastBlockChangeFor(dimension, dx, dy, dz, entry.state);
            }
            for (const auto index : clearedSources) {
                const auto& entry = tmp[index];
                broadcastBlockChangeFor(dimension, entry.x, entry.y,
                                        entry.z, 0);
            }
            flushBlockBatches();
            for (const auto& replacement : entityReplacements) {
                if (!replacement.entity) continue;
                WriteBuffer blockEntityPacket;
                if (!writeBlockEntityUpdatePacketBody(
                        blockEntityPacket, replacement.x, replacement.y,
                        replacement.z, *replacement.entity))
                    continue;
                broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::BlockEntityData,
                    blockEntityPacket);
            }
            std::unordered_set<std::int64_t> changedBlockKeys;
            changedBlockKeys.reserve(blockUpdates.size());
            for (const auto& update : blockUpdates)
                changedBlockKeys.insert(
                    posKey(update.x, update.y, update.z));
            resyncOpenMenusAtBlocks(dimension, changedBlockKeys);
            const auto count = static_cast<int>(tmp.size());
            std::string feedback = "Cloned " + std::to_string(count) +
                                   " blocks";
            if (result == clone_transaction::ApplyResult::
                             AppliedWithNotificationFailure)
                feedback +=
                    " (world observers did not receive every update)";
            sendFeedback(src, feedback);
            return count;
        };
        // base replace/masked/filtered as direct executables
        auto makeLeaf = [&](const std::string& mode, bool masked, bool filtered) -> NodePtr {
            auto lit = CommandNode::literal(mode);
            if(filtered){
                auto filterArg = CommandNode::argument("filter", args::blockPredicateArg());
                filterArg->executable=true;
                filterArg->action=[this,doClone](CommandContext& c){
                    std::string f=c.arg("filter").asStr();
                    return doClone(c,false,true,f,CloneMode::Normal);
                };
                // filtered also supports force/move/normal suffix
                for(auto smode: {"force","move","normal"}){
                    auto smLit = CommandNode::literal(smode);
                    smLit->executable=true;
                    const auto mode = parseCloneMode(smode);
                    smLit->action=[this,doClone,mode](CommandContext& c){
                        std::string f=c.arg("filter").asStr();
                        return doClone(c,false,true,f,mode);
                    };
                    filterArg->then(smLit);
                }
                lit->then(filterArg);
                return lit;
            } else {
                lit->executable=true;
                lit->action=[this,doClone,masked](CommandContext& c){ return doClone(c,masked,false,"",CloneMode::Normal); };
                for(auto smode: {"force","move","normal"}){
                    auto smLit = CommandNode::literal(smode);
                    smLit->executable=true;
                    const auto mode = parseCloneMode(smode);
                    smLit->action=[this,doClone,masked,mode](CommandContext& c){ return doClone(c,masked,false,"",mode); };
                    lit->then(smLit);
                }
                return lit;
            }
        };
        // target without mode (default replace)
        target->executable=true;
        target->action=[this,doClone](CommandContext& c){ return doClone(c,false,false,"",CloneMode::Normal); };
        // add mode children to target
        target->then(makeLeaf("replace",false,false));
        target->then(makeLeaf("masked",true,false));
        target->then(makeLeaf("filtered",false,true));
        // also allow suffix force/move/normal directly without mode? handled via mode's children Clone move as shorthand: clone <from> <to>
        // <target> move -> treated as replace move We'll add a direct move under target as alias
        {
            auto moveLit = CommandNode::literal("move");
            moveLit->executable=true;
            moveLit->action=[this,doClone](CommandContext& c){ return doClone(c,false,false,"",CloneMode::Move); };
            target->then(moveLit);
        }
        to->then(target);
        from->then(to);
        clone->then(from);
        d.root->then(clone);
    }
}

} // namespace cppfm
