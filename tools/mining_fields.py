#!/usr/bin/env python3
"""Shared mechanical derivation of per-block mining fields from PrismarineJS
minecraft-data blocks.json (+ items.json for harvestTools id -> tier mapping).

Source of truth: blocks.json fields `material`, `harvestTools`, `resistance`.
Used by tools/gen_tables.py (full regen) and tools/patch_blockdefs_mining.py
(surgical BlockDef-only patch while mcmeta input is unavailable).

Field semantics (vanilla 1.21.4, cf. plan44 §2 / wiki Breaking):
  toolMask (uint8): REQUIRED tool mask for harvest (drop). 0 = hand harvests.
    bit0 pickaxe, bit1 axe, bit2 shovel, bit3 hoe, bit4 sword, bit5 shears.
    Rule: harvestTools present -> union of mineable/* bits from `material`
    and tool-type bits from harvestTools item names; absent -> 0.
  effMask (uint8): tools that mine FASTER (destroy speed > 1). Same bits.
    Rule: toolMask plus shears heuristic for wool / leaves / vine / cobweb
    materials (vanilla ShearsItem effectiveness; these have harvestTools None
    so toolMask alone would lose the info). `incorrect_for_wooden_tool`
    implies pickaxe effectiveness (covers crafter, the single block with that
    material and harvestTools None).
  needsTier (uint8): minimum harvest level of the REQUIRED tool.
    0 = no tier requirement (hand, or any sword/shears for cobweb),
    1 = wooden+, 2 = stone+, 3 = iron+, 4 = diamond+.
    (golden harvests exactly like wooden in vanilla Tier.java, rank 0;
    no vanilla block requires netherite, so 5 is unused.)
    Rule: harvestTools None -> 0; only swords/shears in harvestTools -> 0;
    else min item tier rank + 1.
  blastResistance (float): blocks.json `resistance` verbatim (TNT/creeper math).
"""

TOOL_PICKAXE = 1
TOOL_AXE = 2
TOOL_SHOVEL = 4
TOOL_HOE = 8
TOOL_SWORD = 16
TOOL_SHEARS = 32

MINEABLE_BITS = {
    "mineable/pickaxe": TOOL_PICKAXE,
    "mineable/axe": TOOL_AXE,
    "mineable/shovel": TOOL_SHOVEL,
    "mineable/hoe": TOOL_HOE,
}

# vanilla Tier harvest ranks (Yarn Tier.java): GOLD behaves like WOOD.
TIER_RANK = {"wooden": 0, "golden": 0, "stone": 1, "iron": 2,
             "diamond": 3, "netherite": 4}

# materials where vanilla shears are speed-effective (ShearsItem).
_SHEARS_MATS = ("wool", "leaves", "vine_or_glow_lichen", "coweb")


def _tool_bits_of_item(name):
    if name == "shears":
        return TOOL_SHEARS
    if name.endswith("_sword"):
        return TOOL_SWORD
    if name.endswith("_pickaxe"):
        return TOOL_PICKAXE
    if name.endswith("_axe"):
        return TOOL_AXE
    if name.endswith("_shovel"):
        return TOOL_SHOVEL
    if name.endswith("_hoe"):
        return TOOL_HOE
    return 0


def derive(b, item_by_id):
    """Return (toolMask, effMask, needsTier, blastResistance) for one
    blocks.json entry `b`. `item_by_id`: {numeric id str/int: item name}."""
    mats = set((b.get("material") or "default").split(";"))
    ht = b.get("harvestTools") or {}

    req = 0
    mat_bits = 0
    for m in mats:
        mat_bits |= MINEABLE_BITS.get(m, 0)
    ht_names = []
    ht_bits = 0
    for tid in ht:
        nm = item_by_id.get(tid, item_by_id.get(int(tid), ""))
        ht_names.append(nm)
        ht_bits |= _tool_bits_of_item(nm)
    if ht:
        req = mat_bits | ht_bits

    # effective = every tool that mines faster, even when hand harvests
    # (oak_planks: hand drops, axe speeds -> eff=axe).
    eff = mat_bits | ht_bits
    if mats & set(_SHEARS_MATS):
        eff |= TOOL_SHEARS
    if "incorrect_for_wooden_tool" in mats:
        eff |= TOOL_PICKAXE

    if not ht:
        tier = 0
    else:
        ranks = []
        for nm in ht_names:
            if nm == "shears" or nm.endswith("_sword"):
                continue
            for prefix, rank in TIER_RANK.items():
                if nm == prefix + "_pickaxe" or nm == prefix + "_axe" or \
                   nm == prefix + "_shovel" or nm == prefix + "_hoe":
                    ranks.append(rank)
                    break
        tier = (min(ranks) + 1) if ranks else 0

    blast = b.get("resistance")
    blast = float(blast) if blast is not None else 0.0
    return req, eff, tier, blast
