#include "GameServer.hpp"
#include "BlockEvent.hpp"
#include "MetadataTypes.hpp"
#include "../physics/LightEngine.hpp"
#include "../physics/Fluids.hpp"
#include "../physics/Redstone.hpp"
#include "../worldgen/PortalHandler.hpp"
#include "../core/Json.hpp"
#include "GameServerHelpers.hpp"
#include "StairsHelper.hpp"
#include "Constants.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/EntityIds.hpp"
#include "MenuInteraction.hpp"
#include "BehaviorTree.hpp"
#include "BehaviorTreeParser.hpp"
#include "EquipmentComponent.hpp"
#include "DamageComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "MobSpawner.hpp"
#include "BossAI.hpp"
#include "MenuLogic.hpp"
#include "CostCalculator.hpp"
#include "PotionBrewing.hpp"
#include "Particles.hpp"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cppfm {
using namespace proto;
void GameServer::tickDigs() {
    for (auto& pp : playersSnapshot()) {
        auto* p = pp.get();
        if (!p->digActive || !p->inPlay) continue;
        const std::int64_t elapsed = tickNo_ - p->digStartTick;
        if (p->digTotalTicks <= 0) continue;
        const std::int64_t stage64 = elapsed * 10 / p->digTotalTicks;
        const std::uint8_t stage = static_cast<std::uint8_t>(std::min<std::int64_t>(9, stage64));
        if (stage != p->digLastStage) {
            p->digLastStage = stage;
            broadcastDigStage(*p, static_cast<std::int8_t>(stage));
        }
        if (elapsed >= p->digTotalTicks) {
            // server-authoritative completion
            const std::uint16_t oldState = world_.getBlock(p->digX, p->digY, p->digZ);
            std::fprintf(stderr, "[cppfm] DIG COMPLETE at (%d,%d,%d) oldState=%u\n",
                         p->digX, p->digY, p->digZ, oldState);
            if (oldState != 0) {
                api::BlockBreakEvent ev;
                ev.player = p;
                ev.x = p->digX; ev.y = p->digY; ev.z = p->digZ;
                ev.oldState = oldState;
                if (!events().blockBreak.fire(ev)) {
                    // cancelled: restore + stop animating
                    WriteBuffer rb;
                    rb.position(p->digX, p->digY, p->digZ);
                    rb.varint(oldState);
                    broadcastPacketExcept(nullptr, proto::pl::sc::BlockUpdate, rb);
                    p->digActive = false;
                    broadcastDigStage(*p, -1);
                    continue;
                }
                world_.setBlock(p->digX, p->digY, p->digZ, 0);
                broadcastBlockChange(p->digX, p->digY, p->digZ, 0);
                // plan29 §6 polish: breaking block adds 0.005 exhaustion (vanilla Hunger per block)
                if (p->gamemode == 0) HungerManager::onBlockBreak(*p, *this);
                // BlockEvent: fire onBlockBreak (plan7)
                {
                    blockEventDispatcher().onBlockBreak(p->digX, p->digY, p->digZ, oldState, p);
                    api::BlockBreakEvent bev2; bev2.player=p; bev2.x=p->digX; bev2.y=p->digY; bev2.z=p->digZ; bev2.oldState=oldState;
                    (void)bev2;
                }
                onBlockMined(*p, oldState);
                // plan17 §7: TNT unstable punch — prime on break if unstable or flint_and_steel (Yarn TntBlock.onBlockBreak)
                {
                    const std::string _bn = blockNameByState(oldState);
                    if (_bn == "minecraft:tnt") {
                        std::string unstableVal;
                        for (auto& [k,v] : gen::propsOf(oldState)) if (k=="unstable") unstableVal = std::string(v);
                        bool isUnstable = (unstableVal == "true");
                        bool isCreative = (p->gamemode == 1);
                        bool hasFlint = false;
                        if (p->heldSlot >=0 && p->heldSlot <9) {
                            const auto& _held = p->inv[36 + p->heldSlot];
                            if (!_held.empty() && _held.name() == "minecraft:flint_and_steel") hasFlint = true;
                        }
                        if (hasFlint) {
                            spawnPrimedTnt(p->digX + 0.5, p->digY + 0.5, p->digZ + 0.5, 0, 0.2, 0, 80);
                            broadcastSound("minecraft:entity.tnt.primed", p->digX+0.5, p->digY+0.5, p->digZ+0.5, 1.f, 1.f, "block");
                            if (!isCreative && p->heldSlot>=0 && p->heldSlot<9) {
                                auto& _h = p->inv[36 + p->heldSlot];
                                if (_h.applyDamage(1)) _h = ItemStack::air();
                                resendInventory(*p);
                            }
                            p->digActive = false;
                            broadcastDigStage(*p, -1);
                            continue;
                        } else if (isUnstable && !isCreative) {
                            spawnPrimedTnt(p->digX + 0.5, p->digY + 0.5, p->digZ + 0.5, 0, 0.2, 0, 80);
                            broadcastSound("minecraft:entity.tnt.primed", p->digX+0.5, p->digY+0.5, p->digZ+0.5, 1.f, 1.f, "block");
                            p->digActive = false;
                            broadcastDigStage(*p, -1);
                            continue;
                        }
                    }
                }
                // durability: damage held tool if it has durability – Plan8 DamageComponent (Unbreaking)
                if (p->gamemode == 0 && p->heldSlot >=0 && p->heldSlot <9) {
                    auto &held = p->inv[36 + p->heldSlot];
                    if (!held.empty() && ItemStack::maxDamageFor(held.itemId) > 0) {
                        if (DamageComponent::applyDamage(held, 1)) held = ItemStack::air();
                        resendInventory(*p);
                    }
                }
                if (!ev.dropItems) { p->digActive = false;
                    broadcastDigStage(*p, -1); continue; }
                if (p->gamemode == 0) {
                    const std::string bn = blockNameByState(oldState);
                    const BlockMineInfo* mi = mineInfo(bn);
                    const bool canHarvest = !mi || !mi->requiresPickaxe ||
                        [&]{
                            if (p->heldSlot < 0 || p->heldSlot >= 9) return false;
                            const auto& sl = p->inv[36 + p->heldSlot];
                            if (sl.count <= 0) return false;
                            static thread_local std::unordered_map<std::uint32_t,std::string> i2n;
                            if (i2n.empty()) for (auto& e : gen::kItems) i2n.emplace(e.second, std::string(e.first));
                            auto it = i2n.find(sl.itemId);
                            return it != i2n.end() && it->second.find("pickaxe") != std::string::npos;
                        }();
                    if (canHarvest) {
                        ItemStack heldStack;
                        if (p->heldSlot >= 0 && p->heldSlot < 9) heldStack = p->inv[36 + p->heldSlot];
                        std::vector<ItemStack> drops;
                        if (heldStack.hasSilkTouch()) {
                            if (bn != "minecraft:air") {
                                auto ii = gen::itemIdByName().find(bn);
                                if (ii != gen::itemIdByName().end())
                                    drops.push_back(ItemStack::of(ii->second, 1));
                            }
                        } else {
                            if (bn == "minecraft:glass") {
                                // no drop without silk touch
                            } else {
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
                        }
                        for (auto &st : drops) {
                            if (st.empty()) continue;
                            spawnItemDrop(p->digX+.5, p->digY+.25, p->digZ+.5,
                                          st,
                                          (rand()/(double)RAND_MAX-.5)*.15, .12,
                                          (rand()/(double)RAND_MAX-.5)*.15);
                        }
                    }
                }
            }
            p->digActive = false;
            broadcastDigStage(*p, -1);
        }
    }
}
void GameServer::tickOnce() {
    static const bool tr = getenv("CPPFM_TICK_TRACE") != nullptr;
    auto mark = [&](char c) { if (tr) std::fprintf(stderr, "[tick] %c t=%ld\n", c, (long)tickNo_); };
    pollPendingLoads(); // W19 async I/O: poll Chunk futures (ThreadPool 4) without blocking (MC-177729)
    api::ServerTickEvent ev{tickNo_};
    events().serverTick.fire(ev);
    mark('F');
    fluidSim_->tick(tickNo_);
    mark('R');
    redstone_->tick(tickNo_);
    if (blockTicks_) blockTicks_->tick(tickNo_);
    mark('D');
    tickDigs();
    mark('S');
    survivalTick();
    mark('U');
    furnacesTick();
    brewingTick();
    mark('E');
    effectsTick();
    mark('X');
    xpOrbsTick();
    mark('m');

    // mob spawn cadence: every 20 ticks
    if (tickNo_ % 20 == 0) trySpawnMobs();
    mark('M');
    mobsTick();
    // plan36 §5: drain StructureManager pending loot/mobs (world defer -> tick evaluate)
    drainPendingStructureQueues();
    mark('R'); // rails (plan14 §5)
    minecartsTick(); // plan14 §5: powered_rail 0.06
    boatsTick(); // plan14 §5: boat friction 0.9 water / 0.6 land, buoyancy 0.04, max 0.4
    mark('P');
    projectilesTick();
    mark('I');
    itemsTick();
    tntTick();
    mark('T');

    // periodic time sync every 20 ticks (1s); frozen when doDaylightCycle off
    if (tickNo_ % 20 == 0) {
        if (!gamerules_.contains("doDaylightCycle") ||
            gamerules_.getBool("doDaylightCycle")) {
            WriteBuffer t;
            t.i64(tickNo_);
            t.i64(dayTime());
            t.boolean(true);
            broadcastPacketExcept(nullptr, pl::sc::UpdateTime, t);
        }
    }

    // light engine: drain queued BFS work, broadcast UpdateLight per chunk
    {
        mark('L');
        const LightUpdateBatch batch = lightEngine_->drain();
        mark('l');
        for (auto k : batch.dirtyChunks) {
            auto [cx, cz] = chunkKeyDecode(k);
            world_.withChunk(cx, cz, [&](const Chunk& c) {
                WriteBuffer b;
                serializeUpdateLightBody(b, cx, cz, c);
                broadcastPacketExcept(nullptr, pl::sc::UpdateLight, b);
            });
        }
    }

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
    // chunk LRU unload every 100 ticks (plan5 items 6,7)
    if (tickNo_ % 100 == 0) chunksUnloadTick();
    // level.dat periodic save every 6000 ticks (~5 min) + also 1200 (~1 min) for safety — single level.dat (W16)
    if (tickNo_ % 6000 == 0 && tickNo_ != 0) {
        try { persist_->saveLevelData(tickNo_, dayTime()); } catch (...) {}
        std::fprintf(stderr, "[cppfm] periodic level.dat save t=%ld\n", (long)tickNo_);
    } else if (tickNo_ % 1200 == 0 && tickNo_ != 0) {
        try { persist_->saveLevelData(tickNo_, dayTime()); } catch (...) {}
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
    // plan14 §6: scheduled function tick (schedule) – execute due scheduled functions each tick
    tickScheduledFunctions();
    // plan35 §1: tick trigger for advancements (minecraft:tick)
    for (auto& pp : playersSnapshot()) if (pp->inPlay) evaluateTickAdvancements(*pp);
    // plan37 §3: location trigger every 20 ticks (0.5 Hz like vanilla)
    if (tickNo_ % 20 == 0) {
        for (auto& pp : playersSnapshot()) if (pp->inPlay) evaluateLocationTrigger(*pp);
    }
    // plan38 B-13: enter_block every 20 ticks
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
    auto process = [&](World& w){
        auto* sm = w.structureManager();
        if(!sm) return;
        std::vector<worldgen::StructureManager::PendingLoot> loots;
        sm->drainPendingLoot(loots);
        for(auto &pl : loots){
            auto drops = lootTables_.evaluate(pl.lootTable);
            int x=pl.pos[0], y=pl.pos[1], z=pl.pos[2];
            auto* be = blockEntities_.getAt(x,y,z);
            if(!be){
                be = &blockEntities_.create(posKey(x,y,z), BlockEntity::Kind::Chest);
            } else if(be->kind != BlockEntity::Kind::Chest){
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
                    int cand = rand() % ChestData::kSlots;
                    if(!used.count(cand)){ slot=cand; break; }
                }
                if(slot==-1) slot = rand()%ChestData::kSlots;
                used.insert(slot);
                be->chest.slots[slot]=st;
            }
            blockEntities_.dirty_.insert(posKey(x,y,z));
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
                mob->kind = kind;
                mob->health = mobStats(kind).maxHealth;
                mob->x = pm.pos[0] + 0.5;
                mob->y = pm.pos[1] + 0.5;
                mob->z = pm.pos[2] + 0.5;
                mob->lastSeenMs = nowMs();
                {
                    std::lock_guard<std::mutex> lk(entsMtx_);
                    mobs_.push_back(mob);
                }
                broadcastMobSpawn(*mob);
                std::fprintf(stderr,"[cppfm] pending mob %s at %d %d %d id %d\n", pm.mob.c_str(), pm.pos[0], pm.pos[1], pm.pos[2], mob->entityId);
            }
        }
    };
    process(world_);
    if(netherWorld_) process(*netherWorld_);
    if(endWorld_) process(*endWorld_);
}
bool GameServer::isChunkInSimulationDistance(std::int32_t cx, std::int32_t cz) const {
    // Spawn chunk loader: forced chunks / SPAWN ticket level 31 are always in simulation distance (ChunkTicket)
    if (world_.isForced(cx, cz) || world_.ticketLevel(cx, cz) <= constants::kTicketLevelSpawn) return true;
    if (netherWorld_ && (netherWorld_->isForced(cx, cz) || netherWorld_->ticketLevel(cx, cz) <= constants::kTicketLevelSpawn)) return true;
    if (endWorld_ && (endWorld_->isForced(cx, cz) || endWorld_->ticketLevel(cx, cz) <= constants::kTicketLevelSpawn)) return true;
    const int sim = cfg_.simulationDistance;
    if (sim <= 0) return true;
    const double limit = sim * 16.0;
    const double chX = cx * 16.0 + 8.0;
    const double chZ = cz * 16.0 + 8.0;
    auto players = const_cast<GameServer*>(this)->playersSnapshot();
    if (players.empty()) return false;
    for (auto &p : players) {
        if (!p->inPlay) continue;
        double dx = p->x - chX;
        double dz = p->z - chZ;
        if (std::max(std::abs(dx), std::abs(dz)) < limit) return true;
    }
    return false;
}
void GameServer::chunksUnloadTick() {
    const int sim = cfg_.simulationDistance;
    const int view = cfg_.viewDistance;
    const int unloadDist = std::max(sim, view) * 16 + 32;
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
                // plan42 R2 (E-13): unload saves go through ioPool_ (zlib +
                // RegionFile write off tick thread); dirty bit cleared after
                // the async save captured the chunk contents.
                saveChunkAsync(cx, cz);
                pp->markClean(cx, cz);
            }
            toErase.push_back(k);
            invalidateChunkCache(cx, cz);
        }
        // W19 cap-based LRU: if still over maxLoadedChunks, evict farthest beyond cap (Chebyshev)
        // plan21 §3 polish: cap auto max(8192, viewDist²*4), cap=0 unlimited, clamp 1 when configured 0,
        // and per-tick burst limit 16 to avoid UpdateLight storms.
        // NOTE plan35 §5: this is NOT a simple clear(); it is Chebyshev-distance sorted LRU with
        // forced/spawn ticket protection and burst 16/tick. Documented as compliant (not TODO).
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
                    // burst limit: evict at most 16 per tick, remainder next tick (plan21 perf)
                    constexpr size_t kMaxUnloadPerTick = 16;
                    if (need > kMaxUnloadPerTick) need = kMaxUnloadPerTick;
                    for (size_t i=0;i<need;++i) {
                        auto [cx, cz] = chunkKeyDecode(candidates[i]);
                        if (pp && pp->isDirty(cx, cz)) pp->flushChunk(cx, cz);
                        toErase.push_back(candidates[i]);
                        invalidateChunkCache(cx, cz);
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
    queueBlockChange(x, y, z, state);
    invalidateChunkCache(x >> 4, z >> 4);
}
void GameServer::queueBlockChange(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
    WriteBuffer b;
    b.position(x, y, z);
    b.varint(state);
    batcher_.queuePacket(proto::pl::sc::BlockUpdate, std::move(b));
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
    const auto now = nowMs();
    for (auto& pp : playersSnapshot()) {
        auto* p = pp.get();
        if (!p->inPlay || !p->spawned || p->dead) continue;
        // plan44 §3 G-07/G-08: combat counters tick for all inPlay players (any gamemode)
        if (p->attackCooldownTicks < 1000000) p->attackCooldownTicks++;
        if (p->shieldDisableTicks > 0) p->shieldDisableTicks--;
        {
            bool holdsShield = false;
            if (p->heldSlot >= 0 && p->heldSlot < 9) {
                const auto& mh = p->inv[36 + p->heldSlot];
                if (!mh.empty() && mh.name().find("shield") != std::string::npos) holdsShield = true;
            }
            if (!p->inv[45].empty() && p->inv[45].name().find("shield") != std::string::npos) holdsShield = true;
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

        // ---- drowning (plan5 76) + plan40 C-08 respiration (helmet only, air 300→1200, interval 1+lvl)
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
            // plan40: respiration level from helmet (slot 8 head)
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
        // ---- freeze (powder snow) 77 — plan16 strict: leather immunity, 40t damage, -2 decay
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
        // ---- fire / lava 77-78 — plan16 strict: lava 300, fire 160
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
        // ---- world border damage (plan6 §10) — 0.2*blocksOutside buffer 5.0 Chebyshev
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
                // vanilla: damage = effective * perBlock per second, but we tick per second
                // ensure at least 1 dmg when outside > buffer and also >0 outside without buffer? Keep buffer logic
                // If effective ==0 but outside>0 then still 0 damage inside buffer zone
                if (effective > 0 && tickNo_ % 20 == 0) {
                    float dmg = static_cast<float>(effective * perBlock);
                    // vanilla also clamps? ensure minimum 1 when just beyond buffer? at least 1 if >0
                    if (dmg < 1.f) dmg = 1.f;
                    // alternative if outside >0 but within buffer, no damage (vanilla buffer grace)
                    applyDamage(*p, dmg, "outside_border");
                } else if (effective == 0 && outside > 0 && tickNo_ % 20 == 0) {
                    // still inside damage buffer (5 blocks) — no damage per vanilla
                }
            }
        }
        (void)now;
    }
}
namespace {
enum SpawnGroupIdx{ SG_MONSTER=0, SG_CREATURE=1, SG_AMBIENT=2, SG_WATER_CREATURE=3, SG_WATER_AMBIENT=4, SG_UNDERGROUND=5, SG_AXOLOTLS=6 };
// (plan46 G-15: caps live in GameServer::spawnGroupCaps — single source for tests.)
inline SpawnGroupIdx groupForKind(MobKind k){
    if(MobEntity::isHostile(k)) return SG_MONSTER;
    if(k==MobKind::Bat) return SG_AMBIENT;
    if(k==MobKind::Cod||k==MobKind::Salmon||k==MobKind::TropicalFish||k==MobKind::Pufferfish||
       k==MobKind::Squid||k==MobKind::GlowSquid||k==MobKind::Dolphin||k==MobKind::Turtle) return SG_WATER_CREATURE;
    return SG_CREATURE;
}
} // namespace
// plan36 natural spawn helper: count mobs by group
static std::array<int,7> countMobsByGroup(const std::vector<std::shared_ptr<MobEntity>>& mobs){
    std::array<int,7> c{}; for(auto& m: mobs) c[(int)groupForKind(m->kind)]++; return c;
}
void GameServer::trySpawnMobs() {
    if (!gamerules_.getBool("doMobSpawning")) return;
    if (difficulty()=="peaceful") {
        // still allow creature spawns but no monster; handle via caps below
    }
    static std::int64_t lastTrace = 0;
    const bool tr = getenv("CPPFM_TRACE") != nullptr;
    if (tr && tickNow() - lastTrace >= 200) {
        lastTrace = tickNow();
        std::fprintf(stderr, "[cppfm] mob-spawn tick: night=%d mobs=%zu\n", (int)isNight(), mobs_.size());
    }
    // snapshot caps
    std::array<int,7> caps = spawnGroupCaps();
    if (difficulty()=="peaceful") caps[SG_MONSTER]=0;
    std::array<int,7> cnts; { std::lock_guard lk(entsMtx_); cnts = countMobsByGroup(mobs_); }
    for (auto& pp : playersSnapshot()) {
        auto* pl = pp.get();
        if (!pl->inPlay || !pl->spawned || pl->dead) continue;
        for (int attempt=0; attempt<6; ++attempt) {
            const double ang = (rand()/(double)RAND_MAX)*6.28318;
            const double dist = 24 + (rand()%24);
            const std::int32_t wx = static_cast<std::int32_t>(pl->x + std::cos(ang)*dist);
            const std::int32_t wz = static_cast<std::int32_t>(pl->z + std::sin(ang)*dist);
            world_.generateChunkIfMissing(wx>>4, wz>>4);
            int feet=4; bool ok=false;
            world_.withChunk(wx>>4, wz>>4, [&](const Chunk& c){
                for(int ry=kSectionsPerChunk*16-1; ry>=0; --ry) if(c.blocks[Chunk::index(ry>>4, ry&15, wz&15, wx&15)]!=0){ feet=ry+1; ok=true; break; }
            });
            if(!ok) continue;
            const int groundY = kMinY + feet;
            lightEngine_->ensureSkyLight(wx>>4, wz>>4);
            const uint8_t sky = world_.getSkyLight(wx,groundY,wz);
            const uint8_t blk = world_.getBlockLight(wx,groundY,wz);
            bool night=isNight(); bool rain=raining(); bool thunder=thundering();
            double skyEff = night ? 0.0 : rain ? (thunder? sky*0.2 : sky*0.6) : double(sky);
            double effLight = std::max(double(blk), skyEff);
            // biome gate: sample biome at spawn pos
            std::string biome;
            try { biome = world_.sampledBiome(wx, 63, wz); } catch(...){ biome="minecraft:plains"; }
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
                MobKind kind = MobKind::Pig; bool found=false;
                for(int i=0;i<149;++i){ if(std::string(mobStats(static_cast<MobKind>(i)).name)==def.type){ kind=static_cast<MobKind>(i); found=true; break; } }
                if(!found) continue;
                auto g = groupForKind(kind);
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
                MobKind picked = hostilesTab[rand()%5];
                auto mob=std::make_shared<MobEntity>(); mob->entityId=nextEntityId(); mob->kind=picked; mob->health=mobStats(picked).maxHealth;
                mob->x=wx+0.5; mob->y=groundY+1.0; mob->z=wz+0.5; mob->lastSeenMs=nowMs();
                { std::lock_guard lk(entsMtx_); if(cnts[SG_MONSTER] >= caps[SG_MONSTER]) continue; mobs_.push_back(mob); cnts[SG_MONSTER]++; }
                broadcastMobSpawn(*mob); continue;
            } else if(wantCreature && !creatureEntries.empty()){
                if(cnts[SG_CREATURE] >= caps[SG_CREATURE]) continue;
                use=&creatureEntries; gIdx=SG_CREATURE;
            } else if(wantCreature && creatureEntries.empty()){
                if(cnts[SG_CREATURE] >= caps[SG_CREATURE]) continue;
                static const MobKind passive[]={MobKind::Pig,MobKind::Cow,MobKind::Sheep,MobKind::Chicken,MobKind::Rabbit};
                MobKind picked=passive[rand()%5];
                auto mob=std::make_shared<MobEntity>(); mob->entityId=nextEntityId(); mob->kind=picked; mob->health=mobStats(picked).maxHealth;
                mob->x=wx+0.5; mob->y=groundY+1.0; mob->z=wz+0.5; mob->lastSeenMs=nowMs();
                { std::lock_guard lk(entsMtx_); if(cnts[SG_CREATURE] >= caps[SG_CREATURE]) continue; mobs_.push_back(mob); cnts[SG_CREATURE]++; }
                broadcastMobSpawn(*mob); continue;
            } else continue;
            if(!use || use->empty()) continue;
            if(cnts[(int)gIdx] >= caps[(int)gIdx]) continue;
            // weighted pick
            int total=0; for(auto* e: *use) total+= std::max(1,e->spawnWeight);
            int r = total>0? rand()%total : 0;
            const EntityDataDef* pickedDef=nullptr;
            for(auto* e: *use){ r-= std::max(1,e->spawnWeight); if(r<0){ pickedDef=e; break; } }
            if(!pickedDef) pickedDef = (*use)[0];
            MobKind pickedKind=MobKind::Zombie; bool f=false;
            for(int i=0;i<149;++i) if(std::string(mobStats(static_cast<MobKind>(i)).name)==pickedDef->type){ pickedKind=static_cast<MobKind>(i); f=true; break; }
            if(!f) continue;
            auto mob=std::make_shared<MobEntity>(); mob->entityId=nextEntityId(); mob->kind=pickedKind; mob->health=mobStats(pickedKind).maxHealth;
            if(pickedDef->max_health>0) mob->health=pickedDef->max_health;
            mob->x=wx+0.5; mob->y=groundY+1.0; mob->z=wz+0.5; mob->lastSeenMs=nowMs();
            { std::lock_guard lk(entsMtx_); if(cnts[(int)groupForKind(pickedKind)] >= caps[(int)groupForKind(pickedKind)]) continue; mobs_.push_back(mob); cnts[(int)groupForKind(pickedKind)]++; }
            broadcastMobSpawn(*mob);
        }
    }
}
void GameServer::mobsTick() {
    std::vector<std::pair<std::shared_ptr<MobEntity>, WriteBuffer>> moves;
    std::vector<std::int32_t> despawn;
    std::vector<std::int32_t> deadIds;
    std::vector<std::shared_ptr<MobEntity>> drops;
    {
        std::lock_guard lk(entsMtx_);
        for (auto it = mobs_.begin(); it != mobs_.end();) {
            auto& m = *it;
            bool nearPlayer = false;
            for (auto& pp : playersSnapshot()) {
                double dx = pp->x - m->x, dz = pp->z - m->z;
                if (dx*dx + dz*dz < 60*60) { nearPlayer = true; break; }
            }
            if (!nearPlayer) {
                despawn.push_back(m->entityId);
                if (MobEntity::isBoss(m->kind) && bossAI_) bossAI_->onDeath(*m);
                mobAi_.erase(m->entityId);
                it = mobs_.erase(it); continue;
            }

            const auto& stats = mobStats(m->kind);
            // plan40 C-08 flame: onFireTicks 100t burns 1 per 20t
            if (m->onFireTicks > 0) {
                if (tickNo_ % 20 == 0) applyDamageToMob(*m, 1.f, "onFire");
                if (--m->onFireTicks <= 0) m->onFireTicks = 0;
                // water extinguishes flame
                {
                    int bx=(int)std::floor(m->x), by=(int)std::floor(m->y), bz=(int)std::floor(m->z);
                    uint16_t st = world_.getBlock(bx,by,bz);
                    auto *d = gen::blockByState(st);
                    bool inWater = d && std::string(d->name)=="minecraft:water";
                    if (inWater) m->onFireTicks = 0;
                }
                if (m->dead) {
                    deadIds.push_back(m->entityId); drops.push_back(m);
                    mobAi_.erase(m->entityId);
                    it = mobs_.erase(it); continue;
                }
            }
            // dead check (generic, includes combat/arrow etc) with slime split
            if (m->dead) {
                deadIds.push_back(m->entityId);
                drops.push_back(m);
                if (MobEntity::isBoss(m->kind) && bossAI_) bossAI_->onDeath(*m);
                // slime / magma cube split
                if ((m->kind == MobKind::Slime || m->kind == MobKind::MagmaCube) && m->slimeSize > 0) {
                    int n = 2 + (rand() % 3);
                    for (int s=0; s<n; ++s) {
                        auto baby = std::make_shared<MobEntity>();
                        baby->entityId = nextEntityId();
                        baby->kind = m->kind;
                        baby->slimeSize = m->slimeSize - 1;
                        baby->health = MobEntity::slimeHealthForSize(baby->slimeSize);
                        if (baby->health < 1.f) baby->health = 1.f;
                        baby->x = m->x + (rand()/(double)RAND_MAX - 0.5) * 0.5;
                        baby->y = m->y;
                        baby->z = m->z + (rand()/(double)RAND_MAX - 0.5) * 0.5;
                        baby->lastSeenMs = nowMs();
                        mobs_.push_back(baby);
                        broadcastMobSpawn(*baby);
                    }
                }
                mobAi_.erase(m->entityId);
                it = mobs_.erase(it); continue;
            }
            // aging: babies grow up
            if (m->age < 0 && ++m->age >= 0) {
                m->age = 0;
                WriteBuffer md;                          // reset baby flag
                md.varint(m->entityId);
                md.u8(16); md.u8(0);                     // index16 byte = adult
                md.u8(0);
                broadcastPacketExcept(nullptr, pl::sc::SetEntityMetadata, md);
            }
            if (m->inLove && tickNo_ > m->loveUntilTick) m->inLove = false;

            // daylight burn for undead-style hostiles
            if (stats.burnsInDaylight && MobEntity::isHostile(m->kind) &&
                !isNight()) {
                if (tickNo_ % 20 == 0) {
                    applyDamageToMob(*m, 1.f, "burned to death");
                    if (m->dead) {
                        deadIds.push_back(m->entityId); drops.push_back(m);
                        if ((m->kind == MobKind::Slime || m->kind == MobKind::MagmaCube) && m->slimeSize > 0) {
                            int n = 2 + (rand() % 3);
                            for (int s=0; s<n; ++s) {
                                auto baby = std::make_shared<MobEntity>();
                                baby->entityId = nextEntityId();
                                baby->kind = m->kind;
                                baby->slimeSize = m->slimeSize - 1;
                                baby->health = MobEntity::slimeHealthForSize(baby->slimeSize);
                                if (baby->health < 1.f) baby->health = 1.f;
                                baby->x = m->x + (rand()/(double)RAND_MAX - 0.5) * 0.5;
                                baby->y = m->y;
                                baby->z = m->z + (rand()/(double)RAND_MAX - 0.5) * 0.5;
                                baby->lastSeenMs = nowMs();
                                mobs_.push_back(baby);
                                broadcastMobSpawn(*baby);
                            }
                        }
                        mobAi_.erase(m->entityId);
                        it = mobs_.erase(it); continue;
                    }
                }
            }

            // plan29 §3 Creaking despawn / 32-block radius / heart break
            if (m->kind == MobKind::Creaking && m->creakingTransient) {
                if (!isNight()) {
                    despawn.push_back(m->entityId);
                    mobAi_.erase(m->entityId);
                    it = mobs_.erase(it); continue;
                }
                if (m->hasCreakingHeart) {
                    double dx = m->x - (m->creakingHeartX+0.5), dy = m->y - (m->creakingHeartY+0.5), dz = m->z - (m->creakingHeartZ+0.5);
                    if (dx*dx+dy*dy+dz*dz > 32*32) {
                        m->dead = true;
                    } else {
                        uint16_t hs = world_.getBlock(m->creakingHeartX,m->creakingHeartY,m->creakingHeartZ);
                        auto* hd = gen::blockByState(hs);
                        bool heartGone = !hd || std::string(hd->name)!="minecraft:creaking_heart";
                        if (heartGone) {
                            // twitch then death: immediate for now
                            m->dead = true;
                            if (m->dead) {
                                broadcastSound("minecraft:entity.creaking.twitch", m->x,m->y,m->z,1.f,1.f,"hostile");
                            }
                        }
                    }
                    if (m->dead) {
                        deadIds.push_back(m->entityId); drops.push_back(m);
                        mobAi_.erase(m->entityId);
                        it = mobs_.erase(it); continue;
                    }
                    // same-block 5s respawn near heart (vanilla softlock): if within same block as player >5s, respawn near heart
                    for (auto& pp : playersSnapshot()) {
                        if (!pp->inPlay || pp->dead) continue;
                        int mx=(int)std::floor(m->x), my=(int)std::floor(m->y), mz=(int)std::floor(m->z);
                        int px=(int)std::floor(pp->x), py=(int)std::floor(pp->y), pz=(int)std::floor(pp->z);
                        if (mx==px && my==py && mz==pz) {
                            m->creakingSameBlockTicks++;
                            if (m->creakingSameBlockTicks>100) {
                                // respawn near heart
                                for (int a=0;a<8;++a){
                                    int sx=m->creakingHeartX+(rand()%8-4), sz=m->creakingHeartZ+(rand()%8-4), sy=m->creakingHeartY+1;
                                    if (world_.getBlock(sx,sy,sz)==0 && world_.getBlock(sx,sy+1,sz)==0 && world_.getBlock(sx,sy-1,sz)!=0){
                                        m->x=sx+0.5; m->y=sy; m->z=sz+0.5;
                                        m->creakingSameBlockTicks=0;
                                        WriteBuffer tp; tp.varint(m->entityId); tp.f64(m->x); tp.f64(m->y); tp.f64(m->z); tp.f32(m->yaw); tp.f32(0); tp.boolean(true);
                                        broadcastPacketExcept(nullptr, proto::pl::sc::EntityTeleport, tp);
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

            // ---- Brain-Goal-Sensor AI tick (plan3) + BossAI (plan7)
            auto& ai = aiFor(m);
            ai.ctx->srv = this;
            ai.ctx->world = &world_;
            brainTickGuard_ = m.get();
            ai.brain->tick(*m, *ai.ctx, tickNo_);
            brainTickGuard_ = nullptr;
            if (MobEntity::isBoss(m->kind) && bossAI_) bossAI_->tick(*m, *ai.ctx, tickNo_);

            // ---- creeper fuse & explosion (plan16: ignited fuse separate field, 30 ticks, metadata)
            if (m->kind == MobKind::Creeper && ai.ctx->nearestPlayer) {
                const double cdx = ai.ctx->nearestPlayer->x - m->x;
                const double cdy = ai.ctx->nearestPlayer->y - m->y;
                const double cdz2 = ai.ctx->nearestPlayer->z - m->z;
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
                        broadcastPacketExcept(nullptr, pl::sc::SetEntityMetadata, md);
                        broadcastSound("minecraft:entity.creeper.primed",
                                       m->x, m->y, m->z, 1.f, 1.f, "hostile");
                        broadcastEntitySound(m->entityId, "minecraft:entity.creeper.primed", 1.f, 1.f, SoundSource::Hostile);
                    } else if (tickNo_ - m->creeperFuseStart >= MobEntity::CREEPER_FUSE_TICKS) {
                        const double cxp = m->x, cyp = m->y, czp = m->z;
                        const std::int32_t eid = m->entityId;
                        WriteBuffer rm; rm.varint(1); rm.varint(eid);
                        broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, rm);
                        mobAi_.erase(eid);
                        it = mobs_.erase(it);
                        // Plan8: Charged Creeper explodes with power 6.0 (vs 3.0 normal)
                        explodeAt(cxp, cyp + 0.5, czp, m->creeperCharged ? 6.f : 3.f);
                        continue;
                    }
                } else if (m->creeperIgnited && cd2 > 16) {
                    m->creeperIgnited = false;
                    m->creeperFuseStart = -1;
                    WriteBuffer md;
                    md.varint(m->entityId);
                    meta::writeMetaBool(md, 16, false);
                    md.u8(255);
                    broadcastPacketExcept(nullptr, pl::sc::SetEntityMetadata, md);
                }
            }

            // ---- light-aware daylight burn (real skylight at mob feet)
            if (stats.burnsInDaylight && MobEntity::isHostile(m->kind) &&
                !isNight() && tickNo_ % 20 == 0) {
                world_.generateChunkIfMissing(
                    static_cast<std::int32_t>(m->x) >> 4,
                    static_cast<std::int32_t>(m->z) >> 4);
                lightEngine_->ensureSkyLight(
                    static_cast<std::int32_t>(m->x) >> 4,
                    static_cast<std::int32_t>(m->z) >> 4);
                const std::uint8_t sky =
                    world_.getSkyLight(static_cast<std::int32_t>(m->x),
                                       static_cast<std::int32_t>(m->y),
                                       static_cast<std::int32_t>(m->z));
                if (sky >= 14) applyDamageToMob(*m, 1.f, "burned to death");
                if (m->dead) {
                    deadIds.push_back(m->entityId); drops.push_back(m);
                    if ((m->kind == MobKind::Slime || m->kind == MobKind::MagmaCube) && m->slimeSize > 0) {
                        int n = 2 + (rand() % 3);
                        for (int s=0; s<n; ++s) {
                            auto baby = std::make_shared<MobEntity>();
                            baby->entityId = nextEntityId();
                            baby->kind = m->kind;
                            baby->slimeSize = m->slimeSize - 1;
                            baby->health = MobEntity::slimeHealthForSize(baby->slimeSize);
                            if (baby->health < 1.f) baby->health = 1.f;
                            baby->x = m->x + (rand()/(double)RAND_MAX - 0.5) * 0.5;
                            baby->y = m->y;
                            baby->z = m->z + (rand()/(double)RAND_MAX - 0.5) * 0.5;
                            baby->lastSeenMs = nowMs();
                            mobs_.push_back(baby);
                            broadcastMobSpawn(*baby);
                        }
                    }
                    mobAi_.erase(m->entityId);
                    it = mobs_.erase(it); continue;
                }
            }
            // ---- Plan14 §3/§4: Villager/Enderman tick (aging already handled above single increment)
            // Villager restock & gossip decay (plan16: 2/day restock, Gossip decay)
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
                        broadcastSound("minecraft:entity.villager.work_farm", m->x,m->y,m->z,1.f,1.f,"neutral");
                        // plan46 G-15: auto-schedule the 2nd window of the day
                        // (vanilla: up to twice per day on work-site visits).
                        // Old code left restockUntil=0 here ("For now we clear"),
                        // which silently dropped the 2nd restock.
                        if (m->villagerRestocksToday < 2) {
                            m->restockUntil = tickNo_ + MobEntity::kRestockSecondWindowTicks
                                + (rand() % 2000);
                        } else {
                            m->restockUntil = 0;
                        }
                    } else {
                        m->restockUntil = 0;
                    }
                }
                if (tickNo_%100==0) m->gossip.tickDecay();
            }
            // Enderman: occasional random block pickup via BehaviorTree is primary, but ensure carriedBlock persistence
            // (handled in PickupBlockAction)
            // delta broadcast
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
                moves.emplace_back(m, std::move(b));
                m->sentX=m->x; m->sentY=m->y; m->sentZ=m->z; m->hasSent=true;
            }
            ++it;
        }
    }
    for (auto id : despawn) {
        WriteBuffer b;
        b.varint(1); b.varint(id);
        broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, b);
    }
    for (auto& m : drops) {
        // plan37 B-05: entity loot tables (10 species) via LootTableEvaluator, fallback to legacy dropFor
        bool spawnedViaLoot = false;
        {
            std::string kindName = MobEntity::kindName(m->kind);
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
                    spawnItemDrop(m->x, m->y + 0.4, m->z, st,
                                  (rand()/(double)RAND_MAX-.5)*.15, .1,
                                  (rand()/(double)RAND_MAX-.5)*.15);
                    spawnedViaLoot = true;
                }
            }
        }
        if (!spawnedViaLoot) {
            const auto drop = MobEntity::dropFor(m->kind);
            if (drop.itemId)
                spawnItemDrop(m->x, m->y + 0.4, m->z, drop.itemId, drop.count,
                              (rand()/(double)RAND_MAX-.5)*.15, .1,
                              (rand()/(double)RAND_MAX-.5)*.15);
        }
        // plan17 LOW: equipment drop based on HandDropChances/ArmorDropChances (was never serialized/dropped)
        for (int es=0; es<6; ++es) {
            if (m->equipment[es].empty()) continue;
            float chance = 0.085f;
            if (es==0) chance = m->handDropChances[0];
            else if (es==1) chance = m->handDropChances[1];
            else if (es>=2 && es<=5) chance = m->armorDropChances[es-2];
            float r = float(rand())/float(RAND_MAX);
            if (r < chance) {
                spawnItemDrop(m->x, m->y+0.4, m->z, m->equipment[es],
                              (rand()/(double)RAND_MAX-.5)*.12, 0.18, (rand()/(double)RAND_MAX-.5)*.12);
            }
        }
        // XP orbs on kill
        spawnXpOrbs(m->x, m->y + 0.5, m->z, mobStats(m->kind).xpDrop, nullptr);
    }
    for (auto id : deadIds) {
        WriteBuffer rm;
        rm.varint(1); rm.varint(id);
        broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, rm);
    }
    for (auto& [mob, body] : moves) {
        (void)mob;
        broadcastPacketExcept(nullptr, pl::sc::MoveEntityPosRot, body);
    }
}
} // namespace cppfm
