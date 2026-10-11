// Piston scheduling, movement planning, and delayed transaction lifecycle.
#include "Redstone.hpp"
#include "RedstoneInternal.hpp"
#include "../game/BlockEntities.hpp"
#include "../generated/BlockStates.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <limits>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

namespace cppfm {

namespace {
void appendPistonUpdate(std::vector<World::BlockUpdate>& updates,
                        std::int32_t x, std::int32_t y, std::int32_t z,
                        std::uint16_t state) {
    const auto existing = std::find_if(
        updates.begin(), updates.end(), [&](const World::BlockUpdate& update) {
            return update.x == x && update.y == y && update.z == z;
        });
    if (existing != updates.end()) {
        existing->state = state;
        return;
    }
    updates.push_back({x, y, z, state});
}

// World::setBlockBatch validates and prepares the complete edit before its
// single locked commit. The old-state snapshot is only for changed-chunk
// accounting after success; a rejected batch has not changed any blocks.
bool applyPistonUpdates(World& world,
                        const std::vector<World::BlockUpdate>& updates,
                        std::vector<std::pair<std::int32_t, std::int32_t>>&
                            changedChunks) {
    changedChunks.clear();
    if (updates.empty()) return true;

    const auto geometry = world.verticalGeometry();
    for (std::size_t i = 0; i < updates.size(); ++i) {
        const auto& update = updates[i];
        if (!geometry.containsY(update.y) ||
            gen::blockByState(update.state) == nullptr)
            return false;
        for (std::size_t j = 0; j < i; ++j) {
            if (updates[j].x == update.x && updates[j].y == update.y &&
                updates[j].z == update.z)
                return false;
        }
    }
    for (const auto& update : updates) {
        if (!world.generateChunkIfMissing(update.x >> 4, update.z >> 4))
            return false;
    }

    std::vector<World::BlockUpdate> previous;
    previous.reserve(updates.size());
    for (const auto& update : updates) {
        previous.push_back({update.x, update.y, update.z,
                            world.getBlock(update.x, update.y, update.z)});
    }

    if (world.setBlockBatch(updates)) {
        for (std::size_t i = 0; i < updates.size(); ++i) {
            if (previous[i].state == updates[i].state) continue;
            const std::pair chunk{updates[i].x >> 4, updates[i].z >> 4};
            if (std::find(changedChunks.begin(), changedChunks.end(), chunk) ==
                changedChunks.end())
                changedChunks.push_back(chunk);
        }
        return true;
    }

    // setBlockBatch is all-or-nothing. Never replay this earlier snapshot on
    // failure: it may predate a newer edit if a caller is not serialized by
    // the world's mutation gate.
    return false;
}
}

static bool isStickyBlock(const std::string& name) {
    // glazed is explicitly not sticky (Yarn GlazedTerracottaBlock PUSH_ONLY)
    if(name.find("glazed_terracotta")!=std::string::npos) return false;
    return name=="minecraft:slime_block" || name=="minecraft:honey_block";
}

static bool isGlazedTerracotta(const std::string& name){
    return name.find("glazed_terracotta")!=std::string::npos;
}

static bool isGlazedTerracotta(std::uint16_t st){
    const gen::BlockDef* bd=gen::blockByState(st);
    return bd && std::string(bd->name).find("glazed_terracotta")!=std::string::npos;
}

static bool sticksTogether(const std::string& a, const std::string& b) {
    if(isGlazedTerracotta(a) || isGlazedTerracotta(b)) return false; // B17
    bool aSticky=isStickyBlock(a), bSticky=isStickyBlock(b);
    if (!aSticky && !bSticky) return false;
    if (a=="minecraft:slime_block" && b=="minecraft:honey_block") return false;
    if (a=="minecraft:honey_block" && b=="minecraft:slime_block") return false;
    return true;
}

enum class PistonBehavior { NORMAL, BLOCK, PUSH_ONLY }

;
static PistonBehavior getPistonBehavior(std::uint16_t st){
    if(st==0) return PistonBehavior::NORMAL;
    if(isGlazedTerracotta(st)) return PistonBehavior::PUSH_ONLY;
    const gen::BlockDef* bd=gen::blockByState(st);
    if(!bd) return PistonBehavior::BLOCK;
    if(bd->hardness < 0) return PistonBehavior::BLOCK;
    if(bd->name=="minecraft:obsidian"||bd->name=="minecraft:bedrock"||bd->name=="minecraft:reinforced_deepslate") return PistonBehavior::BLOCK;
    if(bd->name=="minecraft:moving_piston"||bd->name=="minecraft:piston_head") return PistonBehavior::BLOCK;
    if(std::string(bd->name).find("chest")!=std::string::npos) return PistonBehavior::BLOCK;
    if(std::string(bd->name).find("furnace")!=std::string::npos) return PistonBehavior::BLOCK;
    if(std::string(bd->name).find("shulker")!=std::string::npos) return PistonBehavior::BLOCK;
    return PistonBehavior::NORMAL;
}

static bool isMovable(std::uint16_t st, bool retract){
    auto beh=getPistonBehavior(st);
    if(beh==PistonBehavior::BLOCK) return false;
    if(beh==PistonBehavior::PUSH_ONLY) return !retract; // B17
    return true;
}

struct PistonPosition { int x, y, z; }

;
struct PistonExtension {
    std::vector<PistonPosition> blocks;
    std::unordered_set<std::int64_t> occupied;
    bool valid = false;
}

;

static std::int64_t pistonPositionKey(int x, int y, int z) {
    return (static_cast<std::int64_t>(static_cast<std::uint32_t>(x)) << 32) ^
           (static_cast<std::int64_t>(y & 0xFFF) << 20) ^
           static_cast<std::uint32_t>(z);
}

static PistonExtension collectPistonExtension(
    World& world, int x, int y, int z, int dx, int dy, int dz,
    bool checkBeyond) {
    PistonExtension result;
    const auto geometry = world.verticalGeometry();
    // The head occupies the adjacent position even when there are no blocks
    // to push. Reject before an extension can be queued or published.
    if (!geometry.containsY(y + dy)) return result;
    bool fail = false;
    for (int i = 1; i <= 12; ++i) {
        const int px = x + dx * i;
        const int py = y + dy * i;
        const int pz = z + dz * i;
        const std::uint16_t state = world.getBlock(px, py, pz);
        if (state == 0) break;
        if (!isMovable(state, false)) { fail = true; break; }
        result.blocks.push_back({px, py, pz});
        result.occupied.insert(pistonPositionKey(px, py, pz));
        if (checkBeyond && i == 12 &&
            world.getBlock(px + dx, py + dy, pz + dz) != 0)
            fail = true;
    }
    if (!fail) {
        std::queue<PistonPosition> pending;
        for (const auto& position : result.blocks) {
            const auto* block = gen::blockByState(
                world.getBlock(position.x, position.y, position.z));
            if (block && isStickyBlock(std::string(block->name)))
                pending.push(position);
        }
        static constexpr int sideX[6] = {1, -1, 0, 0, 0, 0};
        static constexpr int sideY[6] = {0, 0, 1, -1, 0, 0};
        static constexpr int sideZ[6] = {0, 0, 0, 0, 1, -1};
        while (!pending.empty() && !fail) {
            const auto current = pending.front();
            pending.pop();
            const auto* currentBlock = gen::blockByState(
                world.getBlock(current.x, current.y, current.z));
            if (!currentBlock) continue;
            const std::string currentName(currentBlock->name);
            for (int side = 0; side < 6; ++side) {
                const int nx = current.x + sideX[side];
                const int ny = current.y + sideY[side];
                const int nz = current.z + sideZ[side];
                const auto key = pistonPositionKey(nx, ny, nz);
                if (result.occupied.count(key) ||
                    (nx == x && ny == y && nz == z)) continue;
                const std::uint16_t state = world.getBlock(nx, ny, nz);
                if (state == 0) continue;
                const auto* neighbor = gen::blockByState(state);
                if (!neighbor) continue;
                const std::string neighborName(neighbor->name);
                if (!sticksTogether(currentName, neighborName)) continue;
                if (!isMovable(state, false) || result.occupied.size() >= 12) {
                    fail = true;
                    break;
                }
                result.occupied.insert(key);
                result.blocks.push_back({nx, ny, nz});
                if (isStickyBlock(neighborName)) pending.push({nx, ny, nz});
            }
        }
        if (!fail) {
            for (const auto& position : result.blocks) {
                const int nx = position.x + dx;
                const int ny = position.y + dy;
                const int nz = position.z + dz;
                if (!geometry.containsY(position.y) ||
                    !geometry.containsY(ny)) {
                    fail = true;
                    break;
                }
                if (world.getBlock(nx, ny, nz) != 0 &&
                    !result.occupied.count(pistonPositionKey(nx, ny, nz))) {
                    fail = true;
                    break;
                }
            }
        }
    }
    result.valid = !fail && result.blocks.size() <= 12;
    return result;
}

void RedstoneEngine::handlePiston(std::int32_t x, std::int32_t y, std::int32_t z) {
    std::uint16_t st = world_.getBlock(x,y,z);
    Comp c = classify(st);
    if (c!=Comp::Piston && c!=Comp::StickyPiston) return;
    const gen::BlockDef* b = gen::blockByState(st);
    if (!b) return;
    std::string facing="north";
    bool extended=false;
    for (auto& [k,v] : gen::propsOf(st)) {
        if (k=="facing") facing=std::string(v);
        if (k=="extended") extended = (v=="true");
    }
    bool powered = isQuasiPowered(x,y,z);
    bool wantExtend = powered;
    if (wantExtend == extended) {
        // cancel any pending piston if state already matches
        pistonQueue_.erase(std::remove_if(pistonQueue_.begin(), pistonQueue_.end(),
            [&](const PistonEntity& pe){ return pe.x==x && pe.y==y && pe.z==z; }), pistonQueue_.end());
        return;
    }
    // check if already scheduled
    for (auto &pe : pistonQueue_) if (pe.x==x && pe.y==y && pe.z==z) return;
    std::int64_t now = redstone_detail::currentTick(
        tickRef_, atomicTickRef_);
    int face=0;
    if (facing=="down") face=0; else if (facing=="up") face=1; else if (facing=="north") face=2; else if (facing=="south") face=3; else if (facing=="west") face=4; else if (facing=="east") face=5;
    if (wantExtend) {
        int dx=0,dy=0,dz=0;
        if (facing=="north") dz=-1; else if (facing=="south") dz=1; else if (facing=="west") dx=-1; else if (facing=="east") dx=1; else if (facing=="up") dy=1; else if (facing=="down") dy=-1;
        const PistonExtension extension =
            collectPistonExtension(world_, x, y, z, dx, dy, dz, true);
        if (!extension.valid) return;
    }
    pistonQueue_.push_back({x,y,z, 0.f, wantExtend, now+2, face});
}

void RedstoneEngine::setPistonChunkDirtyFn(
    std::function<void(std::int32_t, std::int32_t)> fn) {
    std::lock_guard<std::recursive_mutex> operationLock(operationMutex_);
    pistonChunkDirtyFn_ = std::move(fn);
}

void RedstoneEngine::notifyPistonChunksDirty(
    const std::vector<std::pair<std::int32_t, std::int32_t>>& chunks) {
    if (!pistonChunkDirtyFn_) return;
    for (const auto& [chunkX, chunkZ] : chunks) {
        try {
            pistonChunkDirtyFn_(chunkX, chunkZ);
        } catch (const std::exception& failure) {
            std::fprintf(stderr,
                "[cppfm] piston dirty notification failed for chunk (%d,%d): %s\n",
                chunkX, chunkZ, failure.what());
        } catch (...) {
            std::fprintf(stderr,
                "[cppfm] piston dirty notification failed for chunk (%d,%d)\n",
                chunkX, chunkZ);
        }
    }
}

void RedstoneEngine::broadcastPistonUpdates(
    const std::vector<World::BlockUpdate>& updates) noexcept {
    if (!broadcastFn_) return;
    for (const auto& update : updates) {
        try {
            broadcastFn_(update.x, update.y, update.z, update.state);
        } catch (const std::exception& failure) {
            std::fprintf(stderr,
                "[cppfm] piston block broadcast failed at (%d,%d,%d): %s\n",
                update.x, update.y, update.z, failure.what());
        } catch (...) {
            std::fprintf(stderr,
                "[cppfm] piston block broadcast failed at (%d,%d,%d)\n",
                update.x, update.y, update.z);
        }
    }
}

bool RedstoneEngine::pistonQueueActionTouchesChunk(
    const PistonEntity& action,
    const std::pair<std::int32_t, std::int32_t>& chunk) const {
    const auto isInChunk = [&chunk](std::int32_t x, std::int32_t z) {
        return (x >> 4) == chunk.first && (z >> 4) == chunk.second;
    };
    if (isInChunk(action.x, action.z)) return true;

    const std::uint16_t state = world_.getBlock(action.x, action.y, action.z);
    const Comp piston = classify(state);
    if (piston != Comp::Piston && piston != Comp::StickyPiston) return false;

    bool currentlyExtended = false;
    int dx = 0, dy = 0, dz = 0;
    for (const auto& [key, value] : gen::propsOf(state)) {
        if (key == "extended") currentlyExtended = value == "true";
        else if (key == "facing") {
            if (value == "down") dy = -1;
            else if (value == "up") dy = 1;
            else if (value == "north") dz = -1;
            else if (value == "south") dz = 1;
            else if (value == "west") dx = -1;
            else if (value == "east") dx = 1;
        }
    }
    if (currentlyExtended == action.extended) return false;

    const std::int32_t headX = action.x + dx;
    const std::int32_t headZ = action.z + dz;
    if (isInChunk(headX, headZ)) return true;

    if (action.extended) {
        const PistonExtension extension = collectPistonExtension(
            world_, action.x, action.y, action.z, dx, dy, dz, false);
        if (!extension.valid) return false;
        for (const auto& block : extension.blocks) {
            if (isInChunk(block.x, block.z) ||
                isInChunk(block.x + dx, block.z + dz))
                return true;
        }
        return false;
    }

    if (piston != Comp::StickyPiston) return false;

    // Sticky retraction can pull a connected group in any horizontal
    // direction. The pull is capped at 12 blocks; including a 13-block
    // horizontal envelope also covers the head-side starting cell and the
    // one-block pull destination. This intentionally errs toward flushing a
    // nearby chunk rather than allowing a cross-chunk pull to race its save.
    constexpr std::int64_t kStickyPullEnvelope = 13;
    const auto chunkIntersectsAxis = [kStickyPullEnvelope](
        std::int32_t chunkCoordinate, std::int32_t center) {
        const std::int64_t minimum =
            static_cast<std::int64_t>(chunkCoordinate) * 16;
        const std::int64_t maximum = minimum + 15;
        const std::int64_t reach = kStickyPullEnvelope;
        return maximum >= static_cast<std::int64_t>(center) - reach &&
               minimum <= static_cast<std::int64_t>(center) + reach;
    };
    return chunkIntersectsAxis(chunk.first, action.x) &&
           chunkIntersectsAxis(chunk.second, action.z);
}

bool RedstoneEngine::pendingPistonCommitTouchesChunk(
    const PendingPistonCommit& commit,
    const std::pair<std::int32_t, std::int32_t>& chunk) const {
    const auto isInChunk = [&chunk](std::int32_t x, std::int32_t z) {
        return (x >> 4) == chunk.first && (z >> 4) == chunk.second;
    };
    if (isInChunk(commit.pistonX, commit.pistonZ) ||
        isInChunk(commit.hx, commit.hz))
        return true;
    for (const auto& entry : commit.entries) {
        if (isInChunk(entry.x, entry.z) ||
            (commit.extend && isInChunk(entry.x + commit.dx,
                                         entry.z + commit.dz)))
            return true;
    }
    for (const auto& entry : commit.pullEntries) {
        if (isInChunk(entry.x, entry.z) ||
            isInChunk(entry.x - commit.dx, entry.z - commit.dz))
            return true;
    }
    return false;
}

bool RedstoneEngine::hasPistonWorkForChunk(
    const std::pair<std::int32_t, std::int32_t>& chunk) const {
    return std::any_of(pistonQueue_.begin(), pistonQueue_.end(),
                       [&](const PistonEntity& action) {
                           return pistonQueueActionTouchesChunk(action, chunk);
                       }) ||
           std::any_of(pendingPistonCommits_.begin(),
                       pendingPistonCommits_.end(),
                       [&](const PendingPistonCommit& commit) {
                           return pendingPistonCommitTouchesChunk(commit, chunk);
                       });
}

void RedstoneEngine::preparePendingPistonExpectations(
    PendingPistonCommit& commit) {
    const auto affectedCount = commit.extend
        ? commit.entries.size() : commit.pullEntries.size();
    commit.expectedCells.clear();
    commit.expectedCells.reserve(2 + affectedCount * 2);
    const auto add = [&](std::int32_t x, std::int32_t y, std::int32_t z) {
        const auto existing = std::find_if(
            commit.expectedCells.begin(), commit.expectedCells.end(),
            [=](const PendingPistonCommit::ExpectedCell& cell) {
                return cell.x == x && cell.y == y && cell.z == z;
            });
        if (existing == commit.expectedCells.end()) {
            PendingPistonCommit::ExpectedCell cell;
            cell.x = x;
            cell.y = y;
            cell.z = z;
            commit.expectedCells.push_back(std::move(cell));
        }
    };

    add(commit.pistonX, commit.pistonY, commit.pistonZ);
    add(commit.hx, commit.hy, commit.hz);
    if (commit.extend) {
        for (const auto& entry : commit.entries) {
            add(entry.x, entry.y, entry.z);
            add(entry.x + commit.dx, entry.y + commit.dy,
                entry.z + commit.dz);
        }
    } else {
        for (const auto& entry : commit.pullEntries) {
            add(entry.x, entry.y, entry.z);
            add(entry.x - commit.dx, entry.y - commit.dy,
                entry.z - commit.dz);
        }
    }
}

void RedstoneEngine::capturePendingPistonExpectations(
    PendingPistonCommit& commit) {
    for (auto& cell : commit.expectedCells) {
        cell.state = world_.getBlock(cell.x, cell.y, cell.z);
        cell.owner = beStore_
            ? beStore_->getShared(posKey(cell.x, cell.y, cell.z))
            : BlockEntityStore::Owner{};
        if (cell.owner) {
            std::lock_guard ownerLock(*cell.owner->stateMtx);
            cell.ownerKind = cell.owner->kind;
            if (*cell.ownerKind == BlockEntity::Kind::MovingPiston) {
                cell.movingState = cell.owner->movingPiston.state;
                cell.movingFacing = cell.owner->movingPiston.facing;
                cell.movingExtending = cell.owner->movingPiston.extending;
                cell.movingSource = cell.owner->movingPiston.source;
                cell.movingFinishTick = cell.owner->movingPiston.finishTick;
            }
        }
    }
}

bool RedstoneEngine::pendingPistonOwnerMatches(
    const PendingPistonCommit::ExpectedCell& cell) const {
    const auto currentOwner = beStore_
        ? beStore_->getShared(posKey(cell.x, cell.y, cell.z))
        : BlockEntityStore::Owner{};
    if (currentOwner != cell.owner) return false;
    if (!currentOwner) return !cell.ownerKind;

    std::lock_guard ownerLock(*currentOwner->stateMtx);
    if (!cell.ownerKind || currentOwner->kind != *cell.ownerKind)
        return false;
    if (*cell.ownerKind != BlockEntity::Kind::MovingPiston) return true;
    const auto& moving = currentOwner->movingPiston;
    return moving.state == cell.movingState &&
        moving.facing == cell.movingFacing &&
        moving.extending == cell.movingExtending &&
        moving.source == cell.movingSource &&
        moving.finishTick == cell.movingFinishTick;
}

bool RedstoneEngine::pendingPistonExpectationsMatch(
    const PendingPistonCommit& commit) const {
    for (const auto& cell : commit.expectedCells) {
        if (world_.getBlock(cell.x, cell.y, cell.z) != cell.state)
            return false;
        if (!pendingPistonOwnerMatches(cell)) return false;
    }
    return true;
}

bool RedstoneEngine::cancelPendingPistonCommit(
    PendingPistonCommit& commit) {
    std::vector<World::BlockUpdate> updates;
    updates.reserve(commit.entries.size() + 1);
    std::vector<std::pair<std::int64_t, BlockEntityStore::Owner>> ownersToRemove;
    ownersToRemove.reserve(commit.entries.size());

    const auto expectedAt = [&](std::int32_t x, std::int32_t y,
                                std::int32_t z)
        -> const PendingPistonCommit::ExpectedCell* {
        const auto found = std::find_if(
            commit.expectedCells.begin(), commit.expectedCells.end(),
            [=](const PendingPistonCommit::ExpectedCell& cell) {
                return cell.x == x && cell.y == y && cell.z == z;
            });
        return found == commit.expectedCells.end() ? nullptr : &*found;
    };
    const auto movingState = gen::blockNameToState().find(
        "minecraft:moving_piston");
    const bool hasMovingState =
        movingState != gen::blockNameToState().end();
    const auto restoreOwnedMovingBlock = [&](const PendingPistonCommit::Entry& entry) {
        if (!beStore_ || !hasMovingState) return;
        const auto* expected = expectedAt(entry.x, entry.y, entry.z);
        if (!expected || !expected->owner ||
            expected->state != movingState->second ||
            world_.getBlock(entry.x, entry.y, entry.z) !=
                movingState->second ||
            expected->ownerKind != BlockEntity::Kind::MovingPiston ||
            !pendingPistonOwnerMatches(*expected))
            return;
        appendPistonUpdate(updates, entry.x, entry.y, entry.z,
                           entry.state);
        ownersToRemove.emplace_back(
            posKey(entry.x, entry.y, entry.z), expected->owner);
    };

    const auto* expectedHead = expectedAt(commit.hx, commit.hy, commit.hz);
    const bool headUnchanged = expectedHead &&
        world_.getBlock(commit.hx, commit.hy, commit.hz) ==
            expectedHead->state &&
        pendingPistonOwnerMatches(*expectedHead);

    if (commit.extend) {
        for (const auto& entry : commit.entries)
            restoreOwnedMovingBlock(entry);
    } else if (!commit.entries.empty()) {
        // Retraction only animates the piston head. Pulled source blocks stay
        // in place until the delayed transaction commits.
        restoreOwnedMovingBlock(commit.entries.front());
    }

    const auto currentPistonState = world_.getBlock(
        commit.pistonX, commit.pistonY, commit.pistonZ);
    if (currentPistonState == commit.stagedPistonState &&
        (commit.extend || headUnchanged))
        appendPistonUpdate(updates, commit.pistonX, commit.pistonY,
                           commit.pistonZ, commit.originalPistonState);

    std::vector<std::pair<std::int32_t, std::int32_t>> changedChunks;
    if (!applyPistonUpdates(world_, updates, changedChunks)) return false;
    notifyPistonChunksDirty(changedChunks);
    broadcastPistonUpdates(updates);
    for (const auto& [key, owner] : ownersToRemove) {
        const auto x = posKeyUnpackX(key);
        const auto y = posKeyUnpackY(key);
        const auto z = posKeyUnpackZ(key);
        const auto* expected = expectedAt(x, y, z);
        if (beStore_ && expected && expected->owner == owner &&
            pendingPistonOwnerMatches(*expected))
            beStore_->removeAfterWorldBlockReplacement(key);
    }
    return true;
}

std::size_t RedstoneEngine::processPendingPistonCommits(
    std::int64_t now,
    std::optional<std::pair<std::int32_t, std::int32_t>> chunk,
    std::size_t budget,
    bool updateMovingProgress) {
    // A delayed piston completion updates World blocks and block-entity
    // owners as one transaction. Keep save snapshots and chunk publication
    // outside the interval between those two representations.
    auto mutationGate = world_.lockMutationGate();
    std::size_t processed = 0;
    for (auto it = pendingPistonCommits_.begin(); it != pendingPistonCommits_.end(); ) {
        if (chunk && !pendingPistonCommitTouchesChunk(*it, *chunk)) {
            ++it;
            continue;
        }
        if (it->dueTick > now) { ++it; continue; }
        if (processed == budget) break;
        ++processed;
        if (!pendingPistonExpectationsMatch(*it)) {
            // Another world edit won the animation window. Never apply the
            // saved destination layout over that newer edit; resolve only
            // placeholders still owned by this exact delayed operation.
            if (!cancelPendingPistonCommit(*it)) {
                ++it;
                continue;
            }
            it = pendingPistonCommits_.erase(it);
            continue;
        }
        // finalize extend
        if (it->extend) {
            // piston head
            const auto geometry = world_.verticalGeometry();
            const gen::BlockDef* headDef = gen::blockByName("minecraft:piston_head");
            if (!geometry.containsY(it->pistonY) ||
                !geometry.containsY(it->hy) || !headDef) {
                ++it;
                continue;
            }

            std::vector<std::pair<std::string_view,std::string_view>> hp;
            hp.emplace_back("facing", it->facing);
            hp.emplace_back("type", it->isSticky?"sticky":"normal");
            hp.emplace_back("short", "false");
            const auto headState = static_cast<std::uint16_t>(
                gen::stateWithProps(*headDef, hp));
            std::vector<World::BlockUpdate> updates;
            updates.reserve(it->entries.size() * 2 + 1);
            bool destinationsFit = true;
            // Construct the final state in phases. A push can have a source
            // block that is also the previous block's destination; emitting
            // each source clear immediately before its destination lets a
            // farther clear overwrite the nearer block's placement.
            for (const auto& entry : it->entries) {
                const auto destinationY = entry.y + it->dy;
                if (!geometry.containsY(entry.y) ||
                    !geometry.containsY(destinationY)) {
                    destinationsFit = false;
                    break;
                }
                appendPistonUpdate(updates, entry.x, entry.y, entry.z, 0);
            }
            if (!destinationsFit) {
                ++it;
                continue;
            }
            for (const auto& entry : it->entries) {
                const auto destinationY = entry.y + it->dy;
                appendPistonUpdate(updates, entry.x + it->dx, destinationY,
                                   entry.z + it->dz, entry.state);
            }
            appendPistonUpdate(updates, it->hx, it->hy, it->hz, headState);

            // Apply the entire final layout as one checked batch. A rejected
            // batch is atomic and leaves the delayed commit available for retry.
            std::vector<std::pair<std::int32_t, std::int32_t>> changedChunks;
            if (!applyPistonUpdates(world_, updates, changedChunks)) {
                ++it;
                continue;
            }
            notifyPistonChunksDirty(changedChunks);
            if (beStore_) {
                for (const auto& entry : it->entries)
                    beStore_->remove(posKey(entry.x, entry.y, entry.z));
            }
            broadcastPistonUpdates(updates);
        } else {
            const auto geometry = world_.verticalGeometry();
            if (!geometry.containsY(it->pistonY) ||
                !geometry.containsY(it->hy)) {
                ++it;
                continue;
            }
            std::vector<World::BlockUpdate> updates;
            updates.reserve(it->pullEntries.size() * 2 + 1);
            bool destinationsFit = true;
            appendPistonUpdate(updates, it->hx, it->hy, it->hz, 0);
            for (const auto& entry : it->pullEntries) {
                const auto destinationY = entry.y - it->dy;
                if (!geometry.containsY(entry.y) ||
                    !geometry.containsY(destinationY)) {
                    destinationsFit = false;
                    break;
                }
                appendPistonUpdate(updates, entry.x, entry.y, entry.z, 0);
            }
            if (!destinationsFit) {
                ++it;
                continue;
            }
            // Clear every source before placing any destination. Connected
            // sticky groups are discovered by BFS, not movement-axis order;
            // a later source clear can otherwise erase an earlier block's
            // destination when those coordinates overlap.
            for (const auto& entry : it->pullEntries) {
                const auto destinationY = entry.y - it->dy;
                appendPistonUpdate(updates, entry.x - it->dx, destinationY,
                                   entry.z - it->dz, entry.state);
            }
            std::vector<std::pair<std::int32_t, std::int32_t>> changedChunks;
            if (!applyPistonUpdates(world_, updates, changedChunks)) {
                ++it;
                continue;
            }
            notifyPistonChunksDirty(changedChunks);
            if (beStore_) beStore_->remove(posKey(it->hx,it->hy,it->hz));
            broadcastPistonUpdates(updates);
        }
        it = pendingPistonCommits_.erase(it);
    }
    // also tick existing moving_piston BEs progress (0->1 over 2 ticks) for strict audit visibility
    if (updateMovingProgress && beStore_ && (tickRef_ || atomicTickRef_)) {
        beStore_->forEach([&](std::int64_t, BlockEntity& be) {
            if (be.kind != BlockEntity::Kind::MovingPiston) return;
            if (be.movingPiston.finishTick==0) return;
            std::int64_t rem = be.movingPiston.finishTick -
                redstone_detail::currentTick(tickRef_, atomicTickRef_);
            if (rem <0) rem=0;
            float prog = 1.f - float(rem)/2.f;
            if (prog<0) prog=0;
            if (prog>1) prog=1;
            be.movingPiston.progress = prog;
        });
    }
    return processed;
}

void RedstoneEngine::handlePistonScheduled(std::int32_t x, std::int32_t y, std::int32_t z, bool extendNow) {
    // Moving-piston owners are staged before the block batch so allocation or
    // persistence admission failure can reject the whole operation. The World
    // gate prevents a chunk save from observing that staged side data before
    // the matching block-state batch commits.
    auto mutationGate = world_.lockMutationGate();
    std::uint16_t st = world_.getBlock(x,y,z);
    Comp c = classify(st);
    if (c!=Comp::Piston && c!=Comp::StickyPiston) return;
    const gen::BlockDef* b = gen::blockByState(st);
    if (!b) return;
    bool curExt=false;
    for (auto& [k,v] : gen::propsOf(st)) if (k=="extended") curExt=(v=="true");
    if (curExt==extendNow) return;
    std::string facing="north";
    for (auto& [k,v] : gen::propsOf(st)) if (k=="facing") facing=std::string(v);
    std::vector<std::pair<std::string_view,std::string_view>> props;
    for (auto& [k,v] : gen::propsOf(st)) if (k!="extended") props.emplace_back(k,v);
    props.emplace_back("extended", extendNow?"true":"false");
    std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*b, props));
    int dx=0,dy=0,dz=0;
    if (facing=="north") dz=-1; else if (facing=="south") dz=1; else if (facing=="west") dx=-1; else if (facing=="east") dx=1; else if (facing=="up") dy=1; else if (facing=="down") dy=-1;
    int face=0;
    if(facing=="down") face=0; else if(facing=="up") face=1; else if(facing=="north") face=2; else if(facing=="south") face=3; else if(facing=="west") face=4; else if(facing=="east") face=5;
    std::int32_t hx=x+dx, hy=y+dy, hz=z+dz;
    std::int64_t now = redstone_detail::currentTick(
        tickRef_, atomicTickRef_);
    const auto geometry = world_.verticalGeometry();
    if (extendNow) {
        if (!geometry.containsY(y) || !geometry.containsY(hy)) return;
        const PistonExtension extension =
            collectPistonExtension(world_, x, y, z, dx, dy, dz, false);
        if (!extension.valid) return;
        const auto& toPush = extension.blocks;
        PendingPistonCommit commit;
        commit.pistonX=x; commit.pistonY=y; commit.pistonZ=z;
        commit.hx=hx; commit.hy=hy; commit.hz=hz;
        commit.originalPistonState=st;
        commit.stagedPistonState=ns;
        commit.extend=true;
        commit.face=face;
        commit.facing=facing;
        commit.dx=dx; commit.dy=dy; commit.dz=dz;
        commit.isSticky = (c==Comp::StickyPiston);
        commit.dueTick = now + 2;
        commit.entries.reserve(toPush.size());
        for (auto &p: toPush) {
            if (!geometry.containsY(p.y) || !geometry.containsY(p.y + dy))
                return;
            uint16_t s = world_.getBlock(p.x,p.y,p.z);
            commit.entries.push_back({p.x,p.y,p.z, s});
        }
        preparePendingPistonExpectations(commit);
        const auto movingState =
            gen::blockNameToState().find("minecraft:moving_piston");
        const bool movingBEsEnabled =
            beStore_ && movingState != gen::blockNameToState().end() &&
            gen::blockByState(static_cast<std::uint16_t>(movingState->second));
        std::vector<World::BlockUpdate> updates;
        updates.reserve(commit.entries.size() + 1);
        appendPistonUpdate(updates, x, y, z, ns);
        if (movingBEsEnabled) {
            const auto movingBlock =
                static_cast<std::uint16_t>(movingState->second);
            for (const auto& entry : commit.entries)
                appendPistonUpdate(updates, entry.x, entry.y, entry.z,
                                   movingBlock);
        }
        pendingPistonCommits_.reserve(pendingPistonCommits_.size() + 1);

        std::vector<BlockEntityStore::Owner> movingOwners;
        if (movingBEsEnabled && !commit.entries.empty()) {
            std::vector<std::int64_t> keys;
            keys.reserve(commit.entries.size());
            for (const auto& entry : commit.entries)
                keys.push_back(posKey(entry.x, entry.y, entry.z));
            auto batch = beStore_->tryCreateSharedBatch(
                keys, BlockEntity::Kind::MovingPiston);
            if (!batch) return;
            movingOwners = std::move(*batch);
        }

        if (movingBEsEnabled) {
            for (std::size_t i = 0; i < commit.entries.size(); ++i) {
                const auto& entry = commit.entries[i];
                auto& be = movingOwners[i];
                {
                    std::lock_guard lock(*be->stateMtx);
                    be->movingPiston.state = entry.state;
                    be->movingPiston.facing = face;
                    be->movingPiston.extending = true;
                    be->movingPiston.source = false;
                    be->movingPiston.progress = 0.f;
                    be->movingPiston.finishTick = now + 2;
                }
            }
        }

        // Do not mutate or publish until destination bounds and the complete
        // BE/resource batch have passed preflight.
        std::vector<std::pair<std::int32_t, std::int32_t>> changedChunks;
        if (!applyPistonUpdates(world_, updates, changedChunks)) {
            if (beStore_) {
                for (const auto& entry : commit.entries)
                    beStore_->remove(posKey(entry.x, entry.y, entry.z));
            }
            return;
        }
        capturePendingPistonExpectations(commit);
        pendingPistonCommits_.push_back(std::move(commit));
        notifyPistonChunksDirty(changedChunks);
        broadcastPistonUpdates(updates);
    } else {
        std::uint16_t head = world_.getBlock(hx,hy,hz);
        const gen::BlockDef* hb = gen::blockByState(head);
        if (hb && hb->name=="minecraft:piston_head") {
            if (!geometry.containsY(y) || !geometry.containsY(hy)) return;
            // Create moving_piston at head for 2-tick animation, defer removal/pull
            PendingPistonCommit commit;
            commit.pistonX=x; commit.pistonY=y; commit.pistonZ=z;
            commit.hx=hx; commit.hy=hy; commit.hz=hz;
            commit.originalPistonState=st;
            commit.stagedPistonState=ns;
            commit.extend=false;
            commit.face=face;
            commit.facing=facing;
            commit.dx=dx; commit.dy=dy; commit.dz=dz;
            commit.isSticky = (c==Comp::StickyPiston);
            commit.dueTick = now + 2;
            commit.entries.push_back({hx,hy,hz, head});
            if (c==Comp::StickyPiston) {
                std::int32_t px=hx+dx, py=hy+dy, pz=hz+dz;
                std::uint16_t frontSt = world_.getBlock(px,py,pz);
                if(frontSt!=0 && isMovable(frontSt,true)){
                    struct Pos{int x,y,z;};
                    std::vector<Pos> toPull;
                    std::unordered_set<std::int64_t> visited;
                    auto key3 = [&](int px_,int py_,int pz_){ return (static_cast<std::int64_t>(static_cast<std::uint32_t>(px_))<<32) ^ (static_cast<std::int64_t>(py_ & 0xFFF)<<20) ^ static_cast<std::uint32_t>(pz_); };
                    bool fail=false;
                    toPull.push_back({px,py,pz});
                    visited.insert(key3(px,py,pz));
                    std::queue<Pos> q;
                    const gen::BlockDef* frontBd = gen::blockByState(frontSt);
                    if(frontBd && isStickyBlock(std::string(frontBd->name))) q.push({px,py,pz});
                    static constexpr int SDX[6]={1,-1,0,0,0,0};
                    static constexpr int SDY[6]={0,0,1,-1,0,0};
                    static constexpr int SDZ[6]={0,0,0,0,1,-1};
                    while(!q.empty() && !fail){
                        Pos cur=q.front(); q.pop();
                        std::uint16_t curSt=world_.getBlock(cur.x,cur.y,cur.z);
                        const gen::BlockDef* curBd=gen::blockByState(curSt);
                        if(!curBd) continue;
                        std::string curName(curBd->name);
                        for(int d=0; d<6; ++d){
                            int nx=cur.x+SDX[d], ny=cur.y+SDY[d], nz=cur.z+SDZ[d];
                            std::int64_t k=key3(nx,ny,nz);
                            if(visited.count(k)) continue;
                            if(nx==x && ny==y && nz==z) continue;
                            if(nx==hx && ny==hy && nz==hz) continue;
                            const std::uint16_t neighborState =
                                world_.getBlock(nx,ny,nz);
                            if(neighborState==0) continue;
                            const gen::BlockDef* nd=gen::blockByState(neighborState);
                            if(!nd) continue;
                            std::string nName(nd->name);
                            if(!isMovable(neighborState,true)) continue;
                            if(!sticksTogether(curName, nName)) continue;
                            if((int)visited.size()>=12){ fail=true; break; }
                            visited.insert(k);
                            toPull.push_back({nx,ny,nz});
                            if(isStickyBlock(nName)) q.push({nx,ny,nz});
                        }
                    }
                    if((int)toPull.size()>12) fail=true;
                    if(!fail){
                        for(auto &p: toPull){
                            int nx=p.x - dx, ny=p.y - dy, nz=p.z - dz;
                            std::uint16_t dst=world_.getBlock(nx,ny,nz);
                            if(dst==0) continue;
                            if(nx==hx && ny==hy && nz==hz) continue;
                            if(visited.count(key3(nx,ny,nz))) continue;
                            if(nx==x && ny==y && nz==z){ fail=true; break; }
                            if(dst!=0){ fail=true; break; }
                        }
                    }
                    if(!fail){
                        for(auto &p: toPull){
                            uint16_t s = world_.getBlock(p.x,p.y,p.z);
                            commit.pullEntries.push_back({p.x,p.y,p.z, s});
                        }
                    } else {
                        if((int)toPull.size()>12){
                            // 12 overflow -> piston fails to retract (vanilla: sticky pull >12 -> no movement)
                            return;
                        }
                        commit.pullEntries.clear();
                    }
                }
            }

            for (const auto& entry : commit.pullEntries) {
                if (!geometry.containsY(entry.y) ||
                    !geometry.containsY(entry.y - dy))
                    return;
            }
            preparePendingPistonExpectations(commit);

            const auto movingState =
                gen::blockNameToState().find("minecraft:moving_piston");
            const bool movingHeadEnabled =
                beStore_ && movingState != gen::blockNameToState().end() &&
                gen::blockByState(
                    static_cast<std::uint16_t>(movingState->second));
            std::vector<World::BlockUpdate> updates;
            updates.reserve(commit.pullEntries.size() * 2 + 2);
            appendPistonUpdate(updates, x, y, z, ns);
            if (movingHeadEnabled)
                appendPistonUpdate(
                    updates, hx, hy, hz,
                    static_cast<std::uint16_t>(movingState->second));
            pendingPistonCommits_.reserve(pendingPistonCommits_.size() + 1);

            BlockEntityStore::Owner movingOwner;
            if (movingHeadEnabled) {
                auto batch = beStore_->tryCreateSharedBatch(
                    {posKey(hx, hy, hz)}, BlockEntity::Kind::MovingPiston);
                if (!batch) return;
                movingOwner = std::move(batch->front());
            }

            if (movingOwner) {
                std::lock_guard lock(*movingOwner->stateMtx);
                movingOwner->movingPiston.state = head;
                movingOwner->movingPiston.facing = face;
                movingOwner->movingPiston.extending = false;
                movingOwner->movingPiston.source = true;
                movingOwner->movingPiston.progress = 0.f;
                movingOwner->movingPiston.finishTick = now + 2;
            }

            // Retraction also waits until its moving-head BE fits before
            // changing either piston block or publishing its delayed commit.
            std::vector<std::pair<std::int32_t, std::int32_t>> changedChunks;
            if (!applyPistonUpdates(world_, updates, changedChunks)) {
                if (movingOwner) beStore_->remove(posKey(hx, hy, hz));
                return;
            }
            capturePendingPistonExpectations(commit);
            pendingPistonCommits_.push_back(std::move(commit));
            notifyPistonChunksDirty(changedChunks);
            broadcastPistonUpdates(updates);
        } else {
            const std::vector<World::BlockUpdate> updates{{x, y, z, ns}};
            std::vector<std::pair<std::int32_t, std::int32_t>> changedChunks;
            if (!applyPistonUpdates(world_, updates, changedChunks)) return;
            notifyPistonChunksDirty(changedChunks);
            broadcastPistonUpdates(updates);
        }
    }
}

