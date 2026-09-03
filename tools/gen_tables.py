#!/usr/bin/env python3
"""Generate src/generated/BlockStates.hpp from PrismarineJS blocks.json plus
misode/mcmeta block-state summaries (clean-room community datasets).

Emits:
  * legacy symbols kept byte-compatible: kMaxBlockStateId, BlockEntry, kBlocks,
    blockNameToState()
  * rich full-state tables: per-block property descriptors (ordered values),
    default states, light/hardness/opacity data, and helpers to map
    (block, properties) <-> exact global state id.
"""
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mining_fields import derive

PRISMA_BLOCKS = os.environ.get("BLOCKS_JSON", "/tmp/opencode/blocks.json")
MCMETA_BLOCKS = os.environ.get("MCMETA_BLOCKS", "/tmp/opencode/mcmeta_blocks.json")
PRISMA_ITEMS = os.environ.get("ITEMS_JSON", "/tmp/opencode/items.json")
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "src", "generated", "BlockStates.hpp")

data = json.load(open(PRISMA_BLOCKS))
mcmeta = json.load(open(MCMETA_BLOCKS))
try:
    items_data = json.load(open(PRISMA_ITEMS))
    ITEM_BY_ID = {str(i["id"]): i["name"] for i in items_data}
except OSError:
    print("WARN items.json missing (%s): toolMask/needsTier default 0" % PRISMA_ITEMS,
          file=sys.stderr)
    ITEM_BY_ID = {}

# ---------------------------------------------------------------- legacy rows
rows = []
for b in data:
    name = "minecraft:" + b["name"]
    st = b.get("defaultState")
    if st is None:
        st = b.get("minStateId")
    rows.append((name, int(st)))
rows.sort(key=lambda r: r[1])
max_state = max(r[1] for r in rows)
legacy_entries = "\n".join('  {"%s", %d},' % (n, s) for n, s in rows)

# ------------------------------------------------------------- full-state data
value_pool = []          # unique strings of every property value
value_index = {}
def pool_id(s):
    if s not in value_index:
        value_index[s] = len(value_pool)
        value_pool.append(s)
    return value_index[s]

prop_defs = []           # PropDef entries
prop_lookup = {}         # (name, tuple(values)) -> prop def id
def prop_id(name, values):
    key = (name, tuple(values))
    if key not in prop_lookup:
        prop_lookup[key] = len(prop_defs)
        prop_defs.append((name, [pool_id(v) for v in values]))
    return prop_lookup[key]

blocks_full = []
for b in data:
    name = "minecraft:" + b["name"]
    short = b["name"]
    states = b.get("states", [])
    mcm = mcmeta.get(short)
    # property descriptors in declaration order
    plist = []
    for s in states:
        vals = s.get("values")
        if vals is None:
            # derive from mcmeta when prismarine omits explicit values
            if mcm and mcm and isinstance(mcm, list) and mcm:
                defs = mcm[0]
                vals = defs.get(s["name"])
            if vals is None:
                print("WARN no values for %s.%s" % (short, s["name"]), file=sys.stderr)
                vals = []
        plist.append(prop_id(s["name"], list(vals)))
    # defaults from mcmeta ([1] holds default property values)
    defaults = {}
    if mcm and isinstance(mcm, list) and len(mcm) > 1:
        defaults = mcm[1]
    # verify count & compute default index using declared value orders
    stride = 1
    default_idx = 0
    for i in range(len(states) - 1, -1, -1):
        pname = states[i]["name"]
        nv = states[i].get("num_values") or len(
            (prop_defs[plist[i]][1]))
        dv = defaults.get(pname)
        vi = 0
        if dv is not None:
            vals = [value_pool[x] for x in prop_defs[plist[i]][1]]
            if dv in vals:
                vi = vals.index(dv)
            else:
                print("WARN default %s=%s not in values (%s)" % (pname, dv, short),
                      file=sys.stderr)
        default_idx += vi * stride
        stride *= nv
    mn, mx, df = int(b["minStateId"]), int(b["maxStateId"]), int(b["defaultState"])
    if stride != (mx - mn + 1):
        print("WARN count mismatch %s: stride=%d range=%d" % (short, stride, mx - mn + 1),
              file=sys.stderr)
    if mn + default_idx != df:
        print("WARN default mismatch %s: computed %d actual %d"
              % (short, mn + default_idx, df), file=sys.stderr)
    blocks_full.append({
        "name": name,
        "min": mn, "max": mx, "def": df,
        "hardness": float(b.get("hardness", -1)),
        "filter": int(b.get("filterLight", 15)),
        "emit": int(b.get("emitLight", 0)),
        "transparent": bool(b.get("transparent", False)),
        "props": plist,
        # plan44 G-03: mining fields mechanically derived from blocks.json
        # (tools/mining_fields.py:derive). %.2f: all 32 distinct hardness and
        # all 34 distinct resistance values are exact at 2 decimals (%.1f
        # rounded 0.25->0.2 / 1.25->1.2 on 23 blocks, fixed here).
        "mining": derive(b, ITEM_BY_ID),
    })
