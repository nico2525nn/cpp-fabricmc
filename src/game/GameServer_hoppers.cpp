#include "GameServer.hpp"
#include "../core/Random.hpp"
#include "BlockEntityDrops.hpp"
#include "BlockEntityKinds.hpp"
#include "EntitySupport.hpp"
#include "MetadataTypes.hpp"
#include "../physics/Fluids.hpp"
#include "../physics/Redstone.hpp"
#include "Constants.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/EntityIds.hpp"
#include "MenuInteraction.hpp"
#include "StairsHelper.hpp"
#include "EquipmentComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "MeleeHelper.hpp"
#include "CombatManager.hpp"
#include "MobSpawner.hpp"
#include "MenuLogic.hpp"
#include "PotionBrewing.hpp"
#include "GameServerHelpers.hpp"

namespace cppfm {
using namespace proto;

void GameServer::hoppersTick() {
    if (tickNo_ % HOPPER_TRANSFER_INTERVAL_TICKS != 0) return;
    hoppersTickFor(0);
    hoppersTickFor(-1);
    hoppersTickFor(1);
}

class HopperTickService {
public:
    HopperTickService(GameServer& server, std::int8_t requestedDimension)
        : server_(server),
          simulationLock_(server.simulationDispatchMtx_),
          dimension(server.canonicalDimension(requestedDimension)),
          dimensionBlockEntities(server.blockEntitiesFor(dimension)),
          dimensionWorld(server.worldFor(dimension)),
          dimensionRedstone(server.redstoneFor(dimension)),
          dimensionFluids(server.fluidsFor(dimension)),
          dimensionBlockTicks(server.blockTicksFor(dimension)),
          dimensionDispenserPower(
              server.dimensionRuntime(dimension).dispenserPowerByPosition) {}

    void run() {
        // Store keys rather than copying BlockEntity.  BlockEntity owns its slot
        // arrays by value, so a value snapshot would make every transfer mutate a
        // detached copy and silently lose the result.  Re-resolve each key before
        // processing so the authoritative store receives the mutation.
        std::vector<std::pair<std::int64_t, BlockEntity::Kind>> snapshot;
        dimensionBlockEntities.forEach([&](std::int64_t k, BlockEntity& be) {
            if (be.kind == BlockEntity::Kind::Hopper ||
                be.kind == BlockEntity::Kind::Dispenser ||
                be.kind == BlockEntity::Kind::Dropper)
                snapshot.emplace_back(k, be.kind);
        });
        for (const auto& [key, kind] : snapshot) {
            processContainer(key, kind);
        }

    
    }

private:
    GameServer& server_;
    SimulationDispatchGuard simulationLock_;
    std::int8_t dimension;
    BlockEntityStore& dimensionBlockEntities;
    World& dimensionWorld;
    RedstoneEngine& dimensionRedstone;
    FluidSim& dimensionFluids;
    BlockTickScheduler& dimensionBlockTicks;
    std::unordered_map<std::int64_t, bool>& dimensionDispenserPower;

    void broadcastPacketExcept(const Player* except, std::uint8_t id,
                               const WriteBuffer& body) {
        server_.broadcastPacketExceptInDimension(dimension, except, id, body);
    }

    void broadcastBlockChange(std::int32_t x, std::int32_t y,
                              std::int32_t z, std::uint16_t state) {
        server_.broadcastBlockChangeFor(dimension, x, y, z, state);
    }

    void broadcastSound(const char* name, double x, double y, double z,
                        float volume, float pitch, const char* category) {
        server_.broadcastSoundFor(dimension, name, x, y, z, volume, pitch,
                                  category);
    }

    void spawnItemDropStack(double x, double y, double z,
                            const ItemStack& stack, double vx, double vy,
                            double vz) {
        server_.spawnItemDropFor(dimension, x, y, z, stack, vx, vy, vz);
    }

    void spawnPrimedTnt(double x, double y, double z, double vx, double vy,
                        double vz, int fuse) {
        server_.spawnPrimedTntFor(dimension, x, y, z, vx, vy, vz, fuse);
    }

    static ItemStack oneItem(const ItemStack& source) {
        ItemStack one = source;
        one.count = 1;
        return one;
    }

    bool admitEntityChunks(std::int64_t firstKey,
                           std::int64_t secondKey) {
        auto* storage = server_.persistenceFor(dimension);
        if (!storage) return true;
        const auto firstX = posKeyUnpackX(firstKey) >> 4;
        const auto firstZ = posKeyUnpackZ(firstKey) >> 4;
        if (!storage->tryMarkDirty(firstX, firstZ)) return false;
        const auto secondX = posKeyUnpackX(secondKey) >> 4;
        const auto secondZ = posKeyUnpackZ(secondKey) >> 4;
        return (firstX == secondX && firstZ == secondZ) ||
               storage->tryMarkDirty(secondX, secondZ);
    }

    bool admitEntityChunk(std::int64_t key) {
        return admitEntityChunks(key, key);
    }

    void invalidateEntityChunks(std::int64_t firstKey,
                                std::int64_t secondKey) {
        const auto firstX = posKeyUnpackX(firstKey) >> 4;
        const auto firstZ = posKeyUnpackZ(firstKey) >> 4;
        server_.invalidateChunkCacheFor(dimension, firstX, firstZ);
        const auto secondX = posKeyUnpackX(secondKey) >> 4;
        const auto secondZ = posKeyUnpackZ(secondKey) >> 4;
        if (firstX != secondX || firstZ != secondZ)
            server_.invalidateChunkCacheFor(dimension, secondX, secondZ);
    }

    void invalidateEntityChunk(std::int64_t key) {
        invalidateEntityChunks(key, key);
    }

