#include "GameServer.hpp"
#include "Session.hpp"
#include "SessionEntitySupport.hpp"
#include "GameServerHelpers.hpp"
#include "DamageComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "CombatManager.hpp"
#include "MeleeHelper.hpp"
#include "Particles.hpp"
#include "../generated/BlockStates.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace cppfm {
using namespace proto;
using namespace session_entity_detail;

struct Session::EntityAttackPlan {
    struct Attacker {
        std::int32_t entityId = 0;
        std::int8_t dimension = 0;
        std::uint8_t gamemode = 0;
        std::int32_t heldSlot = 0;
        std::shared_ptr<Connection> connection;
        std::string name;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double fallDistance = 0.0;
        bool onGround = false;
        bool sprinting = false;
        bool riding = false;
        std::int32_t attackCooldownTicks = 0;
        std::vector<EffectInstance> effects;
    } attacker;
    ItemStack weapon;
    float damage = 1.0F;
    bool crit = false;
    bool doSweep = false;
    bool weaponIsAxe = false;
    bool weaponIsMace = false;
    int breachLevel = 0;
    int fireAspectLevel = 0;
    int flameLevel = 0;
    int punchLevel = 0;
    int knockbackLevel = 0;
};

struct Session::EntityAttackMobResult {
    bool hit = false;
    bool killed = false;
    bool rejected = false;
    float uncriticalDamage = 0.0F;
    std::shared_ptr<MobEntity> victim;
    std::shared_ptr<MobEntity> target;
    SessionMobSnapshot state;
};

Session::EntityAttackPlan Session::prepareEntityAttackPlan() {
    EntityAttackPlan plan;
    auto& attacker = plan.attacker;
    {
        std::lock_guard playerLock(self_->stateMtx);
        attacker.entityId = self_->entityId;
        attacker.dimension = GameServer::canonicalDimension(self_->dimension);
        attacker.gamemode = self_->gamemode;
        attacker.heldSlot = self_->heldSlot;
        attacker.connection = self_->conn;
        attacker.name = self_->name;
        attacker.x = self_->x;
        attacker.y = self_->y;
        attacker.z = self_->z;
        attacker.fallDistance = self_->fallDist;
        attacker.onGround = self_->onGround;
        attacker.sprinting = self_->isSprinting;
        attacker.riding = self_->vehicleId != -1;
        attacker.attackCooldownTicks = self_->attackCooldownTicks;
        attacker.effects = self_->effects;
        if (attacker.heldSlot >= 0 && attacker.heldSlot < 9)
            plan.weapon = self_->inv[36 + attacker.heldSlot];
        // A swing consumes exhaustion and resets the attack/shield state as a
        // single player-state transaction.  No callback or packet is reached
        // while this lock is held.
        srv_.addHungerExhaustion(*self_, 0.1f);
        self_->attackCooldownTicks = 0;
        self_->isBlocking = false;
        self_->blockingTicks = 0;
    }

    const ItemStack& weaponStack = plan.weapon;
    // PvP keeps its sharpness calculation; mob-specific enchantments are
    // applied later, after the target kind is known.
    const float baseDamage = weaponStack.empty()
        ? 1.f
        : EnchantmentHelper::meleeDamageWithEnchant(
              meleeWeaponBaseDamage(weaponStack.name()), weaponStack);
    plan.damage = baseDamage + meleeDamageBonusFor(attacker.effects);
    // E-06 wire compat: base mapping above is untouched; crit/sweep only scale
    // the result.
    const bool charged = isChargedAttack(attacker.attackCooldownTicks);
    const bool falling = !attacker.onGround && attacker.fallDistance > 0;
    const bool weaponIsSword = !weaponStack.empty() &&
                               isSwordItem(weaponStack.name());
    plan.weaponIsAxe = !weaponStack.empty() &&
                       isAxeItem(weaponStack.name());
    plan.weaponIsMace = !weaponStack.empty() &&
                        isMaceItem(weaponStack.name());
    bool attackerInWater = false;
    {
        const std::uint16_t blockState = srv_.worldFor(attacker.dimension).getBlock(
            static_cast<int>(std::floor(attacker.x)),
            static_cast<int>(std::floor(attacker.y)),
            static_cast<int>(std::floor(attacker.z)));
        if (const auto* block = gen::blockByState(blockState))
            attackerInWater = std::string(block->name).find("water") !=
                              std::string::npos;
    }
    plan.crit = isCritAttack(attacker.onGround, falling, attacker.sprinting,
                             charged, attackerInWater, false,
                             attacker.riding, false);
    plan.doSweep = isSweepAttack(weaponIsSword, attacker.onGround,
                                 attacker.sprinting, charged);
    plan.breachLevel = weaponStack.empty()
        ? 0 : EnchantmentHelper::getBreach(weaponStack);
    plan.fireAspectLevel = weaponStack.empty()
        ? 0 : EnchantmentHelper::getFireAspect(weaponStack);
    plan.flameLevel = weaponStack.empty()
        ? 0 : EnchantmentHelper::getFlame(weaponStack);
    plan.punchLevel = weaponStack.empty()
        ? 0 : EnchantmentHelper::getPunch(weaponStack);
    plan.knockbackLevel = weaponStack.empty()
        ? 0 : EnchantmentHelper::getKnockback(weaponStack);
    return plan;
}

