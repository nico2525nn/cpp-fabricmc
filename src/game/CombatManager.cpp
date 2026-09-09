#include "CombatManager.hpp"
#include "GameServer.hpp"
#include "Entities.hpp"
#include "Attributes.hpp"
#include "EnchantmentHelper.hpp"
#include "MeleeHelper.hpp"
#include "DamageComponent.hpp"
#include "../generated/ItemIds.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>

namespace cppfm {

namespace {

// resendInventory() owns its state snapshot and releases stateMtx before it
// enters Connection::trySendPacket().  In particular, do not put a caller
// lock around this helper: doing so would re-introduce a player-lock/transport
// cycle even though resendInventory() itself is careful about lock scope.
void resendInventoryIfConnected(GameServer& srv, Player& player) {
    srv.resendInventory(player);
}

} // namespace

int CombatManager::armorForItem(uint32_t itemId) {
    return armorPointsForItem(itemId);
}

int CombatManager::totalArmorForPlayer(const Player& p) {
    std::lock_guard playerLock(p.stateMtx);
    return totalArmorPoints(p.inv);
}

int CombatManager::totalArmorForMob(const MobEntity& m) {
    std::lock_guard entityLock(*m.stateMtx);
    return totalArmorPoints(m);
}

int CombatManager::computeEPF(const DamageSource& ds, const Player& p) {
    if (ds.bypassEnchant || ds.isDrown() || ds.isStarveFlag || ds.isSonic()) return 0;
    std::lock_guard playerLock(p.stateMtx);
    int total = 0;
    for (int i = 5; i <= 8; ++i) {
        if (i < 0 || i >= 46 || p.inv[i].empty()) continue;
        total += EnchantmentHelper::getProtectionEPF(ds, p.inv[i]);
    }
    if (total > 20) total = 20;
    return total;
}

int CombatManager::computeEPF(const DamageSource& ds, const MobEntity& m) {
    if (ds.bypassEnchant || ds.isDrown() || ds.isStarveFlag || ds.isSonic()) return 0;
    std::lock_guard entityLock(*m.stateMtx);
    int total = 0;
    for (int i = 2; i < 6; ++i) {
        if (m.equipment[i].empty()) continue;
        total += EnchantmentHelper::getProtectionEPF(ds, m.equipment[i]);
    }
    if (total > 20) total = 20;
    return total;
}

float CombatManager::calculatePlayerDamage(float base, const DamageSource& src,
                                          int armor, double toughness, int epf,
                                          const std::vector<EffectInstance>& effects) {
    return DamageCalculator::calculate(base, src, armor, toughness, epf, effects);
}

float CombatManager::calculateMobDamage(float base, const DamageSource& src,
                                       int armor, double toughness, int epf,
                                       const std::vector<EffectInstance>& effects) {
    return DamageCalculator::calculate(base, src, armor, toughness, epf, effects);
}

void CombatManager::syncPlayerArmor(GameServer& srv, Player& p) {
    std::shared_ptr<Connection> connection;
    WriteBuffer attributes;
    std::int8_t dimension = 0;
    bool sendUpdate = false;
    {
        std::lock_guard playerLock(p.stateMtx);
        const int armor = totalArmorPoints(p.inv);
        int toughness = 0;
        float kbResist = 0.f;
        for (int i = 5; i <= 8; ++i) {
            if (i < 0 || i >= 46 || p.inv[i].empty()) continue;
            const std::string n = p.inv[i].name();
            if (n.find("diamond_") != std::string::npos) toughness += 2;
            else if (n.find("netherite_") != std::string::npos) {
                toughness += 3;
                kbResist += 0.1f;
            }
        }

        const bool dirty = p.attributes.armorDirty(armor, toughness, kbResist);
        p.attributes.syncArmor(armor, toughness, kbResist);
        if (dirty && p.inPlay && p.conn) {
            connection = p.conn;
            dimension = p.dimension;
            p.attributes.writeUpdate(attributes, p.entityId);
            sendUpdate = true;
        }
    }

    // Attribute serialization is a model snapshot; transport and recipient
    // enumeration must happen after the Player lock is released.  The
    // shared_ptr keeps the selected connection alive across the send, while
    // the dimension is the value that was authoritative for this update.
    if (!sendUpdate || !connection) return;
    connection->trySendPacket(proto::pl::sc::UpdateAttributes, attributes);
    srv.broadcastPacketExceptInDimension(dimension, &p,
                                         proto::pl::sc::UpdateAttributes,
                                         attributes);
}

void CombatManager::applyToPlayer(GameServer& srv, Player& p, float amount, const DamageSource& src) {
    if (amount <= 0) return;
    {
        std::lock_guard playerLock(p.stateMtx);
        if (p.gamemode == 1 || p.gamemode == 3 || p.dead || p.health <= 0)
            return;
    }

    // Armor attributes can emit an update packet, so keep this operation
    // outside the damage mutation critical section.  Re-check the player
    // below because a concurrent hook or tick may have changed the state.
    syncPlayerArmor(srv, p);

    struct DamageResult {
        std::shared_ptr<Connection> connection;
        std::int32_t entityId = 0;
        std::int8_t dimension = 0;
        float health = 0.f;
        std::int32_t food = 0;
        float saturation = 0.f;
        bool shouldKill = false;
    } result;

    {
        std::lock_guard playerLock(p.stateMtx);
        if (p.gamemode == 1 || p.gamemode == 3 || p.dead || p.health <= 0)
            return;

        int armor = static_cast<int>(std::round(
            p.attributes.getValue(Attribute::ARMOR)));
        if (armor == 0) armor = totalArmorPoints(p.inv);
        const double toughness = p.attributes.getValue(Attribute::ARMOR_TOUGHNESS);
        int epf = 0;
        if (!src.bypassEnchant && !src.isDrown() && !src.isStarveFlag &&
            !src.isSonic()) {
            for (int i = 5; i <= 8; ++i) {
                if (p.inv[i].empty()) continue;
                epf += EnchantmentHelper::getProtectionEPF(src, p.inv[i]);
            }
            epf = std::min(epf, 20);
        }
        const float finalAmt = DamageCalculator::calculate(
            amount, src, armor, toughness, epf, p.effects);
        if (finalAmt <= 0) return;

        // HungerManager::addExhaustion is model-only today.  Keep the same
        // vanilla amount here without invoking a server/network callback
        // while the player lock is held.
        if (p.gamemode == 0) p.exhaustion += 0.10f;
        p.health -= finalAmt;
        p.hurtCooldown = 10;
        if (p.health <= 0) {
            p.health = 0;
            result.shouldKill = true;
        }
        result.connection = p.conn;
        result.entityId = p.entityId;
        result.dimension = p.dimension;
        result.health = p.health;
        result.food = p.food;
        result.saturation = p.saturation;
    }

    // killPlayer performs drops, hooks, persistence, and transport.  It is
    // deliberately called only after the state lock has been released.
    if (result.shouldKill) srv.killPlayer(p, src.type.c_str());
    if (!result.connection) return;

    WriteBuffer health;
    health.f32(result.health);
    health.varint(result.food);
    health.f32(result.saturation);
    result.connection->trySendPacket(proto::pl::sc::SetHealth, health);

    WriteBuffer damageEvent;
    damageEvent.varint(result.entityId);
    int damageTypeId = srv.gameData_.idOf(
        "minecraft:damage_type", std::string("minecraft:") + src.type);
    if (damageTypeId < 0)
        damageTypeId = srv.gameData_.idOf("minecraft:damage_type",
                                         "minecraft:generic");
    if (damageTypeId < 0) damageTypeId = 0;
    damageEvent.varint(damageTypeId);
    damageEvent.varint(0);
    damageEvent.varint(0);
    damageEvent.boolean(false);
    result.connection->trySendPacket(proto::pl::sc::DamageEvent, damageEvent);
    srv.broadcastPacketExceptInDimension(result.dimension, &p,
                                         proto::pl::sc::DamageEvent,
                                         damageEvent);
}

void CombatManager::applyToMob(GameServer& srv, MobEntity& m, float amount, const DamageSource& src) {
    (void)srv;
    if (amount <= 0) return;
    std::lock_guard entityLock(*m.stateMtx);
    if (m.dead) return;
    const int armor = totalArmorPoints(m);
    int epf = 0;
    if (!src.bypassEnchant && !src.isDrown() && !src.isStarveFlag &&
        !src.isSonic()) {
        for (int i = 2; i < 6; ++i) {
            if (m.equipment[i].empty()) continue;
            epf += EnchantmentHelper::getProtectionEPF(src, m.equipment[i]);
        }
        epf = std::min(epf, 20);
    }
    const float finalAmt = DamageCalculator::calculate(
        amount, src, armor, 0.0, epf, {});
    if (finalAmt <= 0) return;
    m.health -= finalAmt;
    m.hurtCooldown = 10;
    if (m.health <= 0) m.dead = true;
}

static ItemStack* shieldStackFor(Player& p) {
    if (p.heldSlot >= 0 && p.heldSlot < 9) {
        auto& mh = p.inv[36 + p.heldSlot];
        if (!mh.empty() && mh.name().find("shield") != std::string::npos) return &mh;
    }
    auto& off = p.inv[45];
    if (!off.empty() && off.name().find("shield") != std::string::npos) return &off;
    return nullptr;
}
bool CombatManager::tryShieldBlock(GameServer& srv, Player& victim, const DamageSource& src,
                                   double attackerX, double attackerZ, bool attackerWeaponIsAxe) {
    if (src.bypassShield || src.isMagic()) return false; // sonic/magic pierce (guardian beam direct only)
    std::int8_t dimension = 0;
    std::int32_t entityId = 0;
    bool blocked = false;
    {
        std::lock_guard playerLock(victim.stateMtx);
        if (!isShieldBlocking(victim)) return false;
        if (!isFrontal(victim, attackerX, attackerZ)) return false;
        ItemStack* shield = shieldStackFor(victim);
        if (!shield) return false;
        dimension = victim.dimension;
        entityId = victim.entityId;
        if (attackerWeaponIsAxe) {
            // vanilla: axe disables shield 100t (5s); the disabling blow deals damage
            victim.shieldDisableTicks = 100;
            victim.blockingTicks = 0;
            if (DamageComponent::applyDamage(*shield, 1)) *shield = ItemStack::air();
        } else {
            if (DamageComponent::applyDamage(*shield, 1)) *shield = ItemStack::air();
            blocked = true;
        }
    }

    // Neither resendInventory nor the sound broadcast is a damage callback;
    // perform them after the state mutation lock has been released so a
    // transport callback cannot participate in a player-lock cycle.
    resendInventoryIfConnected(srv, victim);
    srv.broadcastEntitySoundFor(
        dimension, entityId,
        attackerWeaponIsAxe ? "minecraft:item.shield.break"
                            : "minecraft:item.shield.block",
        1.f, 1.f, GameServer::SoundSource::Player);
    return blocked;
}
void CombatManager::applyThornsReflection(GameServer& srv, Player& victim,
                                          MobEntity* attackerMob, Player* attackerPlayer) {
    if (!attackerMob && !attackerPlayer) return;
    DamageSource thorns("thorns");
    for (int i = 5; i <= 8; ++i) {
        int reflected = 0;
        ItemStack expected;
        {
            std::lock_guard playerLock(victim.stateMtx);
            auto& piece = victim.inv[i];
            if (piece.empty()) continue;
            int lv = EnchantmentHelper::getThorns(piece);
            if (lv <= 0) continue;
            float roll = static_cast<float>(nextRandom()) / static_cast<float>(RAND_MAX);
            if (!thornsProcs(lv, roll)) continue;
            float rollD = static_cast<float>(nextRandom()) / static_cast<float>(RAND_MAX);
            reflected = thornsDamage(rollD);
            // Keep a value snapshot so the durability decrement can be applied
            // after the reflected damage without damaging a replacement armor
            // item installed by a re-entrant callback.
            expected = piece;
        }

        // Never hold the victim lock while entering another damage pipeline:
        // the attacker may be the player that is concurrently attacking this
        // victim, and its JVM callback may re-enter inventory/state operations.
        if (attackerMob) {
            bool attackerAlive = false;
            {
                std::lock_guard attackerLock(*attackerMob->stateMtx);
                attackerAlive = !attackerMob->dead;
            }
            if (attackerAlive) {
                srv.applyDamageToMob(*attackerMob,
                                     static_cast<float>(reflected), thorns);
            }
        } else if (attackerPlayer && attackerPlayer != &victim) {
            bool attackerAlive = false;
            {
                std::lock_guard attackerLock(attackerPlayer->stateMtx);
                attackerAlive = !attackerPlayer->dead;
            }
            if (attackerAlive)
                srv.applyDamage(*attackerPlayer, static_cast<float>(reflected), thorns);
        }

        {
            std::lock_guard playerLock(victim.stateMtx);
            auto& piece = victim.inv[i];
            if (!piece.empty() && piece.itemId == expected.itemId &&
                piece.count == expected.count &&
                piece.components == expected.components &&
                piece.removedComponents == expected.removedComponents) {
                if (DamageComponent::applyDamage(piece, 2))
                    piece = ItemStack::air();
            }
        }
        resendInventoryIfConnected(srv, victim);
    }
}

} // namespace cppfm
