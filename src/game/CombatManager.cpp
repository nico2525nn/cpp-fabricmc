// CombatManager — plan19 combat polish: EPF weight 1 verified, armor+toughness single formula 30/20, E7 strict.
// plan20-26 combat: regression-verify combat formulas across merges (EPF w1, sonic bypass, caps 30/20).
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

namespace cppfm {

int CombatManager::armorForItem(uint32_t itemId) {
    return armorPointsForItem(itemId);
}

int CombatManager::totalArmorForPlayer(const Player& p) {
    return totalArmorPoints(p.inv);
}

int CombatManager::totalArmorForMob(const MobEntity& m) {
    return totalArmorPoints(m);
}

int CombatManager::computeEPF(const DamageSource& ds, const Player& p) {
    // plan40 C-08: single source via EnchantmentHelper::getProtectionEPF (weight 1/2/2/3 caps 30/20)
    if (ds.bypassEnchant || ds.isDrown() || ds.isStarveFlag || ds.isSonic()) return 0;
    int total = 0;
    for (int i = 5; i <= 8; ++i) {
        if (i < 0 || i >= 46 || p.inv[i].empty()) continue;
        total += EnchantmentHelper::getProtectionEPF(ds, p.inv[i]);
    }
    if (total > 20) total = 20;
    return total;
}

int CombatManager::computeEPF(const DamageSource& ds, const MobEntity& m) {
    // plan40 C-08: single source via EnchantmentHelper::getProtectionEPF
    if (ds.bypassEnchant || ds.isDrown() || ds.isStarveFlag || ds.isSonic()) return 0;
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
    int armor = totalArmorForPlayer(p);
    int toughness = 0;
    float kbResist = 0.f;
    for (int i = 5; i <= 8; ++i) {
        if (i < 0 || i >= 46 || p.inv[i].empty()) continue;
        std::string n = p.inv[i].name();
        if (n.find("diamond_") != std::string::npos) toughness += 2;
        else if (n.find("netherite_") != std::string::npos) { toughness += 3; kbResist += 0.1f; }
    }
    bool dirty = p.attributes.armorDirty(armor, toughness, kbResist);
    p.attributes.syncArmor(armor, toughness, kbResist);
    if (dirty && p.conn && p.inPlay) {
        WriteBuffer ab;
        p.attributes.writeUpdate(ab, p.entityId);
        try { p.conn->sendPacket(proto::pl::sc::UpdateAttributes, ab); } catch (...) {}
        srv.broadcastPacketExcept(&p, proto::pl::sc::UpdateAttributes, ab);
    }
}

void CombatManager::applyToPlayer(GameServer& srv, Player& p, float amount, const DamageSource& src) {
    if (p.gamemode == 1 || p.gamemode == 3) return;
    if (amount <= 0 || p.dead) return;
    syncPlayerArmor(srv, p);
    int armor = static_cast<int>(std::round(p.attributes.getValue(Attribute::ARMOR)));
    if (armor == 0) armor = totalArmorForPlayer(p);
    double toughness = p.attributes.getValue(Attribute::ARMOR_TOUGHNESS);
    int epf = computeEPF(src, p);
    float finalAmt = DamageCalculator::calculate(amount, src, armor, toughness, epf, p.effects);
    if (finalAmt <= 0) return;
    if (p.gamemode == 0) srv.addHungerExhaustion(p, 0.10f);
    p.health -= finalAmt;
    p.hurtCooldown = 10;
    if (p.health <= 0) { p.health = 0; srv.killPlayer(p, src.type.c_str()); }
    srv.sendSetHealth(p);
    if (p.conn) {
        WriteBuffer de;
        de.varint(p.entityId);
        int dtid = srv.gameData_.idOf("minecraft:damage_type", std::string("minecraft:") + src.type);
        if (dtid < 0) dtid = srv.gameData_.idOf("minecraft:damage_type", "minecraft:generic");
        if (dtid < 0) dtid = 0;
        de.varint(dtid >= 0 ? dtid : 0);
        de.varint(0); de.varint(0);
        de.boolean(false);
        try { p.conn->sendPacket(proto::pl::sc::DamageEvent, de); } catch (...) {}
        srv.broadcastPacketExcept(&p, proto::pl::sc::DamageEvent, de);
    }
}

void CombatManager::applyToMob(GameServer& srv, MobEntity& m, float amount, const DamageSource& src) {
    if (amount <= 0 || m.dead) return;
    int armor = totalArmorForMob(m);
    int epf = computeEPF(src, m);
    float finalAmt = DamageCalculator::calculate(amount, src, armor, 0.0, epf, {});
    if (finalAmt <= 0) return;
    m.health -= finalAmt;
    m.hurtCooldown = 10;
    if (m.health <= 0) m.dead = true;
}

// plan44 §3 G-08 shield block — vanilla Blocking (100% frontal negate; the historical "5 軽減" is the
// legacy 1.8 value, 1.21.4 wiki value 100% is implemented here; see docs/SPEC_GAMEPLAY.md)
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
    if (!isShieldBlocking(victim)) return false;
    if (!isFrontal(victim, attackerX, attackerZ)) return false;
    ItemStack* shield = shieldStackFor(victim);
    if (!shield) return false;
    if (attackerWeaponIsAxe) {
        // vanilla: axe disables shield 100t (5s); the disabling blow deals damage
        victim.shieldDisableTicks = 100;
        victim.blockingTicks = 0;
        if (DamageComponent::applyDamage(*shield, 1)) *shield = ItemStack::air();
        srv.resendInventory(victim);
        srv.broadcastEntitySound(victim.entityId, "minecraft:item.shield.break", 1.f, 1.f, GameServer::SoundSource::Player);
        return false;
    }
    if (DamageComponent::applyDamage(*shield, 1)) *shield = ItemStack::air();
    srv.resendInventory(victim);
    srv.broadcastEntitySound(victim.entityId, "minecraft:item.shield.block", 1.f, 1.f, GameServer::SoundSource::Player);
    return true;
}
// plan44 §3 G-09 thorns — per-piece independent lv*15% proc, reflect uniform 1..4 + armor cost 2
void CombatManager::applyThornsReflection(GameServer& srv, Player& victim,
                                          MobEntity* attackerMob, Player* attackerPlayer) {
    if (!attackerMob && !attackerPlayer) return;
    DamageSource thorns("thorns");
    for (int i = 5; i <= 8; ++i) {
        auto& piece = victim.inv[i];
        if (piece.empty()) continue;
        int lv = EnchantmentHelper::getThorns(piece);
        if (lv <= 0) continue;
        float roll = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        if (!thornsProcs(lv, roll)) continue;
        float rollD = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        int reflected = thornsDamage(rollD);
        if (attackerMob && !attackerMob->dead) srv.applyDamageToMob(*attackerMob, static_cast<float>(reflected), thorns);
        else if (attackerPlayer && !attackerPlayer->dead && attackerPlayer != &victim)
            srv.applyDamage(*attackerPlayer, static_cast<float>(reflected), thorns);
        if (DamageComponent::applyDamage(piece, 2)) piece = ItemStack::air();
        srv.resendInventory(victim);
    }
}

} // namespace cppfm