    void processContainer(std::int64_t key, BlockEntity::Kind kind) {
        // World-coupled block-entity writes take the World gate before owner
        // locks. This matches menu/crafter/save ordering and lets dirty
        // generations be admitted before a multi-owner transfer commits.
        auto mutationGate = dimensionWorld.lockMutationGate();
        auto currentOwner = dimensionBlockEntities.getShared(key);
        if (!currentOwner) return;
        std::unique_lock entityLock(*currentOwner->stateMtx);
        auto& be = *currentOwner;
        if (be.kind != kind) return;
        const std::int32_t x = posKeyUnpackX(key);
        const std::int32_t y = posKeyUnpackY(key);
        const std::int32_t z = posKeyUnpackZ(key);
        if (be.kind == BlockEntity::Kind::Hopper && dimensionRedstone.isPoweredHere(x, y, z)) return;
        ItemStack* slots = be.generic.slots;
        const int count = be.kind == BlockEntity::Kind::Hopper ? 5 : 9;


        if (be.kind == BlockEntity::Kind::Hopper) {
            tickHopper(be, key, x, y, z, slots, count);
        }

        if (be.kind == BlockEntity::Kind::Dispenser ||
            be.kind == BlockEntity::Kind::Dropper) {
            tickDispenser(be, key, x, y, z, slots, count);
        }

    }

    void tickHopper(BlockEntity& be, std::int64_t key, int x, int y, int z,
                    ItemStack* slots, int count) {
        // ---- pull from above
        if (auto otherOwner = dimensionBlockEntities.getShared(posKey(x, y + 1, z)))
            extractOneFrom(be, key, x, y, z, count, otherOwner);
        // ---- item entity pickup from the hopper cell itself
        WriteBuffer collected;
        bool collectedItem = false;
        std::shared_ptr<ItemEntity> pickupCandidate;
        BlockEntity pickupCandidateState = be;
        {
            std::lock_guard lk(server_.entsMtx_);
            for (auto& e : server_.itemDrops_) {
                if (!e->collected && server_.canonicalDimension(e->dimension) ==
                    server_.canonicalDimension(dimension) &&
                    std::abs(e->x - (x + .5)) < 0.8 &&
                    std::abs(e->z - (z + .5)) < 0.8 &&
                    e->y > y - 0.2 && e->y < y + 1.3) {
                    ItemStack one = oneItem(e->asStack());
                    if (one.empty()) continue;
                    if (mergeIntoSlots(pickupCandidateState.generic.slots,
                                       count, one)) {
                        pickupCandidate = e;
                        break;
                    }
                }
            }
        }
        if (pickupCandidate) {
            std::vector<BlockEntityStore::SnapshotOverride> candidates;
            candidates.push_back({key, std::move(pickupCandidateState)});
            if (server_.blockEntityCandidatesFitAnvilEntry(dimension, candidates) &&
                admitEntityChunk(key)) {
                {
                    std::lock_guard lk(server_.entsMtx_);
                    if (!pickupCandidate->collected &&
                        pickupCandidate->count > 0) {
                        // Both locks are held. The no-throw payload swap and
                        // item-count update form one in-memory transfer.
                        be.swapState(candidates.front().entity);
                        if (--pickupCandidate->count <= 0) {
                            pickupCandidate->count = 0;
                            pickupCandidate->collected = true;
                            pickupCandidate->stack = ItemStack::air();
                        } else if (!pickupCandidate->stack.empty()) {
                            pickupCandidate->stack.count =
                                pickupCandidate->count;
                        }
                        collected.varint(pickupCandidate->entityId);
                        collected.varint(0);             // collector: hopper
                        collected.varint(1);
                        collectedItem = true;
                    }
                }
                if (collectedItem)
                    invalidateEntityChunk(key);
            }
        }
        if (collectedItem)
            broadcastPacketExcept(nullptr, pl::sc::Collect, collected);
        // ---- push downward
        if (auto belowOwner = dimensionBlockEntities.getShared(posKey(x, y - 1, z))) {
            std::lock_guard belowLock(*belowOwner->stateMtx);
            auto* below = belowOwner.get();
            auto insertOneInto = [&](BlockEntity* target,
                                     const ItemStack& one) -> bool {
                if (!target) return false;
                ItemStack* destinations = nullptr;
                int destinationCount = 0;
                switch (target->kind) {
                case BlockEntity::Kind::Chest:
                case BlockEntity::Kind::Barrel:
                case BlockEntity::Kind::ShulkerBox:
                    destinations = target->chest.slots;
                    destinationCount = ChestData::kSlots;
                    break;
                case BlockEntity::Kind::Hopper:
                case BlockEntity::Kind::Dispenser:
                case BlockEntity::Kind::Dropper:
                    destinations = target->generic.slots;
                    destinationCount = target->kind == BlockEntity::Kind::Hopper ? 5 : 9;
                    break;
                case BlockEntity::Kind::Furnace:
                    // A hopper directly above a furnace feeds its input slot.
                    destinations = target->furnace.slots + FurnaceData::kInput;
                    destinationCount = 1;
                    break;
                case BlockEntity::Kind::Brewing:
                    // A hopper above a brewing stand feeds the ingredient slot.
                    destinations = target->brewing.slots + 3;
                    destinationCount = 1;
                    break;
                case BlockEntity::Kind::Crafter:
                    destinations = target->crafter.slots;
                    destinationCount = CrafterData::kSlots;
                    break;
                default:
                    return false;
                }
                for (int j = 0; j < destinationCount; ++j) {
                    if (target->kind == BlockEntity::Kind::Crafter &&
                        target->crafter.isSlotDisabled(j))
                        continue;
                    auto& dst = destinations[j];
                    if (dst.empty()) {
                        dst = one;
                        return true;
                    }
                    if (dst.sameItemData(one) && dst.count < 64) {
                        ++dst.count;
                        return true;
                    }
                }
                return false;
            };
            if (below != &be) {
                for (int i = 0; i < count; ++i) {
                    auto& s = slots[i];
                    if (s.empty()) continue;
                    ItemStack one = oneItem(s);
                    BlockEntity sourceCandidate = be;
                    BlockEntity destinationCandidate = *below;
                    auto& sourceStack = sourceCandidate.generic.slots[i];
                    if (insertOneInto(&destinationCandidate, one)) {
                        if (--sourceStack.count <= 0)
                            sourceStack = ItemStack::air();
                        const auto belowKey = posKey(x, y - 1, z);
                        std::vector<BlockEntityStore::SnapshotOverride>
                            candidates;
                        candidates.push_back(
                            {key, std::move(sourceCandidate)});
                        candidates.push_back(
                            {belowKey, std::move(destinationCandidate)});
                        if (!server_.blockEntityCandidatesFitAnvilEntry(
                                dimension, candidates))
                            break;
                        if (!admitEntityChunks(key, belowKey)) break;
                        // Candidate projections and capacity checks are done.
                        // Payload swaps are noexcept while both owner locks
                        // remain held, preventing half-transferred inventory.
                        be.swapState(candidates[0].entity);
                        belowOwner->swapState(candidates[1].entity);
                        invalidateEntityChunks(key, belowKey);
                    }
                    break;
                }
            }
        }

    }

