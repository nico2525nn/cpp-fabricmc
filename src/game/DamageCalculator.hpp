// Re-export from DamageSource.hpp for modular ownership. GameServer delegates to CombatManager which uses DamageCalculator.
#pragma once
#include "DamageSource.hpp"
namespace cppfm {
using DamageCalculator = DamageCalculator; // alias for plan10 modular split
}
