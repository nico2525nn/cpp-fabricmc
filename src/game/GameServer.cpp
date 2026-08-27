#include "GameServer.hpp"
#include "BlockEvent.hpp"
#include "../net/PacketHandler.hpp"
#include "../net/PacketEncoder.hpp"
#include "../net/PacketDecoder.hpp"
#include "../physics/LightEngine.hpp"
#include "../physics/Fluids.hpp"
#include "../physics/Redstone.hpp"
#include "../worldgen/PortalHandler.hpp"
#include "../core/Json.hpp"
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
#include "BehaviorTree.hpp"
#include "BehaviorTreeParser.hpp"
#include "EquipmentComponent.hpp"
#include "DamageComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "MobSpawner.hpp"
#include "BossAI.hpp"
#include "MenuLogic.hpp"
#include "CostCalculator.hpp"
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
                // BlockEvent: fire onBlockBreak (plan7)
                {
                    blockEventDispatcher().onBlockBreak(p->digX, p->digY, p->digZ, oldState, p);
                    api::BlockBreakEvent bev2; bev2.player=p; bev2.x=p->digX; bev2.y=p->digY; bev2.z=p->digZ; bev2.oldState=oldState;
                    (void)bev2;
                }
                onBlockMined(*p, oldState);
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
                                          st.itemId, static_cast<std::uint8_t>(st.count),
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

