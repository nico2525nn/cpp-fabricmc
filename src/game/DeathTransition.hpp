#pragma once

namespace cppfm::death_transition {

enum class Resolution { Vetoed, Revived, Commit };

// ALLOW_DEATH vetoes the transition itself; it never heals implicitly.
inline Resolution resolve(bool allowed, float health) noexcept {
    if (!allowed) return Resolution::Vetoed;
    return health > 0.0F ? Resolution::Revived : Resolution::Commit;
}

} // namespace cppfm::death_transition
