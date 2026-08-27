#include "CombatManager.hpp"
#include "GameServer.hpp"
#include "Entities.hpp"
#include "Attributes.hpp"
#include "../generated/ItemIds.hpp"
#include <algorithm>
#include <cmath>

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
    if (ds.bypassEnchant || ds.isDrown() || ds.isStarveFlag) return 0;
    int total = 0;
    for (int i = 5; i <= 8; ++i) {
        if (i < 0 || i >= 46 || p.inv[i].empty()) continue;
        const auto& s = p.inv[i];
        int prot = std::max(s.enchantLevel("protection"), s.enchantLevel("minecraft:protection"));
        int fire = std::max(s.enchantLevel("fire_protection"), s.enchantLevel("minecraft:fire_protection"));
        int blast = std::max(s.enchantLevel("blast_protection"), s.enchantLevel("minecraft:blast_protection"));
        int proj = std::max(s.enchantLevel("projectile_protection"), s.enchantLevel("minecraft:projectile_protection"));
        int feather = std::max(s.enchantLevel("feather_falling"), s.enchantLevel("minecraft:feather_falling"));
        int weight = 1;
        if (ds.isFire() || ds.isExplosion() || ds.isProjectile()) weight = 2;
        else if (ds.isFall()) weight = 1;
        total += prot * weight;
        if (ds.isFire()) total += fire * 2;
        if (ds.isExplosion()) total += blast * 2;
        if (ds.isProjectile()) total += proj * 2;
        if (ds.isFall()) total += feather * 3;
    }
    if (total > 20) total = 20;
    return total;
}

int CombatManager::computeEPF(const DamageSource& ds, const MobEntity& m) {
    if (ds.bypassEnchant || ds.isDrown() || ds.isStarveFlag) return 0;
    int total = 0;
    for (int i = 2; i < 6; ++i) {
        if (m.equipment[i].empty()) continue;
        const auto& s = m.equipment[i];
        int prot = std::max(s.enchantLevel("protection"), s.enchantLevel("minecraft:protection"));
        int fire = std::max(s.enchantLevel("fire_protection"), s.enchantLevel("minecraft:fire_protection"));
        int blast = std::max(s.enchantLevel("blast_protection"), s.enchantLevel("minecraft:blast_protection"));
        int proj = std::max(s.enchantLevel("projectile_protection"), s.enchantLevel("minecraft:projectile_protection"));
        int feather = std::max(s.enchantLevel("feather_falling"), s.enchantLevel("minecraft:feather_falling"));
        int weight = 1;
        if (ds.isFire() || ds.isExplosion() || ds.isProjectile()) weight = 2;
        else if (ds.isFall()) weight = 1;
        total += prot * weight;
        if (ds.isFire()) total += fire * 2;
        if (ds.isExplosion()) total += blast * 2;
        if (ds.isProjectile()) total += proj * 2;
        if (ds.isFall()) total += feather * 3;
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
    (void)srv;
}

} // namespace cppfm
