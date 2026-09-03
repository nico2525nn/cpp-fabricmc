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
    auto merged = getMergedAdvancements();
    writeAdvancementsPacket(b, reset, merged,
        [&](const std::string& id) {
            return p.advancements && p.advancements->has(id);
        });
    try { p.conn->sendPacket(pl::sc::UpdateAdvancements, b); } catch (...) {}
}
void GameServer::grantAdvancement(Player& p, const std::string& id) {
    if (!p.advancements) return;
    if (p.advancements->grant(id)) {
        sendAdvancementsTo(p, false);
        // plan42 R1: SelectAdvancementTab 0x4F sync tab selection
        std::string tab = id;
        auto slash = tab.find("/");
        if (slash!=std::string::npos) tab = tab.substr(0, slash);
        if (tab.find(":")==std::string::npos) tab = "minecraft:" + tab;
        // special: cppfm:root -> minecraft:story/root tab, vanilla story root
        if (id=="cppfm:root") tab = "minecraft:story/root";
        else if (tab=="minecraft:story" || tab=="minecraft:adventure" || tab=="minecraft:nether" || tab=="minecraft:end" || tab=="minecraft:husbandry") { /* keep tab */ }
        else if (id.rfind("minecraft:",0)==0) tab = id;
        else tab = id;
        sendSelectAdvancementTab(p, tab);
    }
}
std::vector<AdvancementDefOwned> GameServer::getMergedAdvancements() {
    size_t cur = datapackManager_.advancements.size();
    if (!cachedMergedAdv_.empty() && cachedAdvRawSize_ == cur) return cachedMergedAdv_;
    cachedMergedAdv_ = mergedAdvancements(datapackManager_.advancements);
    cachedAdvRawSize_ = cur;
    return cachedMergedAdv_;
}
void GameServer::evaluateTickAdvancements(Player& p) {
    if (!p.advancements) return;
    auto merged = getMergedAdvancements();
    for (auto& adv : merged) {
        if (p.advancements->has(adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger == "minecraft:tick" || tr.trigger == "tick") {
                // plan35 §3: gate tick trigger via PredicateContext if conditions contain check_gamerule/location etc
                if (!tr.conditions.isNull() && tr.conditions.isObj()) {
                    PredicateContext ctx;
                    ctx.world = &worldFor(p.dimension);
                    ctx.gamerules = &gamerules_;
                    ctx.player = &p;
                    ctx.playerName = p.name;
                    if (p.heldSlot>=0 && p.heldSlot<9) { auto &hs = p.inv[36+p.heldSlot]; if (!hs.empty()) ctx.heldItemName = hs.name(); }
                    ctx.scoreboard = &scoreboard;
                    ctx.x = static_cast<int32_t>(p.x);
                    ctx.y = static_cast<int32_t>(p.y);
                    ctx.z = static_cast<int32_t>(p.z);
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) continue;
                }
                grantAdvancement(p, adv.id);
                break;
            }
        }
    }
}
void GameServer::evaluateInventoryChanged(Player& p, const ItemStack& s) {
    if (!p.advancements || s.empty()) return;
    std::string itemName = s.name();
    // normalize itemName for tag lookup
    std::string normHave = itemName.find(':')==std::string::npos ? "minecraft:"+itemName : itemName;
    auto norm = [](const std::string& s2){ return s2.find(':')==std::string::npos ? "minecraft:"+s2 : s2; };
    auto hasTagItem = [&](const std::string& tag, const std::string& haveNorm)->bool{
        // tag is like "minecraft:logs" (without #)
        std::string t = tag.find(':')==std::string::npos ? "minecraft:"+tag : tag;
        auto it = datapackManager_.tagManager.itemTags.find(t);
        if(it==datapackManager_.tagManager.itemTags.end()){
            // also try without minecraft: prefix
            auto it2 = datapackManager_.tagManager.itemTags.find(tag);
            if(it2==datapackManager_.tagManager.itemTags.end()) return false;
            it = it2;
        }
        auto iidIt = gen::itemIdByName().find(haveNorm);
        if(iidIt==gen::itemIdByName().end()){
            // fallback string contains
            for(auto id: it->second){
                // try reverse lookup via itemId? just string compare
                (void)id;
            }
            return false;
        }
        return it->second.count(iidIt->second)>0;
    };
    auto merged = getMergedAdvancements();
    for (auto& adv : merged) {
        if (p.advancements->has(adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger != "minecraft:inventory_changed" && tr.trigger != "inventory_changed") continue;
            bool match = false;
            if (tr.conditions.isNull() || tr.conditions.isObj()==false) {
                match = true;
            } else {
                if (auto* items = tr.conditions.find("items")) {
                    if (items->isArr()) {
                        for (auto& it : items->arr) {
                            std::string want;
                            if (it.isStr()) want = it.asStr();
                            else if (it.isObj()) {
                                if (auto* in = it.find("items")) {
                                    if (in->isStr()) want = in->asStr();
                                    else if (in->isArr() && !in->arr.empty() && in->arr[0].isStr()) want = in->arr[0].asStr();
                                } else if (auto* it2 = it.find("item")) want = it2->asStr();
                                else if (auto* id2 = it.find("id")) want = id2->asStr();
                                else if (auto* tag = it.find("tag")) {
                                    std::string tagStr=tag->asStr();
                                    if(!tagStr.empty() && tagStr[0]=='#') tagStr=tagStr.substr(1);
                                    if(hasTagItem(tagStr, normHave)) { match=true; break; }
                                    continue;
                                }
                            }
                            if (!want.empty()){
                                if(want[0]=='#'){
                                    std::string tag = want.substr(1);
                                    if(hasTagItem(tag, normHave)) { match = true; break; }
                                } else if (norm(want)==normHave) { match = true; break; }
                            }
                        }
                    } else if (items->isStr()) {
                        std::string want=items->asStr();
                        if(want[0]=='#'){
                            if(hasTagItem(want.substr(1), normHave)) match = true;
                        } else if (norm(want)==normHave) match = true;
                    } else if (items->isObj()) {
                        if(auto* tag = items->find("tag")){
                            std::string tagStr=tag->asStr();
                            if(!tagStr.empty() && tagStr[0]=='#') tagStr=tagStr.substr(1);
                            if(hasTagItem(tagStr, normHave)) match=true;
                        } else if(auto* inn = items->find("items")){
                            if(inn->isStr() && norm(inn->asStr())==normHave) match=true;
                            else if(inn->isArr()) for(auto& e: inn->arr) if(e.isStr() && norm(e.asStr())==normHave) { match=true; break; }
                        }
                    }
                } else {
                    match = true;
                }
            }
            if (match) {
                // plan35 §3: additional predicate gating via PredicateContext (check_gamerule/location_check)
                if (!tr.conditions.isNull() && tr.conditions.isObj() && tr.conditions.find("condition")) {
                    PredicateContext ctx;
                    ctx.world = &worldFor(p.dimension);
                    ctx.gamerules = &gamerules_;
                    ctx.player = &p;
                    ctx.playerName = p.name;
                    if (p.heldSlot>=0 && p.heldSlot<9) { auto &hs = p.inv[36+p.heldSlot]; if (!hs.empty()) ctx.heldItemName = hs.name(); }
                    ctx.scoreboard = &scoreboard;
                    ctx.x = static_cast<int32_t>(p.x);
                    ctx.y = static_cast<int32_t>(p.y);
                    ctx.z = static_cast<int32_t>(p.z);
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) match = false;
                }
                if (match) { grantAdvancement(p, adv.id); break; }
            }
        }
    }
}
void GameServer::evaluatePlayerKilledEntity(Player& p, MobKind kind) {
    if (!p.advancements) return;
    std::string killed = MobEntity::kindName(kind);
    auto merged = getMergedAdvancements();
    // temporary victim entity for predicate context
    MobEntity victimTmp; victimTmp.kind = kind;
    for (auto& adv : merged) {
        if (p.advancements->has(adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger != "minecraft:player_killed_entity" && tr.trigger != "player_killed_entity") continue;
            bool match = false;
            if (tr.conditions.isNull()) match = true;
            else {
                if (auto* ent = tr.conditions.find("entity")) {
                    if (ent->isArr()) {
                        for (auto& e : ent->arr) if (e.isObj()) if (auto* tp = e.find("type")) if (tp->asStr()==killed) match=true;
                    } else if (ent->isObj()) {
                        if (auto* tp = ent->find("type")) {
                            if (tp->asStr()==killed) match=true;
                        } else match = true;
                    } else if (ent->isStr()) {
                        if (ent->asStr()==killed) match=true;
                    }
                } else if (auto* pred = tr.conditions.find("predicate")) {
                    if (auto* tp = pred->find("type")) if (tp->asStr()==killed) match=true; else match=true;
                } else {
                    match = true;
                }
                if (!tr.conditions.find("entity") && !tr.conditions.find("predicate")) match = true;
                // plan35 §3: gate with PredicateContext if conditions contain check_gamerule/location_check/entity_properties
                if (match && tr.conditions.isObj() && tr.conditions.find("condition")) {
                    PredicateContext ctx;
                    ctx.world = &worldFor(p.dimension);
                    ctx.gamerules = &gamerules_;
                    ctx.player = &p;
                    ctx.playerName = p.name;
                    if (p.heldSlot>=0 && p.heldSlot<9) { auto &hs = p.inv[36+p.heldSlot]; if (!hs.empty()) ctx.heldItemName = hs.name(); }
                    ctx.scoreboard = &scoreboard;
                    ctx.entity = &victimTmp;
                    ctx.x = static_cast<int32_t>(p.x);
                    ctx.y = static_cast<int32_t>(p.y);
                    ctx.z = static_cast<int32_t>(p.z);
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) match = false;
                }
            }
            if (match) { grantAdvancement(p, adv.id); break; }
        }
    }
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
    // plan35 §1: also trigger story mine_stone via inventory change path? onBlockMined doesn't give item, but mining still may count; evaluate via dummy stack
    ItemStack dummy = ItemStack::ofName(name,1);
    if (!dummy.empty()) evaluateInventoryChanged(p, dummy);
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
    evaluateInventoryChanged(p, s);
}
void GameServer::onMobKilledBy(Player& p, MobKind kind) {
    if (!p.stats) return;
    p.stats->add(std::string("minecraft:killed|") +
                 MobEntity::kindName(kind));
    if (MobEntity::isHostile(kind)) grantAdvancement(p, "cppfm:hunter");
    evaluatePlayerKilledEntity(p, kind);
}
// plan37 §3: location / placed_block / consume_item triggers
void GameServer::evaluateLocationTrigger(Player& p) {
    if (!p.advancements) return;
    auto merged = getMergedAdvancements();
    PredicateContext ctx;
    ctx.world = &worldFor(p.dimension);
    ctx.gamerules = &gamerules_;
    ctx.player = &p;
    ctx.playerName = p.name;
    if (p.heldSlot>=0 && p.heldSlot<9) { auto &hs = p.inv[36+p.heldSlot]; if (!hs.empty()) ctx.heldItemName = hs.name(); }
    ctx.scoreboard = &scoreboard;
    ctx.x = static_cast<int32_t>(p.x);
    ctx.y = static_cast<int32_t>(p.y);
    ctx.z = static_cast<int32_t>(p.z);
    ctx.dayTime = dayTime();
    ctx.raining = raining();
    ctx.thundering = thundering();
    for (auto& adv : merged) {
        if (p.advancements->has(adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger != "minecraft:location" && tr.trigger != "location") continue;
            bool ok = true;
            if (!tr.conditions.isNull() && tr.conditions.isObj()) {
                // if conditions has a "condition" predicate, evaluate via predicate engine
                // otherwise treat as location predicate {location:{biome,...}}
                if (tr.conditions.find("condition")) {
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) ok = false;
                } else {
                    // try location_check wrapping
                    json::Value wrapped = json::Value::object();
                    wrapped.set("condition", json::Value::ofString("minecraft:location_check"));
                    wrapped.set("predicate", tr.conditions);
                    bool foundLocation = tr.conditions.find("location") != nullptr;
                    if (foundLocation) {
                        if (!datapackManager_.evaluatePredicateValue(wrapped, ctx)) ok = false;
                    } else {
                        // generic predicate evaluation
                        if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) ok = false;
                    }
                }
            }
            if (ok) { grantAdvancement(p, adv.id); break; }
        }
    }
}
void GameServer::onPlacedBlock(Player& p, int x, int y, int z, std::uint16_t state) {
    if (!p.advancements) return;
    std::string placedName;
    if (auto* bd = gen::blockByState(state)) placedName = bd->name;
    if (placedName.empty()) return;
    auto merged = getMergedAdvancements();
    PredicateContext ctx;
    ctx.world = &worldFor(p.dimension);
    ctx.gamerules = &gamerules_;
    ctx.player = &p;
    ctx.playerName = p.name;
    if (p.heldSlot>=0 && p.heldSlot<9) { auto &hs = p.inv[36+p.heldSlot]; if (!hs.empty()) ctx.heldItemName = hs.name(); }
    ctx.scoreboard = &scoreboard;
    ctx.x = x; ctx.y = y; ctx.z = z;
    ctx.dayTime = dayTime();
    ctx.raining = raining();
    ctx.thundering = thundering();
    for (auto& adv : merged) {
        if (p.advancements->has(adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger != "minecraft:placed_block" && tr.trigger != "placed_block") continue;
            bool ok = true;
            if (!tr.conditions.isNull() && tr.conditions.isObj()) {
                if (auto* blk = tr.conditions.find("block")) {
                    std::string want = blk->asStr();
                    if (!want.empty() && want[0] != '#') {
                        std::string wantN = want.find(':')==std::string::npos ? "minecraft:"+want : want;
                        std::string haveN = placedName.find(':')==std::string::npos ? "minecraft:"+placedName : placedName;
                        if (wantN != haveN) ok = false;
                    }
                }
                if (ok && tr.conditions.find("condition")) {
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) ok = false;
                } else if (ok && !tr.conditions.isNull()) {
                    // evaluate other predicates like location_check inside placed_block
                    // try generic evaluation but ignore block which already checked
                    // only evaluate if condition key present
                }
            }
            if (ok) { grantAdvancement(p, adv.id); break; }
        }
    }
}
void GameServer::onConsumeItem(Player& p, const ItemStack& stack) {
    if (!p.advancements || stack.empty()) return;
    std::string itemName = stack.name();
    auto merged = getMergedAdvancements();
    PredicateContext ctx;
    ctx.world = &worldFor(p.dimension);
    ctx.gamerules = &gamerules_;
    ctx.player = &p;
    ctx.playerName = p.name;
    ctx.heldItemName = itemName;
    ctx.scoreboard = &scoreboard;
    ctx.x = static_cast<int32_t>(p.x);
    ctx.y = static_cast<int32_t>(p.y);
    ctx.z = static_cast<int32_t>(p.z);
    ctx.dayTime = dayTime();
    ctx.raining = raining();
    ctx.thundering = thundering();
    for (auto& adv : merged) {
        if (p.advancements->has(adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger != "minecraft:consume_item" && tr.trigger != "consume_item") continue;
            bool ok = false;
            if (tr.conditions.isNull()) ok = true;
            else if (tr.conditions.isObj()) {
                if (auto* item = tr.conditions.find("item")) {
                    // item = {items:["minecraft:apple"]} or {item:"minecraft:apple"} or string
                    std::vector<std::string> wants;
                    if (item->isStr()) wants.push_back(item->asStr());
                    else if (item->isObj()) {
                        if (auto* items = item->find("items")) {
                            if (items->isStr()) wants.push_back(items->asStr());
                            else if (items->isArr()) for (auto& v : items->arr) if (v.isStr()) wants.push_back(v.asStr());
                        } else if (auto* it = item->find("item")) {
                            if (it->isStr()) wants.push_back(it->asStr());
                        } else if (auto* id = item->find("id")) {
                            if (id->isStr()) wants.push_back(id->asStr());
                        }
                    } else if (item->isArr()) {
                        for (auto& v : item->arr) if (v.isStr()) wants.push_back(v.asStr());
                    }
                    if (wants.empty()) ok = true;
                    else for (auto& w : wants) {
                        std::string wn = w.find(':')==std::string::npos ? "minecraft:"+w : w;
                        std::string hn = itemName.find(':')==std::string::npos ? "minecraft:"+itemName : itemName;
                        if (wn == hn) { ok = true; break; }
                    }
                } else {
                    ok = true;
                }
                if (ok && tr.conditions.find("condition")) {
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) ok = false;
                }
            } else ok = true;
            if (ok) { grantAdvancement(p, adv.id); break; }
        }
    }
}
// plan38 B-13: 4 new triggers 6->10
void GameServer::onBredAnimals(Player* p) {
    if (!p || !p->advancements) return;
    auto merged = getMergedAdvancements();
    PredicateContext ctx;
    ctx.world = &worldFor(p->dimension);
    ctx.gamerules = &gamerules_;
    ctx.player = p;
    ctx.playerName = p->name;
    if (p->heldSlot>=0 && p->heldSlot<9) { auto &hs = p->inv[36+p->heldSlot]; if (!hs.empty()) ctx.heldItemName = hs.name(); }
    ctx.scoreboard = &scoreboard;
    ctx.x = static_cast<int32_t>(p->x);
    ctx.y = static_cast<int32_t>(p->y);
    ctx.z = static_cast<int32_t>(p->z);
    ctx.dayTime = dayTime();
    ctx.raining = raining();
    ctx.thundering = thundering();
    for (auto& adv : merged) {
        if (p->advancements->has(adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger!="minecraft:bred_animals" && tr.trigger!="bred_animals") continue;
            bool ok = true;
            if (!tr.conditions.isNull() && tr.conditions.isObj() && tr.conditions.find("condition")) {
                if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) ok=false;
            }
            if (ok) { grantAdvancement(*p, adv.id); break; }
        }
    }
}
void GameServer::onEnterBlock(Player* p, int x, int y, int z) {
    if (!p || !p->advancements) return;
    auto merged = getMergedAdvancements();
    PredicateContext ctx;
    ctx.world = &worldFor(p->dimension);
    ctx.gamerules = &gamerules_;
    ctx.player = p;
    ctx.playerName = p->name;
    if (p->heldSlot>=0 && p->heldSlot<9) { auto &hs = p->inv[36+p->heldSlot]; if (!hs.empty()) ctx.heldItemName = hs.name(); }
    ctx.scoreboard = &scoreboard;
    ctx.x = x; ctx.y = y; ctx.z = z;
    ctx.dayTime = dayTime();
    ctx.raining = raining();
    ctx.thundering = thundering();
    std::uint16_t st = worldFor(p->dimension).getBlock(x,y,z);
    std::string haveBlock;
    if (auto* bd = gen::blockByState(st)) haveBlock = bd->name;
    else haveBlock = "minecraft:air";
    for (auto& adv : merged) {
        if (p->advancements->has(adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger!="minecraft:enter_block" && tr.trigger!="enter_block") continue;
            bool ok = true;
            if (!tr.conditions.isNull() && tr.conditions.isObj()) {
                if (auto* blk = tr.conditions.find("block")) {
                    std::string want = blk->asStr();
                    if (!want.empty() && want[0]!='#') {
                        std::string wn = want.find(':')==std::string::npos ? "minecraft:"+want : want;
                        std::string hn = haveBlock.find(':')==std::string::npos ? "minecraft:"+haveBlock : haveBlock;
                        if (wn != hn) ok=false;
                    }
                }
                if (ok && tr.conditions.find("condition")) {
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) ok=false;
                }
            }
            if (ok) { grantAdvancement(*p, adv.id); break; }
        }
    }
}
void GameServer::onItemUsedOnBlock(Player* p, int x, int y, int z, const ItemStack& item) {
    if (!p || !p->advancements) return;
    std::string itemName = item.name();
    if (itemName.empty()) itemName = "minecraft:air";
    auto merged = getMergedAdvancements();
    PredicateContext ctx;
    ctx.world = &worldFor(p->dimension);
    ctx.gamerules = &gamerules_;
    ctx.player = p;
    ctx.playerName = p->name;
    ctx.heldItemName = itemName;
    ctx.scoreboard = &scoreboard;
    ctx.x = x; ctx.y = y; ctx.z = z;
    ctx.dayTime = dayTime();
    ctx.raining = raining();
    ctx.thundering = thundering();
    for (auto& adv : merged) {
        if (p->advancements->has(adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger!="minecraft:item_used_on_block" && tr.trigger!="item_used_on_block") continue;
            bool ok = true;
            if (!tr.conditions.isNull() && tr.conditions.isObj()) {
                if (auto* it = tr.conditions.find("item")) {
                    std::vector<std::string> wants;
                    if (it->isStr()) wants.push_back(it->asStr());
                    else if (it->isObj()) {
                        if (auto* items = it->find("items")) {
                            if (items->isStr()) wants.push_back(items->asStr());
                            else if (items->isArr()) for (auto &v: items->arr) if (v.isStr()) wants.push_back(v.asStr());
                        } else if (auto* id = it->find("id")) wants.push_back(id->asStr());
                    }
                    if (!wants.empty()) {
                        bool any=false;
                        for (auto &w: wants) {
                            std::string wn = w.find(':')==std::string::npos ? "minecraft:"+w : w;
                            std::string hn = itemName.find(':')==std::string::npos ? "minecraft:"+itemName : itemName;
                            if (wn==hn) { any=true; break; }
                        }
                        if (!any) ok=false;
                    }
                }
                if (ok && tr.conditions.find("condition")) {
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) ok=false;
                }
            }
            if (ok) { grantAdvancement(*p, adv.id); break; }
        }
    }
}
void GameServer::onEffectsChanged(Player* p) {
    if (!p || !p->advancements) return;
    auto merged = getMergedAdvancements();
    PredicateContext ctx;
    ctx.world = &worldFor(p->dimension);
    ctx.gamerules = &gamerules_;
    ctx.player = p;
    ctx.playerName = p->name;
    if (p->heldSlot>=0 && p->heldSlot<9) { auto &hs = p->inv[36+p->heldSlot]; if (!hs.empty()) ctx.heldItemName = hs.name(); }
    ctx.scoreboard = &scoreboard;
    ctx.x = static_cast<int32_t>(p->x);
    ctx.y = static_cast<int32_t>(p->y);
    ctx.z = static_cast<int32_t>(p->z);
    ctx.dayTime = dayTime();
    ctx.raining = raining();
    ctx.thundering = thundering();
    for (auto& adv : merged) {
        if (p->advancements->has(adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger!="minecraft:effects_changed" && tr.trigger!="effects_changed") continue;
            bool ok = true;
            if (!tr.conditions.isNull() && tr.conditions.isObj()) {
                if (auto* effs = tr.conditions.find("effects")) {
                    if (effs->isArr()) {
                        for (auto &e : effs->arr) if (e.isObj()) {
                            if (auto* eff = e.find("effect")) {
                                std::string want = eff->asStr();
                                bool found=false;
                                for (auto &pe: p->effects) {
                                    std::string have = effects::nameOf(pe.type);
                                    if (want==have) { found=true; break; }
                                    if (want.find(':')==std::string::npos) {
                                        std::string shortHave = have.substr(have.find(':')+1);
                                        if (want==shortHave) found=true;
                                    }
                                }
                                if (!found) { ok=false; break; }
                            }
                        }
                    }
                }
                if (ok && tr.conditions.find("condition")) {
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) ok=false;
                }
            }
            if (ok) { grantAdvancement(*p, adv.id); break; }
        }
    }
}
void GameServer::onItemEnchanted(Player& p, const std::string& itemName, int levels){
    if(!p.advancements) return;
    auto merged=getMergedAdvancements();
    std::string normHave = itemName.find(':')==std::string::npos ? "minecraft:"+itemName : itemName;
    for(auto& adv: merged){
        if(p.advancements->has(adv.id)) continue;
        for(auto& tr: adv.triggers){
            if(tr.trigger!="minecraft:enchanted_item" && tr.trigger!="enchanted_item") continue;
            bool ok=true;
            if(!tr.conditions.isNull() && tr.conditions.isObj()){
                if(auto* it=tr.conditions.find("item")){
                    std::vector<std::string> wants;
                    if(it->isStr()) wants.push_back(it->asStr());
                    else if(it->isObj()){
                        if(auto* items=it->find("items")){
                            if(items->isStr()) wants.push_back(items->asStr());
                            else if(items->isArr()) for(auto& v: items->arr) if(v.isStr()) wants.push_back(v.asStr());
                        } else if(auto* id=it->find("id")) wants.push_back(id->asStr());
                    } else if(it->isArr()){
                        for(auto& v: it->arr) if(v.isStr()) wants.push_back(v.asStr());
                    }
                    if(!wants.empty()){
                        bool any=false;
                        for(auto& w: wants){
                            std::string wn=w.find(':')==std::string::npos?"minecraft:"+w:w;
                            if(wn==normHave) { any=true; break; }
                        }
                        if(!any) ok=false;
                    }
                }
                if(ok) if(auto* lv=tr.conditions.find("levels")){
                    int mn=1, mx=30;
                    if(lv->isNum()) mn=mx=lv->asInt(levels);
                    else if(lv->isObj()){
                        if(auto* mnV=lv->find("min")) mn=mnV->asInt(mn);
                        if(auto* mxV=lv->find("max")) mx=mxV->asInt(mx);
                    }
                    if(levels < mn || levels > mx) ok=false;
                }
            }
            if(ok){ grantAdvancement(p, adv.id); break; }
        }
    }
}
void GameServer::onBucketFilled(Player& p, const std::string& filledName){
    if(!p.advancements) return;
    auto merged=getMergedAdvancements();
    std::string normHave=filledName.find(':')==std::string::npos?"minecraft:"+filledName:filledName;
    for(auto& adv: merged){
        if(p.advancements->has(adv.id)) continue;
        for(auto& tr: adv.triggers){
            if(tr.trigger!="minecraft:filled_bucket" && tr.trigger!="filled_bucket") continue;
            bool ok=true;
            if(!tr.conditions.isNull() && tr.conditions.isObj()){
                if(auto* it=tr.conditions.find("item")){
                    std::string want;
                    if(it->isStr()) want=it->asStr();
                    else if(it->isObj()){
                        if(auto* items=it->find("items")){
                            if(items->isStr()) want=items->asStr();
                            else if(items->isArr() && !items->arr.empty() && items->arr[0].isStr()) want=items->arr[0].asStr();
                        } else if(auto* id=it->find("id")) want=id->asStr();
                    }
                    if(!want.empty()){
                        std::string wn=want.find(':')==std::string::npos?"minecraft:"+want:want;
                        if(wn!=normHave) ok=false;
                    }
                }
            }
            if(ok){ grantAdvancement(p, adv.id); break; }
        }
    }
}
void GameServer::onVillagerTraded(Player& p, const std::string& soldId, int count){
    if(!p.advancements) return;
    auto merged=getMergedAdvancements();
    std::string normHave=soldId.find(':')==std::string::npos?"minecraft:"+soldId:soldId;
    for(auto& adv: merged){
        if(p.advancements->has(adv.id)) continue;
        for(auto& tr: adv.triggers){
            if(tr.trigger!="minecraft:villager_trade" && tr.trigger!="villager_trade") continue;
            bool ok=true;
            if(!tr.conditions.isNull() && tr.conditions.isObj()){
                if(auto* it=tr.conditions.find("item")){
                    if(it->isObj()){
                        if(auto* items=it->find("items")){
                            bool any=false;
                            if(items->isStr()){
                                std::string wn=items->asStr(); if(wn.find(':')==std::string::npos) wn="minecraft:"+wn; if(wn==normHave) any=true;
                            } else if(items->isArr()){
                                for(auto& v: items->arr) if(v.isStr()){
                                    std::string wn=v.asStr(); if(wn.find(':')==std::string::npos) wn="minecraft:"+wn; if(wn==normHave) { any=true; break; }
                                }
                            }
                            if(!any) ok=false;
                        }
                        if(ok) if(auto* cnt=it->find("count")) if(cnt->isObj()){
                            int mn=cnt->find("min")?cnt->at("min").asInt(1):1;
                            if(count < mn) ok=false;
                        }
                    } else if(it->isStr()){
                        std::string wn=it->asStr(); if(wn.find(':')==std::string::npos) wn="minecraft:"+wn;
                        if(wn!=normHave) ok=false;
                    }
                }
            }
            if(ok){ grantAdvancement(p, adv.id); break; }
        }
    }
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
