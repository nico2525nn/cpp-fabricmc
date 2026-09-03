// test_mining_full — plan44 G-03 (MiningCalculator) + TNT blast (blastResistance).
// All expectations from minecraft.wiki/w/Breaking (Calculation):
//   hand/stone 7.5s=150t, iron-pick/stone 0.4s=8t, diamond-pick/obsidian
//   9.4s=188t, hand/dirt 0.75s=15t, hand/glass 0.45s=9t,
//   sword/cobweb 0.4s=8t, shears/wool 0.25s=5t.
// Policy: vanilla spec wins. FAIL is expected where gap exists.
#include <cmath>
#include <cstdio>

#include "generated/BlockStates.hpp"
#include "game/MiningCalculator.hpp"

static int g_pass = 0, g_fail = 0, g_total = 0;
static const char* curSection = "";
static void CHECK(bool cond, const char* name) {
  ++g_total;
  if (cond) { ++g_pass; }
  else { ++g_fail; std::printf("  FAIL [%s] %s\n", curSection, name); }
}
static void CHECK_EQ_INT(int a, int b, const char* name) {
  char buf[512];
  std::snprintf(buf, sizeof buf, "%s (expected %d got %d)", name, b, a);
  CHECK(a == b, buf);
}

using namespace cppfm;
using namespace cppfm::gen;

