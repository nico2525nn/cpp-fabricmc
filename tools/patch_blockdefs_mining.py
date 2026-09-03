#!/usr/bin/env python3
"""Surgically patch src/generated/BlockStates.hpp: extend `struct BlockDef`
with plan44 G-03 mining fields and rewrite the 1095 kBlockDefs rows.

Emits EXACTLY what tools/gen_tables.py would emit for this section
(same tools/mining_fields.py:derive, same %.2f + field order), while leaving
every other line of the header byte-identical (mcmeta input needed for a full
regen is unavailable offline; the baked prop tables are untouched).

Usage (from repo root):
  BLOCKS_JSON=/path/blocks.json ITEMS_JSON=/path/items.json \
    python3 tools/patch_blockdefs_mining.py
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mining_fields import derive

BLOCKS_JSON = os.environ.get("BLOCKS_JSON", "/tmp/opencode/blocks.json")
ITEMS_JSON = os.environ.get("ITEMS_JSON", "/tmp/opencode/items.json")
HEADER = os.path.join(HERE, "..", "src", "generated", "BlockStates.hpp")

blocks = json.load(open(BLOCKS_JSON))
try:
    ITEM_BY_ID = {str(i["id"]): i["name"] for i in json.load(open(ITEMS_JSON))}
except OSError as e:
    sys.exit("need ITEMS_JSON (minecraft-data items.json): %s" % e)

mining = {}
for b in blocks:
    mining["minecraft:" + b["name"]] = derive(b, ITEM_BY_ID) + (float(b.get("hardness", -1)),)
assert len(mining) == 1095, len(mining)

src = open(HEADER).read()

OLD_STRUCT = """struct BlockDef {
  std::string_view name;
  std::uint32_t minState, maxState, defaultState;
  float hardness;
  std::uint8_t filterLight;   // 15 = opaque
  std::uint8_t emitLight;
  bool transparent;
  std::uint16_t propsOff;     // run start inside kBlockPropsRun
  std::uint8_t propCount;
};"""
NEW_STRUCT = """// Mining fields (plan44 G-03, mechanically derived from blocks.json):
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
};"""
if src.count(OLD_STRUCT) == 1:
    src = src.replace(OLD_STRUCT, NEW_STRUCT)
elif src.count(NEW_STRUCT) != 1:
    sys.exit("struct anchor not found (already patched? header changed?)")

ROW = re.compile(
    r'^\s*\{"(minecraft:[^"]+)", (\d+), (\d+), (\d+), ([0-9.\-]+)f, '
    r'(?:[0-9.\-]+f, \d+, \d+, \d+, )?(\d+), (\d+), (true|false), (\d+), (\d+)\},$')

out = []
n = 0
for line in src.split("\n"):
    m = ROW.match(line)
    if not m:
        out.append(line)
        continue
    name = m.group(1)
    assert name in mining, name
    req, eff, tier, blast, hard = mining[name]
    # MUST match gen_tables.py block_def_rows format string exactly.
    out.append('  {"%s", %s, %s, %s, %.2ff, %.2ff, %d, %d, %d, %s, %s, %s, %s, %s},' % (
        name, m.group(2), m.group(3), m.group(4), hard, blast,
        req, eff, tier, m.group(6), m.group(7), m.group(8),
        m.group(9), m.group(10)))
    n += 1
assert n == 1095, n
open(HEADER, "w").write("\n".join(out))
print("patched %s: %d rows" % (HEADER, n))