blocks_full.sort(key=lambda x: x["min"])

prop_run = []
for bd in blocks_full:
    bd["propsOff"] = len(prop_run)
    bd["propCount"] = len(bd["props"])
    prop_run.extend(bd["props"])

block_def_rows = "\n".join(
    '  {"%s", %d, %d, %d, %.2ff, %.2ff, %d, %d, %d, %d, %d, %s, %d, %d},' % (
        bd["name"], bd["min"], bd["max"], bd["def"], bd["hardness"],
        bd["mining"][3], bd["mining"][0], bd["mining"][1], bd["mining"][2],
        bd["filter"], bd["emit"], "true" if bd["transparent"] else "false",
        bd["propsOff"], bd["propCount"])
    for bd in blocks_full)
prop_run_rows = ", ".join(str(x) for x in prop_run)
prop_def_rows = "\n".join(
    '  {"%s", %d, %d},' % (n, first, len(ids))
    for (n, ids), (nm, _) in zip(((pd[0], pd[1]) for pd in prop_defs), prop_defs)
    for first, _ in [(prop_defs[prop_defs.index((n, ids))][1][0], 0)]
) if False else "\n".join(
    '  {"%s", %d, %d},' % (n, ids[0], len(ids)) for n, ids in prop_defs)
value_rows = "\n".join('  "%s",' % v.replace('"', '\\"') for v in value_pool)