void Session::handleEntityAttack(
    const std::int32_t target, SimulationDispatchGuard& simulationLock) {
    const auto plan = prepareEntityAttackPlan();
    if (handlePlayerEntityAttack(target, plan, simulationLock)) return;
    handleMobEntityAttack(target, plan, simulationLock);
}

bool Session::handlePlayerEntityAttack(
    const std::int32_t target, const EntityAttackPlan& plan,
    SimulationDispatchGuard& simulationLock) {
    const auto& attacker = plan.attacker;
    const auto& weaponStack = plan.weapon;
    const float dmg = plan.damage;
    const bool crit = plan.crit;
    const bool weaponIsAxe = plan.weaponIsAxe;
    const bool weaponIsMace = plan.weaponIsMace;
    const int breachLv = plan.breachLevel;
    const int fireAspectLv = plan.fireAspectLevel;
    auto broadcastCritParticles = [&](double x, double y, double z) {
        if (!crit) return;
        WriteBuffer pb = makeWorldParticlesBody(x, y + 1.0, z, 0.2f, 0.2f, 0.2f, 0.3f, 8,
                                                ParticleId::crit, {}, false, false);
        srv_.broadcastPacketExceptInDimension(attacker.dimension, nullptr,
                                              pl::sc::WorldParticles, pb);
    };

    // ---- PVP: check player victims first (items 76-80 combat)
    for (auto &pp : srv_.playersSnapshot()) {
        auto *victimP = pp.get();
        if (!victimP) continue;
        std::int8_t victimDimension = 0;
        double victimTargetX = 0.0, victimTargetY = 0.0, victimTargetZ = 0.0;
        {
            std::lock_guard victimLock(victimP->stateMtx);
            if (victimP->entityId != target || victimP->dead ||
                GameServer::canonicalDimension(victimP->dimension) !=
                    attacker.dimension)
                continue;
            victimDimension = GameServer::canonicalDimension(victimP->dimension);
            victimTargetX = victimP->x;
            victimTargetY = victimP->y;
            victimTargetZ = victimP->z;
        }
        if (victimP == self_.get()) return true; // self-hit ignore
        if (!withinEntityInteractionRange(
                attacker.x, attacker.y, attacker.z, attacker.gamemode,
                victimTargetX, victimTargetY, victimTargetZ))
            return true;
        if (!srv_.config().pvp) {
            // PvP disabled: suppress damage, optionally notify
            return true;
        }
        float pvpDmg = dmg;
        if (crit) pvpDmg = applyCrit(pvpDmg); // plan44 G-07: falling x1.5
        if (weaponIsMace) // plan44 G-09: density smash bonus also applies in PVP
            pvpDmg += EnchantmentHelper::densityBonus(
                static_cast<int>(std::floor(attacker.fallDistance)), weaponStack);
        {
            DamageSource psrc("player");
            if (CombatManager::tryShieldBlock(
                    srv_, *victimP, psrc, attacker.x, attacker.z,
                    weaponIsAxe, pvpDmg))
                return true;
        }
        DamageSource psrc2("player");
        {
            std::lock_guard victimLock(victimP->stateMtx);
            if (victimP->dead || GameServer::canonicalDimension(victimP->dimension) !=
                                    attacker.dimension)
                return true;
        }
        const bool damaged = srv_.applyDamage(
            *victimP, pvpDmg, psrc2, breachLv, &simulationLock);
        double victimX = 0.0, victimY = 0.0, victimZ = 0.0;
        std::shared_ptr<Connection> victimConnection;
        std::int32_t victimEntityId = target;
        {
            std::lock_guard victimLock(victimP->stateMtx);
            if (GameServer::canonicalDimension(victimP->dimension) !=
                    attacker.dimension)
                return true;
            if (damaged && fireAspectLv > 0 && !victimP->dead)
                victimP->fireTicks = 100; // fire aspect ignites PVP victims
            victimX = victimP->x;
            victimY = victimP->y;
            victimZ = victimP->z;
            victimConnection = victimP->conn;
            victimEntityId = victimP->entityId;
        }
        if (damaged) broadcastCritParticles(victimX, victimY, victimZ);
        if (damaged)
            CombatManager::applyThornsReflection(srv_, *victimP, nullptr,
                                                  self_.get());
        if (damaged) {
            // A vetoed AllowDamage event cancels the attack's knockback too;
            // do not turn a rejected hit into a velocity-only hit.
            double dx = victimX - attacker.x;
            double dz = victimZ - attacker.z;
            double len = std::sqrt(dx*dx + dz*dz);
            if (len < 0.01) {
                dx = nextRandom() / static_cast<double>(RAND_MAX) - 0.5;
                dz = nextRandom() / static_cast<double>(RAND_MAX) - 0.5;
                len = std::sqrt(dx*dx + dz*dz);
            }
            const double nx = dx / len;
            const double nz = dz / len;
            WriteBuffer vel;
            vel.varint(victimEntityId);
            vel.i16(static_cast<std::int16_t>(nx * 400));
            vel.i16(static_cast<std::int16_t>(300));
            vel.i16(static_cast<std::int16_t>(nz * 400));
            if (victimConnection)
                victimConnection->trySendPacket(pl::sc::EntityVelocity, vel);
            srv_.broadcastPacketExceptInDimension(victimDimension, victimP,
                                                  pl::sc::EntityVelocity, vel);
        }
        return true;
    }
    return false;
}

