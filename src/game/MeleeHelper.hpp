// MeleeHelper — plan44 §3 G-07/G-08/G-09 combat pure functions (header-only, unit-testable). Vanilla sources: minecraft.wiki Sweeping_Edge
// / Melee_attack#Critical_hit / Blocking, Yarn PlayerEntity.attack (cooldown 84.8%+, T=20/attack_speed), wiki
// Thorns/Density/Breach/Wind_Burst/Riptide. Wire policy: pure math here; GameServer_session onUseEntity + CombatManager consume it.
// E-06/E-07/E-08 lock (DamageCalculator single formula caps 30/20, EPF weights) is NOT touched.
#pragma once
#include <string>
#include <cmath>
#include <algorithm>

namespace cppfm {

// ---- attack cooldown (Yarn PlayerEntity.attack: T = 20/attack_speed, default speed 4 -> 5t) ----
inline float cooldownProgress(int ticksSinceAttack, float attackSpeed = 4.0f) {
    float T = 20.0f / std::max(0.5f, attackSpeed);
    float p = (static_cast<float>(ticksSinceAttack) + 0.5f) / T;
    return std::clamp(p, 0.0f, 1.0f);
}
inline bool isChargedAttack(int ticksSinceAttack, float attackSpeed = 4.0f) {
    return cooldownProgress(ticksSinceAttack, attackSpeed) >= 0.848f; // vanilla 84.8%
}

// ---- weapon base damage tier table (wiki, melee) ----
inline float swordBaseDamage(const std::string& name) {
    if (name.find("netherite_") != std::string::npos) return 8.f;
    if (name.find("diamond_") != std::string::npos) return 7.f;
    if (name.find("iron_") != std::string::npos) return 6.f;
    if (name.find("stone_") != std::string::npos) return 5.f;
    if (name.find("golden_") != std::string::npos) return 4.f;
    if (name.find("wooden_") != std::string::npos) return 4.f;
    return 6.f; // fallback keeps legacy flat sword 6 (E-06 wire compat)
}
inline float axeBaseDamage(const std::string& name) {
    if (name.find("netherite_") != std::string::npos) return 10.f;
    if (name.find("diamond_") != std::string::npos) return 9.f;
    if (name.find("iron_") != std::string::npos) return 9.f;
    if (name.find("stone_") != std::string::npos) return 9.f;
    if (name.find("golden_") != std::string::npos) return 7.f;
    if (name.find("wooden_") != std::string::npos) return 7.f;
    return 7.f; // fallback keeps legacy flat axe 7 (E-06 wire compat)
}
inline bool isSwordItem(const std::string& name) { return name.find("sword") != std::string::npos; }
inline bool isAxeItem(const std::string& name) { return name.find("_axe") != std::string::npos; }
inline bool isMaceItem(const std::string& name) { return name.find("mace") != std::string::npos; }

// ---- G-07 sweep (Sweeping_Edge wiki): 1 + AD*lv/(lv+1), rounded; lv0 => 1 ----
inline float sweepingEdgeDamage(float attackDamage, int sweepingLv) {
    if (sweepingLv <= 0) return 1.f;
    float lv = static_cast<float>(sweepingLv);
    return std::round(1.f + attackDamage * (lv / (lv + 1.f)));
}
// sweep triggers on standing sword attack: sword + onGround + not sprinting + charged
inline bool isSweepAttack(bool weaponIsSword, bool onGround, bool sprinting, bool charged) {
    return weaponIsSword && onGround && !sprinting && charged;
}
inline bool inSweepRange(double ax, double ay, double az, double bx, double by, double bz) {
    double dx = ax - bx, dz = az - bz, dy = ay - by;
    return (dx*dx + dz*dz) <= 4.0 && std::abs(dy) <= 1.0; // 2m horizontal, ±1 vertical
}

// ---- G-07 crit (Yarn PlayerEntity.attack): falling + !onGround + !sprint + charged,
// disabled in water / on ladder-vine (climbing) / riding / blind / slow-falling ----
inline bool isCritAttack(bool onGround, bool falling, bool sprinting, bool charged,
                         bool inWater, bool blind, bool riding, bool climbing) {
    if (onGround || !falling) return false;
    if (sprinting || !charged) return false;
    if (inWater || blind || riding || climbing) return false;
    return true;
}
inline float applyCrit(float dmg) { return dmg * 1.5f; }

// ---- G-08 shield: frontal = horizontal angle < 90deg (pitch ignored) ----
inline float yawTo(double fromX, double fromZ, double toX, double toZ) {
    return static_cast<float>(std::atan2(-(toX - fromX), (toZ - fromZ)) * 180.0 / 3.14159265358979323846);
}
inline float angDiffDeg(float a, float b) {
    float d = std::fmod(a - b + 540.f, 360.f) - 180.f;
    return std::abs(d);
}
// victimYaw: victim facing yaw (degrees, vanilla convention); attacker at (ax,az), victim at (vx,vz)
inline bool isFrontalAttack(float victimYaw, double vx, double vz, double ax, double az) {
    float toAtk = yawTo(vx, vz, ax, az);
    return angDiffDeg(victimYaw, toAtk) < 90.f;
}
// shield active only after 5t (0.25s) of blocking; disabled 100t by axe
inline bool isShieldActive(bool holdingShieldSneak, int blockingTicks, int shieldDisableTicks) {
    return holdingShieldSneak && blockingTicks >= 5 && shieldDisableTicks <= 0;
}

// ---- G-09 thorns: per-piece independent lv*15% proc, reflect uniform 1..4 ----
inline float thornsProcChance(int lv) { return 0.15f * static_cast<float>(lv); }
inline bool thornsProcs(int lv, float roll01) { return lv > 0 && roll01 < thornsProcChance(lv); }
inline int thornsDamage(float roll01) { // uniform 1..4 from [0,1)
    int d = 1 + static_cast<int>(roll01 * 4.f);
    return std::clamp(d, 1, 4);
}

// ---- G-09 breach (mace): ignore 15%/lv of armor — input pre-discount, formula untouched ----
inline int breachAdjustedArmor(int armor, int breachLv) {
    if (breachLv <= 0) return armor;
    float f = 1.f - 0.15f * static_cast<float>(std::min(breachLv, 4));
    return static_cast<int>(std::round(static_cast<float>(armor) * std::max(0.f, f)));
}
// ---- G-09 density (mace): +0.5HP x 0.25 per block fallen per level (plan44 §3) ----
inline float densitySmashBonus(int fallBlocks, int densityLv) {
    if (densityLv <= 0 || fallBlocks <= 0) return 0.f;
    return 0.5f * 0.25f * static_cast<float>(fallBlocks) * static_cast<float>(densityLv);
}
// ---- G-09 wind burst (mace): KB factor 1.15+0.35*lv, launch 7 blocks/lv ----
inline float windBurstKBFactor(int lv) { return lv <= 0 ? 1.f : 1.15f + 0.35f * static_cast<float>(lv); }
inline float windBurstLaunchVy(int lv) { return lv <= 0 ? 0.f : 0.5f + 0.25f * static_cast<float>(lv); }
// ---- G-09 impaling (JE): +2.5/lv vs aquatic ----
inline float impalingBonus(int lv) { return lv <= 0 ? 0.f : 2.5f * static_cast<float>(lv); }

// ---- G-09 crossbow/bow ----
inline int multishotArrowCount(int multishotLv) { return multishotLv > 0 ? 3 : 1; }
inline float quickChargeLoadTime(float baseSec, int lv) {
    return std::max(0.1f, baseSec - 0.25f * static_cast<float>(std::max(0, lv)));
}
// ---- G-09 trident ----
inline float riptideLaunchBlocks(int lv) { // 9/15/21 blocks for I/II/III
    if (lv <= 0) return 0.f;
    return 3.f + 6.f * static_cast<float>(std::min(lv, 3));
}
inline bool channelingShouldStrike(bool thundering, bool canSeeSky, int channelingLv) {
    return thundering && canSeeSky && channelingLv > 0;
}
// ---- G-09 frost walker radius 2+lv (matches existing session impl) ----
inline int frostRadius(int lv) { return 2 + std::max(0, lv); }
// ---- G-09 power bonus 0.25*(lv+1) (mirrors EnchantmentHelper) ----
inline float powerBonus(int lv) { return lv <= 0 ? 0.f : 0.25f * static_cast<float>(lv + 1); }

} // namespace cppfm
