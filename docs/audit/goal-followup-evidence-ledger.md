# Goal follow-up evidence ledger

This ledger records the evidence still needed after the current 90-row feature
ledger. It is deliberately additive: `goal-feature-ledger.md` remains the
coverage source of truth, and no row is promoted by a design note or a broad
smoke result. `UNVERIFIED` means that the implementation may exist but lacks a
row-specific product-entry assertion; `PARTIAL` means that a named assertion
exists while an explicit vanilla or state-transition contract is still missing.

## Snapshot and gates

| item | current evidence |
|---|---|
| protocol target | Java 1.21.4, protocol 769, DataVersion 4189 |
| feature ledger | 90 rows: `PASS=13`, `PARTIAL=39`, `UNVERIFIED=38` |
| fixed eligible count at this HEAD | `src/tests/tools`: 308 files, 101,014 physical lines; the historical baseline snapshot was 302 files, 100,991 lines |
| scoped source diff | `main...HEAD` under `src/`: 2,516 deletions and 2,250 additions, net reduction 266 lines |
| fixed local defects | `GB-G01..GB-G13` plus `NET-01..NET-10`; see the two bug ledgers |
| adversarial review | `P0=0`, `P1=0`; environment and product-scope limitations remain |
| accepted long soak | none; the 7200-second attempt stopped at `t=1200s` with exit `-9` |
| real GUI/client | unavailable on the current headless host |
| cleanup boundary | no protected feature, test, fixture, generated file, evidence, or virtual/plugin surface is a deletion target |

The fixed defect ledgers and the adversarial review are linked from
`docs/audit/README.md`. Their PASS results are not used to close the feature
rows below.

## P0 protocol and session follow-up

| row | status | real entry to run | missing assertion/artifact |
|---|---|---|---|
| #71 VarInt/VarLong/Position | PARTIAL | `test_server_full.py --suites=conn,chat` and `test_goal_live_matrix.py` | Independent boundary vectors for signed VarLong, packed Position, overflow rejection, and endian fields |
| #73 compression/encryption | PARTIAL | owned login fixture with online-mode/RSA/AES configuration | Cipher negotiation, encrypted frame oracle, and failure/cleanup transcript; current login only proves compression |
| #76 KeepAlive/Cookie/ResourcePack | PARTIAL | `test_server_full.py --suites=conn,chat` plus restart fixture | Cookie file round-trip, timeout at the configured deadline, and resource-pack result policy |
| #77 DeclareCommands | PASS (presence only) | `test_goal_live_matrix.py` login capture | Decode the complete Brigadier tree, redirects, parser IDs, and suggestion metadata rather than only packet presence |

## P0 commands and datapacks

| rows | status | production entry | missing assertion/artifact |
|---|---|---|---|
| #57–#59 | PARTIAL/UNVERIFIED | `test_server_full.py --suites=commands,chat` plus owned live `/gi` probe | The live fixture now decodes transaction `1`, range `1/2`, match `give`, and no tooltip for `/gi`; a full per-node Brigadier parser/redirect transcript, every argument type, and all argument-specific completion lists remain open |
| #60–#61 | PASS for named smoke | `test_server_full.py --suites=commands` | Error cases, permissions, block-state/NBT arguments, and exact `/fill` replacement/limit semantics |
| #62 | PARTIAL | real play command `execute as @p run ...` | `at`, `positioned`, `anchored`, nested selectors, and source-position assertions |
| #63, #69 | PARTIAL/UNVERIFIED | datapack fixture with `/function` and `/reload` | `minecraft:tick` tag dispatch, recursion limit, return/store semantics, and reload failure isolation |
| #64–#66, #68 | PASS/PARTIAL/UNVERIFIED | `test_server_full.py --suites=datapack,commands` | Complete tag inventory, ingredient-tag resolution, enable/disable behavior, and all datapack resource roots |
| #70 | PARTIAL | command fixture for `/tag`, `/team`, `/bossbar` | Complete subcommand and packet-state matrix, including remove/list/visibility/name/color/players |

## P0 survival, combat, and persistence

| rows | status | real entry to run | missing assertion/artifact |
|---|---|---|---|
| #81–#82 | UNVERIFIED | owned client using `/setblock` water/powder snow and waiting ticks | Air/freeze counters, damage cadence, water-breathing/freeze-resistance, and gamerule toggles |
| #83–#85 | PARTIAL/UNVERIFIED | owned client using fire/lava, movement, fall landing blocks | Lava/fire-resistance/extinguish paths and controlled fall mitigation on water, slime, honey, hay, and powder snow |
| #84 | UNVERIFIED | hunger fixture with sprint/jump/attack/use-food actions | Saturation/exhaustion transitions, regeneration/starvation cadence, and exact food packets |
| #87 | PARTIAL | `UseEntity` against a player and a mob | Player-victim knockback vector, armor/shield/critical modifiers, and resistance edge cases |
| #88 | UNVERIFIED | stop/restart fixture with player activity | `playerdata`, `stats`, and `advancements` field-level NBT/JSON oracle after restart |
| #89–#90 | PARTIAL | `/xp`, `/effect`, orb/effect entity fixture | Live values now cover XP progress/level/total (`0.7142857313/0/5`) and speed effect entity/id/amplifier/duration/flags (`1/1/0/100/6`); orb spawn/pickup/kill-drop semantics and the wider effect modifier/removal matrix remain open |