Session::EntityAttackMobResult Session::hitMobTarget(
    const std::int32_t target, const EntityAttackPlan& plan,
    SimulationDispatchGuard& simulationLock) {
    EntityAttackMobResult result;
    const auto& attacker = plan.attacker;
    const auto& weaponStack = plan.weapon;
    float mobDamage = plan.damage;

    for (const auto& mob : srv_.mobsSnapshot()) {
        if (!mob) continue;
        const SessionMobSnapshot before = snapshotMobForSession(*mob);
        if (before.entityId != target || before.dead ||
            before.dimension != attacker.dimension)
            continue;
        if (!withinEntityInteractionRange(
                attacker.x, attacker.y, attacker.z, attacker.gamemode,
                before.x, before.y, before.z, before.kind, before.slimeSize)) {
            result.rejected = true;
            return result;
        }

        if (!weaponStack.empty()) {
            // Recompute with the target kind for smite/bane without retaining
            // the Mob lock across damage callbacks.
            mobDamage = EnchantmentHelper::meleeDamageWithEnchant(
                meleeWeaponBaseDamage(weaponStack.name()), weaponStack,
                before.kind);
            mobDamage += meleeDamageBonusFor(attacker.effects);
            mobDamage += EnchantmentHelper::impalingBonusFor(
                weaponStack, before.kind);
            if (plan.weaponIsMace)
                mobDamage += EnchantmentHelper::densityBonus(
                    static_cast<int>(std::floor(attacker.fallDistance)),
                    weaponStack);
        }

        result.uncriticalDamage = mobDamage;
        if (plan.crit) mobDamage = applyCrit(mobDamage);
        const bool damageApplied = srv_.applyDamageToMob(
            *mob, mobDamage, DamageSource("player"), plan.breachLevel,
            &simulationLock);
        SessionMobSnapshot after = snapshotMobForSession(*mob);
        if (after.entityId != target || after.dimension != attacker.dimension)
            continue;

        if (damageApplied && plan.flameLevel > 0 && !after.dead) {
            {
                std::lock_guard entityLock(*mob->stateMtx);
                if (mob->entityId == target && !mob->dead)
                    mob->onFireTicks = 100;
            }
            after = snapshotMobForSession(*mob);
            srv_.broadcastSoundFor(after.dimension,
                                   "minecraft:item.firecharge.use",
                                   after.x, after.y, after.z, 1.0F, 1.0F,
                                   "block");
        }
        if (damageApplied)
            srv_.noteMobHurt(after.entityId, attacker.entityId,
                             EnchantmentHelper::getLooting(weaponStack));

        result.hit = damageApplied;
        if (damageApplied) {
            result.target = mob;
            result.state = after;
        }
        if (damageApplied && after.dead) {
            result.killed = true;
            result.victim = mob;
        }
        return result;
    }
    return result;
}

