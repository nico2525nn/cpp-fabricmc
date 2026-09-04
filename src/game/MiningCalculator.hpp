// (toolMask/effMask/needsTier/blastResistance). Spec: minecraft.wiki/w/Breaking (Calculation) + Yarn Tier harvest levels. speedMultiplier:
// Hand 1 / Wood 2 / Stone 4 / Copper 5 / Iron 6 / Diamond 8 / Netherite 9 / Gold 12. Sword 1 (15 on cobweb). Shears 15 on cobweb, 5 on
// wool/leaves/vines, else 1. efficiency (speed>1 only): speed += eff*eff + 1. Haste: speed *= 1 + 0.2*lv. MiningFatigue: speed *=
// 0.3^min(lv,4). Submerged (no aqua affinity): speed *= 0.2. Not on ground: speed /= 5. damage = speed / hardness / (canHarvest ? 30 :
// 100). ticks = damage>=1 ? 0 (instant) : ceil(1/damage). hardness<0 (bedrock etc.) -> kUnbreakable (-1 ticks).
#pragma once
#include <cmath>
#include <cstdint>
#include <string_view>
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

struct MiningContext {
  ToolKind tool = ToolKind::Hand;
  ToolTier tier = ToolTier::None_;
  int efficiency = 0;
  int haste = 0;
  int fatigue = 0;
  bool inWater = false;
  bool aquaAffinity = false;
  bool onGround = true;
};

struct MiningResult {
  bool harvest = false;
  float speed = 1.f;
  int ticks = 0;
};

struct MiningCalculator {
  static constexpr int kUnbreakable = -1;
  // Player has no spare mining-result field in the protocol-facing state. The
  // high bit of digLastStage carries the start-time harvest decision; the low
  // nibble remains the client-visible 0..9 progress stage. The upper 16 bits
  // of digStartTick carry the exact starting block state (the lower 48 bits
  // retain the tick), so a replacement block cannot consume stale progress.
  static constexpr std::uint8_t kDigNoHarvestFlag = 0x80;
  static constexpr std::uint8_t kDigStageMask = 0x0F;
  static constexpr std::uint64_t kDigTickMask = (std::uint64_t{1} << 48) - 1;

  static std::uint8_t packDigStage(std::uint8_t stage, bool harvest) {
    return static_cast<std::uint8_t>((stage & kDigStageMask) |
                                     (harvest ? 0 : kDigNoHarvestFlag));
  }

  static std::uint8_t unpackDigStage(std::uint8_t packed) {
    return static_cast<std::uint8_t>(packed & kDigStageMask);
  }

  static bool digCanHarvest(std::uint8_t packed) {
    return (packed & kDigNoHarvestFlag) == 0;
  }

  static std::int64_t packDigStartTick(std::int64_t tick,
                                       std::uint16_t state) {
    const auto rawTick = static_cast<std::uint64_t>(tick) & kDigTickMask;
    return static_cast<std::int64_t>(rawTick |
                                     (static_cast<std::uint64_t>(state) << 48));
  }

  static std::int64_t unpackDigStartTick(std::int64_t packed) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(packed) &
                                     kDigTickMask);
  }

  static std::uint16_t digStartingState(std::int64_t packed) {
    return static_cast<std::uint16_t>(static_cast<std::uint64_t>(packed) >> 48);
  }

  static std::string_view itemPath(std::string_view itemName) {
    const auto colon = itemName.rfind(':');
    if (colon == std::string_view::npos) return itemName;
    return itemName.substr(colon + 1);
  }

  // Keep the item-name policy in one place for both mining timing and drops.
  // Unknown/empty items intentionally map to the hand tier.
  static ToolKind toolKindFromItemName(std::string_view itemName) {
    const auto path = itemPath(itemName);
    if (path == "shears") return ToolKind::Shears;
    if (path.ends_with("_pickaxe")) return ToolKind::Pickaxe;
    if (path.ends_with("_axe")) return ToolKind::Axe;
    if (path.ends_with("_shovel")) return ToolKind::Shovel;
    if (path.ends_with("_hoe")) return ToolKind::Hoe;
    if (path.ends_with("_sword")) return ToolKind::Sword;
    return ToolKind::Hand;
  }

  static ToolTier toolTierFromItemName(std::string_view itemName) {
    const auto path = itemPath(itemName);
    if (toolKindFromItemName(itemName) == ToolKind::Hand)
      return ToolTier::None_;
    if (path.starts_with("wooden_")) return ToolTier::Wood;
    if (path.starts_with("stone_")) return ToolTier::Stone;
    if (path.starts_with("golden_")) return ToolTier::Gold;
    if (path.starts_with("iron_")) return ToolTier::Iron;
    if (path.starts_with("diamond_")) return ToolTier::Diamond;
    if (path.starts_with("netherite_")) return ToolTier::Netherite;
    return ToolTier::None_;
  }

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

  // Harvest level of a held tool (needsTier scale: 0 none, 1 wooden+, ...). tierRank is 0-based (wood=0); harvest levels are 1-based.
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

  // Correct tool for HARVEST (drop): required mask match + tier sufficiency. toolMask==0 blocks (dirt/planks/torch) harvest by hand.
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

  // Ticks to break (0 = instant, kUnbreakable = survival-impossible). Double arithmetic with a 1e-7 relative guard so exact tick boundaries
  // (wooden-pick/obsidian = 2500t, hand/glass = 9t per wiki) don't ceil up on float32 noise from 2-decimal hardness literals (e.g. 0.3f).
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

  static MiningResult calculate(const gen::BlockDef& def,
                                const MiningContext& ctx) {
    const bool harvest = canHarvest(def, ctx.tool, ctx.tier);
    const float speed = effectiveSpeed(def, ctx.tool, ctx.tier,
                                      ctx.efficiency, ctx.haste, ctx.fatigue,
                                      ctx.inWater, ctx.aquaAffinity,
                                      ctx.onGround);
    return {harvest, speed, breakTicks(def, speed, harvest)};
  }

  static MiningResult calculateMining(const gen::BlockDef& def,
                                      const MiningContext& ctx) {
    return calculate(def, ctx);
  }

  // Explosion (TNT power 4 / creeper 3): deterministic roll/exposure form of
  // the vanilla "resistance < power*(0.7+0.6*rand)*exposure" destroy check.
  static bool blastSurvives(const gen::BlockDef& def, float power,
                            float exposure = 1.f, float roll = 1.f) {
    return def.blastResistance >= power * (0.7f + 0.6f * roll) * exposure;
  }
};

} // namespace cppfm
