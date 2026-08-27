#include "GameServer.hpp"
#include "../physics/LightEngine.hpp"
#include "../physics/Fluids.hpp"
#include "../physics/Redstone.hpp"
#include "../worldgen/PortalHandler.hpp"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <fstream>
#include "../generated/ItemIds.hpp"
#include "../generated/EntityIds.hpp"
#include "MenuInteraction.hpp"
#include <cerrno>

namespace cppfm {
std::atomic<bool> g_stopRequested{false};

using namespace proto;

static std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// hotbar: block name -> (itemId, stateId) resolved at startup
struct HotbarEntry { std::uint32_t itemId; std::uint16_t stateId; };
static const char* kHotbarNames[] = {
    "minecraft:grass_block", "minecraft:dirt", "minecraft:stone",
    "minecraft:cobblestone", "minecraft:oak_planks", "minecraft:glass",
    "minecraft:sand", "minecraft:oak_log", "minecraft:glowstone",
};
static std::vector<HotbarEntry> resolveHotbar() {
    std::vector<HotbarEntry> v;
    const auto& items = gen::itemIdByName();
    const auto& blocks = gen::blockNameToState();
    for (auto* n : kHotbarNames) {
        auto ii = items.find(n);
        auto bi = blocks.find(n);
        if (ii == items.end() || bi == blocks.end()) continue;
        v.push_back({ii->second, static_cast<std::uint16_t>(bi->second)});
    }
    return v;
}
static std::vector<HotbarEntry> g_hotbar = resolveHotbar();

static const struct { const char* name; int cnt; } kKit[] = {
    {"minecraft:iron_sword",1}, {"minecraft:iron_pickaxe",1}, {"minecraft:iron_axe",1},
    {"minecraft:bread",8}, {"minecraft:apple",4},
    {"minecraft:cobblestone",64}, {"minecraft:oak_planks",64}, {"minecraft:torch",32},
    {"minecraft:dirt",64},
};

// ================================================================== GameServer

void GameServer::startTickLoop() {
    tickThread_ = std::thread([this] {
        using clock = std::chrono::steady_clock;
        auto next = clock::now() + std::chrono::milliseconds(50);
        while (running_) {
            std::this_thread::sleep_until(next);
            next += std::chrono::milliseconds(50);
            if (!running_) break;
            ++tickNo_;
            try { tickOnce(); } catch (...) {}
        }
    });
}
void GameServer::stopTickLoop() {
    if (tickThread_.joinable()) {
        std::fprintf(stderr, "[cppfm] joining tick thread\n");
        tickThread_.join();
        std::fprintf(stderr, "[cppfm] tick thread joined\n");
    }
}

void GameServer::runForever() {
    startTickLoop();
    std::thread janitor([this] {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const auto now = nowMs();
            for (auto& p : playersSnapshot()) {
                if (!p->inPlay) continue;
                if (now - p->lastSeenMs > 60000) {           // hard idle sweep
                    try { p->conn->close(); } catch (...) {}
                    continue;
                }
                if (p->pendingKeepAlive != 0 && now - p->lastSeenMs > 30000) {
                    WriteBuffer reason;
                    nbt::writeTextComponent(reason, "Timed out");
                    try { p->conn->sendPacket(pl::sc::Disconnect, reason); } catch (...) {}
                    try { p->conn->close(); } catch (...) {}
                    continue;
                }
                if (now - p->lastKeepAliveSentMs >= 10000) {
                    const std::int64_t id = ++p->keepAliveCounter;
                    p->pendingKeepAlive = id;
                    p->lastKeepAliveSentMs = now;
                    WriteBuffer b;
                    b.i64(id);
                    try { p->conn->sendPacket(pl::sc::KeepAlive, b); } catch (...) {}
                }
            }
        }
    });
    janitor.detach();

    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) throw std::runtime_error("socket() failed");
    int one = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(cfg_.port);
    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        throw std::runtime_error(std::string("bind() failed: ") + strerror(errno));
    if (::listen(listenFd_, 64) != 0)
        throw std::runtime_error("listen() failed");
    running_ = true;

    acceptLoop();
}

extern std::atomic<bool> g_stopRequested;

void GameServer::acceptLoop() {
    while (running_) {
        sockaddr_in cli{}; socklen_t cl = sizeof(cli);
        int fd = ::accept(listenFd_, reinterpret_cast<sockaddr*>(&cli), &cl);
        if (fd < 0) {
            if (g_stopRequested || !running_) break;
            if (running_) continue;
            break;
        }
        std::fprintf(stderr, "[cppfm] accepted fd=%d\n", fd);
        std::thread([this, fd] {
            auto conn = std::make_unique<Connection>(fd);
            conn->setNoDelay();
            conn->setSendTimeout(15);
            Session s(*this, std::move(conn));
            s.run();
        }).detach();
    }
}

// ------------------------------------------------------------ mining (Ph3)
static std::string blockNameByState(std::uint16_t sid) {
    for (auto& e : gen::kBlocks) if (e.state == sid) return std::string(e.name);
    return "minecraft:air";
}

