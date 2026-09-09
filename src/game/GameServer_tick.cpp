#include "GameServer.hpp"
#include "BlockEvent.hpp"
#include "MetadataTypes.hpp"
#include "../physics/LightEngine.hpp"
#include "../physics/Fluids.hpp"
#include "../physics/Redstone.hpp"
#include "GameServerHelpers.hpp"
#include "Constants.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/EntityIds.hpp"
#include "DamageComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "MiningCalculator.hpp"

namespace cppfm {
using namespace proto;

namespace {
struct PlayerTickView {
    std::shared_ptr<Player> owner;
    std::int8_t dimension = 0;
    std::int32_t entityId = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    bool inPlay = false;
    bool spawned = false;
    bool dead = false;
};

PlayerTickView snapshotPlayerForTick(const std::shared_ptr<Player>& player) {
    PlayerTickView view;
    view.owner = player;
    if (!player) return view;
    std::lock_guard lock(player->stateMtx);
    view.dimension = GameServer::canonicalDimension(player->dimension);
    view.entityId = player->entityId;
    view.x = player->x;
    view.y = player->y;
    view.z = player->z;
    view.inPlay = player->inPlay;
    view.spawned = player->spawned;
    view.dead = player->dead;
    return view;
}
}

void GameServer::drainServerThreadTasks() noexcept {
    for (std::size_t i = 0; i < kMaxServerThreadTasksPerTick; ++i) {
        std::shared_ptr<ServerThreadTask> request;
        {
            std::lock_guard lock(serverThreadTasksMtx_);
            if (serverThreadId_ != std::this_thread::get_id() ||
                !serverThreadAccepting_ ||
                !running_.load(std::memory_order_acquire) ||
                serverThreadTasks_.empty()) return;
            request = std::move(serverThreadTasks_.front());
            serverThreadTasks_.pop_front();
            auto expected = ServerThreadTask::State::Pending;
            if (!request || !request->state.compare_exchange_strong(
                    expected, ServerThreadTask::State::Running,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                continue;
            }
            // Increment while holding the queue mutex.  A concurrent JVM
            // stop() can therefore not observe an empty queue and zero active
            // work in the small window between dequeue and invocation.
            activeServerThreadTasks_.fetch_add(1, std::memory_order_acq_rel);
        }

        try {
            request->task();
            request->state.store(ServerThreadTask::State::Completed,
                                 std::memory_order_release);
        } catch (...) {
            request->state.store(ServerThreadTask::State::Failed,
                                 std::memory_order_release);
        }
        request->waitCv.notify_all();
        activeServerThreadTasks_.fetch_sub(1, std::memory_order_acq_rel);
        serverThreadTaskIdleCv_.notify_all();
    }
}

namespace {
void cancelMiningDig(GameServer& server, Player& player) {
    if (player.digActive) server.broadcastDigStage(player, -1);
    player.digActive = false;
    player.digTotalTicks = 0;
    player.digLastStage = 255;
}
} // namespace

void GameServer::tickDigs() {
    for (auto& pp : playersSnapshot()) {
        auto* p = pp.get();
        if (!p->digActive || !p->inPlay) continue;
        if (p->gamemode != 0) {
            // A survival dig cannot outlive a gamemode change.
            cancelMiningDig(*this, *p);
            continue;
        }
        const std::int64_t elapsed = tickNo_ -
            MiningCalculator::unpackDigStartTick(p->digStartTick);
        if (p->digTotalTicks <= 0) {
            cancelMiningDig(*this, *p);
            continue;
        }
        const bool canHarvest = MiningCalculator::digCanHarvest(p->digLastStage);
        const std::uint8_t previousStage =
            MiningCalculator::unpackDigStage(p->digLastStage);
        const std::int64_t stage64 = elapsed * 10 / p->digTotalTicks;
        const std::uint8_t stage = static_cast<std::uint8_t>(std::clamp<std::int64_t>(stage64, 0, 9));
        if (stage != previousStage) {
            p->digLastStage = MiningCalculator::packDigStage(stage, canHarvest);
            broadcastDigStage(*p, static_cast<std::int8_t>(stage));
        }
        if (elapsed >= p->digTotalTicks) {
            // server-authoritative completion
            World& world = worldFor(p->dimension);
            const std::uint16_t oldState = world.getBlock(p->digX, p->digY, p->digZ);
            std::fprintf(stderr, "[cppfm] DIG COMPLETE at (%d,%d,%d) oldState=%u\n",
                         p->digX, p->digY, p->digZ, oldState);
            const gen::BlockDef* def = gen::blockByState(oldState);
            if (oldState != MiningCalculator::digStartingState(p->digStartTick) ||
                oldState == 0 || !def || def->hardness < 0.f) {
                // The target changed or disappeared while mining; never turn
                // a stale progress record into a break of another block.
                WriteBuffer rb;
                rb.position(p->digX, p->digY, p->digZ);
                rb.varint(oldState);
                broadcastPacketExceptInDimension(p->dimension, nullptr,
                                                 proto::pl::sc::BlockUpdate, rb);
                cancelMiningDig(*this, *p);
                continue;
            }

            api::BlockBreakEvent ev;
            ev.player = p;
            ev.x = p->digX; ev.y = p->digY; ev.z = p->digZ;
            ev.oldState = oldState;
            if (!events().blockBreak.fire(ev)) {
                // cancelled: restore + stop animating
                WriteBuffer rb;
                rb.position(p->digX, p->digY, p->digZ);
                rb.varint(oldState);
                broadcastPacketExceptInDimension(p->dimension, nullptr,
                                                 proto::pl::sc::BlockUpdate, rb);
                cancelMiningDig(*this, *p);
                continue;
            }
            if (jvmRuntime_ && !jvmRuntime_->onBlockBreak(*p, p->digX, p->digY,
                                                          p->digZ, oldState)) {
                WriteBuffer rb;
                rb.position(p->digX, p->digY, p->digZ);
                rb.varint(oldState);
                broadcastPacketExceptInDimension(p->dimension, nullptr,
                                                 proto::pl::sc::BlockUpdate, rb);
                cancelMiningDig(*this, *p);
                continue;
            }
            if (world.getBlock(p->digX, p->digY, p->digZ) != oldState) {
                // Event listeners may synchronously replace the target.
                // Revalidate before mutating, durability, or drops.
                WriteBuffer rb;
                rb.position(p->digX, p->digY, p->digZ);
                rb.varint(world.getBlock(p->digX, p->digY, p->digZ));
                broadcastPacketExcept(nullptr, proto::pl::sc::BlockUpdate, rb);
                cancelMiningDig(*this, *p);
                continue;
            }
            world.setBlock(p->digX, p->digY, p->digZ, 0);
            broadcastBlockChangeFor(p->dimension, p->digX, p->digY, p->digZ, 0);
            if (const auto* broken = gen::blockByState(oldState);
                broken && std::string(broken->name).find("_bed") != std::string::npos) {
                invalidateRespawnPointsAt(p->dimension, p->digX, p->digY, p->digZ);
            }
            HungerManager::onBlockBreak(*p, *this);
            blockEventDispatcher().onBlockBreak(p->digX, p->digY, p->digZ, oldState, p);
            onBlockMined(*p, oldState);
            {
                const std::string _bn = blockNameByState(oldState);
                if (_bn == "minecraft:tnt") {
                    std::string unstableVal;
                    for (auto& [k,v] : gen::propsOf(oldState)) if (k=="unstable") unstableVal = std::string(v);
                    const bool isUnstable = (unstableVal == "true");
                    const bool isCreative = (p->gamemode == 1);
                    bool hasFlint = false;
                    if (p->heldSlot >=0 && p->heldSlot <9) {
                        const auto& _held = p->inv[36 + p->heldSlot];
                        if (!_held.empty() && _held.name() == "minecraft:flint_and_steel") hasFlint = true;
                    }
                    if (hasFlint) {
                        spawnPrimedTntFor(p->dimension, p->digX + 0.5,
                                          p->digY + 0.5, p->digZ + 0.5,
                                          0, 0.2, 0, 80);
                        broadcastSoundFor(p->dimension,
                                          "minecraft:entity.tnt.primed",
                                          p->digX + 0.5, p->digY + 0.5,
                                          p->digZ + 0.5, 1.f, 1.f, "block");
                        if (!isCreative && p->heldSlot>=0 && p->heldSlot<9) {
                            auto& _h = p->inv[36 + p->heldSlot];
                            if (_h.applyDamage(1)) _h = ItemStack::air();
                            resendInventory(*p);
                        }
                        cancelMiningDig(*this, *p);
                        continue;
                    } else if (isUnstable && !isCreative) {
                        spawnPrimedTntFor(p->dimension, p->digX + 0.5,
                                          p->digY + 0.5, p->digZ + 0.5,
                                          0, 0.2, 0, 80);
                        broadcastSoundFor(p->dimension,
                                          "minecraft:entity.tnt.primed",
                                          p->digX + 0.5, p->digY + 0.5,
                                          p->digZ + 0.5, 1.f, 1.f, "block");
                        cancelMiningDig(*this, *p);
                        continue;
                    }
                }
            }
            if (p->heldSlot >=0 && p->heldSlot <9) {
                auto &held = p->inv[36 + p->heldSlot];
                if (!held.empty() && ItemStack::maxDamageFor(held.itemId) > 0) {
                    if (DamageComponent::applyDamage(held, 1)) held = ItemStack::air();
                    resendInventory(*p);
                }
            }
            if (!ev.dropItems) {
                cancelMiningDig(*this, *p);
                continue;
            }

            const std::string bn = blockNameByState(oldState);
            if (canHarvest) {
                ItemStack heldStack;
                if (p->heldSlot >= 0 && p->heldSlot <9) heldStack = p->inv[36 + p->heldSlot];
                std::vector<ItemStack> drops;
                if (heldStack.hasSilkTouch()) {
                    if (bn != "minecraft:air") {
                        auto ii = gen::itemIdByName().find(bn);
                        if (ii != gen::itemIdByName().end())
                            drops.push_back(ItemStack::of(ii->second, 1));
                    }
                } else if (bn != "minecraft:glass") {
                    drops = lootTables_.evaluate(bn, heldStack);
                    if (drops.empty()) {
                        static const std::unordered_map<std::string,std::string> kOv{
                            {"minecraft:grass_block","minecraft:dirt"},
                            {"minecraft:stone","minecraft:cobblestone"}};
                        auto ov = kOv.find(bn);
                        const std::string dn = ov!=kOv.end()?ov->second:bn;
                        auto ii = gen::itemIdByName().find(dn);
                        if (ii != gen::itemIdByName().end())
                            drops.push_back(ItemStack::of(ii->second, 1));
                    }
                }
                for (auto &st : drops) {
                    if (st.empty()) continue;
                    spawnItemDropFor(p->dimension, p->digX + .5,
                                     p->digY + .25, p->digZ + .5, st,
                                     (nextRandom()/(double)RAND_MAX-.5)*.15,
                                     .12,
                                     (nextRandom()/(double)RAND_MAX-.5)*.15);
                }
            }
            cancelMiningDig(*this, *p);
        }
    }
}
void GameServer::tickOnce() {
    // JVM-created workers can only mutate game state through this queue.  Run
    // it before native simulation and again after the synchronous JVM tick
    // callback so a short worker request is visible in the same tick when it
    // does not have to wait behind that callback.
    drainServerThreadTasks();
    pollPendingLoads(); // W19 async I/O: poll Chunk futures (ThreadPool 4) without blocking (MC-177729)
    api::ServerTickEvent ev{tickNo_};
    events().serverTick.fire(ev);
    if (jvmRuntime_) jvmRuntime_->onServerTick(tickNo_);
    drainServerThreadTasks();
    fluidSim_->tick(tickNo_);
    redstone_->tick(tickNo_);
    if (blockTicks_) blockTicks_->tick(tickNo_);
    for (int i = 0; i < 2; ++i) {
        dimFluidSim_[i]->tick(tickNo_);
        dimRedstone_[i]->tick(tickNo_);
        dimBlockTicks_[i]->tick(tickNo_);
    }
    craftersTick();
    tickDigs();
    survivalTick();
    furnacesTick();
    brewingTick();
    hoppersTick();
    effectsTick();
    xpOrbsTick();

    // mob spawn cadence: every 20 ticks
    if (tickNo_ % 20 == 0) trySpawnMobs();
    mobsTick();
    drainPendingStructureQueues();
    minecartsTick(); // plan14 §5: powered_rail 0.06
    boatsTick(); // plan14 §5: boat friction 0.9 water / 0.6 land, buoyancy 0.04, max 0.4
    projectilesTick();
    itemsTick();
    tntTick();

    // periodic time sync every 20 ticks (1s); frozen when doDaylightCycle off
    if (tickNo_ % 20 == 0) {
        if (!gamerules_.contains("doDaylightCycle") ||
            gamerules_.getBool("doDaylightCycle")) {
            WriteBuffer t;
            t.i64(tickNo_);
            t.i64(dayTime());
            t.boolean(true);
            for (const auto dimension : {std::int8_t{0}, std::int8_t{-1},
                                         std::int8_t{1}}) {
                broadcastPacketExceptInDimension(dimension, nullptr,
                                                 pl::sc::UpdateTime, t);
            }
        }
    }

    // Each dimension owns a light queue.  Draining them independently keeps
    // an UpdateLight packet from describing a same-coordinate chunk in the
    // wrong world.
    auto drainLight = [this](std::int8_t dimension, LightEngine& light,
                             World& world) {
        const LightUpdateBatch batch = light.drain();
        for (auto k : batch.dirtyChunks) {
            auto [cx, cz] = chunkKeyDecode(k);
            world.withChunk(cx, cz, [&](const Chunk& c) {
                WriteBuffer b;
                serializeUpdateLightBody(b, cx, cz, c);
                broadcastPacketExceptInDimension(dimension, nullptr,
                                                 pl::sc::UpdateLight, b);
            });
        }
    };
    drainLight(0, *lightEngine_, world_);
    for (int i = 0; i < 2; ++i)
        drainLight(i == 0 ? -1 : 1, *dimLightEngine_[i],
                   *worlds_[i + 1]);

    // periodic progress save every 20 s (play_time accrual + crash safety)
    if (tickNo_ % 400 == 0) {
        for (auto& p : playersSnapshot()) {
            if (p->stats) {
                p->stats->add("minecraft:custom|minecraft:play_time", 400);
                p->stats->save(uuidToHex(p->uuid));
            }
            if (p->advancements) p->advancements->save();
        }
    }
    if (tickNo_ % 100 == 0) chunksUnloadTick();
    // level.dat periodic save every 6000 ticks (~5 min) + also 1200 (~1 min) for safety — single level.dat (W16)
    if (tickNo_ % 6000 == 0 && tickNo_ != 0) {
        try {
            persist_->saveLevelData(tickNo_, dayTime());
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[cppfm] periodic level.dat save failed: %s\n", e.what());
        } catch (...) {
            std::fprintf(stderr, "[cppfm] periodic level.dat save failed\n");
        }
        std::fprintf(stderr, "[cppfm] periodic level.dat save t=%ld\n", (long)tickNo_);
    } else if (tickNo_ % 1200 == 0 && tickNo_ != 0) {
        try {
            persist_->saveLevelData(tickNo_, dayTime());
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[cppfm] periodic level.dat save failed: %s\n", e.what());
        } catch (...) {
            std::fprintf(stderr, "[cppfm] periodic level.dat save failed\n");
        }
    }
    // WorldBorder lerp tick interpolation — Yarn WorldBorder.tick()
    {
        bool changed = tickWorldBorder();
        if (persist_) changed |= persist_->tickWorldBorder();
        if (changed && tickNo_ % 20 == 0) {
            // periodically broadcast interpolated size to keep client in sync (lerp packet)
            broadcastWorldBorder();
        }
    }
    // WanderingTrader scheduling — vanilla 24000 delay + chance (doTraderSpawning)
    tickWanderingTrader();
    // B-12 thunder lightning + weather cycle (doWeatherCycle gate inside weatherTick)
    weatherTick();
    tickScheduledFunctions();
    for (auto& pp : playersSnapshot()) if (pp->inPlay) evaluateTickAdvancements(*pp);
    if (tickNo_ % 20 == 0) {
        for (auto& pp : playersSnapshot()) if (pp->inPlay) evaluateLocationTrigger(*pp);
    }
    if (tickNo_ % 20 == 0) {
        for (auto& pp : playersSnapshot()) if (pp->inPlay) {
            int bx = static_cast<int>(std::floor(pp->x));
            int by = static_cast<int>(std::floor(pp->y));
            int bz = static_cast<int>(std::floor(pp->z));
            onEnterBlock(pp.get(), bx, by, bz);
        }
    }
    // network batching: flush coalesced block updates every tick (50ms window)
    {
        int64_t now = nowMs();
        if (!batcher_.empty() && now - batcher_.lastFlushMs.load() >= constants::kBlockBatchFlushMs) {
            batcher_.flush(*this, nullptr);
            lastBlockBatchFlushMs_ = now;
        } else if (!batcher_.empty() && tickNo_ % 2 == 0) {
            flushBlockBatches();
        }
    }
}
void GameServer::drainPendingStructureQueues() {
    auto process = [&](World& w, std::int8_t dimension){
        auto& blockEntities = blockEntitiesFor(dimension);
        auto* sm = w.structureManager();
        if(!sm) return;
        std::vector<worldgen::StructureManager::PendingLoot> loots;
        sm->drainPendingLoot(loots);
        for(auto &pl : loots){
            auto drops = lootTables_.evaluate(pl.lootTable);
            int x=pl.pos[0], y=pl.pos[1], z=pl.pos[2];
            auto beOwner = blockEntities.getShared(posKey(x, y, z));
            if (!beOwner)
                beOwner = blockEntities.createShared(
                    posKey(x, y, z), BlockEntity::Kind::Chest);
            std::lock_guard entityLock(*beOwner->stateMtx);
            auto* be = beOwner.get();
            if(be->kind != BlockEntity::Kind::Chest){
                be->kind = BlockEntity::Kind::Chest;
                for(int i=0;i<ChestData::kSlots;++i) be->chest.slots[i]=ItemStack::air();
            }
            for(int i=0;i<ChestData::kSlots;++i) be->chest.slots[i]=ItemStack::air();
            if(drops.empty()){
                std::fprintf(stderr,"[cppfm] pending loot %s at %d %d %d => 0 drops\n", pl.lootTable.c_str(), x,y,z);
            }
            std::unordered_set<int> used;
            for(auto &st : drops){
                if(st.empty()) continue;
                int slot=-1;
                for(int attempt=0; attempt<10; ++attempt){
                    int cand = nextRandom() % ChestData::kSlots;
                    if(!used.count(cand)){ slot=cand; break; }
                }
                if(slot==-1) slot = nextRandom()%ChestData::kSlots;
                used.insert(slot);
                be->chest.slots[slot]=st;
            }
            blockEntities.markDirty(posKey(x,y,z));
            if(!drops.empty())
                std::fprintf(stderr,"[cppfm] pending loot %s at %d %d %d => %zu stacks\n", pl.lootTable.c_str(), x,y,z, drops.size());
        }
        std::vector<worldgen::StructureManager::PendingMob> mobs;
        sm->drainPendingMobs(mobs);
        for(auto &pm : mobs){
            MobKind kind = MobKind::Zombie;
            bool found=false;
            for(int i=0;i<149;++i){ if(std::string(mobStats(static_cast<MobKind>(i)).name)==pm.mob){ kind=static_cast<MobKind>(i); found=true; break; } }
            if(!found){
                std::fprintf(stderr,"[cppfm] pending mob unknown %s\n", pm.mob.c_str());
                continue;
            }
            for(int c=0;c<pm.count;++c){
                auto mob = std::make_shared<MobEntity>();
                mob->entityId = nextEntityId();
                mob->dimension = canonicalDimension(dimension);
                mob->kind = kind;
                mob->health = mobStats(kind).maxHealth;
                mob->x = pm.pos[0] + 0.5;
                mob->y = pm.pos[1] + 0.5;
                mob->z = pm.pos[2] + 0.5;
                mob->lastSeenMs = nowMs();
                if (jvmRuntime_ && !jvmRuntime_->onMobSpawn(*mob, mob->x, mob->y, mob->z))
                    continue;
                {
                    std::lock_guard<std::mutex> lk(entsMtx_);
                    mobs_.push_back(mob);
                }
                broadcastMobSpawn(*mob);
                std::fprintf(stderr,"[cppfm] pending mob %s at %d %d %d id %d\n", pm.mob.c_str(), pm.pos[0], pm.pos[1], pm.pos[2], mob->entityId);
            }
        }
    };
    process(world_, 0);
    if(netherWorld_) process(*netherWorld_, -1);
    if(endWorld_) process(*endWorld_, 1);
}
bool GameServer::isChunkInSimulationDistanceFor(std::int8_t dimension,
                                                std::int32_t cx,
                                                std::int32_t cz) const {
    const World& simulatedWorld = worldFor(dimension);
    // Forced chunks and spawn-ticket chunks belong to this dimension only.
    if (simulatedWorld.isForced(cx, cz) ||
        simulatedWorld.ticketLevel(cx, cz) <= constants::kTicketLevelSpawn)
        return true;
    const int sim = cfg_.simulationDistance;
    if (sim <= 0) return true;
    const double limit = sim * 16.0;
    const double chX = cx * 16.0 + 8.0;
    const double chZ = cz * 16.0 + 8.0;
    const auto players = playersSnapshot();
    if (players.empty()) return false;
    for (auto &p : players) {
        if (!p || !p->inPlay || p->dimension != dimension) continue;
        double dx = p->x - chX;
        double dz = p->z - chZ;
        if (std::max(std::abs(dx), std::abs(dz)) < limit) return true;
    }
    return false;
}
bool GameServer::isChunkInSimulationDistance(std::int32_t cx,
                                             std::int32_t cz) const {
    return isChunkInSimulationDistanceFor(0, cx, cz);
}
void GameServer::chunksUnloadTick() {
    const int sim = cfg_.simulationDistance;
    const int view = cfg_.viewDistance;
    // Keep chunks through the configured view/simulation radius, but do not
    // retain an extra two-block ring (32 blocks) after it leaves that radius.
    // The old margin made a long straight traversal accumulate roughly 500
    // live 384-high chunks even though only the configured radius was active.
    const int unloadDist = std::max(sim, view) * 16;
    auto doWorld = [&](World &w, Persistence *pp, std::int8_t dim) {
        auto keys = w.allChunkKeys();
        std::vector<std::int64_t> toErase;
        toErase.reserve(keys.size());
        auto players = playersSnapshot();
        for (auto k : keys) {
            auto [cx, cz] = chunkKeyDecode(k);
            // W17/W19: keep both FORCED and SPAWN (level 31) tickets from unloading
            if (w.isForcedKey(k) || w.ticketLevel(cx, cz) <= constants::kTicketLevelSpawn) continue;
            bool near = false;
            for (auto &pl : players) {
                if (!pl->inPlay) continue;
                if (pl->dimension != dim) continue;
                const double chX = cx * 16.0 + 8.0;
                const double chZ = cz * 16.0 + 8.0;
                double dx = pl->x - chX;
                double dz = pl->z - chZ;
                if (std::max(std::abs(dx), std::abs(dz)) < double(unloadDist)) { near = true; break; }
            }
            if (near) continue;
            bool anyInDim = false;
            for (auto &pl : players) if (pl->inPlay && pl->dimension == dim) { anyInDim = true; break; }
            if (!anyInDim && (w.isForced(cx, cz) || w.ticketLevel(cx, cz) <= constants::kTicketLevelSpawn)) continue;
            if (pp && pp->isDirty(cx, cz)) {
                saveChunkAsyncFor(dim, cx, cz);
                pp->markClean(cx, cz);
            }
            toErase.push_back(k);
            invalidateChunkCacheFor(dim, cx, cz);
        }
        // W19 cap-based LRU: if still over maxLoadedChunks, evict farthest beyond cap (Chebyshev)
        if (cfg_.maxLoadedChunks > 0) {
            size_t remaining = keys.size() > toErase.size() ? keys.size() - toErase.size() : 0;
            if (remaining > (size_t)cfg_.maxLoadedChunks) {
                // guard: forced chunks never evicted; if forced >= cap, warn and skip (avoid infinite loop)
                // W17/W19: count both FORCED and SPAWN (level 31) as protected
                size_t forcedCount = 0;
                for (auto k : keys) {
                    auto [cx, cz] = chunkKeyDecode(k);
                    if (w.isForcedKey(k) || w.ticketLevel(cx,cz) <= constants::kTicketLevelSpawn) ++forcedCount;
                }
                if (forcedCount >= (size_t)cfg_.maxLoadedChunks) {
                    std::fprintf(stderr, "[cppfm] maxLoadedChunks %d < forced %zu, skip cap evict\n",
                                 cfg_.maxLoadedChunks, forcedCount);
                } else {
                    std::unordered_set<std::int64_t> already(toErase.begin(), toErase.end());
                    std::vector<std::int64_t> candidates;
                    candidates.reserve(remaining);
                    for (auto k : keys) {
                        if (already.count(k)) continue;
                        auto [cx, cz] = chunkKeyDecode(k);
                        if (w.isForcedKey(k) || w.ticketLevel(cx,cz) <= constants::kTicketLevelSpawn) continue;
                        candidates.push_back(k);
                    }
                    auto distToNearest = [&](std::int32_t cx, std::int32_t cz) -> double {
                        double best = 1e100;
                        for (auto &pl : players) if (pl->inPlay && pl->dimension == dim) {
                            double dx = std::abs((cx*16.0+8.0)-pl->x);
                            double dz = std::abs((cz*16.0+8.0)-pl->z);
                            double d = std::max(dx, dz);
                            if (d < best) best = d;
                        }
                        if (best < 1e90) return best;
                        auto sp = w.spawnPoint();
                        double dx = std::abs((cx*16.0+8.0)-sp.x);
                        double dz = std::abs((cz*16.0+8.0)-sp.z);
                        return std::max(dx, dz);
                    };
                    std::sort(candidates.begin(), candidates.end(), [&](std::int64_t a, std::int64_t b){
                        auto [ax, az] = chunkKeyDecode(a);
                        auto [bx, bz] = chunkKeyDecode(b);
                        return distToNearest(ax,az) > distToNearest(bx,bz);
                    });
                    size_t need = remaining - (size_t)cfg_.maxLoadedChunks;
                    if (need > candidates.size()) need = candidates.size();
                    constexpr size_t kMaxUnloadPerTick = 16;
                    if (need > kMaxUnloadPerTick) need = kMaxUnloadPerTick;
                    for (size_t i=0;i<need;++i) {
                        auto [cx, cz] = chunkKeyDecode(candidates[i]);
                        if (pp && pp->isDirty(cx, cz)) pp->flushChunk(cx, cz);
                        toErase.push_back(candidates[i]);
                        invalidateChunkCacheFor(dim, cx, cz);
                    }
                }
            }
        }
        for (auto k : toErase) {
            auto [cx, cz] = chunkKeyDecode(k);
            if (w.eraseChunk(cx, cz)) {
                std::fprintf(stderr, "[cppfm] unload chunk dim=%d %d,%d (dist>%d) remaining=%zu\n",
                             (int)dim, cx, cz, unloadDist, w.loadedChunkCount());
            }
        }
    };
    doWorld(world_, persist_.get(), 0);
    for (int d = 0; d < 2; ++d) {
        World &w = worldFor(d == 0 ? -1 : 1);
        Persistence *pp = dimPersist_[d] ? dimPersist_[d].get() : nullptr;
        doWorld(w, pp, d == 0 ? -1 : 1);
    }
}
void GameServer::broadcastBlockChange(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
    const std::int8_t dimension = brainTickGuard_ ? brainTickGuard_->dimension : 0;
    broadcastBlockChangeFor(dimension, x, y, z, state);
}
void GameServer::broadcastBlockChangeFor(std::int8_t dimension,
                                          std::int32_t x, std::int32_t y,
                                          std::int32_t z, std::uint16_t state) {
    queueBlockChangeFor(dimension, x, y, z, state);
    invalidateChunkCacheFor(dimension, x >> 4, z >> 4);
}
void GameServer::queueBlockChange(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
    queueBlockChangeFor(0, x, y, z, state);
}
void GameServer::queueBlockChangeFor(std::int8_t dimension,
                                     std::int32_t x, std::int32_t y,
                                     std::int32_t z, std::uint16_t state) {
    WriteBuffer b;
    b.position(x, y, z);
    b.varint(state);
    batcher_.queuePacketFor(dimension, proto::pl::sc::BlockUpdate, std::move(b));
    if (batcher_.size() >= constants::kBlockBatchMaxPackets) {
        flushBlockBatches();
    }
}
void GameServer::flushBlockBatches() {
    if (batcher_.empty()) return;
    int64_t now = nowMs();
    if (batcher_.size() < constants::kBlockBatchMaxPackets && now - lastBlockBatchFlushMs_ < constants::kBlockBatchFlushMs) return;
    batcher_.flush(*this, nullptr);
    lastBlockBatchFlushMs_ = now;
}
void GameServer::survivalTick() {
    for (auto& pp : playersSnapshot()) {
        auto* p = pp.get();
        if (!p->inPlay || !p->spawned || p->dead) continue;
        if (p->attackCooldownTicks < 1000000) p->attackCooldownTicks++;
        if (p->shieldDisableTicks > 0) p->shieldDisableTicks--;
        {
            const bool holdsShield = CombatManager::holdsShield(*p);
            if (!holdsShield) { p->isBlocking = false; p->blockingTicks = 0; }
            else if ((p->isSneaking || p->isBlocking) && p->shieldDisableTicks <= 0) p->blockingTicks++;
            else p->blockingTicks = 0;
        }
        if (p->gamemode != 0) continue;                  // survival only

        // exhaustion -> saturation/food (modular: HungerManager)
        HungerManager::tickExhaustion(*p, *this);
        // natural regeneration / starvation (modular: HungerManager)
        HungerManager::tickRegenAndStarve(*p, tickNo_, *this);
        // void damage
        if (p->y < kMinY - 16) applyDamage(*p, 4.f, "fell out of the world");

        {
            bool hasWaterBreathing = false;
            for (auto &e : p->effects) if (e.type == effects::WaterBreathing) { hasWaterBreathing = true; break; }
            auto isWaterAt = [&](double px, double py, double pz)->bool {
                int bx = (int)std::floor(px);
                int by = (int)std::floor(py);
                int bz = (int)std::floor(pz);
                uint16_t st = worldFor(p->dimension).getBlock(bx,by,bz);
                if (st==0) return false;
                auto *d = gen::blockByState(st);
                return d && d->name == "minecraft:water";
            };
            double headY = p->y + 1.62;
            bool headInWater = isWaterAt(p->x, headY, p->z);
            int respLvl = 0;
            if (p->inv[8].isArmor() || !p->inv[8].empty()) respLvl = EnchantmentHelper::getRespiration(p->inv[8]);
            else {
                // fallback scan 5..8 for any helm with respiration (allows test helmet in any armor slot)
                for (int i=5;i<=8;++i) if(!p->inv[i].empty()) respLvl = std::max(respLvl, EnchantmentHelper::getRespiration(p->inv[i]));
            }
            if (!headInWater) {
                if (p->airTicks != 300) {
                    p->airTicks = 300;
                }
            } else {
                if (!hasWaterBreathing && gamerules_.getBool("drowningDamage")) {
                    // respiration: air decrement interval = 1+respLvl (1,2,3,4)
                    int interval = 1 + respLvl;
                    if (tickNo_ % interval == 0) p->airTicks = std::max(0, p->airTicks - 1);
                    if (p->airTicks <= 0) {
                        int drownInterval = 20 + respLvl * 15; // 20,35,50,65
                        if (tickNo_ % drownInterval == 0) applyDamage(*p, 1.f, "drown");
                    }
                } else {
                    if (p->airTicks < 300) p->airTicks = std::min(300, p->airTicks + 4);
                }
            }
        }
        {
            auto isPowderSnowAt = [&](int bx,int by,int bz)->bool {
                uint16_t st = worldFor(p->dimension).getBlock(bx,by,bz);
                auto *d = gen::blockByState(st);
                return d && d->name == "minecraft:powder_snow";
            };
            auto hasLeatherArmor = [&]()->bool {
                for (int i=5;i<=8;++i) if (!p->inv[i].empty()) {
                    std::string n = p->inv[i].name();
                    if (n.rfind("minecraft:leather_",0)==0) return true;
                    if (n=="minecraft:leather_horse_armor") return true;
                }
                return false;
            };
            int fx = (int)std::floor(p->x);
            int fy = (int)std::floor(p->y);
            int fz = (int)std::floor(p->z);
            bool inSnow = isPowderSnowAt(fx,fy,fz);
            // also check slightly above feet (if player partially inside)
            if (!inSnow) {
                int fy2 = (int)std::floor(p->y + 0.5);
                if (fy2 != fy) inSnow = isPowderSnowAt(fx,fy2,fz);
            }
            bool leatherImmune = hasLeatherArmor();
            if (inSnow && !leatherImmune) {
                p->freezeTicks = std::min(300, p->freezeTicks + 1);
                if (p->freezeTicks >= 140) {
                    if (gamerules_.getBool("freezeDamage") && tickNo_ % 40 == 0) applyDamage(*p, 1.f, "freeze");
                }
            } else {
                p->freezeTicks = std::max(0, p->freezeTicks - 2);
            }
        }
        {
            auto isFireOrLavaAt = [&](double px,double py,double pz)->bool {
                int bx=(int)std::floor(px); int by=(int)std::floor(py); int bz=(int)std::floor(pz);
                uint16_t st = worldFor(p->dimension).getBlock(bx,by,bz);
                auto *d = gen::blockByState(st);
                if (!d) return false;
                if (d->name == "minecraft:lava" || d->name == "minecraft:fire" || d->name == "minecraft:soul_fire" || d->name == "minecraft:magma_block") return true;
                if (d->name == "minecraft:campfire" || d->name == "minecraft:soul_campfire") {
                    for(auto&[k,v]: gen::propsOf(st)) if(k=="lit" && v=="true") return true;
                    return false;
                }
                return false;
            };
            auto isLavaAt = [&](double px,double py,double pz)->bool {
                int bx=(int)std::floor(px); int by=(int)std::floor(py); int bz=(int)std::floor(pz);
                uint16_t st = worldFor(p->dimension).getBlock(bx,by,bz);
                auto *d = gen::blockByState(st);
                return d && d->name == "minecraft:lava";
            };
            bool hasFireRes = false;
            for (auto &e: p->effects) if (e.type == effects::FireResistance) { hasFireRes = true; break; }
            bool doFire = gamerules_.getBool("doFireTick");
            bool inLavaFire = isFireOrLavaAt(p->x, p->y, p->z) || isFireOrLavaAt(p->x, p->y + 1.0, p->z);
            bool inLava = isLavaAt(p->x, p->y, p->z) || isLavaAt(p->x, p->y + 1.0, p->z);
            if (inLavaFire && !hasFireRes) {
                p->fireTicks = inLava ? 300 : 160;
            }
            if (p->fireTicks > 0) {
                if (!hasFireRes && gamerules_.getBool("fireDamage")) {
                    if (tickNo_ % 20 == 0) applyDamage(*p, 1.f, "onFire");
                }
                p->fireTicks--;
                if (!doFire && !inLavaFire) p->fireTicks = 0;
                // extinguish if in water
                {
                    int hx=(int)std::floor(p->x); int hy=(int)std::floor(p->y+1.0); int hz=(int)std::floor(p->z);
                    uint16_t st = worldFor(p->dimension).getBlock(hx,hy,hz);
                    auto *d = gen::blockByState(st);
                    bool inWater = d && d->name=="minecraft:water";
                    if (!inWater) {
                        int fx=(int)std::floor(p->x); int fy=(int)std::floor(p->y); int fz=(int)std::floor(p->z);
                        uint16_t st2 = worldFor(p->dimension).getBlock(fx,fy,fz);
                        auto *d2 = gen::blockByState(st2);
                        inWater = d2 && d2->name=="minecraft:water";
                    }
                    if (inWater) p->fireTicks = 0;
                }
            }
        }
        {
            double half = worldBorderDiameter_ * 0.5;
            double dx = std::abs(p->x - worldBorderCenterX_);
            double dz = std::abs(p->z - worldBorderCenterZ_);
            double furthest = std::max(dx, dz);
            double outside = furthest - half;
            if (outside > 0) {
                double buffer = worldBorderDamageBuffer(); // 5.0
                double perBlock = worldBorderDamagePerBlock(); // 0.2
                double effective = outside - buffer;
                if (effective < 0) effective = 0;
                // vanilla: damage = effective * perBlock per second, but we tick per second ensure at least 1 dmg when outside > buffer and
                // also >0 outside without buffer? Keep buffer logic If effective ==0 but outside>0 then still 0 damage inside buffer zone
                if (effective > 0 && tickNo_ % 20 == 0) {
                    float dmg = static_cast<float>(effective * perBlock);
                    // vanilla also clamps? ensure minimum 1 when just beyond buffer? at least 1 if >0
                    if (dmg < 1.f) dmg = 1.f;
                    // alternative if outside >0 but within buffer, no damage (vanilla buffer grace)
                    applyDamage(*p, dmg, "outside_border");
                }
            }
        }
    }
}
namespace {
enum SpawnGroupIdx{ SG_MONSTER=0, SG_CREATURE=1, SG_AMBIENT=2, SG_WATER_CREATURE=3, SG_WATER_AMBIENT=4, SG_UNDERGROUND=5, SG_AXOLOTLS=6 };
inline SpawnGroupIdx groupForKind(MobKind k){
    if(MobEntity::isHostile(k)) return SG_MONSTER;
    if(k==MobKind::Bat) return SG_AMBIENT;
    if(k==MobKind::Cod||k==MobKind::Salmon||k==MobKind::TropicalFish||k==MobKind::Pufferfish||
       k==MobKind::Squid||k==MobKind::GlowSquid||k==MobKind::Dolphin||k==MobKind::Turtle) return SG_WATER_CREATURE;
    return SG_CREATURE;
}

std::optional<MobKind> mobKindByName(const std::string& name) {
    for (int i = 0; i < 149; ++i) {
        const auto kind = static_cast<MobKind>(i);
        if (name == mobStats(kind).name) return kind;
    }
    return std::nullopt;
}
} // namespace
static std::array<int,7> countMobsByGroup(
    const std::vector<std::shared_ptr<MobEntity>>& mobs,
    std::int8_t dimension) {
    std::array<int,7> c{};
    for (auto& m : mobs) {
        if (!m) continue;
        // mobsSnapshot() owns the shared_ptr lifetime, while this individual
        // lock protects the fields.  Do not hold entsMtx_ while taking it.
        std::lock_guard entityLock(*m->stateMtx);
        if (GameServer::canonicalDimension(m->dimension) == dimension)
            c[(int)groupForKind(m->kind)]++;
    }
    return c;
}
void GameServer::trySpawnMobs() {
    if (!gamerules_.getBool("doMobSpawning")) return;
    // snapshot caps
    std::array<int,7> caps = spawnGroupCaps();
    if (difficulty()=="peaceful") caps[SG_MONSTER]=0;
    const auto dimensionSlot = [](std::int8_t dimension) {
        switch (GameServer::canonicalDimension(dimension)) {
        case -1: return std::size_t{1};
        case 1: return std::size_t{2};
        default: return std::size_t{0};
        }
    };
    std::array<std::array<int,7>,3> counts{};
    const auto activeMobs = mobsSnapshot();
    for (std::size_t slot = 0; slot < counts.size(); ++slot) {
        const auto dimension = slot == 1 ? std::int8_t{-1} :
                               slot == 2 ? std::int8_t{1} :
                                           std::int8_t{0};
        counts[slot] = countMobsByGroup(activeMobs, dimension);
    }
    for (auto& pp : playersSnapshot()) {
        const auto player = snapshotPlayerForTick(pp);
        if (!player.inPlay || !player.spawned || player.dead) continue;
        const auto dimension = player.dimension;
        auto& cnts = counts[dimensionSlot(dimension)];
        World& world = worldFor(dimension);
        LightEngine& light = lightsFor(dimension);
        const auto spawnCandidate = [&](MobKind kind, int group,
                                        float health, std::int32_t spawnX,
                                        std::int32_t groundY,
                                        std::int32_t spawnZ) {
            if (cnts[group] >= caps[group]) return false;
            auto mob = std::make_shared<MobEntity>();
            mob->entityId = nextEntityId();
            mob->dimension = dimension;
            mob->kind = kind;
            mob->health = health;
            mob->x = spawnX + 0.5;
            mob->y = groundY + 1.0;
            mob->z = spawnZ + 0.5;
            mob->lastSeenMs = nowMs();
            if (jvmRuntime_ && !jvmRuntime_->onMobSpawn(
                    *mob, mob->x, mob->y, mob->z))
                return false;
            {
                std::lock_guard lock(entsMtx_);
                if (cnts[group] >= caps[group]) return false;
                mobs_.push_back(mob);
                ++cnts[group];
            }
            broadcastMobSpawn(*mob);
            return true;
        };
        for (int attempt=0; attempt<6; ++attempt) {
            const double ang = (nextRandom()/(double)RAND_MAX)*6.28318;
            const double dist = 24 + (nextRandom()%24);
            const std::int32_t wx = static_cast<std::int32_t>(player.x + std::cos(ang)*dist);
            const std::int32_t wz = static_cast<std::int32_t>(player.z + std::sin(ang)*dist);
            world.generateChunkIfMissing(wx>>4, wz>>4);
            int feet=4; bool ok=false;
            world.withChunk(wx>>4, wz>>4, [&](const Chunk& c){
                for(int ry=kSectionsPerChunk*16-1; ry>=0; --ry) if(c.blocks[Chunk::index(ry>>4, ry&15, wz&15, wx&15)]!=0){ feet=ry+1; ok=true; break; }
            });
            if(!ok) continue;
            const int groundY = kMinY + feet;
            light.ensureSkyLight(wx>>4, wz>>4);
            const uint8_t sky = world.getSkyLight(wx,groundY,wz);
            const uint8_t blk = world.getBlockLight(wx,groundY,wz);
            bool night=isNight(); bool rain=raining() && dimension == 0;
            bool thunder=thundering() && dimension == 0;
            double skyEff = night ? 0.0 : rain ? (thunder? sky*0.2 : sky*0.6) : double(sky);
            double effLight = std::max(double(blk), skyEff);
            // biome gate: sample biome at spawn pos
            std::string biome;
            try { biome = world.sampledBiome(wx, 63, wz); } catch(...){ biome="minecraft:plains"; }
            if(biome.empty()) biome="minecraft:plains";
            // build candidates
            std::vector<const EntityDataDef*> monsterEntries, creatureEntries;
            for(auto& kv : entityDataLoader_.all()){
                const auto& def = kv.second;
                if(!def.biomes.empty()){
                    bool okB=false;
                    for(auto& b: def.biomes){ if(biome.find(b)!=std::string::npos || biome==b){ okB=true; break; } std::string tb=b; auto p=tb.find(':'); if(p!=std::string::npos) tb=tb.substr(p+1); if(biome.find(tb)!=std::string::npos) okB=true; }
                    if(!okB) continue;
                }
                if(def.lightMin>=0 && effLight < def.lightMin) continue;
                if(def.lightMax>=0 && effLight > def.lightMax) continue;
                // resolve kind
                const auto kind = mobKindByName(def.type);
                if (!kind) continue;
                auto g = groupForKind(*kind);
                if(g==SG_MONSTER) monsterEntries.push_back(&def);
                else if(g==SG_CREATURE) creatureEntries.push_back(&def);
            }
            // fallback if no defs loaded for group: use hardcoded
            bool wantHostile = effLight <= 7 && (night || rain || thunder) && difficulty()!="peaceful";
            bool wantCreature = effLight >= 9;
            const std::vector<const EntityDataDef*>* use=nullptr;
            SpawnGroupIdx gIdx=SG_MONSTER;
            if(wantHostile && !monsterEntries.empty()){
                if(cnts[SG_MONSTER] >= caps[SG_MONSTER]) continue;
                use=&monsterEntries; gIdx=SG_MONSTER;
            } else if(wantHostile && monsterEntries.empty()){
                // fallback hardcoded hostile
                if(cnts[SG_MONSTER] >= caps[SG_MONSTER]) continue;
                static const MobKind hostilesTab[]={MobKind::Zombie,MobKind::Zombie,MobKind::Skeleton,MobKind::Creeper,MobKind::Spider};
                MobKind picked = hostilesTab[nextRandom()%5];
                spawnCandidate(picked, SG_MONSTER,
                               mobStats(picked).maxHealth,
                               wx, groundY, wz);
                continue;
            } else if(wantCreature && !creatureEntries.empty()){
                if(cnts[SG_CREATURE] >= caps[SG_CREATURE]) continue;
                use=&creatureEntries; gIdx=SG_CREATURE;
            } else if(wantCreature && creatureEntries.empty()){
                if(cnts[SG_CREATURE] >= caps[SG_CREATURE]) continue;
                static const MobKind passive[]={MobKind::Pig,MobKind::Cow,MobKind::Sheep,MobKind::Chicken,MobKind::Rabbit};
                MobKind picked=passive[nextRandom()%5];
                spawnCandidate(picked, SG_CREATURE,
                               mobStats(picked).maxHealth,
                               wx, groundY, wz);
                continue;
            } else continue;
            if(!use || use->empty()) continue;
            if(cnts[(int)gIdx] >= caps[(int)gIdx]) continue;
            // weighted pick
            int total=0; for(auto* e: *use) total+= std::max(1,e->spawnWeight);
            int r = total>0? nextRandom()%total : 0;
            const EntityDataDef* pickedDef=nullptr;
            for(auto* e: *use){ r-= std::max(1,e->spawnWeight); if(r<0){ pickedDef=e; break; } }
            if(!pickedDef) pickedDef = (*use)[0];
            const auto pickedKind = mobKindByName(pickedDef->type);
            if (!pickedKind) continue;
            const auto pickedGroup = static_cast<int>(groupForKind(*pickedKind));
            const float health = pickedDef->max_health > 0
                ? pickedDef->max_health : mobStats(*pickedKind).maxHealth;
            spawnCandidate(*pickedKind, pickedGroup, health,
                           wx, groundY, wz);
        }
    }
}
void GameServer::spawnSlimeSplit(MobEntity& m) {
    MobKind kind;
    std::int8_t dimension = 0;
    int slimeSize = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    {
        // The caller normally invokes this after releasing the live entity
        // lock.  Keep the helper safe for other callers as well by taking a
        // short, self-contained source snapshot before any callback or
        // container mutation.
        std::lock_guard lock(*m.stateMtx);
        kind = m.kind;
        dimension = canonicalDimension(m.dimension);
        slimeSize = m.slimeSize;
        x = m.x;
        y = m.y;
        z = m.z;
    }
    if ((kind == MobKind::Slime || kind == MobKind::MagmaCube) && slimeSize > 0) {
        int n = 2 + (nextRandom() % 3);
        for (int s = 0; s < n; ++s) {
            auto baby = std::make_shared<MobEntity>();
            baby->entityId = nextEntityId();
            baby->dimension = dimension;
            baby->kind = kind;
            baby->slimeSize = slimeSize - 1;
            baby->health = MobEntity::slimeHealthForSize(baby->slimeSize);
            if (baby->health < 1.f) baby->health = 1.f;
            baby->x = x + (nextRandom()/(double)RAND_MAX - 0.5) * 0.5;
            baby->y = y;
            baby->z = z + (nextRandom()/(double)RAND_MAX - 0.5) * 0.5;
            baby->lastSeenMs = nowMs();
            if (jvmRuntime_ && !jvmRuntime_->onMobSpawn(*baby, baby->x, baby->y, baby->z)) continue;
            addMob(baby);
            broadcastMobSpawn(*baby);
        }
    }
}
void GameServer::mobsTick() {
    struct PendingMove {
        std::int8_t dimension = 0;
        std::shared_ptr<MobEntity> owner;
        WriteBuffer body;
    };
    std::vector<PendingMove> moves;
    struct EntityRemoval {
        std::int8_t dimension = 0;
        std::int32_t entityId = 0;
    };
    struct PendingExplosion {
        std::int8_t dimension = 0;
        double x = 0, y = 0, z = 0;
        float power = 0;
    };
    struct MobDropSnapshot {
        std::int8_t dimension = 0;
        MobKind kind = MobKind::Pig;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        std::array<ItemStack, 6> equipment{};
        std::array<float, 2> handDropChances{};
        std::array<float, 4> armorDropChances{};
    };
    std::vector<EntityRemoval> despawn;
    std::vector<EntityRemoval> deadIds;
    std::vector<MobDropSnapshot> drops;
    std::vector<std::shared_ptr<MobEntity>> removed;
    std::vector<PendingExplosion> explosions;
    std::unordered_set<std::int32_t> removedIds;
    std::unordered_set<const MobEntity*> removedPtrs;
    std::vector<std::int32_t> aiToErase;
    auto queueRemoval = [&](const std::shared_ptr<MobEntity>& mob,
                            bool death) {
        // queueRemoval is called with the current mob's state lock held.
        // Only copy state here; AI-map removal is deliberately deferred until
        // after the entity lock has been released.
        if (!mob || !removedIds.insert(mob->entityId).second) return;
        removedPtrs.insert(mob.get());
        const auto dimension = canonicalDimension(mob->dimension);
        if (death) {
            deadIds.push_back({dimension, mob->entityId});
            MobDropSnapshot drop;
            drop.dimension = dimension;
            drop.kind = mob->kind;
            drop.x = mob->x;
            drop.y = mob->y;
            drop.z = mob->z;
            drop.equipment = mob->equipment;
            drop.handDropChances = mob->handDropChances;
            drop.armorDropChances = mob->armorDropChances;
            drops.push_back(std::move(drop));
        } else {
            despawn.push_back({dimension, mob->entityId});
        }
        aiToErase.push_back(mob->entityId);
        removed.push_back(mob);
    };
    struct MobDimensionGuard {
        GameServer& server;
        const MobEntity* previous;
        MobDimensionGuard(GameServer& serverIn, const MobEntity* mob)
            : server(serverIn), previous(serverIn.brainTickGuard_) {
            server.brainTickGuard_ = mob;
        }
        ~MobDimensionGuard() { server.brainTickGuard_ = previous; }
    };
    std::vector<PlayerTickView> players;
    const auto playerOwners = playersSnapshot();
    players.reserve(playerOwners.size());
    for (const auto& player : playerOwners)
        players.push_back(snapshotPlayerForTick(player));
    const auto activeMobs = mobsSnapshot();
    for (const auto& m : activeMobs) {
            if (!m) continue;
            // aiFor() takes mobAiMtx_ after a short state snapshot.  Resolve it
            // before taking the live entity lock so stateMtx -> mobAiMtx_ is
            // never held by this loop.
            const auto ai = aiFor(m);
            if (!ai) continue;
            std::unique_lock entityStateLock(*m->stateMtx);
            auto withoutEntityStateLock = [&](auto&& operation) {
                entityStateLock.unlock();
                try {
                    operation();
                } catch (...) {
                    entityStateLock.lock();
                    throw;
                }
                entityStateLock.lock();
            };
            MobDimensionGuard dimensionGuard(*this, m.get());
            const auto dimension = canonicalDimension(m->dimension);
            World& world = worldFor(dimension);
            LightEngine& light = lightsFor(dimension);
            bool nearPlayer = false;
            for (const auto& pp : players) {
                if (!pp.inPlay || !pp.spawned || pp.dead || pp.dimension != dimension)
                    continue;
                double dx = pp.x - m->x, dz = pp.z - m->z;
                if (dx*dx + dz*dz < 60*60) { nearPlayer = true; break; }
            }
            if (!nearPlayer) {
                if (MobEntity::isBoss(m->kind) && bossAI_)
                    withoutEntityStateLock([&] { bossAI_->onDeath(*m); });
                queueRemoval(m, false);
                continue;
            }

            const auto& stats = mobStats(m->kind);
            if (m->onFireTicks > 0) {
                if (tickNo_ % 20 == 0)
                    withoutEntityStateLock([&] {
                        applyDamageToMob(*m, 1.f, "onFire");
                    });
                if (--m->onFireTicks <= 0) m->onFireTicks = 0;
                // water extinguishes flame
                {
                    int bx=(int)std::floor(m->x), by=(int)std::floor(m->y), bz=(int)std::floor(m->z);
                    uint16_t st = world.getBlock(bx,by,bz);
                    auto *d = gen::blockByState(st);
                    bool inWater = d && std::string(d->name)=="minecraft:water";
                    if (inWater) m->onFireTicks = 0;
                }
                if (m->dead) {
                    queueRemoval(m, true);
                    continue;
                }
            }
            // dead check (generic, includes combat/arrow etc) with slime split
            if (m->dead) {
                if (MobEntity::isBoss(m->kind) && bossAI_)
                    withoutEntityStateLock([&] { bossAI_->onDeath(*m); });
                // slime / magma cube split
                withoutEntityStateLock([&] { spawnSlimeSplit(*m); });
                queueRemoval(m, true);
                continue;
            }
            // aging: babies grow up
            if (m->age < 0 && ++m->age >= 0) {
                m->age = 0;
                WriteBuffer md;                          // reset baby flag
                md.varint(m->entityId);
                md.u8(16); md.u8(0);                     // index16 byte = adult
                md.u8(0);
                broadcastPacketExceptInDimension(dimension, nullptr,
                                                 pl::sc::SetEntityMetadata, md);
            }
            if (m->inLove && tickNo_ > m->loveUntilTick) m->inLove = false;

            // daylight burn for undead-style hostiles
            if (stats.burnsInDaylight && MobEntity::isHostile(m->kind) &&
                !isNight()) {
                if (tickNo_ % 20 == 0) {
                    withoutEntityStateLock([&] {
                        applyDamageToMob(*m, 1.f, "burned to death");
                    });
                    if (m->dead) {
                        withoutEntityStateLock([&] { spawnSlimeSplit(*m); });
                        queueRemoval(m, true);
                        continue;
                    }
                }
            }

            if (m->kind == MobKind::Creaking && m->creakingTransient) {
                if (!isNight()) {
                    queueRemoval(m, false);
                    continue;
                }
                if (m->hasCreakingHeart) {
                    double dx = m->x - (m->creakingHeartX+0.5), dy = m->y - (m->creakingHeartY+0.5), dz = m->z - (m->creakingHeartZ+0.5);
                    if (dx*dx+dy*dy+dz*dz > 32*32) {
                        m->dead = true;
                    } else {
                        uint16_t hs = world.getBlock(m->creakingHeartX,m->creakingHeartY,m->creakingHeartZ);
                        auto* hd = gen::blockByState(hs);
                        bool heartGone = !hd || std::string(hd->name)!="minecraft:creaking_heart";
                        if (heartGone) {
                            // twitch then death: immediate for now
                            m->dead = true;
                            if (m->dead) {
                                broadcastSoundFor(
                                    dimension, "minecraft:entity.creaking.twitch",
                                    m->x,m->y,m->z,1.f,1.f,"hostile");
                            }
                        }
                    }
                    if (m->dead) {
                        queueRemoval(m, true);
                        continue;
                    }
                    // same-block 5s respawn near heart (vanilla softlock): if within same block as player >5s, respawn near heart
                    for (const auto& pp : players) {
                        if (!pp.inPlay || !pp.spawned || pp.dead || pp.dimension != dimension)
                            continue;
                        int mx=(int)std::floor(m->x), my=(int)std::floor(m->y), mz=(int)std::floor(m->z);
                        int px=(int)std::floor(pp.x), py=(int)std::floor(pp.y), pz=(int)std::floor(pp.z);
                        if (mx==px && my==py && mz==pz) {
                            m->creakingSameBlockTicks++;
                            if (m->creakingSameBlockTicks>100) {
                                // respawn near heart
                                for (int a=0;a<8;++a){
                                    int sx=m->creakingHeartX+(nextRandom()%8-4), sz=m->creakingHeartZ+(nextRandom()%8-4), sy=m->creakingHeartY+1;
                                    if (world.getBlock(sx,sy,sz)==0 && world.getBlock(sx,sy+1,sz)==0 && world.getBlock(sx,sy-1,sz)!=0){
                                        m->x=sx+0.5; m->y=sy; m->z=sz+0.5;
                                        m->creakingSameBlockTicks=0;
                                        WriteBuffer tp; tp.varint(m->entityId); tp.f64(m->x); tp.f64(m->y); tp.f64(m->z); tp.f32(m->yaw); tp.f32(0); tp.boolean(true);
                                        broadcastPacketExceptInDimension(
                                            dimension, nullptr,
                                            proto::pl::sc::EntityTeleport, tp);
                                        broadcastSyncEntityPosition(*m, nullptr);
                                        break;
                                    }
                                }
                            }
                        } else {
                            m->creakingSameBlockTicks=0;
                        }
                    }
                }
            }

            ai->ctx->srv = this;
            ai->ctx->world = &world;
            auto previousBehaviorTreeLock =
                setBehaviorTreeMobStateLock(m.get(), &entityStateLock);
            try {
                ai->brain->tick(*m, *ai->ctx, tickNo_);
            } catch (...) {
                setBehaviorTreeMobStateLock(previousBehaviorTreeLock.mob,
                                             previousBehaviorTreeLock.lock);
                throw;
            }
            setBehaviorTreeMobStateLock(previousBehaviorTreeLock.mob,
                                         previousBehaviorTreeLock.lock);
            if (MobEntity::isBoss(m->kind) && bossAI_)
                withoutEntityStateLock([&] {
                    bossAI_->tick(*m, *ai->ctx, tickNo_);
                });

            const PlayerTickView* nearestPlayer = nullptr;
            for (const auto& player : players) {
                if (player.owner.get() == ai->ctx->nearestPlayer) {
                    nearestPlayer = &player;
                    break;
                }
            }
            if (m->kind == MobKind::Creeper && nearestPlayer) {
                const double cdx = nearestPlayer->x - m->x;
                const double cdy = nearestPlayer->y - m->y;
                const double cdz2 = nearestPlayer->z - m->z;
                const double cd2 = cdx*cdx + cdy*cdy + cdz2*cdz2;
                if (cd2 < 9) {
                    if (!m->creeperIgnited) {
                        m->creeperIgnited = true;
                        m->creeperFuseStart = tickNo_;
                        // SetEntityMetadata ignited flag (index 16, Yarn CreeperEntity IGNITED Boolean)
                        WriteBuffer md;
                        md.varint(m->entityId);
                        meta::writeMetaBool(md, 16, true);
                        md.u8(255);
                        broadcastPacketExceptInDimension(
                            dimension, nullptr, pl::sc::SetEntityMetadata, md);
                        broadcastSoundFor(dimension,
                                          "minecraft:entity.creeper.primed",
                                          m->x, m->y, m->z, 1.f, 1.f,
                                          "hostile");
                        broadcastEntitySoundFor(
                            dimension, m->entityId,
                            "minecraft:entity.creeper.primed", 1.f, 1.f,
                            SoundSource::Hostile);
                    } else if (tickNo_ - m->creeperFuseStart >= MobEntity::CREEPER_FUSE_TICKS) {
                        const double cxp = m->x, cyp = m->y, czp = m->z;
                        const std::int32_t eid = m->entityId;
                        const bool charged = m->creeperCharged;
                        WriteBuffer rm; rm.varint(1); rm.varint(eid);
                        broadcastPacketExceptInDimension(
                            dimension, nullptr, pl::sc::RemoveEntities, rm);
                        queueRemoval(m, false);
                        explosions.push_back({dimension, cxp, cyp + 0.5, czp,
                                              charged ? 6.f : 3.f});
                        continue;
                    }
                } else if (m->creeperIgnited && cd2 > 16) {
                    m->creeperIgnited = false;
                    m->creeperFuseStart = -1;
                    WriteBuffer md;
                    md.varint(m->entityId);
                    meta::writeMetaBool(md, 16, false);
                    md.u8(255);
                    broadcastPacketExceptInDimension(
                        dimension, nullptr, pl::sc::SetEntityMetadata, md);
                }
            }

            // ---- light-aware daylight burn (real skylight at mob feet)
            if (stats.burnsInDaylight && MobEntity::isHostile(m->kind) &&
                !isNight() && tickNo_ % 20 == 0) {
                world.generateChunkIfMissing(
                    static_cast<std::int32_t>(m->x) >> 4,
                    static_cast<std::int32_t>(m->z) >> 4);
                light.ensureSkyLight(
                    static_cast<std::int32_t>(m->x) >> 4,
                    static_cast<std::int32_t>(m->z) >> 4);
                const std::uint8_t sky =
                    world.getSkyLight(static_cast<std::int32_t>(m->x),
                                       static_cast<std::int32_t>(m->y),
                                       static_cast<std::int32_t>(m->z));
                if (sky >= 14)
                    withoutEntityStateLock([&] {
                        applyDamageToMob(*m, 1.f, "burned to death");
                    });
                if (m->dead) {
                    withoutEntityStateLock([&] { spawnSlimeSplit(*m); });
                    queueRemoval(m, true);
                    continue;
                }
            }
            if (m->kind==MobKind::Villager) {
                // day rollover for 2/day limit (vanilla: 2 restocks per in-game day)
                std::int64_t curDay = tickNo_ / 24000;
                if (curDay != m->villagerLastRestockDay) {
                    m->villagerRestocksToday = 0;
                    m->villagerLastRestockDay = curDay;
                }
                if (tickNo_ >= m->restockUntil && m->restockUntil!=0) {
                    if (m->villagerRestocksToday < 2) {
                        m->villagerRestocksToday++;
                        m->villagerLastRestockTick = tickNo_;
                        broadcastSoundFor(dimension,
                                          "minecraft:entity.villager.work_farm",
                                          m->x,m->y,m->z,1.f,1.f,"neutral");
                        if (m->villagerRestocksToday < 2) {
                            m->restockUntil = tickNo_ + MobEntity::kRestockSecondWindowTicks
                                + (nextRandom() % 2000);
                        } else {
                            m->restockUntil = 0;
                        }
                    } else {
                        m->restockUntil = 0;
                    }
                }
                if (tickNo_%100==0) m->gossip.tickDecay();
            }
            // Enderman: occasional random block pickup via BehaviorTree is primary, but ensure carriedBlock persistence (handled in
            // PickupBlockAction) delta broadcast
            if (!m->hasSent ||
                std::abs(m->x-m->sentX)+std::abs(m->y-m->sentY)+std::abs(m->z-m->sentZ) > 0.03) {
                WriteBuffer b;
                b.varint(m->entityId);
                b.i16((std::int16_t)((m->x-m->sentX) * 4096));
                b.i16((std::int16_t)((m->y-m->sentY) * 4096));
                b.i16((std::int16_t)((m->z-m->sentZ) * 4096));
                b.i8((std::int8_t)(m->yaw * constants::kAngleScaleNum / constants::kAngleScaleDen));
                b.i8(0);
                b.boolean(true);
                moves.push_back({dimension, m, std::move(b)});
                m->sentX=m->x; m->sentY=m->y; m->sentZ=m->z; m->hasSent=true;
            }
    }
    {
        std::lock_guard lk(entsMtx_);
        mobs_.erase(std::remove_if(mobs_.begin(), mobs_.end(),
                                   [&](const auto& mob) {
                                       return mob && removedPtrs.count(mob.get()) != 0;
                                   }),
                    mobs_.end());
    }
    for (const auto entityId : aiToErase)
        eraseMobAi(entityId);
    // Native/JVM callbacks above run without entsMtx_.  Remove the entities
    // selected during this snapshot before emitting their final packets and
    // drops; newly spawned entities remain eligible for the next tick.
    for (const auto& explosion : explosions)
        explodeAtFor(explosion.dimension, explosion.x, explosion.y,
                     explosion.z, explosion.power);
    for (const auto& mob : removed) invalidateJvmMob(mob);
    for (const auto& removal : despawn) {
        WriteBuffer b;
        b.varint(1); b.varint(removal.entityId);
        broadcastPacketExceptInDimension(removal.dimension, nullptr,
                                         pl::sc::RemoveEntities, b);
    }
    for (const auto& m : drops) {
        bool spawnedViaLoot = false;
        {
            std::string kindName = MobEntity::kindName(m.kind);
            std::string base = kindName.find(':')!=std::string::npos ? kindName.substr(kindName.find(':')+1) : kindName;
            std::string tblId = "minecraft:entities/" + base;
            if (lootTables_.find(tblId)) {
                LootContext ctx;
                // try to get looting from killer's tool if available (fallback 0)
                ctx.lootingLevel = 0;
                ctx.fortuneLevel = 0;
                auto loot = lootTables_.evaluateEntity(kindName, &ctx);
                for (auto& st : loot) {
                    if (st.empty()) continue;
                    spawnItemDropFor(m.dimension, m.x, m.y + 0.4, m.z, st,
                                     (nextRandom()/(double)RAND_MAX-.5)*.15,
                                     .1, (nextRandom()/(double)RAND_MAX-.5)*.15);
                    spawnedViaLoot = true;
                }
            }
        }
        if (!spawnedViaLoot) {
            const auto drop = MobEntity::dropFor(m.kind);
            if (drop.itemId)
                spawnItemDropFor(m.dimension, m.x, m.y + 0.4, m.z,
                                 drop.itemId, drop.count,
                                 (nextRandom()/(double)RAND_MAX-.5)*.15, .1,
                                 (nextRandom()/(double)RAND_MAX-.5)*.15);
        }
        for (int es=0; es<6; ++es) {
            if (m.equipment[es].empty()) continue;
            float chance = 0.085f;
            if (es==0) chance = m.handDropChances[0];
            else if (es==1) chance = m.handDropChances[1];
            else if (es>=2 && es<=5) chance = m.armorDropChances[es-2];
            float r = float(nextRandom())/float(RAND_MAX);
            if (r < chance) {
                spawnItemDropFor(m.dimension, m.x, m.y+0.4, m.z,
                                 m.equipment[es],
                                 (nextRandom()/(double)RAND_MAX-.5)*.12,
                                 0.18, (nextRandom()/(double)RAND_MAX-.5)*.12);
            }
        }
        // XP orbs on kill
        spawnXpOrbsFor(m.dimension, m.x, m.y + 0.5, m.z,
                       mobStats(m.kind).xpDrop, nullptr);
    }
    for (const auto& removal : deadIds) {
        WriteBuffer rm;
        rm.varint(1); rm.varint(removal.entityId);
        broadcastPacketExceptInDimension(removal.dimension, nullptr,
                                         pl::sc::RemoveEntities, rm);
    }
    for (const auto& move : moves) {
        broadcastPacketExceptInDimension(move.dimension, nullptr,
                                         pl::sc::MoveEntityPosRot, move.body);
    }
}
} // namespace cppfm
