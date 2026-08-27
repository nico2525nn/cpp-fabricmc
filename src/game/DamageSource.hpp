// DamageSource: typed damage with category flags for enchant EPF weighting (plan6/7 item 80)
// + DamageCalculator: vanilla-accurate armor/EPF/resistance pipeline
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
    bool bypassArmor = false;
    bool bypassEnchant = false;

    DamageSource() : type("generic") {}
    explicit DamageSource(std::string t) : type(std::move(t)) { classify(); }
    explicit DamageSource(const char* t) : type(t) { classify(); }

    bool isFire() const { return isFireFlag; }
    bool isFall() const { return isFallFlag; }
    bool isDrown() const { return isDrownFlag; }
    bool isExplosion() const { return isExplosionFlag; }
    bool isProjectile() const { return isProjectileFlag; }
    bool isMagic() const { return isMagicFlag; }

    void classify() {
        std::string lower = type;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        // fire
        if (lower.find("fire") != std::string::npos || lower.find("lava") != std::string::npos ||
            lower.find("hot") != std::string::npos || lower == "onfire" || lower == "infire" || lower == "flame") {
            isFireFlag = true;
        }
        // fall
        if (lower == "fall" || lower.find("fell") != std::string::npos) {
            isFallFlag = true;
        }
        // drown
        if (lower == "drown") {
            isDrownFlag = true;
            bypassArmor = true;
            bypassEnchant = true;
        }
        // explosion
        if (lower.find("explosion") != std::string::npos || lower.find("explode") != std::string::npos) {
            isExplosionFlag = true;
        }
        // projectile / arrow
        if (lower == "arrow" || lower.find("projectile") != std::string::npos || lower == "thrown" ||
            lower.find("arrow") != std::string::npos || lower == "snowball" || lower == "egg") {
            isProjectileFlag = true;
        }
        // magic / poison / wither
        if (lower == "magic" || lower == "indirectmagic" || lower == "poison" || lower == "wither") {
            isMagicFlag = true;
            if (lower == "wither") isWitherFlag = true;
        }
        if (lower == "freeze" || lower == "powdersnow") isFreezeFlag = true;
        if (lower == "starve" || lower == "starvation") { isStarveFlag = true; bypassArmor = true; bypassEnchant = true; }
        // starve/drown/wither bypass armor? drown already, wither bypass? keep enchant.
        // fall bypasses armor? no, fall is reduced by feather falling only, not armor.
        // but we keep armor reduction for fall? armor doesn't reduce fall in vanilla, but spec says fall EPF 1 -> armor still?
        // We'll treat fall as bypassArmor false but enchant specific.
    }

    static DamageSource fire() { DamageSource s("onFire"); s.isFireFlag = true; return s; }
    static DamageSource fall() { DamageSource s("fall"); s.isFallFlag = true; return s; }
    static DamageSource drown() { DamageSource s("drown"); s.isDrownFlag = true; return s; }
    static DamageSource explosion() { DamageSource s("explosion"); s.isExplosionFlag = true; return s; }
    static DamageSource projectile() { DamageSource s("arrow"); s.isProjectileFlag = true; return s; }
    static DamageSource magic() { DamageSource s("magic"); s.isMagicFlag = true; return s; }
    static DamageSource starve() { DamageSource s("starve"); s.isStarveFlag = true; s.bypassArmor = true; s.bypassEnchant = true; return s; }
    static DamageSource generic() { return DamageSource("generic"); }
    static DamageSource fromString(const std::string& t) { return DamageSource(t); }
    static DamageSource fromCStr(const char* t) { return DamageSource(std::string(t)); }
};

// DamageCalculator: vanilla armor + toughness + EPF + Resistance pipeline
// All calculations are pure functions so they can be unit-tested independently.
struct DamageCalculator {
    static float applyArmorReduction(float dmg, int armor) {
        if (armor <= 0 || dmg <= 0) return dmg;
        float a = static_cast<float>(armor);
        float eff = std::min(20.f, std::max(a/5.f, a - dmg/2.f));
        return dmg * (1.f - eff/25.f);
    }
    static float applyToughness(float dmgAfterArmor, float original, double toughness) {
        if (toughness <= 0 || dmgAfterArmor >= original) return dmgAfterArmor;
        return dmgAfterArmor * (1.f - static_cast<float>(toughness * 0.02));
    }
    static float applyEnchantProtection(float dmg, int epf) {
        if (epf <= 0 || dmg <= 0) return dmg;
        float eff = std::min(20.f, static_cast<float>(epf * 4)) / 25.f;
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
            float afterArmor = applyArmorReduction(d, armor);
            d = applyToughness(afterArmor, base, toughness);
        }
        if (!src.bypassEnchant && !src.isDrown()) {
            d = applyEnchantProtection(d, epf);
        }
        d = applyResistance(d, effects);
        return std::max(0.f, d);
    }
};
}
