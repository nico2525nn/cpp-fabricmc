// MiningCalculator: plan44 G-02/G-03 — vanilla survival mining math.
// Pure functions over gen::BlockDef (toolMask/effMask/needsTier/blastResistance).
// Spec: minecraft.wiki/w/Breaking (Calculation) + Yarn Tier harvest levels.
//   speedMultiplier: Hand 1 / Wood 2 / Stone 4 / Copper 5 / Iron 6 /
//     Diamond 8 / Netherite 9 / Gold 12. Sword 1 (15 on cobweb).
//     Shears 15 on cobweb, 5 on wool/leaves/vines, else 1.
//   efficiency (speed>1 only): speed += eff*eff + 1.
//   Haste: speed *= 1 + 0.2*lv. MiningFatigue: speed *= 0.3^min(lv,4).
//   Submerged (no aqua affinity): speed *= 0.2. Not on ground: speed /= 5.
//   damage = speed / hardness / (canHarvest ? 30 : 100).
//   ticks = damage>=1 ? 0 (instant) : ceil(1/damage).
//   hardness<0 (bedrock etc.) -> kUnbreakable (-1 ticks).
#pragma once
#include <cmath>
#include <cstdint>
#include "../generated/BlockStates.hpp"

namespace cppfm {

// Tool bits mirror tools/mining_fields.py (bit0 pickaxe ... bit5 shears).
inline constexpr std::uint8_t kToolPickaxe = 1;
inline constexpr std::uint8_t kToolAxe = 2;
inline constexpr std::uint8_t kToolShovel = 4;
inline constexpr std::uint8_t kToolHoe = 8;
inline constexpr std::uint8_t kToolSword = 16;
inline constexpr std::uint8_t kToolShears = 32;

enum class ToolKind : std::uint8_t { Hand, Pickaxe, Axe, Shovel, Hoe, Sword, Shears };
// Harvest ranks (Yarn Tier.java): gold harvests like wood.
enum class ToolTier : std::uint8_t { None_, Wood, Stone, Gold, Iron, Diamond, Netherite };

struct MiningCalculator {
  static constexpr int kUnbreakable = -1;

  static int tierRank(ToolTier t) {
    switch (t) {
      case ToolTier::Wood: return 0;
      case ToolTier::Gold: return 0;
      case ToolTier::Stone: return 1;
      case ToolTier::Iron: return 2;
      case ToolTier::Diamond: return 3;
      case ToolTier::Netherite: return 4;
      default: return 0;
    }
  }

  // Harvest level of a held tool (needsTier scale: 0 none, 1 wooden+, ...).
  // tierRank is 0-based (wood=0); harvest levels are 1-based.
  static int harvestLevel(ToolTier t) {
    return t == ToolTier::None_ ? 0 : tierRank(t) + 1;
  }

  static std::uint8_t maskOf(ToolKind k) {
    switch (k) {
      case ToolKind::Pickaxe: return kToolPickaxe;
      case ToolKind::Axe: return kToolAxe;
      case ToolKind::Shovel: return kToolShovel;
      case ToolKind::Hoe: return kToolHoe;
      case ToolKind::Sword: return kToolSword;
      case ToolKind::Shears: return kToolShears;
      default: return 0;
    }
  }

  // Correct tool for HARVEST (drop): required mask match + tier sufficiency.
  // toolMask==0 blocks (dirt/planks/torch) harvest by hand.
  static bool canHarvest(const gen::BlockDef& def, ToolKind kind, ToolTier tier) {
    if (def.toolMask == 0) return true;
    if ((maskOf(kind) & def.toolMask) == 0) return false;
    return harvestLevel(tier) >= def.needsTier;
  }

  // Destroy speed of the held tool against this block.
  static float destroySpeed(const gen::BlockDef& def, ToolKind kind, ToolTier tier) {
    const bool cobweb = def.name == "minecraft:cobweb";
    if (kind == ToolKind::Sword) return cobweb ? 15.f : 1.f;
    if (kind == ToolKind::Shears) {
      if (cobweb) return 15.f;
      return (def.effMask & kToolShears) ? 5.f : 1.f;
    }
    if (kind == ToolKind::Hand) return 1.f;
    if ((maskOf(kind) & def.effMask) == 0) return 1.f;
    switch (tier) {
      case ToolTier::Wood: return 2.f;
      case ToolTier::Stone: return 4.f;
      case ToolTier::Gold: return 12.f;
      case ToolTier::Iron: return 6.f;
      case ToolTier::Diamond: return 8.f;
      case ToolTier::Netherite: return 9.f;
      default: return 1.f;
    }
  }

  // Full speed multiplier with enchant/potion/environment modifiers.
  static float effectiveSpeed(const gen::BlockDef& def, ToolKind kind, ToolTier tier,
                              int efficiencyLv, int hasteLv, int fatigueLv,
                              bool inWater, bool aquaAffinity, bool onGround) {
    float speed = destroySpeed(def, kind, tier);
    if (speed > 1.f && efficiencyLv > 0)
      speed += (float)(efficiencyLv * efficiencyLv) + 1.f;
    if (hasteLv > 0) speed *= 1.f + 0.2f * (float)hasteLv;
    if (fatigueLv > 0) {
      int lv = fatigueLv < 4 ? fatigueLv : 4;
      float m = 1.f;
      for (int i = 0; i < lv; ++i) m *= 0.3f;
      speed *= m;
    }
    if (inWater && !aquaAffinity) speed *= 0.2f;
    if (!onGround) speed /= 5.f;
    return speed;
  }

  // Ticks to break (0 = instant, kUnbreakable = survival-impossible).
  // Double arithmetic with a 1e-7 relative guard so exact tick boundaries
  // (wooden-pick/obsidian = 2500t, hand/glass = 9t per wiki) don't ceil up
  // on float32 noise from 2-decimal hardness literals (e.g. 0.3f).
  static int breakTicks(const gen::BlockDef& def, float speed, bool harvest) {
    if (def.hardness < 0.f) return kUnbreakable;
    if (def.hardness <= 0.f) return 0;
    const double dmg = (double)speed / (double)def.hardness / (harvest ? 30.0 : 100.0);
    if (dmg >= 1.0) return 0;
    return (int)std::ceil((1.0 / dmg) * (1.0 - 1e-7));
  }

  static int breakTicks(const gen::BlockDef& def, ToolKind kind, ToolTier tier,
                        int efficiencyLv = 0, int hasteLv = 0, int fatigueLv = 0,
                        bool inWater = false, bool aquaAffinity = false,
                        bool onGround = true) {
    const float speed = effectiveSpeed(def, kind, tier, efficiencyLv, hasteLv,
                                       fatigueLv, inWater, aquaAffinity, onGround);
    return breakTicks(def, speed, canHarvest(def, kind, tier));
  }

  // Explosion (TNT power 4 / creeper 3): deterministic roll/exposure form of
  // the vanilla "resistance < power*(0.7+0.6*rand)*exposure" destroy check.
  static bool blastSurvives(const gen::BlockDef& def, float power,
                            float exposure = 1.f, float roll = 1.f) {
    return def.blastResistance >= power * (0.7f + 0.6f * roll) * exposure;
  }
};

} // namespace cppfm