void GameServer::broadcastDigStage(Player& p, std::int8_t stage) {
    WriteBuffer b;
    b.varint(p.entityId);
    b.position(p.digX, p.digY, p.digZ);
    b.i8(stage);
    broadcastPacketExcept(nullptr, proto::pl::sc::BlockBreakAnimation, b);
}

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
                onBlockMined(*p, oldState);
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
                        static const std::unordered_map<std::string,std::string> kOv{
                            {"minecraft:grass_block","minecraft:dirt"},
                            {"minecraft:stone","minecraft:cobblestone"}};
                        auto ov = kOv.find(bn);
                        const std::string dn = ov!=kOv.end()?ov->second:bn;
                        if (bn != "minecraft:glass") {
                            auto ii = gen::itemIdByName().find(dn);
                            if (ii != gen::itemIdByName().end())
                                spawnItemDrop(p->digX+.5, p->digY+.25, p->digZ+.5,
                                              ii->second, 1,
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

// ============================================================ ticking (Ph3/4)
void GameServer::sendSetHealth(Player& p) {
    WriteBuffer b;
    b.f32(p.health);
    b.varint(p.food);
    b.f32(p.saturation);
    try { p.conn->sendPacket(pl::sc::SetHealth, b); } catch (...) {}
}

void GameServer::applyDamage(Player& p, float amount, const char* cause) {
    if (p.gamemode == 1 || p.gamemode == 3) return;      // creative/spectator immune
    if (amount <= 0 || p.dead) return;
    p.health -= amount;
    if (p.health <= 0) { p.health = 0; killPlayer(p, cause); }
    sendSetHealth(p);
}

void GameServer::killPlayer(Player& p, const char* cause) {
    if (p.dead) return;
    p.dead = true;
    if (p.stats) p.stats->add("minecraft:custom|minecraft:deaths");
    scoreboard.addScore("deaths", p.name, 1);
    sendScoreAll("deaths", p.name,
                 scoreboard.getScore("deaths", p.name));
    broadcastSystemText(std::string("\u00a7c") + p.name + " died (" + cause + ")", &p);
}

void GameServer::tickOnce() {
    static const bool tr = getenv("CPPFM_TICK_TRACE") != nullptr;
    auto mark = [&](char c) { if (tr) std::fprintf(stderr, "[tick] %c t=%ld\n", c, (long)tickNo_); };
    api::ServerTickEvent ev{tickNo_};
    events().serverTick.fire(ev);
    mark('F');
    fluidSim_->tick(tickNo_);
    mark('R');
    redstone_->tick(tickNo_);
    mark('D');
    tickDigs();
    mark('S');
    survivalTick();
    mark('U');
    furnacesTick();
    mark('E');
    effectsTick();
    mark('X');
    xpOrbsTick();
    mark('m');

    // mob spawn cadence: every 20 ticks
    if (tickNo_ % 20 == 0) trySpawnMobs();
    mark('M');
    mobsTick();
    mark('I');
    itemsTick();
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
            const std::int32_t cx = static_cast<std::int32_t>(k >> 32);
            const std::int32_t cz = static_cast<std::int32_t>(k & 0xFFFFFFFFLL);
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
    // level.dat periodic save every 6000 ticks (~5 min) + also 1200 (~1 min) for safety
    if (tickNo_ % 6000 == 0 && tickNo_ != 0) {
        try { persist_->saveLevelData(tickNo_, dayTime()); } catch (...) {}
        for (int d = 0; d < 2; ++d) if (dimPersist_[d]) try { dimPersist_[d]->saveLevelData(tickNo_, dayTime()); } catch (...) {}
        std::fprintf(stderr, "[cppfm] periodic level.dat save t=%ld\n", (long)tickNo_);
    } else if (tickNo_ % 1200 == 0 && tickNo_ != 0) {
        try { persist_->saveLevelData(tickNo_, dayTime()); } catch (...) {}
    }
}

bool GameServer::isChunkInSimulationDistance(std::int32_t cx, std::int32_t cz) const {
    const int sim = cfg_.simulationDistance;
    if (sim <= 0) return true;
    const double limit = sim * 16.0;
    const double limit2 = limit * limit;
    const double chX = cx * 16.0 + 8.0;
    const double chZ = cz * 16.0 + 8.0;
    auto players = const_cast<GameServer*>(this)->playersSnapshot();
    if (players.empty()) return false;
    for (auto &p : players) {
        if (!p->inPlay) continue;
        double dx = p->x - chX;
        double dz = p->z - chZ;
        if (dx*dx + dz*dz < limit2) return true;
    }
    return false;
}

void GameServer::chunksUnloadTick() {
    const int sim = cfg_.simulationDistance;
    const int view = cfg_.viewDistance;
    const int unloadDist = std::max(sim, view) * 16 + 32;
    const double unloadDist2 = double(unloadDist) * double(unloadDist);
    auto doWorld = [&](World &w, Persistence *pp, std::int8_t dim) {
        auto keys = w.allChunkKeys();
        std::vector<std::int64_t> toErase;
        toErase.reserve(keys.size());
        auto players = playersSnapshot();
        for (auto k : keys) {
            if (w.isForcedKey(k)) continue;
            const std::int32_t cx = static_cast<std::int32_t>(k >> 32);
            const std::int32_t cz = static_cast<std::int32_t>(k & 0xFFFFFFFFLL);
            bool near = false;
            for (auto &pl : players) {
                if (!pl->inPlay) continue;
                if (pl->dimension != dim) continue;
                const double chX = cx * 16.0 + 8.0;
                const double chZ = cz * 16.0 + 8.0;
                double dx = pl->x - chX;
                double dz = pl->z - chZ;
                if (dx*dx + dz*dz < unloadDist2) { near = true; break; }
            }
            if (near) continue;
            bool anyInDim = false;
            for (auto &pl : players) if (pl->inPlay && pl->dimension == dim) { anyInDim = true; break; }
            if (!anyInDim && w.isForced(cx, cz)) continue;
            if (pp && pp->isDirty(cx, cz)) {
                pp->flushChunk(cx, cz);
            }
            toErase.push_back(k);
            invalidateChunkCache(cx, cz);
        }
        for (auto k : toErase) {
            const std::int32_t cx = static_cast<std::int32_t>(k >> 32);
            const std::int32_t cz = static_cast<std::int32_t>(k & 0xFFFFFFFFLL);
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

void GameServer::survivalTick() {
    const auto now = nowMs();
    for (auto& pp : playersSnapshot()) {
        auto* p = pp.get();
        if (!p->inPlay || !p->spawned || p->dead) continue;
        if (p->gamemode != 0) continue;                  // survival only

        // exhaustion -> saturation/food
        if (p->exhaustion >= 4.0) {
            p->exhaustion -= 4.0;
            if (p->saturation > 0) p->saturation = std::max(0.f, p->saturation - 1.f);
            else p->food = std::max(0, p->food - 1);
            sendSetHealth(*p);
        }
        // natural regeneration (every 4s)
        if (tickNo_ % 80 == 0 && p->food >= 18 && p->health < 20.f) {
            p->health = std::min(20.f, p->health + 1.f);
            p->saturation = std::max(0.f, p->saturation - 1.f);
            sendSetHealth(*p);
        }
        // starvation
        if (tickNo_ % 80 == 0 && p->food == 0 && p->health > 1.f) {
            p->health -= 1.f;
            sendSetHealth(*p);
        }
        // void damage
        if (p->y < kMinY - 16) applyDamage(*p, 4.f, "fell out of the world");
        // keepalive watchdog uses lastSeenMs via janitor; nothing here
        (void)now;
    }
}

void GameServer::trySpawnMobs() {
    if (!gamerules_.getBool("doMobSpawning")) return;
    static std::int64_t lastTrace = 0;
    const bool tr = getenv("CPPFM_TRACE") != nullptr;
    if (tr && tickNow() - lastTrace >= 200) {
        lastTrace = tickNow();
        std::fprintf(stderr, "[cppfm] mob-spawn tick: night=%d mobs=%zu\n",
                     (int)isNight(), mobs_.size());
    }
    std::lock_guard lk(entsMtx_);
    for (auto& pp : playersSnapshot()) {
        auto* pl = pp.get();
        if (!pl->inPlay || !pl->spawned) continue;
        int nearby = 0;
        for (auto& m : mobs_) {
            double dx = m->x - pl->x, dz = m->z - pl->z;
            if (dx*dx + dz*dz < 48*48) ++nearby;
        }
        if (nearby >= 8) continue;
        // 2 attempts
        for (int a = 0; a < 2; ++a) {
            const double ang = (rand() / (double)RAND_MAX) * 6.28318;
            const double dist = 14 + (rand() % 22);
            const std::int32_t wx = static_cast<std::int32_t>(pl->x + std::cos(ang)*dist);
            const std::int32_t wz = static_cast<std::int32_t>(pl->z + std::sin(ang)*dist);
            world_.generateChunkIfMissing(wx >> 4, wz >> 4);
            int feet = 4;
            bool ok = false;
            world_.withChunk(wx >> 4, wz >> 4, [&](const Chunk& c) {
                for (int ry = kSectionsPerChunk*16 - 1; ry >= 0; --ry)
                    if (c.blocks[Chunk::index(ry>>4, ry&15, wz&15, wx&15)] != 0) { feet = ry+1; ok=true; break; }
            });
            if (!ok) continue;
            const int groundY = kMinY + feet;                 // first solid world y

            // light-aware spawn rules: hostiles need light < 8 at the spawn
            // cell (skylight scaled by weather/daytime), passives need >= 9.
            lightEngine_->ensureSkyLight(wx >> 4, wz >> 4);
            const std::uint8_t sky =
                world_.getSkyLight(wx, groundY, wz);
            const std::uint8_t blk =
                world_.getBlockLight(wx, groundY, wz);
            const double skyEff = isNight() ? 0.0
                                  : raining() ? sky * 0.6 : double(sky);
            const double effLight = std::max(double(blk), skyEff);

            static const MobKind passive[] = {MobKind::Pig, MobKind::Cow,
                                              MobKind::Sheep, MobKind::Chicken,
                                              MobKind::Rabbit};
            MobKind picked;
            const bool wantHostile = effLight < 8.0 && (isNight() || raining());
            if (wantHostile) {
                int hostiles = 0;
                for (auto& m : mobs_)
                    if (MobEntity::isHostile(m->kind)) ++hostiles;
                if (hostiles >= 6) continue;
                static const MobKind hostilesTab[] = {MobKind::Zombie,
                                                      MobKind::Zombie,
                                                      MobKind::Skeleton,
                                                      MobKind::Creeper,
                                                      MobKind::Spider};
                picked = hostilesTab[rand() % 5];
            } else if (effLight >= 9.0) {
                picked = passive[rand() % 5];
            } else continue;
            {
                auto mob = std::make_shared<MobEntity>();
                mob->entityId = nextEntityId();
                mob->kind = picked;
                mob->health = mobStats(picked).maxHealth;
                mob->x = wx + 0.5; mob->y = groundY + 1.0; mob->z = wz + 0.5;
                mob->lastSeenMs = nowMs();
                mobs_.push_back(mob);
                broadcastMobSpawn(*mob);
            }
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
        const auto now = nowMs();
        for (auto it = mobs_.begin(); it != mobs_.end();) {
            auto& m = *it;
            bool nearPlayer = false;
            for (auto& pp : playersSnapshot()) {
                double dx = pp->x - m->x, dz = pp->z - m->z;
                if (dx*dx + dz*dz < 60*60) { nearPlayer = true; break; }
            }
            if (!nearPlayer) {
                despawn.push_back(m->entityId);
                mobAi_.erase(m->entityId);
                it = mobs_.erase(it); continue;
            }

            const auto& stats = mobStats(m->kind);
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
                    if (m->dead) { deadIds.push_back(m->entityId); drops.push_back(m);
                        mobAi_.erase(m->entityId);
                        it = mobs_.erase(it); continue; }
                }
            }

            // ---- Brain-Goal-Sensor AI tick (plan3)
            auto& ai = aiFor(m);
            ai.ctx->srv = this;
            ai.ctx->world = &world_;
            brainTickGuard_ = m.get();
            ai.brain->tick(*m, *ai.ctx, tickNo_);
            brainTickGuard_ = nullptr;

            // ---- creeper fuse & explosion
            if (m->kind == MobKind::Creeper && ai.ctx->nearestPlayer) {
                const double cdx = ai.ctx->nearestPlayer->x - m->x;
                const double cdy = ai.ctx->nearestPlayer->y - m->y;
                const double cdz2 = ai.ctx->nearestPlayer->z - m->z;
                const double cd2 = cdx*cdx + cdy*cdy + cdz2*cdz2;
                if (cd2 < 9) {
                    if (m->nextWanderAt == 0) {
                        m->nextWanderAt = tickNo_ + 30;   // 1.5 s fuse
                        broadcastSound("minecraft:block.grass.break",
                                       m->x, m->y, m->z, 1.f, 1.4f);
                    } else if (tickNo_ >= m->nextWanderAt) {
                        const double cxp = m->x, cyp = m->y, czp = m->z;
                        const std::int32_t eid = m->entityId;
                        WriteBuffer rm; rm.varint(1); rm.varint(eid);
                        broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, rm);
                        mobAi_.erase(eid);
                        it = mobs_.erase(it);
                        explodeAt(cxp, cyp + 0.5, czp, 3.f);
                        continue;
                    }
                } else if (m->nextWanderAt != 0 && cd2 > 16) {
                    m->nextWanderAt = 0;                   // defuse
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
            }
            // delta broadcast
            if (!m->hasSent ||
                std::abs(m->x-m->sentX)+std::abs(m->y-m->sentY)+std::abs(m->z-m->sentZ) > 0.03) {
                WriteBuffer b;
                b.varint(m->entityId);
                b.i16((std::int16_t)((m->x-m->sentX) * 4096));
                b.i16((std::int16_t)((m->y-m->sentY) * 4096));
                b.i16((std::int16_t)((m->z-m->sentZ) * 4096));
                b.i8((std::int8_t)(m->yaw * 256.f/360.f));
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
        const auto drop = MobEntity::dropFor(m->kind);
        if (drop.itemId)
            spawnItemDrop(m->x, m->y + 0.4, m->z, drop.itemId, drop.count,
                          (rand()/(double)RAND_MAX-.5)*.15, .1,
                          (rand()/(double)RAND_MAX-.5)*.15);
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

void GameServer::spawnMob(MobKind kind, double x, double y, double z) {
    auto mob = std::make_shared<MobEntity>();
    mob->entityId = nextEntityId();
    mob->kind = kind;
    const auto& stats = mobStats(kind);
    mob->health = stats.maxHealth;
    mob->x = x; mob->y = y; mob->z = z;
    mob->lastSeenMs = nowMs();
    {
        std::lock_guard lk(entsMtx_);
        mobs_.push_back(mob);
    }
    broadcastMobSpawn(*mob);
}

void GameServer::broadcastMobSpawn(const MobEntity& mob) {
    WriteBuffer b;
    b.varint(mob.entityId);
    static std::uint8_t zero[16] = {};
    b.uuid(zero);
    b.varint(static_cast<std::int32_t>(MobEntity::typeId(mob.kind)));
    b.f64(mob.x); b.f64(mob.y); b.f64(mob.z);
    b.i8(0); b.i8(0); b.i8(0);
    b.varint(0); b.i16(0); b.i16(0); b.i16(0);
    broadcastPacketExcept(nullptr, pl::sc::SpawnEntity, b);
}

void GameServer::mobAttackPlayer(MobEntity& m, Player& target) {
    const float dmg = mobStats(m.kind).attackDamage;
    if (dmg <= 0) return;
    const float before = target.health;
    std::string cause = MobEntity::kindName(m.kind);   // e.g. minecraft:zombie
    const auto slash = cause.find(':');
    if (slash != std::string::npos) cause = cause.substr(slash + 1);
    applyDamage(target, dmg, cause.c_str());
    if (before != target.health) m.angerTargetEntityId = target.entityId;
}

bool GameServer::tryBreedFeed(Player& p, MobEntity& m) {
    const auto foodId = MobEntity::breedingItemFor(m.kind);
    if (!foodId || MobEntity::isBaby(m)) return false;
    // consume one breeding item from hotbar/main inv
    for (auto& s : p.inv)
        if (s.itemId == foodId && s.count > 0) {
            if (--s.count <= 0) s = ItemStack::air();
            resendInventory(p);
            m.inLove = true;
            m.loveUntilTick = tickNoForTest() + 30 * 20;
            // entity status 18 = hearts
            WriteBuffer st;
            st.i32(m.entityId); st.i8(18);
            broadcastPacketExcept(nullptr, pl::sc::EntityEvent, st);
            return true;
        }
    return false;
}


GameServer::MobAiEntry& GameServer::aiFor(const std::shared_ptr<MobEntity>& m) {
    auto it = mobAi_.find(m->entityId);
    if (it == mobAi_.end()) {
        MobAiEntry e;
        e.brain = std::make_unique<Brain>();
        e.ctx = std::make_unique<AiContext>();
        it = mobAi_.emplace(m->entityId, std::move(e)).first;
    }
    return it->second;
}

std::shared_ptr<MobEntity> GameServer::findLovePartner(const MobEntity& seeker) {
    std::lock_guard lk(entsMtx_);
    for (auto& other : mobs_) {
        if (other.get() == &seeker || other->kind != seeker.kind ||
            !other->inLove || MobEntity::isBaby(*other))
            continue;
        const double dx = other->x - seeker.x, dz = other->z - seeker.z;
        if (dx * dx + dz * dz < 64) return other;
    }
    return nullptr;
}

// ------------------------------------------------------- progress tracking

void GameServer::initPlayerProgress(Player& p) {
    const std::string hex = uuidToHex(p.uuid);
    p.stats = std::make_unique<StatsManager>();
    p.advancements = std::make_unique<AdvancementManager>(hex);
    p.stats->load(hex);
    p.advancements->load();
    p.joinTick = tickNo_;
    grantAdvancement(p, "cppfm:root");
}

void GameServer::savePlayerProgress(Player& p) {
    if (!p.stats || !p.advancements) return;
    if (p.joinTick) {
        const std::int64_t ticks = tickNo_ - p.joinTick;
        p.stats->add("minecraft:custom|minecraft:play_time", ticks);
        p.joinTick = tickNo_;
    }
    p.stats->save(uuidToHex(p.uuid));
    p.advancements->save();
}

void GameServer::sendAdvancementsTo(Player& p, bool reset) {
    WriteBuffer b;
    writeAdvancementsPacket(b, reset, advancementDefs(),
        [&](const std::string& id) {
            return p.advancements && p.advancements->has(id);
        });
    try { p.conn->sendPacket(pl::sc::UpdateAdvancements, b); } catch (...) {}
}

void GameServer::grantAdvancement(Player& p, const std::string& id) {
    if (!p.advancements) return;
    if (p.advancements->grant(id)) sendAdvancementsTo(p, false);
}

void GameServer::onBlockMined(Player& p, std::uint16_t oldState) {
    if (!p.stats) return;
    static thread_local std::unordered_map<std::uint32_t, std::string> inv;
    if (inv.empty())
        for (auto& [n, s] : gen::kBlocks) inv.emplace(s, std::string(n));
    auto it = inv.find(oldState);
    const std::string name = it != inv.end() ? it->second : "minecraft:air";
    p.stats->add("minecraft:mined|" + name);
    if (name == "minecraft:oak_log") grantAdvancement(p, "cppfm:wood");
    if (name == "minecraft:stone") { /* stone age analog */ }
}

void GameServer::onItemObtained(Player& p, const ItemStack& s,
                                const char* how) {
    if (!p.stats) return;
    const std::string n = s.name();
    p.stats->add(std::string("minecraft:") + how + "|" + n,
                 s.count);
    if (how == std::string("crafted")) {
        if (n == "minecraft:crafting_table") grantAdvancement(p, "cppfm:bench");
        if (n == "minecraft:stone_pickaxe") grantAdvancement(p, "cppfm:tools");
    }
    if (how == std::string("smelted")) {
        if (n == "minecraft:iron_ingot") grantAdvancement(p, "cppfm:iron");
        grantAdvancement(p, "cppfm:cook");
    }
    if (n == "minecraft:diamond") grantAdvancement(p, "cppfm:diamonds");
}

void GameServer::onMobKilledBy(Player& p, MobKind kind) {
    if (!p.stats) return;
    p.stats->add(std::string("minecraft:killed|") +
                 MobEntity::kindName(kind));
    if (MobEntity::isHostile(kind)) grantAdvancement(p, "cppfm:hunter");
}

// --------------------------------------------------- weather / explosions

void GameServer::weatherTick() {
    if (!gamerules_.getBool("doWeatherCycle")) return;
    if (tickNo_ < weatherUntilTick_) return;
    setWeather(raining() ? Weather::Clear : Weather::Rain,
               (6000 + rand() % 24000) * 20LL);
}

void GameServer::setWeather(Weather w, std::int64_t durationTicks) {
    if (w == weather_) return;
    weather_ = w;
    WriteBuffer b;
    b.u8(w == Weather::Rain ? 2 : 1);                 // begin/end raining
    b.f32(0.f);
    broadcastPacketExcept(nullptr, pl::sc::GameEvent, b);
    weatherUntilTick_ = tickNo_ + durationTicks;
}

void GameServer::broadcastSound(const char* name, double x, double y,
                                double z, float volume, float pitch,
                                const char* category) {
    static const std::unordered_map<std::string, std::uint8_t> kCat = {
        {"master", 0}, {"music", 1}, {"record", 2}, {"weather", 3},
        {"block", 4}, {"hostile", 5}, {"neutral", 6}, {"player", 7},
        {"ambient", 8}, {"voice", 9}};
    WriteBuffer b;
    b.varint(0);                                       // holder: direct entry
    b.string(name);                                    // sound name
    b.boolean(false);                                  // no fixed range
    auto it = kCat.find(category);
    b.varint(it != kCat.end() ? it->second : 0);
    b.i32(static_cast<std::int32_t>(x * 8.0));
    b.i32(static_cast<std::int32_t>(y * 8.0));
    b.i32(static_cast<std::int32_t>(z * 8.0));
    b.f32(volume);
    b.f32(pitch);
    b.i64(rand());
    broadcastPacketExcept(nullptr, pl::sc::SoundEffect, b);
}

void GameServer::explodeAt(double x, double y, double z, float power) {
    const int r = static_cast<int>(std::ceil(power));
    // block destruction sphere with randomised edges
    std::vector<std::array<std::int32_t, 3>> changed;
    for (int dy = -r; dy <= r; ++dy)
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                const double d = std::sqrt(double(dx*dx + dy*dy + dz*dz));
                if (d > power - 0.5 +
                    TerrainGenerator::posHash(explosionSeed_,
                        static_cast<std::int32_t>(x)+dx, dy,
                        static_cast<std::int32_t>(z)+dz) * 0.8)
                    continue;
                const auto bx = static_cast<std::int32_t>(x) + dx;
                const auto by = static_cast<std::int32_t>(y) + dy;
                const auto bz = static_cast<std::int32_t>(z) + dz;
                const auto st = world_.getBlock(bx, by, bz);
                if (st == 0) continue;
                const gen::BlockDef* def = gen::blockByState(st);
                if (def && (def->name == "minecraft:bedrock" ||
                            def->name == "minecraft:obsidian" ||
                            def->hardness < 0))
                    continue;
                world_.setBlock(bx, by, bz, 0);
                broadcastBlockChange(bx, by, bz, 0);
                changed.push_back({bx, by, bz});
            }
    // entity damage: distance-scaled
    for (auto& p : playersSnapshot()) {
        const double dx = p->x - x, dy = p->y - y, dz = p->z - z;
        const double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (dist > power * 2) continue;
        const float dmg =
            (power * power - static_cast<float>(dist)) / power * 8.f;
        if (dmg > 0)
            applyDamage(*p, dmg, "explosion");
        // knockback
        const double inv = 1.0 / std::max(1.0, dist);
        WriteBuffer v;
        v.varint(p->entityId);
        v.i16(static_cast<std::int16_t>(dx * inv * 12000));
        v.i16(static_cast<std::int16_t>((dy * inv + 0.4) * 12000));
        v.i16(static_cast<std::int16_t>(dz * inv * 12000));
        try { p->conn->sendPacket(pl::sc::EntityVelocity, v); } catch (...) {}
        // DamageEvent for the hurt animation/flash
        WriteBuffer de;
        de.varint(p->entityId);
        de.varint(gameData_.idOf("minecraft:damage_type",
                                 "minecraft:explosion") >= 0
                      ? gameData_.idOf("minecraft:damage_type",
                                       "minecraft:explosion")
                      : 0);
        de.varint(0); de.varint(0);
        de.boolean(false);
        try { p->conn->sendPacket(pl::sc::DamageEvent, de); } catch (...) {}
    }
    {
        std::lock_guard lk(entsMtx_);
        std::vector<std::shared_ptr<MobEntity>> dead;
        for (auto& m : mobs_) {
            const double dx = m->x - x, dy = m->y - y, dz = m->z - z;
            const double dist = std::sqrt(dx*dx+dy*dy+dz*dz);
            if (dist > power * 2 || m->dead) continue;
            applyDamageToMob(*m,
                (power * power - static_cast<float>(dist)) / power * 8.f,
                "explosion");
            if (m->dead) dead.push_back(m);
        }
        for (auto& m : dead) {
            const auto drop = MobEntity::dropFor(m->kind);
            if (drop.itemId)
                spawnItemDrop(m->x, m->y + .4f, m->z, drop.itemId, drop.count);
            WriteBuffer rm; rm.varint(1); rm.varint(m->entityId);
            broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, rm);
            mobAi_.erase(m->entityId);
            mobs_.erase(std::remove(mobs_.begin(), mobs_.end(), m),
                        mobs_.end());
        }
    }
    // visuals & audio
    for (int i = 0; i < 4; ++i) {
        WriteBuffer pt;
        pt.boolean(true);                                // long distance
        pt.boolean(false);                               // not always shown
        pt.f64(x + (rand()%7 - 3) * 0.5);
        pt.f64(y + (rand()%5 - 2) * 0.5);
        pt.f64(z + (rand()%7 - 3) * 0.5);
        pt.f32(0); pt.f32(0); pt.f32(0);
        pt.f32(0);
        pt.varint(i == 0 ? 21 : 22);                     // emitter/explosion
        broadcastPacketExcept(nullptr, pl::sc::WorldParticles, pt);
    }
    broadcastSound("minecraft:entity.generic.explode", x, y, z, 4.f, 1.f,
                   "blocks");
    if (getenv("CPPFM_TRACE"))
        std::fprintf(stderr, "[cppfm] explosion at %.1f/%.1f/%.1f (%zu blocks)\n",
                     x, y, z, changed.size());
}

void GameServer::hoppersTick() {
    if (tickNo_ % 8 != 0) return;
    std::vector<std::pair<std::int64_t, BlockEntity>> snapshot;
    blockEntities_.forEach([&](std::int64_t k, BlockEntity& be) {
        if (be.kind == BlockEntity::Kind::Hopper ||
            be.kind == BlockEntity::Kind::Dispenser)
            snapshot.emplace_back(k, be);
    });
    for (auto& [key, be] : snapshot) {
        const std::int32_t x = posKeyUnpackX(key);
        const std::int32_t y = posKeyUnpackY(key);
        const std::int32_t z = posKeyUnpackZ(key);
        ItemStack* slots = be.generic.slots;
        const int count = be.kind == BlockEntity::Kind::Hopper ? 5 : 9;

        auto mergeIntoFirstFit = [&](const ItemStack& src) -> bool {
            for (int i = 0; i < count; ++i) {
                auto& s = slots[i];
                if (s.empty()) { s = src; return true; }
                if (s.itemId == src.itemId && s.count < 64) {
                    const int take = std::min<int>(64 - s.count, src.count);
                    s.count += take;
                    if (take >= src.count) return true;
                }
            }
            return false;
        };
        auto extractOneFrom = [&](BlockEntity* other) -> bool {
            if (!other) return false;
            ItemStack* oslots = nullptr; int on = 0;
            switch (other->kind) {
            case BlockEntity::Kind::Chest: oslots = other->chest.slots; on = 27; break;
            case BlockEntity::Kind::Hopper: oslots = other->generic.slots; on = 5; break;
            case BlockEntity::Kind::Dispenser: oslots = other->generic.slots; on = 9; break;
            default: return false;
            }
            for (int i = 0; i < on; ++i) {
                auto& s = oslots[i];
                if (s.empty()) continue;
                ItemStack one = ItemStack::of(s.itemId, 1);
                if (mergeIntoFirstFit(one)) {
                    if (--s.count <= 0) s = ItemStack::air();
                    blockEntities_.dirty_.insert(key);
                    return true;
                }
            }
            return false;
        };

        // ---- pull from above
        int n = 0; BlockEntity::Kind k{};
        if (ItemStack* p =
                containerAt(x, y + 1, z, n, k)) {
            (void)p; (void)n; (void)k;
            if (auto* other = blockEntities_.getAt(x, y + 1, z))
                extractOneFrom(other);
        }
        // ---- item entity pickup from the hopper cell itself
        {
            std::lock_guard lk(entsMtx_);
            for (auto& e : itemDrops_) {
                if (!e->collected &&
                    std::abs(e->x - (x + .5)) < 0.8 &&
                    std::abs(e->z - (z + .5)) < 0.8 &&
                    e->y > y - 0.2 && e->y < y + 1.3) {
                    ItemStack one = ItemStack::of(e->itemId, 1);
                    if (mergeIntoFirstFit(one)) {
                        if (--e->count <= 0) e->collected = true;
                        WriteBuffer c;
                        c.varint(e->entityId);
                        c.varint(0);                     // collector: hopper
                        c.varint(1);
                        broadcastPacketExcept(nullptr, pl::sc::Collect, c);
                        break;
                    }
                }
            }
        }
        // ---- push downward
        if (auto* below = blockEntities_.getAt(x, y - 1, z)) {
            if (below != &be && below->kind != BlockEntity::Kind::Furnace) {
                for (int i = 0; i < count; ++i) {
                    auto& s = slots[i];
                    if (s.empty()) continue;
                    ItemStack one = ItemStack::of(s.itemId, 1);
                    ItemStack* oslots = nullptr; int on = 0;
                    switch (below->kind) {
                    case BlockEntity::Kind::Chest: oslots = below->chest.slots; on = 27; break;
                    case BlockEntity::Kind::Hopper: oslots = below->generic.slots; on = 5; break;
                    case BlockEntity::Kind::Dispenser: oslots = below->generic.slots; on = 9; break;
                    default: break;
                    }
                    bool moved = false;
                    if (oslots) {
                        for (int j = 0; j < on && !moved; ++j) {
                            auto& d = oslots[j];
                            if (d.empty()) { d = one; moved = true; }
                            else if (d.itemId == one.itemId && d.count < 64) {
                                ++d.count; moved = true;
                            }
                        }
                    }
                    if (moved) {
                        if (--s.count <= 0) s = ItemStack::air();
                        blockEntities_.dirty_.insert(key);
                    }
                    break;
                }
            }
        }

        // ---- dispenser: eject when powered (edge-triggered) per-item (plan5 items 48-51)
        if (be.kind == BlockEntity::Kind::Dispenser) {
            bool powered = redstone_->isPoweredHere(x, y, z);
            bool& was = dispenserPower_[key];
            if (powered && !was) {
                for (int i = 0; i < 9; ++i) {
                    auto& s = slots[i];
                    if (s.empty()) continue;
                    // facing → direction
                    double dx = 0, dy = 0, dz = 0;
                    std::string facing = "north";
                    std::uint16_t bstate = world_.getBlock(x, y, z);
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
                    double sx = x + .5 + dx * .6;
                    double sy = y + .5 + dy * .6;
                    double sz = z + .5 + dz * .6;
                    std::string iname = s.name();
                    bool handled = false;
                    if (iname.find("arrow") != std::string::npos) {
                        spawnProjectile(ProjectileKind::Arrow, sx, sy, sz, dx*1.2, dy*0.2+0.15, dz*1.2, -1, false);
                        handled = true;
                    } else if (iname.find("snowball") != std::string::npos) {
                        spawnProjectile(ProjectileKind::Snowball, sx, sy, sz, dx*1.2, dy*0.2+0.12, dz*1.2, -1, false);
                        handled = true;
                    } else if (iname == "minecraft:egg") {
                        spawnProjectile(ProjectileKind::Egg, sx, sy, sz, dx*1.2, dy*0.2+0.12, dz*1.2, -1, false);
                        handled = true;
                    } else if (iname.find("ender_pearl") != std::string::npos) {
                        spawnProjectile(ProjectileKind::EnderPearl, sx, sy, sz, dx*1.2, dy*0.2+0.12, dz*1.2, -1, false);
                        handled = true;
                    } else if (iname.find("fire_charge") != std::string::npos) {
                        spawnProjectile(ProjectileKind::Fireball, sx, sy, sz, dx*0.5, dy*0.5, dz*0.5, -1, false);
                        handled = true;
                    }
                    if (!handled) {
                        if (iname == "minecraft:tnt" || iname.find("tnt") != std::string::npos) {
                            // primed TNT: explode at front with delay via explodeAt
                            explodeAt(x + dx + 0.5, y + dy + 0.5, z + dz + 0.5, 4.f);
                        } else {
                            spawnItemDrop(sx, sy, sz, s.itemId, 1, dx * .25, .15, dz * .25);
                        }
                    }
                    if (--s.count <= 0) s = ItemStack::air();
                    broadcastSound("minecraft:entity.dispenser.dispense",
                                   x + .5, y + .5, z + .5, 1.f, 1.f,
                                   "blocks");
                    blockEntities_.dirty_.insert(key);
                    break;
                }
            }
            was = powered;
        }
    }
}

ItemStack* GameServer::containerAt(std::int32_t x, std::int32_t y,
                                   std::int32_t z, int& countOut,
                                   BlockEntity::Kind& kindOut) {
    auto* be = blockEntities_.getAt(x, y, z);
    if (!be) return nullptr;
    kindOut = be->kind;
    switch (be->kind) {
    case BlockEntity::Kind::Chest: countOut = 27; return be->chest.slots;
    case BlockEntity::Kind::Hopper: countOut = 5; return be->generic.slots;
    case BlockEntity::Kind::Dispenser: countOut = 9; return be->generic.slots;
    default: return nullptr;
    }
}

// ------------------------------------------------------- villager trading

const std::vector<TradeOffer>& GameServer::tradeTable() {
    using TO = TradeOffer;
    static const std::vector<TradeOffer> table = [] {
        auto id = [](const char* n) {
            return gen::itemIdByName().at(n);
        };
        return std::vector<TO>{
            {id("minecraft:wheat"), 20, 0, 0, id("minecraft:emerald"), 1},
            {id("minecraft:coal"), 15, 0, 0, id("minecraft:emerald"), 1},
            {id("minecraft:emerald"), 1, 0, 0, id("minecraft:bread"), 4},
            {id("minecraft:emerald"), 3, 0, 0, id("minecraft:iron_pickaxe"), 1},
            {id("minecraft:porkchop"), 7, 0, 0, id("minecraft:emerald"), 1},
        };
    }();
    return table;
}

bool GameServer::openTrading(Player& p, MobEntity& v) {
    if (!p.conn) return false;
    const int windowId = ++villagerWindowSeq_;
    WriteBuffer b;
    b.varint(windowId);
    b.varint(menus::kMerchant);
    nbt::writeTextComponent(b, "Villager");
    try { p.conn->sendPacket(proto::pl::sc::OpenScreen, b); } catch (...) {}
    // Trade List payload
    WriteBuffer tl;
    tl.i8(static_cast<std::uint8_t>(windowId));
    const auto& trades = tradeTable();
    tl.varint(static_cast<std::int32_t>(trades.size()));
    for (const auto& t : trades) {
        // inputItem1
        tl.varint(static_cast<std::int32_t>(t.inItem));
        tl.varint(t.inCount);
        tl.varint(0);                                    // no components
        // outputItem as Slot
        ItemStack::of(t.outItem, t.outCount).write(tl);
        tl.boolean(false);                               // inputItem2 absent
        tl.boolean(false);                               // trade disabled
        tl.i32(0);                                       // uses
        tl.i32(9999);                                    // max uses
        tl.i32(1);                                       // xp
        tl.i32(0);                                       // special price
        tl.f32(0.05f);                                   // price multiplier
        tl.i32(0);                                       // demand
    }
    tl.varint(0);                                        // villager entity id? (1.21: not present)
    tl.varint(0);                                        // increase min uses?
    // 1.21.4 trade list tail: villager level varint + xp varint + showProgress bool
    tl.varint(1);
    tl.i32(0);
    tl.boolean(true);
    try { p.conn->sendPacket(proto::pl::sc::TradeList, tl); } catch (...) {}
    return true;
}

bool GameServer::selectTrade(Player& p, std::int32_t index) {
    const auto& trades = tradeTable();
    if (index < 0 || static_cast<std::size_t>(index) >= trades.size())
        return false;
    const auto& t = trades[static_cast<std::size_t>(index)];
    // verify inputs present
    int have = 0;
    for (auto& s : p.inv)
        if (!s.empty() && s.itemId == t.inItem) have += s.count;
    if (have < t.inCount) return false;
    int need = t.inCount;
    for (auto& s : p.inv) {
        if (need <= 0) break;
        if (!s.empty() && s.itemId == t.inItem) {
            const int take = std::min<int>(s.count, need);
            s.count -= take; need -= take;
            if (s.count <= 0) s = ItemStack::air();
        }
    }
    addToInventory(p, t.outItem, t.outCount);
    resendInventory(p);
    spawnXpOrbs(p.x, p.y + 1, p.z, 2, &p);
    broadcastSound("minecraft:entity.villager.yes", p.x, p.y, p.z,
                   .8f, 1.f, "neutral");
    return true;
}

void GameServer::applyDamageToMob(MobEntity& m, float amount, const char* cause) {
    (void)cause;
    if (amount <= 0) return;
    m.health -= amount;
    if (m.health <= 0) m.dead = true;
}

void GameServer::itemsTick() {
    struct Pickup { std::shared_ptr<ItemEntity> ent; Player* collector; };
    std::vector<Pickup> pickups;
    std::vector<std::uint8_t> none;
    {
        std::lock_guard lk(entsMtx_);
        for (auto it = itemDrops_.begin(); it != itemDrops_.end();) {
            auto& e = *it;
            ++e->ageTicks;
            if (e->ageTicks > 6000) { it = itemDrops_.erase(it); continue; }
            // gravity-lite
            e->vy -= 0.04; if (e->vy < -0.5) e->vy = -0.5;
            e->y += e->vy; e->x += e->vx; e->z += e->vz;
            // crude ground clamp
            world_.generateChunkIfMissing(static_cast<std::int32_t>(e->x)>>4,
                                   static_cast<std::int32_t>(e->z)>>4);
            int col=4;
            world_.withChunk(static_cast<std::int32_t>(e->x)>>4,
                      static_cast<std::int32_t>(e->z)>>4,[&](const Chunk& c){
                for (int ry=kSectionsPerChunk*16-1; ry>=0; --ry)
                    if (c.blocks[Chunk::index(ry>>4,ry&15,
                        static_cast<std::int32_t>(e->z)&15,
                        static_cast<std::int32_t>(e->x)&15)]!=0){col=ry+1;break;}
            });
            const double gy = kMinY + col + 0.25;
            if (e->y < gy) { e->y = gy; e->vy = 0; e->vx *= 0.6; e->vz *= 0.6; }

            if (e->ageTicks > 10) {
                for (auto& pp : playersSnapshot()) {
                    auto* pl = pp.get();
                    if (!pl->inPlay || pl->dead) continue;
                    double dx=pl->x-e->x, dy=(pl->y+0.9)-e->y, dz=pl->z-e->z;
                    if (dx*dx+dy*dy+dz*dz < 2.0) {
                        pickups.push_back({e, pl});
                        break;
                    }
                }
            }
            ++it;
        }
    }
    for (auto& pk : pickups) {
        if (addToInventory(*pk.collector, pk.ent->itemId, pk.ent->count)) {
            onItemObtained(*pk.collector,
                           ItemStack::of(pk.ent->itemId, pk.ent->count),
                           "picked_up");
            WriteBuffer c;
            c.varint(pk.ent->entityId);
            c.varint(pk.collector->entityId);
            c.varint(pk.ent->count);
            broadcastPacketExcept(nullptr, 0x76 /*collect*/, c);
            resendInventory(*pk.collector);
            std::lock_guard lk(entsMtx_);
            pk.ent->collected = true;
            itemDrops_.erase(std::remove_if(itemDrops_.begin(), itemDrops_.end(),
                [&](const std::shared_ptr<ItemEntity>& x){ return x.get()==pk.ent.get(); }),
                itemDrops_.end());
            WriteBuffer rm;
            rm.varint(1); rm.varint(pk.ent->entityId);
            broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, rm);
        }
    }
}

void GameServer::spawnItemDrop(double x,double y,double z,std::uint32_t itemId,std::uint8_t cnt,
                               double vx,double vy,double vz) {
    auto e = std::make_shared<ItemEntity>();
    e->entityId = nextEntityId();
    e->itemId = itemId; e->count = cnt;
    e->x=x; e->y=y; e->z=z; e->vx=vx; e->vy=vy; e->vz=vz;
    {
        std::lock_guard lk(entsMtx_);
        itemDrops_.push_back(e);
    }
    broadcastSpawnItem(*e);
}

void GameServer::broadcastSpawnItem(const ItemEntity& it) {
    WriteBuffer b;
    b.varint(it.entityId);
    std::uint8_t zero[16] = {};
    b.uuid(zero);
    b.varint(static_cast<std::int32_t>(gen::entityTypeIdByName().at("minecraft:item")));
    b.f64(it.x); b.f64(it.y); b.f64(it.z);
    b.i8(0); b.i8(0); b.i8(0);
    b.varint(1);                                        // objectData = 1 (item w/ stack)
    b.i16(static_cast<std::int16_t>(it.vx*8000));
    b.i16(static_cast<std::int16_t>(it.vy*8000));
    b.i16(static_cast<std::int16_t>(it.vz*8000));
    broadcastPacketExcept(nullptr, pl::sc::SpawnEntity, b);
    // metadata index 8 = item stack: [idx][7][count][itemId][addC=0][remC=0], then FF
    WriteBuffer md;
    md.varint(it.entityId);
    md.u8(8); md.u8(7);
    md.varint(it.count ? it.count : 1);
    md.varint(static_cast<std::int32_t>(it.itemId));
    md.varint(0); md.varint(0);
    md.u8(255);
    broadcastPacketExcept(nullptr, pl::sc::SetEntityMetadata, md);
}

bool GameServer::addToInventory(Player& p, std::uint32_t itemId, std::uint16_t count) {
    // merge into existing stacks (hotbar 36..44, main 9..35)
    for (int pass = 0; pass < 2; ++pass) {
        for (int i : (pass == 0 ? std::initializer_list<int>{36,37,38,39,40,41,42,43,44}
                                : std::initializer_list<int>{9,10,11,12,13,14,15,16,17,18,19,
                                                             20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35})) {
            auto& s = p.inv[i];
            if (pass == 0 && s.itemId == itemId && s.count > 0 && s.count < 64) {
                const auto take = std::min<int16_t>((int16_t)(64 - s.count), (int16_t)count);
                s.count += take; count -= take;
                if (count == 0) return true;
            } else if (pass == 1 && s.count == 0) {
                s.itemId = itemId; s.count = std::min<int16_t>(64, (int16_t)count);
                count -= s.count;
                if (count == 0) return true;
            }
        }
    }
    return false;                                       // inventory full: stays on ground
}

void GameServer::resendInventory(Player& p) {
    WriteBuffer b;
    b.u8(0);                                            // window 0
    b.varint(++p.invStateId);
    b.varint(46);
    for (int i = 0; i < 46; ++i) p.inv[i].write(b);
    ItemStack::air().write(b);                          // carried
    try { p.conn->sendPacket(pl::sc::ContainerSetContent, b); } catch (...) {}
}

// ===================================================================== Session

// ===================================================================== Session


// ------------------------------------------------------ level.dat + playerdata
static std::string uuidToHexString(const std::array<std::uint8_t,16>& uuid) {
    char buf[33];
    for (int i = 0; i < 16; ++i) snprintf(buf + i * 2, 3, "%02x", uuid[i]);
    return std::string(buf, 32);
}

void GameServer::saveLevelData() {
    persist_->saveLevelData(tickNo_, dayTime());
}

void GameServer::loadLevelData() {
    persist_->loadLevelData();
}


// ---------------------------------------------------- playerdata NBT I/O
static void savePlayerNBT(const std::string& path, Player& p) {
    WriteBuffer out;
    out.u8(10); out.u16(0);                            // root compound
    out.u8(5); out.u16(6); out.raw("Health", 6); out.f32(p.health);
    out.u8(3); out.u16(9); out.raw("foodLevel", 9); out.i32(p.food);
    out.u8(5); out.u16(10); out.raw("foodSaturation", 10); out.f32(p.saturation);
    out.u8(3); out.u16(9); out.raw("XpLevel", 9); out.i32(p.xp.level);
    out.u8(3); out.u16(9); out.raw("XpTotal", 9); out.i32(p.xp.totalXp);
    out.u8(5); out.u16(13); out.raw("XpP", 13); out.f32(p.xp.progress);
    // playerDim / pos
    out.u8(3); out.u16(3); out.raw("Dim", 3); out.i32(static_cast<std::int32_t>(p.dimension));
    out.u8(9); out.u16(3); out.raw("Pos", 3);
    out.u8(6); out.i32(3);
    out.f64(p.x); out.f64(p.y); out.f64(p.z);
    out.u8(9); out.u16(9); out.raw("Inventory", 9);
    int count = 0;
    for (int i = 0; i < 46; ++i)
        if (!p.inv[i].empty()) ++count;
    out.i32(count);
    for (int i = 0; i < 46; ++i) {
        const auto& sl = p.inv[i];
        if (sl.empty()) continue;
        out.u8(10);
        const std::string nm = sl.name();
        out.u16((uint16_t)nm.size()); out.raw(nm.data(), nm.size());
        out.u8(1); out.u16(5); out.raw("Count", 5); out.i8((int8_t)sl.count);
        out.u8(1); out.u16(4); out.raw("Slot", 4); out.i8((int8_t)i);
        out.u8(0);
    }
    out.u8(0);
    out.u8(0);
    std::filesystem::create_directories(
        path.substr(0, path.find_last_of('/')));
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data.data()), out.data.size());
}

static bool loadPlayerNBT(const std::string& path, Player& p) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    if (bytes.size() < 10 || bytes[0] != 10) return false;
    try {
        ReadBuffer r(bytes);
        nbt::Parser parser(r);
        nbt::Value root = parser.readFileRoot();
        if (const auto* v = root.get("Health")) p.health = v->f;
        if (const auto* v = root.get("foodLevel")) p.food = v->i;
        if (const auto* v = root.get("foodSaturation")) p.saturation = v->f;
        if (const auto* v = root.get("XpLevel")) p.xp.level = v->i;
        if (const auto* v = root.get("XpTotal")) p.xp.totalXp = v->i;
        if (const auto* v = root.get("XpP")) p.xp.progress = v->f;
        if (const auto* v = root.get("Dim"))
            p.dimension = static_cast<std::int8_t>(v->i);
        if (const auto* v = root.get("Pos")) {
            if (v->list.size() == 3) {
                p.x = v->list[0].d; p.y = v->list[1].d; p.z = v->list[2].d;
                p.prevFeetY = p.y;
            }
        }
        if (const auto* invv = root.get("Inventory")) {
            for (const auto& item : invv->list) {
                const auto* idv = item.get("id");
                const auto* cv = item.get("Count");
                const auto* sv = item.get("Slot");
                if (!idv || !sv) continue;
                auto it = gen::itemIdByName().find(idv->str);
                if (it == gen::itemIdByName().end()) continue;
                const int slot = sv->b;
                if (slot < 0 || slot >= 46) continue;
                p.inv[slot] = ItemStack::of(it->second,
                                            cv ? static_cast<std::int16_t>(cv->b) : 1);
            }
        }
        return true;
    } catch (...) { return false; }
}

void GameServer::savePlayerData(const std::string& uuidHex, Player& p) {
    std::filesystem::create_directories(cfg_.worldDir + "/playerdata");
    savePlayerNBT(cfg_.worldDir + "/playerdata/" + uuidHex + ".dat", p);
}
bool GameServer::loadPlayerData(const std::string& uuidHex, Player& p) {
    return loadPlayerNBT(cfg_.worldDir + "/playerdata/" + uuidHex + ".dat", p);
}

void Session::run() {
    try {
        while (state_ != State::Done && srv_.running()) {
            switch (state_) {
            case State::Handshake: {
                auto frame = conn_->readFrame();
                ReadBuffer in(frame);
                const std::uint8_t pid = in.u8();
                if (pid != hb::cs::Intention)
                    throw std::runtime_error("expected handshake intention");
                handleHandshake(in);
                break;
            }
            case State::Status:
                handleStatus();
                state_ = State::Done;                 // vanilla closes after status
                break;
            case State::Login:
                handleLogin();
                break;
            case State::Configuration:
                handleConfiguration();
                break;
            case State::Play:
                handlePlay();
                break;
            default:
                return;
            }
        }
    } catch (const SocketClosedError&) {
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] session %s error: %s\n",
                     conn_->peer().c_str(), e.what());
    }
    if (registered_) {
        api::PlayerQuitEvent qev;
        qev.player = self_.get();
        srv_.events().quit.fire(qev);
        srv_.savePlayerProgress(*self_);
        srv_.broadcastSystemText("\u00a7e" + self_->name + " left the game", nullptr);
        WriteBuffer rm;
        rm.varint(1);
        rm.uuid(self_->uuid.data());
        srv_.broadcastPacketExcept(nullptr, pl::sc::PlayerInfoRemove, rm);
        WriteBuffer ent;
        ent.varint(1);
        ent.varint(self_->entityId);
        srv_.broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, ent);
                srv_.savePlayerData(GameServer::uuidToHex(self_->uuid), *self_);
srv_.removePlayer(self_.get());
        registered_ = false;
    }
}

