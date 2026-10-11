#pragma once

namespace cppfm {

struct AfterDamageAmounts {
    float baseDamageTaken = 0.0F;
    float damageTaken = 0.0F;
    bool blocked = false;
};

// Fabric reports the original amount separately from the amount left after
// pre-armor effects (notably a shield block). Armor, enchantment protection,
// and later health-loss mitigation must not be folded into damageTaken.
inline AfterDamageAmounts makeAfterDamageAmounts(float baseDamage,
                                                 float beforeArmor,
                                                 bool blocked = false) noexcept {
    return {baseDamage, beforeArmor, blocked};
}

} // namespace cppfm
