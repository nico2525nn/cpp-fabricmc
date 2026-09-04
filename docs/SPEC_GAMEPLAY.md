# SPEC_GAMEPLAY — world and behavior contract

This document describes the current behavior surface for Minecraft Java 1.21.4,
protocol 769, and DataVersion 4189 at merge baseline
`f21e42327342fe1e8486960f2c43805711280ffd` (rechecked 2026-09-04). It covers
MISSING **#1–#70**, **#80–#90**, the Fabric-specific rows, and the world-generation
G-10/G-11 evidence. Packet fields remain in
[SPEC_WIRE.md](SPEC_WIRE.md); operational thresholds remain in
[SPEC_OPS.md](SPEC_OPS.md).

**Status:** source-backed behavior contract, not a claim of universal vanilla
byte-identical gameplay. **Limitations:** Fabric JVM mods cannot execute in the C++
process; world-generation seed parity is self-consistent L1/L2 but vanilla Xoroshiro
byte parity is not proven (L3); simplified features are called out with
`DECLARED-LIMITATION`.

## 1. Feature overview

The gameplay contract has seven domains:

1. world, dimensions, chunks, registries, and persistence;
2. block placement, scheduled/random ticks, fluids, redstone, and light;
3. world generation, density, biomes, and structures;
4. entity registry, MobStats, AI, spawning, projectiles, and bosses;
5. items, data components, menus, recipes, and villager trades;
6. commands, datapacks, tags, loot, predicates, and advancements; and
7. combat, damage, effects, hunger, survival, time, and weather.

Each domain records source symbols, an event/state flow, MISSING target numbers, and
evidence rather than copying an old audit table.

## 2. Vanilla/reference specification

| provenance | use | label |
|---|---|---|
| Yarn 1.21.4 concepts and Fabric 1.21.4 release documentation | names and lifecycle concepts | `VANILLA-CONCEPT` |
| Minecraft Wiki and generated 1.21.4 data | numerical rules and registries | `VANILLA-CONCEPT` |
| reference server/client captures | observable behavior | `CAPTURED` |
| current C++ definitions | implementation fact | `IMPLEMENTATION` |
| `tests/test_gameplay_full.cpp` and focused tests | regression evidence | `WIRE-ORACLE`/`CAPTURED` as applicable |

The version boundary is strict: dimensions use the 1.21.4 world height of 384 blocks
(minimum Y `-64`, exclusive upper bound `320`) and 24 sixteen-block sections. A
simulation-distance decision is separate from view-distance sending and uses the
Chebyshev neighborhood (`max(abs(dx), abs(dz))`) in the server path. A claim that
only says “the class exists” is not treated as full vanilla parity.

## 3. Classes and data structures

| domain | implementation/data | input/state | output/evidence | status/provenance |
|---|---|---|---|---|
| world/chunk | `src/game/World.hpp::World`, `Chunk` | dimension, seed, block/biome arrays, revision | lazy chunk generation, block lookup and edits | `IMPLEMENTATION` |
| persistence | `src/game/WorldDataManager.*`, `Persistence.hpp`, `Anvil.hpp`, `RegionFile.hpp` | NBT, region entries, DataVersion | level/region/player state | `IMPLEMENTATION` + `test_recovery` |
| block/physics | `src/physics/BlockTickScheduler.*`, `Fluids.*`, `Redstone.*`, `LightEngine.*` | state changes, tick number, gamerules | scheduled updates, light, redstone/fluid consequences | `IMPLEMENTATION` + focused tests |
| worldgen | `src/worldgen/DensityFunction.*`, `MultiNoise.*`, `StructureManager.*`, `StructurePlacer.*` | seed, coordinates, dimension | biome and structure decisions | L1/L2 `IMPLEMENTATION`; L3 `DECLARED-LIMITATION` |
| entity | `src/game/Entities.hpp`, `EntityData.*`, `BehaviorTree.*`, `AiBrain.*`, `MobSpawner.*` | entity state and tick context | AI, movement, spawn, metadata, drops | `IMPLEMENTATION` + gameplay/smoke |
| mob fixture | `docs/mob_stats_149.csv`, `Entities.hpp::MobStats` | 11 CSV columns, MobKind index | 149-row table and stat lookup | `IMPLEMENTATION` + `test_mob_stats_full` |
| inventory | `src/game/Items.hpp`, `Containers.*`, `MenuInteraction.*`, `InventoryController.*` | Slot/components, menu click | authoritative inventory and sync trigger | `IMPLEMENTATION` + wire/gameplay |
| recipes/data | `Recipes.*`, `TagManager.hpp`, `DatapackManager.hpp`, `FunctionEvaluator.*`, Brigadier | JSON, tags, commands | crafting, reload, functions and suggestions | `IMPLEMENTATION`; simplified functions are declared |
| combat/survival | `CombatManager.*`, `DamageSource.hpp`, `Attributes.hpp`, `HungerManager.*`, `MobEffects.hpp` | source, attributes, effects, food | damage, knockback, hunger and status effects | focused tests + `IMPLEMENTATION` |