h = """// GENERATED by tools/gen_tables.py -- DO NOT EDIT BY HAND.
// Block state id tables for protocol 769 (1.21.4).
// Sources: PrismarineJS minecraft-data blocks.json + misode/mcmeta block summary
// (community-maintained clean-room datasets).
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cppfm::gen {

inline constexpr std::uint16_t kMaxBlockStateId = %(max_state)d;

struct BlockEntry { std::string_view name; std::uint32_t state; };

inline constexpr std::array<BlockEntry, %(n_legacy)d> kBlocks = {{
%(legacy)s
}};

// name -> default state id
inline const std::unordered_map<std::string_view, std::uint32_t>& blockNameToState() {
  static const std::unordered_map<std::string_view, std::uint32_t> m = [] {
    std::unordered_map<std::string_view, std::uint32_t> mm;
    mm.reserve(kBlocks.size());
    for (auto& e : kBlocks) mm.emplace(e.name, e.state);
    return mm;
  }();
  return m;
}

// ============================ full state tables ============================

// Unique property value strings, shared by all property definitions.
inline constexpr std::array<std::string_view, %(n_values)d> kPropValuePool = {{
%(values)s
}};

// A named property with its ordered possible values (vanilla declaration order).
struct PropDef { std::string_view name; std::uint16_t firstValue; std::uint16_t numValues; };
inline constexpr std::array<PropDef, %(n_props)d> kPropDefs = {{
%(props)s
}};

// One definition per block, sorted by minState (binary-searchable by state id).
// Mining fields (plan44 G-03, mechanically derived from blocks.json):
//   blastResistance: explosion resistance (TNT math)
//   toolMask: REQUIRED tool bits for harvest (0 = hand harvests)
//   effMask: tools mining faster (destroy speed > 1)
//   needsTier: min harvest level (0 none, 1 wooden+, 2 stone+, 3 iron+, 4 diamond+)
// Tool bits: bit0 pickaxe, bit1 axe, bit2 shovel, bit3 hoe, bit4 sword, bit5 shears.
struct BlockDef {
  std::string_view name;
  std::uint32_t minState, maxState, defaultState;
  float hardness;
  float blastResistance;
  std::uint8_t toolMask;
  std::uint8_t effMask;
  std::uint8_t needsTier;
  std::uint8_t filterLight;   // 15 = opaque
  std::uint8_t emitLight;
  bool transparent;
  std::uint16_t propsOff;     // run start inside kBlockPropsRun
  std::uint8_t propCount;
};
inline constexpr std::array<BlockDef, %(n_blocks)d> kBlockDefs = {{
%(blocks)s
}};

// Flattened runs of PropDef indices, one run per block (see BlockDef::propsOff).
inline constexpr std::array<std::uint16_t, %(n_run)d> kBlockPropsRun = {{ %(run)s }};

// Block containing this exact state id (nullptr if out of range).
inline const BlockDef* blockByState(std::uint32_t state) {
  std::size_t lo = 0, hi = kBlockDefs.size();
  while (lo < hi) {
    const std::size_t mid = (lo + hi) / 2;
    if (state < kBlockDefs[mid].minState) hi = mid;
    else if (state > kBlockDefs[mid].maxState) lo = mid + 1;
    else return &kBlockDefs[mid];
  }
  return nullptr;
}
inline const BlockDef* blockByName(std::string_view name) {
  // linear over ~1.1k names is fine at startup; callers should cache
  for (const auto& b : kBlockDefs) if (b.name == name) return &b;
  return nullptr;
}
inline const PropDef& propAt(std::uint16_t id) { return kPropDefs[id]; }

// Properties of a state, in declaration order (name,value).
inline std::vector<std::pair<std::string_view, std::string_view>> propsOf(std::uint32_t state) {
  std::vector<std::pair<std::string_view, std::string_view>> out;
  const BlockDef* b = blockByState(state);
  if (!b || !b->propCount) return out;
  std::uint32_t idx = state - b->minState;
  std::int32_t strides[16]; std::uint8_t nv[16];
  std::int32_t stride = 1;
  for (int i = b->propCount - 1; i >= 0; --i) {
    const PropDef& pd = kPropDefs[kBlockPropsRun[b->propsOff + i]];
    strides[i] = stride; nv[i] = pd.numValues;
    stride *= pd.numValues;
  }
  for (int i = 0; i < b->propCount; ++i) {
    const PropDef& pd = kPropDefs[kBlockPropsRun[b->propsOff + i]];
    const std::uint16_t vi = static_cast<std::uint16_t>((idx / strides[i]) %% pd.numValues);
    out.emplace_back(pd.name, kPropValuePool[pd.firstValue + vi]);
  }
  return out;
}

// Resolve the state id of `block` for given properties (unknown keys/values fall
// back to the block default).
inline std::uint32_t stateWithProps(const BlockDef& b,
    const std::vector<std::pair<std::string_view, std::string_view>>& props) {
  std::uint32_t idx = b.defaultState - b.minState;
  if (!b.propCount) return b.minState + idx;
  // decode current default into per-prop indices first
  std::uint16_t cur[16];
  std::int32_t strides[16];
  std::int32_t stride = 1;
  for (int i = b.propCount - 1; i >= 0; --i) {
    const PropDef& pd = kPropDefs[kBlockPropsRun[b.propsOff + i]];
    strides[i] = stride;
    cur[i] = static_cast<std::uint16_t>(((b.defaultState - b.minState) / stride) %% pd.numValues);
    stride *= pd.numValues;
  }
  for (auto& [k, v] : props) {
    for (int i = 0; i < b.propCount; ++i) {
      const PropDef& pd = kPropDefs[kBlockPropsRun[b.propsOff + i]];
      if (pd.name != k) continue;
      for (std::uint16_t t = 0; t < pd.numValues; ++t)
        if (kPropValuePool[pd.firstValue + t] == v) { cur[i] = t; break; }
      break;
    }
  }
  std::uint32_t out = 0;
  for (int i = 0; i < b.propCount; ++i) out += static_cast<std::uint32_t>(cur[i]) * strides[i];
  return b.minState + out;
}
inline std::uint32_t stateWithPropsList(std::string_view name,
    const std::vector<std::pair<std::string_view, std::string_view>>& props) {
  const BlockDef* b = blockByName(name);
  return b ? stateWithProps(*b, props) : 0;
}

} // namespace cppfm::gen
""" % {
    "max_state": max_state,
    "n_legacy": len(rows),
    "legacy": legacy_entries,
    "n_values": len(value_pool),
    "values": value_rows,
    "n_props": len(prop_defs),
    "props": prop_def_rows,
    "n_blocks": len(blocks_full),
    "blocks": block_def_rows,
    "n_run": len(prop_run),
    "run": prop_run_rows,
}

open(OUT, "w").write(h)
print("wrote %s: %d blocks, maxState=%d, props=%d values=%d run=%d"
      % (OUT, len(blocks_full), max_state, len(prop_defs), len(value_pool), len(prop_run)))
