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
void GameServer::broadcastDigStage(Player& p, std::int8_t stage) {
    WriteBuffer b;
    b.varint(p.entityId);
    b.position(p.digX, p.digY, p.digZ);
    b.i8(stage);
    broadcastPacketExcept(nullptr, proto::pl::sc::BlockBreakAnimation, b);
}
void GameServer::sendSetHealth(Player& p) {
    WriteBuffer b;
    b.f32(p.health);
    b.varint(p.food);
    b.f32(p.saturation);
    try { p.conn->sendPacket(pl::sc::SetHealth, b); } catch (...) {}
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
void GameServer::broadcastPlayerChat(Player& sender, const std::string& message, int64_t timestamp) {
    WriteBuffer b;
    b.uuid(sender.uuid.data());
    b.varint(0);
    b.boolean(false);
    b.string(message);
    b.i64(timestamp);
    b.i64(0);
    b.varint(0);
    b.boolean(false);
    b.varint(0);
    b.varint(0);
    nbt::writeTextComponent(b, sender.name);
    b.boolean(false);
    broadcastPacketExcept(nullptr, proto::pl::sc::PlayerChat, b);
}
bool GameServer::validateFeatureFlags(const std::vector<std::array<std::string,3>>& clientPacks) {
    // plan22 network polish: FeatureFlags 0x0C vanilla ["minecraft:vanilla"] + SelectKnownPacks core 1.21.4.
    // Accept empty (vanilla fallback) and any pack containing minecraft:core or minecraft:vanilla.
    // Lenient: also accept unknown packs to avoid kicking modded clients; strict would reject non-vanilla.
    if (clientPacks.empty()) return true;
    for (auto &p : clientPacks) if (p[0]=="minecraft" && (p[1]=="core" || p[1]=="vanilla")) return true;
    return true; // lenient accept (was false for non-empty, too strict)
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
    // plan16: Slime health scaling size² (vanilla: health = size², size=4 =>16, 2=>4,1=>1)
    if (kind==MobKind::Slime || kind==MobKind::MagmaCube) {
        mob->health = MobEntity::slimeHealthForSize(mob->slimeSize);
    }
    // plan16: Horse variant random (vanilla HorseEntity random health 15-30, variant 0..34 = 7 colors *5 markings) — plan18 polish strict 35
    if (kind==MobKind::Horse) {
        int color = rand() % 7; // 0..6
        int marking = rand() % 5; // 0..4
        mob->horseVariant = color * 5 + marking; // 0..34
        mob->horseJumpStrength = 0.4f + (rand()/(float)RAND_MAX)*0.6f; // 0.4..1.0
        mob->health = 15.0f + (rand() % 16); // 15..30 vanilla random
    }
    // plan14 §4: VillagerData init (profession/level/type) — plan18 polish: NITWIT 1/12 random per Yarn VillagerProfession NITWIT
    if (kind==MobKind::Villager) {
        mob->villagerData.type = static_cast<VillagerData::Type>(rand()%7);
        if (rand() % 12 == 0) mob->villagerData.profession = VillagerData::NITWIT;
        else mob->villagerData.profession = VillagerData::FARMER;
        mob->villagerData.level = 1;
        mob->villagerLevel = 1;
        mob->villagerXp = 0;
        mob->villagerRestocksToday = 0;
        mob->villagerLastRestockDay = -1;
        mob->restockUntil = 0;
    }
    // plan17 LOW: sheep woolColor random per wiki 81.8% white, 5% black/gray/light_gray, 3% brown, 0.164% pink (was always white) — plan18 polish refine
    if (kind==MobKind::Sheep) {
        int r = rand() % 1000;
        if (r < 818) mob->woolColor = 0; // white 81.8%
        else if (r < 868) mob->woolColor = 15; // black 5%
        else if (r < 918) mob->woolColor = 7; // gray 5%
        else if (r < 968) mob->woolColor = 8; // light_gray 5%
        else if (r < 998) mob->woolColor = 12; // brown 3%
        else mob->woolColor = 6; // pink 0.2% (~0.164% vanilla)
        // set sheared metadata false initially
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
void GameServer::broadcastSetPassengersEmpty(std::int32_t vehicleId) {
    WriteBuffer b;
    b.varint(vehicleId);
    b.varint(0);
    broadcastPacketExcept(nullptr, proto::pl::sc::SetPassengers, b);
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
bool GameServer::spawnMobByTypeName(const std::string& name, double x, double y,
                                     double z) {
    // Plan8: handle lightning_bolt via strikeLightning (charged creeper)
    // plan22 inventory polish: expand 107->149 (MobKind 149, Yarn EntityType parity E1) for SpawnEgg linkage
    // Use dynamic count via MobKind::WitherSkull+1 so future 149+ stays correct; also handle bare name + prefix fallback
    if (name=="minecraft:lightning_bolt" || name=="lightning_bolt" || name=="minecraft:lightning") {
        strikeLightning(x,y,z);
        return true;
    }
    constexpr int kMobCount = static_cast<int>(MobKind::WitherSkull) + 1; // 149 in 1.21.4
    for (int i = 0; i < kMobCount; ++i) {
        auto kind = static_cast<MobKind>(i);
        const char* n = MobEntity::kindName(kind);
        if (name == n) { spawnMob(kind, x, y, z); return true; }
    }
    if (name.find(':') == std::string::npos) {
        std::string full = "minecraft:" + name;
        for (int i = 0; i < kMobCount; ++i) {
            auto kind = static_cast<MobKind>(i);
            if (full == MobEntity::kindName(kind)) { spawnMob(kind, x, y, z); return true; }
        }
        // also try without prefix via entityTypeId map (some callers pass short name)
        auto it2 = gen::entityTypeIdByName().find(full);
        if (it2 != gen::entityTypeIdByName().end()) {
            for (int i = 0; i < kMobCount; ++i) {
                auto kind = static_cast<MobKind>(i);
                if (MobEntity::typeId(kind) == it2->second) { spawnMob(kind, x, y, z); return true; }
            }
        }
    }
    auto it = gen::entityTypeIdByName().find(name);
    if (it != gen::entityTypeIdByName().end()) {
        for (int i = 0; i < kMobCount; ++i) {
            auto kind = static_cast<MobKind>(i);
            if (MobEntity::typeId(kind) == it->second) { spawnMob(kind, x, y, z); return true; }
        }
        // fallback: handle short name without minecraft: via map (e.g., "armadillo")
        if (name.find(':') != std::string::npos) {
            auto shortName = name.substr(name.find(':')+1);
            auto itS = gen::entityTypeIdByName().find(shortName);
            if (itS != gen::entityTypeIdByName().end()) {
                for (int i = 0; i < kMobCount; ++i) {
                    auto kind = static_cast<MobKind>(i);
                    if (MobEntity::typeId(kind) == itS->second) { spawnMob(kind, x, y, z); return true; }
                }
            }
        }
    }
    // also handle spawn_egg style name directly (e.g., "minecraft:armadillo" from "minecraft:armadillo_spawn_egg" already stripped)
    // but if caller passes the egg name itself, strip suffix and retry once
    if (name.ends_with("_spawn_egg")) {
        std::string base = name.substr(0, name.size()-std::string("_spawn_egg").size());
        if (base != name) return spawnMobByTypeName(base, x, y, z);
    }
    return false;
}
bool GameServer::trySpawnEgg(Player& p, ItemStack& stack, BlockPos hitPos, int face) {
    std::string n = stack.name();
    if (!n.ends_with("_spawn_egg")) return false;
    BlockPos spawnPos = hitPos.offset(face);
    World& w = worldFor(p.dimension);
    // plan17 LOW: vanilla isSpaceEmpty(entity bbox) – check air + replaceable (tall_grass, snow etc) not just air
    {
        std::uint16_t st = w.getBlock(spawnPos.x, spawnPos.y, spawnPos.z);
        if (st != 0) {
            auto* def = gen::blockByState(st);
            bool replaceable = false;
            if (def) {
                std::string_view bn = def->name;
                // vanilla SpawnEggItem requires collision empty: short grass, fern, vines etc
                if (bn=="minecraft:short_grass"||bn=="minecraft:tall_grass"||bn=="minecraft:fern"||bn=="minecraft:large_fern"
                    ||bn=="minecraft:dead_bush"||bn=="minecraft:vine"||bn=="minecraft:snow"||bn=="minecraft:air"
                    ||bn=="minecraft:cave_air"||bn=="minecraft:void_air"||bn.find("water")!=std::string::npos) replaceable = true;
            }
            if (!replaceable) return false;
        }
        // also check block above for 2-high mobs is not solid (best effort)
        std::uint16_t st2 = w.getBlock(spawnPos.x, spawnPos.y+1, spawnPos.z);
        if (st2 != 0) {
            auto* d2 = gen::blockByState(st2);
            if (d2 && std::string(d2->name)!="minecraft:air" && std::string(d2->name)!="minecraft:cave_air"
                && std::string(d2->name).find("water")==std::string::npos
                && std::string(d2->name)!="minecraft:short_grass" && std::string(d2->name)!="minecraft:tall_grass")
            {
                // allow if same replaceable, else still allow but log
            }
        }
    }
    if (!isInsideBorder(spawnPos.x + 0.5, spawnPos.z + 0.5)) return false;
    std::string mob = n.substr(0, n.size() - std::string("_spawn_egg").size());
    if (mob.empty()) return false;
    double sx = spawnPos.x + 0.5, sy = spawnPos.y, sz = spawnPos.z + 0.5;
    if (!spawnMobByTypeName(mob, sx, sy, sz)) return false;
    if (p.gamemode != 1) {
        if (--stack.count <= 0) stack = ItemStack::air();
        resendInventory(p);
    }
    return true;
}
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
} // namespace cppfm
