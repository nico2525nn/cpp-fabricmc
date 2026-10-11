#pragma once

namespace cppfm {

struct AfterDamageAmounts {
    float baseDamageTaken = 0.0F;
    float damageTaken = 0.0F;
    bool blocked = false;
};

// Fabric reports post-shield/pre-armor damage, not the final health delta.
inline AfterDamageAmounts makeAfterDamageAmounts(float baseDamage,
                                                 float beforeArmor,
                                                 bool blocked = false) noexcept {
    return {baseDamage, beforeArmor, blocked};
}

} // namespace cppfm
