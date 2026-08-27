// DamageCalculator: plan10 §7 — vanilla armor/EPF/resistance pipeline (armor 5..20 -> 4%..80% reduce)
// Re-export from DamageSource.hpp for modular ownership. GameServer delegates to CombatManager which uses DamageCalculator.
#pragma once
#include "DamageSource.hpp"
namespace cppfm {
using DamageCalculator = DamageCalculator; // alias for plan10 modular split
}
