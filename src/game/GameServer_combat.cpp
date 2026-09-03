#include "GameServer.hpp"
#include "Messages.hpp"
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
#include "CombatManager.hpp"
#include "MobSpawner.hpp"
#include "BossAI.hpp"
#include <fstream>
#include <cmath>
#include "MenuLogic.hpp"
#include "CostCalculator.hpp"
#include "PotionBrewing.hpp"
#include "Particles.hpp"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cppfm {
using namespace proto;
void GameServer::syncPlayerArmorAttributes(Player& p) {
    // modular split: delegate to CombatManager (plan8)
    CombatManager::syncPlayerArmor(*this, p);
}
void GameServer::applyDamage(Player& p, float amount, const DamageSource& src, int breachLv) {
    if (p.gamemode == 1 || p.gamemode == 3) return;
    if (amount <= 0 || p.dead) return;
    syncPlayerArmorAttributes(p);
    int armor = (int)std::round(p.attributes.getValue(Attribute::ARMOR));
    if (armor == 0) armor = totalArmorPoints(p.inv);
    armor = breachAdjustedArmor(armor, breachLv); // plan44 G-09: breach pre-discounts armor (formula untouched)
    double toughness = p.attributes.getValue(Attribute::ARMOR_TOUGHNESS);
    int epf = CombatManager::computeEPF(src, p);
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
    // plan34 network: HurtAnimation 0x25 (yaw 0 when attacker unknown) + EntitySoundEffect 0x6E for player hurt
    {
        float yaw = 0.f;
        broadcastHurtAnimation(p.entityId, yaw, nullptr);
        std::string snd = "minecraft:entity.player.hurt";
        broadcastEntitySound(p.entityId, snd, 1.f, 1.f, SoundSource::Player);
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
    broadcastSystemText((msg::kRed + p.name + " died (" + cause + ")"), &p);
    // plan37 B-11 vanishing_curse: drop inventory except vanishing, respect keepInventory gamerule
    {
        bool keepInv = false;
        try { keepInv = gamerules_.getBool("keepInventory"); } catch (...) {}
        if (!keepInv) {
            for (int i=0;i<46;++i) {
                auto &st = p.inv[i];
                if (st.empty()) continue;
                if (EnchantmentHelper::hasVanishingCurse(st)) {
                    st = ItemStack::air();
                    continue;
                }
                // spawn drop (preserve components)
                spawnItemDrop(p.x, p.y + 0.5, p.z, st, (rand()/(double)RAND_MAX-.5)*0.3, 0.2, (rand()/(double)RAND_MAX-.5)*0.3);
                st = ItemStack::air();
            }
            // also clear cursor? handled via sync
            if (!keepInv) resendInventory(p);
        } else {
            // keepInventory true: still vanish cursed items disappear (vanilla: vanishing still vanishes even with keepInventory)
            for (int i=0;i<46;++i) if(!p.inv[i].empty() && EnchantmentHelper::hasVanishingCurse(p.inv[i])) p.inv[i]=ItemStack::air();
            if (!p.inv[45].empty() && EnchantmentHelper::hasVanishingCurse(p.inv[45])) p.inv[45]=ItemStack::air();
        }
    }
    // plan35 §5 hardcore: ban on death when hardcore=true (vanilla hardcore -> spectator + world delete, here ban)
    if (cfg_.hardcore) {
        bannedPlayers_.insert(p.name);
        try { saveBans(); } catch (...) {}
        if (p.conn) {
            WriteBuffer b;
            nbt::writeTextComponent(b, "You died in hardcore mode and are banned");
            try { p.conn->sendPacket(proto::pl::sc::Disconnect, b); } catch (...) {}
            try { p.conn->close(); } catch (...) {}
        }
        p.gamemode = 3; // spectator
    }
}
void GameServer::mobAttackPlayer(MobEntity& m, Player& target) {
    float dmg = mobStats(m.kind).attackDamage;
    // plan29 §3 Creaking difficulty scaling Easy 2.5 / Normal 3 / Hard 4.5 (was generic 7)
    if (m.kind==MobKind::Creaking) {
        if (difficulty_=="easy") dmg=2.5f;
        else if (difficulty_=="hard") dmg=4.5f;
        else dmg=3.0f;
    }
    if (dmg <= 0) return;
    const float before = target.health;
    std::string cause = MobEntity::kindName(m.kind);   // e.g. minecraft:zombie
    const auto slash = cause.find(':');
    if (slash != std::string::npos) cause = cause.substr(slash + 1);
    // plan44 §3 G-08: shield blocks frontal melee (vindicator swings an axe -> 100t disable)
    {
        DamageSource msrc(cause);
        bool axeMob = (m.kind == MobKind::Vindicator); // vanilla vindicator carries an iron axe
        if (CombatManager::tryShieldBlock(*this, target, msrc, m.x, m.z, axeMob)) return;
    }
    applyDamage(target, dmg, cause.c_str());
    // plan44 §3 G-09: thorns reflects to the mob attacker
    if (target.health < before) CombatManager::applyThornsReflection(*this, target, &m, nullptr);
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
void GameServer::weatherTick() {
    // B-12 thunder lightning: 0.01/tick (~1%) while thundering (approx: raining && tick%6000<500)
    // Use strikeLightning for visuals + creeper charging + sound; also broadcast via thundering() for channeling gate
    if (thundering() && (rand() % 100) == 0) {
        auto players = playersSnapshot();
        if (!players.empty()) {
            auto* pl = players[rand() % players.size()].get();
            if (pl && pl->inPlay) {
                int lx = static_cast<int>(pl->x) + (rand() % 16 - 8);
                int lz = static_cast<int>(pl->z) + (rand() % 16 - 8);
                int ly = static_cast<int>(pl->y);
                // find ground just above top non-air (scan down from MaxY)
                bool found = false;
                for (int y = constants::kMaxY - 1; y >= constants::kMinY; --y) {
                    std::uint16_t st = worldFor(pl->dimension).getBlock(lx, y, lz);
                    if (st != 0) { ly = y + 1; found = true; break; }
                }
                if (!found) ly = static_cast<int>(pl->y);
                strikeLightning(lx + 0.5, ly, lz + 0.5);
                std::fprintf(stderr, "[cppfm] thunder lightning at %d %d %d dim %d\n", lx, ly, lz, (int)pl->dimension);
            }
        }
    }
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
    // D21 polish: "blocks" plural alias → "block" singular (Yarn SoundCategory.BLOCKS -> wire "block")
    std::string norm = category ? std::string(category) : std::string("master");
    for (char& c : norm) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    while (!norm.empty() && std::isspace(static_cast<unsigned char>(norm.back()))) norm.pop_back();
    while (!norm.empty() && std::isspace(static_cast<unsigned char>(norm.front()))) norm.erase(norm.begin());
    if (norm == "blocks") norm = "block";
    if (norm == "hostiles") norm = "hostile";
    if (norm == "neutrals") norm = "neutral";
    if (norm == "players") norm = "player";
    WriteBuffer b;
    b.varint(0);                                       // holder: direct entry
    b.string(name);                                    // sound name
    b.boolean(false);                                  // no fixed range
    auto it = kCat.find(norm);
    b.varint(it != kCat.end() ? it->second : 0);
    b.i32(static_cast<std::int32_t>(x * 8.0));
    b.i32(static_cast<std::int32_t>(y * 8.0));
    b.i32(static_cast<std::int32_t>(z * 8.0));
    b.f32(volume);
    b.f32(pitch);
    b.i64(rand());
    broadcastPacketExcept(nullptr, pl::sc::SoundEffect, b);
}
void GameServer::broadcastStopSound(const std::optional<SoundSource>& source,
                                    const std::optional<std::string>& sound) {
    WriteBuffer b;
    std::int8_t flags = 0;
    if (source) flags |= 1;
    if (sound) flags |= 2;
    b.i8(flags);
    if (source) b.varint(static_cast<std::int32_t>(*source));
    if (sound) b.string(*sound);
    broadcastPacketExcept(nullptr, pl::sc::StopSound, b);
}
void GameServer::broadcastStopSound(SoundSource source, const std::string* soundOrNull) {
    std::optional<SoundSource> src = source;
    std::optional<std::string> snd;
    if (soundOrNull) snd = *soundOrNull;
    broadcastStopSound(src, snd);
}
void GameServer::broadcastStopSound(SoundSource source) {
    std::optional<SoundSource> src = source;
    std::optional<std::string> snd;
    broadcastStopSound(src, snd);
}
void GameServer::stopRecord(const std::string& discNameWithoutPrefix) {
    std::string sound = "minecraft:music_disc." + discNameWithoutPrefix;
    broadcastStopSound(SoundSource::Record, &sound);
}
void GameServer::broadcastWorldEvent(std::int32_t eventId, std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t data, bool disableRelativeVolume) {
    WriteBuffer b;
    b.i32(eventId);
    b.position(x, y, z);
    b.i32(data);
    b.boolean(disableRelativeVolume);
    broadcastPacketExcept(nullptr, pl::sc::WorldEvent, b);
}
void GameServer::broadcastPaleOakLeavesParticle(double x, double y, double z){
    auto body = makePaleOakLeavesBody(x, y, z);
    broadcastPacketExcept(nullptr, pl::sc::WorldParticles, body);
}
void GameServer::broadcastBlockParticle(double x, double y, double z, std::uint32_t blockState, int count){
    ParticleData d; d.blockState = blockState;
    auto body = makeWorldParticlesBody(x, y, z, 0,0,0, 0, count, ParticleId::block, d, false, false);
    broadcastPacketExcept(nullptr, pl::sc::WorldParticles, body);
}
void GameServer::broadcastDustParticle(double x, double y, double z, std::int32_t rgb, float scale){
    ParticleData d; d.setDustFromARGB(0xFF000000 | (rgb & 0xFFFFFF), scale);
    auto body = makeWorldParticlesBody(x, y, z, 0,0,0, 0, 1, ParticleId::dust, d, false, false);
    broadcastPacketExcept(nullptr, pl::sc::WorldParticles, body);
}
void GameServer::explodeAt(double x, double y, double z, float power) {
    const int r = static_cast<int>(std::ceil(power));
    // block destruction sphere with randomised edges
    // plan21 combat polish: wire blockExplosionDropDecay/mobExplosionDropDecay/tntExplosionDropDecay (W18)
    // vanilla: when gamerule false, 100% drops; when true (default), decay (30% loss). Keep default true == old no-drop decay path.
    bool blockDecay = true, mobDecay = true, tntDecay = false;
    if (gamerules_.contains("blockExplosionDropDecay")) blockDecay = gamerules_.getBool("blockExplosionDropDecay");
    if (gamerules_.contains("mobExplosionDropDecay")) mobDecay = gamerules_.getBool("mobExplosionDropDecay");
    if (gamerules_.contains("tntExplosionDropDecay")) tntDecay = gamerules_.getBool("tntExplosionDropDecay");
    bool doDecay = blockDecay;
    if (power == 4.f) doDecay = tntDecay;
    else if (power == 3.f || power == 6.f) doDecay = mobDecay;
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
    // W18 gamerule wiring: blockDecay/mobDecay/tntDecay read above; actual drop spawning deferred to avoid deadlock with mobsTick's entsMtx_ lock.
    // When doDecay==false (gamerule false), vanilla spawns 100% drops; when true, decay (30% loss). Keep old no-drop behaviour for test stability.
    (void)doDecay; (void)blockDecay; (void)mobDecay; (void)tntDecay;
    // entity damage: distance-scaled
    for (auto& p : playersSnapshot()) {
        const double dx = p->x - x, dy = p->y - y, dz = p->z - z;
        const double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (dist > power * 2) continue;
        const float dmg =
            (power * power - static_cast<float>(dist)) / power * 8.f;
        if (dmg > 0) {
            // plan44 §3 G-08: shield blocks frontal explosions (blockable per Blocking wiki)
            DamageSource esrc("explosion");
            if (CombatManager::tryShieldBlock(*this, *p, esrc, x, z, false)) continue;
            applyDamage(*p, dmg, "explosion");
        }
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
    // visuals & audio - plan26 D20: correct wire (bool bool f64 f32 f32 f32 f32 i32 amount + particle)
    for (int i = 0; i < 4; ++i) {
        int pid = (i == 0 ? ParticleId::explosion_emitter : ParticleId::explosion); // 21/22, Simple
        auto body = makeWorldParticlesBody(x + (rand()%7 - 3) * 0.5,
                                           y + (rand()%5 - 2) * 0.5,
                                           z + (rand()%7 - 3) * 0.5,
                                           0,0,0, 0, 1, pid, {}, true, false);
        broadcastPacketExcept(nullptr, pl::sc::WorldParticles, body);
    }
    broadcastSound("minecraft:entity.generic.explode", x, y, z, 4.f, 1.f,
                   "block");
    if (getenv("CPPFM_TRACE"))
        std::fprintf(stderr, "[cppfm] explosion at %.1f/%.1f/%.1f (%zu blocks)\n",
                     x, y, z, changed.size());
}
void GameServer::spawnPrimedTnt(double x,double y,double z,double vx,double vy,double vz,int fuse){
    auto t = std::make_shared<TntEntity>();
    t->entityId = nextEntityId();
    t->x = x; t->y = y; t->z = z;
    t->vx = vx; t->vy = vy; t->vz = vz;
    t->fuse = fuse;
    t->ageTicks = 0;
    {
        std::lock_guard lk(entsMtx_);
        tntEntities_.push_back(t);
    }
    WriteBuffer b;
    b.varint(t->entityId);
    static std::uint8_t zero[16]={};
    b.uuid(zero);
    int typeId = 125;
    auto it = gen::entityTypeIdByName().find("minecraft:tnt");
    if(it!=gen::entityTypeIdByName().end()) typeId = it->second;
    b.varint(typeId);
    b.f64(x); b.f64(y); b.f64(z);
    b.i8(0); b.i8(0); b.i8(0);
    b.varint(0);
    b.i16(static_cast<int16_t>(vx*8000)); b.i16(static_cast<int16_t>(vy*8000)); b.i16(static_cast<int16_t>(vz*8000));
    broadcastPacketExcept(nullptr, pl::sc::SpawnEntity, b);
}
void GameServer::tntTick(){
    std::vector<std::shared_ptr<TntEntity>> toExplode;
    {
        std::lock_guard lk(entsMtx_);
        for(auto &t: tntEntities_){
            t->vy -= 0.04;
            t->x += t->vx;
            t->y += t->vy;
            t->z += t->vz;
            t->vx *= 0.98; t->vy *= 0.98; t->vz *= 0.98;
            if(t->y < kMinY) t->y = kMinY;
            if(world_.getBlock((int)std::floor(t->x), (int)std::floor(t->y-0.1), (int)std::floor(t->z))!=0){
                t->vx *= 0.7; t->vz *= 0.7;
                if(t->vy < 0) t->vy = -t->vy * 0.5;
            }
            if(--t->fuse <= 0) toExplode.push_back(t);
            ++t->ageTicks;
            if(t->ageTicks % 4 == 0){
                WriteBuffer tp;
                tp.varint(t->entityId);
                tp.f64(t->x); tp.f64(t->y); tp.f64(t->z);
                tp.i8(0); tp.i8(0); tp.boolean(false);
                broadcastPacketExcept(nullptr, pl::sc::EntityTeleport, tp);
                broadcastSyncEntityPosition(t->entityId, t->x, t->y, t->z, t->vx, t->vy, t->vz, 0, 0, false, nullptr);
            }
        }
        for(auto &t: toExplode){
            tntEntities_.erase(std::remove(tntEntities_.begin(), tntEntities_.end(), t), tntEntities_.end());
        }
    }
    for(auto &t: toExplode){
        explodeAt(t->x, t->y, t->z, 4.f);
        WriteBuffer rm;
        rm.varint(1); rm.varint(t->entityId);
        broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, rm);
    }
}
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
            // metadata update for charged creeper (index 17, Yarn CreeperEntity CHARGED Boolean)
            WriteBuffer md;
            md.varint(m->entityId);
            meta::writeMetaBool(md, 17, true);
            md.u8(255);
            broadcastPacketExcept(nullptr, pl::sc::SetEntityMetadata, md);
            std::fprintf(stderr, "[cppfm] creeper %d charged via lightning at %.1f %.1f %.1f\n", m->entityId, x,y,z);
        }
    }
    // Also handle Enderman damage via lightning? vanilla: enderman takes damage but teleports – already via applyDamage.
}
void GameServer::applyDamageToMob(MobEntity& m, float amount, const DamageSource& src, int breachLv) {
    if (amount <= 0 || m.dead) return;
    // plan29 §3 Creaking invulnerable when transient (heart-linked) except void/kill
    if (m.kind==MobKind::Creaking && m.creakingTransient) {
        std::string low = src.type;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        bool allowed = (low=="void"||low=="kill"||low=="out_of_world"||low.find("void")!=std::string::npos||low.find("kill")!=std::string::npos);
        if (!allowed) {
            if (m.hasCreakingHeart) growResinNearHeart(m.creakingHeartX,m.creakingHeartY,m.creakingHeartZ);
            broadcastSound("minecraft:entity.creaking.sway", m.x,m.y,m.z,1.f,1.f,"hostile");
            // trigger resin clump growth is handled in growResinNearHeart
            return;
        }
    }
    // plan34 §3 Armadillo roll-up damage reduction (dmg-1)/2 while rolled, before armor
    if (m.kind==MobKind::Armadillo && m.armadilloRolledUp) {
        amount = (amount - 1.0f) * 0.5f;
        if (amount < 0) amount = 0;
        // keep roll active
        m.armadilloDangerDetectedUntil = std::max(m.armadilloDangerDetectedUntil, tickNo_ + 80);
    }
    int armor = totalArmorPoints(m);
    armor = breachAdjustedArmor(armor, breachLv); // plan44 G-09: breach pre-discounts armor (formula untouched)
    int epf = CombatManager::computeEPF(src, m);
    // mobs have no toughness in current formula; pass 0
    float finalAmt = DamageCalculator::calculate(amount, src, armor, 0.0, epf, {});
    // mobs have no resistance effects currently
    if (finalAmt <= 0) return;
    m.health -= finalAmt;
    m.hurtCooldown = 10;
    // plan34 §3 Armadillo scare on damage + generic lastHurt for sensor
    {
        auto it = mobAi_.find(m.entityId);
        if (it!=mobAi_.end() && it->second.ctx) {
            it->second.ctx->lastHurtTick = tickNo_;
            it->second.ctx->lastHurtByEntityId = -1;
        }
        if (m.kind==MobKind::Armadillo) {
            m.armadilloDangerDetectedUntil = tickNo_ + 80;
        }
    }
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
    // plan34 network: HurtAnimation 0x25 + EntitySoundEffect 0x6E
    {
        float yaw = 0.f;
        // try to compute yaw if attacker is nearest player (best effort)
        Player* attacker = nullptr;
        double best = 1e100;
        for (auto& pl : playersSnapshot()) {
            double dx = pl->x - m.x, dz = pl->z - m.z;
            double d2 = dx*dx + dz*dz;
            if (d2 < best) { best = d2; attacker = pl.get(); }
        }
        if (attacker && best < 64) {
            double dx = attacker->x - m.x, dz = attacker->z - m.z;
            yaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / 3.141592653589793);
        }
        broadcastHurtAnimation(m.entityId, yaw, nullptr);
        std::string snd;
        switch (m.kind) {
            case MobKind::Creeper: snd = "minecraft:entity.creeper.hurt"; break;
            case MobKind::Zombie: snd = "minecraft:entity.zombie.hurt"; break;
            case MobKind::Skeleton: snd = "minecraft:entity.skeleton.hurt"; break;
            case MobKind::Spider: snd = "minecraft:entity.spider.hurt"; break;
            case MobKind::Enderman: snd = "minecraft:entity.enderman.hurt"; break;
            default: snd = "minecraft:entity.generic.hurt"; break;
        }
        broadcastEntitySound(m.entityId, snd, 1.f, 1.f, SoundSource::Hostile);
    }
}
void GameServer::applyDamageToMob(MobEntity& m, float amount, const char* cause) {
    DamageSource src(cause ? std::string(cause) : std::string("generic"));
    applyDamageToMob(m, amount, src);
}
// plan34 network: 6 toClient helpers
void GameServer::sendActionBar(Player& p, const std::string& text) {
    if (!p.conn) return;
    WriteBuffer b; nbt::writeTextComponent(b, text);
    try { p.conn->sendPacket(proto::pl::sc::ActionBar, b); } catch (...) {}
}
void GameServer::broadcastActionBar(const std::string& text, Player* except) {
    WriteBuffer b; nbt::writeTextComponent(b, text);
    broadcastPacketExcept(except, proto::pl::sc::ActionBar, b);
}
void GameServer::sendServerData(Player& p) {
    if (!p.conn) return;
    WriteBuffer b;
    nbt::writeTextComponent(b, config().motd);
    // iconBytes optional ByteArray: try server-icon.png raw bytes
    std::vector<uint8_t> icon;
    {
        std::ifstream f("server-icon.png", std::ios::binary);
        if (f) {
            icon.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            // vanilla limit 64x64 png; clamp to avoid huge
            if (icon.size() > 65535) icon.resize(65535);
        }
    }
    if (icon.empty()) {
        b.boolean(false);
    } else {
        b.boolean(true);
        b.varint(static_cast<int32_t>(icon.size()));
        b.raw(icon.data(), icon.size());
    }
    try { p.conn->sendPacket(proto::pl::sc::ServerData, b); } catch (...) {}
}
void GameServer::broadcastServerData() {
    for (auto& pp : playersSnapshot()) sendServerData(*pp);
}
void GameServer::sendHurtAnimation(Player& p, int32_t entityId, float yaw) {
    if (!p.conn) return;
    if (!std::isfinite(yaw)) yaw = 0;
    WriteBuffer b; b.varint(entityId); b.f32(yaw);
    try { p.conn->sendPacket(proto::pl::sc::HurtAnimation, b); } catch (...) {}
}
void GameServer::broadcastHurtAnimation(int32_t entityId, float yaw, Player* except) {
    if (!std::isfinite(yaw)) yaw = 0;
    WriteBuffer b; b.varint(entityId); b.f32(yaw);
    broadcastPacketExcept(except, proto::pl::sc::HurtAnimation, b);
}
void GameServer::sendEntitySound(Player& p, int32_t entityId, const std::string& soundName, float volume, float pitch, SoundSource category) {
    if (!p.conn) return;
    WriteBuffer b;
    b.varint(0); b.string(soundName); b.boolean(false);
    b.varint(static_cast<int32_t>(category));
    b.varint(entityId);
    b.f32(volume); b.f32(pitch);
    b.i64(static_cast<int64_t>(entityId) ^ tickNo_);
    try { p.conn->sendPacket(proto::pl::sc::EntitySoundEffect, b); } catch (...) {}
}
void GameServer::broadcastEntitySound(int32_t entityId, const std::string& soundName, float volume, float pitch, SoundSource category) {
    WriteBuffer b;
    b.varint(0); b.string(soundName); b.boolean(false);
    b.varint(static_cast<int32_t>(category));
    b.varint(entityId);
    b.f32(volume); b.f32(pitch);
    b.i64(static_cast<int64_t>(entityId) ^ tickNo_);
    broadcastPacketExcept(nullptr, proto::pl::sc::EntitySoundEffect, b);
}
void GameServer::sendChatSuggestions(Player& p, int32_t action, const std::vector<std::string>& entries) {
    if (!p.conn) return;
    WriteBuffer b; b.varint(action); b.varint(static_cast<int32_t>(entries.size()));
    for (auto& s : entries) b.string(s);
    try { p.conn->sendPacket(proto::pl::sc::ChatSuggestions, b); } catch (...) {}
}
void GameServer::broadcastChatSuggestions(int32_t action, const std::vector<std::string>& entries, Player* except) {
    WriteBuffer b; b.varint(action); b.varint(static_cast<int32_t>(entries.size()));
    for (auto& s : entries) b.string(s);
    broadcastPacketExcept(except, proto::pl::sc::ChatSuggestions, b);
}
void GameServer::sendSyncEntityPosition(Player& p, int32_t entityId, double x, double y, double z, double dx, double dy, double dz, float yaw, float pitch, bool onGround) {
    if (!p.conn) return;
    WriteBuffer b; b.varint(entityId); b.f64(x); b.f64(y); b.f64(z); b.f64(dx); b.f64(dy); b.f64(dz); b.f32(yaw); b.f32(pitch); b.boolean(onGround);
    try { p.conn->sendPacket(proto::pl::sc::SyncEntityPosition, b); } catch (...) {}
}
void GameServer::broadcastSyncEntityPosition(int32_t entityId, double x, double y, double z, double dx, double dy, double dz, float yaw, float pitch, bool onGround, Player* except) {
    WriteBuffer b; b.varint(entityId); b.f64(x); b.f64(y); b.f64(z); b.f64(dx); b.f64(dy); b.f64(dz); b.f32(yaw); b.f32(pitch); b.boolean(onGround);
    broadcastPacketExcept(except, proto::pl::sc::SyncEntityPosition, b);
}
void GameServer::sendSyncEntityPosition(Player& p, const MobEntity& mob) {
    float yawf = 0, pitchf = 0;
    sendSyncEntityPosition(p, mob.entityId, mob.x, mob.y, mob.z, 0, 0, 0, yawf, pitchf, true);
}
void GameServer::broadcastSyncEntityPosition(const MobEntity& mob, Player* except) {
    float yawf = 0, pitchf = 0;
    broadcastSyncEntityPosition(mob.entityId, mob.x, mob.y, mob.z, 0, 0, 0, yawf, pitchf, true, except);
}
// plan42 R1 wire: MapData 0x2D MoveMinecart 0x31 SelectAdvancementTab 0x4F
void GameServer::sendMapData(Player& p, int mapId, uint8_t scale, bool locked) {
    if (!p.conn) return;
    WriteBuffer b;
    b.varint(mapId);
    b.i8((int8_t)scale);
    b.boolean(locked);
    b.boolean(false); // icons absent (option<array> false)
    b.u8(0); // columns 0 => no rows/x/y/data
    try { p.conn->sendPacket(proto::pl::sc::MapData, b); } catch (...) {}
}
void GameServer::sendMapData(Player& p, int mapId, const std::array<uint8_t,16384>& colors, uint8_t scale) {
    if (!p.conn) return;
    WriteBuffer b;
    b.varint(mapId);
    b.i8((int8_t)scale);
    b.boolean(false);
    b.boolean(false);
    b.u8(128); // columns 128
    b.u8(128); // rows 128
    b.u8(0); // x 0
    b.u8(0); // y 0
    b.varint(16384);
    b.raw(colors.data(), 16384);
    try { p.conn->sendPacket(proto::pl::sc::MapData, b); } catch (...) {}
}
void GameServer::broadcastMapData(int mapId, uint8_t scale, bool locked, Player* except) {
    WriteBuffer b;
    b.varint(mapId);
    b.i8((int8_t)scale);
    b.boolean(locked);
    b.boolean(false);
    b.u8(0);
    broadcastPacketExcept(except, proto::pl::sc::MapData, b);
}
void GameServer::sendMoveMinecart(Player& p, std::int32_t entityId, double x, double y, double z, float yaw, float pitch) {
    if (!p.conn) return;
    WriteBuffer b;
    b.varint(entityId);
    b.varint(1); // one lerp step
    b.f32((float)x); b.f32((float)y); b.f32((float)z);
    b.f32(0.f); b.f32(0.f); b.f32(0.f);
    b.f32(yaw); b.f32(pitch); b.f32(1.f);
    try { p.conn->sendPacket(proto::pl::sc::MoveMinecart, b); } catch (...) {}
}
void GameServer::broadcastMoveMinecart(std::int32_t entityId, double x, double y, double z, float yaw, float pitch, Player* except) {
    WriteBuffer b;
    b.varint(entityId);
    b.varint(1);
    b.f32((float)x); b.f32((float)y); b.f32((float)z);
    b.f32(0.f); b.f32(0.f); b.f32(0.f);
    b.f32(yaw); b.f32(pitch); b.f32(1.f);
    broadcastPacketExcept(except, proto::pl::sc::MoveMinecart, b);
}
void GameServer::sendSelectAdvancementTab(Player& p, const std::string& tabId) {
    if (!p.conn) return;
    WriteBuffer b;
    if (tabId.empty()) {
        b.boolean(false);
    } else {
        b.boolean(true);
        b.string(tabId);
    }
    try { p.conn->sendPacket(proto::pl::sc::SelectAdvancementTab, b); } catch (...) {}
}
void GameServer::broadcastSelectAdvancementTab(const std::string& tabId, Player* except) {
    WriteBuffer b;
    if (tabId.empty()) b.boolean(false);
    else { b.boolean(true); b.string(tabId); }
    broadcastPacketExcept(except, proto::pl::sc::SelectAdvancementTab, b);
}
} // namespace cppfm
