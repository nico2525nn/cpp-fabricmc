// test_block_hardness_full — plan44 G-02 (hardness 1095 lock) + G-03 (mining fields).
// Compares every gen::kBlockDefs row against tests/data/block_mining_expected.inc
// (generated from minecraft-data blocks.json by the INDEPENDENT
// tools/check_hardness.py — shares no code with tools/gen_tables.py).
// Policy: vanilla spec wins. Any mismatch FAILs.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "generated/BlockStates.hpp"
#include "data/block_mining_expected.inc"

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

int main() {
  using namespace cppfm::gen;
  curSection = "hardness1095";
  // blastResistance is float32 in BlockDef: barrier/light 3600000.8f loses
  // sub-0.25 precision, so blast uses abs eps 0.5 (all other values exact).
  int mismatch = 0, hardBad = 0, blastBad = 0, toolBad = 0, checked = 0;
  for (int i = 0; i < kExpectedMiningCount; ++i) {
    const ExpectedMining& e = kExpectedMining[i];
    const BlockDef* d = blockByName(e.name);
    if (!d) {
      ++mismatch;
      std::printf("  FAIL [hardness1095] missing in kBlockDefs: %s\n", e.name);
      continue;
    }
    ++checked;
    bool ok = true;
    if (std::fabs((double)d->hardness - (double)e.hardness) > 1e-6) { ok = false; ++hardBad; }
    if (std::fabs((double)d->blastResistance - (double)e.blast) > 0.5) { ok = false; ++blastBad; }
    if (d->toolMask != e.toolMask || d->effMask != e.effMask || d->needsTier != e.needsTier) {
      ok = false; ++toolBad;
    }
    if (!ok) {
      ++mismatch;
      if (mismatch <= 10)
        std::printf("  FAIL [hardness1095] %s: got(h=%.4f b=%.2f t=%u e=%u n=%u)"
                    " want(h=%.4f b=%.2f t=%u e=%u n=%u)\n", e.name,
                    (double)d->hardness, (double)d->blastResistance,
                    d->toolMask, d->effMask, d->needsTier,
                    (double)e.hardness, (double)e.blast,
                    e.toolMask, e.effMask, e.needsTier);
    }
  }
  std::printf("  checked=%d mismatch=%d (hard=%d blast=%d tool=%d)\n",
              checked, mismatch, hardBad, blastBad, toolBad);
  CHECK_EQ_INT(checked, 1095, "all 1095 blocks checked");
  CHECK_EQ_INT(mismatch, 0, "hardness/blast/tools 1095 mismatch 0");

  curSection = "spot";
  auto findDef = [&](const char* n) -> const BlockDef* { return blockByName(n); };
  if (auto d = findDef("minecraft:stone")) {
    CHECK(std::fabs(d->hardness - 1.5) < 1e-6, "stone hardness 1.5");
    CHECK(d->toolMask == 1 && d->needsTier == 1, "stone pickaxe/wooden+");
    CHECK(std::fabs(d->blastResistance - 6.0) < 1e-6, "stone blast 6");
  } else CHECK(false, "stone present");
  if (auto d = findDef("minecraft:obsidian")) {
    CHECK(std::fabs(d->hardness - 50.0) < 1e-6, "obsidian hardness 50");
    CHECK(d->toolMask == 1 && d->needsTier == 4, "obsidian pickaxe/diamond+");
    CHECK(std::fabs(d->blastResistance - 1200.0) < 0.5, "obsidian blast 1200");
  } else CHECK(false, "obsidian present");
  if (auto d = findDef("minecraft:bedrock")) {
    CHECK(d->hardness < 0, "bedrock unbreakable");
    CHECK(std::fabs(d->blastResistance - 3600000.0) < 0.5, "bedrock blast 3.6M");
  } else CHECK(false, "bedrock present");
  if (auto d = findDef("minecraft:cobweb")) {
    CHECK(d->toolMask == 48 && d->effMask == 48, "cobweb sword|shears");
    CHECK(d->needsTier == 0, "cobweb no tier gate");
  } else CHECK(false, "cobweb present");
  if (auto d = findDef("minecraft:white_wool")) {
    CHECK(d->toolMask == 0 && d->effMask == 32, "wool hand-harvest, shears-fast");
  } else CHECK(false, "wool present");
  if (auto d = findDef("minecraft:oak_planks")) {
    CHECK(d->toolMask == 0 && d->effMask == 2, "planks hand-harvest, axe-fast");
  } else CHECK(false, "planks present");
  if (auto d = findDef("minecraft:suspicious_sand"))
    CHECK(std::fabs(d->hardness - 0.25) < 1e-6, "suspicious_sand 0.25 (%.2f fix)");
  else CHECK(false, "suspicious_sand present");
  if (auto d = findDef("minecraft:basalt"))
    CHECK(std::fabs(d->hardness - 1.25) < 1e-6, "basalt 1.25 (%.2f fix)");
  else CHECK(false, "basalt present");

  std::printf("block_hardness_full: %d/%d passed %d failed\n", g_pass, g_total, g_fail);
  return g_fail ? 1 : 0;
}