void Session::applyMobSweep(const EntityAttackPlan& plan,
                            const EntityAttackMobResult& result,
                            SimulationDispatchGuard& simulationLock) {
    const auto& attacker = plan.attacker;
    const auto& weapon = plan.weapon;
    const int sweepLevel = weapon.empty()
        ? 0 : EnchantmentHelper::getSweepingEdge(weapon);
    const float sweepDamage = sweepingEdgeDamage(
        result.uncriticalDamage, sweepLevel);
    struct SplashTarget {
        std::shared_ptr<MobEntity> mob;
        SessionMobSnapshot state;
    };
    std::vector<SplashTarget> splash;
    for (const auto& mob : srv_.mobsSnapshot()) {
        if (!mob) continue;
        const SessionMobSnapshot state = snapshotMobForSession(*mob);
        if (state.entityId == result.state.entityId || state.dead ||
            state.dimension != attacker.dimension)
            continue;
        if (!withinEntityInteractionRange(
                attacker.x, attacker.y, attacker.z, attacker.gamemode,
                state.x, state.y, state.z, state.kind, state.slimeSize))
            continue;
        if (inSweepRange(result.state.x, result.state.y, result.state.z,
                         state.x, state.y, state.z))
            splash.push_back({mob, state});
    }

    DamageSource source("player");
    for (auto& candidate : splash) {
        const bool damageApplied = srv_.applyDamageToMob(
            *candidate.mob, sweepDamage, source, plan.breachLevel,
            &simulationLock);
        SessionMobSnapshot state = snapshotMobForSession(*candidate.mob);
        if (!damageApplied) continue;
        srv_.noteMobHurt(state.entityId, attacker.entityId,
                         EnchantmentHelper::getLooting(weapon));
        if (state.entityId != candidate.state.entityId ||
            state.dimension != attacker.dimension || state.dead)
            continue;
        if (plan.fireAspectLevel > 0 || plan.flameLevel > 0) {
            std::lock_guard entityLock(*candidate.mob->stateMtx);
            if (!candidate.mob->dead)
                candidate.mob->onFireTicks = 100;
            state = snapshotMobForSession(*candidate.mob);
        }
        double dx = state.x - attacker.x;
        double dz = state.z - attacker.z;
        double length = std::sqrt(dx * dx + dz * dz);
        if (length < 0.01) {
            dx = 0.5;
            dz = 0.0;
            length = 0.5;
        }
        WriteBuffer velocity;
        velocity.varint(state.entityId);
        velocity.i16(static_cast<std::int16_t>(dx / length * 400));
        velocity.i16(300);
        velocity.i16(static_cast<std::int16_t>(dz / length * 400));
        srv_.broadcastPacketExceptInDimension(
            state.dimension, nullptr, pl::sc::EntityVelocity, velocity);
    }

    if (splash.empty()) return;
    WriteBuffer particles = makeWorldParticlesBody(
        result.state.x, result.state.y + 1.0, result.state.z,
        0.3F, 0.2F, 0.3F, 0.2F, 6, ParticleId::sweep_attack, {}, false,
        false);
    srv_.broadcastPacketExceptInDimension(
        result.state.dimension, nullptr, pl::sc::WorldParticles, particles);
    srv_.broadcastSoundFor(
        result.state.dimension, "minecraft:entity.player.attack.sweep",
        result.state.x, result.state.y, result.state.z, 1.0F, 1.0F,
        "player");
}