void Session::handleHandshake(ReadBuffer& in) {
    const std::int32_t protoVer = in.varint();
    const std::string address = in.string(1024);
    const std::uint16_t port = in.u16();
    const std::int32_t nextState = in.varint();
    (void)address; (void)port;
    if (nextState == 1) { state_ = State::Status; return; }
    if (nextState == 2) {
        if (protoVer != kProtocolVersion) {
            state_ = State::Login;
            disconnectIn("{\"text\":\"Outdated client! Please use 1.21.4\"}");
            state_ = State::Done;
            return;
        }
        state_ = State::Login;
        return;
    }
    throw std::runtime_error("bad handshake next state");
}

void Session::handleStatus() {
    for (;;) {
        auto frame = conn_->readFrame();
        ReadBuffer in(frame);
        switch (in.u8()) {
        case st::cs::Request: {
            std::string sample;
            {
                int n = 0;
                for (auto& p : srv_.playersSnapshot()) {
                    if (n++ >= 2) break;
                    sample += (n > 1 ? "," : "");
                    sample += "{\"name\":\"" + p->name +
                              "\",\"id\":\"" +
                              GameServer::uuidToDashed(p->uuid) + "\"}";
                }
            }
            std::string favicon;
            {   // optional icon.png next to server.properties
                std::ifstream f("server-icon.png", std::ios::binary);
                if (f) {
                    std::string bytes((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());
                    static const char* b64 =
                        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                        "0123456789+/";
                    const std::string prefix = "data:image/png;base64,";
                    size_t i = 0;
                    while (i < bytes.size()) {
                        const uint32_t chunk[3] = {
                            bytes[i],
                            i + 1 < bytes.size() ? bytes[i + 1] : 0,
                            i + 2 < bytes.size() ? bytes[i + 2] : 0};
                        favicon += b64[(chunk[0] >> 2) & 0x3F];
                        favicon += b64[((chunk[0] & 0x03) << 4) |
                                       ((chunk[1] >> 4) & 0x0F)];
                        favicon += i + 1 < bytes.size()
                                       ? b64[((chunk[1] & 0x0F) << 2) |
                                             ((chunk[2] >> 6) & 0x03)]
                                       : '=';
                        favicon += i + 2 < bytes.size()
                                       ? b64[chunk[2] & 0x3F]
                                       : '=';
                        i += 3;
                    }
                    favicon.insert(0, prefix);
                }
            }
            std::string json =
                "{\"version\":{\"name\":\"" + std::string(kMinecraftVersion) +
                "\",\"protocol\":" + std::to_string(kProtocolVersion) +
                "},\"players\":{\"max\":" + std::to_string(srv_.config().maxPlayers) +
                ",\"online\":" + std::to_string(srv_.playerCount() + 0) +
                ",\"sample\":[" + sample + "]}" +
                (favicon.empty() ? "" :
                 ",\"favicon\":\"" + favicon + "\"") +
                ",\"description\":{\"text\":\"" + srv_.config().motd +
                "\"},\"enforcesSecureChat\":false}";
            WriteBuffer body;
            body.string(json);
            conn_->sendPacket(st::sc::Response, body);
            break;
        }
        case st::cs::Ping: {
            WriteBuffer body;
            body.i64(in.i64());
            conn_->sendPacket(st::sc::Pong, body);
            return;
        }
        default:
            throw std::runtime_error("unexpected status packet");
        }
    }
}

void Session::disconnectIn(const char* textJson) {
    WriteBuffer body;
    nbt::writeTextComponent(body, textJson);
    switch (state_) {
    case State::Play:          conn_->sendPacket(pl::sc::Disconnect, body); break;
    case State::Configuration: conn_->sendPacket(cf::sc::Disconnect, body); break;
    default:                   conn_->sendPacket(lo::sc::Disconnect, body); break;
    }
}

std::string GameServer::dispatchConsole(const std::string& line) {
    brigadier::CommandContext ctx;
    ctx.source.console = true;
    ctx.srcX = 0; ctx.srcY = -60; ctx.srcZ = 0;
    for (auto& p : playersSnapshot())
        ctx.playerNames.push_back(p->name);
    ctx.resolveSelector = [this](const std::string& raw,
                                 brigadier::SelectorResult& out) {
        out = resolveSelector(raw, nullptr);
    };
    const auto res = commands_.execute(line, brigadier::CommandSource{});
    return res.ok ? "ok" : ("error: " + res.errorText);
}

void Session::handleLogin() {
    auto frame = conn_->readFrame();
    ReadBuffer in(frame);
    if (in.u8() != lo::cs::Hello) throw std::runtime_error("expected login hello");

    self_->name = in.string(16);

    auto uuidBytes = in.bytes(16);
    std::copy(uuidBytes.begin(), uuidBytes.end(), self_->uuid.begin());
    if (srv_.config().whitelist) {
        bool ok = false;
        // any registered-name match is impossible pre-join; check file-backed list
        ok = srv_.whitelist().enabled() ? srv_.whitelist().contains(self_->name)
                                        : true;
        if (!ok) {
            WriteBuffer kick;
            nbt::writeTextComponent(kick, "You are not whitelisted on this server");
            conn_->sendPacket(proto::lo::sc::Disconnect, kick);
            state_ = State::Done;
            return;
        }
    }
    self_->entityId = 0; // set on play entry

    if (srv_.config().compressionThreshold >= 0) {
        WriteBuffer sc;
        sc.varint(srv_.config().compressionThreshold);
        conn_->sendPacket(lo::sc::SetCompression, sc);
        conn_->setCompression(srv_.config().compressionThreshold);
    }

    std::fprintf(stderr, "[cppfm] login hello: %s from %s\n",
                 self_->name.c_str(), conn_->peer().c_str());
    if (srv_.config().onlineMode) {
        std::fprintf(stderr, "[cppfm] ONLINE: sending encryption request to %s\n", self_->name.c_str());
        if (!srv_.loginVerifyToken_.size()) {
            srv_.loginKeys_.generate();
            srv_.loginVerifyToken_.resize(16);
            RAND_bytes(reinterpret_cast<unsigned char*>(srv_.loginVerifyToken_.data()), 16);
        }
        WriteBuffer er;
        er.string("");                                // serverId
        er.varint(static_cast<std::int32_t>(srv_.loginKeys_.publicDer.size()));
        er.raw(srv_.loginKeys_.publicDer.data(), srv_.loginKeys_.publicDer.size());
        er.varint(16);
        er.raw(srv_.loginVerifyToken_.data(), 16);
        conn_->sendPacket(proto::lo::sc::EncryptionRequest, er);

        auto pbody = conn_->readFrame();
        std::fprintf(stderr, "[cppfm] ONLINE: got response frame %zu bytes head=%s\n",
                     pbody.size(),
                     [&]{ std::string h; for (std::size_t k = 0; k < pbody.size() && k < 12; ++k)
                         { char x[4]; snprintf(x,3,"%02x",pbody[k]); h+=x; } return h; }().c_str());
        ReadBuffer rin(pbody);
        const auto respPid = rin.u8();
        std::fprintf(stderr, "[cppfm] ONLINE: response pid=%02x\n", respPid);
        if (respPid != proto::lo::cs::Key) throw std::runtime_error("expected encryption response");
        try {
        std::fprintf(stderr, "[cppfm] D1: parsing secret ct\n");
        const auto slen = rin.varint();
        std::fprintf(stderr, "[cppfm] D2: slen=%d\n", slen);
        const auto secretCt = rin.bytes(slen);
        std::fprintf(stderr, "[cppfm] D3: got %zu ct bytes\n", secretCt.size());
        const auto tlen = rin.varint();
        std::fprintf(stderr, "[cppfm] D4: tlen=%d\n", tlen);
        const auto tokenCt = rin.bytes(tlen);
        std::fprintf(stderr, "[cppfm] D5: got %zu tok ct\n", tokenCt.size());

        std::fprintf(stderr, "[cppfm] D6: rsa decrypt secret\n");
        auto secret = crypto::rsaDecryptP(srv_.loginKeys_.pkey, secretCt.data(), secretCt.size());
        std::fprintf(stderr, "[cppfm] D7: secret size=%zu\n", secret.size());
        auto tokenBack = crypto::rsaDecryptP(srv_.loginKeys_.pkey, tokenCt.data(), tokenCt.size());
        std::fprintf(stderr, "[cppfm] D8: token size=%zu\n", tokenBack.size());
        std::fprintf(stderr, "[cppfm] D9: srv token=%s\n",
            [&]{std::string r;for(auto b:srv_.loginVerifyToken_) {char x[4];snprintf(x,3,"%02x",b);r+=x;}return r;}().c_str());
        std::fprintf(stderr, "[cppfm] D10: got token=%s\n",
            [&]{std::string r;for(auto b:tokenBack) {char x[4];snprintf(x,3,"%02x",b);r+=x;}return r;}().c_str());
        if (tokenBack != srv_.loginVerifyToken_)
            throw std::runtime_error("verify token mismatch");
        if (secret.size() != 16) throw std::runtime_error("bad shared secret size");
        std::fprintf(stderr, "[cppfm] D9: all checks passed\n");

        // Mojang session-server authentication
        std::string hash = crypto::mcSha1Hex("", secret, srv_.loginKeys_.publicDer);
        bool authOk = false;
        std::string uuidHex;
        if (getenv("CPPFM_AUTH_STUB")) {
            // test mode: accept any session
            unsigned char md[16];
            unsigned int ml = 0;
            EVP_MD_CTX* mm = EVP_MD_CTX_new();
            EVP_DigestInit_ex(mm, EVP_sha1(), nullptr);
            EVP_DigestUpdate(mm, self_->name.data(), self_->name.size());
            EVP_DigestFinal_ex(mm, md, &ml);
            EVP_MD_CTX_free(mm);
            char hexbuf[33];
            for (int q = 0; q < 16; ++q) snprintf(hexbuf + q * 2, 3, "%02x", md[q]);
            uuidHex = std::string(hexbuf, 32);
            authOk = true;
        } else {
            try {
                const std::string url = "https://sessionserver.mojang.com/session/minecraft/hasJoined?username=" +
                    self_->name + "&serverId=" + hash;
                const std::string json = httpGet(url);
                HasJoinedResult r;
                authOk = parseHasJoined(json, r);
                if (authOk) uuidHex = r.uuidNoDashes;
                if (authOk) {
                    for (auto& pr : r.props) self_->loginProps.push_back({pr.name, pr.value, pr.signature});
                }
            } catch (const std::exception& e) {
                authOk = false;
            }
        }
        if (!authOk) {
            WriteBuffer kick;
            nbt::writeTextComponent(kick, "Failed to verify your session (online mode)");
            conn_->sendPacket(proto::lo::sc::Disconnect, kick);
            state_ = State::Done;
            return;
        }
        for (int q = 0; q < 16; ++q)
            self_->uuid[q] = static_cast<std::uint8_t>(std::stoul(uuidHex.substr(q * 2, 2), nullptr, 16));

        std::fprintf(stderr, "[cppfm] %s online auth ok, enabling encryption\n", self_->name.c_str());
        conn_->enableEncryption(secret);
        WriteBuffer scp;
        scp.varint(256);
        conn_->sendPacket(proto::lo::sc::SetCompression, scp);
        conn_->setCompression(256);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[cppfm] ONLINE AUTH ERROR [%s]: %s\n",
                         self_->name.c_str(), e.what());
            throw;  // re-throw for session cleanup
        }
        std::fprintf(stderr, "[cppfm] %s sent compression+success\n", self_->name.c_str());
    }

    // login success: uuid, name, property list (verified against capture)
    WriteBuffer ok;
    ok.uuid(self_->uuid.data());
    ok.string(self_->name);
    ok.varint(static_cast<std::int32_t>(self_->loginProps.size()));
    for (const auto& pr : self_->loginProps) {
        ok.string(pr.name);
        ok.string(pr.value);
        ok.boolean(!pr.signature.empty());
        if (!pr.signature.empty()) ok.string(pr.signature);
    }
    conn_->sendPacket(lo::sc::GameProfile, ok);

    // wait for LoginAcknowledged (tolerate compression request even though we never send it)
    for (;;) {
        auto f2 = conn_->readFrame();
        ReadBuffer in2(f2);
        switch (in2.u8()) {
        case lo::cs::LoginAcknowledged:
            state_ = State::Configuration;
            return;
        default:
            throw std::runtime_error("unexpected packet during login ack wait");
        }
    }
}

