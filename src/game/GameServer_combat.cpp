#include "GameServer.hpp"
#include "Messages.hpp"
#include "MetadataTypes.hpp"
#include "Constants.hpp"
#include "../generated/EntityIds.hpp"
#include "DamageComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "CombatManager.hpp"
#include "Particles.hpp"

namespace cppfm {
using namespace proto;

namespace {

struct PlayerCombatSnapshot {
    std::shared_ptr<Connection> connection;
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    bool inPlay = false;
    bool dead = false;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float health = 0.0f;
    std::int32_t food = 0;
    float saturation = 0.0f;
};

PlayerCombatSnapshot snapshotPlayerCombat(const Player& player) {
    std::lock_guard playerLock(player.stateMtx);
    return {player.conn, player.entityId, player.dimension, player.inPlay,
            player.dead, player.x, player.y, player.z, player.health,
            player.food, player.saturation};
}

std::shared_ptr<Connection> snapshotPlayerConnection(const Player& player) {
    std::lock_guard playerLock(player.stateMtx);
    return player.conn;
}

struct MobCombatSnapshot {
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    MobKind kind = MobKind::Pig;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    bool dead = false;
};

MobCombatSnapshot snapshotMobCombat(const MobEntity& mob) {
    std::lock_guard entityLock(*mob.stateMtx);
    return {mob.entityId, mob.dimension, mob.kind, mob.x, mob.y, mob.z,
            mob.dead};
}

} // namespace

void GameServer::syncPlayerArmorAttributes(Player& p) {
    CombatManager::syncPlayerArmor(*this, p);
}
void GameServer::applyDamage(Player& p, float amount, const DamageSource& src, int breachLv) {
    if (mobStateLockOwnedByCurrentThread()) {
        const DamageSource source = src;
        runWithoutMobStateLock([this, &p, amount, source, breachLv] {
            applyDamage(p, amount, source, breachLv);
        });
        return;
    }
    if (amount <= 0) return;

    // The JVM event is user code and may synchronously call back into the
    // native bridge (or queue a server-thread mutation that waits for this
    // call).  Never hold the player state lock while invoking it.  The second
    // guarded check below closes the re-entry window: a callback may have
    // killed the player or changed its game mode while deciding the damage.
    {
        std::lock_guard playerLock(p.stateMtx);
        if (p.gamemode == 1 || p.gamemode == 3 || p.dead || p.health <= 0)
            return;
    }
    if (jvmRuntime_ && !jvmRuntime_->onEntityDamage(&p, nullptr, amount, src.type)) return;

    // Armor synchronization has its own short player-state critical section.
    // Keep it outside this function's mutation lock: the helper may emit an
    // attribute packet and must not be nested inside the damage transaction.
    syncPlayerArmorAttributes(p);

    PlayerCombatSnapshot result;
    bool shouldKill = false;
    {
        std::lock_guard playerLock(p.stateMtx);
        // The callback and armor sync both leave a re-entry window.  Do not
        // mutate a player that became dead or switched to an invulnerable
        // game mode while either operation was running.
        if (p.gamemode == 1 || p.gamemode == 3 || p.dead || p.health <= 0)
            return;
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
        if (p.health <= 0) {
            p.health = 0;
            shouldKill = true;
        }
        result = snapshotPlayerCombat(p);
    }

    // Death bookkeeping, drops, callbacks, and network I/O all happen after
    // the player state lock is released.  health <= 0 above prevents another
    // damage transaction from overtaking killPlayer during this hand-off.
    if (shouldKill) killPlayer(p, src.type.c_str());
    if (result.connection) {
        WriteBuffer health;
        health.f32(result.health);
        health.varint(result.food);
        health.f32(result.saturation);
        result.connection->trySendPacket(pl::sc::SetHealth, health);

        WriteBuffer de;
        de.varint(result.entityId);
        int dtid = gameData_.idOf("minecraft:damage_type", std::string("minecraft:") + src.type);
        if (dtid < 0) dtid = gameData_.idOf("minecraft:damage_type", "minecraft:generic");
        if (dtid < 0) dtid = 0;
        de.varint(dtid >= 0 ? dtid : 0);
        de.varint(0); de.varint(0);
        de.boolean(false);
        result.connection->trySendPacket(pl::sc::DamageEvent, de);
        broadcastPacketExceptInDimension(result.dimension, &p,
                                         pl::sc::DamageEvent, de);
    }
    {
        float yaw = 0.f;
        broadcastHurtAnimationFor(result.dimension, result.entityId, yaw, nullptr);
        std::string snd = "minecraft:entity.player.hurt";
        broadcastEntitySoundFor(result.dimension, result.entityId, snd, 1.f, 1.f,
                                SoundSource::Player);
    }
}
void GameServer::applyDamage(Player& p, float amount, const char* cause) {
    DamageSource src(cause ? std::string(cause) : std::string("generic"));
    applyDamage(p, amount, src);
}
void GameServer::killPlayer(Player& p, const char* cause) {
    const std::string causeText = cause ? cause : "generic";
    bool keepInventory = false;
    try { keepInventory = gamerules_.getBool("keepInventory"); } catch (...) {}

    std::string playerName;
    std::int8_t dimension = 0;
    double x = 0.0, y = 0.0, z = 0.0;
    std::shared_ptr<Connection> connection;
    std::vector<ItemStack> drops;
    bool inventoryChanged = false;
    const bool hardcore = cfg_.hardcore;
    {
        std::lock_guard playerLock(p.stateMtx);
        if (p.dead) return;
        p.dead = true;
        playerName = p.name;
        dimension = p.dimension;
        x = p.x;
        y = p.y;
        z = p.z;
        connection = p.conn;
        if (p.stats) p.stats->add("minecraft:custom|minecraft:deaths");

        if (!keepInventory) {
            drops.reserve(46);
            for (auto& st : p.inv) {
                if (st.empty()) continue;
                inventoryChanged = true;
                if (EnchantmentHelper::hasVanishingCurse(st)) {
                    st = ItemStack::air();
                    continue;
                }
                // Copy while protected, then clear the authoritative slot.
                // Actual entity creation is deliberately deferred below.
                drops.push_back(st);
                st = ItemStack::air();
            }
        } else {
            // Vanishing still applies with keepInventory enabled.
            for (auto& st : p.inv) {
                if (!st.empty() && EnchantmentHelper::hasVanishingCurse(st)) {
                    st = ItemStack::air();
                    inventoryChanged = true;
                }
            }
        }
        if (hardcore) p.gamemode = 3; // spectator; publish under the same lock
    }

    // Everything below can re-enter the server or block on transport/storage,
    // so it intentionally runs after the player model lock is released.
    scoreboard.addScore("deaths", playerName, 1);
    sendScoreAll("deaths", playerName,
                 scoreboard.getScore("deaths", playerName));
    broadcastSystemText((msg::kRed + playerName + " died (" + causeText + ")"),
                         &p);
    for (const auto& stack : drops) {
        spawnItemDropFor(dimension, x, y + 0.5, z, stack,
                         (nextRandom() / (double)RAND_MAX - .5) * 0.3,
                         0.2,
                         (nextRandom() / (double)RAND_MAX - .5) * 0.3);
    }
    if (inventoryChanged && connection) resendInventory(p);

    if (hardcore) {
        bannedPlayers_.insert(playerName);
        try { saveBans(); } catch (...) {}
        if (connection) {
            WriteBuffer b;
            nbt::writeTextComponent(b, "You died in hardcore mode and are banned");
            connection->trySendPacket(proto::pl::sc::Disconnect, b);
            connection->close();
        }
    }
}
void GameServer::mobAttackPlayer(MobEntity& m, Player& target) {
    if (mobStateLockOwnedByCurrentThread()) {
        runWithoutMobStateLock([this, &m, &target] {
            mobAttackPlayer(m, target);
        });
        return;
    }
    std::int8_t targetDimension = 0;
    std::int32_t targetEntityId = 0;
    float before = 0.f;
    {
        std::lock_guard playerLock(target.stateMtx);
        targetDimension = target.dimension;
        targetEntityId = target.entityId;
        before = target.health;
    }
    std::int8_t mobDimension = 0;
    MobKind mobKind = MobKind::Pig;
    double mobX = 0.0, mobZ = 0.0;
    std::int32_t mobEntityId = 0;
    {
        std::lock_guard entityLock(*m.stateMtx);
        mobDimension = m.dimension;
        mobKind = m.kind;
        mobX = m.x;
        mobZ = m.z;
        mobEntityId = m.entityId;
    }
    if (canonicalDimension(mobDimension) != canonicalDimension(targetDimension))
        return;
    float dmg = mobStats(mobKind).attackDamage;
    if (mobKind==MobKind::Creaking) {
        if (difficulty_=="easy") dmg=2.5f;
        else if (difficulty_=="hard") dmg=4.5f;
        else dmg=3.0f;
    }
    if (dmg <= 0) return;
    std::string cause = MobEntity::kindName(mobKind);   // e.g. minecraft:zombie
    const auto slash = cause.find(':');
    if (slash != std::string::npos) cause = cause.substr(slash + 1);
    {
        DamageSource msrc(cause);
        bool axeMob = (mobKind == MobKind::Vindicator); // vanilla vindicator carries an iron axe
        if (CombatManager::tryShieldBlock(*this, target, msrc, mobX, mobZ, axeMob)) return;
    }
    applyDamage(target, dmg, cause.c_str());
    bool damaged = false;
    {
        std::lock_guard playerLock(target.stateMtx);
        damaged = target.health < before;
    }
    if (damaged) CombatManager::applyThornsReflection(*this, target, &m, nullptr);
    if (damaged) {
        std::lock_guard entityLock(*m.stateMtx);
        if (m.entityId == mobEntityId) m.angerTargetEntityId = targetEntityId;
    }
}
bool GameServer::tryBreedFeed(Player& p, MobEntity& m) {
    std::int8_t dimension = 0;
    std::int32_t entityId = 0;
    bool consumed = false;
    {
        std::scoped_lock stateLock(p.stateMtx, *m.stateMtx);
        if (canonicalDimension(p.dimension) != canonicalDimension(m.dimension))
            return false;
        const auto foodId = MobEntity::breedingItemFor(m.kind);
        if (!foodId || MobEntity::isBaby(m)) return false;
        // consume one breeding item from hotbar/main inv
        for (auto& s : p.inv) {
            if (s.itemId != foodId || s.count <= 0) continue;
            if (--s.count <= 0) s = ItemStack::air();
            m.inLove = true;
            m.loveUntilTick = tickNoForTest() + 30 * 20;
            dimension = m.dimension;
            entityId = m.entityId;
            consumed = true;
            break;
        }
    }
    if (!consumed) return false;
    // Network and extension-facing work stays outside both model locks.
    resendInventory(p);
    WriteBuffer st;
    st.i32(entityId); st.i8(18);
    broadcastPacketExceptInDimension(dimension, nullptr,
                                     pl::sc::EntityEvent, st);
    return true;
}
void GameServer::weatherTick() {
    // B-12 thunder lightning: 0.01/tick (~1%) while thundering (approx: raining && tick%6000<500)
    // Weather is currently modelled as the Overworld's shared weather state;
    // never select a Nether/End player as a lightning target.
    if (thundering() && (nextRandom() % 100) == 0) {
        struct WeatherTarget {
            std::int8_t dimension = 0;
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
        };
        std::vector<WeatherTarget> overworldPlayers;
        for (const auto& player : playersSnapshot()) {
            if (!player) continue;
            const auto state = snapshotPlayerCombat(*player);
            if (state.inPlay && canonicalDimension(state.dimension) == 0)
                overworldPlayers.push_back(
                    {state.dimension, state.x, state.y, state.z});
        }
        if (!overworldPlayers.empty()) {
            const auto& target =
                overworldPlayers[nextRandom() % overworldPlayers.size()];
            int lx = static_cast<int>(target.x) + (nextRandom() % 16 - 8);
            int lz = static_cast<int>(target.z) + (nextRandom() % 16 - 8);
            int ly = static_cast<int>(target.y);
            // find ground just above top non-air (scan down from MaxY)
            bool found = false;
            for (int scanY = kMaxY - 1; scanY >= kMinY; --scanY) {
                std::uint16_t st = worldFor(target.dimension).getBlock(lx, scanY, lz);
                if (st != 0) { ly = scanY + 1; found = true; break; }
            }
            if (!found) ly = static_cast<int>(target.y);
            strikeLightningFor(0, lx + 0.5, ly, lz + 0.5);
            std::fprintf(stderr, "[cppfm] thunder lightning at %d %d %d dim 0\n", lx, ly, lz);
        }
    }
    if (!gamerules_.getBool("doWeatherCycle")) return;
    if (tickNo_ < weatherUntilTick_) return;
    setWeather(raining() ? Weather::Clear : Weather::Rain,
               (6000 + nextRandom() % 24000) * 20LL);
}
void GameServer::setWeather(Weather w, std::int64_t durationTicks) {
    if (w == weather_) return;
    weather_ = w;
    WriteBuffer b;
    b.u8(w == Weather::Rain ? 2 : 1);                 // begin/end raining
    b.f32(0.f);
    // Nether and End do not have Overworld rain/clear visual events.
    broadcastPacketExceptInDimension(0, nullptr, pl::sc::GameEvent, b);
    weatherUntilTick_ = tickNo_ + durationTicks;
}
void GameServer::broadcastSound(const char* name, double x, double y,
                                double z, float volume, float pitch,
                                const char* category) {
    const std::int8_t dimension =
        brainTickGuard_ ? snapshotMobCombat(*brainTickGuard_).dimension : 0;
    broadcastSoundFor(dimension, name, x, y, z, volume, pitch, category);
}
void GameServer::broadcastSoundFor(std::int8_t dimension, const char* name,
                                   double x, double y, double z, float volume,
                                   float pitch, const char* category) {
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
    b.i64(nextRandom());
    broadcastPacketExceptInDimension(dimension, nullptr, pl::sc::SoundEffect, b);
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
    const std::int8_t dimension =
        brainTickGuard_ ? snapshotMobCombat(*brainTickGuard_).dimension : 0;
    broadcastWorldEventFor(dimension, eventId, x, y, z, data,
                           disableRelativeVolume);
}
void GameServer::broadcastWorldEventFor(std::int8_t dimension,
                                        std::int32_t eventId,
                                        std::int32_t x, std::int32_t y,
                                        std::int32_t z, std::int32_t data,
                                        bool disableRelativeVolume) {
    WriteBuffer b;
    b.i32(eventId);
    b.position(x, y, z);
    b.i32(data);
    b.boolean(disableRelativeVolume);
    broadcastPacketExceptInDimension(dimension, nullptr, pl::sc::WorldEvent, b);
}
void GameServer::broadcastPaleOakLeavesParticle(double x, double y, double z){
    const std::int8_t dimension =
        brainTickGuard_ ? snapshotMobCombat(*brainTickGuard_).dimension : 0;
    broadcastPaleOakLeavesParticleFor(dimension, x, y, z);
}
void GameServer::broadcastPaleOakLeavesParticleFor(std::int8_t dimension,
                                                   double x, double y, double z){
    auto body = makePaleOakLeavesBody(x, y, z);
    broadcastPacketExceptInDimension(dimension, nullptr, pl::sc::WorldParticles, body);
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
    explodeAtFor(0, x, y, z, power);
}
void GameServer::explodeAtFor(std::int8_t dimension, double x, double y,
                              double z, float power) {
    World& world = worldFor(dimension);
    const auto targetDimension = canonicalDimension(dimension);
    const int r = static_cast<int>(std::ceil(power));
    // block destruction sphere with randomised edges
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
                const auto st = world.getBlock(bx, by, bz);
                if (st == 0) continue;
                const gen::BlockDef* def = gen::blockByState(st);
                if (def && (def->name == "minecraft:bedrock" ||
                            def->name == "minecraft:obsidian" ||
                            def->hardness < 0))
                    continue;
                world.setBlock(bx, by, bz, 0);
                broadcastBlockChangeFor(targetDimension, bx, by, bz, 0);
                changed.push_back({bx, by, bz});
            }
    // W18 gamerule wiring: blockDecay/mobDecay/tntDecay read above; actual drop spawning deferred to avoid deadlock with mobsTick's entsMtx_ lock.
    (void)doDecay; (void)blockDecay; (void)mobDecay; (void)tntDecay;
    // entity damage: distance-scaled.  Read a coherent player snapshot before
    // invoking shield/damage logic; neither operation runs while this loop
    // holds the player's state lock.
    for (auto& p : playersSnapshot()) {
        if (!p) continue;
        const auto state = snapshotPlayerCombat(*p);
        if (canonicalDimension(state.dimension) != targetDimension) continue;
        const double dx = state.x - x, dy = state.y - y, dz = state.z - z;
        const double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (dist > power * 2) continue;
        const float dmg =
            (power * power - static_cast<float>(dist)) / power * 8.f;
        if (dmg > 0) {
            DamageSource esrc("explosion");
            if (CombatManager::tryShieldBlock(*this, *p, esrc, x, z, false)) continue;
            applyDamage(*p, dmg, "explosion");
        }
        if (!state.connection) continue;
        // knockback
        const double inv = 1.0 / std::max(1.0, dist);
        WriteBuffer v;
        v.varint(state.entityId);
        v.i16(static_cast<std::int16_t>(dx * inv * 12000));
        v.i16(static_cast<std::int16_t>((dy * inv + 0.4) * 12000));
        v.i16(static_cast<std::int16_t>(dz * inv * 12000));
        state.connection->trySendPacket(pl::sc::EntityVelocity, v);
        // DamageEvent for the hurt animation/flash
        WriteBuffer de;
        de.varint(state.entityId);
        de.varint(gameData_.idOf("minecraft:damage_type",
                                 "minecraft:explosion") >= 0
                      ? gameData_.idOf("minecraft:damage_type",
                                       "minecraft:explosion")
                      : 0);
        de.varint(0); de.varint(0);
        de.boolean(false);
        state.connection->trySendPacket(pl::sc::DamageEvent, de);
    }
    struct DeferredDrop {
        double x = 0, y = 0, z = 0;
        std::uint32_t itemId = 0;
        std::uint8_t count = 0;
    };
    std::vector<DeferredDrop> drops;
    std::vector<std::shared_ptr<MobEntity>> removed;
    // Damage callbacks can re-enter the server (including Java callbacks),
    // so never hold entsMtx_ while applying damage.  The shared_ptr snapshot
    // keeps each target alive until the post-callback removal phase.
    const auto candidates = mobsSnapshot();
    std::vector<std::shared_ptr<MobEntity>> dead;
    for (const auto& m : candidates) {
        if (!m) continue;
        const auto state = snapshotMobCombat(*m);
        if (canonicalDimension(state.dimension) != targetDimension) continue;
        const double dx = state.x - x, dy = state.y - y, dz = state.z - z;
        const double dist = std::sqrt(dx*dx+dy*dy+dz*dz);
        if (dist > power * 2 || state.dead) continue;
        applyDamageToMob(*m,
            (power * power - static_cast<float>(dist)) / power * 8.f,
            "explosion");
        bool deadNow = false;
        {
            std::lock_guard mobLock(*m->stateMtx);
            deadNow = m->dead;
        }
        if (deadNow) dead.push_back(m);
    }
    {
        std::lock_guard lk(entsMtx_);
        for (const auto& m : dead) {
            const auto it = std::find(mobs_.begin(), mobs_.end(), m);
            if (it == mobs_.end()) continue;
            mobs_.erase(it);
            removed.push_back(m);
        }
    }
    for (const auto& m : removed) {
        const auto state = snapshotMobCombat(*m);
        eraseMobAi(state.entityId);
        const auto drop = MobEntity::dropFor(state.kind);
        if (drop.itemId)
            drops.push_back({state.x, state.y + .4f, state.z, drop.itemId,
                             drop.count});
        WriteBuffer rm; rm.varint(1); rm.varint(state.entityId);
        broadcastPacketExceptInDimension(state.dimension, nullptr,
                                         pl::sc::RemoveEntities, rm);
    }
    for (const auto& drop : drops)
        spawnItemDropFor(targetDimension, drop.x, drop.y, drop.z,
                         drop.itemId, drop.count);
    for (const auto& mob : removed) invalidateJvmMob(mob);
    for (int i = 0; i < 4; ++i) {
        int pid = (i == 0 ? ParticleId::explosion_emitter : ParticleId::explosion); // 21/22, Simple
        auto body = makeWorldParticlesBody(x + (nextRandom()%7 - 3) * 0.5,
                                           y + (nextRandom()%5 - 2) * 0.5,
                                           z + (nextRandom()%7 - 3) * 0.5,
                                           0,0,0, 0, 1, pid, {}, true, false);
        broadcastPacketExceptInDimension(targetDimension, nullptr,
                                         pl::sc::WorldParticles, body);
    }
    broadcastSoundFor(targetDimension, "minecraft:entity.generic.explode", x,
                      y, z, 4.f, 1.f, "block");
}
void GameServer::spawnPrimedTnt(double x,double y,double z,double vx,double vy,double vz,int fuse){
    spawnPrimedTntFor(0, x, y, z, vx, vy, vz, fuse);
}
void GameServer::spawnPrimedTntFor(std::int8_t dimension, double x, double y,
                                   double z, double vx, double vy, double vz,
                                   int fuse){
    auto t = std::make_shared<TntEntity>();
    t->entityId = nextEntityId();
    t->dimension = canonicalDimension(dimension);
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
    broadcastPacketExceptInDimension(t->dimension, nullptr,
                                     pl::sc::SpawnEntity, b);
}
void GameServer::tntTick(){
    std::vector<std::shared_ptr<TntEntity>> toExplode;
    std::vector<std::shared_ptr<TntEntity>> active;
    {
        std::lock_guard lk(entsMtx_);
        active = tntEntities_;
    }
    for (auto& t : active) {
        if (!t) continue;
        const auto dimension = canonicalDimension(t->dimension);
        World& world = worldFor(dimension);
        t->vy -= 0.04;
        t->x += t->vx;
        t->y += t->vy;
        t->z += t->vz;
        t->vx *= 0.98;
        t->vy *= 0.98;
        t->vz *= 0.98;
        if (t->y < kMinY) t->y = kMinY;
        if (world.getBlock(static_cast<int>(std::floor(t->x)),
                           static_cast<int>(std::floor(t->y - 0.1)),
                           static_cast<int>(std::floor(t->z))) != 0) {
            t->vx *= 0.7;
            t->vz *= 0.7;
            if (t->vy < 0) t->vy = -t->vy * 0.5;
        }
        if (--t->fuse <= 0) toExplode.push_back(t);
        ++t->ageTicks;
        if (t->ageTicks % 4 != 0) continue;

        WriteBuffer tp;
        tp.varint(t->entityId);
        tp.f64(t->x);
        tp.f64(t->y);
        tp.f64(t->z);
        tp.i8(0);
        tp.i8(0);
        tp.boolean(false);
        broadcastPacketExceptInDimension(dimension, nullptr,
                                         pl::sc::EntityTeleport, tp);

        WriteBuffer sync;
        sync.varint(t->entityId);
        sync.f64(t->x);
        sync.f64(t->y);
        sync.f64(t->z);
        sync.f64(t->vx);
        sync.f64(t->vy);
        sync.f64(t->vz);
        sync.f32(0);
        sync.f32(0);
        sync.boolean(false);
        broadcastPacketExceptInDimension(dimension, nullptr,
                                         pl::sc::SyncEntityPosition, sync);
    }
    if (!toExplode.empty()) {
        std::lock_guard lk(entsMtx_);
        for (const auto& t : toExplode) {
            tntEntities_.erase(
                std::remove(tntEntities_.begin(), tntEntities_.end(), t),
                tntEntities_.end());
        }
    }
    for(auto &t: toExplode){
        explodeAtFor(t->dimension, t->x, t->y, t->z, 4.f);
        WriteBuffer rm;
        rm.varint(1); rm.varint(t->entityId);
        broadcastPacketExceptInDimension(t->dimension, nullptr,
                                         pl::sc::RemoveEntities, rm);
    }
}
void GameServer::strikeLightning(double x, double y, double z) {
    const std::int8_t dimension =
        brainTickGuard_ ? snapshotMobCombat(*brainTickGuard_).dimension : 0;
    strikeLightningFor(dimension, x, y, z);
}
void GameServer::strikeLightningFor(std::int8_t dimension, double x, double y,
                                    double z) {
    if (mobStateLockOwnedByCurrentThread()) {
        runWithoutMobStateLock([this, dimension, x, y, z] {
            strikeLightningFor(dimension, x, y, z);
        });
        return;
    }
    // Visual: spawn lightning bolt entity and broadcast sound
    {
        auto bolt = std::make_shared<LightningBoltEntity>();
        bolt->entityId = nextEntityId();
        bolt->dimension = canonicalDimension(dimension);
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
        broadcastPacketExceptInDimension(bolt->dimension, nullptr,
                                         pl::sc::SpawnEntity, b);
        broadcastSoundFor(bolt->dimension,
                          "minecraft:entity.lightning_bolt.thunder", x, y, z,
                          2.f, 1.f, "weather");
        broadcastSoundFor(bolt->dimension,
                          "minecraft:entity.lightning_bolt.impact", x, y, z,
                          1.f, 1.f, "weather");
    }
    // Charge creepers within 4 blocks (includes via trident channeling).  The
    // state transition is protected, but packet construction/broadcast is
    // deliberately outside entsMtx_: a transport callback must not re-enter
    // the entity container while it is locked.
    struct ChargedCreeper {
        std::int8_t dimension = 0;
        std::int32_t entityId = 0;
    };
    std::vector<ChargedCreeper> charged;
    for (const auto& m : mobsSnapshot()) {
        if (!m) continue;
        std::lock_guard mobLock(*m->stateMtx);
        if (m->dead || m->kind != MobKind::Creeper ||
            canonicalDimension(m->dimension) !=
                canonicalDimension(dimension) || m->creeperCharged)
            continue;
        const double dx = m->x - x;
        const double dy = m->y - y;
        const double dz = m->z - z;
        if (dx * dx + dy * dy + dz * dz >= 16) continue;
        m->creeperCharged = true;
        charged.push_back({m->dimension, m->entityId});
    }
    for (const auto& creeper : charged) {
        WriteBuffer md;
        md.varint(creeper.entityId);
        // metadata update for charged creeper (index 17, Yarn CreeperEntity CHARGED Boolean)
        meta::writeMetaBool(md, 17, true);
        md.u8(255);
        broadcastPacketExceptInDimension(creeper.dimension, nullptr,
                                         pl::sc::SetEntityMetadata, md);
        std::fprintf(stderr,
                     "[cppfm] creeper %d charged via lightning at %.1f %.1f %.1f\n",
                     creeper.entityId, x, y, z);
    }
    // Also handle Enderman damage via lightning? vanilla: enderman takes damage but teleports – already via applyDamage.
}
void GameServer::applyDamageToMob(MobEntity& m, float amount, const DamageSource& src, int breachLv) {
    if (mobStateLockOwnedByCurrentThread()) {
        const DamageSource source = src;
        runWithoutMobStateLock([this, &m, amount, source, breachLv] {
            applyDamageToMob(m, amount, source, breachLv);
        });
        return;
    }
    if (amount <= 0) return;

    // Fabric callbacks are user code.  In particular, an allow-damage
    // callback may call back into the native bridge and mutate this entity.
    // Mob state is protected independently from vector ownership.  The
    // caller owns the entity lifetime (normally through a shared snapshot),
    // so taking entsMtx_ here would only add an avoidable lock-order edge.
    {
        std::lock_guard entityLock(*m.stateMtx);
        if (m.dead) return;
    }
    if (jvmRuntime_ && !jvmRuntime_->onEntityDamage(nullptr, &m, amount, src.type)) return;

    struct DamageResult {
        std::int8_t dimension = 0;
        std::int32_t entityId = 0;
        MobKind kind = MobKind::Pig;
        double x = 0, y = 0, z = 0;
        bool applied = false;
        bool creakingBlocked = false;
        bool creakingHeart = false;
        std::int32_t heartX = 0, heartY = 0, heartZ = 0;
        bool boss = false;
        MobEntity bossSnapshot;
        bool dead = false;
    } result;

    {
        // No JVM, AI, packet, or world callback is made while the field lock
        // is held; those paths may re-enter the server.  Vector membership is
        // owned by entsMtx_ and is intentionally not coupled to this lock.
        std::lock_guard entityLock(*m.stateMtx);
        if (m.dead) return;

        result.dimension = m.dimension;
        result.entityId = m.entityId;
        result.kind = m.kind;
        result.x = m.x;
        result.y = m.y;
        result.z = m.z;
        if (MobEntity::isBoss(m.kind)) {
            result.boss = true;
            // Boss callbacks run after the mob field lock is released.  Copy
            // the state while it is protected so a concurrent removal or tick
            // cannot make the callback observe a partially updated entity.
            result.bossSnapshot = m;
        }

        if (m.kind == MobKind::Creaking && m.creakingTransient) {
            std::string low = src.type;
            std::transform(low.begin(), low.end(), low.begin(), ::tolower);
            const bool allowed =
                low == "void" || low == "kill" || low == "out_of_world" ||
                low.find("void") != std::string::npos ||
                low.find("kill") != std::string::npos;
            if (!allowed) {
                result.creakingBlocked = true;
                result.creakingHeart = m.hasCreakingHeart;
                result.heartX = m.creakingHeartX;
                result.heartY = m.creakingHeartY;
                result.heartZ = m.creakingHeartZ;
            }
        }

        if (!result.creakingBlocked) {
            float effectiveAmount = amount;
            if (m.kind == MobKind::Armadillo && m.armadilloRolledUp) {
                effectiveAmount = (effectiveAmount - 1.0f) * 0.5f;
                if (effectiveAmount < 0) effectiveAmount = 0;
                // keep roll active
                m.armadilloDangerDetectedUntil =
                    std::max(m.armadilloDangerDetectedUntil, tickNo_ + 80);
            }
            int armor = totalArmorPoints(m);
            armor = breachAdjustedArmor(armor, breachLv); // plan44 G-09: breach pre-discounts armor (formula untouched)
            int epf = CombatManager::computeEPF(src, m);
            // mobs have no toughness in current formula; pass 0
            const float finalAmt =
                DamageCalculator::calculate(effectiveAmount, src, armor, 0.0,
                                             epf, {});
            // mobs have no resistance effects currently
            if (finalAmt <= 0) return;
            m.health -= finalAmt;
            m.hurtCooldown = 10;
            if (m.kind == MobKind::Armadillo)
                m.armadilloDangerDetectedUntil = tickNo_ + 80;

            if (m.kind == MobKind::Enderman && !m.dead) {
                // 50% chance to teleport when hurt, respecting cooldown
                if (nextRandom() % 2 == 0 &&
                    tickNo_ - m.lastTeleportTick > 20) {
                    // trigger teleport via AiContext next tick; also mark hurt
                    m.lastTeleportTick = tickNo_;
                }
            }
            result.dead = m.health <= 0;
            if (result.dead) m.dead = true;
            result.applied = true;
        }
    }

    if (result.creakingBlocked) {
        if (result.creakingHeart)
            growResinNearHeartFor(result.dimension, result.heartX, result.heartY,
                                  result.heartZ);
        broadcastSoundFor(result.dimension, "minecraft:entity.creaking.sway",
                           result.x, result.y, result.z, 1.f, 1.f,
                           "hostile");
        // trigger resin clump growth is handled in growResinNearHeart
        return;
    }
    if (!result.applied) return;

    noteMobHurt(result.entityId, -1);
    if (result.boss && bossAI_) {
        if (!result.dead) bossAI_->onDamage(result.bossSnapshot);
        else bossAI_->onDeath(result.bossSnapshot);
    }

    float yaw = 0.f;
    // Try to compute yaw if the nearest player is the attacker (best effort).
    double best = 1e100;
    double attackerX = 0, attackerZ = 0;
    for (auto& pl : playersSnapshot()) {
        if (!pl) continue;
        double px = 0, pz = 0;
        std::int8_t playerDimension = 0;
        {
            std::lock_guard playerLock(pl->stateMtx);
            playerDimension = pl->dimension;
            px = pl->x;
            pz = pl->z;
        }
        if (canonicalDimension(playerDimension) !=
            canonicalDimension(result.dimension))
            continue;
        const double dx = px - result.x;
        const double dz = pz - result.z;
        const double d2 = dx * dx + dz * dz;
        if (d2 < best) {
            best = d2;
            attackerX = px;
            attackerZ = pz;
        }
    }
    if (best < 64) {
        yaw = static_cast<float>(
            std::atan2(attackerZ - result.z, attackerX - result.x) *
            180.0 / 3.141592653589793);
    }
    broadcastHurtAnimationFor(result.dimension, result.entityId, yaw, nullptr);
    std::string snd;
    switch (result.kind) {
        case MobKind::Creeper: snd = "minecraft:entity.creeper.hurt"; break;
        case MobKind::Zombie: snd = "minecraft:entity.zombie.hurt"; break;
        case MobKind::Skeleton: snd = "minecraft:entity.skeleton.hurt"; break;
        case MobKind::Spider: snd = "minecraft:entity.spider.hurt"; break;
        case MobKind::Enderman: snd = "minecraft:entity.enderman.hurt"; break;
        default: snd = "minecraft:entity.generic.hurt"; break;
    }
    broadcastEntitySoundFor(result.dimension, result.entityId, snd, 1.f, 1.f,
                            SoundSource::Hostile);
}
void GameServer::applyDamageToMob(MobEntity& m, float amount, const char* cause) {
    DamageSource src(cause ? std::string(cause) : std::string("generic"));
    applyDamageToMob(m, amount, src);
}
void GameServer::sendActionBar(Player& p, const std::string& text) {
    const auto connection = snapshotPlayerConnection(p);
    if (!connection) return;
    WriteBuffer b; nbt::writeTextComponent(b, text);
    connection->trySendPacket(proto::pl::sc::ActionBar, b);
}
void GameServer::broadcastActionBar(const std::string& text, Player* except) {
    WriteBuffer b; nbt::writeTextComponent(b, text);
    broadcastPacketExcept(except, proto::pl::sc::ActionBar, b);
}
void GameServer::sendServerData(Player& p) {
    const auto connection = snapshotPlayerConnection(p);
    if (!connection) return;
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
    connection->trySendPacket(proto::pl::sc::ServerData, b);
}
void GameServer::broadcastServerData() {
    for (auto& pp : playersSnapshot()) sendServerData(*pp);
}
void GameServer::sendHurtAnimation(Player& p, int32_t entityId, float yaw) {
    const auto connection = snapshotPlayerConnection(p);
    if (!connection) return;
    if (!std::isfinite(yaw)) yaw = 0;
    WriteBuffer b; b.varint(entityId); b.f32(yaw);
    connection->trySendPacket(proto::pl::sc::HurtAnimation, b);
}
void GameServer::broadcastHurtAnimation(int32_t entityId, float yaw, Player* except) {
    std::int8_t dimension =
        brainTickGuard_ ? snapshotMobCombat(*brainTickGuard_).dimension : 0;
    if (!brainTickGuard_) {
        bool found = false;
        for (const auto& player : playersSnapshot()) {
            if (!player) continue;
            const auto state = snapshotPlayerCombat(*player);
            if (state.entityId == entityId) {
                dimension = state.dimension;
                found = true;
                break;
            }
        }
        if (!found) {
            for (const auto& mob : mobsSnapshot()) {
                if (!mob) continue;
                const auto state = snapshotMobCombat(*mob);
                if (state.entityId == entityId) {
                    dimension = state.dimension;
                    break;
                }
            }
        }
    }
    broadcastHurtAnimationFor(dimension, entityId, yaw, except);
}
void GameServer::broadcastHurtAnimationFor(std::int8_t dimension,
                                           int32_t entityId, float yaw,
                                           Player* except) {
    if (!std::isfinite(yaw)) yaw = 0;
    WriteBuffer b; b.varint(entityId); b.f32(yaw);
    broadcastPacketExceptInDimension(dimension, except,
                                     proto::pl::sc::HurtAnimation, b);
}
void GameServer::sendEntitySound(Player& p, int32_t entityId, const std::string& soundName, float volume, float pitch, SoundSource category) {
    const auto connection = snapshotPlayerConnection(p);
    if (!connection) return;
    WriteBuffer b;
    b.varint(0); b.string(soundName); b.boolean(false);
    b.varint(static_cast<int32_t>(category));
    b.varint(entityId);
    b.f32(volume); b.f32(pitch);
    b.i64(static_cast<int64_t>(entityId) ^ tickNo_);
    connection->trySendPacket(proto::pl::sc::EntitySoundEffect, b);
}
void GameServer::broadcastEntitySound(int32_t entityId, const std::string& soundName, float volume, float pitch, SoundSource category) {
    std::int8_t dimension =
        brainTickGuard_ ? snapshotMobCombat(*brainTickGuard_).dimension : 0;
    if (!brainTickGuard_) {
        bool found = false;
        for (const auto& player : playersSnapshot()) {
            if (!player) continue;
            const auto state = snapshotPlayerCombat(*player);
            if (state.entityId == entityId) {
                dimension = state.dimension;
                found = true;
                break;
            }
        }
        if (!found) {
            for (const auto& mob : mobsSnapshot()) {
                if (!mob) continue;
                const auto state = snapshotMobCombat(*mob);
                if (state.entityId == entityId) {
                    dimension = state.dimension;
                    break;
                }
            }
        }
    }
    broadcastEntitySoundFor(dimension, entityId, soundName, volume, pitch,
                            category);
}
void GameServer::broadcastEntitySoundFor(std::int8_t dimension,
                                         int32_t entityId,
                                         const std::string& soundName,
                                         float volume, float pitch,
                                         SoundSource category) {
    WriteBuffer b;
    b.varint(0); b.string(soundName); b.boolean(false);
    b.varint(static_cast<int32_t>(category));
    b.varint(entityId);
    b.f32(volume); b.f32(pitch);
    b.i64(static_cast<int64_t>(entityId) ^ tickNo_);
    broadcastPacketExceptInDimension(dimension, nullptr,
                                     proto::pl::sc::EntitySoundEffect, b);
}
void GameServer::sendChatSuggestions(Player& p, int32_t action, const std::vector<std::string>& entries) {
    const auto connection = snapshotPlayerConnection(p);
    if (!connection) return;
    WriteBuffer b; b.varint(action); b.varint(static_cast<int32_t>(entries.size()));
    for (auto& s : entries) b.string(s);
    connection->trySendPacket(proto::pl::sc::ChatSuggestions, b);
}
void GameServer::broadcastChatSuggestions(int32_t action, const std::vector<std::string>& entries, Player* except) {
    WriteBuffer b; b.varint(action); b.varint(static_cast<int32_t>(entries.size()));
    for (auto& s : entries) b.string(s);
    broadcastPacketExcept(except, proto::pl::sc::ChatSuggestions, b);
}
void GameServer::sendSyncEntityPosition(Player& p, int32_t entityId, double x, double y, double z, double dx, double dy, double dz, float yaw, float pitch, bool onGround) {
    const auto connection = snapshotPlayerConnection(p);
    if (!connection) return;
    WriteBuffer b; b.varint(entityId); b.f64(x); b.f64(y); b.f64(z); b.f64(dx); b.f64(dy); b.f64(dz); b.f32(yaw); b.f32(pitch); b.boolean(onGround);
    connection->trySendPacket(proto::pl::sc::SyncEntityPosition, b);
}
void GameServer::broadcastSyncEntityPosition(int32_t entityId, double x, double y, double z, double dx, double dy, double dz, float yaw, float pitch, bool onGround, Player* except) {
    WriteBuffer b; b.varint(entityId); b.f64(x); b.f64(y); b.f64(z); b.f64(dx); b.f64(dy); b.f64(dz); b.f32(yaw); b.f32(pitch); b.boolean(onGround);
    broadcastPacketExcept(except, proto::pl::sc::SyncEntityPosition, b);
}
void GameServer::sendSyncEntityPosition(Player& p, const MobEntity& mob) {
    const auto state = snapshotMobCombat(mob);
    float yawf = 0, pitchf = 0;
    sendSyncEntityPosition(p, state.entityId, state.x, state.y, state.z,
                           0, 0, 0, yawf, pitchf, true);
}
void GameServer::broadcastSyncEntityPosition(const MobEntity& mob, Player* except) {
    const auto state = snapshotMobCombat(mob);
    float yawf = 0, pitchf = 0;
    WriteBuffer b;
    b.varint(state.entityId); b.f64(state.x); b.f64(state.y); b.f64(state.z);
    b.f64(0); b.f64(0); b.f64(0); b.f32(yawf); b.f32(pitchf);
    b.boolean(true);
    broadcastPacketExceptInDimension(state.dimension, except,
                                     proto::pl::sc::SyncEntityPosition, b);
}
void GameServer::sendMapData(Player& p, int mapId, uint8_t scale, bool locked) {
    const auto connection = snapshotPlayerConnection(p);
    if (!connection) return;
    WriteBuffer b;
    b.varint(mapId);
    b.i8((int8_t)scale);
    b.boolean(locked);
    b.boolean(false); // icons absent (option<array> false)
    b.u8(0); // columns 0 => no rows/x/y/data
    connection->trySendPacket(proto::pl::sc::MapData, b);
}
void GameServer::sendMapData(Player& p, int mapId, const std::array<uint8_t,16384>& colors, uint8_t scale) {
    const auto connection = snapshotPlayerConnection(p);
    if (!connection) return;
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
    connection->trySendPacket(proto::pl::sc::MapData, b);
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
    const auto connection = snapshotPlayerConnection(p);
    if (!connection) return;
    WriteBuffer b;
    b.varint(entityId);
    b.varint(1); // one lerp step
    b.f32((float)x); b.f32((float)y); b.f32((float)z);
    b.f32(0.f); b.f32(0.f); b.f32(0.f);
    b.f32(yaw); b.f32(pitch); b.f32(1.f);
    connection->trySendPacket(proto::pl::sc::MoveMinecart, b);
}
void GameServer::broadcastMoveMinecart(std::int32_t entityId, double x, double y, double z, float yaw, float pitch, Player* except) {
    WriteBuffer b;
    b.varint(entityId);
    b.varint(1);
    b.f32((float)x); b.f32((float)y); b.f32((float)z);
    b.f32(0.f); b.f32(0.f); b.f32(0.f);
    b.f32(yaw); b.f32(pitch); b.f32(1.f);
    std::int8_t dimension = 0;
    if (except) {
        std::lock_guard playerLock(except->stateMtx);
        dimension = except->dimension;
    }
    for (const auto& mob : mobsSnapshot()) {
        if (!mob) continue;
        const auto state = snapshotMobCombat(*mob);
        if (state.entityId == entityId) {
            dimension = state.dimension;
            break;
        }
    }
    broadcastPacketExceptInDimension(dimension, except,
                                     proto::pl::sc::MoveMinecart, b);
}
void GameServer::sendSelectAdvancementTab(Player& p, const std::string& tabId) {
    const auto connection = snapshotPlayerConnection(p);
    if (!connection) return;
    WriteBuffer b;
    if (tabId.empty()) {
        b.boolean(false);
    } else {
        b.boolean(true);
        b.string(tabId);
    }
    connection->trySendPacket(proto::pl::sc::SelectAdvancementTab, b);
}
void GameServer::broadcastSelectAdvancementTab(const std::string& tabId, Player* except) {
    WriteBuffer b;
    if (tabId.empty()) b.boolean(false);
    else { b.boolean(true); b.string(tabId); }
    broadcastPacketExcept(except, proto::pl::sc::SelectAdvancementTab, b);
}
} // namespace cppfm