    void tickDispenser(BlockEntity& be, std::int64_t key, int x, int y, int z,
                       ItemStack* slots, int count) {
        bool powered = dimensionRedstone.isQuasiPowered(x, y, z);
        bool& was = dimensionDispenserPower[key];
        if (powered && !was) {
            // detect dropper vs dispenser by world block name
            bool isDropper = be.kind == BlockEntity::Kind::Dropper;
            {
                uint16_t bs = dimensionWorld.getBlock(x, y, z);
                const gen::BlockDef* bd = gen::blockByState(bs);
                if (bd && std::string(bd->name)=="minecraft:dropper") isDropper=true;
            }
            // pick random non-empty slot (vanilla random)
            std::vector<int> nonEmpty;
            for(int i=0;i<9;++i) if(!slots[i].empty()) nonEmpty.push_back(i);
            if(!nonEmpty.empty()){
                // Dispensing can spawn entities, alter the world, or consume
                // the source stack. Register persistence before those
                // externally visible effects; on admission failure leave the
                // powered edge retryable and the inventory untouched.
                if (!admitEntityChunk(key)) {
                    was = false;
                    return;
                }
                int pick = nonEmpty[nextRandom()%nonEmpty.size()];
                auto& s = slots[pick];
                double dx = 0, dy = 0, dz = 0;
                std::string facing = "north";
                std::uint16_t bstate = dimensionWorld.getBlock(x, y, z);
                if (bstate) {
                    for (auto& [pk, pv] : gen::propsOf(bstate))
                        if (pk == "facing") facing = std::string(pv);
                }
                if (facing == "north") dz = -1;
                else if (facing == "south") dz = 1;
                else if (facing == "west") dx = -1;
                else if (facing == "east") dx = 1;
                else if (facing == "up") dy = 1;
                else if (facing == "down") dy = -1;
                int tx = x + (int)dx, ty = y + (int)dy, tz = z + (int)dz;
                double sx = x + .5 + dx * .7;
                double sy = y + .5 + dy * .7;
                double sz = z + .5 + dz * .7;
                std::string iname = s.name();
                bool rejectedByAnvilLimit = false;


                if(isDropper){
                    // Dropper: always try insert, else drop item (never projectile)
                    bool inserted = insertDropperItem(be, key, x, y, z, pick, s, facing, tx, ty, tz, rejectedByAnvilLimit);
                    if (rejectedByAnvilLimit) {
                        // Keep the source stack intact and retry when the
                        // target chunk has enough durable Anvil capacity.
                        was = false;
                        return;
                    }
                    if(!inserted){
                        spawnItemDropStack(tx+0.5, ty+0.5, tz+0.5, oneItem(s), dx*0.25, 0.15, dz*0.25);
                        if (--s.count <= 0) s = ItemStack::air();
                        invalidateEntityChunk(key);
                    }
                    broadcastSound("minecraft:block.dispenser.dispense", x+.5,y+.5,z+.5,1.f,1.f,"block");
                } else {
                    dispenseItem(be, key, x, y, z, tx, ty, tz, dx, dy, dz,
                                 sx, sy, sz, facing, s, iname);
                }
            }
        }
        was = powered;

    }

    bool mergeIntoSlots(ItemStack* destination, int slotCount,
                        const ItemStack& src) {
        for (int i=0;i<slotCount;++i) {
            auto& stack=destination[i];
            if (stack.empty()) { stack=src; return true; }
            if (stack.sameItemData(src) && stack.count<64) {
                const int take=std::min<int>(64-stack.count,src.count);
                stack.count=static_cast<std::int16_t>(stack.count+take);
                if(take>=src.count)return true;
            }
        }
        return false;
    }