The CSV has 11 columns: name, health, repo-local movement speed, attack, follow
range, XP, drop, minimum drop, maximum drop, breeding item, and provenance. Its
default path is a stable runtime API, not disposable documentation.

## 4. Packet consequences

Gameplay does not duplicate field tables. It names the observable wire consequence:

| behavior | cause/order | wire reference |
|---|---|---|
| block edit | `World::setBlock` → hooks/physics → queued broadcast | `BlockUpdate`/`MultiBlockChange` in [SPEC_WIRE.md#packet-contract-table](SPEC_WIRE.md#packet-contract-table) |
| chunk/light change | generation or LightEngine drain | `LevelChunkWithLight`/`UpdateLight` |
| menu mutation | click validation → authoritative stack change → cursor/content sync | `OpenScreen`, `ContainerSetContent`, `ContainerSetSlot` |
| entity damage/spawn | tracking and state change → metadata/equipment/velocity/damage | actor packet contracts in WIRE |
| command feedback | dispatch result | `SystemChat`/command suggestions |

The field bytes and IDs are owned by WIRE. This separation prevents a gameplay
description from accidentally reviving an old packet ID.

## 5. Events and ordering

The current event hooks are explicit C++ callbacks, not a Fabric JVM event bus.

1. `World::setBlock` writes the state, calls `onBlockChanged`, emits place/break or
   replacement hooks, notifies six neighbors, and marks the chunk edited.
2. Block physics schedules fluid, redstone, light, gravity, and random-tick work;
   simulation-distance gates prevent ordinary off-range ticking.
3. `GameServer::tickOnce` runs the tick pipeline: scheduled functions and block work,
   survival, XP, mobs/AI, projectiles, persistence cadence, and packet flushes.
4. Menu clicks are parsed by `MenuInteraction::ClickLogic`, mutate the authoritative
   menu/player inventory, then resynchronize slots/cursor.
5. Entity spawn/damage/death updates tracking, metadata/equipment, XP/loot, and
   persistence where that path is implemented.
6. `/reload` reloads supported recipes/tags/loot and function/data managers without
   retaining invalid stale references.

## 6. State transitions

```text
chunk: missing → generated/loaded → dirty → saved → evicted
block: placed → scheduled/random tick → neighbor update → stable
entity: spawn → active/tick → damage/effect → death/despawn → persistence/loot
menu: closed → open → click/drag → authoritative resync → close
recipe/data: discover → load/resolve tags → execute → reload
dimension: current world → cache invalidation/Respawn → safe spawn → current world
```

World generation is deterministic for the same implementation and seed. It is not
documented as byte-identical vanilla RNG at L3. Dimension switching resets the
client-visible dimension state, abilities, and safe spawn path where implemented.

## 7. Reproduction and implementation flow

For a behavior claim:

1. state the MISSING target and domain;
2. find the source symbol and data input;
3. identify event/tick ordering and state transitions;
4. link a focused or integration test and distinguish a real-client capture from a
   unit approximation;
5. label exact behavior, approximation, alternative, or declared limitation; and
6. keep packet details in WIRE and thresholds in OPS.

No gameplay code is added by this documentation change. Existing plan47 cleanup
and line-reduction work is not proposed again.

## 8. C++ behavior-contract example

```text
Block change pipeline
- MISSING: #11–#27, #75, #78
- input: World::setBlock(x, y, z, state)
- ordering: onBlockChanged → place/break/neighbor hooks → physics scheduling
- persistence: chunk revision/dirty state → Persistence save cadence
- network: PacketBatcher queue; WIRE owns bytes
- evidence: test_gameplay_full, test_redstone_engine_full, test_spec_wire, smoke80
- status: IMPLEMENTATION path; verify against current test run
```

This is a documentation form for existing behavior, not an API proposal.

## 9. Source/class composition

| layer | source symbols |
|---|---|
| world | `World`, `Chunk`, `WorldManager`, `ChunkTicketManager` |
| storage | `WorldDataManager`, `Persistence`, `RegionFile`, `SessionLock` |
| physics | `BlockTickScheduler`, `FluidSim`, `RedstoneEngine`, `LightEngine` |
| generation | `MultiNoiseBiomeSource`, `DensityFunction`, `StructureManager`, `StructurePlacer` |
| entities | `MobEntity`, `mobStats`, `BehaviorTreeParser`, `AiBrain`, `BossAI` |
| inventory | `ItemStack`, `Menu`, `ClickLogic`, `RecipeManager`, `TagManager` |
| commands | Brigadier `CommandDispatcher`, `Commands`, `DatapackManager`, `FunctionEvaluator` |
| survival | `CombatManager`, `DamageCalculator`, `AttributeManager`, `HungerManager` |

## 10. Module split and ownership

| owner | owns | does not own |
|---|---|---|
| world/storage | dimensions, chunks, NBT and Anvil persistence | packet field encoding |
| physics | ticks, fluids, redstone, light | command parsing |
| worldgen | density, climate, structures and placement | operational recovery |
| entity/combat | MobStats, AI, effects, damage and drops | registry packet table |
| inventory/data | components, menus, recipes, tags and datapacks | firewall/backup procedure |

`src/generated/` and `assets/` are inputs with their own provenance. Tests are
evidence, not a gameplay module.

<a id="declared-limitations"></a>
## 11. Cautions and declared limitations

- The 90 numbered MISSING rows are base 80 plus extension 10; do not silently shrink
  them to 80 or infer status from an old summary sentence.
- `docs/mob_stats_149.csv` is the stable fixture and must remain at that exact path.
  Speed is a repo-local AI unit; the test comments distinguish it from vanilla exact
  HP/attack/XP fields.
- Recipes report 1,581 source JSON files but 1,578 loaded: three all-blank-pattern
  debug recipes are removed by `Recipe::trimBlankRows`; smithing recipes remain their
  own kind.
- Worldgen MultiNoise/structure placement is deterministic and independently
  cross-checked, but exact vanilla Xoroshiro sequence parity is a
  `DECLARED-LIMITATION` (L3), not a hidden pass.
- `DECLARED-LIMITATION`: arbitrary Fabric Loader/JVM mods and Fabric event-bus
  bytecode are not executable in `cppfm` (E-14).
- Protocol 776's later Bundle item is outside this 1.21.4 contract.
- No current real-client/GUI artifact or accepted 2-hour/24-hour run is available;
  bot and synthetic evidence remain separately labelled.

## 12. Performance

Gameplay owns the cost boundaries, not the measured operational budget:

- 20 TPS tick ordering and per-tick scheduled block/entity/projectile work;
- view-distance sending versus simulation-distance ticking;
- chunk cache/LRU and generation bursts;
- entity-heavy and active-redstone workloads; and
- persistence cadence (short safety save, periodic save, and long-run save).

Measurements, thresholds, and run IDs belong in
[SPEC_OPS.md#performance-and-load](SPEC_OPS.md#performance-and-load). This
documentation-only commit changes no runtime path.

## 13. Thread safety

`World::getBlock`/`withChunk` take shared locks; writes and chunk replacement take a
unique lock. The entity mutex must not be held while calling back into
`World::setBlock`. Session/connection work, the game tick, persistence I/O, and RCON
workers have separate ownership. This document records the existing model; it does
not add a lock or move a callback to another thread.

## 14. Edge cases

- world height bounds, negative coordinates, unloaded chunks, forced/spawn tickets;
- Nether bedrock/terrain, End islands, portal 8× scaling, safe spawn and ability reset;
- waterlogged blocks, piston 12-block limit, observer two-tick pulse, comparator
  signal, fluid solidification and simulation culling;
- entity UUID/duplicate sessions, slime size, villager restock, projectile hit/stuck,
  boss and metadata state;
- menu slot `-1` cursor, drag modes, max stack sizes, empty shaped rows and component
  preservation; and
- intentional E-14, seed RNG L3, simplified vehicle/worldgen pieces, and protocol
  776 features.

## 15. Test method and evidence

Fresh focused results:

| target | result |
|---|---|
| `test_gameplay_full` | `734 PASS / 1 intentional E-14 FAIL / 735`, exit 1 |
| `test_smoke_80` | `212 PASS 0 FAIL` |
| `test_seed_parity` | `201 PASS 0 FAIL` (L1 independent hand-calc plus L2 deterministic 50-chunk comparison) |
| `test_mining_full` | `38/38 passed` |
| `test_block_hardness_full` | `16/16 passed; 1095 mismatch=0` |
| `test_mob_stats_full` | `131 PASS 0 FAIL` |
| `test_redstone_engine_full` | `29 PASS 0 FAIL` |
| `test_recipes_mirror` | `76 PASS 0 FAIL` |
| `test_plan43` | `82 PASS 0 FAIL` in 25.01s |
| `test_native` | `ALL PASS` in 2.33s |
| `test_server_full` | `234 PASS 0 FAIL` in 273.79s |
| `multi_client` | `ALL PASS` in 17.83s |
| `bot_smoke` | `ALL PASS` in 20.65s |
| `tests/soak_test.py --duration 300` | `PASS` in 301.18s; 150 keepalives, 0 disconnects, actions 2930, RSS growth 14.5% |
| `tools/soak_bot.py --duration 300` | `FAIL` in 301.89s; keepAlives 3 (<7), kicks 0, chunks 182, time updates 23 |
| `test_recovery` | registered evidence for the recovery matrix; rerun status is recorded separately in VERIFICATION |

The E-14 assertion is intentionally `CHECK(false, ...)` in
`tests/test_gameplay_full.cpp`; changing it would destroy the honesty gate. Full
commands and the allowed-failure policy are in
[VERIFICATION.md#gameplay-gate](VERIFICATION.md#gameplay-gate).

## 16. Priority and status

**Priority: highest after WIRE.** World/block/entity/inventory/command/combat paths
are current implementation contracts where source and tests say so. `DONE` in the
legacy gap matrix is not expanded into “all vanilla internals are identical.” The
declared boundaries—Fabric JVM mods, vanilla RNG L3, protocol 776, and missing
long-run/real-client evidence—remain visible; the final-gates publication status is
`BLOCKED` by the failing `soak_bot` run and must be addressed by a separately
approved plan if the scope changes.