void Session::handleConfiguration() {
    // 0. resource pack (plan3 Resource Pack) — configured via server.properties
    if (!srv_.config().resourcePackUrl.empty()) {
        WriteBuffer b;
        b.string(srv_.config().resourcePackUrl);
        b.string(srv_.config().resourcePackSha1);
        b.boolean(srv_.config().resourcePackForced);
        b.boolean(false);                              // no prompt message
        conn_->sendPacket(cf::sc::AddResourcePack, b);
    }
    // 1. brand
    {
        WriteBuffer b;
        b.string("minecraft:brand");               // channel
        WriteBuffer payload;
        payload.string("CppFabricMC");
        b.raw(payload.data.data(), payload.data.size());
        conn_->sendPacket(cf::sc::CustomPayload, b);
    }
    // 2. known packs: we advertise none -> client expects full registry data
    {
        WriteBuffer b;
        b.varint(0);
        conn_->sendPacket(cf::sc::SelectKnownPacks, b);
    }
    // 3. wait for the client's SelectKnownPacks answer (server hangs otherwise!)
    for (;;) {
        auto frame = conn_->readFrame();
        ReadBuffer in(frame);
        const std::uint8_t kpid = in.u8();
        switch (kpid) {
        case cf::cs::SelectKnownPacks: {
            const std::int32_t n = in.varint();
            for (std::int32_t i = 0; i < n; ++i) {
                (void)in.string();                  // namespace
                (void)in.string();                  // id
                (void)in.string();                  // version
            }
            goto packsDone;
        }
        case cf::cs::KeepAlive: {                   // echo
            WriteBuffer e; e.raw(in.p + in.off, in.remaining());
            conn_->sendPacket(cf::sc::KeepAlive, e);
            break;
        }
        case cf::cs::ClientInformation: {           // settings: parse & ignore
            (void)in.string();                      // locale
            (void)in.i8();                          // view distance
            (void)in.varint();                      // chat mode
            (void)in.boolean();                     // chat colors
            (void)in.u8();                          // skin parts
            (void)in.varint();                      // main hand
            (void)in.boolean();                     // text filtering
            (void)in.boolean();                     // allow server listings
            break;
        }
        case cf::cs::CustomPayload: {                 // plugin channels (config)
            const std::string channel = in.string(256);
            api::ChannelRegistry::Payload body(in.p + in.off, in.p + in.len);
            onPluginPayload(channel, body, 0);
            break;
        }
        case cf::cs::CookieResponse: {
            const std::string key = in.string(256);
            if (in.boolean()) {
                const auto len = in.varint();
                self_->cookies[key] = in.bytes(static_cast<std::size_t>(len));
                srv_.storeCookie(self_->uuid, key, self_->cookies[key]);
            } else srv_.eraseCookie(self_->uuid, key);
            break;
        }
        case cf::cs::ResourcePackResponse:
            (void)in.u8(); (void)in.varint();
            break;
        case cf::cs::Pong:
            (void)in.i32();
            break;
        default:
            throw std::runtime_error("unexpected packet 0x" + [&]{ 
                char b[3]; snprintf(b,3,"%02x", kpid); return std::string(b); }() + " while awaiting known-packs reply");
        }
    }
packsDone:
    // 4. registry blobs, verbatim wire order
    for (const auto& r : srv_.data().registries()) {
        WriteBuffer pkt;
        pkt.u8(cf::sc::RegistryData);
        pkt.raw(r.body.data(), r.body.size());
        conn_->sendRawBody(pkt.data);
    }
    // 5. tags (captured verbatim)
    {
        WriteBuffer pkt;
        pkt.u8(cf::sc::UpdateTags);
        pkt.raw(srv_.data().tags().data(), srv_.data().tags().size());
        conn_->sendRawBody(pkt.data);
    }
    // 6. finish & await acknowledgement
    conn_->sendPacket(cf::sc::FinishConfiguration, {});
    for (;;) {
        auto frame = conn_->readFrame();
        ReadBuffer in(frame);
        switch (in.u8()) {
        case cf::cs::FinishAcknowledgement:
            std::fprintf(stderr, "[cppfm] %s: finish ack at %.2f\n", self_->name.c_str(),
                         std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
            state_ = State::Play;
            onEnterPlay();
            std::fprintf(stderr, "[cppfm] %s: onEnterPlay done\n", self_->name.c_str());
            return;
        case cf::cs::KeepAlive: {
            WriteBuffer e; e.raw(in.p + in.off, in.remaining());
            conn_->sendPacket(cf::sc::KeepAlive, e);
            break;
        }
        case cf::cs::CustomPayload:
            (void)in.string(); in.skipRest();
            break;
        default:
            throw std::runtime_error("unexpected packet during finish-ack wait");
        }
    }
}

// ------------------------------------------------------------------ play join

void Session::onEnterPlay() {
    self_->conn = conn_.get();
    self_->entityId = srv_.nextEntityId();
    self_->lastSeenMs = nowMs();

    sendJoinGame();
    sendAbilities();

    {   // brand again in play phase (vanilla does both)
        WriteBuffer b;
        b.string("minecraft:brand");
        WriteBuffer p;
        p.string("CppFabricMC");
        b.raw(p.data.data(), p.data.size());
        conn_->sendPacket(pl::sc::CustomPayload, b);
    }
    {   // held slot 0
        WriteBuffer b; b.i8(0);
        conn_->sendPacket(pl::sc::SetHeldSlot, b);
    }
    {   // default spawn point
        WriteBuffer b;
        b.position(0, -60, 0);
        b.f32(0.f);
        conn_->sendPacket(pl::sc::SetDefaultSpawn, b);
    }
    sendTeleport(0.5, -60.0, 0.5, 0.f, 0.f);

    sendPlayerInfoAddSelf();
    // tell everyone about us / tell us about everyone
    broadcastPlayerInfoAdd(self_.get());
    for (auto& other : srv_.playersSnapshot()) {
        if (other.get() == self_.get()) continue;
        WriteBuffer add;
        add.u8(0x01 | 0x08);                       // add_player | update_listed
        add.varint(1);
        add.uuid(other->uuid.data());
        add.string(other->name);
        add.varint(0);                             // props
        add.varint(1);                             // listed
        conn_->sendPacket(pl::sc::PlayerInfoUpdate, add);
    }

    registered_ = true;
    srv_.addPlayer(self_);
    self_->inPlay = true;
    self_->gamemode = 1;   // creative default for building comfort
    self_->health = 20; self_->food = 20; self_->saturation = 5;
    self_->exhaustion = 0; self_->fallDist = 0; self_->dead = false;

    srv_.loadPlayerData(GameServer::uuidToHex(self_->uuid), *self_);
    // cookies from disk (plan3 Cookie persistence)
    if (!self_->cookies.empty()) {}                    // populated on demand
    self_->prevFeetY = self_->y;

    api::PlayerJoinEvent jev;
    jev.player = self_.get();
    srv_.events().join.fire(jev);
    srv_.initPlayerProgress(*self_);
    srv_.sendAdvancementsTo(*self_, true);

    broadcastSpawnEntity(self_.get());
    sendDeclareCommands();
    sendRecipeBook();
    srv_.sendSetExperience(*self_);

    sendStarterInventory();
    {   // health (creative ignores but harmless)
        WriteBuffer b;
        b.f32(20.f); b.varint(20); b.f32(5.f);
        conn_->sendPacket(pl::sc::SetHealth, b);
    }

    srv_.broadcastSystemText("\u00a7e" + self_->name + " joined the game", nullptr);
    sendSystemText("\u00a77Welcome to \u00a7bCppFabricMC\u00a77! Build with the hotbar, chat freely.");
}

static WriteBuffer makeWorldState(const ServerConfig& c) {
    WriteBuffer w;
    w.varint(0);                                   // dimension type index
    w.string("minecraft:overworld");
    w.i64(c.hashedSeed);
    w.i8(0);                                       // gamemode survival
    w.u8(255);                                     // previous gamemode: none
    w.boolean(false);                              // is debug
    w.boolean(true);                               // is flat
    w.boolean(false);                              // has death location
    w.varint(0);                                   // portal cooldown
    w.varint(kSeaLevelFlat);
    return w;
}

// Minimal command tree: root -> /help, /ping  (literals only)
static void writeDeclareCommands(WriteBuffer& b) {
    const char* literals[] = {"help", "ping"};
    b.varint(3);                       // node count
    // node0: root, children = {1,2}
    b.u8(0x00);
    b.varint(2); b.varint(1); b.varint(2);
    for (const char* name : literals) {
        b.u8(0x01 | 0x04);             // literal | executable
        b.varint(0);                   // no children
        b.string(name);
    }
    b.varint(0);                       // root index
}

void Session::sendDeclareCommands() {
    WriteBuffer b;
    writeDeclareCommands(b);
    conn_->sendPacket(pl::sc::DeclareCommands, b);
}

void Session::handleRespawnRequest() {
    self_->dead = false;
    self_->health = 20; self_->food = 20; self_->saturation = 5;
    self_->fallDist = 0;
    WriteBuffer ws = makeWorldState(srv_.config());
    WriteBuffer b;
    b.raw(ws.data.data(), ws.data.size());
    b.u8(0x03);                                    // keep metadata + attributes
    conn_->sendPacket(pl::sc::Respawn, b);
    {   // re-sync position & vitals
        WriteBuffer hp;
        hp.f32(20.f); hp.varint(20); hp.f32(5.f);
        conn_->sendPacket(pl::sc::SetHealth, hp);
    }
    sendTeleport(self_->x, -60.0, self_->z, self_->yaw, self_->pitch);
}

void Session::sendJoinGame() {
    const ServerConfig& c = srv_.config();
    WriteBuffer b;
    b.i32(self_->entityId);
    b.boolean(false);                              // hardcore
    b.varint(1);                                   // worlds[]
    b.string("minecraft:overworld");
    b.varint(c.maxPlayers);
    b.varint(c.viewDistance);
    b.varint(std::min(c.simulationDistance, 10));
    b.boolean(false);                              // reduced debug
    b.boolean(true);                               // respawn screen
    b.boolean(false);                              // do limited crafting
    // SpawnInfo
    {
        WriteBuffer ws = makeWorldState(c);
        b.raw(ws.data.data(), ws.data.size());
    }
    b.boolean(false);                              // enforces secure chat
    conn_->sendPacket(pl::sc::Login, b);
}

void Session::sendAbilities() {
    WriteBuffer b;
    b.i8(0x01 | 0x04 | 0x08);                      // invulnerable, allow flying, instant build
    b.f32(0.05f);
    b.f32(0.10f);
    conn_->sendPacket(pl::sc::Abilities, b);
}

void Session::sendTeleport(double x, double y, double z, float yaw, float pitch) {
    self_->x = x; self_->y = y; self_->z = z;
    self_->yaw = yaw; self_->pitch = pitch;
    WriteBuffer b;
    b.varint(++teleportId_);
    b.f64(x); b.f64(y); b.f64(z);
    b.f64(0); b.f64(0); b.f64(0);                  // velocity
    b.f32(yaw); b.f32(pitch);
    b.u32(0);                                      // relatives flags: absolute all
    conn_->sendPacket(pl::sc::PlayerPosition, b);
}

static WriteBuffer makeSpawnEntity(const Player& p) {
    WriteBuffer b;
    b.varint(p.entityId);
    b.uuid(p.uuid.data());
    b.varint(static_cast<std::int32_t>(gen::kPlayerEntityTypeId));
    b.f64(p.x); b.f64(p.y); b.f64(p.z);
    const auto toAngle = [](float deg) { return static_cast<std::uint8_t>(deg * 256.f / 360.f); };
    b.i8(static_cast<std::int8_t>(toAngle(p.pitch)));
    b.i8(static_cast<std::int8_t>(toAngle(p.yaw)));
    b.i8(static_cast<std::int8_t>(toAngle(p.yaw)));   // head pitch
    b.varint(0);                                      // object data
    b.i16(0); b.i16(0); b.i16(0);                     // velocity
    return b;
}

static void sendSkinMetadata(Player& to, std::int32_t entityId) {
    WriteBuffer md;
    md.varint(entityId);
    md.u8(17); md.u8(0);            // index 17, type byte
    md.u8(0x7F);                    // all skin layers on
    md.u8(255);                     // end
    try { to.conn->sendPacket(pl::sc::SetEntityMetadata, md); } catch (...) {}
}

void Session::broadcastSpawnEntity(Player* about) {
    WriteBuffer b = makeSpawnEntity(*about);
    if (getenv("CPPFM_TRACE"))
        std::fprintf(stderr, "[cppfm] spawn-broadcast of %s (eid=%d)\n",
                     about->name.c_str(), about->entityId);
    srv_.broadcastPacketExcept(about, pl::sc::SpawnEntity, b);
    sendSkinMetadata(*about, about->entityId);
    // also tell the newcomer about everyone else
    for (auto& other : srv_.playersSnapshot()) {
        if (other.get() == about || !other->inPlay) continue;
        WriteBuffer ob = makeSpawnEntity(*other);
        try {
            about->conn->sendPacket(pl::sc::SpawnEntity, ob);
            sendSkinMetadata(*about, other->entityId);
        } catch (...) {}
    }
}

void Session::sendPlayerInfoAddSelf() {
    WriteBuffer add;
    add.u8(0x01 | 0x04 | 0x08);                    // add_player | update_game_mode | update_listed
    add.varint(1);
    add.uuid(self_->uuid.data());
    add.string(self_->name);
    add.varint(0);                                 // properties
    add.varint(self_->gamemode);
    add.varint(1);                                 // listed
    conn_->sendPacket(pl::sc::PlayerInfoUpdate, add);
}

void Session::broadcastPlayerInfoAdd(Player* about) {
    WriteBuffer add;
    add.u8(0x01 | 0x04 | 0x08);
    add.varint(1);
    add.uuid(about->uuid.data());
    add.string(about->name);
    add.varint(0);
    add.varint(1);
    add.varint(1);
    srv_.broadcastPacketExcept(about, pl::sc::PlayerInfoUpdate, add);
}

void Session::sendStarterInventory() {
    // build inventory model from starter kit
    for (auto& s2 : self_->inv) { s2.itemId = 0; s2.count = 0; }
    {
        int hot = 36;
        for (auto& e : kKit) {
            auto ii = gen::itemIdByName().find(e.name);
            if (ii == gen::itemIdByName().end()) continue;
            if (hot < 45) { self_->inv[hot] = InvSlot::of(ii->second, static_cast<std::int16_t>(e.cnt)); ++hot; }
        }
    }
    WriteBuffer b;
    b.u8(0);                                       // window id: player inventory
    b.varint(++self_->invStateId);
    b.varint(46);                                  // slots
    for (int i = 0; i < 46; ++i) self_->inv[i].write(b);
    ItemStack::air().write(b);                     // carried item
    conn_->sendPacket(pl::sc::ContainerSetContent, b);
}


void Session::onWindowClick(ReadBuffer& in) {
    const auto windowId = in.u8();
    (void)in.varint();                                // stateId
    const auto slotIdx = in.i16();
    const auto button = in.i8();
    const auto mode = in.varint();

    // changed slots array (client prediction; we recompute server-side)
    const auto nChanged = in.varint();
    for (std::int32_t i = 0; i < nChanged; ++i) {
        (void)in.i16();
        ItemStack::read(in);
    }
    ItemStack clientCursor = ItemStack::read(in);
    (void)clientCursor;

    if (windowId != 0 && openMenu_ && openMenu_->windowId == windowId) {
        handleMenuClick(*openMenu_, slotIdx, button, mode);
        return;
    }
    if (windowId == 0) {
        // player-inventory clicks: trust the predicted slots, then resync.
        // (Full authoritative cursor handling lives in the menu path.)
        srv_.resendInventory(*self_);
    }
}

// ------------------------------------------------------- xp / effects / furnaces

void GameServer::sendSetExperience(Player& p) {
    WriteBuffer b;
    b.f32(p.xp.progress);
    b.varint(p.xp.level);
    b.varint(p.xp.totalXp);
    try { p.conn->sendPacket(pl::sc::SetExperience, b); } catch (...) {}
}

void GameServer::effectsTick() {
    for (auto& pp : playersSnapshot()) {
        auto* p = pp.get();
        if (!p->inPlay || p->effects.empty()) continue;
        bool changed = false;
        for (auto it = p->effects.begin(); it != p->effects.end();) {
            // instant effects apply once then vanish
            if (it->type == effects::InstantHealth && !it->expired()) {
                p->health = std::min(20.f, p->health + 4.f * (it->amplifier + 1));
                sendSetHealth(*p);
                it = p->effects.erase(it);
                changed = true;
                continue;
            }
            if (it->type == effects::InstantDamage && !it->expired()) {
                applyDamage(*p, 6.f * (it->amplifier + 1), "magic");
                it = p->effects.erase(it);
                changed = true;
                continue;
            }
            --it->durationTicks;
            if (it->expired()) {
                WriteBuffer b;
                b.varint(p->entityId);
                b.varint(it->type);
                try { p->conn->sendPacket(pl::sc::RemoveMobEffect, b); }
                catch (...) {}
                it = p->effects.erase(it);
                changed = true;
                continue;
            }
            // regeneration / poison style periodic damage & heal
            if (it->type == effects::Regeneration &&
                tickNo_ % std::max(1, 50 >> it->amplifier) == 0)
                p->health = std::min(20.f, p->health + 1.f), sendSetHealth(*p);
            if ((it->type == effects::Poison || it->type == effects::Wither) &&
                tickNo_ % std::max(1, 40 >> it->amplifier) == 0)
                applyDamage(*p, 1.f, it->type == effects::Poison ? "poison"
                                                                 : "wither");
            ++it;
        }
        (void)changed;
    }
}

void GameServer::furnacesTick() {
    const auto& items = gen::itemIdByName();
    blockEntities_.forEach([&](std::int64_t key, BlockEntity& be) {
        if (be.kind != BlockEntity::Kind::Furnace) return;
        FurnaceData& f = be.furnace;
        const std::int32_t x = posKeyUnpackX(key);
        const std::int32_t y = posKeyUnpackY(key);
        const std::int32_t z = posKeyUnpackZ(key);
        world_.generateChunkIfMissing(x >> 4, z >> 4);
        const std::uint16_t stateHere = world_.getBlock(x, y, z);

        // fuel consumption
        if (f.burnTicks > 0) --f.burnTicks;
        const Recipe* recipe =
            f.slots[FurnaceData::kInput].empty()
                ? nullptr
                : recipes_.findSmelting(f.slots[FurnaceData::kInput].itemId);
        const bool canSmelt =
            recipe && (!f.slots[FurnaceData::kOutput].empty() ||
                       true) /* output merge handled below */;
        if (f.burnTicks <= 0 && canSmelt && !f.slots[FurnaceData::kFuel].empty()) {
            const int ft = furnaceFuelTicks(f.slots[FurnaceData::kFuel].itemId);
            if (ft > 0) {
                f.burnDuration = static_cast<std::int16_t>(ft);
                f.burnTicks = f.burnDuration;
                ItemStack& fuel = f.slots[FurnaceData::kFuel];
                if (--fuel.count <= 0) fuel = ItemStack::air();
                blockEntities_.dirty_.insert(key);
            }
        }
        const bool burning = f.burnTicks > 0;
        if (canSmelt && burning) {
            if (++f.cookProgress >= f.cookTotal) {
                f.cookProgress = 0;
                auto out = recipe->result;
                auto& dst = f.slots[FurnaceData::kOutput];
                if (dst.empty()) dst = out;
                else if (dst.itemId == out.itemId) dst.count += out.count;
                else { f.cookProgress = f.cookTotal; return; }
                ItemStack& in = f.slots[FurnaceData::kInput];
                if (--in.count <= 0) in = ItemStack::air();
                blockEntities_.dirty_.insert(key);
                // xp orbs on manual collection only; skip here
            }
        } else {
            f.cookProgress = 0;
        }

        // lit-state block update (vanilla swaps furnace[lit=...])
        static const gen::BlockDef* fdef = gen::blockByName("minecraft:furnace");
        if (fdef && stateHere == fdef->defaultState || stateHere == 4351) {
            const std::uint16_t want = gen::stateWithPropsList("minecraft:furnace",
                {{"lit", burning ? "true" : "false"}});
            if (stateHere != want) {
                world_.setBlock(x, y, z, want);
                broadcastBlockChange(x, y, z, want);
            }
        }
        (void)items;
    });
}

void GameServer::spawnXpOrbs(double x, double y, double z, int totalPoints,
                             Player* directTo) {
    // split into vanilla-ish orb sizes
    static const int kSizes[] = {1, 3, 7, 17, 37, 73, 149, 307, 617, 1237};
    std::vector<int> orbs;
    while (totalPoints > 0) {
        int pick = 0;
        for (int i = 0; i < 10; ++i)
            if (kSizes[i] <= totalPoints) pick = i;
        if (pick == 0 && totalPoints < 1) break;
        const int v = kSizes[pick];
        orbs.push_back(std::min(v, totalPoints));
        totalPoints -= std::min(v, totalPoints);
        if (orbs.size() >= 16) break;                    // sanity cap
    }
    if (orbs.empty()) return;
    std::vector<std::shared_ptr<XpOrbEntity>> created;
    {
        std::lock_guard lk(entsMtx_);
        for (int v : orbs) {
            auto e = std::make_shared<XpOrbEntity>();
            e->entityId = nextEntityId();
            e->value = static_cast<std::uint16_t>(v);
            e->x = x + ((rand() % 5) - 2) * 0.1;
            e->y = y; e->z = z + ((rand() % 5) - 2) * 0.1;
            e->vy = 0.08;
            xpOrbs_.push_back(e);
            created.push_back(e);
        }
    }
    for (auto& e : created) {
        WriteBuffer b;
        b.varint(e->entityId);
        b.f64(e->x); b.f64(e->y); b.f64(e->z);
        b.i16(static_cast<std::int16_t>(e->value));
        broadcastPacketExcept(nullptr, pl::sc::SpawnExperienceOrb, b);
    }
}

void GameServer::xpOrbsTick() {
    struct Pickup { std::shared_ptr<XpOrbEntity> orb; Player* p; };
    std::vector<Pickup> pickups;
    {
        std::lock_guard lk(entsMtx_);
        for (auto it = xpOrbs_.begin(); it != xpOrbs_.end();) {
            auto& e = *it;
            ++e->ageTicks;
            if (e->ageTicks > 6000) { it = xpOrbs_.erase(it); continue; }
            e->vy -= 0.03; if (e->vy < -0.4) e->vy = -0.4;
            e->y += e->vy;
            world_.generateChunkIfMissing(static_cast<std::int32_t>(e->x)>>4,
                                   static_cast<std::int32_t>(e->z)>>4);
            int col=4;
            world_.withChunk(static_cast<std::int32_t>(e->x)>>4,
                      static_cast<std::int32_t>(e->z)>>4,[&](const Chunk& c){
                for (int ry=kSectionsPerChunk*16-1; ry>=0; --ry)
                    if (c.blocks[Chunk::index(ry>>4,ry&15,
                        static_cast<std::int32_t>(e->z)&15,
                        static_cast<std::int32_t>(e->x)&15)]!=0){col=ry+1;break;}
            });
            const double gy = kMinY + col + 0.25;
            if (e->y < gy) { e->y = gy; e->vy = 0; }
            if (e->ageTicks > 10) {
                for (auto& pp : playersSnapshot()) {
                    auto* pl = pp.get();
                    if (!pl->inPlay || pl->dead || pl->gamemode != 0) continue;
                    double dx=pl->x-e->x, dy=(pl->y+0.9)-e->y, dz=pl->z-e->z;
                    if (dx*dx+dy*dy+dz*dz < 2.5) { pickups.push_back({e, pl}); break; }
                }
            }
            ++it;
        }
    }
    for (auto& pk : pickups) {
        Player& p = *pk.p;
        p.xp.addPoints(pk.orb->value);
        sendSetExperience(p);
        WriteBuffer c;
        c.varint(pk.orb->entityId);
        c.varint(p.entityId);
        c.varint(1);
        broadcastPacketExcept(nullptr, pl::sc::Collect, c);
        WriteBuffer rm;
        rm.varint(1); rm.varint(pk.orb->entityId);
        broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, rm);
        std::lock_guard lk(entsMtx_);
        xpOrbs_.erase(std::remove_if(xpOrbs_.begin(), xpOrbs_.end(),
            [&](const std::shared_ptr<XpOrbEntity>& x){
                return x.get()==pk.orb.get(); }),
            xpOrbs_.end());
    }
}