void GameServer::syncPlayerArmorAttributes(Player& p) {
    // modular split: delegate to CombatManager (plan8)
    CombatManager::syncPlayerArmor(*this, p);
}
int GameServer::computeProtectionEPF(const DamageSource& ds, const Player& p) const {
    // Plan8 EnchantmentHelper: delegate EPF calculation to centralized helper
    if (ds.bypassEnchant || ds.isDrown() || ds.isStarveFlag) return 0;
    int total = 0;
    for (int i = 5; i <= 8; ++i) {
        if (i < 0 || i >= 46 || p.inv[i].empty()) continue;
        const auto& s = p.inv[i];
        total += EnchantmentHelper::getProtectionEPF(ds, s);
    }
    if (total > 20) total = 20;
    return total;
}
int GameServer::computeProtectionEPF(const DamageSource& ds, const MobEntity& m) const {
    // modular split: delegate to CombatManager (plan8)
    return CombatManager::computeEPF(ds, m);
}
void GameServer::addHungerExhaustion(Player& p, float amount) {
    // modular split: delegate to HungerManager (plan8)
    HungerManager::addExhaustion(p, amount);
}
void GameServer::addFoodAndSaturation(Player& p, int food, float sat) {
    HungerManager::addFoodAndSaturation(p, food, sat);
    sendSetHealth(p);
}
void GameServer::handleFoodConsume(Player& p, const std::string& itemName) {
    // modular split: delegate to HungerManager (plan8)
    HungerManager::handleFoodConsume(p, itemName, *this);
}
bool handleCakeBlockConsume(GameServer& srv, Player& p, std::int32_t x, std::int32_t y, std::int32_t z){
    // modular split: delegate to HungerManager (plan8)
    return HungerManager::handleCakeBlockConsume(srv, p, x, y, z);
}
void GameServer::applyDamage(Player& p, float amount, const DamageSource& src) {
    if (p.gamemode == 1 || p.gamemode == 3) return;
    if (amount <= 0 || p.dead) return;
    syncPlayerArmorAttributes(p);
    int armor = (int)std::round(p.attributes.getValue(Attribute::ARMOR));
    if (armor == 0) armor = totalArmorPoints(p.inv);
    double toughness = p.attributes.getValue(Attribute::ARMOR_TOUGHNESS);
    int epf = computeProtectionEPF(src, p);
    float finalAmt = DamageCalculator::calculate(amount, src, armor, toughness, epf, p.effects);
    if (finalAmt <= 0) return;
    // attack exhaustion for attacker is handled elsewhere; damage taken also adds exhaustion
    if (p.gamemode == 0) addHungerExhaustion(p, 0.1f);
    p.health -= finalAmt;
    p.hurtCooldown = 10;
    if (p.health <= 0) { p.health = 0; killPlayer(p, src.type.c_str()); }
    sendSetHealth(p);
    if (p.conn) {
        WriteBuffer de;
        de.varint(p.entityId);
        int dtid = gameData_.idOf("minecraft:damage_type", std::string("minecraft:") + src.type);
        if (dtid < 0) dtid = gameData_.idOf("minecraft:damage_type", "minecraft:generic");
        if (dtid < 0) dtid = 0;
        de.varint(dtid >= 0 ? dtid : 0);
        de.varint(0); de.varint(0);
        de.boolean(false);
        try { p.conn->sendPacket(pl::sc::DamageEvent, de); } catch (...) {}
        broadcastPacketExcept(&p, pl::sc::DamageEvent, de);
    }
}
void GameServer::applyDamage(Player& p, float amount, const char* cause) {
    DamageSource src(cause ? std::string(cause) : std::string("generic"));
    applyDamage(p, amount, src);
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
    tickBorderLerp();
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
    mark('P');
    projectilesTick();
    primedTntsTick();
    mark('Q');
    tntTick();
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
    // network batching: flush coalesced block updates every tick (50ms window)
    {
        int64_t now = nowMs();
        if (!batcher_.empty() && now - batcher_.lastFlushMs >= 50) {
            batcher_.flush(*this, nullptr);
            lastBlockBatchFlushMs_ = now;
        } else if (!batcher_.empty() && tickNo_ % 2 == 0) {
            flushBlockBatches();
        }
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

void GameServer::broadcastBlockChange(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
    queueBlockChange(x, y, z, state);
    invalidateChunkCache(x >> 4, z >> 4);
}

void GameServer::queueBlockChange(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
    WriteBuffer b;
    b.position(x, y, z);
    b.varint(state);
    batcher_.queuePacket(proto::pl::sc::BlockUpdate, std::move(b));
    if (batcher_.size() >= 64) {
        flushBlockBatches();
    }
}

void GameServer::flushBlockBatches() {
    if (batcher_.empty()) return;
    int64_t now = nowMs();
    if (batcher_.size() < 64 && now - lastBlockBatchFlushMs_ < 50) return;
    batcher_.flush(*this, nullptr);
    lastBlockBatchFlushMs_ = now;
}

void GameServer::broadcastPlayerChat(Player& sender, const std::string& message, int64_t timestamp) {
    // Delegates to full variant with empty salt/signature (legacy path)
    WriteBuffer b;
    b.uuid(sender.uuid.data());
    b.varint(0); // index
    b.boolean(false); // no signature present flag (simple)
    b.string(message);
    b.i64(timestamp);
    b.i64(0); // salt
    // signature field
    b.varint(0); // sig len 0
    b.boolean(false); // no unsigned content?
    b.varint(0); // filter mask
    b.varint(0);
    nbt::writeTextComponent(b, sender.name);
    b.boolean(false);
    broadcastPacketExcept(nullptr, proto::pl::sc::PlayerChat, b);
}
// Full variant with salt+signature for verified sessions (plan10 §5)
void GameServer::broadcastPlayerChat(Player& sender, const std::string& message, int64_t timestamp, int64_t salt, const std::vector<uint8_t>& signature) {
    WriteBuffer b;
    b.uuid(sender.uuid.data());
    b.varint(sender.lastChatAckOffset); // index / offset
    if (!signature.empty()) {
        b.boolean(true);
        b.varint(static_cast<int32_t>(signature.size()));
        b.raw(signature.data(), signature.size());
    } else {
        b.boolean(false);
    }
    b.string(message);
    b.i64(timestamp);
    b.i64(salt);
    // signature already handled above as optional; for 1.21.4 the packet also carries message signature after salt
    // but we already encoded it as "has signature" before message; keep compatibility:
    // Ensure we also support fallback where signature is empty => just send without
    // Append chat formatting and unsigned fields for vanilla parity:
    // chatType, network name, target name
    // Simplified vanilla: after salt/sig comes unsigned body checks
    // For plan10 compliance we ensure PlayerChat packet is distinguished from SystemChat:
    // Add chat formatting id 0 and network name.
    // Note: vanilla PlayerChat also contains lastSeen update – we approximate as zero.
    if (signature.empty()) {
        // Need to fill remaining vanilla fields to avoid client mis-parse: we already wrote empty sig above
        // Now add chat formatting placeholders
        b.varint(0);
        b.boolean(false);
        b.varint(0);
        b.varint(0);
    } else {
        b.varint(0);
        b.boolean(false);
        b.varint(0);
        b.varint(0);
    }
    nbt::writeTextComponent(b, sender.name);
    b.boolean(false);
    broadcastPacketExcept(nullptr, proto::pl::sc::PlayerChat, b);
    // Also send a DisguisedChat fallback is not needed; PlayerChat is sufficient.
}

bool GameServer::validateFeatureFlags(const std::vector<std::array<std::string,3>>& clientPacks) {
    if (!clientPacks.empty()) return false;
    return true;
}

void GameServer::survivalTick() {
    const auto now = nowMs();
    for (auto& pp : playersSnapshot()) {
        auto* p = pp.get();
        if (!p->inPlay || !p->spawned || p->dead) continue;
        if (p->gamemode != 0) continue;                  // survival only

        // exhaustion -> saturation/food (modular: HungerManager)
        HungerManager::tickExhaustion(*p, *this);
        // natural regeneration / starvation (modular: HungerManager)
        HungerManager::tickRegenAndStarve(*p, tickNo_, *this);
        // void damage
        if (p->y < kMinY - 16) applyDamage(*p, 4.f, "fell out of the world");

        // ---- drowning (plan5 76)
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
            if (!headInWater) {
                // also check if eye is inside waterlogged? simplified: check water at feet for swimming
                // reset air
                if (p->airTicks != 300) {
                    p->airTicks = 300;
                }
            } else {
                if (!hasWaterBreathing && gamerules_.getBool("drowningDamage")) {
                    p->airTicks = std::max(0, p->airTicks - 1);
                    if (p->airTicks <= 0) {
                        if (tickNo_ % 20 == 0) applyDamage(*p, 1.f, "drown");
                    }
                } else {
                    // with water breathing, don't decrement, slowly recover if needed
                    if (p->airTicks < 300) p->airTicks = std::min(300, p->airTicks + 4);
                }
            }
        }
        // ---- freeze (powder snow) 77
        {
            auto isPowderSnowAt = [&](int bx,int by,int bz)->bool {
                uint16_t st = worldFor(p->dimension).getBlock(bx,by,bz);
                auto *d = gen::blockByState(st);
                return d && d->name == "minecraft:powder_snow";
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
            if (inSnow) {
                p->freezeTicks = std::min(300, p->freezeTicks + 1);
                if (p->freezeTicks >= 140) {
                    if (gamerules_.getBool("freezeDamage") && tickNo_ % 20 == 0) applyDamage(*p, 1.f, "freeze");
                }
            } else {
                p->freezeTicks = std::max(0, p->freezeTicks - 1);
            }
        }
        // ---- fire / lava 77-78
        {
            auto isFireOrLavaAt = [&](double px,double py,double pz)->bool {
                int bx=(int)std::floor(px); int by=(int)std::floor(py); int bz=(int)std::floor(pz);
                uint16_t st = worldFor(p->dimension).getBlock(bx,by,bz);
                auto *d = gen::blockByState(st);
                if (!d) return false;
                return d->name == "minecraft:lava" || d->name == "minecraft:fire" || d->name == "minecraft:soul_fire"
                    || d->name == "minecraft:magma_block" || d->name == "minecraft:campfire" || d->name == "minecraft:soul_campfire";
            };
            bool hasFireRes = false;
            for (auto &e: p->effects) if (e.type == effects::FireResistance) { hasFireRes = true; break; }
            bool doFire = gamerules_.getBool("doFireTick");
            bool inLavaFire = isFireOrLavaAt(p->x, p->y, p->z) || isFireOrLavaAt(p->x, p->y + 1.0, p->z);
            if (inLavaFire && !hasFireRes) {
                p->fireTicks = 160;
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
        // ---- world border damage (plan6 §10 + plan10 damagePerBlock/buffer)
        {
            double distOut = borderDistanceOutside(p->x, p->z);
            if (distOut > 0) {
                double effective = distOut - worldBorderSafeZone_;
                if (effective > 0) {
                    // vanilla: damage = floor((effective+1)*damagePerBlock) per tick? We do per second scaling
                    // Simplify: damage per second = max(1, floor(effective * damagePerBlock + 1))
                    if (tickNo_ % 20 == 0) {
                        float dmg = (float)std::max(1.0, std::floor(effective * worldBorderDamagePerBlock_ + 0.5) + 1.0);
                        // per-block scaling: at least 1, grows with distance
                        applyDamage(*p, dmg, "outside_border");
                    }
                } else {
                    // inside safe zone: no damage but warning
                    if (tickNo_ % 100 == 0 && effective > -2.0) {
                        // could send warning packet, but ignore for now
                    }
                }
            }
        }
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
                if (MobEntity::isBoss(m->kind) && bossAI_) bossAI_->onDeath(*m);
                mobAi_.erase(m->entityId);
                it = mobs_.erase(it); continue;
            }

            const auto& stats = mobStats(m->kind);
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
                        const auto& bs = mobStats(baby->kind);
                        baby->health = bs.maxHealth * 0.5f;
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
                                const auto& bs = mobStats(baby->kind);
                                baby->health = bs.maxHealth * 0.5f;
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

            // ---- Brain-Goal-Sensor AI tick (plan3) + BossAI (plan7)
            auto& ai = aiFor(m);
            ai.ctx->srv = this;
            ai.ctx->world = &world_;
            brainTickGuard_ = m.get();
            ai.brain->tick(*m, *ai.ctx, tickNo_);
            brainTickGuard_ = nullptr;
            if (MobEntity::isBoss(m->kind) && bossAI_) bossAI_->tick(*m, *ai.ctx, tickNo_);

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
                        // Plan8: Charged Creeper explodes with power 6.0 (vs 3.0 normal)
                        explodeAt(cxp, cyp + 0.5, czp, m->creeperCharged ? 6.f : 3.f);
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
                if (m->dead) {
                    deadIds.push_back(m->entityId); drops.push_back(m);
                    if ((m->kind == MobKind::Slime || m->kind == MobKind::MagmaCube) && m->slimeSize > 0) {
                        int n = 2 + (rand() % 3);
                        for (int s=0; s<n; ++s) {
                            auto baby = std::make_shared<MobEntity>();
                            baby->entityId = nextEntityId();
                            baby->kind = m->kind;
                            baby->slimeSize = m->slimeSize - 1;
                            const auto& bs = mobStats(baby->kind);
                            baby->health = bs.maxHealth * 0.5f;
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
            // ---- Plan8 Breeding/Villager/Enderman tick enhancements
            // Baby aging: age<0 increments towards 0 (20 ticks per minute vanilla ~ 20*20, but our -60*20)
            if (m->age < 0) {
                m->age++;
                if (m->age == 0) {
                    // grown up – broadcast metadata for baby flag (index 15? simplified)
                    WriteBuffer md;
                    md.varint(m->entityId);
                    md.u8(15); md.varint(0); md.boolean(false);
                    md.u8(255);
                    broadcastPacketExcept(nullptr, pl::sc::SetEntityMetadata, md);
                }
            }
            // Villager restock & gossip decay
            if (m->kind==MobKind::Villager) {
                if (tickNo_ >= m->restockUntil && m->restockUntil!=0) {
                    m->restockUntil = 0;
                    // restock sound
                    broadcastSound("minecraft:entity.villager.work_farm", m->x,m->y,m->z,1.f,1.f,"neutral");
                }
                if (tickNo_%100==0 && m->gossip>0) m->gossip--;
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
    if (auto *def = entityDataLoader_.get(MobEntity::kindName(kind))) {
        if (def->max_health > 0) mob->health = def->max_health;
        if (!def->equipment.empty()) {
            for (auto &kv : def->equipment) {
                int slot = kv.first;
                auto it = gen::itemIdByName().find(kv.second);
                if (it != gen::itemIdByName().end() && slot>=0 && slot<6) mob->equipment[slot] = ItemStack::of(it->second, 1);
            }
        }
        if (kind==MobKind::Slime || kind==MobKind::MagmaCube) {
            // slimeSize from def? use max_health scaling if present
        }
    }
    mob->x = x; mob->y = y; mob->z = z;
    mob->lastSeenMs = nowMs();
    {
        std::lock_guard lk(entsMtx_);
        mobs_.push_back(mob);
    }
    broadcastMobSpawn(*mob);
    if (MobEntity::isBoss(kind) && bossAI_) bossAI_->onSpawn(*mob);
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
    sendEquipment(mob);
}

void GameServer::sendEquipment(const MobEntity& mob) {
    // Plan8 EquipmentComponent: wrap equipment array and sync via SetEquipment
    EquipmentComponent comp(mob.equipment);
    if (!comp.hasAny()) return;
    WriteBuffer b;
    b.varint(mob.entityId);
    comp.writePayload(b);
    broadcastPacketExcept(nullptr, proto::pl::sc::SetEquipment, b);
}

void GameServer::broadcastSetPassengers(std::int32_t vehicleId) {
    std::shared_ptr<MobEntity> veh;
    {
        std::lock_guard lk(entsMtx_);
        for (auto &m : mobs_) if (m->entityId==vehicleId) { veh=m; break; }
    }
    if (!veh) return;
    WriteBuffer b;
    b.varint(vehicleId);
    if (veh->riderEntityId != -1) {
        b.varint(1);
        b.varint(veh->riderEntityId);
    } else {
        b.varint(0);
    }
    broadcastPacketExcept(nullptr, proto::pl::sc::SetPassengers, b);
}

void GameServer::broadcastSetPassengersEmpty(std::int32_t vehicleId) {
    WriteBuffer b;
    b.varint(vehicleId);
    b.varint(0);
    broadcastPacketExcept(nullptr, proto::pl::sc::SetPassengers, b);
}

float GameServer::applyArmorReduction(float dmg, int armor) const {
    if (armor <= 0 || dmg <= 0) return dmg;
    float a = static_cast<float>(armor);
    float eff = std::min(20.f, std::max(a/5.f, a - dmg/2.f));
    return dmg * (1.f - eff/25.f);
}

int GameServer::totalProtectionForPlayer(const Player& p) const {
    int prot=0;
    for (int i=5;i<=8;++i) if (i>=0 && i<46 && !p.inv[i].empty()) prot += p.inv[i].enchantLevel("protection");
    return prot;
}

int GameServer::totalProtectionForMob(const MobEntity& m) const {
    int prot=0;
    for (int i=2;i<6;++i) if (!m.equipment[i].empty()) prot += m.equipment[i].enchantLevel("protection");
    return prot;
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
        // Plan8 BehaviorTreeParser: data-driven BehaviorTree from EntityDataDef (Selector/Sequence/Condition/Action via JSON)
        // Parser is now BehaviorTreeParser::parse which delegates to EntityDataLoader for backward compat.
        if (auto* def = entityDataLoader_.get(MobEntity::kindName(m->kind))) {
            auto fresh = BehaviorTreeParser::parse(*def);
            if (!fresh) fresh = EntityDataLoader::buildUniqueTreeFor(*def);
            if (fresh) e.brain->setBehaviorTree(std::move(fresh));
            else if (m->kind==MobKind::Enderman) e.brain->setBehaviorTree(buildEndermanTree());
            else if (m->kind==MobKind::Wither) e.brain->setBehaviorTree(buildWitherTree());
            else if (m->kind==MobKind::EnderDragon) e.brain->setBehaviorTree(buildDragonTree());
        } else {
            if (m->kind==MobKind::Enderman) e.brain->setBehaviorTree(buildEndermanTree());
            else if (m->kind==MobKind::Wither) e.brain->setBehaviorTree(buildWitherTree());
            else if (m->kind==MobKind::EnderDragon) e.brain->setBehaviorTree(buildDragonTree());
        }
        // EquipmentComponent: apply equipment from definition if present (already in spawnMob)
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

// Plan8 Charged Creeper: lightning strike charges creepers within 3 blocks, spawns bolt entity & visuals
void GameServer::strikeLightning(double x, double y, double z) {
    // Visual: spawn lightning bolt entity and broadcast sound
    {
        auto bolt = std::make_shared<LightningBoltEntity>();
        bolt->entityId = nextEntityId();
        bolt->x = x; bolt->y = y; bolt->z = z;
        // Broadcast SpawnEntity for lightning (type 94? Use generic)
        WriteBuffer b;
        b.varint(bolt->entityId);
        static std::uint8_t zero[16]={};
        b.uuid(zero);
        b.varint(94); // lightning bolt entity type id (approx)
        b.f64(x); b.f64(y); b.f64(z);
        b.i8(0); b.i8(0); b.i8(0);
        b.varint(0); b.i16(0); b.i16(0); b.i16(0);
        broadcastPacketExcept(nullptr, pl::sc::SpawnEntity, b);
        broadcastSound("minecraft:entity.lightning_bolt.thunder", x,y,z, 2.f, 1.f, "weather");
        broadcastSound("minecraft:entity.lightning_bolt.impact", x,y,z, 1.f, 1.f, "weather");
    }
    // Charge creepers within 4 blocks (includes via trident channeling)
    std::lock_guard lk(entsMtx_);
    for (auto& m : mobs_) if (m->kind==MobKind::Creeper && !m->creeperCharged) {
        double dx=m->x - x, dy=m->y - y, dz=m->z - z;
        if (dx*dx + dy*dy + dz*dz < 16) {
            m->creeperCharged = true;
            // metadata update for charged creeper (index 17? simplified)
            WriteBuffer md;
            md.varint(m->entityId);
            md.u8(17); md.varint(0); md.u8(1);
            md.u8(255);
            broadcastPacketExcept(nullptr, pl::sc::SetEntityMetadata, md);
            std::fprintf(stderr, "[cppfm] creeper %d charged via lightning at %.1f %.1f %.1f\n", m->entityId, x,y,z);
        }
    }
    // Also handle Enderman damage via lightning? vanilla: enderman takes damage but teleports – already via applyDamage.
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
        // hopper lock: when powered by redstone, skip transfer (plan8 hopper fix)
        if (be.kind == BlockEntity::Kind::Hopper && redstone_ && redstone_->isPoweredHere(x, y, z)) continue;
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
                    bool isDropper=false;
                    {
                        auto* bd=gen::blockByState(world_.getBlock(x,y,z));
                        if (bd && std::string(bd->name)=="minecraft:dropper") isDropper=true;
                    }
                    std::string iname = s.name();
                    bool handled = false;
                    if (!isDropper) {
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
                        } else if (iname.find("_spawn_egg") != std::string::npos) {
                            // Plan8 MobSpawner: dispenser can spawn mobs via spawn eggs
                            MobSpawner spawner2(*this);
                            if (spawner2.spawnFromDispenser(iname, x, y, z, facing)) handled = true;
                            else {
                                // fallback to item drop if spawner fails
                                spawnItemDrop(sx, sy, sz, s.itemId, 1, dx * .25, .15, dz * .25);
                                handled = true;
                            }
                        }
                    } // dropper: skip projectile handling, fall through to item drop (plan9 #26)
                    if (!handled) {
                        if (!isDropper && (iname == "minecraft:tnt" || iname.find("tnt") != std::string::npos)) {
                            // primed TNT: explode at front with delay via explodeAt (plan10 §8 would spawn primed entity)
                            explodeAt(x + dx + 0.5, y + dy + 0.5, z + dz + 0.5, 4.f);
                        if (iname == "minecraft:tnt" || iname.find("tnt") != std::string::npos) {
                            // primed TNT entity with fuse 80 (plan10 §8)
                            spawnPrimedTnt(sx, sy, sz, dx*0.25, 0.2, dz*0.25);
                        if (iname == "minecraft:tnt" || iname.find("tnt") != std::string::npos) {
                            // plan10 §8 TNT: primed entity with fuse 80t, small random velocity
                            double rvx = dx * 0.7 + (rand()/(double)RAND_MAX - 0.5) * 0.1;
                            double rvz = dz * 0.7 + (rand()/(double)RAND_MAX - 0.5) * 0.1;
                            spawnTnt(x + dx + 0.5, y + dy + 0.5, z + dz + 0.5, rvx, 0.2, rvz, 80);
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
    case BlockEntity::Kind::Barrel: countOut = 27; return be->chest.slots;
    case BlockEntity::Kind::ShulkerBox: countOut = 27; return be->chest.slots;
    case BlockEntity::Kind::Hopper: countOut = 5; return be->generic.slots;
    case BlockEntity::Kind::Dispenser: countOut = 9; return be->generic.slots;
    case BlockEntity::Kind::Brewing: countOut = 5; return be->brewing.slots;
    case BlockEntity::Kind::Furnace: countOut = 3; return be->furnace.slots;
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
    // Plan8 Villager: XP, gossip, level progression, restock timer
    // Find nearest villager within 8 blocks to apply trade effects (simplified: first villager)
    {
        std::lock_guard lk(entsMtx_);
        for (auto& m : mobs_) if (m->kind==MobKind::Villager) {
            double dx=m->x - p.x, dz=m->z - p.z;
            if (dx*dx+dz*dz < 64) {
                m->villagerXp += 3 + (rand()%4);
                m->gossip += 2;
                // Level up check: every 10 xp -> level++
                if (m->villagerXp >= m->villagerLevel * 10 && m->villagerLevel < 5) {
                    m->villagerLevel++;
                    broadcastSound("minecraft:entity.villager.levelup", m->x,m->y,m->z,1.f,1.f,"neutral");
                }
                // Restock: set restockUntil to now+ 20*120 (6 min) if not already
                if (m->restockUntil < tickNo_) m->restockUntil = tickNo_ + 120*20;
                break;
            }
        }
    }
    return true;
}

void GameServer::applyDamageToMob(MobEntity& m, float amount, const DamageSource& src) {
    if (amount <= 0 || m.dead) return;
    // Plan8 EnchantmentHelper + EquipmentComponent: armor via EquipmentComponent, EPF via EnchantmentHelper
    int armor = totalArmorPoints(m);
    int epf = computeProtectionEPF(src, m);
    // mobs have no toughness in current formula; pass 0
    float finalAmt = DamageCalculator::calculate(amount, src, armor, 0.0, epf, {});
    // mobs have no resistance effects currently
    if (finalAmt <= 0) return;
    m.health -= finalAmt;
    m.hurtCooldown = 10;
    // Plan8 Enderman: damage triggers teleport (hurt condition already in BehaviorTree, but also instant chance)
    if (m.kind==MobKind::Enderman && !m.dead) {
        // 50% chance to teleport when hurt, respecting cooldown
        if (rand()%2==0 && tickNo_ - m.lastTeleportTick > 20) {
            // trigger teleport via AiContext next tick; also mark hurt
            m.lastTeleportTick = tickNo_; // temporary, actual teleport will happen via BehaviorTree IsHurt->TeleportRandom
            // we also update AiContext lastHurt for IsHurtCondition
            auto it = mobAi_.find(m.entityId);
            if (it!=mobAi_.end() && it->second.ctx) {
                it->second.ctx->lastHurtTick = tickNo_;
                it->second.ctx->lastHurtByEntityId = -1;
            }
        }
    }
    // Plan8 Charged Creeper: lightning handled separately; charged state persists
    if (MobEntity::isBoss(m.kind) && bossAI_) {
        if (m.health > 0) bossAI_->onDamage(m);
        else bossAI_->onDeath(m);
    }
    if (m.health <= 0) m.dead = true;
}
void GameServer::applyDamageToMob(MobEntity& m, float amount, const char* cause) {
    DamageSource src(cause ? std::string(cause) : std::string("generic"));
    applyDamageToMob(m, amount, src);
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
    // Equipment sync: armor attributes (plan8+plan9 item 30)
    syncPlayerArmorAttributes(p);
}

// ---- TNT primed (plan10 §8) ----
void GameServer::spawnPrimedTnt(double x, double y, double z, double vx, double vy, double vz) {
    auto t = std::make_shared<PrimedTntEntity>();
    t->entityId = nextEntityId();
    t->x = x; t->y = y; t->z = z;
    t->vx = vx; t->vy = vy; t->vz = vz;
    t->fuse = 80;
    {
        std::lock_guard lk(entsMtx_);
        primedTnts_.push_back(t);
    }
    WriteBuffer b;
    b.varint(t->entityId);
    static std::uint8_t zero[16]={};
    b.uuid(zero);
    // tnt entity type id via registry
    auto it = gen::entityTypeIdByName().find("minecraft:tnt");
    std::int32_t tid = it != gen::entityTypeIdByName().end() ? (std::int32_t)it->second : 68;
    b.varint(tid);
    b.f64(x); b.f64(y); b.f64(z);
    b.i8(0); b.i8(0); b.i8(0);
    b.varint(0); b.i16(0); b.i16(0); b.i16(0);
    broadcastPacketExcept(nullptr, pl::sc::SpawnEntity, b);
    WriteBuffer vel;
    vel.varint(t->entityId);
    vel.i16((std::int16_t)(vx*8000)); vel.i16((std::int16_t)(vy*8000)); vel.i16((std::int16_t)(vz*8000));
    broadcastPacketExcept(nullptr, pl::sc::EntityVelocity, vel);
}
void GameServer::primedTntsTick() {
    std::vector<std::shared_ptr<PrimedTntEntity>> toExplode;
    {
        std::lock_guard lk(entsMtx_);
        for (auto it = primedTnts_.begin(); it != primedTnts_.end();) {
            auto& t = *it;
            t->vx *= 0.98; t->vz *= 0.98;
            t->vy -= 0.04;
            t->x += t->vx; t->y += t->vy; t->z += t->vz;
            // ground clamp
            world_.generateChunkIfMissing((std::int32_t)t->x>>4,(std::int32_t)t->z>>4);
            std::uint16_t below = world_.getBlock((std::int32_t)t->x, (std::int32_t)t->y -1, (std::int32_t)t->z);
            bool solid = below != 0;
            if (solid && t->y < -60 + 5) { // simple ground
                t->y = std::floor(t->y)+0.49;
                t->vy *= -0.3;
                if (std::abs(t->vy) < 0.05) t->vy = 0;
                t->vx *= 0.7; t->vz *= 0.7;
            }
            if (--t->fuse <= 0) {
                toExplode.push_back(t);
                it = primedTnts_.erase(it);
            } else ++it;
        }
    }
    for (auto& t : toExplode) {
        // remove entity
        WriteBuffer rm; rm.varint(1); rm.varint(t->entityId);
        broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, rm);
        explodeAt(t->x, t->y, t->z, 4.0f);
    }
}

// ---- Teams / BossBar / Tags / Equipment (plan10 §6) ----
bool GameServer::createTeam(const std::string& name, const std::string& display) {
    if (!scoreboard.addTeam(name, display)) return false;
    auto* t = scoreboard.findTeam(name);
    if (!t) return false;
    WriteBuffer b;
    scoreboard.writeTeamsCreate(b, *t);
    broadcastPacketExcept(nullptr, pl::sc::Teams, b);
    return true;
}
bool GameServer::removeTeam(const std::string& name) {
    auto* t = scoreboard.findTeam(name);
    if (!t) return false;
    WriteBuffer b;
    scoreboard.writeTeamsRemove(b, name);
    broadcastPacketExcept(nullptr, pl::sc::Teams, b);
    scoreboard.removeTeam(name);
    return true;
}
bool GameServer::joinTeam(const std::string& team, const std::string& member) {
    // auto-leave previous team (vanilla)
    scoreboard.leaveTeam(member);
    if (!scoreboard.addTeamMember(team, member)) return false;
    WriteBuffer b;
    scoreboard.writeTeamsAddPlayers(b, team, std::vector<std::string>{member});
    broadcastPacketExcept(nullptr, pl::sc::Teams, b);
    return true;
}
bool GameServer::leaveTeam(const std::string& member) {
    std::string prev;
    for (auto& kv : scoreboard.teams) {
        auto& mems = kv.second.members;
        if (std::find(mems.begin(), mems.end(), member) != mems.end()) { prev = kv.first; break; }
    }
    if (prev.empty()) return false;
    if (!scoreboard.removeTeamMember(prev, member)) return false;
    WriteBuffer b;
    scoreboard.writeTeamsRemovePlayers(b, prev, std::vector<std::string>{member});
    broadcastPacketExcept(nullptr, pl::sc::Teams, b);
    return true;
}
bool GameServer::createBossBar(const std::string& id, const std::string& title) {
    if (genericBossBars_.count(id)) return false;
    BossBar bar;
    // derive uuid from id hash
    std::hash<std::string> h;
    std::size_t hv = h(id);
    for (int i=0;i<16;++i) bar.uuid[i] = std::uint8_t((hv >> (i*3)) & 0xFF);
    bar.uuid[6] = (bar.uuid[6] & 0x0F) | 0x40;
    bar.uuid[8] = (bar.uuid[8] & 0x3F) | 0x80;
    bar.title = title;
    bar.health = 1.0f;
    bar.color = 5;
    bar.division = 0;
    bar.flags = 0;
    genericBossBars_[id] = bar;
    WriteBuffer b;
    b.uuid(bar.uuid.data());
    b.varint(0);
    nbt::writeTextComponent(b, bar.title);
    b.f32(bar.health);
    b.varint(bar.color);
    b.varint(bar.division);
    b.u8(bar.flags);
    broadcastPacketExcept(nullptr, pl::sc::BossBar, b);
    return true;
}
bool GameServer::removeBossBar(const std::string& id) {
    auto it = genericBossBars_.find(id);
    if (it == genericBossBars_.end()) return false;
    WriteBuffer b;
    b.uuid(it->second.uuid.data());
    b.varint(1);
    broadcastPacketExcept(nullptr, pl::sc::BossBar, b);
    genericBossBars_.erase(it);
    return true;
}
bool GameServer::tagAdd(Player* p, const std::string& tag) {
    if (!p) return false;
    auto res = p->tags.insert(tag);
    return res.second;
}
bool GameServer::tagRemove(Player* p, const std::string& tag) {
    if (!p) return false;
    return p->tags.erase(tag) > 0;
}
bool GameServer::tagAddMob(std::int32_t eid, const std::string& tag) {
    std::lock_guard lk(entsMtx_);
    for (auto& m : mobs_) if (m->entityId==eid) { auto res=m->tags.insert(tag); return res.second; }
    return false;
}
bool GameServer::tagRemoveMob(std::int32_t eid, const std::string& tag) {
    std::lock_guard lk(entsMtx_);
    for (auto& m : mobs_) if (m->entityId==eid) return m->tags.erase(tag)>0;
    return false;
}
void GameServer::updateMobEquipment(std::int32_t eid, int slot, const ItemStack& stack) {
    std::lock_guard lk(entsMtx_);
    for (auto& m : mobs_) if (m->entityId==eid) {
        if (slot>=0 && slot<6) m->equipment[slot]=stack;
        syncMobEquipment(*m);
        break;
    }
}
void GameServer::syncMobEquipment(const MobEntity& mob) {
    sendEquipment(mob);
}
void GameServer::handleHorseJump(Player& p, int jumpPower) {
    if (p.vehicleId==-1) return;
    // find vehicle mob (horse)
    std::shared_ptr<MobEntity> veh;
    {
        std::lock_guard lk(entsMtx_);
        for (auto& m : mobs_) if (m->entityId==p.vehicleId) { veh=m; break; }
    }
    if (!veh) return;
    if (veh->kind!=MobKind::Horse) return;
    double power = std::clamp(jumpPower / 100.0, 0.0, 1.0);
    // apply vertical velocity to horse
    veh->y += power * 1.2;
    // broadcast position
    WriteBuffer b;
    b.varint(veh->entityId);
    b.f64(veh->x); b.f64(veh->y); b.f64(veh->z);
    b.f32(veh->yaw); b.f32(0);
    b.boolean(true);
    broadcastPacketExcept(nullptr, pl::sc::EntityTeleport, b);
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

void GameServer::loadOps() {
    ops_.clear();
    try {
        std::ifstream f("ops.json");
        if (!f) return;
        std::string txt((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto v = json::Value::parse(txt);
        if (v.isArr()) {
            for (auto& e : v.arr) {
                if (e.isObj()) {
                    if (auto* n = e.find("name")) ops_.insert(n->asStr());
                } else if (e.isStr()) ops_.insert(e.asStr());
            }
        } else if (v.isObj()) {
            for (auto& [k,_] : v.obj) ops_.insert(k);
        }
    } catch (...) {}
    // also allow ops.txt one name per line fallback
    try {
        std::ifstream f2("ops.txt");
        std::string line;
        while (std::getline(f2, line)) {
            if (!line.empty() && line.back()=='\r') line.pop_back();
            if (!line.empty()) ops_.insert(line);
        }
    } catch (...) {}
}
void GameServer::setBorderLerp(double from, double to, std::int64_t durationTicks) {
    worldBorderLerpFrom_ = from;
    worldBorderLerpTo_ = to;
    worldBorderLerpStartTick_ = tickNo_;
    worldBorderLerpEndTick_ = tickNo_ + std::max<std::int64_t>(0, durationTicks);
    if (durationTicks <= 0) {
        worldBorderDiameter_ = to;
        worldBorderLerpFrom_ = to;
        worldBorderLerpTo_ = to;
    }
}
void GameServer::tickBorderLerp() {
    if (worldBorderLerpEndTick_ <= worldBorderLerpStartTick_) return;
    double cur = currentBorderDiameter();
    if (tickNo_ >= worldBorderLerpEndTick_) {
        worldBorderDiameter_ = worldBorderLerpTo_;
        worldBorderLerpFrom_ = worldBorderLerpTo_;
        worldBorderLerpStartTick_ = worldBorderLerpEndTick_;
        // persist final
        if (persist_) persist_->setWorldBorder(worldBorderDiameter_, worldBorderCenterX_, worldBorderCenterZ_);
        return;
    }
    // update diameter for persistence check (not every tick to avoid spam)
    if (tickNo_ % 20 == 0) {
        worldBorderDiameter_ = cur;
    }
}
void GameServer::sendWorldBorderTo(Player& p) const {
    if (!p.conn) return;
    double curDia = currentBorderDiameter();
    double targetDia = worldBorderLerpEndTick_ > worldBorderLerpStartTick_ && tickNo_ < worldBorderLerpEndTick_ ? worldBorderLerpTo_ : curDia;
    std::int64_t remainingTicks = std::max<std::int64_t>(0, worldBorderLerpEndTick_ - tickNo_);
    std::int64_t lerpMs = remainingTicks * 50;
    // InitializeWorldBorder full packet (vanilla: x,z,oldDia,newDia,speed,portalBoundary,warningTime,warningBlocks)
    WriteBuffer i;
    i.f64(worldBorderCenterX_); i.f64(worldBorderCenterZ_);
    // old vs new diameter for lerp
    if (remainingTicks > 0) {
        i.f64(worldBorderLerpFrom_); i.f64(worldBorderLerpTo_);
        i.varlong(lerpMs);
    } else {
        i.f64(curDia); i.f64(curDia);
        i.varlong(0);
    }
    i.varint(29999984); // max portal boundary
    i.varint(worldBorderWarningTime_);
    i.varint(worldBorderWarningBlocks_);
    try { p.conn->sendPacket(proto::pl::sc::InitializeWorldBorder, i); } catch (...) {}
    // also send separate Center / Size / Lerp for spec compliance (clients may listen to any)
    {
        WriteBuffer c;
        c.f64(worldBorderCenterX_); c.f64(worldBorderCenterZ_);
        try { p.conn->sendPacket(proto::pl::sc::WorldBorderCenter, c); } catch (...) {}
    }
    {
        WriteBuffer s;
        s.f64(curDia);
        try { p.conn->sendPacket(proto::pl::sc::WorldBorderSize, s); } catch (...) {}
    }
    if (remainingTicks > 0) {
        WriteBuffer ls;
        ls.f64(worldBorderLerpFrom_); ls.f64(worldBorderLerpTo_);
        ls.varlong(lerpMs);
        try { p.conn->sendPacket(proto::pl::sc::WorldBorderLerpSize, ls); } catch (...) {}
    }
    {
        WriteBuffer wd;
        wd.varint(worldBorderWarningTime_);
        try { p.conn->sendPacket(proto::pl::sc::WorldBorderWarningDelay, wd); } catch (...) {}
        WriteBuffer wb;
        wb.varint(worldBorderWarningBlocks_);
        try { p.conn->sendPacket(proto::pl::sc::WorldBorderWarningReach, wb); } catch (...) {}
    }
}
void GameServer::broadcastWorldBorder() {
    for (auto& p : playersSnapshot()) {
        if (!p->inPlay || !p->conn) continue;
        sendWorldBorderTo(*p);
    }
    if (persist_) {
        // persist current effective diameter and lerp target
        persist_->setWorldBorder(currentBorderDiameter(), worldBorderCenterX_, worldBorderCenterZ_);
        persist_->setWorldBorderLerp(worldBorderLerpFrom_, worldBorderLerpTo_, worldBorderLerpEndTick_ - tickNo_);
        persist_->saveLevelData(tickNo_, dayTime());
    }
}

std::string GameServer::dispatchConsole(const std::string& line) {
    brigadier::CommandSource src;
    src.console = true;
    src.srcX = 0; src.srcY = -60; src.srcZ = 0;
    src.resolveSelector = [this](const std::string& raw,
                                 brigadier::SelectorResult& out) {
        out = resolveSelector(raw, nullptr);
    };
    const auto res = commands_.execute(line, std::move(src));
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
    // plan6 §7: send InitializeWorldBorder on join
    srv_.sendWorldBorderTo(*self_);

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
    if (srv_.bossAI()) srv_.bossAI()->onPlayerJoin(*self_);
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

void Session::onEnchantItem(ReadBuffer& in) {
    int windowId = 0;
    int button = 0;
    try {
        windowId = in.varint();
        if (in.remaining() > 0) button = in.varint();
        else button = 0;
    } catch (...) {
        try { windowId = in.u8(); button = in.u8(); } catch(...) { return; }
    }
    if (!openMenu_ || openMenu_->windowId != windowId) return;
    if (openMenu_->type != MenuType::Enchantment) return;
    auto* logic = getMenuLogic(MenuType::Enchantment);
    if (!logic) return;
    auto* ench = dynamic_cast<EnchantmentMenuLogic*>(logic);
    if (!ench) return;
    int bs = 0;
    if (openMenu_->blockKey >= 0) {
        int bx = posKeyUnpackX(openMenu_->blockKey);
        int by = posKeyUnpackY(openMenu_->blockKey);
        int bz = posKeyUnpackZ(openMenu_->blockKey);
        for (int dx = -2; dx <= 2; ++dx)
            for (int dz = -2; dz <= 2; ++dz)
                for (int dy = 0; dy <= 1; ++dy) {
                    if (dx == 0 && dz == 0) continue;
                    auto st = srv_.world().getBlock(bx + dx, by + dy, bz + dz);
                    auto* d = gen::blockByState(st);
                    if (d && std::string(d->name) == "minecraft:bookshelf") ++bs;
                }
        if (bs > 15) bs = 15;
    } else {
        bs = 15;
    }
    struct LocalIo : MenuIo {
        Session& s;
        explicit LocalIo(Session& ss): s(ss){}
        void dropFromPlayer(Player& p, const ItemStack& stack, bool whole) override {
            s.server().spawnItemDrop(p.x, p.y + 1.2, p.z, stack.itemId, static_cast<std::uint8_t>(whole?stack.count:1), 0,0.15,0);
        }
        void blockEntityChanged(std::int64_t key) override { s.server().blockEntities().dirty_.insert(key); }
        void itemCrafted(Player& p, const ItemStack& result) override { s.server().onItemObtained(p,result,"crafted"); }
        void itemSmelted(Player& p, const ItemStack& result) override { s.server().onItemObtained(p,result,"smelted"); }
    } io(*this);
    bool ok = ench->onEnchantButton(*openMenu_, *self_, button, io, bs);
    if (ok) {
        auto costs = CostCalculator::enchantingCostsForShelves(*self_, bs);
        for (int i = 0; i < 3; ++i) {
            WriteBuffer pb;
            pb.u8(static_cast<std::uint8_t>(openMenu_->windowId));
            pb.i16(static_cast<std::int16_t>(i));
            pb.i16(costs[i]);
            try { conn_->sendPacket(pl::sc::ContainerSetData, pb); } catch (...) {}
        }
        sendMenuContent(*openMenu_);
        syncCursorItem();
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
            if (it->type == effects::Regeneration &&
                tickNo_ % std::max(1, 50 >> it->amplifier) == 0)
                p->health = std::min(20.f, p->health + 1.f), sendSetHealth(*p);
            if ((it->type == effects::Poison || it->type == effects::Wither) &&
                tickNo_ % std::max(1, 40 >> it->amplifier) == 0)
                applyDamage(*p, 1.f, it->type == effects::Poison ? "poison" : "wither");
            if (it->type == effects::Saturation && tickNo_ % std::max(1, 2 >> it->amplifier) == 0) {
                addFoodAndSaturation(*p, 1, float(it->amplifier + 1));
            }
            if (it->type == effects::Hunger && tickNo_ % 30 == 0) {
                addHungerExhaustion(*p, 0.005f * float(it->amplifier + 1) * 20.f);
            }
            if (it->type == effects::Wither && tickNo_ % 40 == 0) {
                // wither already handled
            }
            ++it;
        }
        (void)changed;
        // per-tick metadata effects: invisibility/glowing/levitation/slow-falling
        if (hasEffect(p->effects, effects::Levitation)) {
            int amp = amplifierFor(p->effects, effects::Levitation);
            // levitate upward ~0.05*(amp+1) per tick, clamped
            double dy = 0.05 * double(amp + 1);
            p->y += dy;
            // broadcast movement for levitation
            if (p->conn) {
                WriteBuffer lev;
                lev.varint(p->entityId);
                lev.f64(p->x); lev.f64(p->y); lev.f64(p->z);
                lev.i8((int8_t)(p->yaw*256.f/360.f)); lev.i8((int8_t)(p->pitch*256.f/360.f));
                lev.boolean(p->onGround);
                try { broadcastPacketExcept(nullptr, pl::sc::EntityTeleport, lev); } catch(...) {}
            }
        }
        if (hasEffect(p->effects, effects::SlowFalling)) {
            if (p->fallDist > 0) p->fallDist *= 0.9; // reduce fall distance
        }
    }
    for (auto &pp2 : playersSnapshot()) {
        auto* p2 = pp2.get();
        if (!p2->inPlay || !p2->conn) continue;
        p2->attributes.applyEffectModifiers(p2->effects);
        if (tickNo_ % 20 == 0 && (!p2->effects.empty() || p2->attributes.getValue(Attribute::MOVEMENT_SPEED) != 0.10
            || p2->attributes.getValue(Attribute::MAX_HEALTH) != 20.0
            || p2->attributes.getValue(Attribute::ARMOR) != 0
            || p2->attributes.getValue(Attribute::ATTACK_DAMAGE) != 1.0)) {
            WriteBuffer ab;
            p2->attributes.writeUpdate(ab, p2->entityId);
            try { p2->conn->sendPacket(pl::sc::UpdateAttributes, ab); } catch(...) {}
            try { broadcastPacketExcept(p2, pl::sc::UpdateAttributes, ab); } catch(...) {}
        }
        // sync invisibility/glowing metadata: index 0 flags, index 6 pose already
        if (tickNo_ % 20 == 0) {
            bool invis = isInvisible(p2->effects);
            bool glow = isGlowing(p2->effects);
            if (invis || glow) {
                WriteBuffer md;
                md.varint(p2->entityId);
                if (invis) { md.u8(0); md.varint(0); md.u8(0x20); }
                if (glow) { md.u8(0); md.varint(0); md.u8(0x40); }
                md.u8(255);
                if (md.data.size() > 2) try { broadcastPacketExcept(nullptr, pl::sc::SetEntityMetadata, md); } catch(...) {}
            }
        }
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

void GameServer::brewingTick() {
    // Brewing stand: fuel (blaze powder -> 20 fuel) + brewTime 400 ticks.
    const auto itBlaze = gen::itemIdByName().find("minecraft:blaze_powder");
    const std::uint32_t blazeId = itBlaze != gen::itemIdByName().end() ? itBlaze->second : 0;
    blockEntities_.forEach([&](std::int64_t key, BlockEntity& be) {
        if (be.kind != BlockEntity::Kind::Brewing) return;
        BrewingData& b = be.brewing;
        // replenish fuel from blaze powder in slot 4
        if (b.fuel <= 0 && !b.slots[4].empty() && (blazeId == 0 || b.slots[4].itemId == blazeId)) {
            if (--b.slots[4].count <= 0) b.slots[4] = ItemStack::air();
            b.fuel = 20;
            blockEntities_.dirty_.insert(key);
        }
        if (b.brewTime > 0) {
            --b.brewTime;
            blockEntities_.dirty_.insert(key);
            if (b.brewTime == 0) {
                uint32_t ingId = b.slots[3].empty() ? 0 : b.slots[3].itemId;
                if (!b.slots[3].empty()) {
                    for (int i = 0; i < 3; ++i) {
                        if (b.slots[i].empty()) continue;
                        std::string ingName;
                        for (auto &e : gen::kItems) if (e.second == ingId) { ingName = e.first; break; }
                        if (!ingName.empty()) {
                            b.slots[i].components.erase(
                                std::remove_if(b.slots[i].components.begin(), b.slots[i].components.end(),
                                               [](auto& pr){ return pr.first==99; }),
                                b.slots[i].components.end());
                            std::vector<uint8_t> payload(ingName.begin(), ingName.end());
                            b.slots[i].components.emplace_back(99, std::move(payload));
                        }
                    }
                    if (--b.slots[3].count <= 0) b.slots[3] = ItemStack::air();
                    blockEntities_.dirty_.insert(key);
                } else {
                    b.brewTime = 0;
                }
            }
        } else {
            bool hasIngredient = !b.slots[3].empty();
            bool hasPotion = !b.slots[0].empty() || !b.slots[1].empty() || !b.slots[2].empty();
            if (hasIngredient && hasPotion && b.fuel > 0) {
                --b.fuel;
                b.brewTime = 400;
                blockEntities_.dirty_.insert(key);
            }
        }
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
        // Mending: repair damaged item with XP before adding to player (plan9 item 32)
        bool mendingApplied = false;
        for (int i=0; i<46 && !mendingApplied; ++i) {
            auto& s = p.inv[i];
            if (!s.empty() && EnchantmentHelper::hasMending(s) && s.getDamage()>0) {
                int dmg = s.getDamage();
                int repair = pk.orb->value * 2;
                int newDmg = dmg - repair;
                if (newDmg < 0) {
                    int excessXp = (-newDmg + 1)/2;
                    s.setDamage(0);
                    resendInventory(p);
                    p.xp.addPoints(excessXp);
                    sendSetExperience(p);
                } else {
                    s.setDamage(newDmg);
                    resendInventory(p);
                    // no XP to player, orb consumed for repair
                    sendSetExperience(p);
                }
                mendingApplied = true;
            }
        }
        if (!mendingApplied) {
            p.xp.addPoints(pk.orb->value);
            sendSetExperience(p);
        }
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
                    if (pr->kind == ProjectileKind::Arrow) {
                        pr->stuck = true;
                    } else if (pr->kind == ProjectileKind::EnderPearl) {
                        // pearl teleport: find owner player and teleport
                        Player* owner = nullptr;
                        for (auto &pp : playersSnapshot()) if (pp->entityId == pr->ownerId && pr->ownerIsPlayer) { owner = pp.get(); break; }
                        if (owner) {
                            double tx = pr->x + 0.5;
                            double ty = pr->y + 0.5;
                            double tz = pr->z + 0.5;
                            // clamp to avoid inside block: raise by 0.5
                            owner->x = tx; owner->y = ty; owner->z = tz;
                            // teleport packet
                            if (owner->conn) {
                                WriteBuffer tb;
                                tb.varint(0); // teleport id not tracked for pearl? use 0
                                tb.f64(tx); tb.f64(ty); tb.f64(tz);
                                tb.f64(0); tb.f64(0); tb.f64(0);
                                tb.f32(owner->yaw); tb.f32(owner->pitch);
                                tb.u32(0);
                                try { owner->conn->sendPacket(proto::pl::sc::PlayerPosition, tb); } catch(...) {}
                            }
                            // broadcast to others
                            {
                                WriteBuffer tp;
                                tp.varint(owner->entityId);
                                tp.f64(tx); tp.f64(ty); tp.f64(tz);
                                tp.i8(static_cast<int8_t>(owner->yaw*256.f/360.f));
                                tp.i8(static_cast<int8_t>(owner->pitch*256.f/360.f));
                                tp.boolean(false);
                                broadcastPacketExcept(nullptr, proto::pl::sc::EntityTeleport, tp);
                            }
                            applyDamage(*owner, 5.f, "fall");
                            owner->lastEnderPearlTick = tickNo_;
                            // cooldown packet
                            if (owner->conn) {
                                auto pid = gen::itemIdByName().find("minecraft:ender_pearl");
                                if (pid != gen::itemIdByName().end()) {
                                    WriteBuffer cd;
                                    cd.varint(static_cast<int32_t>(pid->second));
                                    cd.varint(20*3); // 3 sec
                                    try { owner->conn->sendPacket(proto::pl::sc::SetCooldown, cd); } catch(...) {}
                                }
                            }
                        }
                        despawn.push_back(pr->entityId);
                        it = projectiles_.erase(it); continue;
                    } else {
                        despawn.push_back(pr->entityId);
                        it = projectiles_.erase(it); continue;
                    }
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

void GameServer::spawnTnt(double x,double y,double z,double vx,double vy,double vz,int fuse) {
    auto t = std::make_shared<TntEntity>();
    t->entityId = nextEntityId();
    t->x=x; t->y=y; t->z=z; t->vx=vx; t->vy=vy; t->vz=vz; t->fuse=fuse;
    {
        std::lock_guard lk(entsMtx_);
        tnts_.push_back(t);
    }
    WriteBuffer b;
    b.varint(t->entityId);
    static std::uint8_t zero[16]={};
    b.uuid(zero);
    auto tid = gen::entityTypeIdByName().find("minecraft:tnt");
    b.varint(tid!=gen::entityTypeIdByName().end()? (int32_t)tid->second : 68);
    b.f64(x); b.f64(y); b.f64(z);
    b.i8(0); b.i8(0); b.i8(0);
    b.varint(0); b.i16(0); b.i16(0); b.i16(0);
    broadcastPacketExcept(nullptr, proto::pl::sc::SpawnEntity, b);
    // fuse as metadata (varint index 8) + also via SetEntityMetadata if needed
    if (fuse!=80) {
        WriteBuffer md; md.varint(t->entityId);
        md.u8(8); md.varint(2); md.varint(fuse);
        md.u8(255);
        broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);
    }
}

void GameServer::tntTick() {
    std::vector<std::shared_ptr<TntEntity>> toExplode;
    {
        std::lock_guard lk(entsMtx_);
        for (auto it = tnts_.begin(); it != tnts_.end();) {
            auto &t = *it;
            // gravity
            t->vy -= 0.04;
            t->x += t->vx; t->y += t->vy; t->z += t->vz;
            t->vx *= 0.98; t->vy *= 0.98; t->vz *= 0.98;
            // ground collision: simple floor check
            int bx = (int)std::floor(t->x);
            int by = (int)std::floor(t->y);
            int bz = (int)std::floor(t->z);
            world_.generateChunkIfMissing(bx>>4, bz>>4);
            if (world_.getBlock(bx, by, bz)!=0 && t->vy < 0) {
                t->vy = -t->vy * 0.4;
                t->y = by + 1.001;
                t->vx *= 0.7; t->vz *= 0.7;
            }
            // broadcast movement
            {
                WriteBuffer mv; mv.varint(t->entityId);
                mv.f64(t->x); mv.f64(t->y); mv.f64(t->z);
                mv.i8(0); mv.i8(0); mv.boolean(false);
                broadcastPacketExcept(nullptr, proto::pl::sc::EntityTeleport, mv);
            }
            if (--t->fuse <= 0) {
                toExplode.push_back(t);
                WriteBuffer rm; rm.varint(1); rm.varint(t->entityId);
                broadcastPacketExcept(nullptr, proto::pl::sc::RemoveEntities, rm);
                it = tnts_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto &t : toExplode) {
        explodeAt(t->x, t->y + 0.5, t->z, 4.f);
    }
}

bool GameServer::spawnMobByTypeName(const std::string& name, double x, double y,
                                     double z) {
    // Plan8: handle lightning_bolt via strikeLightning (charged creeper)
    if (name=="minecraft:lightning_bolt" || name=="lightning_bolt" || name=="minecraft:lightning") {
        strikeLightning(x,y,z);
        return true;
    }
    for (int i = 0; i < 46; ++i) {
        auto kind = static_cast<MobKind>(i);
        const char* n = MobEntity::kindName(kind);
        if (name == n) { spawnMob(kind, x, y, z); return true; }
    }
    if (name.find(':') == std::string::npos) {
        std::string full = "minecraft:" + name;
        for (int i = 0; i < 46; ++i) {
            auto kind = static_cast<MobKind>(i);
            if (full == MobEntity::kindName(kind)) { spawnMob(kind, x, y, z); return true; }
        }
    }
    auto it = gen::entityTypeIdByName().find(name);
    if (it != gen::entityTypeIdByName().end()) {
        for (int i = 0; i < 46; ++i) {
            auto kind = static_cast<MobKind>(i);
            if (MobEntity::typeId(kind) == it->second) { spawnMob(kind, x, y, z); return true; }
        }
    }
    return false;
}

// ------------------------------------------------------------- session io

void Session::onTabComplete(ReadBuffer& in) {
    const auto transactionId = in.varint();
    const std::string text = in.string(65536);
    (void)in.boolean();                               // assume command

    brigadier::CommandSource src;
    src.player = self_.get();
    src.name = self_->name;
    src.console = false;
    src.srcX = self_->x; src.srcY = self_->y; src.srcZ = self_->z;
    src.resolveSelector = [this](const std::string& raw,
                                 brigadier::SelectorResult& out) {
        out = srv_.resolveSelector(raw, self_.get());
    };

    const auto suggestions = srv_.commands().suggest(text, std::move(src));

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
    // Stonecutter output take (slot 1) - consume input, give result
    if (m.type == MenuType::Stonecutter && slot == 1 && mode == 0 && button == 0) {
        ItemStack* inp = m.container ? &m.container[0] : &m.extraSlots[0];
        ItemStack* out = m.container ? &m.container[1] : &m.extraSlots[1];
        if (!out->empty() && !inp->empty()) {
            if (cursorItem_.empty()) cursorItem_ = *out;
            else if (cursorItem_.itemId == out->itemId && cursorItem_.count + out->count <= 64) cursorItem_.count = static_cast<std::int16_t>(cursorItem_.count + out->count);
            else srv_.addToInventory(*self_, out->itemId, out->count);
            if (--inp->count <= 0) *inp = ItemStack::air();
            if (!inp->empty()) {
                const Recipe* r = srv_.recipes().findStonecutting(inp->itemId);
                if (r) *out = r->result;
                else *out = ItemStack::air();
            } else *out = ItemStack::air();
            sendMenuContent(m);
            syncCursorItem();
            sendSetSlot(m.windowId, self_->invStateId, 1, *out);
            return;
        }
    }
    // Anvil output take (slot 2) - charge XP, consume inputs (per-menu rename)
    if (m.type == MenuType::Anvil && slot == 2 && mode == 0 && button == 0) {
        ItemStack* out = &m.extraSlots[2];
        if (!out->empty()) {
            std::string rename = !m.anvilRename.empty() ? m.anvilRename : std::string("");
            int cost = CostCalculator::anvilCost(m.extraSlots[0], m.extraSlots[1], rename);
            if (cost < 0) cost = 0;
            if ((self_->xp.level >= cost || self_->gamemode == 1) && cost > 0 && cost <= 39) {
                if (self_->gamemode == 0) {
                    self_->xp.level -= cost;
                    GameServer::sendSetExperience(*self_);
                }
                if (cursorItem_.empty()) cursorItem_ = *out;
                else if (cursorItem_.itemId == out->itemId) cursorItem_.count = static_cast<std::int16_t>(cursorItem_.count + out->count);
                else srv_.addToInventory(*self_, out->itemId, out->count);
                if (--m.extraSlots[0].count <= 0) m.extraSlots[0] = ItemStack::air();
                if (!m.extraSlots[1].empty() && --m.extraSlots[1].count <= 0) m.extraSlots[1] = ItemStack::air();
                *out = ItemStack::air();
                m.anvilRename.clear();
                if (auto* al = dynamic_cast<AnvilMenuLogic*>(getMenuLogic(MenuType::Anvil))) al->setRenameText("");
                // refresh cost
                int newCost = CostCalculator::anvilCost(m.extraSlots[0], m.extraSlots[1], m.anvilRename);
                WriteBuffer pb;
                pb.u8(static_cast<std::uint8_t>(m.windowId));
                pb.i16(0);
                pb.i16(static_cast<std::int16_t>(newCost < 0 ? 0 : newCost));
                try { conn_->sendPacket(pl::sc::ContainerSetData, pb); } catch (...) {}
                sendMenuContent(m);
                syncCursorItem();
                return;
            }
        }
    }
    SessionMenuIo io(*this);
    // Plan7 MenuLogic dispatch — per-menu-type object-oriented handling for Anvil/Enchantment/Brewing etc.
    if (auto* logic = getMenuLogic(m.type)) {
        // Check if click is within container region; let logic handle it, fallback to generic for player inv
        int cont = m.totalSlots() - 36;
        if (slot >=0 && slot < cont) {
            bool handled = logic->onSlotClick(m, *self_, slot, button, mode, cursorItem_, io, srv_.recipes());
            if (handled) {
                logic->onContentChanged(m, *self_);
                if (m.type == MenuType::Crafting) m.refreshCraftResult(srv_.recipes());
                sendMenuContent(m);
                syncCursorItem();
                return;
            }
        }
    }
    // crafting result refresh before interaction
    m.refreshCraftResult(srv_.recipes());
    const bool changed = ClickLogic::apply(m, *self_, srv_.recipes(),
                                           slot, button, mode, cursorItem_, io);
    if (m.type == MenuType::Crafting) m.refreshCraftResult(srv_.recipes());
    // Also notify MenuLogic of content change for result recomputation (e.g., Anvil)
    if (auto* logic2 = getMenuLogic(m.type)) logic2->onContentChanged(m, *self_);
    sendMenuContent(m);
    syncCursorItem();
    // Stonecutter ghost auto update
    if (m.type == MenuType::Stonecutter) {
        ItemStack* inp = m.container ? &m.container[0] : &m.extraSlots[0];
        ItemStack* out = m.container ? &m.container[1] : &m.extraSlots[1];
        if (!inp->empty()) {
            const Recipe* r = srv_.recipes().findStonecutting(inp->itemId);
            if (r) *out = r->result;
            else *out = ItemStack::air();
        } else {
            *out = ItemStack::air();
        }
        sendSetSlot(m.windowId, self_->invStateId, 1, *out);
    }
    if (m.type == MenuType::Anvil) {
        std::string rename = !m.anvilRename.empty() ? m.anvilRename : std::string("");
        int cost = CostCalculator::anvilCost(m.extraSlots[0], m.extraSlots[1], rename);
        WriteBuffer pb;
        pb.u8(static_cast<std::uint8_t>(m.windowId));
        pb.i16(0);
        pb.i16(static_cast<std::int16_t>(cost < 0 ? 0 : cost));
        try { conn_->sendPacket(pl::sc::ContainerSetData, pb); } catch (...) {}
        if (!m.extraSlots[0].empty() && cost > 0 && cost <= 39) {
            if (m.extraSlots[2].empty()) m.extraSlots[2] = m.extraSlots[0];
            sendSetSlot(m.windowId, self_->invStateId, 2, m.extraSlots[2]);
        } else {
            m.extraSlots[2] = ItemStack::air();
            sendSetSlot(m.windowId, self_->invStateId, 2, m.extraSlots[2]);
        }
    }
    if (m.type == MenuType::Enchantment) {
        int bs = 0;
        if (m.blockKey >= 0) {
            int bx = posKeyUnpackX(m.blockKey);
            int by = posKeyUnpackY(m.blockKey);
            int bz = posKeyUnpackZ(m.blockKey);
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                    for (int dy = 0; dy <= 1; ++dy) {
                        if (dx == 0 && dz == 0) continue;
                        auto st = srv_.world().getBlock(bx + dx, by + dy, bz + dz);
                        auto* d = gen::blockByState(st);
                        if (d && std::string(d->name) == "minecraft:bookshelf") ++bs;
                    }
            if (bs > 15) bs = 15;
        }
        auto costs = CostCalculator::enchantingCostsForShelves(*self_, bs);
        for (int i = 0; i < 3; ++i) {
            WriteBuffer pb;
            pb.u8(static_cast<std::uint8_t>(m.windowId));
            pb.i16(static_cast<std::int16_t>(i));
            pb.i16(costs[i]);
            try { conn_->sendPacket(pl::sc::ContainerSetData, pb); } catch (...) {}
        }
    }
    if (m.type == MenuType::Brewing) {
        if (m.blockEntity && m.blockEntity->kind == BlockEntity::Kind::Brewing) {
            auto& b = m.blockEntity->brewing;
            for (int prop = 0; prop < 2; ++prop) {
                WriteBuffer pb;
                pb.u8(static_cast<std::uint8_t>(m.windowId));
                pb.i16(static_cast<std::int16_t>(prop));
                pb.i16(prop == 0 ? b.brewTime : b.fuel);
                try { conn_->sendPacket(pl::sc::ContainerSetData, pb); } catch (...) {}
            }
        }
    }
    if (m.type == MenuType::Furnace) {
        if (m.blockEntity && m.blockEntity->kind == BlockEntity::Kind::Furnace) {
            auto& f = m.blockEntity->furnace;
            const int props[4] = {f.cookProgress, f.cookTotal, f.burnTicks, f.burnDuration};
            for (int prop = 0; prop < 4; ++prop) {
                WriteBuffer pb;
                pb.u8(static_cast<std::uint8_t>(m.windowId));
                pb.i16(static_cast<std::int16_t>(prop));
                pb.i16(static_cast<std::int16_t>(props[prop]));
                try { conn_->sendPacket(pl::sc::ContainerSetData, pb); } catch (...) {}
            }
        }
    }
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
    } else if (name == "minecraft:furnace" || name == "minecraft:blast_furnace" ||
               name == "minecraft:smoker") {
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
    } else if (name == "minecraft:enchanting_table") {
        menu->type = MenuType::Enchantment;
        menu->container = menu->extraSlots;
        menu->containerCount = 2;
    } else if (name == "minecraft:anvil" || name == "minecraft:chipped_anvil" ||
               name == "minecraft:damaged_anvil") {
        menu->type = MenuType::Anvil;
        menu->container = menu->extraSlots;
        menu->containerCount = 3;
        menu->anvilRename.clear();
    } else if (name.find("anvil") != std::string::npos) {
        menu->type = MenuType::Anvil;
        menu->container = menu->extraSlots;
        menu->containerCount = 3;
        menu->anvilRename.clear();
    } else if (name == "minecraft:brewing_stand") {
        auto* be = srv_.blockEntities().getAt(x, y, z);
        if (!be)
            be = &srv_.blockEntities().create(menu->blockKey,
                                              BlockEntity::Kind::Brewing);
        menu->type = MenuType::Brewing;
        menu->container = be->brewing.slots;
        menu->containerCount = 5;
        menu->blockEntity = be;
    } else if (name == "minecraft:stonecutter") {
        menu->type = MenuType::Stonecutter;
        menu->container = menu->extraSlots;
        menu->containerCount = 2;
    } else if (name == "minecraft:grindstone") {
        menu->type = MenuType::Grindstone;
        menu->container = menu->extraSlots;
        menu->containerCount = 3;
    } else if (name == "minecraft:smithing_table") {
        menu->type = MenuType::Smithing;
        menu->container = menu->extraSlots;
        menu->containerCount = 4;
    } else if (name == "minecraft:beacon") {
        menu->type = MenuType::Beacon;
        menu->container = menu->extraSlots;
        menu->containerCount = 1;
    } else if (name == "minecraft:loom") {
        menu->type = MenuType::Loom;
        menu->container = menu->extraSlots;
        menu->containerCount = 4;
    } else if (name == "minecraft:barrel") {
        auto* be = srv_.blockEntities().getAt(x, y, z);
        if (!be) be = &srv_.blockEntities().create(menu->blockKey, BlockEntity::Kind::Chest);
        be->kind = BlockEntity::Kind::Barrel;
        menu->type = MenuType::Barrel;
        menu->container = be->chest.slots;
        menu->containerCount = 27;
        menu->blockEntity = be;
    } else if (name.find("shulker_box") != std::string::npos) {
        auto* be = srv_.blockEntities().getAt(x, y, z);
        if (!be)
            be = &srv_.blockEntities().create(menu->blockKey,
                                              BlockEntity::Kind::ShulkerBox);
        be->kind = BlockEntity::Kind::ShulkerBox;
        menu->type = MenuType::ShulkerBox;
        menu->container = be->chest.slots;
        menu->containerCount = ChestData::kSlots;
        menu->blockEntity = be;
    } else return;

    // Open Screen packet — plan7 MenuLogic: proper titles for Enchantment/Anvil/Brewing etc.
    {
        WriteBuffer b;
        b.varint(menu->windowId);
        b.varint(menu->openScreenTypeId());
        const char* title = "Container";
        switch(menu->type) {
            case MenuType::Chest: title="Chest"; break;
            case MenuType::Furnace: title="Furnace"; break;
            case MenuType::Crafting: title="Crafting"; break;
            case MenuType::Enchantment: title="Enchant"; break;
            case MenuType::Anvil: title="Repair & Name"; break;
            case MenuType::Brewing: title="Brewing Stand"; break;
            case MenuType::Stonecutter: title="Stonecutter"; break;
            case MenuType::Grindstone: title="Grindstone"; break;
            case MenuType::Smithing: title="Smithing Table"; break;
            case MenuType::Beacon: title="Beacon"; break;
            case MenuType::Loom: title="Loom"; break;
            case MenuType::Barrel: title="Barrel"; break;
            case MenuType::ShulkerBox: title="Shulker Box"; break;
            case MenuType::Hopper: title="Hopper"; break;
            case MenuType::Dispenser: title="Dispenser"; break;
            default: title="Container"; break;
        const char* title = "Chest";
        switch (menu->type) {
        case MenuType::Chest: title = "Chest"; break;
        case MenuType::Furnace: title = "Furnace"; break;
        case MenuType::Crafting: title = "Crafting"; break;
        case MenuType::Hopper: title = "Hopper"; break;
        case MenuType::Dispenser: title = "Dispenser"; break;
        case MenuType::Barrel: title = "Barrel"; break;
        case MenuType::ShulkerBox: title = "Shulker Box"; break;
        case MenuType::Enchantment: title = "Enchanting"; break;
        case MenuType::Anvil: title = "Anvil"; break;
        case MenuType::Brewing: title = "Brewing"; break;
        case MenuType::Stonecutter: title = "Stonecutter"; break;
        case MenuType::Grindstone: title = "Grindstone"; break;
        case MenuType::Smithing: title = "Smithing"; break;
        case MenuType::Beacon: title = "Beacon"; break;
        case MenuType::Loom: title = "Loom"; break;
        }
        nbt::writeTextComponent(b, title);
        conn_->sendPacket(pl::sc::OpenScreen, b);
    }
    openMenu_ = std::move(menu);
    openMenu_->refreshCraftResult(srv_.recipes());
    sendMenuContent(*openMenu_);
    // Send initial ContainerSetData for menus that need it
    if (openMenu_->type == MenuType::Enchantment) {
        int bs = 0;
        if (openMenu_->blockKey >= 0) {
            int bx = posKeyUnpackX(openMenu_->blockKey);
            int by = posKeyUnpackY(openMenu_->blockKey);
            int bz = posKeyUnpackZ(openMenu_->blockKey);
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                    for (int dy = 0; dy <= 1; ++dy) {
                        if (dx == 0 && dz == 0) continue;
                        auto st = srv_.world().getBlock(bx + dx, by + dy, bz + dz);
                        auto* d = gen::blockByState(st);
                        if (d && std::string(d->name) == "minecraft:bookshelf") ++bs;
                    }
            if (bs > 15) bs = 15;
        }
        auto costs = CostCalculator::enchantingCostsForShelves(*self_, bs);
        for (int i = 0; i < 3; ++i) {
            WriteBuffer pb;
            pb.u8(static_cast<std::uint8_t>(openMenu_->windowId));
            pb.i16(static_cast<std::int16_t>(i));
            pb.i16(costs[i]);
            try { conn_->sendPacket(pl::sc::ContainerSetData, pb); } catch (...) {}
        }
    } else if (openMenu_->type == MenuType::Anvil) {
        ItemStack left = openMenu_->extraSlots[0];
        ItemStack right = openMenu_->extraSlots[1];
        int cost = CostCalculator::anvilCost(left, right, "");
        WriteBuffer pb;
        pb.u8(static_cast<std::uint8_t>(openMenu_->windowId));
        pb.i16(0);
        pb.i16(static_cast<std::int16_t>(cost < 0 ? 0 : cost));
        try { conn_->sendPacket(pl::sc::ContainerSetData, pb); } catch (...) {}
    } else if (openMenu_->type == MenuType::Brewing) {
        if (openMenu_->blockEntity && openMenu_->blockEntity->kind == BlockEntity::Kind::Brewing) {
            auto &b = openMenu_->blockEntity->brewing;
            for (int prop = 0; prop < 2; ++prop) {
                WriteBuffer pb;
                pb.u8(static_cast<std::uint8_t>(openMenu_->windowId));
                pb.i16(static_cast<std::int16_t>(prop));
                pb.i16(prop == 0 ? b.brewTime : b.fuel);
                try { conn_->sendPacket(pl::sc::ContainerSetData, pb); } catch (...) {}
            }
        }
    } else if (openMenu_->type == MenuType::Furnace) {
        if (openMenu_->blockEntity && openMenu_->blockEntity->kind == BlockEntity::Kind::Furnace) {
            auto &f = openMenu_->blockEntity->furnace;
            const int props[4] = {f.cookProgress, f.cookTotal, f.burnTicks, f.burnDuration};
            for (int prop = 0; prop < 4; ++prop) {
                WriteBuffer pb;
                pb.u8(static_cast<std::uint8_t>(openMenu_->windowId));
                pb.i16(static_cast<std::int16_t>(prop));
                pb.i16(static_cast<std::int16_t>(props[prop]));
                try { conn_->sendPacket(pl::sc::ContainerSetData, pb); } catch (...) {}
            }
        }
    }
}
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
            std::uint32_t stonecutterItem = 0;
            auto itSc = gen::itemIdByName().find("minecraft:stonecutter");
            if (itSc != gen::itemIdByName().end()) stonecutterItem = itSc->second;
            else stonecutterItem = furnaceItem;
            writeSlotDisplayItem(stonecutterItem);
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
// (index into RecipeManager::all()). Handles Crafting, Furnace, Stonecutter.
void Session::handlePlaceRecipe(std::int32_t recipeId, bool makeAll) {
    if (!openMenu_) return;
    Menu& m = *openMenu_;
    const auto& all = srv_.recipes().all();
    if (recipeId < 0 || static_cast<std::size_t>(recipeId) >= all.size()) return;
    const Recipe& r = all[static_cast<std::size_t>(recipeId)];

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

    if (m.type == MenuType::Crafting) {
        // return current grid contents to inventory first
        for (auto& s : m.craftGrid) {
            if (!s.empty()) {
                srv_.addToInventory(*self_, s.itemId, s.count);
                s = ItemStack::air();
            }
        }
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
        return;
    } else if (m.type == MenuType::Furnace) {
        if (r.kind != Recipe::Kind::Smelting) return;
        // Place ingredient into input slot 0, and if needed fuel into slot 1
        ItemStack* input = m.container ? &m.container[0] : &m.extraSlots[0];
        ItemStack* fuel = m.container ? &m.container[1] : &m.extraSlots[1];
        // if input already occupied, return it first
        if (!input->empty()) {
            srv_.addToInventory(*self_, input->itemId, input->count);
            *input = ItemStack::air();
        }
        const Ingredient& ing = r.cells.front();
        ItemStack got = take(ing);
        if (got.empty()) return;
        *input = got;
        // try to place fuel if empty and makeAll is true or slot empty
        if (fuel->empty()) {
            // find any fuel item in inventory
            for (auto& s : self_->inv) {
                if (!s.empty() && isFuelItem(s.itemId)) {
                    ItemStack one = ItemStack::of(s.itemId, 1);
                    if (--s.count <= 0) s = ItemStack::air();
                    *fuel = one;
                    break;
                }
            }
        }
        // sync
        sendMenuContent(m);
        srv_.resendInventory(*self_);
        syncCursorItem();
        // also send ContainerSetData update (cook progress etc will be ticked)
        if (m.blockEntity && m.blockEntity->kind == BlockEntity::Kind::Furnace) {
            auto &f = m.blockEntity->furnace;
            WriteBuffer pb;
            pb.u8(static_cast<std::uint8_t>(m.windowId));
            pb.i16(0);
            pb.i16(f.cookProgress);
            try { conn_->sendPacket(pl::sc::ContainerSetData, pb); } catch (...) {}
        }
        (void)makeAll;
        return;
    } else if (m.type == MenuType::Stonecutter) {
        if (r.kind != Recipe::Kind::Stonecutting) return;
        ItemStack* input = m.container ? &m.container[0] : &m.extraSlots[0];
        ItemStack* output = m.container ? &m.container[1] : &m.extraSlots[1];
        if (!input->empty()) {
            srv_.addToInventory(*self_, input->itemId, input->count);
            *input = ItemStack::air();
        }
        const Ingredient& ing = r.cells.front();
        ItemStack got = take(ing);
        if (got.empty()) return;
        *input = got;
        *output = r.result;
        // ghost preview: also send PlaceGhostRecipe to client
        {
            WriteBuffer b;
            b.varint(m.windowId);
            b.varint(recipeId);
            try { conn_->sendPacket(pl::sc::PlaceGhostRecipe, b); } catch (...) {}
        }
        // also send ContainerSetSlot for output
        sendSetSlot(m.windowId, self_->invStateId + 1, 1, *output);
        sendMenuContent(m);
        srv_.resendInventory(*self_);
        syncCursorItem();
        (void)makeAll;
        return;
    }
    // For other containers (Enchantment, Anvil, Brewing, etc.), PlaceRecipe is no-op but we still ack
    (void)makeAll;
}

void Session::handlePlaceGhostRecipe(std::int32_t recipeId) {
    if (!openMenu_ || openMenu_->type != MenuType::Stonecutter) return;
    Menu& m = *openMenu_;
    const auto& all = srv_.recipes().all();
    if (recipeId < 0 || static_cast<std::size_t>(recipeId) >= all.size()) return;
    const Recipe& r = all[static_cast<std::size_t>(recipeId)];
    if (r.kind != Recipe::Kind::Stonecutting) return;
    ItemStack* input = m.container ? &m.container[0] : &m.extraSlots[0];
    ItemStack* output = m.container ? &m.container[1] : &m.extraSlots[1];
    if (input->empty() || !r.cells.front().accepts(input->itemId)) return;
    *output = r.result;
    // send ghost slot update
    sendSetSlot(m.windowId, self_->invStateId + 1, 1, *output);
    // echo PlaceGhostRecipe back to client
    WriteBuffer b;
    b.varint(m.windowId);
    b.varint(recipeId);
    try { conn_->sendPacket(pl::sc::PlaceGhostRecipe, b); } catch (...) {}
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
    if ((channel == "minecraft:item_name" || channel == "MC|ItemName" || channel == "minecraft:anvil_rename")
        && openMenu_ && openMenu_->type == MenuType::Anvil) {
        std::string newName;
        try {
            ReadBuffer rb(std::vector<uint8_t>(body.begin(), body.end()));
            if (rb.remaining() > 0) {
                newName = rb.string(256);
                if (newName.size() > 50) newName = newName.substr(0,50);
            }
        } catch (...) {
            newName = std::string(body.begin(), body.end());
            size_t nul = newName.find(char(0));
            if (nul != std::string::npos) newName = newName.substr(0, nul);
            if (newName.size() > 50) newName = newName.substr(0,50);
        }
        if (newName.size() > 50) newName.resize(50);
        openMenu_->anvilRename = newName;
        if (auto* logic = getMenuLogic(MenuType::Anvil)) {
            if (auto* al = dynamic_cast<AnvilMenuLogic*>(logic)) {
                al->setRenameForMenu(*openMenu_, newName);
                al->onContentChanged(*openMenu_, *self_);
            }
        }
        int cost = CostCalculator::anvilCost(openMenu_->extraSlots[0], openMenu_->extraSlots[1], newName);
        WriteBuffer pb;
        pb.u8(static_cast<uint8_t>(openMenu_->windowId));
        pb.i16(0);
        pb.i16(static_cast<int16_t>(cost < 0 ? 0 : cost));
        try { conn_->sendPacket(pl::sc::ContainerSetData, pb); } catch (...) {}
        sendSetSlot(openMenu_->windowId, self_->invStateId, 2, openMenu_->extraSlots[2]);
        sendMenuContent(*openMenu_);
        return;
    }
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
        const uint8_t pid = in.u8();
        // PacketHandler registry demo (plan7 network): try dispatch via registry for unknown packets
        // This shows the Netty ChannelPipeline style abstraction; main dispatch is still switch for performance.
        // The registry is used as fallback for extensibility (mods can register handlers).
        switch (pid) {
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
        case pl::cs::MessageAck: {
            // plan10 §5: client acknowledges last seen chat offset (3-byte bitset + offset varint)
            // Format in 1.21.4: varint offset + fixed bitset (3 bytes for 20 messages)
            try {
                int32_t ackOffset = in.varint();
                self_->lastChatAckOffset = ackOffset;
                // remaining bitset (typically 3 bytes) – consume if present
                if (in.remaining() >= 3) in.bytes(3);
                else if (in.remaining() > 0) in.skipRest();
            } catch (...) { in.skipRest(); }
            break;
        }
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
        case pl::cs::EnchantItem:         onEnchantItem(in); break;   // 0x0F plan7
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
        case pl::cs::PlayerInput: {
            // plan9 riding: forward/strafe/jump inputs for horse control
            float sideways = in.f32(); float forward = in.f32(); std::uint8_t flags = in.u8();
            bool jump = (flags & 0x01) != 0;
            (void)sideways; (void)forward;
            if (jump && self_->vehicleId != -1) {
                // trigger horse jump if on horse
                srv_.handleHorseJump(*self_, 80);
            }
            break;
        }
        case pl::cs::MoveVehicle: {
            // MoveVehicle 0x20: double x,y,z + float yaw,pitch (vehicle controlled by player)
            double vx = in.f64(), vy = in.f64(), vz = in.f64();
            float vyaw = in.f32(), vpitch = in.f32();
            if (self_->vehicleId != -1) {
                std::lock_guard lk(srv_.entsMtx_);
                for (auto& m : srv_.mobsForTest()) if (m->entityId==self_->vehicleId) {
                    m->x = vx; m->y = vy; m->z = vz; m->yaw = vyaw;
                    // broadcast to tracking players
                    WriteBuffer b;
                    b.varint(m->entityId);
                    b.f64(vx); b.f64(vy); b.f64(vz);
                    b.f32(vyaw); b.f32(vpitch);
                    b.boolean(true);
                    srv_.broadcastPacketExcept(self_.get(), pl::sc::EntityTeleport, b);
                    break;
                }
            }
            break;
        }
        case pl::cs::SignUpdate: { // 0x39 - also PlaceGhostRecipe for stonecutter
            if (openMenu_ && openMenu_->type == MenuType::Stonecutter && in.len - in.off < 16) {
                // treat as PlaceGhostRecipe: windowId + recipeId
                try {
                    std::uint8_t win = in.u8();
                    std::int32_t rid = in.varint();
                    (void)win;
                    handlePlaceGhostRecipe(rid);
                } catch (...) {}
            } else {
                in.skipRest();
            }
            break;
        }
        case pl::cs::EntityAction: {
            const std::int32_t eid = in.varint();
            const std::int32_t action = in.varint();
            const std::int32_t jumpBoost = in.varint();
            (void)eid; (void)jumpBoost;
            bool wasSneak = self_->isSneaking;
            bool wasSprint = self_->isSprinting;
            if (action == 0) self_->isSneaking = true;
            else if (action == 1) self_->isSneaking = false;
            else if (action == 3) self_->isSprinting = true;
            else if (action == 4) self_->isSprinting = false;
            if (wasSneak != self_->isSneaking) {
                // pose metadata index 6 varint: 5 crouching, 0 standing
                WriteBuffer md;
                md.varint(self_->entityId);
                md.u8(6); md.varint(1); md.varint(self_->isSneaking ? 5 : 0);
                md.u8(255);
                srv_.broadcastPacketExcept(self_.get(), pl::sc::SetEntityMetadata, md);
                // also flags byte index 0 bit 1 for sneaking
                WriteBuffer fl;
                fl.varint(self_->entityId);
                fl.u8(0); fl.varint(0); fl.u8(self_->isSneaking ? 0x02 : 0x00);
                fl.u8(255);
                srv_.broadcastPacketExcept(self_.get(), pl::sc::SetEntityMetadata, fl);
                // Plan8 EquipmentComponent/EntityAction: sneak dismount from vehicle (horse/llama/pig)
                // Vanilla sends EntityAction 0x28 with action 0 for sneak start; if player is riding, dismount.
                if (self_->isSneaking && self_->vehicleId != -1) {
                    int veh = self_->vehicleId;
                    // clear player vehicle
                    self_->vehicleId = -1;
                    // clear mob rider
                    {
                        std::lock_guard lk(srv_.entsMtx_);
                        for (auto& m : srv_.mobsForTest()) if (m->entityId==veh) m->riderEntityId=-1;
                    }
                    srv_.broadcastSetPassengersEmpty(veh);
                }
            }
            // Plan8/9: handle jump actions (horse jump) – action 7 is horse jump with boost
            if (action==5 || action==6 || action==7) {
                if (self_->vehicleId != -1) {
                    // horse jump handling via jumpBoost (plan9 item 31)
                    srv_.handleHorseJump(*self_, jumpBoost);
                    WriteBuffer je;
                    je.varint(self_->entityId); je.varint(action);
                    srv_.broadcastPacketExcept(self_.get(), pl::sc::SetEntityMetadata, je);
                }
            }
            (void)wasSprint;
            break;
        }
        default:
            // Try PacketHandler registry before treating as unknown (plan7 network)
            {
                // Reconstruct packet payload for handler (id already consumed, remaining is payload)
                std::vector<uint8_t> payload;
                if (in.remaining() > 0) payload.assign(in.p + in.off, in.p + in.len);
                Packet pkt{pid, std::move(payload)};
                bool handled = globalPacketHandlers().dispatch(pkt, *conn_);
                if (handled) break;
            }
            // Unknown packets: skip payload to stay aligned, but log loudly.
            std::fprintf(stderr, "[cppfm] unknown play packet pid=%02x from %s\n",
                         pid, conn_->peer().c_str());
            in.skipRest();
            break;
        }
    }
}

void Session::onMovement(ReadBuffer& in, bool hasPos, bool hasRot) {
    const double oldX = self_->x, oldY = self_->y, oldZ = self_->z;
    const bool wasOnGround = self_->onGround;
    if (hasPos) {
        const double nx = in.f64(), ny = in.f64(), nz = in.f64();
        if (!self_->onGround && ny < self_->y && self_->gamemode == 0)
            self_->fallDist += self_->y - ny;
        self_->x = nx; self_->y = ny; self_->z = nz;
        // WorldBorder check in onMovement (plan10 §1): apply damage/ warning if outside
        if (!srv_.isInsideBorder(self_->x, self_->z) && self_->gamemode == 0) {
            // vanilla does not teleport back; just marks for damage on next survivalTick
            // but we can send a warning via actionbar if far outside
            (void)srv_.borderDistanceOutside(self_->x, self_->z);
        }
    }
    if (hasRot) {
        self_->yaw = in.f32();
        self_->pitch = in.f32();
    }
    const bool nowGround = in.boolean();
    if (hasPos) {
        if (self_->y < -2048.0 || self_->y > 2048.0)
            throw std::runtime_error("player moved out of world bounds");
        // landing — fall mitigation (water/slime/honey/hay/powder_snow+slowfalling)
        auto isFallMitigated = [&]() -> bool {
            if (self_->gamemode == 1 || self_->gamemode == 3) return true;
            int bx = (int)std::floor(self_->x);
            int by = (int)std::floor(self_->y - 0.2);
            int bz = (int)std::floor(self_->z);
            uint16_t st = srv_.worldFor(self_->dimension).getBlock(bx,by,bz);
            auto *d = gen::blockByState(st);
            if (!d) return false;
            if (d->name == "minecraft:water") return true;
            if (d->name == "minecraft:slime_block") return true;
            if (d->name == "minecraft:honey_block") return true;
            if (d->name == "minecraft:hay_block") return true;
            if (d->name == "minecraft:powder_snow") {
                for (auto &e : self_->effects) if (e.type == effects::SlowFalling) return true;
                return false;
            }
            // also check if landing block is waterlogged? simplified
            return false;
        };
        if (nowGround && !self_->onGround) {
            // Farmland trample (plan6 item 15): if landed on farmland with fallDist >0.5
            if (self_->fallDist > 0.5) {
                int bx = static_cast<int>(std::floor(self_->x));
                int by = static_cast<int>(std::floor(self_->y - 0.2));
                int bz = static_cast<int>(std::floor(self_->z));
                World& w = srv_.worldFor(self_->dimension);
                std::uint16_t st = w.getBlock(bx, by, bz);
                const gen::BlockDef* bd = gen::blockByState(st);
                if (bd && std::string(bd->name) == "minecraft:farmland") {
                    // set moisture 0
                    bool hasMoisture = false;
                    for (auto& [k,v] : gen::propsOf(st)) if (k=="moisture") hasMoisture=true;
                    if (hasMoisture) {
                        const gen::BlockDef* d = bd;
                        std::vector<std::pair<std::string_view,std::string_view>> props;
                        for (auto& [k,v] : gen::propsOf(st)) if (k!="moisture") props.emplace_back(k,v);
                        props.emplace_back("moisture", "0");
                        std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*d, props));
                        w.setBlock(bx, by, bz, ns);
                        srv_.broadcastBlockChange(bx, by, bz, ns);
                        // if no crop above, revert to dirt
                        if (w.getBlock(bx, by+1, bz)==0) {
                            auto it = gen::blockNameToState().find("minecraft:dirt");
                            if (it != gen::blockNameToState().end()) {
                                std::uint16_t dirt = static_cast<std::uint16_t>(it->second);
                                w.setBlock(bx, by, bz, dirt);
                                srv_.broadcastBlockChange(bx, by, bz, dirt);
                            }
                        }
                    }
                }
            }
            // BlockEvent: onEntityLand (plan7) – fire when entity lands on block
            {
                int lbx = static_cast<int>(std::floor(self_->x));
                int lby = static_cast<int>(std::floor(self_->y - 0.2));
                int lbz = static_cast<int>(std::floor(self_->z));
                std::uint16_t lst = srv_.worldFor(self_->dimension).getBlock(lbx, lby, lbz);
                blockEventDispatcher().onEntityLand(self_.get(), lbx, lby, lbz, lst, self_->fallDist);
                api::EntityLandEvent lev; lev.entity=self_.get(); lev.x=lbx; lev.y=lby; lev.z=lbz; lev.blockState=lst; lev.fallDistance=self_->fallDist;
                api::events().entityLand.fire(lev);
            }
            if (getenv("CPPFM_TRACE"))
                std::fprintf(stderr, "[cppfm] %s landed fallDist=%.2f gm=%u\n",
                             self_->name.c_str(), self_->fallDist, self_->gamemode);
            bool mitigated = false;
            if (self_->fallDist > 3.0) mitigated = isFallMitigated();
            if (mitigated) {
                self_->fallDist = 0;
            } else if (self_->fallDist > 3.0) {
                if (srv_.gamerules_.getBool("fallDamage"))
                    srv_.applyDamage(*self_, static_cast<float>(std::floor(self_->fallDist - 3.0)),
                                "fall");
                self_->fallDist = 0;
            } else {
                self_->fallDist = 0;
            }
        }
        if (nowGround) self_->fallDist = 0;
        // exhaustion: walk/sprint/swim/jump (plan9 #84 + plan10 survival) via HungerManager
        if (self_->gamemode == 0) {
            HungerManager::onPlayerMove(*self_, oldX, oldY, oldZ,
                                        self_->x, self_->y, self_->z,
                                        wasOnGround, nowGround,
                                        self_->isSprinting, self_->isSwimming, srv_);
            double dy = self_->y - oldY;
            HungerManager::onPlayerJump(*self_, wasOnGround, nowGround, dy, self_->isSprinting, srv_);
        }
        self_->spawned = true;
        if (!chunksStreamed_) streamInitialChunks();
        else tickChunksAround(self_->x, self_->z);
    }
    self_->onGround = nowGround;
    // Plan8 EnchantmentHelper: Frost Walker – freeze water around feet when on ground
    if (self_->onGround && hasPos) {
        bool hasFrost = false;
        for (int i=5;i<=8;++i) if (!self_->inv[i].empty() && EnchantmentHelper::hasFrostWalker(self_->inv[i])) { hasFrost=true; break; }
        if (hasFrost) {
            World& w = srv_.worldFor(self_->dimension);
            int bx = (int)std::floor(self_->x);
            int by = (int)std::floor(self_->y - 0.5);
            int bz = (int)std::floor(self_->z);
            int lvl = 0;
            for (int i=5;i<=8;++i) if (!self_->inv[i].empty()) lvl = std::max(lvl, self_->inv[i].enchantLevel("frost_walker"));
            int radius = 2 + lvl;
            auto frostIt = gen::blockNameToState().find("minecraft:frosted_ice");
            if (frostIt != gen::blockNameToState().end()) {
                std::uint16_t frosted = (std::uint16_t)frostIt->second;
                for (int dx=-radius; dx<=radius; ++dx) for (int dz=-radius; dz<=radius; ++dz) {
                    if (dx*dx+dz*dz > (radius+1)*(radius+1)) continue;
                    int wx = bx+dx, wz = bz+dz;
                    std::uint16_t st = w.getBlock(wx, by, wz);
                    const gen::BlockDef* bd = gen::blockByState(st);
                    if (bd && std::string(bd->name)=="minecraft:water") {
                        // check level 0 source
                        bool isSource=false;
                        for (auto& [k,v]: gen::propsOf(st)) if (k=="level" && v=="0") isSource=true;
                        if (!isSource) continue;
                        w.setBlock(wx, by, wz, frosted);
                        srv_.broadcastBlockChange(wx, by, wz, frosted);
                    }
                }
            }
        }
    }
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
    int64_t timestamp = in.i64();
    int64_t salt = in.i64();
    std::vector<uint8_t> signature;
    bool hasSig = in.boolean();
    if (hasSig) {
        // vanilla RSA signature is 256 bytes; some clients send varint len, we support both
        try {
            signature = in.bytes(256);
        } catch (...) {
            // fallback: read varint len prefix if 256 fails (plan10 §5)
            in.off -= 1; // step back one byte if we mis-read? just skip
            signature.clear();
        }
    }
    int32_t offset = in.varint();
    std::vector<uint8_t> acknowledged;
    try { acknowledged = in.bytes(3); } catch (...) { acknowledged.clear(); }
    self_->lastChatAckOffset = offset;
    self_->lastChatTimestamp = timestamp;
    self_->lastChatSalt = salt;
    // simple replay protection: reject duplicate salt within 20 recent
    {
        bool duplicate = false;
        for (auto s : self_->lastSeenSignatures) if ((int64_t)s == salt) duplicate = true;
        if (duplicate) {
            std::fprintf(stderr, "[cppfm] chat duplicate salt %lld from %s\n", (long long)salt, self_->name.c_str());
            return;
        }
        self_->lastSeenSignatures.push_back(static_cast<uint8_t>(salt & 0xFF));
        if (self_->lastSeenSignatures.size() > 20) self_->lastSeenSignatures.erase(self_->lastSeenSignatures.begin());
    }
    // timestamp window ±5 minutes (plan10 §5)
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (std::llabs(timestamp - nowMs) > 5 * 60 * 1000 && timestamp != 0) {
        std::fprintf(stderr, "[cppfm] chat timestamp out of window for %s: %lld vs %lld\n", self_->name.c_str(), (long long)timestamp, (long long)nowMs);
        // allow but warn; vanilla would reject if enforcesSecureChat
    }

    // events: PlayerChat (cancellable)
    api::PlayerChatEvent ev;
    ev.player = self_.get();
    ev.message = msg;
    if (!srv_.events().chat.fire(ev)) return;

    if (!ev.message.empty() && ev.message[0] == '/')
        return dispatchCommand(ev.message.substr(1));

    bool verified = ChatMessageProcessor::verify(*self_, ev.message, timestamp, salt, signature);
    bool shouldUsePlayerChat = ChatMessageProcessor::shouldUsePlayerChat(*self_);
    bool canUsePlayerChat = shouldUsePlayerChat && verified && !signature.empty();

    // EnforcesSecureChat policy (plan10 §5): if true and verification fails, disconnect
    if (srv_.config().enforcesSecureChat && shouldUsePlayerChat && !verified) {
        std::fprintf(stderr, "[cppfm] chat verify failed and enforcesSecureChat=true, disconnect %s\n", self_->name.c_str());
        WriteBuffer disc;
        nbt::writeTextComponent(disc, "Chat signature verification failed");
        try { conn_->sendPacket(proto::pl::sc::Disconnect, disc); } catch(...) {}
        conn_->close();
        return;
    }

    if (canUsePlayerChat) {
        // Broadcast as PlayerChat 0x3B (verified, secure)
        srv_.broadcastPlayerChat(*self_, ev.message, timestamp, salt, signature);
    } else {
        // Fallback to SystemChat 0x73 (unsigned / no session)
        const std::string line = "<" + self_->name + "> " + ev.message;
        srv_.broadcastSystemText(line, nullptr);
        // Ensure sender also sees it (broadcast includes all, but if sender was excluded via except, send explicitly)
        // broadcastSystemText already excludes none, includes sender via broadcastPacketExcept(nullptr)
        // No extra send needed; kept for parity
    }
}

void Session::onChatCommand(ReadBuffer& in) {
    const std::string cmd = in.string(256);
    dispatchCommand(cmd);
}

void Session::dispatchCommand(const std::string& line) {
    brigadier::CommandSource src;
    src.player = self_.get();
    src.name = self_->name;
    src.console = false;
    src.srcX = self_->x; src.srcY = self_->y; src.srcZ = self_->z;
    src.srcYaw = self_->yaw; src.srcPitch = self_->pitch;
    src.resolveSelector = [this](const std::string& raw,
                                 brigadier::SelectorResult& out) {
        out = srv_.resolveSelector(raw, self_.get());
    };

    const auto res = srv_.commands().execute(line, std::move(src));
    if (!res.ok) {
        std::fprintf(stderr,"[cppfm] command '%s' failed: %s\n", line.c_str(), res.errorText.c_str());
        sendSystemText("\u00a7c" + (res.errorText.empty()
                          ? "Incorrect argument for command"
                          : res.errorText));
    } else {
        std::fprintf(stderr,"[cppfm] command '%s' ok val=%d\n", line.c_str(), res.value);
    }
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

    // spawn-protection check (plan6 §9): non-OP cannot break within spawnProtection_
    if ((status==0 || status==2) && srv_.isSpawnProtected(x, z) && !srv_.isOp(self_->name)) {
        // cancel: re-send block and ack
        const std::uint16_t cur = srv_.world().getBlock(x, y, z);
        WriteBuffer rb; rb.position(x,y,z); rb.varint(cur);
        try { conn_->sendPacket(proto::pl::sc::BlockUpdate, rb); } catch(...) {}
        sendSystemText("\u00a7cSpawn protection prevents building here");
        ack(sequence);
        self_->digActive=false;
        return;
    }

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
            float effMult = 1.f;
            if (self_->heldSlot>=0 && self_->heldSlot<9) {
                auto &sl = self_->inv[36+self_->heldSlot];
                if (!sl.empty()) {
                    effMult = EnchantmentHelper::getEfficiencyMultiplier(sl);
                    std::string n = sl.name();
                    if (n.find("netherite")!=std::string::npos) effMult *= 9;
                    else if (n.find("diamond")!=std::string::npos) effMult *= 8;
                    else if (n.find("iron")!=std::string::npos) effMult *= 6;
                    else if (n.find("stone")!=std::string::npos) effMult *= 4;
                    else if (n.find("wooden")!=std::string::npos) effMult *= 2;
                    else if (n.find("golden")!=std::string::npos) effMult *= 12;
                }
            }
            const float speed = effMult;
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
                    if (self_->heldSlot>=0 && self_->heldSlot<9) {
                        auto &held = self_->inv[36 + self_->heldSlot];
                        if (!held.empty() && ItemStack::maxDamageFor(held.itemId)>0) {
                            if (held.applyDamage(1)) held = ItemStack::air();
                            srv_.resendInventory(*self_);
                        }
                    }
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
    // --- ItemUseContext (plan6) ---
    ItemUseContext ctx;
    ctx.player = self_.get();
    ctx.world = &srv_.worldFor(self_->dimension);
    ctx.hitPos = {x, y, z};
    ctx.placePos = {tx, ty, tz};
    ctx.face = d;
    ctx.cursor = {static_cast<double>(cursorX), static_cast<double>(cursorY), static_cast<double>(cursorZ)};
    ctx.yaw = self_->yaw;
    ctx.isSneaking = self_->isSneaking;

    // spawn-protection for placement (plan6 §9)
    if (srv_.isSpawnProtected(tx, tz) && !srv_.isOp(self_->name)) {
        // check if placing a block (held is block item) – cancel
        const bool isBlockPlace = (self_->heldSlot>=0 && self_->heldSlot<9 && !self_->inv[36+self_->heldSlot].empty()
            && gen::blockByName(self_->inv[36+self_->heldSlot].name()) != nullptr);
        if (isBlockPlace) {
            sendSystemText("\u00a7cSpawn protection prevents building here");
            ack(sequence);
            return;
        }
    }

    // BlockEvent: fire onBlockClicked for every right-click (plan7)
    {
        const std::uint16_t _clickedSt = srv_.worldFor(self_->dimension).getBlock(x, y, z);
        blockEventDispatcher().onBlockClicked(x, y, z, _clickedSt, d, self_.get());
        api::BlockClickedEvent _bcev; _bcev.player=self_.get(); _bcev.x=x; _bcev.y=y; _bcev.z=z; _bcev.state=_clickedSt; _bcev.face=d;
        api::events().blockClicked.fire(_bcev);
    }
    // right-click on interactive blocks opens menus (vanilla behaviour)
    // right-click on interactive blocks opens menus (vanilla behaviour) — plan7 MenuLogic: Enchantment/Anvil/Brewing etc.
    {
        const std::uint16_t clickedState = srv_.world().getBlock(x, y, z);
        const gen::BlockDef* bdef = gen::blockByState(clickedState);
        if (bdef) {
            const std::string bn(bdef->name);
            bool isMenuBlock = bn.find("chest") != std::string::npos ||
                bn == "minecraft:furnace" || bn == "minecraft:blast_furnace" ||
                bn == "minecraft:smoker" ||
                bn == "minecraft:hopper" || bn == "minecraft:dispenser" ||
                bn == "minecraft:dropper" ||
                bn == "minecraft:crafting_table" ||
                bn == "minecraft:enchanting_table" ||
                bn.find("anvil") != std::string::npos ||
                bn == "minecraft:brewing_stand" ||
                bn == "minecraft:stonecutter" ||
                bn == "minecraft:grindstone" ||
                bn.find("smithing") != std::string::npos ||
                bn == "minecraft:beacon" ||
                bn == "minecraft:loom" ||
                bn == "minecraft:barrel" ||
                bn.find("shulker_box") != std::string::npos;
            if (isMenuBlock) {
                // Allow opening from any face if not sneaking; ensure sneaking bypass
                if (ctx.isSneaking && !bn.empty()) {
                    // sneaking still places block, so skip menu
                } else {
                    openMenuAt(x, y, z, clickedState);
                    ack(sequence);
                    return;
                }
            if (d == 1) {
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
    }

    // cake slice eat (plan7 hunger): right-click cake block consumes slice
    {
        World& ww = srv_.worldFor(self_->dimension);
        uint16_t cst = ww.getBlock(x,y,z);
        auto* cdef = gen::blockByState(cst);
        if (cdef && std::string(cdef->name)=="minecraft:cake") {
            if (handleCakeBlockConsume(srv_, *self_, x,y,z)) { ack(sequence); return; }
        }
    }

    // Place the actually-held block item (vanilla semantics).
    static const InvSlot airSlot = InvSlot::air();
    const bool survival = self_->gamemode == 0;
    const InvSlot& heldItem =
        (self_->heldSlot >= 0 && self_->heldSlot < 9)
            ? self_->inv[36 + self_->heldSlot] : airSlot;

    // ---- TNT ignition (plan10 §8): flint_and_steel / fire_charge on TNT -> primed TNT entity (fuse 80t)
    {
        World& w = srv_.worldFor(self_->dimension);
        std::uint16_t clickedSt = w.getBlock(x, y, z);
        const gen::BlockDef* td = gen::blockByState(clickedSt);
        if (td && std::string(td->name)=="minecraft:tnt" && !heldItem.empty()) {
            const std::string hn = heldItem.name();
            if (hn=="minecraft:flint_and_steel" || hn=="minecraft:fire_charge") {
                w.setBlock(x, y, z, 0);
                srv_.broadcastBlockChange(x, y, z, 0);
                double vx = (rand()/(double)RAND_MAX - 0.5)*0.2;
                double vz = (rand()/(double)RAND_MAX - 0.5)*0.2;
                srv_.spawnTnt(x + 0.5, y + 0.5, z + 0.5, vx, 0.4, vz, 80);
                if (survival) {
                    auto* mh = &self_->inv[36 + self_->heldSlot];
                    if (hn=="minecraft:flint_and_steel") { if (mh->applyDamage(1)) *mh = ItemStack::air(); }
                    else { if (--mh->count <= 0) *mh = ItemStack::air(); }
                    srv_.resendInventory(*self_);
                }
                srv_.broadcastSound("minecraft:entity.tnt.primed", x+0.5, y+0.5, z+0.5, 1.f, 1.f, "blocks");
                ack(sequence);
                return;
            }
        }
    }

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
            // TNT ignition -> primed TNT (plan10 §8)
            {
                std::uint16_t clickedSt = srv_.world().getBlock(x, y, z);
                const gen::BlockDef* cd2 = gen::blockByState(clickedSt);
                if (cd2 && std::string(cd2->name)=="minecraft:tnt") {
                    srv_.world().setBlock(x, y, z, 0);
                    srv_.broadcastBlockChange(x, y, z, 0);
                    srv_.spawnPrimedTnt(x+0.5, y+0.5, z+0.5, (rand()/(double)RAND_MAX-0.5)*0.3, 0.4, (rand()/(double)RAND_MAX-0.5)*0.3);
                    if (survival) {
                        auto* mh = &self_->inv[36 + self_->heldSlot];
                        if (heldName=="minecraft:flint_and_steel") {
                            if (DamageComponent::applyDamage(*mh, 1)) *mh = ItemStack::air();
                        } else {
                            if (--mh->count <=0) *mh = ItemStack::air();
                        }
                        srv_.resendInventory(*self_);
                    }
                    srv_.broadcastSound("minecraft:entity.tnt.primed", x+0.5, y+0.5, z+0.5, 1.f, 1.f, "blocks");
                    ack(sequence);
                    return;
                }
            }
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
                    if (ItemStack::maxDamageFor(mh->itemId) > 0) {
                        if (mh->applyDamage(1)) *mh = ItemStack::air();
                        srv_.resendInventory(*self_);
                    } else {
                        if (--mh->count <= 0) *mh = InvSlot::air();
                        srv_.resendInventory(*self_);
                    }
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
    // Plan8 MobSpawner: spawn egg via UseItemOn (egg -> spawn mob at clicked face)
    if (itemName.find("_spawn_egg") != std::string::npos) {
        MobSpawner spawner(srv_);
        // spawn at offset position tx,ty,tz (or slightly above if blocked)
        double sx = tx + 0.5, sy = ty + 0.5, sz = tz + 0.5;
        // if target block is solid, use tx,ty,tz as spawn, else check air
        if (spawner.spawnFromEgg(itemName, sx, sy, sz)) {
            if (survival) {
                auto* mh = &self_->inv[36 + self_->heldSlot];
                if (--mh->count <= 0) *mh = ItemStack::air();
                srv_.resendInventory(*self_);
            }
            ack(sequence);
            return;
        }
    }
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
        // context-aware placement using ItemUseContext (plan6 item 11/15)
        float yaw = ctx.yaw;
        const char* facing = "north";
        if (yaw >= 45.f && yaw < 135.f) facing = "east";
        else if (yaw >= 135.f && yaw < 225.f) facing = "south";
        else if (yaw >= 225.f && yaw < 315.f) facing = "west";
        bool hasFacing = false;
        bool hasHalf = false, hasShape = false, hasSnowy = false, hasWaterlogged = false, hasAxis = false;
        for (int i = 0; i < bdef2->propCount; ++i) {
            const auto& pd = gen::kPropDefs[gen::kBlockPropsRun[bdef2->propsOff + i]];
            if (pd.name == "facing") hasFacing = true;
            if (pd.name == "half") hasHalf = true;
            if (pd.name == "shape") hasShape = true;
            if (pd.name == "snowy") hasSnowy = true;
            if (pd.name == "waterlogged") hasWaterlogged = true;
            if (pd.name == "axis") hasAxis = true;
        }
        if (hasFacing) props.emplace_back("facing", facing);
        // stairs/slab half based on face and cursor.y (plan6)
        const char* halfVal = "bottom";
        if (hasHalf) {
            const char* half = "bottom";
            if (ctx.face == 0) half = "top";
            else if (ctx.face == 1) half = "bottom";
            else {
                half = (ctx.cursor.y > 0.5 ? "top" : "bottom");
            }
            halfVal = half;
            props.emplace_back("half", half);
        }
        // stairs shape: inner/outer/straight based on adjacent stairs (plan9 #11)
        if (hasShape) {
            auto computeStairsShape = [&](std::string_view curFacing, std::string_view curHalf) -> std::string {
                // Determine direction vectors for current facing
                int fdx=0,fdz=0;
                if (curFacing=="north") fdz=-1;
                else if (curFacing=="south") fdz=1;
                else if (curFacing=="west") fdx=-1;
                else if (curFacing=="east") fdx=1;
                auto isStairsAt = [&](int nx,int ny,int nz, std::string &nFacing, std::string &nHalf)->bool{
                    std::uint16_t ns = ctx.world->getBlock(nx, ny, nz);
                    const gen::BlockDef* nd = gen::blockByState(ns);
                    if (!nd) return false;
                    if (std::string(nd->name).find("stairs")==std::string::npos) return false;
                    nFacing.clear(); nHalf.clear();
                    for (auto& [k,v] : gen::propsOf(ns)) {
                        if (k=="facing") nFacing=std::string(v);
                        if (k=="half") nHalf=std::string(v);
                    }
                    return !nFacing.empty();
                };
                // helper to check axis difference
                auto axisOf = [&](std::string_view f)->char{
                    if (f=="north"||f=="south") return 'z';
                    if (f=="east"||f=="west") return 'x';
                    return '?';
                };
                auto leftOf = [&](std::string_view f)->std::string{
                    if (f=="north") return "west";
                    if (f=="south") return "east";
                    if (f=="west") return "south";
                    if (f=="east") return "north";
                    return "";
                };
                // outer: check behind position (opposite of facing)
                {
                    int bx = ctx.placePos.x - fdx;
                    int bz = ctx.placePos.z - fdz;
                    std::string nFacing, nHalf;
                    if (isStairsAt(bx, ctx.placePos.y, bz, nFacing, nHalf) && nHalf==curHalf && nFacing!=curFacing) {
                        if (axisOf(nFacing)!=axisOf(curFacing)) {
                            // outer shape if neighbor behind is perpendicular
                            std::string left = leftOf(curFacing);
                            if (nFacing==left) return "outer_left";
                            else return "outer_right";
                        }
                    }
                }
                // inner: check front position
                {
                    int fx = ctx.placePos.x + fdx;
                    int fz = ctx.placePos.z + fdz;
                    std::string nFacing, nHalf;
                    if (isStairsAt(fx, ctx.placePos.y, fz, nFacing, nHalf) && nHalf==curHalf && nFacing!=curFacing) {
                        if (axisOf(nFacing)!=axisOf(curFacing)) {
                            std::string left = leftOf(curFacing);
                            if (nFacing==left) return "inner_left";
                            else return "inner_right";
                        }
                    }
                }
                return "straight";
            };
            std::string shape = computeStairsShape(facing, halfVal);
            props.emplace_back("shape", shape);
        }
        // waterlogged: check if placePos currently water
        if (hasWaterlogged) {
            bool waterlogged = false;
            std::uint16_t before = ctx.world->getBlock(ctx.placePos.x, ctx.placePos.y, ctx.placePos.z);
            const gen::BlockDef* bd = gen::blockByState(before);
            if (bd && std::string(bd->name).find("water") != std::string::npos) waterlogged = true;
            props.emplace_back("waterlogged", waterlogged ? "true" : "false");
        }
        if (hasSnowy) {
            bool snowy = false;
            std::uint16_t above = ctx.world->getBlock(ctx.placePos.x, ctx.placePos.y + 1, ctx.placePos.z);
            const gen::BlockDef* ad = gen::blockByState(above);
            if (ad && std::string(ad->name) == "minecraft:snow") snowy = true;
            props.emplace_back("snowy", snowy ? "true" : "false");
        }
        if (hasAxis) {
            const char* axis = "y";
            if (ctx.face == 4 || ctx.face == 5) axis = "x";
            else if (ctx.face == 2 || ctx.face == 3) axis = "z";
            props.emplace_back("axis", axis);
        }
        // slab type handling: double slab merging (plan9 #11)
        bool hasTypeSlab = false;
        for (int i = 0; i < bdef2->propCount; ++i) {
            const auto& pd = gen::kPropDefs[gen::kBlockPropsRun[bdef2->propsOff + i]];
            if (pd.name == "type") { hasTypeSlab = true; break; }
        }
        if (hasTypeSlab && std::string(bdef2->name).find("_slab") != std::string::npos) {
            std::uint16_t existing = ctx.world->getBlock(ctx.placePos.x, ctx.placePos.y, ctx.placePos.z);
            const gen::BlockDef* exDef = gen::blockByState(existing);
            bool mergedDouble = false;
            if (exDef && std::string(exDef->name)==std::string(bdef2->name)) {
                for (auto& [k,v] : gen::propsOf(existing)) if (k=="type" && v!="double") {
                    // same slab material, existing is single -> merge to double
                    bool alreadyHasType=false;
                    for (auto &pr: props) if (pr.first=="type") alreadyHasType=true;
                    if (!alreadyHasType) props.emplace_back("type", "double");
                    mergedDouble=true; break;
                }
            }
            if (!mergedDouble) {
                const char* type = "bottom";
                if (ctx.face == 0) type = "top";
                else if (ctx.face == 1) type = "bottom";
                else type = (ctx.cursor.y > 0.5 ? "top" : "bottom");
                // handle placing on top of existing slab block below/above
                // Also check if the clicked face is the slab itself: if existing slab at hitPos is single, double it
                std::uint16_t hitSt = ctx.world->getBlock(ctx.hitPos.x, ctx.hitPos.y, ctx.hitPos.z);
                const gen::BlockDef* hd = gen::blockByState(hitSt);
                if (hd && std::string(hd->name)==std::string(bdef2->name)) {
                    for (auto& [k,v] : gen::propsOf(hitSt)) if (k=="type" && v!="double") {
                        // if we clicked on a bottom slab from top face or top slab from bottom face, would have placed in same block but we are offset
                        // fallback: if we are placing adjacent, keep single; the double case already handled via existing at placePos
                    }
                }
                props.emplace_back("type", type);
            }
        }
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
    // BlockEvent: fire onBlockPlace (plan7) after successful placement
    {
        std::uint16_t oldSt = 0; // air before
        blockEventDispatcher().onBlockPlace(tx, ty, tz, oldSt, newState, self_.get());
    }
    if (survival) {
        auto mutableHeld = &self_->inv[36 + self_->heldSlot];
        if (ItemStack::maxDamageFor(mutableHeld->itemId) > 0) {
            if (mutableHeld->applyDamage(1)) *mutableHeld = ItemStack::air();
        } else {
            if (--mutableHeld->count <= 0) *mutableHeld = ItemStack::air();
        }
        srv_.resendInventory(*self_);
    }
    ack(sequence);
}
}

void Session::onUseItem(ReadBuffer& in) {
    (void)in.varint();
    const std::int32_t sequence = in.varint();
    (void)in.f32(); (void)in.f32();
    if (self_->heldSlot >= 0 && self_->heldSlot < 9) {
        auto& sl = self_->inv[36 + self_->heldSlot];
        bool handledFood = false;
        if (!sl.empty() && self_->food < 20) {
            std::string iname = sl.name();
            bool isFood = false;
            int beforeFood = self_->food;
            float beforeSat = self_->saturation;
            srv_.handleFoodConsume(*self_, iname);
            if (self_->food != beforeFood || self_->saturation != beforeSat) isFood = true;
            else {
                // generic fallback for unknown food names that handleFoodConsume might not have matched (e.g., modded)
                if (iname.find("stew")!=std::string::npos||iname.find("soup")!=std::string::npos||iname.find("cake")!=std::string::npos) isFood=true;
            }
            if (isFood) {
                // exhaustion for eating (plan10 survival) via HungerManager
                HungerManager::addExhaustion(*self_, HungerManager::EXHAUST_EAT);
                // consume item (stew leaves bowl already handled inside handleFoodConsume via addToInventory)
                bool isStew = iname.find("stew")!=std::string::npos || iname.find("soup")!=std::string::npos;
                bool isCake = iname.find("cake")!=std::string::npos;
                if (!isStew && !isCake) {
                    if (--sl.count <= 0) sl = InvSlot::air();
                } else if (isStew) {
                    // stew consumed: bowl already added, just decrement stew
                    auto tmp = sl;
                    if (--tmp.count <=0) sl = InvSlot::air(); else sl = tmp;
                } else if (isCake) {
                    if (--sl.count <=0) sl = InvSlot::air();
                }
                srv_.resendInventory(*self_);
                handledFood = true;
            } else {
                // revert if not food (handleFoodConsume might have clamped without change)
                self_->food = beforeFood; self_->saturation = beforeSat;
            }
        }
        // bow use exhaustion (plan9 #84) — UseItem with bow/crossbow triggers 0.01 exhaustion even if not eating
        if (!handledFood && !sl.empty()) {
            std::string iname = sl.name();
            if (iname.find("bow")!=std::string::npos || iname.find("crossbow")!=std::string::npos) {
                HungerManager::onBowUse(*self_, srv_);
            }
        }
    }
    ack(sequence);
}

void Session::onUseEntity(ReadBuffer& in) {
    const std::int32_t target = in.varint();
    const std::int32_t mouse = in.varint();
    if (mouse == 2) { (void)in.f32(); (void)in.f32(); (void)in.f32(); }
    if (mouse != 1) {
        // INTERACT (0) / INTERACT_AT (2)
        if (mouse == 0 || mouse == 2) {
            (void)in.varint();                        // sneaking flag
            // check shear and riding before trading
            {
                std::lock_guard lk(srv_.entsMtx_);
                for (auto& m : srv_.mobsForTest()) {
                    if (m->entityId != target) continue;
                    // shear sheep
                    if (m->kind == MobKind::Sheep && !m->sheared) {
                        auto &held = self_->inv[36 + self_->heldSlot];
                        auto shearsIdIt = gen::itemIdByName().find("minecraft:shears");
                        if (shearsIdIt != gen::itemIdByName().end() && held.itemId == shearsIdIt->second) {
                            m->sheared = true;
                            // drop wool: 1-3
                            static const char* woolNames[] = {
                                "minecraft:white_wool","minecraft:orange_wool","minecraft:magenta_wool","minecraft:light_blue_wool",
                                "minecraft:yellow_wool","minecraft:lime_wool","minecraft:pink_wool","minecraft:gray_wool",
                                "minecraft:light_gray_wool","minecraft:cyan_wool","minecraft:purple_wool","minecraft:blue_wool",
                                "minecraft:brown_wool","minecraft:green_wool","minecraft:red_wool","minecraft:black_wool"
                            };
                            int col = m->woolColor % 16;
                            auto wit = gen::itemIdByName().find(woolNames[col]);
                            if (wit != gen::itemIdByName().end()) {
                                int cnt = 1 + (rand() % 3);
                                srv_.spawnItemDrop(m->x, m->y+0.8, m->z, wit->second, (uint8_t)cnt,
                                    (rand()/(double)RAND_MAX-.5)*0.12, 0.12, (rand()/(double)RAND_MAX-.5)*0.12);
                            }
                            // metadata: sheep index 17 sheared flag
                            {
                                WriteBuffer md;
                                md.varint(m->entityId);
                                md.u8(17); md.u8(0); md.u8(0x10);
                                md.u8(255);
                                srv_.broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);
                            }
                            // durability on shears
                            if (held.applyDamage(1)) {
                                held = ItemStack::air();
                            }
                            srv_.resendInventory(*self_);
                            return;
                        }
                    }
                    // riding: horse/llama/pig + boat/minecart (plan9 item 31,45)
                    if (m->kind == MobKind::Horse || m->kind == MobKind::Llama || m->kind == MobKind::Pig
                        || m->kind == MobKind::Boat || m->kind == MobKind::Minecart) {
                        if (self_->vehicleId == -1 && m->riderEntityId == -1) {
                            self_->vehicleId = m->entityId;
                            m->riderEntityId = self_->entityId;
                            srv_.broadcastSetPassengers(m->entityId);
                            return;
                        }
                    }
                    // breeding
                    if (srv_.tryBreedFeed(*self_, *m)) return;
                    if (m->kind == MobKind::Villager) {
                        srv_.openTrading(*self_, *m);
                        tradingVillager_ = target;
                        openMenu_ = nullptr;
                    }
                    break;
                }
            }
        } else {
            (void)in.varint();
        }
        return;
    }

    float dmg = 1.f;
    if (self_->heldSlot >= 0 && self_->heldSlot < 9) {
        const auto& sl = self_->inv[36 + self_->heldSlot];
        if (sl.count > 0) {
            // generic weapon damage + Plan8 EnchantmentHelper sharpness
            std::string iname = sl.name();
            if (iname.find("sword") != std::string::npos) dmg = 6.f;
            else if (iname.find("axe") != std::string::npos) dmg = 7.f;
            else if (iname.find("_sword") != std::string::npos) dmg = 5.f;
            if (sl.itemId == gen::itemIdByName().at("minecraft:iron_sword")) dmg = 6.f;
            // EnchantmentHelper: sharpness bonus
            dmg = EnchantmentHelper::meleeDamageWithEnchant(dmg, sl);
        }
    }
    // strength/weakness bonus
    dmg += meleeDamageBonusFor(self_->effects);
    // attack exhaustion (plan9 #84 + plan10 survival) via HungerManager
    HungerManager::onPlayerAttack(*self_, srv_);

    // ---- PVP: check player victims first (items 76-80 combat)
    for (auto &pp : srv_.playersSnapshot()) {
        auto *victimP = pp.get();
        if (victimP->entityId != target || victimP->dead) continue;
        if (victimP == self_.get()) break; // self-hit ignore
        float before = victimP->health;
        srv_.applyDamage(*victimP, dmg, "player");
        // knockback impulse
        double dx = victimP->x - self_->x;
        double dz = victimP->z - self_->z;
        double len = std::sqrt(dx*dx + dz*dz);
        if (len < 0.01) { dx = (rand()/(double)RAND_MAX - 0.5); dz = (rand()/(double)RAND_MAX - 0.5); len = std::sqrt(dx*dx+dz*dz); }
        double nx = dx / len;
        double nz = dz / len;
        WriteBuffer vel;
        vel.varint(victimP->entityId);
        vel.i16(static_cast<std::int16_t>(nx * 400));
        vel.i16(static_cast<std::int16_t>(300));
        vel.i16(static_cast<std::int16_t>(nz * 400));
        try { victimP->conn->sendPacket(pl::sc::EntityVelocity, vel); } catch (...) {}
        srv_.broadcastPacketExcept(victimP, pl::sc::EntityVelocity, vel);
        (void)before;
        return;
    }

    bool killed = false;
    std::shared_ptr<MobEntity> victim;
    bool hitMob = false;
    MobEntity* hitPtr = nullptr;
    {
        std::lock_guard lk(srv_.entsMtx_);
        for (auto& m : srv_.mobsForTest()) {
            if (m->entityId != target || m->dead) continue;
            srv_.applyDamageToMob(*m, dmg, "player");
            // AI hurt memory → panic/anger
            auto it = srv_.mobAi_.find(m->entityId);
            if (it != srv_.mobAi_.end()) {
                it->second.ctx->lastHurtTick = srv_.tickNoForTest();
                it->second.ctx->lastHurtByEntityId = self_->entityId;
            }
            hitMob = true;
            hitPtr = m.get();
            if (m->dead) { killed = true; victim = m; }
            break;
        }
    }
    // durability on held item (attack) – Plan8 DamageComponent with Unbreaking
    if (self_->heldSlot >=0 && self_->heldSlot < 9) {
        auto &held = self_->inv[36 + self_->heldSlot];
        if (!held.empty() && ItemStack::maxDamageFor(held.itemId) > 0) {
            bool broken = DamageComponent::applyDamage(held, 1);
            if (broken) held = ItemStack::air();
            srv_.resendInventory(*self_);
        }
    }
    // PVP knockback for mob victim (even if not killed)
    if (hitMob && hitPtr) {
        double dx = hitPtr->x - self_->x;
        double dz = hitPtr->z - self_->z;
        double len = std::sqrt(dx*dx + dz*dz);
        if (len < 0.01) { dx = (rand()/(double)RAND_MAX - 0.5); dz = (rand()/(double)RAND_MAX - 0.5); len = std::sqrt(dx*dx+dz*dz); }
        double nx = dx / len;
        double nz = dz / len;
        WriteBuffer vel;
        vel.varint(hitPtr->entityId);
        vel.i16(static_cast<std::int16_t>(nx * 400));
        vel.i16(static_cast<std::int16_t>(300));
        vel.i16(static_cast<std::int16_t>(nz * 400));
        srv_.broadcastPacketExcept(nullptr, pl::sc::EntityVelocity, vel);
        // damage event for mob (no conn, broadcast only for animation via generic?)
        // Could broadcast DamageEvent if needed, but mob has no player conn
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
        // slime split on player kill
        if ((victim->kind == MobKind::Slime || victim->kind == MobKind::MagmaCube) && victim->slimeSize > 0) {
            std::lock_guard lk(srv_.entsMtx_);
            int n = 2 + (rand() % 3);
            for (int s=0; s<n; ++s) {
                auto baby = std::make_shared<MobEntity>();
                baby->entityId = srv_.nextEntityId();
                baby->kind = victim->kind;
                baby->slimeSize = victim->slimeSize - 1;
                const auto& bs = mobStats(baby->kind);
                baby->health = bs.maxHealth * 0.5f;
                if (baby->health < 1.f) baby->health = 1.f;
                baby->x = victim->x + (rand()/(double)RAND_MAX - 0.5) * 0.5;
                baby->y = victim->y;
                baby->z = victim->z + (rand()/(double)RAND_MAX - 0.5) * 0.5;
                baby->lastSeenMs = 0;
                srv_.mobsForTest().push_back(baby);
                srv_.broadcastMobSpawn(*baby);
            }
        }
        std::lock_guard lk(srv_.entsMtx_);
        srv_.mobAi_.erase(target);
        srv_.mobsForTest().erase(
            std::remove_if(srv_.mobsForTest().begin(), srv_.mobsForTest().end(),
                [&](const std::shared_ptr<MobEntity>& x){ return x.get()==victim.get(); }),
            srv_.mobsForTest().end());
        // dismount if victim was vehicle
        if (self_->vehicleId == target) {
            self_->vehicleId = -1;
            srv_.broadcastSetPassengersEmpty(target);
        }
    }
}

} // namespace cppfm
