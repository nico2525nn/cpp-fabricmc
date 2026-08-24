#include "GameServer.hpp"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include "../generated/ItemIds.hpp"
#include "../generated/EntityIds.hpp"
#include <cerrno>

namespace cppfm {

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
    if (tickThread_.joinable()) { tickThread_.join(); }
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

void GameServer::acceptLoop() {
    while (running_) {
        sockaddr_in cli{}; socklen_t cl = sizeof(cli);
        int fd = ::accept(listenFd_, reinterpret_cast<sockaddr*>(&cli), &cl);
        if (fd < 0) { if (running_) continue; break; }
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
            if (oldState != 0) {
                world_.setBlock(p->digX, p->digY, p->digZ, 0);
                broadcastBlockChange(p->digX, p->digY, p->digZ, 0);
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
    broadcastSystemText(std::string("\u00a7c") + p.name + " died (" + cause + ")", &p);
}

void GameServer::tickOnce() {
    tickDigs();
    survivalTick();

    // mob spawn cadence: every 20 ticks
    if (tickNo_ % 20 == 0) trySpawnMobs();
    mobsTick();
    itemsTick();

    // periodic time sync every 20 ticks (1s)
    if (tickNo_ % 20 == 0) {
        WriteBuffer t;
        t.i64(tickNo_);
        t.i64(dayTime());
        t.boolean(true);
        broadcastPacketExcept(nullptr, pl::sc::SetTime, t);
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
            // require grass-ish surface? MVP: any non-water solid
            auto mob = std::make_shared<MobEntity>();
            mob->entityId = nextEntityId();
            static const MobKind passive[] = {MobKind::Pig, MobKind::Cow, MobKind::Sheep, MobKind::Chicken};
            if (isNight()) {
                int hostiles = 0;
                for (auto& m : mobs_) if (m->kind == MobKind::Zombie) ++hostiles;
                if (hostiles >= 6) continue;
                mob->kind = MobKind::Zombie;
                std::fprintf(stderr, "[cppfm] zombie spawned eid=%d at %.1f/%.1f\n",
                             mob->entityId, mob->x, mob->z);
            } else {
                mob->kind = passive[rand() % 4];
            }
            mob->x = wx + 0.5; mob->z = wz + 0.5; mob->y = groundY + 1.0;
            mob->lastSeenMs = nowMs();
            mobs_.push_back(mob);

            // broadcast spawn to everyone in play
            WriteBuffer b;
            b.varint(mob->entityId);
            static std::uint8_t zero[16] = {};
            b.uuid(zero);
            b.varint(static_cast<std::int32_t>(MobEntity::typeId(mob->kind)));
            b.f64(mob->x); b.f64(mob->y); b.f64(mob->z);
            b.i8(0); b.i8(0); b.i8(0);
            b.varint(0); b.i16(0); b.i16(0); b.i16(0);
            broadcastPacketExcept(nullptr, pl::sc::SpawnEntity, b);
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
            if (!nearPlayer) { despawn.push_back(m->entityId); it = mobs_.erase(it); continue; }

            Player* chaseTarget = nullptr;
            if (m->kind == MobKind::Zombie && !isNight() ) {
                // burn in daylight
                if (tickNo_ % 20 == 0) {
                    applyDamageToMob(*m, 1.f, "burned to death");
                    if (m->dead) { deadIds.push_back(m->entityId); drops.push_back(m); it = mobs_.erase(it); continue; }
                }
            }
            if (m->kind == MobKind::Zombie) {
                double best = 24*24;
                for (auto& pp : playersSnapshot()) {
                    if (!pp->inPlay || pp->dead || pp->gamemode == 1) continue;
                    double ddx = pp->x-m->x, ddz = pp->z-m->z;
                    double d2 = ddx*ddx+ddz*ddz;
                    if (d2 < best) { best = d2; chaseTarget = pp.get(); }
                }
                if (chaseTarget) {
                    m->tx = chaseTarget->x; m->tz = chaseTarget->z; m->hasTarget = true;
                    // melee
                    double cdx = chaseTarget->x-m->x, cdz = chaseTarget->z-m->z;
                    double cd = sqrt(cdx*cdx+cdz*cdz);
                    static std::int64_t nextAttack = 0;
                    (void)nextAttack;
                    if (cd < 1.8 && tickNo_ % 24 == 0) {
                        // attack via srv loop context below? We're inside GameServer method:
                        float before = chaseTarget->health;
                        applyDamage(*chaseTarget, 3.f, "Zombie");
                        if (before != chaseTarget->health && chaseTarget->conn) {
                            // knockback-ish: small velocity not synced for players MVP
                        }
                    }
                }
            }
            if (!chaseTarget && (!m->hasTarget || now >= m->nextWanderAt)) {
                m->tx = m->x + (rand()/(double)RAND_MAX - .5) * 10;
                m->tz = m->z + (rand()/(double)RAND_MAX - .5) * 10;
                m->hasTarget = true;
                m->nextWanderAt = now + 3000 + rand() % 4000;
            }
            // walk toward target at ~1.2 m/s (per tick 0.06*speed factor)
            double dx = (chaseTarget ? chaseTarget->x : m->tx) - m->x;
            double dz = (chaseTarget ? chaseTarget->z : m->tz) - m->z;
            const double dist = std::sqrt(dx*dx + dz*dz);
            if (dist > 0.3) {
                const double step = std::min(0.09, dist);
                m->yaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / 3.14159 - 90.0);
                m->x += dx / dist * step;
                m->z += dz / dist * step;
                world_.generateChunkIfMissing(static_cast<std::int32_t>(m->x) >> 4,
                                       static_cast<std::int32_t>(m->z) >> 4);
                int col = 4;
                world_.withChunk(static_cast<std::int32_t>(m->x) >> 4,
                          static_cast<std::int32_t>(m->z) >> 4, [&](const Chunk& c) {
                    for (int ry = kSectionsPerChunk*16-1; ry >= 0; --ry)
                        if (c.blocks[Chunk::index(ry>>4, ry&15,
                            static_cast<std::int32_t>(m->z)&15,
                            static_cast<std::int32_t>(m->x)&15)] != 0) { col = ry+1; break; }
                });
                m->y = kMinY + col + 1.0;
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
        spawnItemDrop(m->x, m->y + 0.4, m->z, drop.itemId, drop.count,
                      (rand()/(double)RAND_MAX-.5)*.15, .1,
                      (rand()/(double)RAND_MAX-.5)*.15);
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
    for (int i = 0; i < 46; ++i) {
        const auto& s = p.inv[i];
        if (s.count > 0) {
            b.varint(s.count);
            b.varint(static_cast<std::int32_t>(s.itemId));
            b.varint(0); b.varint(0);
        } else b.varint(0);
    }
    b.varint(0);                                        // carried
    try { p.conn->sendPacket(pl::sc::ContainerSetContent, b); } catch (...) {}
}

// ===================================================================== Session

// ===================================================================== Session

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
        srv_.broadcastSystemText("\u00a7e" + self_->name + " left the game", nullptr);
        WriteBuffer rm;
        rm.varint(1);
        rm.uuid(self_->uuid.data());
        srv_.broadcastPacketExcept(nullptr, pl::sc::PlayerInfoRemove, rm);
        WriteBuffer ent;
        ent.varint(1);
        ent.varint(self_->entityId);
        srv_.broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, ent);
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
            const std::string json =
                "{\"version\":{\"name\":\"" + std::string(kMinecraftVersion) +
                "\",\"protocol\":" + std::to_string(kProtocolVersion) +
                "},\"players\":{\"max\":" + std::to_string(srv_.config().maxPlayers) +
                ",\"online\":" + std::to_string(srv_.playerCount() + 0) +
                ",\"sample\":[]},\"description\":{\"text\":\"" + srv_.config().motd +
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
    const auto sp = line.find(' ');
    const std::string head = line.substr(0, sp == std::string::npos ? line.size() : sp);
    std::string out;
    if (head == "list") {
        for (auto& p : playersSnapshot()) out += p->name + " ";
        out += "(" + std::to_string(playersSnapshot().size()) + " online)";
    } else if (head == "say") {
        const std::string msg = "[Server] " +
            (sp == std::string::npos ? "" : line.substr(sp + 1));
        broadcastSystemText("\u00a7d" + msg);
        out = "broadcast sent";
    } else if (head == "help") {
        out = "commands: list | say <msg> | help";
    } else out = "unknown command";
    return out;
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
        case cf::cs::CustomPayload:                 // channel+rest: ignore
            (void)in.string();
            in.skipRest();
            break;
        case cf::cs::ResourcePackResponse:
            (void)in.u8(); (void)in.varint();
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
    self_->gamemode = 0;
    self_->health = 20; self_->food = 20; self_->saturation = 5;
    self_->exhaustion = 0; self_->fallDist = 0; self_->dead = false;

    broadcastSpawnEntity(self_.get());
    sendDeclareCommands();

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
            if (hot < 45) { self_->inv[hot] = InvSlot{ii->second, static_cast<std::int16_t>(e.cnt)}; ++hot; }
        }
    }
    WriteBuffer b;
    b.u8(0);                                       // window id: player inventory
    b.varint(++self_->invStateId);
    b.varint(46);                                  // slots
    for (int i = 0; i < 46; ++i) {
        const auto& sl = self_->inv[i];
        if (sl.count > 0) {
            b.varint(sl.count);
            b.varint(static_cast<std::int32_t>(sl.itemId));
            b.varint(0); b.varint(0);
        } else b.varint(0);
    }
    b.varint(0);                                   // carried item
    conn_->sendPacket(pl::sc::ContainerSetContent, b);
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
        case pl::cs::UseEntity:           onUseEntity(in); break;
        case pl::cs::ChatCommand:         onChatCommand(in); break;
        case pl::cs::PlayerAction:        onPlayerAction(in); break;
        case pl::cs::UseItemOn:           onUseItemOn(in); break;
        case pl::cs::UseItem:             onUseItem(in); break;
        case pl::cs::HeldItemSlot:        onHeldSlot(in); break;
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
            // parse defensively: plain items only; bail out on components
            (void)in.i16();
            if (in.varint() > 0) {
                (void)in.varint();                  // item id
                const std::int32_t add = in.varint();
                const std::int32_t rem = in.varint();
                if (add != 0 || rem != 0)
                    throw std::runtime_error("creative slot with unsupported components");
            }
            break;
        }
        case pl::cs::ChangeDifficulty: (void)in.u8(); break;
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

    if (!msg.empty() && msg[0] == '/') return dispatchCommand(msg.substr(1));
    const std::string line = "<" + self_->name + "> " + msg;
    srv_.broadcastSystemText(line, nullptr);
    sendSystemText(line);
}