void GameServer::spawnProjectile(ProjectileKind kind, double x, double y,
                                 double z, double vx, double vy, double vz,
                                 std::int32_t ownerId, bool ownerIsPlayer) {
    auto e = std::make_shared<ProjectileEntity>();
    e->entityId = nextEntityId();
    e->kind = kind;
    e->x = x; e->y = y; e->z = z;
    e->vx = vx; e->vy = vy; e->vz = vz;
    e->ownerId = ownerId;
    e->ownerIsPlayer = ownerIsPlayer;
    projectiles_.push_back(e);
    {
        std::lock_guard lk(entsMtx_);
        // (kept consistent with other spawn paths)
    }
    const auto& types = gen::entityTypeIdByName();
    static const char* kNames[] = {"minecraft:arrow", "minecraft:snowball",
                                   "minecraft:egg", "minecraft:ender_pearl"};
    auto ti = types.find(kNames[static_cast<int>(kind)]);
    WriteBuffer b;
    b.varint(e->entityId);
    std::uint8_t zero[16] = {};
    b.uuid(zero);
    b.varint(ti != types.end() ? static_cast<std::int32_t>(ti->second) : 0);
    b.f64(x); b.f64(y); b.f64(z);
    b.i8(0); b.i8(0); b.i8(0);
    b.varint(1);                                        // objectData: velocity
    b.i16(static_cast<std::int16_t>(vx * 8000));
    b.i16(static_cast<std::int16_t>(vy * 8000));
    b.i16(static_cast<std::int16_t>(vz * 8000));
    broadcastPacketExcept(nullptr, pl::sc::SpawnEntity, b);
}

void GameServer::projectilesTick() {
    struct Hit { std::shared_ptr<ProjectileEntity> p; Player* player; std::shared_ptr<MobEntity> mob; float dmg; };
    std::vector<Hit> hits;
    std::vector<std::int32_t> despawn;
    {
        for (auto it = projectiles_.begin(); it != projectiles_.end();) {
            auto& pr = *it;
            ++pr->ageTicks;
            if (pr->ageTicks > 1200 || pr->stuck && pr->ageTicks > 600 + 1200) {
                despawn.push_back(pr->entityId);
                it = projectiles_.erase(it);
                continue;
            }
            if (!pr->stuck) {
                const double g = pr->kind == ProjectileKind::Arrow ? 0.05 : 0.03;
                pr->vy -= g;
                pr->x += pr->vx; pr->y += pr->vy; pr->z += pr->vz;
                world_.generateChunkIfMissing(
                    static_cast<std::int32_t>(pr->x) >> 4,
                    static_cast<std::int32_t>(pr->z) >> 4);
                // block collision
                if (world_.getBlock(static_cast<std::int32_t>(pr->x),
                                    static_cast<std::int32_t>(pr->y),
                                    static_cast<std::int32_t>(pr->z)) != 0) {
                    if (pr->kind == ProjectileKind::Arrow) pr->stuck = true;
                    else { despawn.push_back(pr->entityId);
                           it = projectiles_.erase(it); continue; }
                } else {
                    // entity collision
                    bool hitSomething = false;
                    for (auto& pp : playersSnapshot()) {
                        if (pr->ownerIsPlayer && pp->entityId == pr->ownerId)
                            continue;
                        if (pp->dead || !pp->inPlay) continue;
                        const double dx = pp->x - pr->x;
                        const double dy = pp->y + 0.9 - pr->y;
                        const double dz = pp->z - pr->z;
                        if (dx*dx + dy*dy + dz*dz < 0.55) {
                            const float base =
                                pr->kind == ProjectileKind::Arrow ? 6.f : 0.f;
                            const float dmg = base *
                                static_cast<float>(std::min(
                                    1.0, std::sqrt(pr->vx*pr->vx +
                                                   pr->vy*pr->vy +
                                                   pr->vz*pr->vz) / 2.0));
                            if (dmg > 0)
                                hits.push_back({pr, pp.get(), nullptr, dmg});
                            hitSomething = true;
                            break;
                        }
                    }
                    if (!hitSomething) {
                        std::lock_guard lk(entsMtx_);
                        for (auto& m : mobs_) {
                            if (!pr->ownerIsPlayer &&
                                m->entityId == pr->ownerId) continue;
                            const double dx = m->x - pr->x;
                            const double dy = m->y + 0.8 - pr->y;
                            const double dz = m->z - pr->z;
                            if (dx*dx + dy*dy + dz*dz < 0.55) {
                                const float dmg = 5.f;
                                hits.push_back({pr, nullptr, m, dmg});
                                hitSomething = true;
                                break;
                            }
                        }
                    }
                    if (hitSomething) {
                        despawn.push_back(pr->entityId);
                        it = projectiles_.erase(it);
                        continue;
                    }
                }
            }
            ++it;
        }
    }
    for (auto& h : hits) {
        if (h.player) {
            applyDamage(*h.player, h.dmg, "arrow");
            WriteBuffer de;
            de.varint(h.player->entityId);
            const auto dtid = gameData_.idOf("minecraft:damage_type",
                                             "minecraft:arrow");
            de.varint(dtid >= 0 ? dtid : 0);
            de.varint(0); de.varint(0);
            de.boolean(false);
            try { h.player->conn->sendPacket(pl::sc::DamageEvent, de); }
            catch (...) {}
        } else if (h.mob) {
            applyDamageToMob(*h.mob, h.dmg, "arrow");
            if (h.mob->dead) {
                WriteBuffer rm; rm.varint(1); rm.varint(h.mob->entityId);
                broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, rm);
                const auto drop = MobEntity::dropFor(h.mob->kind);
                if (drop.itemId)
                    spawnItemDrop(h.mob->x, h.mob->y + .4, h.mob->z,
                                  drop.itemId, drop.count);
                std::lock_guard lk(entsMtx_);
                mobAi_.erase(h.mob->entityId);
                mobs_.erase(std::remove(mobs_.begin(), mobs_.end(), h.mob),
                            mobs_.end());
            }
        }
    }
    for (auto id : despawn) {
        WriteBuffer rm; rm.varint(1); rm.varint(id);
        broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, rm);
    }
}

bool GameServer::spawnMobByTypeName(const std::string& name, double x, double y,
                                    double z) {    MobKind kind;
    if (name == "minecraft:pig") kind = MobKind::Pig;
    else if (name == "minecraft:cow") kind = MobKind::Cow;
    else if (name == "minecraft:sheep") kind = MobKind::Sheep;
    else if (name == "minecraft:chicken") kind = MobKind::Chicken;
    else if (name == "minecraft:zombie") kind = MobKind::Zombie;
    else if (name == "minecraft:creeper") kind = MobKind::Creeper;
    else if (name == "minecraft:skeleton") kind = MobKind::Skeleton;
    else if (name == "minecraft:spider") kind = MobKind::Spider;
    else return false;
    spawnMob(kind, x, y, z);
    return true;
}

// ------------------------------------------------------------- session io

void Session::onTabComplete(ReadBuffer& in) {
    const auto transactionId = in.varint();
    const std::string text = in.string(65536);
    (void)in.boolean();                               // assume command

    brigadier::CommandContext ctx;
    ctx.source.player = self_.get();
    ctx.source.name = self_->name;
    ctx.source.console = false;
    ctx.srcX = self_->x; ctx.srcY = self_->y; ctx.srcZ = self_->z;
    for (auto& p : srv_.playersSnapshot())
        if (p.get() != self_.get()) ctx.playerNames.push_back(p->name);
    ctx.resolveSelector = [this](const std::string& raw,
                                 brigadier::SelectorResult& out) {
        out = srv_.resolveSelector(raw, self_.get());
    };

    const auto suggestions = srv_.commands().suggest(text, [&]{
        brigadier::CommandSource s;
        s.player = self_.get(); s.name = self_->name; s.console = false;
        return s;
    }());

    WriteBuffer b;
    b.varint(transactionId);
    b.varint(0);                                      // start of range
    b.varint(static_cast<std::int32_t>(text.size())); // length replaced
    b.varint(static_cast<std::int32_t>(suggestions.size()));
    for (auto& [match, tooltip] : suggestions) {
        b.string(match);
        b.boolean(false);
    }
    try { conn_->sendPacket(pl::sc::CommandSuggestions, b); } catch (...) {}
}



void Session::sendSetSlot(std::int32_t windowId, std::int32_t stateId,
                          std::int16_t slot, const ItemStack& s) {
    WriteBuffer b;
    b.i8(static_cast<std::int8_t>(windowId));
    b.varint(stateId);
    b.i16(slot);
    s.write(b);
    try { conn_->sendPacket(pl::sc::ContainerSetSlot, b); } catch (...) {}
}

void Session::syncCursorItem() {
    WriteBuffer b;
    cursorItem_.write(b);
    try { conn_->sendPacket(pl::sc::SetCursorItem, b); } catch (...) {}
}

namespace {
struct SessionMenuIo : MenuIo {
    Session& s;
    explicit SessionMenuIo(Session& ss) : s(ss) {}
    void dropFromPlayer(Player& p, const ItemStack& stack, bool whole) override {
        s.server().spawnItemDrop(p.x, p.y + 1.2, p.z,
                                 stack.itemId, static_cast<std::uint8_t>(
                                     whole ? stack.count : 1),
                                 0, 0.15, 0);
    }
    void blockEntityChanged(std::int64_t key) override {
        s.server().blockEntities().dirty_.insert(key);
    }
    void itemCrafted(Player& p, const ItemStack& result) override {
        s.server().onItemObtained(p, result, "crafted");
    }
    void itemSmelted(Player& p, const ItemStack& result) override {
        s.server().onItemObtained(p, result, "smelted");
    }
};
} // namespace

