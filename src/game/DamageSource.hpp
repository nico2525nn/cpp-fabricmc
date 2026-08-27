// DamageSource: typed damage with category flags for enchant EPF weighting (plan6 item 80)
#pragma once
#include <string>
#include <algorithm>
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
    static DamageSource generic() { return DamageSource("generic"); }
    static DamageSource fromString(const std::string& t) { return DamageSource(t); }
    static DamageSource fromCStr(const char* t) { return DamageSource(std::string(t)); }
};
}