    bool extractOneFrom(BlockEntity& be, std::int64_t key, int x, int y,
                        int z, int count,
                        const BlockEntityStore::Owner& otherOwner) {
        if (!otherOwner) return false;
        std::lock_guard otherLock(*otherOwner->stateMtx);
        BlockEntity sourceCandidate = *otherOwner;
        ItemStack* oslots = nullptr; int on = 0;
        switch (sourceCandidate.kind) {
        case BlockEntity::Kind::Chest: oslots = sourceCandidate.chest.slots; on = 27; break;
        case BlockEntity::Kind::Barrel:
        case BlockEntity::Kind::ShulkerBox:
            oslots = sourceCandidate.chest.slots;
            on = 27;
            break;
        case BlockEntity::Kind::Hopper: oslots = sourceCandidate.generic.slots; on = 5; break;
        case BlockEntity::Kind::Dispenser: oslots = sourceCandidate.generic.slots; on = 9; break;
        case BlockEntity::Kind::Dropper: oslots = sourceCandidate.generic.slots; on = 9; break;
        case BlockEntity::Kind::Furnace:
            oslots = sourceCandidate.furnace.slots + FurnaceData::kOutput;
            on = 1;
            break;
        case BlockEntity::Kind::Brewing: oslots = sourceCandidate.brewing.slots; on = 3; break;
        case BlockEntity::Kind::Crafter: oslots = sourceCandidate.crafter.slots; on = CrafterData::kSlots; break;
        default: return false;
        }
        for (int i = 0; i < on; ++i) {
            auto& s = oslots[i];
            if (s.empty()) continue;
            ItemStack one = oneItem(s);
            BlockEntity destinationCandidate = be;
            if (mergeIntoSlots(destinationCandidate.generic.slots,
                               count, one)) {
                if (--s.count <= 0) s = ItemStack::air();
                const std::int64_t sourceKey = posKey(x, y + 1, z);
                std::vector<BlockEntityStore::SnapshotOverride> candidates;
                candidates.push_back({key, std::move(destinationCandidate)});
                candidates.push_back(
                    {sourceKey, std::move(sourceCandidate)});
                if (!server_.blockEntityCandidatesFitAnvilEntry(dimension,
                                                        candidates)) {
                    sourceCandidate = *otherOwner;
                    continue;
                }
                const auto sourceKey = posKey(x, y + 1, z);
                if (!admitEntityChunks(key, sourceKey)) return false;
                be.swapState(candidates[0].entity);
                otherOwner->swapState(candidates[1].entity);
                invalidateEntityChunks(key, sourceKey);
                return true;
            }
        }
        return false;

    }

    bool insertDropperItem(BlockEntity& be, std::int64_t key, int x, int y,
                           int z, int pick, ItemStack& s,
                           const std::string& facing, int tx, int ty, int tz,
                           bool& rejectedByAnvilLimit) {
        auto targetOwner = dimensionBlockEntities.getShared(posKey(tx,ty,tz));
        if(!targetOwner) return false;
        std::lock_guard targetLock(*targetOwner->stateMtx);
        BlockEntity destinationCandidate = *targetOwner;
        auto* beT = &destinationCandidate;
        const auto commitInsertion = [&]() {
            BlockEntity sourceCandidate = be;
            auto& sourceStack =
                sourceCandidate.generic.slots[pick];
            if (--sourceStack.count <= 0)
                sourceStack = ItemStack::air();
            const auto targetKey = posKey(tx, ty, tz);
            std::vector<
                BlockEntityStore::SnapshotOverride> candidates;
            candidates.push_back(
                {key, std::move(sourceCandidate)});
            candidates.push_back(
                {targetKey, std::move(destinationCandidate)});
            if (!server_.blockEntityCandidatesFitAnvilEntry(
                    dimension, candidates)) {
                rejectedByAnvilLimit = true;
                return false;
            }
            if (!admitEntityChunks(key, targetKey)) {
                rejectedByAnvilLimit = true;
                return false;
            }
            be.swapState(candidates[0].entity);
            targetOwner->swapState(candidates[1].entity);
            invalidateEntityChunks(key, targetKey);
            return true;
        };
        std::string insertDir;
        if(facing=="north") insertDir="south";
        else if(facing=="south") insertDir="north";
        else if(facing=="west") insertDir="east";
        else if(facing=="east") insertDir="west";
        else if(facing=="up") insertDir="down";
        else if(facing=="down") insertDir="up";
        else insertDir="up";
        ItemStack one = oneItem(s);
        if(beT->kind==BlockEntity::Kind::Furnace){
            int trySlot = (insertDir=="up") ? 0 : 1;
            if(trySlot==1 && !isFuelItem(s.itemId)) return false;
            auto &dst = beT->furnace.slots[trySlot];
            if(dst.empty()){
                dst = one;
                return commitInsertion();
            } else if(dst.sameItemData(one) && dst.count<64){
                ++dst.count;
                return commitInsertion();
            } else return false;
        }
        if(beT->kind==BlockEntity::Kind::Brewing){
            if(insertDir=="up"){
                auto &dst = beT->brewing.slots[3];
                if(dst.empty()){
                    dst = one;
                    return commitInsertion();
                } else if(dst.sameItemData(one) && dst.count<64){
                    ++dst.count;
                    return commitInsertion();
                } else return false;
            }
            for(int idx : {0,1,2,4}){
                auto &d = beT->brewing.slots[idx];
                if(d.empty()){ d=one; return commitInsertion(); }
                if(d.sameItemData(one) && d.count<64){ ++d.count; return commitInsertion(); }
            }
            return false;
        }
        ItemStack* oslots=nullptr; int on=0;
        switch(beT->kind){
            case BlockEntity::Kind::Chest:
            case BlockEntity::Kind::Barrel:
            case BlockEntity::Kind::ShulkerBox:
                oslots=beT->chest.slots; on=27; break;
            case BlockEntity::Kind::Hopper:
                oslots=beT->generic.slots; on=5; break;
            case BlockEntity::Kind::Dispenser:
            case BlockEntity::Kind::Dropper:
                oslots=beT->generic.slots; on=9; break;
            case BlockEntity::Kind::Crafter:
                oslots=beT->crafter.slots; on=CrafterData::kSlots; break;
            default: return false;
        }
        if(oslots){
            for(int j=0;j<on;++j){
                if (beT->kind == BlockEntity::Kind::Crafter &&
                    beT->crafter.isSlotDisabled(j))
                    continue;
                auto &d=oslots[j];
                if(d.empty()){ d=one; return commitInsertion(); }
                if(d.sameItemData(one) && d.count<64){ ++d.count; return commitInsertion(); }
            }
        }
        return false;

    }