void Session::handleMenuClick(Menu& m, int slot, int button, int mode) {
    SessionMenuIo io(*this);
    // crafting result refresh before interaction
    m.refreshCraftResult(srv_.recipes());
    const bool changed = ClickLogic::apply(m, *self_, srv_.recipes(),
                                           slot, button, mode, cursorItem_, io);
    if (m.type == MenuType::Crafting) m.refreshCraftResult(srv_.recipes());
    sendMenuContent(m);
    syncCursorItem();
    (void)changed;
}

void Session::sendMenuContent(Menu& m) {
    WriteBuffer b;
    b.u8(static_cast<std::uint8_t>(m.windowId));
    b.varint(++self_->invStateId);
    b.varint(m.totalSlots());
    for (int i = 0; i < m.totalSlots(); ++i) {
        ItemStack* s = m.slotAt(i, self_->inv.data());
        if (s) s->write(b);
        else ItemStack::air().write(b);
    }
    cursorItem_.write(b);
    try { conn_->sendPacket(pl::sc::ContainerSetContent, b); } catch (...) {}
}

void Session::openMenuAt(std::int32_t x, std::int32_t y, std::int32_t z,
                         std::uint16_t stateOfBlock) {
    using BD = cppfm::gen::BlockDef;
    const gen::BlockDef* def = gen::blockByState(stateOfBlock);
    if (!def) return;
    const std::string name(def->name);

    auto menu = std::make_unique<Menu>();
    menu->windowId = ++menuWindowCounter_;
    menu->blockKey = posKey(x, y, z);

    if (name.find("chest") != std::string::npos &&
        name.find("ender") == std::string::npos) {
        auto* be = srv_.blockEntities().getAt(x, y, z);
        if (!be)
            be = &srv_.blockEntities().create(menu->blockKey,
                                              BlockEntity::Kind::Chest);
        menu->type = MenuType::Chest;
        menu->container = be->chest.slots;
        menu->containerCount = ChestData::kSlots;
        menu->blockEntity = be;
    } else if (name == "minecraft:hopper" || name == "minecraft:dispenser" ||
               name == "minecraft:dropper") {
        auto* be = srv_.blockEntities().getAt(x, y, z);
        const bool hopper = name == "minecraft:hopper";
        if (!be)
            be = &srv_.blockEntities().create(menu->blockKey,
                hopper ? BlockEntity::Kind::Hopper
                       : BlockEntity::Kind::Dispenser);
        menu->type = hopper ? MenuType::Hopper : MenuType::Dispenser;
        menu->container = be->generic.slots;
        menu->containerCount_ = hopper ? 5 : 9;
        menu->containerCount = menu->containerCount_;
        menu->blockEntity = be;
    } else if (name == "minecraft:furnace") {
        auto* be = srv_.blockEntities().getAt(x, y, z);
        if (!be)
            be = &srv_.blockEntities().create(menu->blockKey,
                                              BlockEntity::Kind::Furnace);
        menu->type = MenuType::Furnace;
        menu->container = be->furnace.slots;
        menu->containerCount = 3;
        menu->blockEntity = be;
    } else if (name == "minecraft:crafting_table") {
        menu->type = MenuType::Crafting;
    } else return;

    // Open Screen packet
    {
        WriteBuffer b;
        b.varint(menu->windowId);
        b.varint(menu->openScreenTypeId());
        nbt::writeTextComponent(
            b, menu->type == MenuType::Chest ? "Chest"
               : menu->type == MenuType::Furnace ? "Furnace" : "Crafting");
        conn_->sendPacket(pl::sc::OpenScreen, b);
    }
    openMenu_ = std::move(menu);
    openMenu_->refreshCraftResult(srv_.recipes());
    sendMenuContent(*openMenu_);
}

void Session::closeOpenMenu(bool sendPacketToClient) {
    if (!openMenu_) return;
    // return crafting-grid contents to the player (or drop when full)
    if (openMenu_->type == MenuType::Crafting) {
        for (auto& s : openMenu_->craftGrid) {
            if (s.empty()) continue;
            if (!srv_.addToInventory(*self_, s.itemId, s.count))
                srv_.spawnItemDrop(self_->x, self_->y + 0.5, self_->z,
                                   s.itemId, static_cast<std::uint8_t>(s.count),
                                   0, 0.1, 0);
            s = ItemStack::air();
        }
        if (!cursorItem_.empty()) {
            if (!srv_.addToInventory(*self_, cursorItem_.itemId, cursorItem_.count))
                srv_.spawnItemDrop(self_->x, self_->y + 0.5, self_->z,
                                   cursorItem_.itemId,
                                   static_cast<std::uint8_t>(cursorItem_.count),
                                   0, 0.1, 0);
            cursorItem_ = ItemStack::air();
        }
    }
    openMenu_.reset();
    if (sendPacketToClient) {
        WriteBuffer b;
        b.u8(0);
        try { conn_->sendPacket(pl::sc::CloseContainer, b); } catch (...) {}
    }
}

void Session::onCloseContainer() {
    closeOpenMenu(false);
    syncCursorItem();
}

// ------------------------------------------------------- recipe book sync

void Session::sendRecipeBook() {
    // settings: 8 booleans (gui open / filtering per station)
    {
        WriteBuffer b;
        for (int i = 0; i < 8; ++i) b.boolean(false);
        conn_->sendPacket(pl::sc::RecipeBookSettings, b);
    }
    const auto& all = srv_.recipes().all();
    WriteBuffer b;
    b.varint(static_cast<std::int32_t>(all.size()));
    std::int32_t displayId = 0;
    const auto tableItem = gen::itemIdByName().at("minecraft:crafting_table");
    const auto furnaceItem = gen::itemIdByName().at("minecraft:furnace");
    for (const auto& r : all) {
        // entry: {recipe:{displayId,display,group,category,requirements?},flags}
        b.varint(displayId);
        auto writeSlotDisplayItem = [&](std::uint32_t itemId) {
            b.varint(itemId ? 2 : 0);          // item display | empty
            if (itemId) b.varint(static_cast<std::int32_t>(itemId));
        };

        switch (r.kind) {
        case Recipe::Kind::Shaped:
            b.varint(1);                       // crafting_shaped
            b.varint(r.width);
            b.varint(r.height);
            b.varint(static_cast<std::int32_t>(r.cells.size()));
            for (auto& ing : r.cells)
                writeSlotDisplayItem(ing.items.empty()
                                         ? 0 : *ing.items.begin());
            writeSlotDisplayItem(r.result.itemId);
            writeSlotDisplayItem(tableItem);   // craftingStation
            break;
        case Recipe::Kind::Shapeless: {
            b.varint(0);                       // crafting_shapeless
            b.varint(static_cast<std::int32_t>(r.ingredients.size()));
            for (auto& ing : r.ingredients)
                writeSlotDisplayItem(ing.items.empty()
                                         ? 0 : *ing.items.begin());
            writeSlotDisplayItem(r.result.itemId);
            writeSlotDisplayItem(tableItem);
            break;
        }
        case Recipe::Kind::Smelting: {
            b.varint(2);                       // furnace
            writeSlotDisplayItem(r.cells.front().items.empty()
                                     ? 0 : *r.cells.front().items.begin());
            writeSlotDisplayItem(
                gen::itemIdByName().at("minecraft:coal"));   // fuel
            writeSlotDisplayItem(r.result.itemId);
            writeSlotDisplayItem(furnaceItem); // station
            b.varint(r.cookingTicks);
            b.f32(r.experience);
            break;
        }
        case Recipe::Kind::Stonecutting: {
            b.varint(3);                       // stonecutter
            writeSlotDisplayItem(r.cells.front().items.empty()
                                     ? 0 : *r.cells.front().items.begin());
            writeSlotDisplayItem(r.result.itemId);
            writeSlotDisplayItem(furnaceItem);
            break;
        }
        }
        b.varint(0);                           // group: none
        b.varint(r.kind == Recipe::Kind::Smelting ? 6 : r.kind ==
                  Recipe::Kind::Stonecutting ? 10 : 3);   // category
        b.boolean(false);                      // craftingRequirements absent
        b.u8(0x03);                            // notification | highlight
        ++displayId;
    }
    b.boolean(true);                           // replace=true
    try { conn_->sendPacket(pl::sc::RecipeBookAdd, b); } catch (...) {}
}

// Place-recipe: fill the crafting grid from inventory for recipe `recipeId`
// (index into RecipeManager::all()).
void Session::handlePlaceRecipe(std::int32_t recipeId, bool makeAll) {
    if (!openMenu_ || openMenu_->type != MenuType::Crafting) return;
    Menu& m = *openMenu_;
    const auto& all = srv_.recipes().all();
    if (recipeId < 0 || static_cast<std::size_t>(recipeId) >= all.size()) return;
    const Recipe& r = all[static_cast<std::size_t>(recipeId)];

    // return current grid contents to inventory first
    for (auto& s : m.craftGrid) {
        if (!s.empty()) {
            srv_.addToInventory(*self_, s.itemId, s.count);
            s = ItemStack::air();
        }
    }
    auto take = [&](const Ingredient& ing) -> ItemStack {
        for (auto& s : self_->inv) {
            if (!s.empty() && ing.accepts(s.itemId)) {
                ItemStack one = ItemStack::of(s.itemId, 1);
                if (--s.count <= 0) s = ItemStack::air();
                return one;
            }
        }
        return ItemStack::air();
    };
    bool complete = true;
    if (r.kind == Recipe::Kind::Shaped) {
        for (int y = 0; y < r.height && complete; ++y)
            for (int x = 0; x < r.width && complete; ++x) {
                const auto& ing = r.cells[static_cast<std::size_t>(y) *
                                          r.width + x];
                if (ing.empty()) continue;
                ItemStack it2 = take(ing);
                if (it2.empty()) { complete = false; break; }
                m.craftGrid[static_cast<std::size_t>(y) * 3 + x] = it2;
            }
    } else if (r.kind == Recipe::Kind::Shapeless) {
        int i = 0;
        for (const auto& ing : r.ingredients) {
            if (i >= 9) break;
            ItemStack it2 = take(ing);
            if (it2.empty()) { complete = false; break; }
            m.craftGrid[i++] = it2;
        }
    } else complete = false;
    if (!complete) {
        // give back whatever we pulled
        for (auto& s : m.craftGrid)
            if (!s.empty()) {
                srv_.addToInventory(*self_, s.itemId, s.count);
                s = ItemStack::air();
            }
    }
    m.refreshCraftResult(srv_.recipes());
    srv_.resendInventory(*self_);
    sendMenuContent(m);
    syncCursorItem();
    (void)makeAll;
}

// ------------------------------------------------------------- cookies ----

void GameServer::storeCookie(const std::array<std::uint8_t, 16>& uuid,
                             const std::string& key,
                             const std::vector<std::uint8_t>& value) {
    try {
        const std::string dir = cfg_.worldDir + "/data/cookies/" + uuidToHex(uuid);
        std::filesystem::create_directories(dir + "/../.." );
        std::filesystem::create_directories(dir.substr(0, dir.find_last_of('/')));
        // sanitize key into a file name
        std::string safe = key;
        for (auto& c : safe)
            if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
        std::ofstream f(dir + "/" + safe, std::ios::binary);
        f.write(reinterpret_cast<const char*>(value.data()),
                static_cast<std::streamsize>(value.size()));
    } catch (...) {}
}

void GameServer::eraseCookie(const std::array<std::uint8_t, 16>& uuid,
                             const std::string& key) {
    std::string safe = key;
    for (auto& c : safe)
        if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
    std::error_code ec;
    std::filesystem::remove(cfg_.worldDir + "/data/cookies/" +
                            uuidToHex(uuid) + "/" + safe, ec);
}

std::vector<std::uint8_t> GameServer::loadCookie(
    const std::array<std::uint8_t, 16>& uuid, const std::string& key) {
    std::string safe = key;
    for (auto& c : safe)
        if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
    std::ifstream f(cfg_.worldDir + "/data/cookies/" + uuidToHex(uuid) + "/" + safe,
                    std::ios::binary);
    if (!f) return {};
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
}

bool GameServer::requestCookie(Player& p, const std::string& key) {
    if (!p.conn) return false;
    WriteBuffer b;
    b.string(key);
    try { p.conn->sendPacket(proto::pl::sc::CookieRequest, b); } catch (...) {}
    return true;
}

// ------------------------------------------------------- plugin channels

void Session::onPluginPayload(const std::string& channel,
                              const api::ChannelRegistry::Payload& body,
                              int phase) {
    if (channel == "minecraft:register") {
        // NUL-separated channel list
        std::string joined(body.begin(), body.end());
        std::size_t start = 0;
        while (start <= joined.size()) {
            auto end = joined.find('\0', start);
            if (end == std::string::npos) end = joined.size();
            if (end > start)
                self_->clientChannels.insert(joined.substr(start, end - start));
            start = end + 1;
        }
        return;
    }
    if (channel == "minecraft:unregister") {
        std::string joined(body.begin(), body.end());
        self_->clientChannels.erase(joined);
        return;
    }
    api::ChannelRegistry::get().dispatch(phase, channel, body);
}

void Session::sendPluginPayload(int phase, const std::string& channel,
                                const std::vector<std::uint8_t>& body) {
    WriteBuffer b;
    b.string(channel);
    b.raw(body.data(), body.size());
    const std::uint8_t id = phase == 0 ? cf::sc::CustomPayload
                                       : pl::sc::CustomPayload;
    try { conn_->sendPacket(id, b); } catch (...) {}
}

void Session::sendSystemText(const std::string& text) {
    WriteBuffer body;
    nbt::writeTextComponent(body, text);
    body.boolean(false);
    conn_->sendPacket(pl::sc::SystemChat, body);
}

// ------------------------------------------------------------------ chunking

void Session::sendChunk(std::int32_t cx, std::int32_t cz) {
    static const std::uint32_t biomeIdx = srv_.data().biomeIndex(srv_.config().worldBiome);
    GameServer::ChunkBodyRef body;
    if (!srv_.getCachedChunk(cx, cz, biomeIdx, body)) {
        auto fresh = std::make_shared<const std::vector<std::uint8_t>>([&]{
            WriteBuffer wb;
            srv_.world().generateChunkIfMissing(cx, cz);
            srv_.world().withChunk(cx, cz, [&](const Chunk& c) {
                serializeLevelChunkBody(wb, cx, cz, c, biomeIdx);
            });
            return wb.data;
        }());
        srv_.storeChunk(cx, cz, 0, fresh);
        body = fresh;
    }
    conn_->sendPacketBuf(pl::sc::LevelChunkWithLight, *body);
    sentChunks_.insert(chunkKey(cx, cz));
}

void Session::streamInitialChunks() {
    std::fprintf(stderr, "[cppfm] %s: streaming initial chunks\n", self_->name.c_str());
    chunksStreamed_ = true;
    tickChunksAround(self_->x, self_->z);
}

void Session::tickChunksAround(double px, double pz) {
    const int vd = std::min(srv_.config().viewDistance, 12);
    const std::int32_t pcx = static_cast<std::int32_t>(std::floor(px)) >> 4;
    const std::int32_t pcz = static_cast<std::int32_t>(std::floor(pz)) >> 4;

    if (pcx != lastCx_ || pcz != lastCz_) {
        WriteBuffer center;
        center.varint(pcx);
        center.varint(pcz);
        try { conn_->sendPacket(pl::sc::SetCenterChunk, center); } catch (...) {}
        lastCx_ = pcx; lastCz_ = pcz;
    }

    // collect missing chunks in view, sorted by distance to player chunk
    std::vector<std::pair<std::int64_t, std::pair<std::int32_t,std::int32_t>>> todo;
    for (std::int32_t dz = -vd; dz <= vd; ++dz)
        for (std::int32_t dx = -vd; dx <= vd; ++dx) {
            const std::int32_t cx = pcx + dx, cz = pcz + dz;
            const std::int64_t k = chunkKey(cx, cz);
            if (!sentChunks_.count(k)) todo.emplace_back(
                static_cast<std::int64_t>(dx) * dx + static_cast<std::int64_t>(dz) * dz,
                std::make_pair(cx, cz));
        }
    std::sort(todo.begin(), todo.end());

    if (!todo.empty()) {
        try {
            conn_->sendPacket(pl::sc::ChunkBatchStart, {});
            for (auto& t : todo) sendChunk(t.second.first, t.second.second);
            WriteBuffer fin;
            fin.varint(static_cast<std::int32_t>(todo.size()));
            conn_->sendPacket(pl::sc::ChunkBatchFinished, fin);
        } catch (...) {}
    }

    // forget distant chunks
    std::vector<std::int64_t> forget;
    for (auto k : sentChunks_) {
        const std::int32_t cx = static_cast<std::int32_t>(k >> 32);
        const std::int32_t cz = static_cast<std::int32_t>(k & 0xFFFFFFFFLL);
        if (std::abs(cx - pcx) > vd + 1 || std::abs(cz - pcz) > vd + 1)
            forget.push_back(k);
    }
    if (!forget.empty()) {
        for (auto k : forget) {
            WriteBuffer f;
            f.i32(static_cast<std::int32_t>(k & 0xFFFFFFFFLL));   // z first per schema!
            f.i32(static_cast<std::int32_t>(k >> 32));
            try { conn_->sendPacket(pl::sc::ForgetLevelChunk, f); } catch (...) {}
            sentChunks_.erase(k);
        }
    }
}

// ------------------------------------------------------------------ play loop

void Session::ack(std::int32_t sequence) {
    WriteBuffer b;
    b.varint(sequence);
    conn_->sendPacket(pl::sc::AckBlockChange, b);
}

