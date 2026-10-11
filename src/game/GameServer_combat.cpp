#include "GameServer.hpp"
#include "EntityTrackingBroadcast.hpp"
#include "Session.hpp"
#include "Messages.hpp"
#include "MetadataTypes.hpp"
#include "Constants.hpp"
#include "../generated/EntityIds.hpp"
#include "DamageComponent.hpp"
#include "DeathProtection.hpp"
#include "DeathTransition.hpp"
#include "AfterDamageAmounts.hpp"
#include "EnchantmentHelper.hpp"
#include "CombatManager.hpp"
#include "Particles.hpp"
#include "GameServerHelpers.hpp"
#include "PlayerCollision.hpp"

#include <cstdio>
#include <cstdlib>
#include <cmath>

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

bool isLivingDamageTarget(const MobKind kind) noexcept {
    // follow_range is absent (zero) for the non-living entity records in
    // MobStats. Armor stands are LivingEntity subclasses despite having no
    // follow range, so retain that explicit exception.
    return kind == MobKind::ArmorStand || mobStats(kind).followRange > 0.0F;
}

std::shared_ptr<Player> playerEntityInDimension(
    GameServer& server, const std::int32_t entityId,
    const std::int8_t dimension) {
    for (const auto& player : server.playersSnapshot()) {
        if (!player) continue;
        std::lock_guard playerLock(player->stateMtx);
        if (player->entityId == entityId &&
            GameServer::canonicalDimension(player->dimension) ==
                GameServer::canonicalDimension(dimension))
            return player;
    }
    return {};
}

WriteBuffer primedTntSpawnBody(const TntEntity& tnt) {
    WriteBuffer body;
    body.varint(tnt.entityId);
    const auto uuid = entityUuidForId(tnt.entityId);
    body.uuid(uuid.data());
    const auto& types = gen::entityTypeIdByName();
    const auto type = types.find("minecraft:tnt");
    body.varint(type != types.end()
        ? static_cast<std::int32_t>(type->second) : 125);
    body.f64(tnt.x); body.f64(tnt.y); body.f64(tnt.z);
    body.i8(0); body.i8(0); body.i8(0);
    body.varint(0);
    body.i16(static_cast<std::int16_t>(tnt.vx * 8000));
    body.i16(static_cast<std::int16_t>(tnt.vy * 8000));
    body.i16(static_cast<std::int16_t>(tnt.vz * 8000));
    return body;
}

bool sendPrimedTntSpawn(Connection& connection, const TntEntity& tnt) {
    const auto body = primedTntSpawnBody(tnt);
    return connection.trySendPacketLowPriority(pl::sc::SpawnEntity, body);
}

std::shared_ptr<Connection> snapshotPlayerConnection(const Player& player) {
    std::lock_guard playerLock(player.stateMtx);
    return player.conn;
}

void sendPlayerEntityPacketToSelfIfInPlay(
    Player& player, const std::uint8_t packetId, const WriteBuffer& body) {
    std::shared_ptr<Connection> connection;
    {
        std::lock_guard playerLock(player.stateMtx);
        if (player.inPlay) connection = player.conn;
    }
    if (connection && !connection->trySendPacket(packetId, body))
        connection->close();
}

struct MobCombatSnapshot {
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    MobKind kind = MobKind::Pig;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    double velocityZ = 0.0;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool dead = false;
};

MobCombatSnapshot snapshotMobCombat(const MobEntity& mob) {
    std::lock_guard entityLock(*mob.stateMtx);
    return {mob.entityId, mob.dimension, mob.kind, mob.x, mob.y, mob.z,
            mob.velX, mob.velY, mob.velZ, mob.yaw, mob.pitch, mob.dead};
}

bool tryRandomTeleportImpl(GameServer& server, Player& player,
                           float diameter) {
    if (!std::isfinite(diameter) || diameter <= 0.0F) return false;
    std::int8_t dimension = 0;
    double originX = 0.0;
    double originY = 0.0;
    double originZ = 0.0;
    {
        std::lock_guard playerLock(player.stateMtx);
        dimension = player.dimension;
        originX = player.x;
        originY = player.y;
        originZ = player.z;
    }

    const auto& world = server.worldFor(dimension);
    const double halfDiameter = static_cast<double>(diameter) * 0.5;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const auto offset = [diameter, halfDiameter] {
            return static_cast<double>(nextRandom()) /
                       static_cast<double>(RAND_MAX) * diameter -
                   halfDiameter;
        };
        const double x = originX + offset();
        const double y = originY + offset();
        const double z = originZ + offset();
        if (y < world.minY() || y + 1.8 >= world.maxY() ||
            !server.isInsideBorder(x, z) ||
            player_collision::intersects(world, x, y, z))
            continue;
        if (server.teleportPlayer(player, x, y, z)) return true;
    }
    return false;
}

} // namespace