    void dispenseItem(BlockEntity& be, std::int64_t key, int x, int y, int z,
                      int tx, int ty, int tz, double dx, double dy, double dz,
                      double sx, double sy, double sz,
                      const std::string& facing, ItemStack& s,
                      const std::string& iname) {
        bool handled = false;
        if (iname.find("shulker_box") != std::string::npos) {
            // getBlock intentionally treats an unloaded chunk
            // as air. Resolve item data first, then acquire a
            // loaded-chunk mutation lease that retries if
            // eviction wins between loading and locking.
            const auto itemData = shulkerBoxDataFromItem(s);
            const gen::BlockDef* def =
                gen::blockByName(iname);
            if (!def)
                def = gen::blockByName(
                    "minecraft:shulker_box");
            if (def && itemData) {
                auto mutationGate =
                    dimensionWorld.lockLoadedChunkMutationGate(
                        tx >> 4, tz >> 4);
                if (mutationGate) {
                    const bool targetStillEmpty =
                        isAirBlockState(
                            dimensionWorld.getBlock(tx, ty,
                                                    tz)) &&
                        !dimensionBlockEntities.containsAt(
                            tx, ty, tz);
                    if (targetStillEmpty) {
                        const auto belowState =
                            dimensionWorld.getBlock(
                                tx, ty - 1, tz);
                        const std::string shulkerFacing =
                            isAirBlockState(belowState)
                                ? facing : "up";
                        const auto ns =
                            static_cast<std::uint16_t>(
                                gen::stateWithProps(
                                    *def,
                                    {{"facing", shulkerFacing}}));
                        const std::vector<World::BlockUpdate>
                            projectedBlocks{{tx, ty, tz, ns}};
                        auto savedEntity =
                            shulkerBoxBlockEntityNbt(
                                *itemData, tx, ty, tz);
                        if (dimensionWorld.setBlockIfAir(
                                tx, ty, tz, ns)) {
                            bool blockEntityInstalled = false;
                            try {
                                blockEntityInstalled =
                                    dimensionBlockEntities
                                        .replaceNbtAtIfAdmitted(
                                            tx, ty, tz,
                                            std::move(savedEntity),
                                            [this,
                                             &projectedBlocks](
                                                const BlockEntityStore::NbtReplacement& replacement) {
                                                const std::vector<BlockEntityStore::NbtReplacement>
                                                    projectedEntities{replacement};
                                                return dimensionBlockEntities
                                                           .canReplaceNbtBatch(projectedEntities) &&
                                                       server_.blockMutationCandidatesFitAnvilEntries(
                                                           dimension,
                                                           projectedBlocks,
                                                           projectedEntities);
                                            });
                            } catch (const std::exception& error) {
                                std::fprintf(stderr,
                                    "[cppfm] shulker dispense rejected before block-entity publication: %s\n",
                                    error.what());
                            } catch (...) {
                                std::fprintf(stderr,
                                    "[cppfm] shulker dispense rejected before block-entity publication\n");
                            }
                            if (blockEntityInstalled) {
                                broadcastBlockChange(tx, ty, tz,
                                                     ns);
                                if (--s.count <= 0)
                                    s = ItemStack::air();
                                broadcastSound(
                                    "minecraft:block.dispenser.dispense",
                                    x + .5, y + .5, z + .5,
                                    1.f, 1.f, "block");
                                handled = true;
                            } else if (
                                !dimensionBlockEntities
                                     .containsAt(tx, ty, tz)) {
                                if (dimensionWorld
                                        .setBlockIfState(
                                            tx, ty, tz, ns, 0))
                                    broadcastBlockChange(
                                        tx, ty, tz, 0);
                            } else {
                                // Another store owner won
                                // publication. Leave its
                                // record and the dispenser
                                // item untouched for a retry.
                                handled = true;
                            }
                        }
                    }
                }
            }
            if (!handled) {
                spawnItemDropStack(sx, sy, sz, oneItem(s),
                                    dx * .25, .15, dz * .25);
                if(--s.count<=0) s=ItemStack::air();
                handled = true;
            }
        }
        // bucket fluid dispense
        if(!handled && (iname=="minecraft:water_bucket" || iname=="minecraft:lava_bucket" || iname=="minecraft:powder_snow_bucket")){
            uint16_t tSt = dimensionWorld.getBlock(tx,ty,tz);
            bool replaceable = (tSt==0);
            // check replaceable: air or non-solid? simplified air only
            if(replaceable){
                std::string fluid = iname=="minecraft:lava_bucket" ? "minecraft:lava" : (iname=="minecraft:powder_snow_bucket" ? "minecraft:powder_snow" : "minecraft:water");
                uint16_t fluidSt = 0;
                if(fluid=="minecraft:powder_snow"){
                    auto it=gen::blockNameToState().find(fluid);
                    if(it!=gen::blockNameToState().end()) fluidSt=static_cast<uint16_t>(it->second);
                } else {
                    fluidSt = static_cast<uint16_t>(gen::stateWithPropsList(fluid, {{"level","0"}}));
                    if(fluidSt==0){ auto it=gen::blockNameToState().find(fluid); if(it!=gen::blockNameToState().end()) fluidSt=static_cast<uint16_t>(it->second); }
                }
                // Nether water evaporates
                if(fluid=="minecraft:water" && dimensionWorld.dimensionId()==-1){
                    // evaporate with particles/sound
                    broadcastSound("minecraft:block.fire.extinguish", tx+0.5,ty+0.5,tz+0.5,0.5f,2.6f,"block");
                } else {
                    dimensionWorld.setBlock(tx,ty,tz,fluidSt);
                    broadcastBlockChange(tx,ty,tz,fluidSt);
                    if(fluid=="minecraft:water" || fluid=="minecraft:lava"){
                        dimensionFluids.touch(tx,ty,tz);
                    }
                }
                // replace with empty bucket
                s = ItemStack::ofName("minecraft:bucket",1);
                handled=true;
            } else {
                // fallback drop
                spawnItemDropStack(sx, sy, sz, oneItem(s),
                                    dx * .25, .15, dz * .25);
                if(--s.count<=0) s=ItemStack::air();
                handled=true;
            }
        } else if(!handled && iname=="minecraft:bucket"){
            uint16_t tSt = dimensionWorld.getBlock(tx,ty,tz);
            const gen::BlockDef* td = gen::blockByState(tSt);
            bool isWater=false,isLava=false,isPowder=false;
            if(td){
                if(td->name=="minecraft:water"){
                    for(auto&[k,v]: gen::propsOf(tSt)) if(k=="level"&&v=="0") isWater=true;
                } else if(td->name=="minecraft:lava"){
                    for(auto&[k,v]: gen::propsOf(tSt)) if(k=="level"&&v=="0") isLava=true;
                } else if(td->name=="minecraft:powder_snow") isPowder=true;
            }
            if(isWater||isLava||isPowder){
                dimensionWorld.setBlock(tx,ty,tz,0);
                broadcastBlockChange(tx,ty,tz,0);
                std::string newName = isLava?"minecraft:lava_bucket":(isPowder?"minecraft:powder_snow_bucket":"minecraft:water_bucket");
                s = ItemStack::ofName(newName,1);
                handled=true;
            }
        } else if(!handled && (iname.find("splash_potion")!=std::string::npos || iname.find("lingering_potion")!=std::string::npos || iname=="minecraft:potion")){
            // strict B23: potion projectile should be Potion entity, not Snowball
            server_.spawnProjectileFor(dimension, ProjectileKind::Potion, sx, sy, sz, dx*1.1, dy*0.2+0.12, dz*1.1, -1, false);
            if(--s.count<=0) s=ItemStack::air();
            handled=true;
        } else if(!handled && (iname.find("_helmet")!=std::string::npos || iname.find("_chestplate")!=std::string::npos || iname.find("_leggings")!=std::string::npos || iname.find("_boots")!=std::string::npos || iname.find("horse_armor")!=std::string::npos || iname=="minecraft:elytra" || iname=="minecraft:turtle_helmet" || iname=="minecraft:carved_pumpkin" || iname=="minecraft:skull")){
            // strict B24: dispenser armor equip (vanilla Dispenser armor)
            bool equipped=false;
            std::shared_ptr<Player> equippedPlayer;
            // Try players at target
            for(auto &pp : server_.playersSnapshot()){
                int slot=-1;
                if(iname.find("_helmet")!=std::string::npos || iname=="minecraft:turtle_helmet" || iname=="minecraft:carved_pumpkin" || iname.find("skull")!=std::string::npos) slot=8;
                else if(iname.find("_chestplate")!=std::string::npos || iname=="minecraft:elytra") slot=7;
                else if(iname.find("_leggings")!=std::string::npos) slot=6;
                else if(iname.find("_boots")!=std::string::npos) slot=5;
                else if(iname.find("horse_armor")!=std::string::npos) slot=-1; // not for player
                if (slot < 5 || slot > 8) continue;

                // The session thread and item pickup/tick code can
                // touch the same inventory concurrently.  Keep
                // only the position check and the actual equip
                // mutation under the player's state lock; the
                // attribute/equipment notifications below may
                // send packets and therefore stay outside it.
                std::unique_lock playerLock(pp->stateMtx);
                if (!pp->inPlay || server_.canonicalDimension(pp->dimension) !=
                    server_.canonicalDimension(dimension)) continue;
                const int px=(int)std::floor(pp->x);
                const int py=(int)std::floor(pp->y);
                const int pz=(int)std::floor(pp->z);
                // target is tx,ty,tz; allow one block tolerance for standing entity (ty may be feet)
                if (px != tx || pz != tz ||
                    (py != ty && py != ty+1 && py != ty-1)) continue;
                if (pp->inv[slot].empty()) {
                    pp->inv[slot]=oneItem(s);
                    equippedPlayer = pp;
                    break;
                }
            }
            if (equippedPlayer) {
                server_.syncPlayerArmorAttributes(*equippedPlayer);
                server_.broadcastPlayerEquipment(*equippedPlayer);
                equipped=true;
            }
            if(!equipped){
                // Try mobs at target
                std::shared_ptr<MobEntity> equippedMob;
                int equippedMobSlot = -1;
                for (const auto& m : server_.mobsSnapshot()) {
                    if (!m) continue;
                    std::lock_guard mobLock(*m->stateMtx);
                    if (server_.canonicalDimension(m->dimension) !=
                        server_.canonicalDimension(dimension)) continue;
                    const int mx = static_cast<int>(std::floor(m->x));
                    const int my = static_cast<int>(std::floor(m->y));
                    const int mz = static_cast<int>(std::floor(m->z));
                    if (mx != tx || mz != tz ||
                        (my != ty && my != ty + 1 && my != ty - 1))
                        continue;
                    int eslot = -1;
                    if (iname.find("_helmet") != std::string::npos ||
                        iname == "minecraft:turtle_helmet" ||
                        iname == "minecraft:carved_pumpkin")
                        eslot = 5;
                    else if (iname.find("_chestplate") != std::string::npos ||
                             iname == "minecraft:elytra")
                        eslot = 4;
                    else if (iname.find("_leggings") != std::string::npos)
                        eslot = 3;
                    else if (iname.find("_boots") != std::string::npos)
                        eslot = 2;
                    else if (iname.find("horse_armor") != std::string::npos &&
                             (m->kind == MobKind::Horse ||
                              m->kind == MobKind::Donkey ||
                              m->kind == MobKind::Mule ||
                              m->kind == MobKind::Llama ||
                              m->kind == MobKind::TraderLlama))
                        eslot = 4;
                    if (eslot < 2 || eslot > 5 ||
                        !m->equipment[eslot].empty())
                        continue;
                    m->equipment[eslot] = oneItem(s);
                    equippedMob = m;
                    equippedMobSlot = eslot;
                    equipped = true;
                    break;
                }
                if (equippedMob)
                    server_.sendEquipmentSlot(*equippedMob, equippedMobSlot);
            }
            if(equipped){
                if(--s.count<=0) s=ItemStack::air();
                handled=true;
            } else {
                spawnItemDropStack(sx, sy, sz, oneItem(s),
                                    dx * .25, .15, dz * .25);
                if(--s.count<=0) s=ItemStack::air();
                handled=true;
            }
        } else if(!handled && iname.find("arrow") != std::string::npos) {
            server_.spawnProjectileFor(dimension, ProjectileKind::Arrow, sx, sy, sz, dx*1.2, dy*0.2+0.15, dz*1.2, -1, false);
            if(--s.count<=0) s=ItemStack::air();
            handled = true;
        } else if(!handled && iname.find("snowball") != std::string::npos) {
            server_.spawnProjectileFor(dimension, ProjectileKind::Snowball, sx, sy, sz, dx*1.2, dy*0.2+0.12, dz*1.2, -1, false);
            if(--s.count<=0) s=ItemStack::air();
            handled = true;
        } else if(!handled && iname == "minecraft:egg") {
            server_.spawnProjectileFor(dimension, ProjectileKind::Egg, sx, sy, sz, dx*1.2, dy*0.2+0.12, dz*1.2, -1, false);
            if(--s.count<=0) s=ItemStack::air();
            handled = true;
        } else if(!handled && iname.find("ender_pearl") != std::string::npos) {
            server_.spawnProjectileFor(dimension, ProjectileKind::EnderPearl, sx, sy, sz, dx*1.2, dy*0.2+0.12, dz*1.2, -1, false);
            if(--s.count<=0) s=ItemStack::air();
            handled = true;
        } else if(!handled && iname.find("fire_charge") != std::string::npos) {
            server_.spawnProjectileFor(dimension, ProjectileKind::Fireball, sx, sy, sz, dx*0.5, dy*0.5, dz*0.5, -1, false);
            if(--s.count<=0) s=ItemStack::air();
            handled = true;
        } else if(!handled && iname.find("_spawn_egg") != std::string::npos) {
            MobSpawner spawner2(server_);
            if (spawner2.spawnFromDispenserFor(dimension, iname,
                                               x, y, z, facing)) {
                if(--s.count<=0) s=ItemStack::air();
            } else {
                spawnItemDropStack(sx, sy, sz, oneItem(s),
                                    dx * .25, .15, dz * .25);
                if(--s.count<=0) s=ItemStack::air();
            }
            handled = true;
        } else if(!handled && iname=="minecraft:shears"){
            // try shear sheep at target
            std::shared_ptr<MobEntity> shearedMob;
            std::int32_t shearedEntityId = 0;
            std::int8_t shearedDimension = server_.canonicalDimension(dimension);
            double shearedX = 0.0;
            double shearedY = 0.0;
            double shearedZ = 0.0;
            int shearedColor = 0;
            {
                for (const auto& m : server_.mobsSnapshot()) {
                    if (!m) continue;
                    std::lock_guard mobLock(*m->stateMtx);
                    if (server_.canonicalDimension(m->dimension) !=
                        server_.canonicalDimension(dimension)) continue;
                    const int mx = static_cast<int>(std::floor(m->x));
                    const int my = static_cast<int>(std::floor(m->y));
                    const int mz = static_cast<int>(std::floor(m->z));
                    if (mx == tx && my == ty && mz == tz &&
                        m->kind == MobKind::Sheep && !m->sheared) {
                        m->sheared = true;
                        shearedMob = m;
                        shearedEntityId = m->entityId;
                        shearedDimension = server_.canonicalDimension(m->dimension);
                        shearedX = m->x;
                        shearedY = m->y;
                        shearedZ = m->z;
                        shearedColor = m->woolColor % 16;
                        break;
                    }
                }
            }
            if(shearedMob){
                // Do not call entity-spawn/broadcast helpers
                // while entsMtx_ is held: both helpers may
                // take the same lock or snapshot players.
                static const char* woolNamesD[] = {
                    "minecraft:white_wool","minecraft:orange_wool","minecraft:magenta_wool","minecraft:light_blue_wool",
                    "minecraft:yellow_wool","minecraft:lime_wool","minecraft:pink_wool","minecraft:gray_wool",
                    "minecraft:light_gray_wool","minecraft:cyan_wool","minecraft:purple_wool","minecraft:blue_wool",
                    "minecraft:brown_wool","minecraft:green_wool","minecraft:red_wool","minecraft:black_wool"
                };
                int colD = shearedColor;
                auto woolIt=gen::itemIdByName().find(woolNamesD[colD]);
                if(woolIt!=gen::itemIdByName().end()){
                    int cnt=1+nextRandom()%3;
                    server_.spawnItemDropFor(shearedDimension, shearedX,
                                  shearedY + 0.8, shearedZ,
                                  woolIt->second,
                                  (uint8_t)cnt,
                                  (nextRandom()/(double)RAND_MAX-.5)*0.12,
                                  0.12,
                                  (nextRandom()/(double)RAND_MAX-.5)*0.12);
                }
                WriteBuffer md; md.varint(shearedEntityId);
                md.u8(17); md.u8(8); md.u8(1); md.u8(255);
                server_.broadcastPacketExceptInDimension(
                    shearedDimension, nullptr,
                    proto::pl::sc::SetEntityMetadata, md);
                if(s.applyDamage(1)) s=ItemStack::air();
                handled=true;
            } else {
                // check for snow_golem/mooshroom simplified: just drop if not sheared
                spawnItemDropStack(sx, sy, sz, oneItem(s),
                                    dx * .25, .15, dz * .25);
                // don't consume? vanilla consumes durability only on success, but we treat as not consumed
                handled=true; // don't double-decrement
            }
        } else if(!handled && iname=="minecraft:flint_and_steel"){
            uint16_t tSt = dimensionWorld.getBlock(tx,ty,tz);
            uint16_t below = dimensionWorld.getBlock(tx,ty-1,tz);
            const gen::BlockDef* td=gen::blockByState(tSt);
            const gen::BlockDef* bd=gen::blockByState(below);
            bool isAir = tSt==0;
            bool belowSolid = bd && td==nullptr; // simplified: any non-air below is solid
            // also check for TNT, campfire, portal
            bool handledFS=false;
            if(td && std::string(td->name)=="minecraft:tnt"){
                spawnPrimedTnt(tx+0.5, ty+0.5, tz+0.5, 0, 0.2, 0, 80);
                broadcastSound("minecraft:entity.tnt.primed", tx+0.5, ty+0.5, tz+0.5, 1.f, 1.f, "block");
                dimensionWorld.setBlock(tx,ty,tz,0); broadcastBlockChange(tx,ty,tz,0);
                handledFS=true;
            } else if(td && (std::string(td->name)=="minecraft:campfire" || std::string(td->name)=="minecraft:soul_campfire")){
                std::string lit=getPropStr(tSt,"lit");
                if(lit=="false"){
                    std::vector<std::pair<std::string_view,std::string_view>> props;
                    for(auto&[k,v]: gen::propsOf(tSt)) if(k!="lit") props.emplace_back(k,v);
                    props.emplace_back("lit","true");
                    uint16_t ns=static_cast<uint16_t>(gen::stateWithProps(*td, props));
                    dimensionWorld.setBlock(tx,ty,tz,ns); broadcastBlockChange(tx,ty,tz,ns);
                    handledFS=true;
                }
            } else if(isAir && belowSolid){
                bool soulBase = false;
                if (bd) {
                    auto &tags = server_.datapackManager_.tagManager.blockTags;
                    auto it = tags.find("minecraft:soul_fire_base_blocks");
                    if (it != tags.end()) {
                        auto nit = gen::blockNameToState().find(std::string(bd->name));
                        if (nit != gen::blockNameToState().end()) soulBase = it->second.count(static_cast<uint32_t>(nit->second))>0;
                    }
                    if (!soulBase) soulBase = std::string(bd->name)=="minecraft:soul_sand"||std::string(bd->name)=="minecraft:soul_soil";
                }
                std::string fn = soulBase?"minecraft:soul_fire":"minecraft:fire";
                auto it=gen::blockNameToState().find(fn);
                if(it!=gen::blockNameToState().end()){
                    uint16_t fs=static_cast<uint16_t>(it->second);
                    dimensionWorld.setBlock(tx,ty,tz,fs); broadcastBlockChange(tx,ty,tz,fs);
                    handledFS=true;
                }
            }
            if(handledFS){
                if(s.applyDamage(1)) s=ItemStack::air();
                handled=true;
            } else {
                spawnItemDropStack(sx, sy, sz, oneItem(s),
                                    dx * .25, .15, dz * .25);
                handled=true;
            }
        } else if(!handled && iname=="minecraft:bone_meal"){
            uint16_t tSt = dimensionWorld.getBlock(tx,ty,tz);
            const gen::BlockDef* td=gen::blockByState(tSt);
            bool fertilized=false;
            if(td){
                auto* beh=dimensionBlockTicks.behaviorFor(std::string(td->name));
                if(beh && beh->fertilize(dimensionWorld, tx,ty,tz,tSt,&server_)){
                    uint16_t ns=dimensionWorld.getBlock(tx,ty,tz);
                    broadcastBlockChange(tx,ty,tz,ns);
                    broadcastSound("minecraft:item.bone_meal.use", tx+0.5,ty+0.5,tz+0.5,1.f,1.f,"block");
                    fertilized=true;
                }
            }
            if(fertilized){
                if(--s.count<=0) s=ItemStack::air();
                handled=true;
            } else {
                spawnItemDropStack(sx, sy, sz, oneItem(s),
                                    dx * .25, .15, dz * .25);
                if(--s.count<=0) s=ItemStack::air();
                handled=true;
            }
        } else if(!handled && (iname=="minecraft:tnt" || iname.find("tnt") != std::string::npos)) {
            spawnPrimedTnt(x + dx + 0.5, y + 0.3, z + dz + 0.5, dx*0.2, 0.2, dz*0.2, 80);
            broadcastSound("minecraft:entity.tnt.primed", x+dx+0.5, y+dy+0.5, z+dz+0.5, 1.f, 1.f, "block");
            if(--s.count<=0) s=ItemStack::air();
            handled=true;
        } else if(!handled) {
            // default drop
            spawnItemDropStack(sx, sy, sz, oneItem(s),
                                dx * .25, .15, dz * .25);
            if(--s.count<=0) s=ItemStack::air();
            handled=true;
        }
        if(handled){
            broadcastSound("minecraft:block.dispenser.dispense", x + .5, y + .5, z + .5, 1.f, 1.f, "block");
            invalidateEntityChunk(key);
        }

    }

};

void GameServer::hoppersTickFor(std::int8_t dimension) {
    HopperTickService(*this, dimension).run();
}

} // namespace cppfm