void Session::handlePlay() {
    for (;;) {
        auto frame = conn_->readFrame();
        ReadBuffer in(frame);
        self_->lastSeenMs = nowMs();
        switch (in.u8()) {
        case pl::cs::AcceptTeleportation: {
            in.varint();
            self_->spawned = true;
            if (!chunksStreamed_) streamInitialChunks();
            break;
        }
        case pl::cs::MovePlayerPos:       onMovement(in, true, false); break;
        case pl::cs::MovePlayerPosRot:    onMovement(in, true, true);  break;
        case pl::cs::MovePlayerRot:       onMovement(in, false, true); break;
        case pl::cs::MovePlayerStatusOnly:onMovement(in, false, false);break;
        case pl::cs::KeepAlive: {
            // Client's response: just clear the pending flag. Sending anything
            // here creates an infinite keepalive ping-pong.
            const std::int64_t id = in.i64();
            if (self_->pendingKeepAlive == 0 || id == self_->pendingKeepAlive)
                self_->pendingKeepAlive = 0;
            break;
        }
        case pl::cs::ChatMessage:         onChatMessage(in); break;
        case pl::cs::ChatCommandSigned: {             // signed command: parse
            const std::string cmd = in.string(256);
            (void)in.i64(); (void)in.i64();
            if (in.boolean()) in.bytes(256);
            // argument signatures list
            const auto n = in.varint();
            for (std::int32_t q = 0; q < n; ++q) {
                (void)in.string(16);
                if (in.boolean()) {
                    const auto len = in.varint();
                    in.bytes(static_cast<std::size_t>(len));
                }
            }
            (void)in.varint();                        // offset
            in.bytes(3 * 20);                         // lastSeen acknowledgements
            dispatchCommand(cmd);
            break;
        }
        case pl::cs::ChatSessionUpdate: {             // plan3 Chat signing
            self_->chatPubKey.clear();
            std::array<std::uint8_t, 16> sid{};
            auto sb = in.bytes(16);
            std::copy(sb.begin(), sb.end(), sid.begin());
            self_->chatSessionExpiry = in.i64();
            const auto pkLen = in.varint();
            self_->chatPubKey = in.bytes(static_cast<std::size_t>(pkLen));
            const auto sigLen = in.varint();
            in.bytes(static_cast<std::size_t>(sigLen));
            self_->hasChatSession = pkLen > 0;
            break;
        }
        case pl::cs::MessageAck: in.skipRest(); break;
        case pl::cs::CookieResponse: {                // plan3 Cookie
            const std::string key = in.string(256);
            if (in.boolean()) {
                const auto len = in.varint();
                self_->cookies[key] =
                    in.bytes(static_cast<std::size_t>(len));
                srv_.storeCookie(self_->uuid, key, self_->cookies[key]);
            } else {
                srv_.eraseCookie(self_->uuid, key);
            }
            break;
        }
        case pl::cs::CustomPayload: {                 // plugin messaging API
            const std::string channel = in.string(256);
            api::ChannelRegistry::Payload body(
                in.p + in.off, in.p + in.len);
            onPluginPayload(channel, body, 1);
            break;
        }
        case pl::cs::UseEntity:           onUseEntity(in); break;
        case pl::cs::ChatCommand:         onChatCommand(in); break;
        case pl::cs::PlayerAction:        onPlayerAction(in); break;
        case pl::cs::UseItemOn:           onUseItemOn(in); break;
        case pl::cs::UseItem:             onUseItem(in); break;
        case pl::cs::HeldItemSlot:        onHeldSlot(in); break;
        case pl::cs::WindowClick:         onWindowClick(in); break;   // 0x10
        case pl::cs::CloseContainer:      onCloseContainer(); break;  // 0x11
        case pl::cs::PlaceRecipe: {                                   // 0x25
            (void)in.u8();                     // windowId
            const auto recipeId = in.varint();
            const auto makeAll = in.boolean();
            handlePlaceRecipe(recipeId, makeAll);
            break;
        }
        case pl::cs::TabComplete:         onTabComplete(in); break;
        case pl::cs::SelectTrade: {                                   // 0x31
            const auto idx = in.varint();
            if (tradingVillager_ >= 0) srv_.selectTrade(*self_, idx);
            break;
        }
        case pl::cs::ChunkBatchReceived:  in.f32(); break;
        case pl::cs::PingRequest: {
            const std::int64_t id = in.i64();
            WriteBuffer b; b.i64(id);
            conn_->sendPacket(0x38 /*ping response*/, b);
            break;
        }
        case pl::cs::ClientTickEnd: break;
        case pl::cs::PlayerLoaded:                    // 0x2a
            if (!chunksStreamed_) streamInitialChunks();
            break;
        case pl::cs::Swing: break;
        case pl::cs::SetCreativeModeSlot: {
            const std::int16_t slot = in.i16();
            const auto stack = ItemStack::read(in);
            if (slot >= 0 && slot < 46) {
                self_->inv[slot] = stack;
                // keep other viewers in sync if needed (no-op for single)
            } else if (slot == -1 && stack.empty()) {
                // cursor clear - ignore
            }
            break;
        }
        case pl::cs::SetDifficulty: (void)in.u8(); break;
        case pl::cs::ClientCommand: {
            const std::int32_t action = in.varint();
            if (action == 0) handleRespawnRequest();
            break;
        }
        case pl::cs::PlayerInput: in.skipRest(); break;
        case pl::cs::MoveVehicle: in.skipRest(); break;
        case pl::cs::SignUpdate: in.skipRest(); break;
        default:
            // Unknown packets: skip payload to stay aligned, but log loudly.
            std::fprintf(stderr, "[cppfm] unknown play packet from %s\n",
                         conn_->peer().c_str());
            in.skipRest();
            break;
        }
    }
}

void Session::onMovement(ReadBuffer& in, bool hasPos, bool hasRot) {
    const double oldX = self_->x, oldZ = self_->z;
    if (hasPos) {
        const double nx = in.f64(), ny = in.f64(), nz = in.f64();
        if (!self_->onGround && ny < self_->y && self_->gamemode == 0)
            self_->fallDist += self_->y - ny;       // descending while airborne
        self_->x = nx; self_->y = ny; self_->z = nz;
    }
    if (hasRot) {
        self_->yaw = in.f32();
        self_->pitch = in.f32();
    }
    const bool nowGround = in.boolean();
    if (hasPos) {
        if (self_->y < -2048.0 || self_->y > 2048.0)
            throw std::runtime_error("player moved out of world bounds");
        // landing
        if (nowGround && !self_->onGround) {
            if (getenv("CPPFM_TRACE"))
                std::fprintf(stderr, "[cppfm] %s landed fallDist=%.2f gm=%u\n",
                             self_->name.c_str(), self_->fallDist, self_->gamemode);
            if (self_->fallDist > 3.0)
                srv_.applyDamage(*self_, static_cast<float>(std::floor(self_->fallDist - 3.0)),
                            "fell from a high place");
            self_->fallDist = 0;
        }
        if (nowGround) self_->fallDist = 0;
        // exhaustion from horizontal movement
        const double hdx = self_->x - oldX, hdz = self_->z - oldZ;
        self_->exhaustion += std::sqrt(hdx*hdx + hdz*hdz) * 0.01;
        self_->spawned = true;
        if (!chunksStreamed_) streamInitialChunks();
        else tickChunksAround(self_->x, self_->z);
    }
    self_->onGround = nowGround;
    broadcastMovement();
    // portal step-in teleport (plan5)
    {
        if (srv_.tickNow() > self_->portalCooldownUntilTick) {
            World& curW = srv_.worldFor(self_->dimension);
            std::int32_t bx = static_cast<std::int32_t>(std::floor(self_->x));
            std::int32_t by = static_cast<std::int32_t>(std::floor(self_->y));
            std::int32_t bz = static_cast<std::int32_t>(std::floor(self_->z));
            bool inNether = false, inEnd = false;
            for (int dy = 0; dy <= 1; ++dy) {
                std::int32_t yy = by + dy;
                std::uint16_t st = curW.getBlock(bx, yy, bz);
                const gen::BlockDef* d = gen::blockByState(st);
                if (d) {
                    if (std::string_view(d->name) == "minecraft:nether_portal") inNether = true;
                    if (std::string_view(d->name) == "minecraft:end_portal") inEnd = true;
                }
            }
            std::int8_t target = 127;
            if (inNether) {
                if (self_->dimension == 0) target = -1;
                else if (self_->dimension == -1) target = 0;
                else if (self_->dimension == 1) target = 0;
            } else if (inEnd) {
                if (self_->dimension == 0) target = 1;
                else if (self_->dimension == 1) target = 0;
                else if (self_->dimension == -1) target = 0;
            }
            if (target != 127) {
                bool ok = PortalHandler::tryTeleport(srv_, *self_, target);
                if (ok) {
                    sentChunks_.clear();
                    lastCx_ = INT32_MAX; lastCz_ = INT32_MAX;
                    try { tickChunksAround(self_->x, self_->z); } catch (...) {}
                }
            }
        }
    }
}

void Session::broadcastMovement() {
    if (!self_->spawned) return;
    const bool first = !hasSent_;
    if (first) {                                   // initial absolute pose
        WriteBuffer b;
        b.varint(self_->entityId);
        b.f64(self_->x); b.f64(self_->y); b.f64(self_->z);
        b.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
        b.i8(static_cast<std::int8_t>(self_->pitch * 256.f / 360.f));
        b.boolean(self_->onGround);
        srv_.broadcastPacketExcept(nullptr, pl::sc::EntityTeleport, b);
        sentX_ = self_->x; sentY_ = self_->y; sentZ_ = self_->z;
        sentYaw_ = self_->yaw; sentPitch_ = self_->pitch;
        hasSent_ = true;
        return;
    }
    const double dx = self_->x - sentX_;
    const double dy = first ? 0 : self_->y - sentY_;
    const double dz = first ? 0 : self_->z - sentZ_;
    const bool rotated = first || self_->yaw != sentYaw_ || self_->pitch != sentPitch_;

    constexpr double kMaxRel = 7.999;              // i16 fixed point range /4096
    if (!first && dx*dx + dy*dy + dz*dz > 0.0001) {
        if (std::abs(dx) < kMaxRel && std::abs(dy) < kMaxRel && std::abs(dz) < kMaxRel) {
            if (rotated) {
                WriteBuffer b;
                b.varint(self_->entityId);
                b.i16(static_cast<std::int16_t>(dx * 4096));
                b.i16(static_cast<std::int16_t>(dy * 4096));
                b.i16(static_cast<std::int16_t>(dz * 4096));
                b.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
                b.i8(static_cast<std::int8_t>(self_->pitch * 256.f / 360.f));
                b.boolean(self_->onGround);
                srv_.broadcastPacketExcept(nullptr, pl::sc::MoveEntityPosRot, b);
            } else {
                WriteBuffer b;
                b.varint(self_->entityId);
                b.i16(static_cast<std::int16_t>(dx * 4096));
                b.i16(static_cast<std::int16_t>(dy * 4096));
                b.i16(static_cast<std::int16_t>(dz * 4096));
                b.boolean(self_->onGround);
                srv_.broadcastPacketExcept(nullptr, pl::sc::MoveEntityPos, b);
            }
            WriteBuffer h;
            h.varint(self_->entityId);
            h.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
            srv_.broadcastPacketExcept(nullptr, pl::sc::RotateHead, h);
        } else {                                    // teleport-class delta
            WriteBuffer b;
            b.varint(self_->entityId);
            b.f64(self_->x); b.f64(self_->y); b.f64(self_->z);
            b.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
            b.i8(static_cast<std::int8_t>(self_->pitch * 256.f / 360.f));
            b.boolean(self_->onGround);
            srv_.broadcastPacketExcept(nullptr, pl::sc::EntityTeleport, b);
        }
    } else if (rotated) {                           // pure rotation
        WriteBuffer b;
        b.varint(self_->entityId);
        b.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
        b.i8(static_cast<std::int8_t>(self_->pitch * 256.f / 360.f));
        b.boolean(self_->onGround);
        srv_.broadcastPacketExcept(nullptr, pl::sc::EntityLook, b);
        WriteBuffer h;
        h.varint(self_->entityId);
        h.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
        srv_.broadcastPacketExcept(nullptr, pl::sc::RotateHead, h);
    }

    sentX_ = self_->x; sentY_ = self_->y; sentZ_ = self_->z;
    sentYaw_ = self_->yaw; sentPitch_ = self_->pitch;
    hasSent_ = true;
}

void Session::onChatMessage(ReadBuffer& in) {
    const std::string msg = in.string(256);
    (void)in.i64();                                  // timestamp
    (void)in.i64();                                  // salt
    if (in.boolean()) in.bytes(256);                 // signature
    (void)in.varint();                               // offset
    in.bytes(3);                                     // acknowledged

    // events: PlayerChat (cancellable)
    api::PlayerChatEvent ev;
    ev.player = self_.get();
    ev.message = msg;
    if (!srv_.events().chat.fire(ev)) return;

    if (!ev.message.empty() && ev.message[0] == '/')
        return dispatchCommand(ev.message.substr(1));
    const std::string line = "<" + self_->name + "> " + ev.message;
    srv_.broadcastSystemText(line, nullptr);
    sendSystemText(line);
}

void Session::onChatCommand(ReadBuffer& in) {
    const std::string cmd = in.string(256);
    dispatchCommand(cmd);
}

void Session::dispatchCommand(const std::string& line) {
    brigadier::CommandContext ctx;
    ctx.source.player = self_.get();
    ctx.source.name = self_->name;
    ctx.source.console = false;
    ctx.srcX = self_->x; ctx.srcY = self_->y; ctx.srcZ = self_->z;
    ctx.srcYaw = self_->yaw; ctx.srcPitch = self_->pitch;
    for (auto& p : srv_.playersSnapshot())
        if (p.get() != self_.get()) ctx.playerNames.push_back(p->name);
    ctx.resolveSelector = [this](const std::string& raw,
                                 brigadier::SelectorResult& out) {
        out = srv_.resolveSelector(raw, self_.get());
        // entity selectors resolve to nothing name-wise; commands using names
        // will fall back to source.
    };

    const auto res =
        srv_.commands().execute(line, [&]{
            brigadier::CommandSource s;
            s.player = self_.get(); s.name = self_->name; s.console = false;
            return s;
        }());
    if (!res.ok)
        sendSystemText("\u00a7c" + (res.errorText.empty()
                          ? "Incorrect argument for command"
                          : res.errorText));
}

void Session::onHeldSlot(ReadBuffer& in) {
    const std::int16_t slot = in.i16();
    if (slot >= 0 && slot < 9) self_->heldSlot = slot;
}

void Session::onPlayerAction(ReadBuffer& in) {
    const std::int32_t status = in.varint();
    std::int32_t x, y, z;
    in.position(x, y, z);
    (void)in.i8();                                    // face
    const std::int32_t sequence = in.varint();

    if (status == 0 || status == 2) {                   // start / finish dig
        const std::uint16_t oldState = srv_.world().getBlock(x, y, z);
        const std::string bn = blockNameByState(oldState);
        const BlockMineInfo* mi = mineInfo(bn);
        const bool unbreakable = mi && mi->hardness < 0;

        if (status == 0 && self_->gamemode != 0) {          // creative: instant break
            if (oldState != 0) {
                api::BlockBreakEvent ev;
                ev.player = self_.get();
                ev.x = x; ev.y = y; ev.z = z;
                ev.oldState = oldState;
                if (!srv_.events().blockBreak.fire(ev)) { ack(sequence); return; }
                srv_.world().setBlock(x, y, z, 0);
                srv_.broadcastBlockChange(x, y, z, 0);
                srv_.world().scheduleNeighborUpdates(x, y, z);
            }
        } else if (status == 0 && self_->gamemode == 0 && !unbreakable && oldState != 0) {
            // begin tracked dig
            self_->digActive = true;
            self_->digX=x; self_->digY=y; self_->digZ=z;
            self_->digStartTick = srv_.tickNoForTest();
            const bool canHarvest = !mi || !mi->requiresPickaxe ||
                [&]{
                    if (self_->heldSlot < 0 || self_->heldSlot >= 9) return false;
                    const auto& sl = self_->inv[36 + self_->heldSlot];
                    if (sl.count <= 0) return false;
                    static thread_local std::unordered_map<std::uint32_t,std::string> i2n;
                    if (i2n.empty()) for (auto& e : gen::kItems) i2n.emplace(e.second, std::string(e.first));
                    auto it = i2n.find(sl.itemId);
                    return it != i2n.end() && it->second.find("pickaxe") != std::string::npos;
                }();
            const float speed = 1.f;                     // held-tool speed MVP
            const float h = mi ? mi->hardness : 1.f;
            const float denom = canHarvest ? 30.f : 100.f;
            self_->digTotalTicks = h <= 0 ? 1 :
                static_cast<std::int32_t>(std::ceil(h * denom / std::max(1.f, speed)));
            self_->digLastStage = 255;
            srv_.broadcastDigStage(*self_, 0);
        } else if (status == 1) {                        // cancelled
            if (self_->digActive) srv_.broadcastDigStage(*self_, -1);
            self_->digActive = false;
        } else if (status == 2) {                        // finished (client-side timing)
            if (self_->gamemode == 0) {
                if (unbreakable || oldState == 0) {
                    // reject: re-send authoritative block
                    WriteBuffer rb;
                    rb.position(x, y, z);
                    rb.varint(oldState);
                    conn_->sendPacket(proto::pl::sc::BlockUpdate, rb);
                } else if (!self_->digActive ||
                           self_->digX!=x || self_->digY!=y || self_->digZ!=z) {
                    // no tracked dig (or wrong spot): trust client, break now
                    srv_.world().setBlock(x,y,z,0);
                    srv_.broadcastBlockChange(x,y,z,0);
                } else {
                    const std::int64_t elapsed = srv_.tickNoForTest() - self_->digStartTick;
                    if (elapsed + 4 >= self_->digTotalTicks) {
                        // let tick completion fire naturally this tick or force now
                        self_->digTotalTicks = std::min(self_->digTotalTicks,
                            static_cast<std::int32_t>(elapsed + 1));
                    } else {
                        // too fast: revert
                        WriteBuffer rb;
                        rb.position(x, y, z);
                        rb.varint(oldState);
                        conn_->sendPacket(proto::pl::sc::BlockUpdate, rb);
                        self_->digActive = false;
                        srv_.broadcastDigStage(*self_, -1);
                    }
                }
            }
            // tick loop completes survival digs via digActive
        }
    }
    ack(sequence);                                      // ALWAYS ack sequences
}