void Session::onChatCommand(ReadBuffer& in) {
    const std::string cmd = in.string(256);
    dispatchCommand(cmd);
}

void Session::dispatchCommand(const std::string& line) {
    const std::string head = line.substr(0, line.find(' '));
    if (head == "ping") sendSystemText("\u00a7aPong!");
    else if (head == "help") sendSystemText("\u00a77Commands: /ping /help");
    else sendSystemText("\u00a7cUnknown command: /" + line);
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

        if (status == 0 && self_->gamemode == 0 && !unbreakable && oldState != 0) {
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
            } else {                                      // creative instant
                if (oldState != 0) {
                    srv_.world().setBlock(x, y, z, 0);
                    srv_.broadcastBlockChange(x, y, z, 0);
                }
            }
            self_->digActive = self_->digActive;          // tick completes survival digs
        }
    }
    ack(sequence);                                      // ALWAYS ack sequences
}

void Session::onUseItemOn(ReadBuffer& in) {
    (void)in.varint();                                  // hand
    std::int32_t x, y, z;
    in.position(x, y, z);
    const std::int32_t dir = in.varint();
    (void)in.f32(); (void)in.f32(); (void)in.f32();     // cursor
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

    if (srv_.world().getBlock(tx, ty, tz) == 0 &&
        self_->heldSlot < static_cast<std::int32_t>(g_hotbar.size())) {
        const auto& entry = g_hotbar[static_cast<std::size_t>(self_->heldSlot)];
        srv_.world().setBlock(tx, ty, tz, entry.stateId);
        srv_.broadcastBlockChange(tx, ty, tz, entry.stateId);
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
                    if (--s2.count <= 0) s2 = InvSlot{};
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
    if (mouse != 1) return;                             // only ATTACK

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
            if (m->health <= 0) { m->dead = true; killed = true; victim = m; }
            break;
        }
    }
    if (killed && victim) {
        WriteBuffer rm;
        rm.varint(1); rm.varint(target);
        srv_.broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, rm);
        const auto drop = MobEntity::dropFor(victim->kind);
        srv_.spawnItemDrop(victim->x, victim->y + 0.4, victim->z, drop.itemId, drop.count,
                           (rand()/(double)RAND_MAX-.5)*.15, .1,
                           (rand()/(double)RAND_MAX-.5)*.15);
        std::lock_guard lk(srv_.entsMtx_);
        srv_.mobsForTest().erase(
            std::remove_if(srv_.mobsForTest().begin(), srv_.mobsForTest().end(),
                [&](const std::shared_ptr<MobEntity>& x){ return x.get()==victim.get(); }),
            srv_.mobsForTest().end());
    }
}

} // namespace cppfm
