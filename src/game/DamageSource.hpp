// explosion(2), projectile(2), fall(feather_falling*3) bypassArmor: drown/starve; bypassEnchant: drown/starve + DamageCalculator: vanilla
// bypassArmor, sonic 15x20 bypass, EPF w1.
#pragma once
#include <string>
#include <algorithm>
#include <vector>
#include <cmath>
#include "MobEffects.hpp"
namespace cppfm {
struct DamageSource {
    std::string type;
    bool isFireFlag = false;
    bool isFallFlag = false;
    bool isDrownFlag = false;
    bool isExplosionFlag = false;
    bool isProjectileFlag = false;
    bool isMagicFlag = false;
    bool isWitherFlag = false;
    bool isFreezeFlag = false;
    bool isStarveFlag = false;
    bool isLightningFlag = false;
    bool isCrammingFlag = false;
    bool isSonicFlag = false;
    bool bypassArmor = false;
    bool bypassEnchant = false;
    bool bypassShield = false;

    DamageSource() : type("generic") {}
    explicit DamageSource(std::string t) : type(std::move(t)) { classify(); }
    explicit DamageSource(const char* t) : type(t) { classify(); }

    bool isFire() const { return isFireFlag; }
    bool isFall() const { return isFallFlag; }
    bool isDrown() const { return isDrownFlag; }
    bool isExplosion() const { return isExplosionFlag; }
    bool isProjectile() const { return isProjectileFlag; }
    bool isMagic() const { return isMagicFlag; }
    bool isWither() const { return isWitherFlag; }
    bool isFreeze() const { return isFreezeFlag; }
    bool isStarve() const { return isStarveFlag; }
    bool isLightning() const { return isLightningFlag; }
    bool isSonic() const { return isSonicFlag; }

    void classify() {
        std::string lower = type;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find("fire") != std::string::npos || lower.find("lava") != std::string::npos ||
            lower.find("hot") != std::string::npos || lower == "onfire" || lower == "infire" || lower == "flame") {
            isFireFlag = true;
        }
        if (lower == "fall" || lower.find("fell") != std::string::npos) {
            isFallFlag = true;
            bypassArmor = true;
        }
        if (lower == "drown") {
            isDrownFlag = true;
            bypassArmor = true;
            bypassEnchant = true;
        }
        if (lower.find("explosion") != std::string::npos || lower.find("explode") != std::string::npos) {
            isExplosionFlag = true;
        }
        if (lower == "arrow" || lower.find("projectile") != std::string::npos || lower == "thrown" ||
            lower.find("arrow") != std::string::npos || lower == "snowball" || lower == "egg" ||
            lower == "trident" || lower.find("wither_skull") != std::string::npos) {
            isProjectileFlag = true;
        }
        if (lower == "magic" || lower == "indirectmagic" || lower == "poison" || lower == "wither") {
            isMagicFlag = true;
            if (lower == "wither") isWitherFlag = true;
        }
        if (lower == "freeze" || lower == "powdersnow" || lower == "powder_snow") isFreezeFlag = true;
        if (lower == "starve" || lower == "starvation") { isStarveFlag = true; bypassArmor = true; bypassEnchant = true; }
        if (lower == "lightning" || lower == "lightningbolt") isLightningFlag = true;
        if (lower == "cramming" || lower.find("cram") != std::string::npos) isCrammingFlag = true;
        if (lower == "sonic_boom" || lower == "sonicboom" || lower.find("sonic") != std::string::npos) { isSonicFlag = true; bypassArmor = true; bypassEnchant = true; bypassShield = true; }
    }

    static DamageSource fire() { DamageSource s("onFire"); s.isFireFlag = true; return s; }
    static DamageSource fall() { DamageSource s("fall"); s.isFallFlag = true; s.bypassArmor = true; return s; }
    static DamageSource drown() { DamageSource s("drown"); s.isDrownFlag = true; s.bypassArmor = true; s.bypassEnchant = true; return s; }
    static DamageSource explosion() { DamageSource s("explosion"); s.isExplosionFlag = true; return s; }
    static DamageSource projectile() { DamageSource s("arrow"); s.isProjectileFlag = true; return s; }
    static DamageSource magic() { DamageSource s("magic"); s.isMagicFlag = true; return s; }
    static DamageSource wither() { DamageSource s("wither"); s.isWitherFlag = true; s.isMagicFlag = true; return s; }
    static DamageSource freeze() { DamageSource s("freeze"); s.isFreezeFlag = true; return s; }
    static DamageSource starve() { DamageSource s("starve"); s.isStarveFlag = true; s.bypassArmor = true; s.bypassEnchant = true; return s; }
    static DamageSource lightning() { DamageSource s("lightningBolt"); s.isLightningFlag = true; return s; }
    static DamageSource sonicBoom() { DamageSource s("sonic_boom"); s.isSonicFlag = true; s.bypassArmor = true; s.bypassEnchant = true; s.bypassShield = true; return s; }
    static DamageSource generic() { return DamageSource("generic"); }
    static DamageSource fromString(const std::string& t) { return DamageSource(t); }
    static DamageSource fromCStr(const char* t) { return DamageSource(std::string(t)); }
};

// DamageCalculator: vanilla armor + toughness + EPF + Resistance pipeline
// All calculations are pure functions so they can be unit-tested independently.
struct DamageCalculator {
    static float applyArmorAndToughness(float dmg, float armor, float toughness) {
        if (dmg <= 0 || !std::isfinite(dmg)) return 0.f;
        if (!std::isfinite(armor)) armor = 0.f;
        if (!std::isfinite(toughness)) toughness = 0.f;
        float a = std::clamp(armor, 0.f, 30.f);
        float t = std::clamp(toughness, 0.f, 20.f);
        if (a <= 0) return dmg;
        float f = 2.f + t / 4.f;
        float g = std::clamp(a - dmg / f, a * 0.2f, 20.f);
        return dmg * (1.f - g / 25.f);
    }
    static float applyArmorReduction(float dmg, int armor) {
        return applyArmorAndToughness(dmg, static_cast<float>(armor), 0.f);
    }
    static float applyToughness(float dmgAfterArmor, float original, double toughness) {
        if (toughness <= 0) return dmgAfterArmor;
        return dmgAfterArmor;
    }
    static float applyEnchantProtection(float dmg, int epf) {
        if (epf <= 0 || dmg <= 0) return dmg;
        float eff = std::min(20.f, static_cast<float>(epf)) / 25.f;
        eff = std::min(eff, 0.8f);
        return dmg * (1.f - eff);
    }
    static float applyResistance(float dmg, const std::vector<EffectInstance>& effects) {
        float out = dmg;
        for (auto &e : effects) if (e.type == effects::Resistance) {
            float red = 0.2f * float(e.amplifier + 1);
            if (red > 0.8f) red = 0.8f;
            out *= (1.f - red);
        }
        return out;
    }
    static float calculate(float base, const DamageSource& src,
                           int armor, double toughness, int epf,
                           const std::vector<EffectInstance>& effects) {
        if (base <= 0) return 0.f;
        float d = base;
        if (!src.bypassArmor) {
            d = applyArmorAndToughness(d, static_cast<float>(armor), static_cast<float>(toughness));
        }
        if (!src.bypassEnchant && !src.isDrown() && !src.isSonic()) {
            d = applyEnchantProtection(d, epf);
        }
        // sonic_boom bypasses armor/enchant/shield but NOT Resistance (and not wolf armor)
        d = applyResistance(d, effects);
        return std::max(0.f, d);
    }
};
}