void Session::onUseItemOn(ReadBuffer& in) {
    (void)in.varint();                                  // hand
    std::int32_t x, y, z;
    in.position(x, y, z);
    const std::int32_t dir = in.varint();
    const float cursorX = in.f32();
    const float cursorY = in.f32();
    const float cursorZ = in.f32();
    (void)cursorX; (void)cursorZ;
    (void)in.boolean();                                 // inside block
    (void)in.boolean();                                 // world border hit
    const std::int32_t sequence = in.varint();

    static constexpr int DX[] = {0, 0, 0, 0, -1, 1};
    static constexpr int DY[] = {1, -1, 0, 0, 0, 0};    // face: -Y? order below
    static constexpr int DZ[] = {0, 0, 1, -1, 0, 0};
    // vanilla face ids: 0 bottom(-Y), 1 top(+Y), 2 north(-Z), 3 south(+Z), 4 west(-X), 5 east(+X)
    static constexpr int FX[] = {0, 0, 0, 0, -1, 1};
    static constexpr int FY[] = {-1, 1, 0, 0, 0, 0};
    static constexpr int FZ[] = {0, 0, -1, 1, 0, 0};
    (void)DX; (void)DY; (void)DZ;
    const int d = (dir >= 0 && dir < 6) ? dir : 0;
    const std::int32_t tx = x + FX[d], ty = y + FY[d], tz = z + FZ[d];

    // right-click on interactive blocks opens menus (vanilla behaviour)
    {
        const std::uint16_t clickedState = srv_.world().getBlock(x, y, z);
        const gen::BlockDef* bdef = gen::blockByState(clickedState);
        if (bdef && d == 1) {
            const std::string bn(bdef->name);
            if (bn.find("chest") != std::string::npos ||
                bn == "minecraft:furnace" ||
                bn == "minecraft:hopper" || bn == "minecraft:dispenser" ||
                bn == "minecraft:dropper" ||
                bn == "minecraft:crafting_table") {
                openMenuAt(x, y, z, clickedState);
                ack(sequence);
                return;
            }
            // redstone interactables (lever / button) consume the click
            if (bn == "minecraft:lever" ||
                bn.find("_button") != std::string::npos) {
                srv_.redstone_->onInteract(x, y, z, srv_.tickNoForTest());
                ack(sequence);
                return;
            }
            // beds: sleep through the night (plan4 P1-C)
            if (bn.find("_bed") != std::string::npos &&
                bn.rfind("minecraft:", 0) == 0 && bn != "minecraft:bedrock") {
                const bool night = srv_.isNight();
                if (!night) {
                    sendSystemText("\u00a77You can only sleep at night");
                    ack(sequence);
                    return;
                }
                self_->sleeping = true;
                self_->bedX = x; self_->bedY = y; self_->bedZ = z;
                WriteBuffer sp;
                sp.position(x, y, z);
                sp.f32(0.f);
                try { conn_->sendPacket(proto::pl::sc::SetDefaultSpawn, sp); }
                catch (...) {}
                int sleepingCount = 0, survivalCount = 0;
                for (auto& p : srv_.playersSnapshot()) {
                    if (!p->inPlay || p->gamemode != 0) continue;
                    ++survivalCount;
                    if (p->sleeping) ++sleepingCount;
                }
                if (sleepingCount >= survivalCount) {
                    srv_.setTimeOfDay(0);              // morning
                    if (srv_.raining()) srv_.forceWeatherClear();
                    for (auto& p : srv_.playersSnapshot())
                        if (p->sleeping) {
                            p->sleeping = false;
                            double wx = p->bedX + 1.5, wz = p->bedZ + 0.5;
                            WriteBuffer tb;
                            tb.varint(++teleportId_);
                            tb.f64(wx); tb.f64(p->bedY + 0.5); tb.f64(wz);
                            tb.f64(0); tb.f64(0); tb.f64(0);
                            tb.f32(p->yaw); tb.f32(0);
                            tb.u32(0);
                            try { p->conn->sendPacket(
                                      proto::pl::sc::PlayerPosition, tb); }
                            catch (...) {}
                        }
                    srv_.broadcastSystemText("\u00a77Good morning!");
                } else {
                    sendSystemText("\u00a77Sleeping... (" +
                                   std::to_string(sleepingCount) + "/" +
                                   std::to_string(survivalCount) + ")");
                }
                ack(sequence);
                return;
            }
        }
    }

    // Place the actually-held block item (vanilla semantics).
    static const InvSlot airSlot = InvSlot::air();
    const bool survival = self_->gamemode == 0;
    const InvSlot& heldItem =
        (self_->heldSlot >= 0 && self_->heldSlot < 9)
            ? self_->inv[36 + self_->heldSlot] : airSlot;

    // ---- portal ignition (plan5): flint_and_steel / fire_charge on obsidian frame 4x5 -> nether portal
    {
        InvSlot heldCopy = (self_->heldSlot >= 0 && self_->heldSlot < 9) ? self_->inv[36 + self_->heldSlot] : InvSlot::air();
        const std::string heldNameForPortal = heldCopy.empty() ? std::string() : heldCopy.name();
        bool isFlint = heldNameForPortal == "minecraft:flint_and_steel";
        bool isFireCharge = heldNameForPortal == "minecraft:fire_charge";
        if ((isFlint || isFireCharge) && !heldCopy.empty()) {
            World& w = srv_.worldFor(self_->dimension);
            std::uint16_t clickedSt = w.getBlock(x, y, z);
            const gen::BlockDef* cd = gen::blockByState(clickedSt);
            bool clickedIsObsidian = cd && std::string(cd->name) == "minecraft:obsidian";
            if (clickedIsObsidian) {
                const auto& mp = gen::blockNameToState();
                auto obsIt = mp.find("minecraft:obsidian");
                std::uint16_t obsidian = obsIt != mp.end() ? static_cast<std::uint16_t>(obsIt->second) : 2397;
                const gen::BlockDef* portalDef = gen::blockByName("minecraft:nether_portal");
                bool ignited = false;
                auto fillInterior = [&](int orient, int ox, int oy, int oz) {
                    std::uint16_t portalState = 6033;
                    if (portalDef) {
                        if (orient == 0) portalState = static_cast<std::uint16_t>(gen::stateWithProps(*portalDef, {{"axis","x"}}));
                        else portalState = static_cast<std::uint16_t>(gen::stateWithProps(*portalDef, {{"axis","z"}}));
                    } else {
                        auto it2 = mp.find("minecraft:nether_portal");
                        if (it2 != mp.end()) portalState = static_cast<std::uint16_t>(it2->second);
                    }
                    for (int dy=1; dy<=3; ++dy) for (int dx=1; dx<=2; ++dx) {
                        int32_t wx, wz;
                        if (orient==0) { wx = ox+dx; wz = oz; }
                        else { wx = ox; wz = oz+dx; }
                        int32_t wy = oy+dy;
                        w.setBlock(wx, wy, wz, portalState);
                        srv_.broadcastBlockChange(wx, wy, wz, portalState);
                    }
                    int32_t cxp = ox+1 + (orient==0?1:0);
                    int32_t czp = oz + (orient==1?1:0);
                    srv_.broadcastSound("minecraft:block.portal.ambient", cxp+0.5, oy+2, czp+0.5, 0.8f, 1.0f, "blocks");
                    srv_.broadcastSound("minecraft:item.flintandsteel.use", x+0.5, y+0.5, z+0.5, 1.f, 1.f, "blocks");
                };
                for (int oy = y - 4; oy <= y && !ignited; ++oy) {
                    for (int ox = x - 3; ox <= x && !ignited; ++ox) {
                        if (oy < kMinY || oy+4 >= kMaxY) continue;
                        bool valid = true;
                        for (int dy=0; dy<5 && valid; ++dy) for (int dx=0; dx<4 && valid; ++dx) {
                            int32_t wx = ox+dx; int32_t wy = oy+dy; int32_t wz = z;
                            w.generateChunkIfMissing(wx>>4, wz>>4);
                            std::uint16_t st = w.getBlock(wx, wy, wz);
                            bool isBorder = (dx==0 || dx==3 || dy==0 || dy==4);
                            if (isBorder) { if (st != obsidian) valid=false; }
                            else { if (st != 0) valid=false; }
                        }
                        if (!valid) continue;
                        fillInterior(0, ox, oy, z);
                        ignited = true;
                    }
                }
                if (!ignited) {
                    for (int oy = y - 4; oy <= y && !ignited; ++oy) {
                        for (int oz = z - 3; oz <= z && !ignited; ++oz) {
                            if (oy < kMinY || oy+4 >= kMaxY) continue;
                            bool valid = true;
                            for (int dy=0; dy<5 && valid; ++dy) for (int dx=0; dx<4 && valid; ++dx) {
                                int32_t wx = x; int32_t wy = oy+dy; int32_t wz = oz+dx;
                                w.generateChunkIfMissing(wx>>4, wz>>4);
                                std::uint16_t st = w.getBlock(wx, wy, wz);
                                bool isBorder = (dx==0 || dx==3 || dy==0 || dy==4);
                                if (isBorder) { if (st != obsidian) valid=false; }
                                else { if (st != 0) valid=false; }
                            }
                            if (!valid) continue;
                            fillInterior(1, x, oy, oz);
                            ignited = true;
                        }
                    }
                }
                if (ignited) {
                    if (self_->gamemode == 0) {
                        if (isFlint) {
                            auto* slot = &self_->inv[36 + self_->heldSlot];
                            bool broken = slot->applyDamage(1);
                            if (broken) *slot = InvSlot::air();
                            srv_.resendInventory(*self_);
                        } else if (isFireCharge) {
                            auto* slot = &self_->inv[36 + self_->heldSlot];
                            if (--slot->count <= 0) *slot = InvSlot::air();
                            srv_.resendInventory(*self_);
                        }
                    }
                    ack(sequence);
                    return;
                }
            }
        }
    }

    // ---- buckets: water/lava placement and pickup (plan5 items 48-51)
    if (!heldItem.empty()) {
        const std::string heldName = heldItem.name();
        if (heldName == "minecraft:water_bucket" || heldName == "minecraft:lava_bucket") {
            std::uint16_t target = srv_.world().getBlock(tx, ty, tz);
            bool replaceable = (target == 0);
            // also consider replaceable plants? treat only air for now
            if (replaceable) {
                std::string fluidName = (heldName == "minecraft:water_bucket") ? "minecraft:water" : "minecraft:lava";
                std::uint16_t fluidState = static_cast<std::uint16_t>(gen::stateWithPropsList(fluidName, {{"level","0"}}));
                if (fluidState==0) {
                    auto it = gen::blockNameToState().find(fluidName);
                    if (it != gen::blockNameToState().end()) fluidState = static_cast<std::uint16_t>(it->second);
                }
                srv_.world().setBlock(tx, ty, tz, fluidState);
                srv_.broadcastBlockChange(tx, ty, tz, fluidState);
                if (survival) {
                    auto* mh = &self_->inv[36 + self_->heldSlot];
                    *mh = ItemStack::ofName("minecraft:bucket", 1);
                    srv_.resendInventory(*self_);
                }
                srv_.broadcastSound("minecraft:item.bucket.empty", tx+0.5, ty+0.5, tz+0.5, 1.f, 1.f, "blocks");
                ack(sequence);
                return;
            }
        } else if (heldName == "minecraft:bucket") {
            auto tryPick = [&](std::int32_t px,std::int32_t py,std::int32_t pz)->bool{
                std::uint16_t bs = srv_.world().getBlock(px,py,pz);
                const gen::BlockDef* bd = gen::blockByState(bs);
                if (!bd) return false;
                bool isWater=false,isLava=false;
                if (bd->name=="minecraft:water") {
                    for (auto& [k,v]: gen::propsOf(bs)) if (k=="level" && v=="0") isWater=true;
                } else if (bd->name=="minecraft:lava") {
                    for (auto& [k,v]: gen::propsOf(bs)) if (k=="level" && v=="0") isLava=true;
                }
                if (!isWater && !isLava) return false;
                srv_.world().setBlock(px,py,pz, 0);
                srv_.broadcastBlockChange(px,py,pz, 0);
                if (survival) {
                    auto* mh = &self_->inv[36 + self_->heldSlot];
                    std::string newName = isWater ? "minecraft:water_bucket" : "minecraft:lava_bucket";
                    *mh = ItemStack::ofName(newName, 1);
                    srv_.resendInventory(*self_);
                }
                srv_.broadcastSound("minecraft:item.bucket.fill", px+0.5, py+0.5, pz+0.5, 1.f, 1.f, "blocks");
                return true;
            };
            if (tryPick(x,y,z) || tryPick(tx,ty,tz)) {
                ack(sequence);
                return;
            }
        } else if (heldName == "minecraft:flint_and_steel" || heldName == "minecraft:fire_charge") {
            std::uint16_t target = srv_.world().getBlock(tx, ty, tz);
            if (target == 0) {
                bool canPlace = true;
                if (srv_.gameRules().contains("doFireTick") && !srv_.gameRules().getBool("doFireTick")) canPlace = false;
                if (canPlace) {
                    auto it = gen::blockNameToState().find("minecraft:fire");
                    if (it != gen::blockNameToState().end()) {
                        std::uint16_t fireState = static_cast<std::uint16_t>(it->second);
                        srv_.world().setBlock(tx, ty, tz, fireState);
                        srv_.broadcastBlockChange(tx, ty, tz, fireState);
                        if (survival) {
                            auto* mh = &self_->inv[36 + self_->heldSlot];
                            if (heldName=="minecraft:flint_and_steel") {
                                if (mh->applyDamage(1)) *mh = ItemStack::air();
                            } else {
                                if (--mh->count <=0) *mh = ItemStack::air();
                            }
                            srv_.resendInventory(*self_);
                        }
                        srv_.broadcastSound("minecraft:item.flintandsteel.use", tx+0.5, ty+0.5, tz+0.5, 1.f, 1.f, "blocks");
                    }
                }
                ack(sequence);
                return;
            }
        }
    }

    // ---- bone meal fertilize hook ----
    if (!heldItem.empty() && heldItem.name() == "minecraft:bone_meal" && srv_.blockTicks_) {
        const std::uint16_t clickedSt = srv_.world().getBlock(x, y, z);
        if (clickedSt != 0) {
            const gen::BlockDef* cb = gen::blockByState(clickedSt);
            if (cb) {
                const std::string bn(cb->name);
                auto* beh = srv_.blockTicks_->behaviorFor(bn);
                if (beh && beh->fertilize(srv_.world(), x, y, z, clickedSt, &srv_)) {
                    const std::uint16_t newSt = srv_.world().getBlock(x, y, z);
                    srv_.broadcastBlockChange(x, y, z, newSt);
                    srv_.broadcastSound("minecraft:item.bone_meal.use", x + 0.5, y + 0.5, z + 0.5);
                    if (survival) {
                        auto* mh = &self_->inv[36 + self_->heldSlot];
                        if (--mh->count <= 0) *mh = InvSlot::air();
                        srv_.resendInventory(*self_);
                    }
                    ack(sequence);
                    return;
                }
            }
        }
    }

    // ---- doors: two-block placement + toggle (plan4 P3-M)
    if (!heldItem.empty()) {
        const std::string heldName = heldItem.name();
        if (heldName.size() > 5 && heldName.rfind("_door", heldName.size() - 5) != std::string::npos) {
            const gen::BlockDef* ddef = gen::blockByName(heldName);
            if (ddef && srv_.world().getBlock(tx, ty, tz) == 0 &&
                srv_.world().getBlock(tx, ty + 1, tz) == 0) {
                float yaw = self_->yaw;
                const char* facing = "north";
                if (yaw >= 45.f && yaw < 135.f) facing = "west";
                else if (yaw >= 135.f && yaw < 225.f) facing = "south";
                else if (yaw >= 225.f && yaw < 315.f) facing = "east";
                const auto lower =
                    static_cast<std::uint16_t>(gen::stateWithProps(*ddef,
                        {{"half","lower"},{"facing",facing},{"open","false"},{"hinge","left"}}));
                const auto upper =
                    static_cast<std::uint16_t>(gen::stateWithProps(*ddef,
                        {{"half","upper"},{"facing",facing},{"open","false"},{"hinge","left"}}));
                srv_.world().setBlock(tx, ty, tz, lower);
                srv_.broadcastBlockChange(tx, ty, tz, lower);
                srv_.world().setBlock(tx, ty + 1, tz, upper);
                srv_.broadcastBlockChange(tx, ty + 1, tz, upper);
                if (survival) {
                    auto mh = &self_->inv[36 + self_->heldSlot];
                    if (--mh->count <= 0) *mh = InvSlot::air();
                    srv_.resendInventory(*self_);
                }
                ack(sequence);
                return;
            }
        }
    }

    if (srv_.world().getBlock(tx, ty, tz) != 0 || heldItem.empty()) {
        // toggling an existing door?
        const std::uint16_t clickedState = srv_.world().getBlock(x, y, z);
        const gen::BlockDef* cdef = gen::blockByState(clickedState);
        if (cdef && cdef->name.size() > 5 &&
            cdef->name.rfind("_door", cdef->name.size() - 5) != std::string::npos) {
            bool open = false, upperHalf = false;
            for (auto& [k, v] : gen::propsOf(clickedState)) {
                if (k == "open") open = v == "true";
                if (k == "half") upperHalf = v == "upper";
            }
            std::string facing;
            std::string hinge = "left";
            for (auto& [k, v] : gen::propsOf(clickedState)) {
                if (k == "facing") facing = std::string(v);
                if (k == "hinge") hinge = std::string(v);
            }
            const std::uint16_t st1 = static_cast<std::uint16_t>(
                gen::stateWithProps(*cdef,
                    {{"open", open ? "false" : "true"},
                     {"half", upperHalf ? "upper" : "lower"},
                     {"facing", facing}, {"hinge", hinge}}));
            const std::int32_t oy = upperHalf ? y - 1 : y + 1;
            const std::uint16_t st2 = static_cast<std::uint16_t>(
                gen::stateWithProps(*cdef,
                    {{"open", open ? "false" : "true"},
                     {"half", upperHalf ? "lower" : "upper"},
                     {"facing", facing}, {"hinge", hinge}}));
            srv_.world().setBlock(x, y, z, st1);
            srv_.broadcastBlockChange(x, y, z, st1);
            srv_.world().setBlock(x, oy, z, st2);
            srv_.broadcastBlockChange(x, oy, z, st2);
            srv_.broadcastSound("minecraft:block.wooden_door.toggle",
                                x + .5, y + .5, z + .5, 1.f,
                                open ? 0.7f : 0.9f);
        }
        ack(sequence);
        return;
    }
    // item id -> block name (block items share the name)
    std::string itemName = heldItem.name();
    std::uint16_t newState = 0;
    const gen::BlockDef* bdef2 = gen::blockByName(itemName);
    if (!bdef2) {                                          // not a placeable block
        // special items handled elsewhere (food via UseItem); nothing to do
        ack(sequence);
        return;
    }
    std::vector<std::pair<std::string_view, std::string_view>> props;
    (void)props;
    {
        // context-aware defaults: facing opposite of player yaw
        float yaw = self_->yaw;
        const char* facing = "north";
        if (yaw >= 45.f && yaw < 135.f) facing = "east";
        else if (yaw >= 135.f && yaw < 225.f) facing = "south";
        else if (yaw >= 225.f && yaw < 315.f) facing = "west";
        bool hasFacing = false;
        for (int i = 0; i < bdef2->propCount; ++i) {
            const auto& pd = gen::kPropDefs[gen::kBlockPropsRun[bdef2->propsOff + i]];
            if (pd.name == "facing") hasFacing = true;
        }
        if (hasFacing) props.emplace_back("facing", facing);
        newState = static_cast<std::uint16_t>(gen::stateWithProps(*bdef2, props));
    }

    api::BlockPlaceEvent ev;
    ev.player = self_.get();
    ev.x = tx; ev.y = ty; ev.z = tz;
    ev.newState = newState;
    if (!srv_.events().blockPlace.fire(ev)) { ack(sequence); return; }

    srv_.world().setBlock(tx, ty, tz, newState);
    srv_.broadcastBlockChange(tx, ty, tz, newState);
    srv_.world().scheduleNeighborUpdates(tx, ty, tz);
    if (survival) {
        auto mutableHeld = &self_->inv[36 + self_->heldSlot];
        if (--mutableHeld->count <= 0) *mutableHeld = InvSlot::air();
        srv_.resendInventory(*self_);
    }
    ack(sequence);
}

void Session::onUseItem(ReadBuffer& in) {
    (void)in.varint();                                  // hand
    const std::int32_t sequence = in.varint();
    (void)in.f32(); (void)in.f32();                     // rotation
    if (self_->heldSlot >= 0 && self_->heldSlot < 9) {
        const auto& sl = self_->inv[36 + self_->heldSlot];
        static const std::unordered_map<std::uint32_t, std::pair<int,float>> kFood{
            {gen::itemIdByName().at("minecraft:bread"), {5,6}},
            {gen::itemIdByName().at("minecraft:apple"), {4,3}},
        };
        auto f = kFood.find(sl.itemId);
        if (f != kFood.end() && self_->food < 20) {
            self_->food = std::min(20, self_->food + f->second.first);
            self_->saturation = std::min<float>((float)self_->food,
                                                self_->saturation + f->second.second);
            srv_.sendSetHealth(*self_);
            for (auto& s2 : self_->inv)
                if (s2.itemId == sl.itemId && s2.count > 0) {
                    if (--s2.count <= 0) s2 = InvSlot::air();
                    break;
                }
            srv_.resendInventory(*self_);
        }
    }
    ack(sequence);
}

void Session::onUseEntity(ReadBuffer& in) {
    const std::int32_t target = in.varint();
    const std::int32_t mouse = in.varint();
    if (mouse == 2) { (void)in.f32(); (void)in.f32(); (void)in.f32(); }
    if (mouse != 1) {
        // INTERACT (0) / INTERACT_AT (2): open trading for villagers
        if (mouse == 0 || mouse == 2) {
            (void)in.varint();                        // sneaking flag
            std::lock_guard lk(srv_.entsMtx_);
            for (auto& m : srv_.mobsForTest()) {
                if (m->entityId != target) continue;
                if (m->kind == MobKind::Villager) {
                    srv_.openTrading(*self_, *m);
                    tradingVillager_ = target;
                    openMenu_ = nullptr;              // merchant menu tracked separately
                }
                break;
            }
        } else {
            (void)in.varint();
        }
        return;
    }

    float dmg = 1.f;
    if (self_->heldSlot >= 0 && self_->heldSlot < 9) {
        const auto& sl = self_->inv[36 + self_->heldSlot];
        if (sl.count > 0 && sl.itemId == gen::itemIdByName().at("minecraft:iron_sword"))
            dmg = 6.f;
    }

    bool killed = false;
    std::shared_ptr<MobEntity> victim;
    {
        std::lock_guard lk(srv_.entsMtx_);
        for (auto& m : srv_.mobsForTest()) {
            if (m->entityId != target || m->dead) continue;
            m->health -= dmg;
            // AI hurt memory → panic/anger
            auto it = srv_.mobAi_.find(m->entityId);
            if (it != srv_.mobAi_.end()) {
                it->second.ctx->lastHurtTick = srv_.tickNoForTest();
                it->second.ctx->lastHurtByEntityId = self_->entityId;
            }
            if (m->health <= 0) { m->dead = true; killed = true; victim = m; }
            break;
        }
    }
    if (killed && victim) {
        WriteBuffer rm;
        rm.varint(1); rm.varint(target);
        srv_.broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, rm);
        srv_.onMobKilledBy(*self_, victim->kind);
        srv_.scoreboard.addScore("kills", self_->name, 1);
        srv_.sendScoreAll("kills", self_->name,
                          srv_.scoreboard.getScore("kills", self_->name));
        const auto drop = MobEntity::dropFor(victim->kind);
        if (drop.itemId)
            srv_.spawnItemDrop(victim->x, victim->y + 0.4, victim->z, drop.itemId, drop.count,
                               (rand()/(double)RAND_MAX-.5)*.15, .1,
                               (rand()/(double)RAND_MAX-.5)*.15);
        srv_.spawnXpOrbs(victim->x, victim->y + 0.5, victim->z,
                         mobStats(victim->kind).xpDrop, self_.get());
        std::lock_guard lk(srv_.entsMtx_);
        srv_.mobAi_.erase(target);
        srv_.mobsForTest().erase(
            std::remove_if(srv_.mobsForTest().begin(), srv_.mobsForTest().end(),
                [&](const std::shared_ptr<MobEntity>& x){ return x.get()==victim.get(); }),
            srv_.mobsForTest().end());
    }
}

} // namespace cppfm