int main() {
  const BlockDef* stone = blockByName("minecraft:stone");
  const BlockDef* dirt = blockByName("minecraft:dirt");
  const BlockDef* obsidian = blockByName("minecraft:obsidian");
  const BlockDef* bedrock = blockByName("minecraft:bedrock");
  const BlockDef* glass = blockByName("minecraft:glass");
  const BlockDef* torch = blockByName("minecraft:torch");
  const BlockDef* cobweb = blockByName("minecraft:cobweb");
  const BlockDef* wool = blockByName("minecraft:white_wool");
  const BlockDef* ironOre = blockByName("minecraft:iron_ore");
  const BlockDef* planks = blockByName("minecraft:oak_planks");
  CHECK(stone && dirt && obsidian && bedrock && glass && torch && cobweb && wool &&
        ironOre && planks, "mining blocks present");
  if (!(stone && dirt && obsidian && bedrock && glass && torch && cobweb && wool &&
        ironOre && planks)) {
    std::printf("mining_full: %d/%d passed %d failed\n", g_pass, g_total, g_fail);
    return 1;
  }

  curSection = "canHarvest";
  CHECK(MiningCalculator::canHarvest(*stone, ToolKind::Pickaxe, ToolTier::Wood),
        "stone wooden-pick harvests");
  CHECK(!MiningCalculator::canHarvest(*stone, ToolKind::Hand, ToolTier::None_),
        "stone hand does not harvest");
  CHECK(!MiningCalculator::canHarvest(*stone, ToolKind::Axe, ToolTier::Diamond),
        "stone diamond-axe does not harvest");
  CHECK(MiningCalculator::canHarvest(*ironOre, ToolKind::Pickaxe, ToolTier::Stone),
        "iron_ore stone-pick harvests");
  CHECK(!MiningCalculator::canHarvest(*ironOre, ToolKind::Pickaxe, ToolTier::Wood),
        "iron_ore wooden-pick does not harvest");
  CHECK(!MiningCalculator::canHarvest(*obsidian, ToolKind::Pickaxe, ToolTier::Iron),
        "obsidian iron-pick does not harvest");
  CHECK(MiningCalculator::canHarvest(*obsidian, ToolKind::Pickaxe, ToolTier::Diamond),
        "obsidian diamond-pick harvests");
  CHECK(MiningCalculator::canHarvest(*dirt, ToolKind::Hand, ToolTier::None_),
        "dirt hand harvests");
  CHECK(MiningCalculator::canHarvest(*planks, ToolKind::Hand, ToolTier::None_),
        "planks hand harvests");
  CHECK(MiningCalculator::canHarvest(*cobweb, ToolKind::Sword, ToolTier::Wood),
        "cobweb wooden-sword harvests");
  CHECK(MiningCalculator::canHarvest(*cobweb, ToolKind::Shears, ToolTier::None_),
        "cobweb shears harvests");
  CHECK(!MiningCalculator::canHarvest(*cobweb, ToolKind::Hand, ToolTier::None_),
        "cobweb hand does not harvest");

  curSection = "ticks";
  using MC = MiningCalculator;
  CHECK_EQ_INT(MC::breakTicks(*stone, ToolKind::Pickaxe, ToolTier::Iron), 8,
               "iron-pick stone 8t (0.4s wiki)");
  CHECK_EQ_INT(MC::breakTicks(*obsidian, ToolKind::Pickaxe, ToolTier::Diamond), 188,
               "diamond-pick obsidian 188t (9.4s wiki)");
  CHECK_EQ_INT(MC::breakTicks(*stone, ToolKind::Hand, ToolTier::None_), 150,
               "hand stone 150t (7.5s wiki)");
  CHECK_EQ_INT(MC::breakTicks(*dirt, ToolKind::Hand, ToolTier::None_), 15,
               "hand dirt 15t (0.75s wiki)");
  CHECK_EQ_INT(MC::breakTicks(*glass, ToolKind::Hand, ToolTier::None_), 9,
               "hand glass 9t (0.45s wiki)");
  CHECK_EQ_INT(MC::breakTicks(*cobweb, ToolKind::Sword, ToolTier::Wood), 8,
               "sword cobweb 8t (0.4s wiki)");
  CHECK_EQ_INT(MC::breakTicks(*wool, ToolKind::Shears, ToolTier::None_), 5,
               "shears wool 5t (0.25s wiki)");
  CHECK_EQ_INT(MC::breakTicks(*torch, ToolKind::Hand, ToolTier::None_), 0,
               "torch instant");
  CHECK_EQ_INT(MC::breakTicks(*bedrock, ToolKind::Pickaxe, ToolTier::Netherite),
               MC::kUnbreakable, "bedrock unbreakable");
  CHECK_EQ_INT(MC::breakTicks(*obsidian, ToolKind::Pickaxe, ToolTier::Wood), 2500,
               "wooden-pick obsidian 2500t (no-harvest /100)");

  curSection = "modifiers";
  CHECK_EQ_INT(MC::breakTicks(*stone, ToolKind::Pickaxe, ToolTier::Diamond, 5), 2,
               "eff5 diamond-pick stone 2t");
  CHECK_EQ_INT(MC::breakTicks(*dirt, ToolKind::Hand, ToolTier::None_,
                              0, 0, 0, true, false, true), 75,
               "submerged dirt 75t (5x)");
  CHECK_EQ_INT(MC::breakTicks(*dirt, ToolKind::Hand, ToolTier::None_,
                              0, 0, 0, true, true, true), 15,
               "aqua-affinity submerged dirt 15t");
  CHECK_EQ_INT(MC::breakTicks(*dirt, ToolKind::Hand, ToolTier::None_,
                              0, 0, 0, false, false, false), 75,
               "airborne dirt 75t (/5)");
  CHECK_EQ_INT(MC::breakTicks(*dirt, ToolKind::Hand, ToolTier::None_,
                              0, 2, 0, false, false, true), 11,
               "haste2 dirt 11t");
  CHECK_EQ_INT(MC::breakTicks(*dirt, ToolKind::Hand, ToolTier::None_,
                              0, 0, 3, false, false, true), 556,
               "fatigue3 dirt 556t");
  CHECK(MC::destroySpeed(*stone, ToolKind::Pickaxe, ToolTier::Gold) == 12.f,
        "gold pick speed 12");
  CHECK(MC::destroySpeed(*dirt, ToolKind::Axe, ToolTier::Diamond) == 1.f,
        "wrong tool speed 1");

  curSection = "blast";
  CHECK(MC::blastSurvives(*obsidian, 4.f), "TNT: obsidian survives");
  CHECK(MC::blastSurvives(*bedrock, 4.f), "TNT: bedrock survives");
  CHECK(MC::blastSurvives(*stone, 4.f), "TNT point-blank: stone survives (6>=5.2)");
  CHECK(!MC::blastSurvives(*dirt, 4.f), "TNT: dirt destroyed");
  CHECK(!MC::blastSurvives(*glass, 4.f), "TNT: glass destroyed");
  CHECK(!MC::blastSurvives(*planks, 4.f), "TNT: planks destroyed");
  CHECK(MC::blastSurvives(*dirt, 4.f, 0.05f), "TNT far: dirt survives (exposure)");

  std::printf("mining_full: %d/%d passed %d failed\n", g_pass, g_total, g_fail);
  return g_fail ? 1 : 0;
}