bool GameServer::tryRandomTeleport(Player& player, const float diameter) {
    return tryRandomTeleportImpl(*this, player, diameter);
}

void GameServer::syncPlayerArmorAttributes(Player& p) {
    CombatManager::syncPlayerArmor(*this, p);
}
bool GameServer::applyDamage(Player& p, float amount, const DamageSource& src,
                             int breachLv,
                             SimulationDispatchGuard* simulationLock) {
    if (mobStateLockOwnedByCurrentThread()) {
        const DamageSource source = src;
        bool applied = false;
        runWithoutMobStateLock(
            [this, &p, amount, source, breachLv, simulationLock, &applied] {
                applied = applyDamage(p, amount, source, breachLv,
                                      simulationLock);
            });
        return applied;
    }
    if (amount <= 0) return false;
    if (!SimulationDispatchGuard::currentThreadOwns(
            &simulationDispatchMtx_)) {
        SimulationDispatchGuard mutationGate(simulationDispatchMtx_);
        return applyDamage(p, amount, src, breachLv, &mutationGate);
    }

    // The JVM event is user code and may synchronously call back into the
    // native bridge (or queue a server-thread mutation that waits for this
    // call).  Never hold the player state lock while invoking it.  The second
    // guarded check below closes the re-entry window: a callback may have
    // killed the player or changed its game mode while deciding the damage.
    {
        std::lock_guard playerLock(p.stateMtx);
        if (!p.inPlay || p.gamemode == 1 || p.gamemode == 3 || p.dead ||
            p.health <= 0)
            return false;
    }
    if (jvmRuntime_) {
        const auto invokeDamageHook = [&] {
                return jvmRuntime_->onEntityDamage(
                    &p, nullptr, amount, src.type);
        };
        const bool allowed = simulationLock
            ? simulationLock->withLockReleased(invokeDamageHook)
            : SimulationDispatchGuard::withCurrentLockReleased(
                  invokeDamageHook);
        if (!allowed) return false;
    }

    // Armor synchronization has its own short player-state critical section.
    // Keep it outside this function's mutation lock: the helper may emit an
    // attribute packet and must not be nested inside the damage transaction.
    syncPlayerArmorAttributes(p);

    PlayerCombatSnapshot result;
    bool shouldKill = false;
    bool totemUsed = false;
    bool inventoryChanged = false;
    std::string usedProtectionItem;
    std::vector<std::uint8_t> removedEffects;
    std::vector<EffectInstance> protectionEffects;
    std::vector<float> protectionTeleportDiameters;
    std::vector<death_protection::SoundCue> protectionSounds;
    {
        std::lock_guard playerLock(p.stateMtx);
        // The callback and armor sync both leave a re-entry window.  Do not
        // mutate a player that became dead or switched to an invulnerable
        // game mode while either operation was running.
        if (!p.inPlay || p.gamemode == 1 || p.gamemode == 3 || p.dead ||
            p.health <= 0)
            return false;
        int armor = (int)std::round(p.attributes.getValue(Attribute::ARMOR));
        if (armor == 0) armor = totalArmorPoints(p.inv);
        armor = breachAdjustedArmor(armor, breachLv); // plan44 G-09: breach pre-discounts armor (formula untouched)
        double toughness = p.attributes.getValue(Attribute::ARMOR_TOUGHNESS);
        int epf = CombatManager::computeEPF(src, p);
        float finalAmt = DamageCalculator::calculate(amount, src, armor, toughness, epf, p.effects);
        if (finalAmt <= 0) return false;
        // attack exhaustion for attacker is handled elsewhere; damage taken also adds exhaustion
        if (p.gamemode == 0) addHungerExhaustion(p, 0.1f);
        p.health -= finalAmt;
        p.hurtCooldown = 10;
        if (p.health <= 0) {
            p.health = 0.0F;
            shouldKill = true;
        }
        result = snapshotPlayerCombat(p);
    }

    // Death bookkeeping, drops, callbacks, and network I/O all happen after
    // the player state lock is released.  health <= 0 above prevents another
    // damage transaction from overtaking killPlayer during this hand-off.
    bool allowDeath = true;
    if (shouldKill && jvmRuntime_) {
        const auto invokeAllowDeath = [&] {
            return jvmRuntime_->onEntityAllowDeath(
                &p, nullptr, amount, src.type);
        };
        allowDeath = simulationLock
            ? simulationLock->withLockReleased(invokeAllowDeath)
            : SimulationDispatchGuard::withCurrentLockReleased(
                  invokeAllowDeath);
    }
    if (shouldKill) {
        std::lock_guard playerLock(p.stateMtx);
        const auto resolution = death_transition::resolve(allowDeath, p.health);
        if (resolution != death_transition::Resolution::Commit) {
            // ALLOW_DEATH vetoes the transition itself, without implicitly
            // healing a listener's zero- or negative-health state.
            shouldKill = false;
            p.dead = false;
            if (p.health <= 0.0F) {
                p.pendingDeathTick = tickNo_ + 1;
                p.pendingDeathCause = src.type;
            } else {
                p.pendingDeathTick = -1;
                p.pendingDeathCause.clear();
            }
        } else {
            p.pendingDeathTick = -1;
            p.pendingDeathCause.clear();
            // Fabric's allow-death event precedes totem activation. Only an
            // allowed fatal transition reaches the native death-protection
            // path, matching LivingEntity.onDeath ordering.
            p.health = 0.0F;
            p.dead = false;
            std::array<std::size_t, 2> handSlots{};
            std::size_t handCount = 0;
            if (p.heldSlot >= 0 && p.heldSlot < 9)
                handSlots[handCount++] =
                    static_cast<std::size_t>(36 + p.heldSlot);
            handSlots[handCount++] = 45;

            std::optional<death_protection::Protection> protection;
            std::size_t protectionSlot = 0;
            for (std::size_t index = 0; index < handCount; ++index) {
                const auto slot = handSlots[index];
                protection = death_protection::fromStack(p.inv[slot]);
                if (protection) {
                    protectionSlot = slot;
                    break;
                }
            }

            if (protection) {
                auto& stack = p.inv[protectionSlot];
                usedProtectionItem = stack.name();
                if (stack.count > 0 && --stack.count == 0)
                    stack = ItemStack::air();
                inventoryChanged = true;
                totemUsed = true;
                shouldKill = false;
                p.health = 1.0F;
                auto changes = death_protection::apply(
                    *protection, p.effects, [this] {
                        return static_cast<double>(nextRandom()) /
                               static_cast<double>(RAND_MAX);
                    }, [this](const std::string& tag,
                              std::uint8_t effectId) {
                        const auto* members = datapackManager_.tagManager.getStatusEffectTag(tag);
                        return members && members->contains(effectId);
                    });
                removedEffects = std::move(changes.removed);
                protectionTeleportDiameters =
                    std::move(changes.randomTeleportDiameters);
                protectionSounds = std::move(changes.sounds);
                protectionEffects = persistentEntityEffectUpdates(
                    changes.updated, p.effects);
                if (p.stats)
                    p.stats->add("minecraft:used|" + usedProtectionItem);
            }
        }
        result = snapshotPlayerCombat(p);
    }
    if (shouldKill) {
        killPlayer(p, src.type.c_str());
        result = snapshotPlayerCombat(p);
    }
    if (totemUsed) {
        if (inventoryChanged) resendInventory(p);
        for (const auto diameter : protectionTeleportDiameters)
            (void)tryRandomTeleport(p, diameter);
        publishPlayerEffectChanges(
            p, result.entityId, result.dimension, result.connection,
            removedEffects, protectionEffects, true);

        WriteBuffer status;
        status.i32(result.entityId);
        status.i8(35); // Entity status: totem activation animation.
        if (result.connection)
            result.connection->trySendPacket(pl::sc::EntityEvent, status);
        broadcastTrackedPlayerPacket(p, pl::sc::EntityEvent, status);
        broadcastSoundFor(result.dimension, "minecraft:item.totem.use",
                          result.x, result.y, result.z, 1.0F, 1.0F,
                          "player");
        for (const auto& sound : protectionSounds)
            broadcastSoundFor(result.dimension, sound.id.c_str(), result.x,
                              result.y, result.z, 1.0F, 1.0F, "player",
                              sound.fixedRange);
        if (usedProtectionItem == "minecraft:totem_of_undying")
            grantAdvancement(p, "minecraft:adventure/totem_of_undying");
    }
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
        broadcastTrackedPlayerPacket(p, pl::sc::DamageEvent, de);
    }
    {
        float yaw = 0.f;
        broadcastHurtAnimationFor(result.dimension, result.entityId, yaw, nullptr);
        std::string snd = "minecraft:entity.player.hurt";
        broadcastEntitySoundFor(result.dimension, result.entityId, snd, 1.f, 1.f,
                                SoundSource::Player);
    }
    if (jvmRuntime_) {
        // `amount` is at Fabric's pre-armor boundary. Shield-blocked hits
        // dispatch separately from tryShieldBlock with damageTaken == 0.
        const auto damage = makeAfterDamageAmounts(amount, amount);
        const bool died = result.dead && result.health <= 0.0F;
        const auto invokeDamageApplied = [&] {
            jvmRuntime_->onEntityDamageApplied(
                &p, nullptr, damage.baseDamageTaken, damage.damageTaken,
                damage.blocked, died, src.type);
        };
        if (simulationLock)
            simulationLock->withLockReleased(invokeDamageApplied);
        else
            SimulationDispatchGuard::withCurrentLockReleased(
                invokeDamageApplied);
    }
    return true;
}
void GameServer::notifyShieldBlockedDamage(Player& p,
                                           const float baseDamage,
                                           const DamageSource& src) {
    if (!jvmRuntime_ || !std::isfinite(baseDamage) || baseDamage <= 0.0F)
        return;
    const auto damage = makeAfterDamageAmounts(baseDamage, 0.0F, true);
    const auto invokeDamageApplied = [&] {
        jvmRuntime_->onEntityDamageApplied(
            &p, nullptr, damage.baseDamageTaken, damage.damageTaken,
            damage.blocked, false, src.type);
    };
    SimulationDispatchGuard::withCurrentLockReleased(invokeDamageApplied);
}
bool GameServer::applyDamage(Player& p, float amount, const char* cause) {
    DamageSource src(cause ? std::string(cause) : std::string("generic"));
    return applyDamage(p, amount, src);
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
        p.pendingDeathTick = -1;
        p.pendingDeathCause.clear();
        playerName = p.name;
        dimension = p.dimension;
        x = p.x;
        y = p.y;
        z = p.z;
        connection = p.conn;
        if (p.stats) p.stats->add("minecraft:custom|minecraft:deaths");

        if (!keepInventory) {
            drops.reserve(kPlayerInventorySlotCount);
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
        if (hardcore) {
            p.previousGamemode = p.gamemode;
            p.gamemode = 3; // spectator; publish under the same lock
        }
    }

    {
        SimulationDispatchGuard simulation(simulationDispatchMtx_);
        for (Session* session : activeSessions_) {
            if (session && session->self_.get() == &p)
                session->stopAllItemUse();
        }
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
    {
        std::lock_guard playerLock(target.stateMtx);
        targetDimension = target.dimension;
        targetEntityId = target.entityId;
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
        if (CombatManager::tryShieldBlock(*this, target, msrc, mobX, mobZ,
                                          axeMob, dmg)) return;
    }
    const bool damaged = applyDamage(target, dmg, cause.c_str());
    if (damaged) CombatManager::applyThornsReflection(*this, target, &m, nullptr);
    if (damaged) {
        std::lock_guard entityLock(*m.stateMtx);
        if (m.entityId == mobEntityId) m.angerTargetEntityId = targetEntityId;
    }
}
bool GameServer::tryBreedFeed(Player& p, MobEntity& m,
                              const std::size_t inventorySlot) {
    std::int32_t entityId = 0;
    bool consumed = false;
    bool enteredLove = false;
    bool becameAdult = false;
    bool inventoryChanged = false;
    {
        std::scoped_lock stateLock(p.stateMtx, *m.stateMtx);
        if (inventorySlot >= p.inv.size() || !p.inPlay || p.dead ||
            p.gamemode == 3 || m.dead ||
            canonicalDimension(p.dimension) != canonicalDimension(m.dimension))
            return false;
        const bool mainHandSlot = inventorySlot >= 36 && inventorySlot <= 44;
        if ((!mainHandSlot && inventorySlot != 45) ||
            (mainHandSlot &&
             inventorySlot != static_cast<std::size_t>(36 + p.heldSlot)))
            return false;
        const auto foodId = MobEntity::breedingItemFor(m.kind);
        const auto now = tickNow();
        if (!foodId) return false;
        auto& held = p.inv[inventorySlot];
        if (held.itemId != foodId || held.count <= 0) return false;

        const bool baby = MobEntity::isBaby(m);
        const bool horseRequiresTaming =
            m.kind == MobKind::Horse || m.kind == MobKind::Donkey ||
            m.kind == MobKind::Mule || m.kind == MobKind::Llama ||
            m.kind == MobKind::TraderLlama;
        const bool canEnterLove = !baby &&
                                  (!horseRequiresTaming || m.isTamed) &&
                                  !m.inLove &&
                                  now >= m.breedCooldownUntil;
        const bool goldenCarrotHorse =
            (m.kind == MobKind::Horse || m.kind == MobKind::Donkey ||
             m.kind == MobKind::Mule) && held.itemId == foodId;
        const double horseMaxHealth = m.kind == MobKind::Horse
            ? m.horseMaxHealth : mobStats(m.kind).maxHealth;
        const bool healsHorse = goldenCarrotHorse &&
                                m.health < horseMaxHealth;
        const bool gainsHorseTemper = goldenCarrotHorse &&
                                      m.canGainHorseTemper();
        const bool healsCamel = m.kind == MobKind::Camel &&
                                m.health < mobStats(m.kind).maxHealth;
        // Vanilla lets breeding-item interactions accelerate a baby's growth
        // even though it cannot enter love mode. PassiveEntity applies 10% of
        // the remaining age, rounded through whole seconds before converting
        // back to ticks; retain that order so near-adult babies match vanilla.
        if (!baby && !canEnterLove && !healsHorse && !gainsHorseTemper &&
            !healsCamel) {
            return false;
        }

        if (p.gamemode != 1) {
            if (--held.count <= 0) held = ItemStack::air();
            inventoryChanged = true;
        }
        if (baby) {
            // CamelEntity overrides AbstractHorseEntity.receiveFood and
            // advances a baby camel by exactly ten seconds per cactus.
            const std::int32_t growthSeconds = m.kind == MobKind::Camel
                ? 10
                : static_cast<std::int32_t>(static_cast<float>(
                      -static_cast<std::int64_t>(m.age) / 20) * 0.1F);
            const auto previousAge = m.age;
            m.age = std::min<std::int32_t>(
                0, m.age + growthSeconds * 20);
            becameAdult = previousAge < 0 && m.age == 0;
        }
        if (healsCamel)
            m.health = std::min<double>(mobStats(m.kind).maxHealth,
                                        m.health + 2.0);
        if (healsHorse)
            m.health = std::min(horseMaxHealth, m.health + 4.0);
        if (gainsHorseTemper)
            (void)m.addHorseTemper(MobEntity::kHorseGoldenCarrotTemper);
        if (canEnterLove) {
            m.inLove = true;
            m.loveUntilTick = now + 30 * 20;
            enteredLove = true;
        }
        entityId = m.entityId;
        consumed = true;
    }
    if (!consumed) return false;
    // Network and extension-facing work stays outside both model locks.
    if (inventoryChanged) resendInventory(p);
    if (becameAdult) {
        WriteBuffer metadata;
        metadata.varint(entityId);
        metadata.u8(16);  // ageable flags
        metadata.u8(0);   // byte serializer
        metadata.u8(0);   // adult
        metadata.u8(255); // end metadata list
        broadcastMobPositionUpdate(
            m, pl::sc::SetEntityMetadata, metadata);
    }
    if (enteredLove) {
        WriteBuffer status;
        status.i32(entityId);
        status.i8(18);
        broadcastMobPositionUpdate(m, pl::sc::EntityEvent, status);
    }
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
            World& targetWorld = worldFor(target.dimension);
            for (int scanY = targetWorld.maxY() - 1;
                 scanY >= targetWorld.minY(); --scanY) {
                std::uint16_t st = targetWorld.getBlock(lx, scanY, lz);
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
                                   float pitch, const char* category,
                                   std::optional<float> fixedRange) {
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
    const bool hasFixedRange = fixedRange &&
        std::isfinite(*fixedRange) && *fixedRange > 0.0F;
    b.boolean(hasFixedRange);
    if (hasFixedRange) b.f32(*fixedRange);
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
void GameServer::explodeAt(double x, double y, double z, float power) {
    explodeAtFor(0, x, y, z, power);
}
void GameServer::explodeAtFor(std::int8_t dimension, double x, double y,
                              double z, float power) {
    SimulationDispatchGuard simulation(simulationDispatchMtx_);
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
            if (CombatManager::tryShieldBlock(*this, *p, esrc, x, z, false,
                                              dmg)) continue;
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
        broadcastTrackedPlayerPacket(*p, pl::sc::EntityVelocity, v);
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
        if (deadNow && m->tryClaimDeathHandling()) dead.push_back(m);
    }
    for (const auto& mob : dead) invalidateJvmMob(mob);
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
        bool entityOnFire = false;
        {
            std::lock_guard mobLock(*m->stateMtx);
            entityOnFire = m->onFireTicks > 0;
        }
        const auto lootContext = mobLootContextForDeath(
            state.entityId, entityOnFire, power);
        spawnMobLootFor(state.kind, state.dimension, state.x,
                        state.y + .4, state.z, lootContext);
        spawnXpOrbsFor(state.dimension, state.x, state.y + .5, state.z,
                       mobStats(state.kind).xpDrop, nullptr);
        eraseMobAi(state.entityId);
        broadcastMobRemoval(state.entityId);
    }
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
    SimulationDispatchGuard simulation(simulationDispatchMtx_);
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
    publishTrackedEntity(
        playersSnapshot(), t->entityId, t->dimension, t->x, t->z,
        MobKind::Tnt, cfg_.viewDistance,
        cfg_.entityBroadcastRangePercentage,
        [t](Connection& connection, Player&) {
            return sendPrimedTntSpawn(connection, *t);
        },
        [](Connection&, Player&) { return true; });
}
void GameServer::tntTick(){
    SimulationDispatchGuard simulation(simulationDispatchMtx_);
    const auto trackedViewers = playersSnapshot();
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
        if (t->y < world.minY()) t->y = world.minY();
        if (world.getBlock(static_cast<int>(std::floor(t->x)),
                           static_cast<int>(std::floor(t->y - 0.1)),
                           static_cast<int>(std::floor(t->z))) != 0) {
            t->vx *= 0.7;
            t->vz *= 0.7;
            if (t->vy < 0) t->vy = -t->vy * 0.5;
        }
        if (--t->fuse <= 0) toExplode.push_back(t);
        ++t->ageTicks;
        const bool sendMovement = t->ageTicks % 4 == 0;
        WriteBuffer teleport;
        WriteBuffer sync;
        if (sendMovement) {
            teleport = makeEntityTeleportBody(
                t->entityId, t->x, t->y, t->z, t->vx, t->vy, t->vz,
                0.0f, 0.0f, 0, false);
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
        }
        publishTrackedEntity(
            trackedViewers, t->entityId, dimension, t->x, t->z,
            MobKind::Tnt, cfg_.viewDistance,
            cfg_.entityBroadcastRangePercentage,
            [t](Connection& connection, Player&) {
                return sendPrimedTntSpawn(connection, *t);
            },
            [&, sendMovement](Connection& connection, Player&) {
                if (!sendMovement) return true;
                return connection.trySendPacketLowPriority(
                           pl::sc::EntityTeleport, teleport) &&
                       connection.trySendPacketLowPriority(
                           pl::sc::SyncEntityPosition, sync);
            });
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
        removeTrackedEntity(trackedViewers, t->entityId);
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
    SimulationDispatchGuard simulation(simulationDispatchMtx_);
    // Visual: spawn lightning bolt entity and broadcast sound
    {
        auto bolt = std::make_shared<LightningBoltEntity>();
        bolt->entityId = nextEntityId();
        bolt->dimension = canonicalDimension(dimension);
        bolt->x = x; bolt->y = y; bolt->z = z;
        // Broadcast SpawnEntity for lightning (type 94? Use generic)
        WriteBuffer b;
        b.varint(bolt->entityId);
        const auto uuid = entityUuidForId(bolt->entityId);
        b.uuid(uuid.data());
        b.varint(static_cast<std::int32_t>(
            gen::entityTypeIdByName().at("minecraft:lightning_bolt")));
        b.f64(x); b.f64(y); b.f64(z);
        b.i8(0); b.i8(0); b.i8(0);
        b.varint(0); b.i16(0); b.i16(0); b.i16(0);
        publishTransientEntitySpawn(
            playersSnapshot(), bolt->dimension, bolt->x, bolt->z,
            MobKind::LightningBolt, cfg_.viewDistance,
            cfg_.entityBroadcastRangePercentage,
            [&b](Connection& connection, Player&) {
                return connection.trySendPacketLowPriority(
                    pl::sc::SpawnEntity, b);
            });
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
        std::shared_ptr<MobEntity> mob;
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
        charged.push_back({m, m->entityId});
    }
    for (const auto& creeper : charged) {
        WriteBuffer md;
        md.varint(creeper.entityId);
        // metadata update for charged creeper (index 17, Yarn CreeperEntity CHARGED Boolean)
        meta::writeMetaBool(md, 17, true);
        md.u8(255);
        std::vector<Player*> previouslyTracking;
        const auto viewers = playersSnapshot();
        for (const auto& viewer : viewers) {
            if (!viewer) continue;
            std::lock_guard playerLock(viewer->stateMtx);
            if (viewer->entityTracking.tracksEntity(creeper.entityId))
                previouslyTracking.push_back(viewer.get());
        }
        broadcastMobPositionUpdate(*creeper.mob,
                                   pl::sc::SetEntityMetadata, md);
        // sendMobSpawnToViewer publishes the vanilla spawn/equipment state;
        // append the just-committed charged flag for viewers newly entering
        // range, while already-tracking viewers received the update above.
        for (const auto& viewer : viewers) {
            if (!viewer || std::find(previouslyTracking.begin(),
                                     previouslyTracking.end(),
                                     viewer.get()) != previouslyTracking.end())
                continue;
            bool nowTracking = false;
            std::shared_ptr<Connection> connection;
            {
                std::lock_guard playerLock(viewer->stateMtx);
                nowTracking = viewer->entityTracking.tracksEntity(
                    creeper.entityId);
                connection = viewer->conn;
            }
            if (nowTracking && connection &&
                !connection->trySendPacketLowPriority(
                    pl::sc::SetEntityMetadata, md))
                connection->close();
        }
        std::fprintf(stderr,
                     "[cppfm] creeper %d charged via lightning at %.1f %.1f %.1f\n",
                     creeper.entityId, x, y, z);
    }
    // Also handle Enderman damage via lightning? vanilla: enderman takes damage but teleports – already via applyDamage.
}
bool GameServer::applyDamageToMob(MobEntity& m, float amount,
                                  const DamageSource& src, int breachLv,
                                  SimulationDispatchGuard* simulationLock) {
    if (mobStateLockOwnedByCurrentThread()) {
        const DamageSource source = src;
        bool applied = false;
        runWithoutMobStateLock(
            [this, &m, amount, source, breachLv, simulationLock, &applied] {
                applied = applyDamageToMob(
                    m, amount, source, breachLv, simulationLock);
            });
        return applied;
    }
    if (amount <= 0) return false;
    if (!SimulationDispatchGuard::currentThreadOwns(
            &simulationDispatchMtx_)) {
        SimulationDispatchGuard mutationGate(simulationDispatchMtx_);
        return applyDamageToMob(m, amount, src, breachLv, &mutationGate);
    }
    if (!containsMob(&m)) return false;

    // Fabric callbacks are user code.  In particular, an allow-damage
    // callback may call back into the native bridge and mutate this entity.
    // Mob state is protected independently from vector ownership.  The
    // caller owns the entity lifetime (normally through a shared snapshot),
    // so taking entsMtx_ here would only add an avoidable lock-order edge.
    bool livingTarget = false;
    {
        std::lock_guard entityLock(*m.stateMtx);
        if (m.dead || m.health <= 0.0 || m.deathHandlingClaimed ||
            m.removalRequest != MobEntity::RemovalRequest::None)
            return false;
        livingTarget = isLivingDamageTarget(m.kind);
    }
    if (jvmRuntime_ && livingTarget) {
        const auto invokeDamageHook = [&] {
                return jvmRuntime_->onEntityDamage(
                    nullptr, &m, amount, src.type);
        };
        const bool allowed = simulationLock
            ? simulationLock->withLockReleased(invokeDamageHook)
            : SimulationDispatchGuard::withCurrentLockReleased(
                  invokeDamageHook);
        if (!allowed) return false;
    }
    // The callback ran without the global mutation gate. It may have removed
    // this entity from the authoritative roster; the shared_ptr held by the
    // caller keeps the object alive but does not make it an active world entity.
    if (!containsMob(&m)) return false;
    {
        std::lock_guard entityLock(*m.stateMtx);
        if (m.dead || m.health <= 0.0 || m.deathHandlingClaimed ||
            m.removalRequest != MobEntity::RemovalRequest::None)
            return false;
    }

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
        float damageTaken = 0.0F;
        bool dead = false;
        bool pendingDeath = false;
    } result;
    struct DeathReservation {
        MobEntity& mob;
        bool active = false;

        ~DeathReservation() {
            if (!active) return;
            std::lock_guard entityLock(*mob.stateMtx);
            mob.deathHandlingClaimed = false;
        }
    } deathReservation{m};

    {
        // No JVM, AI, packet, or world callback is made while the field lock
        // is held; those paths may re-enter the server.  Vector membership is
        // owned by entsMtx_ and is intentionally not coupled to this lock.
        std::lock_guard entityLock(*m.stateMtx);
        if (m.dead || m.health <= 0.0 || m.deathHandlingClaimed ||
            m.removalRequest != MobEntity::RemovalRequest::None)
            return false;

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
            if (finalAmt <= 0) return false;
            // Preserve the amount that reaches armor/enchantment mitigation.
            // Armadillo's rolled-up reduction is modeled before that point;
            // shield and extra-freezing stages are not modeled by this native
            // entry and thus remain an explicit Fabric parity limitation.
            result.damageTaken = effectiveAmount;
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
            result.pendingDeath = m.health <= 0.0;
            if (result.pendingDeath) {
                // Reserve death handling while ALLOW_DEATH runs without the
                // mutation gate. Tick/removal paths must not consume the
                // entity before the callback decides whether death commits.
                m.health = 0.0;
                m.dead = false;
                m.deathHandlingClaimed = true;
                deathReservation.active = true;
            }
            result.applied = true;
            if (result.boss) result.bossSnapshot = m;
        }
    }

    if (result.pendingDeath) {
        bool allowDeath = true;
        if (jvmRuntime_ && livingTarget) {
            const auto invokeAllowDeath = [&] {
                return jvmRuntime_->onEntityAllowDeath(
                    nullptr, &m, amount, src.type);
            };
            allowDeath = simulationLock
                ? simulationLock->withLockReleased(invokeAllowDeath)
                : SimulationDispatchGuard::withCurrentLockReleased(
                      invokeAllowDeath);
        }
        if (!containsMob(&m)) return false;
        {
            std::lock_guard entityLock(*m.stateMtx);
            const auto resolution = death_transition::resolve(
                allowDeath, static_cast<float>(m.health));
            if (resolution != death_transition::Resolution::Commit) {
                // A veto keeps the transition pending even when health is
                // non-positive. ALLOW_DEATH never heals implicitly.
                m.dead = false;
                m.deathHandlingClaimed = false;
                deathReservation.active = false;
                if (m.health <= 0.0) {
                    m.pendingDeathTick = tickNo_ + 1;
                    m.pendingDeathCause = src.type;
                } else {
                    m.pendingDeathTick = -1;
                    m.pendingDeathCause.clear();
                }
            } else {
                m.pendingDeathTick = -1;
                m.pendingDeathCause.clear();
                m.health = 0.0;
                m.dead = true;
                result.dead = true;
                // Retain the reservation through AFTER_DAMAGE/AFTER_DEATH;
                // the normal caller may claim removal once this function
                // returns.
            }
            if (result.boss) result.bossSnapshot = m;
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
        return false;
    }
    if (!result.applied) return false;

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
    WriteBuffer hurt;
    hurt.varint(result.entityId);
    hurt.f32(yaw);
    if (result.dead) {
        sendPacketToTrackedEntityViewers(
            playersSnapshot(), result.entityId, pl::sc::HurtAnimation, hurt);
    } else {
        broadcastMobPositionUpdate(m, pl::sc::HurtAnimation, hurt);
    }
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
    if (jvmRuntime_ && livingTarget) {
        const auto damage = makeAfterDamageAmounts(
            amount, result.damageTaken);
        const auto invokeDamageApplied = [&] {
            jvmRuntime_->onEntityDamageApplied(
                nullptr, &m, damage.baseDamageTaken, damage.damageTaken,
                damage.blocked,
                result.dead, src.type);
        };
        if (simulationLock)
            simulationLock->withLockReleased(invokeDamageApplied);
        else
            SimulationDispatchGuard::withCurrentLockReleased(
                invokeDamageApplied);
    }
    return true;
}
bool GameServer::applyDamageToMob(MobEntity& m, float amount, const char* cause) {
    DamageSource src(cause ? std::string(cause) : std::string("generic"));
    return applyDamageToMob(m, amount, src);
}
void GameServer::sendActionBar(Player& p, const std::string& text) {
    const auto connection = snapshotPlayerConnection(p);
    if (!connection) return;
    WriteBuffer b; nbt::writeTextComponent(b, text);
    connection->trySendPacket(proto::pl::sc::ActionBar, b);
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
void GameServer::broadcastHurtAnimationFor(std::int8_t dimension,
                                           int32_t entityId, float yaw,
                                           Player* except) {
    if (!std::isfinite(yaw)) yaw = 0;
    WriteBuffer b; b.varint(entityId); b.f32(yaw);
    if (const auto player = playerEntityInDimension(*this, entityId, dimension)) {
        if (player.get() != except)
            sendPlayerEntityPacketToSelfIfInPlay(
                *player, proto::pl::sc::HurtAnimation, b);
        broadcastTrackedPlayerPacket(*player, proto::pl::sc::HurtAnimation,
                                     b, except);
        return;
    }
    broadcastPacketExceptInDimension(dimension, except,
                                     proto::pl::sc::HurtAnimation, b);
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
    if (const auto player = playerEntityInDimension(*this, entityId, dimension)) {
        sendPlayerEntityPacketToSelfIfInPlay(
            *player, proto::pl::sc::EntitySoundEffect, b);
        broadcastTrackedPlayerPacket(
            *player, proto::pl::sc::EntitySoundEffect, b);
        return;
    }
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
    const WriteBuffer body = makeSyncEntityPositionBody(
        entityId, x, y, z, dx, dy, dz, yaw, pitch, onGround);
    connection->trySendPacket(proto::pl::sc::SyncEntityPosition, body);
}
void GameServer::sendSyncEntityPosition(Player& p, const MobEntity& mob) {
    const auto state = snapshotMobCombat(mob);
    sendSyncEntityPosition(p, state.entityId, state.x, state.y, state.z,
                           state.velocityX, state.velocityY,
                           state.velocityZ, state.yaw, state.pitch, true);
}
void GameServer::broadcastSyncEntityPosition(const MobEntity& mob,
                                            Player* except,
                                            const bool onGround) {
    const auto state = snapshotMobCombat(mob);
    const WriteBuffer b = makeSyncEntityPositionBody(
        state.entityId, state.x, state.y, state.z, state.velocityX,
        state.velocityY, state.velocityZ, state.yaw, state.pitch, onGround);
    if (!except)
        broadcastMobPositionUpdate(mob, proto::pl::sc::SyncEntityPosition, b);
    else
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
void GameServer::broadcastMoveMinecart(std::int32_t entityId, double x, double y, double z, float yaw, float pitch, Player* except) {
    std::shared_ptr<MobEntity> minecart;
    for (const auto& candidate : mobsSnapshot()) {
        if (!candidate) continue;
        const auto state = snapshotMobCombat(*candidate);
        if (state.entityId != entityId) continue;
        minecart = candidate;
        break;
    }
    if (!minecart) return;

    WriteBuffer b;
    b.varint(entityId);
    b.varint(1);
    b.f32((float)x); b.f32((float)y); b.f32((float)z);
    b.f32(0.f); b.f32(0.f); b.f32(0.f);
    b.f32(yaw); b.f32(pitch); b.f32(1.f);
    broadcastMobPositionUpdate(*minecart, proto::pl::sc::MoveMinecart, b,
                               except);
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
} // namespace cppfm