## P1 world, dimension, and block mechanics

| rows | status | production entry | missing assertion/artifact |
|---|---|---|---|
| #1–#2 | UNVERIFIED | non-flat Nether/End world fixture | Canonical chunk, biome, structure, and seed/call-order oracle; the current live fixture is flat overworld |
| #3–#7 | PARTIAL/UNVERIFIED | portal, light, spawn-ticket, simulation-distance, unload fixture | Round-trip portal safe-spawn/frame construction, cross-chunk light values, ticket retention, culling, LRU unload and restart evidence |
| #9–#10 | PARTIAL | persistence and `/worldborder` command fixture | Full `level.dat` DataVersion/dimension fields and warning/damage/gamerule/dimension border matrix |
| #11–#15 | UNVERIFIED | `UseItemOn`/`PlayerAction` with stairs, doors, crops, bonemeal, farmland | Placement context, two-block atomicity, random-tick probability, bonemeal growth, and trample state/particles |
| #17–#18 | UNVERIFIED | TNT and bucket item-action fixture | Fuse/explosion propagation, dispenser behavior, source/flowing bucket transitions, and flint damage |
| #21–#27 | UNVERIFIED | hopper/comparator/observer/rail/dispenser/dropper/cactus fixture | Inventory transfer, redstone analog/output timing, observer pulse, rail shape, per-item dispenser effects, and growth state |

## P1 entities, menus, and projectiles

| rows | status | real entry to run | missing assertion/artifact |
|---|---|---|---|
| #8, #16, #19, #20, #24 | PARTIAL | block-action fixture with tick/state capture | Structure placement, fire, piston transient NBT, fluid solidification, and rail edge cases |
| #28–#45 | PARTIAL/UNVERIFIED | `/summon`, `UseEntity`, `UseItem`, and tick fixture | Full mob roster/equipment/passengers, durability/enchant effects, split/boss AI, breeding/trading, projectile metadata/impact, and vehicle physics |
| #46–#50 | PARTIAL | menu fixture using `UseItemOn` and container packets | Barrel/shulker/anvil/brewing/stonecutter item mutation, result clicks, recipe previews, and persistence |
| #51–#55 | PARTIAL/UNVERIFIED | `SetCreativeModeSlot`, `ContainerClick`, drag mode 5 | Cursor/stack/component validation, every click mode, exact `OpenScreen`/content/set-slot/data state and hopper transfer |

The projectile-specific gap is intentional: a spawn and movement packet does
not prove entity-specific object data, metadata, swept collision, impact
effects, pickup, or pearl safe-space/teleport-confirm behavior.

## Review and release boundaries

| boundary | status | required next artifact |
|---|---|---|
| arbitrary Fabric `GameProvider`/JVM mod compatibility | PARTIAL | independent arbitrary-mod corpus and official-provider bootstrap; selected bounded probes are not universal compatibility |
| vanilla worldgen L3 parity | PARTIAL | seeded call-order trace plus canonical chunk/biome/structure NBT comparison |
| signed command argument transcript | PARTIAL | authenticated client transcript; secure enforcement currently fails closed |
| moving-piston transient NBT | PARTIAL | save/reload during an active piston movement and compare entity/block state |
| real client GUI/first login/release artifact | UNAVAILABLE | display-backed vanilla client run with retained artifact; `xvfb-run` alone is insufficient |
| accepted 2-hour/24-hour soak | TODO | complete owned-process run with RSS/keepalive/disconnect/cleanup result; the prior 7200-second run is negative evidence |
| complete marker/comment inventory | UNVERIFIED | reproducible full-tree inventory with a fixed allowlist and artifact |

## Verification routing

Use the exact existing entry points before changing a row:

```text
timeout --foreground --kill-after=5 600 python3 tests/test_server_full.py --binary ./build/cppfm --suites=conn,commands,permissions,chat,datapack,persistence,restart
timeout --foreground --kill-after=5 180 python3 tests/test_goal_live_matrix.py --binary ./build/cppfm
timeout --foreground --kill-after=5 180 python3 tests/test_goal_live_remaining.py --binary ./build/cppfm
timeout --foreground --kill-after=5 120 python3 tests/bot_smoke.py --binary ./build/cppfm --duration 30
```

Each promotion requires a row ID, a product-entry command, a named assertion,
and an artifact path. Broad CTest, packet presence, source inspection, or a
passing implementation unit test alone cannot promote an `UNVERIFIED` row.

The latest row-specific live probe uses the existing `test_goal_live_features.py`
entrypoint with an owned `cppfm` process. Its retained JSON records the exact
`/gi`, `/xp`, and `/effect` observations above; the fixture still exits nonzero
if any field parser or value assertion fails.