void Session::applyMobKnockback(
    const EntityAttackPlan& plan, const EntityAttackMobResult& result) {
    if (!result.hit || !result.target) return;
    const auto& attacker = plan.attacker;
    const SessionMobSnapshot target = snapshotMobForSession(*result.target);
    double dx = target.x - attacker.x;
    double dz = target.z - attacker.z;
    double length = std::sqrt(dx * dx + dz * dz);
    if (length < 0.01) {
        dx = static_cast<double>(nextRandom()) / RAND_MAX - 0.5;
        dz = static_cast<double>(nextRandom()) / RAND_MAX - 0.5;
        length = std::sqrt(dx * dx + dz * dz);
    }
    const double nx = dx / length;
    const double nz = dz / length;
    const float extraForce = 0.4F * static_cast<float>(
        plan.punchLevel + plan.knockbackLevel) +
        (plan.punchLevel > 0 ? 0.5F : 0.0F);
    int horizontal = 400 + static_cast<int>(extraForce * 8000 * 0.05);
    if (extraForce > 0.0F)
        horizontal = static_cast<int>(400 + extraForce * 400);

    WriteBuffer velocity;
    velocity.varint(target.entityId);
    velocity.i16(static_cast<std::int16_t>(nx * horizontal));
    velocity.i16(static_cast<std::int16_t>(300 +
                                           (extraForce > 0.0F ? 100 : 0)));
    velocity.i16(static_cast<std::int16_t>(nz * horizontal));
    srv_.broadcastPacketExceptInDimension(
        target.dimension, nullptr, pl::sc::EntityVelocity, velocity);

    if (extraForce <= 0.0F) return;
    std::lock_guard entityLock(*result.target->stateMtx);
    if (result.target->entityId != target.entityId || result.target->dead)
        return;
    result.target->velX += nx * extraForce * 0.3;
    result.target->velZ += nz * extraForce * 0.3;
    result.target->velY += 0.1;
}