void RedstoneEngine::processPistonQueue(std::int64_t now) {
    const int budget = 32;
    int done = 0;
    for (std::size_t i = 0; i < pistonQueue_.size() && done < budget; ) {
        if (pistonQueue_[i].dueTick <= now) {
            PistonEntity pe = pistonQueue_[i];
            pistonQueue_.erase(pistonQueue_.begin() + static_cast<std::ptrdiff_t>(i));
            handlePistonScheduled(pe.x, pe.y, pe.z, pe.extended);
            ++done;
            // don't advance: the slot at i now holds the next entry (or new end)
        } else ++i;
    }
    processPendingPistonCommits(now, std::nullopt,
                                std::numeric_limits<std::size_t>::max(), true);
}

void RedstoneEngine::flushPendingPistons(std::int32_t chunkX, std::int32_t chunkZ) {
    std::lock_guard<std::recursive_mutex> operationLock(operationMutex_);
    // Persistence invokes this barrier before taking World::lockMutationGate.
    // Drain queued World-hook notifications now, under the established
    // operation -> World lock order. World callbacks only enqueue, avoiding
    // the inverse World -> operation order that deadlocks concurrent edits.
    drainBlockChanges();
    const std::pair chunk{chunkX, chunkZ};
    constexpr std::size_t kMaxPistonWorkItemsPerBarrier = 256;
    std::size_t work = 0;

    while (work < kMaxPistonWorkItemsPerBarrier) {
        work += processPendingPistonCommits(
            std::numeric_limits<std::int64_t>::max(), chunk,
            kMaxPistonWorkItemsPerBarrier - work, false);
        if (work == kMaxPistonWorkItemsPerBarrier) break;

        const auto scheduled = std::find_if(
            pistonQueue_.begin(), pistonQueue_.end(),
            [&](const PistonEntity& action) {
                return pistonQueueActionTouchesChunk(action, chunk);
            });
        if (scheduled == pistonQueue_.end()) {
            if (hasPistonWorkForChunk(chunk))
                throw std::runtime_error(
                    "piston save barrier could not complete animation work "
                    "for chunk " + std::to_string(chunkX) + "," +
                    std::to_string(chunkZ));
            break;
        }

        const PistonEntity action = *scheduled;
        pistonQueue_.erase(scheduled);
        ++work;
        // A persistence barrier deliberately forces this already-scheduled
        // operation through its ordinary start path, then the next iteration
        // drains its animation commit before the snapshot may proceed.
        handlePistonScheduled(action.x, action.y, action.z,
                              action.extended);
    }

    processPendingPistonCommits(
        std::numeric_limits<std::int64_t>::max(), chunk,
        kMaxPistonWorkItemsPerBarrier - work, true);
    if (hasPistonWorkForChunk(chunk))
        throw std::runtime_error(
            "piston save barrier exceeded its work-item budget for "
            "chunk " + std::to_string(chunkX) + "," +
            std::to_string(chunkZ));
}

} // namespace cppfm