void Session::finishMobKill(
    const std::int32_t target, const EntityAttackPlan& plan,
    const EntityAttackMobResult& result,
    SimulationDispatchGuard& simulationLock) {
    if (!result.killed || !result.victim ||
        !result.victim->tryClaimDeathHandling())
        return;
    const SessionMobSnapshot death = snapshotMobForSession(*result.victim);
    if (death.entityId != target || !death.dead) return;

    WriteBuffer remove;
    remove.varint(1);
    remove.varint(target);
    srv_.broadcastPacketExceptInDimension(
        death.dimension, nullptr, pl::sc::RemoveEntities, remove);
    srv_.onMobKilledBy(*self_, death.kind);
    srv_.scoreboard.addScore("kills", plan.attacker.name, 1);
    srv_.sendScoreAll(
        "kills", plan.attacker.name,
        srv_.scoreboard.getScore("kills", plan.attacker.name));

    auto lootContext = srv_.mobLootContextForDeath(
        death.entityId, death.entityOnFire);
    lootContext.wasKilledByPlayer = true;
    lootContext.lootingLevel = EnchantmentHelper::getLooting(plan.weapon);
    srv_.spawnMobLootFor(death.kind, death.dimension, death.x,
                         death.y + 0.4, death.z, lootContext);
    srv_.spawnXpOrbsFor(death.dimension, death.x, death.y + 0.5,
                        death.z, mobStats(death.kind).xpDrop, self_.get());

    if ((death.kind == MobKind::Slime || death.kind == MobKind::MagmaCube) &&
        death.slimeSize > 0) {
        std::vector<std::shared_ptr<MobEntity>> babies;
        const int count = 2 + (nextRandom() % 3);
        for (int index = 0; index < count; ++index) {
            auto baby = std::make_shared<MobEntity>();
            baby->entityId = srv_.nextEntityId();
            baby->kind = death.kind;
            baby->dimension = death.dimension;
            baby->slimeSize = death.slimeSize - 1;
            baby->health = std::max(
                1.0F, MobEntity::slimeHealthForSize(baby->slimeSize));
            baby->x = death.x +
                (static_cast<double>(nextRandom()) / RAND_MAX - 0.5) * 0.5;
            baby->y = death.y;
            baby->z = death.z +
                (static_cast<double>(nextRandom()) / RAND_MAX - 0.5) * 0.5;
            baby->lastSeenMs = 0;
            if (auto* runtime = srv_.jvmRuntime(); runtime &&
                !simulationLock.withLockReleased([&] {
                    return runtime->onMobSpawn(
                        *baby, baby->x, baby->y, baby->z);
                })) continue;
            babies.push_back(std::move(baby));
        }
        for (auto& baby : babies) srv_.addMob(baby);
        for (auto& baby : babies) srv_.broadcastMobSpawn(*baby);
    }

    srv_.eraseMobAi(target);
    srv_.removeMob(result.victim);
    bool wasVehicle = false;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (self_->vehicleId == target) {
            self_->vehicleId = -1;
            wasVehicle = true;
        }
    }
    if (wasVehicle)
        srv_.broadcastSetPassengersEmptyFor(death.dimension, target);
}

void Session::handleMobEntityAttack(
    const std::int32_t target, const EntityAttackPlan& plan,
    SimulationDispatchGuard& simulationLock) {
    const auto& attacker = plan.attacker;
    const auto& weaponStack = plan.weapon;
    const auto result = hitMobTarget(target, plan, simulationLock);
    if (result.rejected) return;
    if (result.hit && result.target) {
        if (plan.doSweep) applyMobSweep(plan, result, simulationLock);
        if (plan.weaponIsMace && attacker.fallDistance > 1.5) {
            int wb = weaponStack.empty() ? 0 : EnchantmentHelper::getWindBurst(weaponStack);
            if (wb > 0) {
                WriteBuffer wv;
                wv.varint(attacker.entityId);
                wv.i16(0);
                wv.i16(static_cast<std::int16_t>(windBurstLaunchVy(wb) * 8000));
                wv.i16(0);
                if (attacker.connection)
                    attacker.connection->trySendPacket(pl::sc::EntityVelocity, wv);
                srv_.broadcastPacketExceptInDimension(attacker.dimension,
                                                      self_.get(),
                                                      pl::sc::EntityVelocity, wv);
            }
        }
    }
    bool inventoryChanged = false;
    if (attacker.heldSlot >= 0 && attacker.heldSlot < 9) {
        std::lock_guard playerLock(self_->stateMtx);
        auto& held = self_->inv[36 + attacker.heldSlot];
        if (self_->heldSlot == attacker.heldSlot && !held.empty() &&
            ItemStack::maxDamageFor(held.itemId) > 0) {
            if (DamageComponent::applyDamage(held, 1)) held = ItemStack::air();
            inventoryChanged = true;
        }
    }
    if (inventoryChanged) srv_.resendInventory(*self_);
    applyMobKnockback(plan, result);
    finishMobKill(target, plan, result, simulationLock);
}

} // namespace cppfm
