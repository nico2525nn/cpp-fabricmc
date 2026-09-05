# CppFabricMC — a clean-room Minecraft 1.21.4 server in C++

A from-scratch C++20 implementation of the **Minecraft: Java Edition 1.21.4
(protocol 769)** server protocol, targeting behavioral compatibility with an
unmodded Fabric/vanilla server for core gameplay.

**Private research project.** It contains no Mojang/Microsoft code or assets:
every byte it emits is produced by our own code, whose correctness is pinned by
automated comparison against captured reference-server wire data.

---

## Documentation

Start with the [canonical documentation index](docs/README.md). The current source of truth is split by responsibility:

- [Wire specification](docs/SPEC_WIRE.md)
- [Gameplay specification](docs/SPEC_GAMEPLAY.md)
- [Operations specification](docs/SPEC_OPS.md)
- [Development guide](docs/DEVELOPMENT.md)
- [Verification and evidence](docs/VERIFICATION.md)
- [Current state](docs/CURRENT_STATE.md)
- [Feature/status matrix](docs/MISSING_FEATURES_1_21_4.md)
- [Audit and history index](docs/audit/README.md)

Historical assessment text is indexed from the audit page and is not a current specification.

## What works today (implementation baseline `17ab09f` — 2026-09-05 runtime follow-up)

| Area | Status |
|---|---|
| Server-list ping (status JSON, favicon, player sample, ping/pong) | ✅ |
| Login → Login Success → Login Ack; **online-mode auth + AES encryption** | ✅ |
| Configuration phase (brand, known-packs, 12 registries, tags, FeatureFlags) | ✅ |
| Join Game exact 1.21.4 layout, chunk streaming w/ batching, center-chunk, per-dimension worlds | ✅ |
| Anvil persistence (`region/*.mca`) — blocks, per-cell biomes, block entities; vanilla interop both directions; `level.dat` full (DataVersion, Difficulty, WorldBorder, Version) | ✅ |
| Worldgen v3+: MultiNoise biomes (30 pts) + Density pipeline + **Nether/End biomes** (basalt deltas, warped, outer islands, chorus), **ConfiguredFeature/PlacedFeature + Jigsaw** (`StructurePlacer`) for village/stronghold/mineshaft, triangle ores | ✅ |
| Light engine: **cross-chunk** block-light BFS + sky-light BFS with `LightUpdateQueue`, `UpdateLight` broadcast | ✅ |
| Fluids (scheduled ticks, solidify `cobble/obsidian/stone`), **BlockTickScheduler** (`Crop/Sapling/Stem/Farmland/Fire/PortalAge`) with `randomTickSpeed` & simulation-distance culling | ✅ |
| Redstone: wires/levers/buttons/torches/lamps **+ comparator/observer/rails/pistons** (`MovingPiston` 2-tick), `analogOutput` from containers | ✅ |
| Weather cycle + `/weather`; creeper explosions (charged 6.0) w/ terrain & knockback; buckets `water/lava` + `flint_and_steel` fire | ✅ |
| Mobs: **46 kinds** (wither/dragon/warden/shulker…), **BehaviorTree** (`Selector/Sequence/Condition/Action`) data-driven via `assets/entities/*.json`, `Enderman` teleport, `WitherSkull`, `Dragon` phases, `Slime` split, breeding/aging | ✅ |
| Survival: hunger (`exhaustion` sprint/jump/attack/bow, `saturation` cake/stew), regen/starve/void, **air 300 drown, freeze powder-snow, fire lava**, `water/slime` fall mitigation, `EntityAction 0x28` sneak pose, `EntityVelocity 0x5F` knockback | ✅ |
| Inventory: ItemStack data-components, **Barrel/ShulkerBox/Enchanting/Anvil/Brewing/Stonecutter/Grindstone/Smithing/Beacon/Loom** menus, drag `mode5` + creative `mode3`, `ContainerSetContent`/`SetSlot` authoritative | ✅ |
| Crafting: **1578 recipes JSON-driven** (`assets/data/recipes/*.json` 1581, 1578 loaded), recipe book `0x44`, `PlaceRecipe 0x25` + `PlaceGhostRecipe 0x39` for `Furnace`/`Stonecutter`, furnace 3-slot + `ContainerSetData` | ✅ |
| XP orbs/levels, **full AttributeManager** (`ARMOR/TOUGHNESS/KB_RESIST` sync `UpdateAttributes 0x7C`), status effects `Invisibility/Levitation/Glowing` via `SetEntityMetadata` | ✅ |
| Commands: Brigadier port (48 arg types), **30+ vanilla commands** (`/execute` modifiers `as/at/positioned/anchored/in/dimension/rotated/facing/if/unless` + `run`, `/data get/merge/remove`, `/clone`, `/loot give/fish`, `/place feature/structure/jigsaw`, `/locate structure/biome/poi`, `/spreadplayers`, `/enchant`, `/attribute`, `/trigger`, `/advancement`, `/recipe`, `/item`, `/me`, `/msg`, `/ban`, `/ban-ip`, `/pardon`, `/op`, `/deop`, `/whitelist`, `/kick` admin + `/fill`, `/function`, `/reload`, `/tag`/`/team`/`/bossbar`) + selectors `@a/@p/@e` | ✅ |
| Tags 67 item / 20 block via `TagManager`, `LootTables` `fortune`/`silk_touch` + `DatapackManager` `assets/data` | ✅ |
| Scoreboard objectives/scores/sidebar + `Teams 0x67` + `BossBar 0x0A` (wither/dragon) | ✅ |
| Stats (`world/stats/*.json`) + advancements `cppfm:*` story 20 + `UpdateAdvancements 0x7B` + toasts + `PredicateEvaluator` 8種 (location/bow/distance…) + loot functions `set_count/looting_enchant` | ✅ |
| Beds: night skip + spawn point + `SetDefaultSpawn` | ✅ |
| Projectiles: arrows/snowballs/eggs/pearls/**WitherSkull/DragonFireball/TNT primed** + skeleton archers | ✅ |
| Dimensions: **Nether/End** `fillNether`/`fillEnd` + `PortalHandler` 8× + `findSafeSpawn` 6-up/down + `PortalAge` + `Abilities` reset + `Respawn 0x4C` per-dim | ✅ |
| Network: cookies, resource packs, transfer, plugin channels, **PacketBatcher** `Bundle 0x00` + `MultiBlockChange 0x4E`, **ChatMessageProcessor** `PlayerChat 0x3B` verify (RSA-SHA256) | ✅ |
| Event bus (`BlockPlace/Break/NeighborChange`) + `ItemUseContext` + `DamageSource` | ✅ |
| RCON + whitelist + `spawn-protection` + `WorldBorder` damage | ✅ |
| 20 TPS loop + `simulationDistance` tick culling + configured-radius `chunksUnloadTick` LRU + `level.dat` periodic 6000/1200t | ✅ |
| Admin: `/ban`/`ban-ip`/`pardon`/`op`/`deop`/`whitelist`/`kick` + `Rcon` + `whitelist.json`/`ops.json` enforcement | ✅ |

The focused wire, gameplay, integration, and operations evidence below is tied to the
named baseline and its final-gates record. Chunk serialization is proven **byte-identical
to a real reference server's output** by golden tests; this does not make every gameplay
or long-run behavior a universal vanilla-parity claim.

## Boundaries and declared limitations

- **Feature/status matrix:** [`docs/MISSING_FEATURES_1_21_4.md`](docs/MISSING_FEATURES_1_21_4.md) retains the 80-row base taxonomy plus 10 numbered extension rows; the historical taxonomy classification is 90/90 `DONE`, not a universal parity claim.
- **Audit history:** the audit index records the completed assessment series, including **assessment-6 44/44 FIXED (HIGH 25/25)**. Historical audits are evidence, not the current specification.
- **Version and parity boundaries:** the project targets Minecraft Java 1.21.4 / protocol 769; broader vanilla RNG/structure-NBT parity and other unverified behavior remain separately documented in the canonical specifications.
- **Gameplay boundary:** `test_gameplay_full` has **803 PASS / 1 intentional E-14 FAIL / 804** and exits 1; the Fabric JVM-mod gap is intentional and by design.
- **Fabric mods cannot run inside a C++ process.** Mods are JVM bytecode loaded through the Fabric Loader; "Fabric-compatible" here means *protocol-compatible with what an unmodded Fabric server puts on the wire*.
- **Publication remains `BLOCKED`:** short bot/soak gates pass, but the attempted 2-hour run was interrupted above its RSS gate; no accepted 2-hour/24-hour artifact and no current real-client/GUI artifact exist.

## Testing — evidence (what the numbers mean)

- **`test_native` (C++ self-test, status/join + plan38 QC/macro/predicate16 cases: status/join/chunk/chat/persist/multi/stress + plan34 fuzz/soak):** `ALL PASS`, 2.33s — run `./build/test_native ./build/cppfm`. Verifies `status 769`, `Login Success`, `Join Game`, `LevelChunkWithLight`, `BlockUpdate` broadcast, `SystemChat`, and cross-client visibility.
- **`test_server_full` (full live-server protocol/gameplay/admin suite):** `234 PASS 0 FAIL` — run the current server-full harness as described in [Verification](docs/VERIFICATION.md).
- **`test_smoke_80` (80-row taxonomy + plan32-41 拡張, `tests/test_smoke_80.cpp`):** `212 PASS 0 FAIL` (80-row taxonomy + 82 拡張チェック plan41 horse/vehicle/bench/recipes) — each check verifies a vanilla packet/NBT (e.g., `worldborder size → InitializeWorldBorder 0x26`, `glowstone → UpdateLight 0x2B`, `wither → BossBar 0x0A`, `ActionBar 0x51`, `OpenHorseWindow 0x24`, `VehicleMove 0x33`, predicate 22). Run `./build/test_smoke_80 ./build/cppfm` (450s, 600s under load; `=== SMOKE 80: 212 PASS 0 FAIL ===`, exit 0).
- **`test_scoreboard_reset` (ResetScore `0x49` round-trip, `tests/test_scoreboard_reset.cpp`):** `22/22 PASS` — holder + optional objectiveName round-trip / wildcard null broadcast / copy-before-erase. Run `./build/test_scoreboard_reset` (ctest `scoreboard_reset`, TIMEOUT 30).
- **`test_spec_wire` (wire byte-identical, `tests/test_spec_wire.cpp` plan30-43):** `392 PASS 0 FAIL 0 SKIP` — 25+ wire cases covering all play `toClient` families (chunk/light/bundle, `UpdateAttributes 0x7C` `H1` varint mapper + horse `0x24` vehicle `0x33` + recipes tag, predicate 22) vs Prismarine `protocol.json` spec bytes (`EXPECT_EQ` `WriteBuffer` vs spec) + plan43 P43-1..7 shape locks. Run `./build/test_spec_wire` (ctest `spec_wire`, TIMEOUT 30).
- **`test_wire_full` (full play matrix, `tests/test_wire_full.cpp`):** `405 PASS 0 FAIL` — toClientplay 全型の byte lock。Run `./build/test_wire_full` (ctest `wire_full`).
- **`test_gameplay_full` (gameplay asserts, `tests/test_gameplay_full.cpp` plan42-46):** `803 PASS / 1 intentional E-14 FAIL / 804`, exit 1 — redstone engine・combat sweep/crit/shield・mob behavior・density・food/potion・crops/villager 込。Run `./build/test_gameplay_full` (ctest `gameplay_full`).
- **`test_wire_b6` (plan45 B6 wire, `tests/test_wire_b6.cpp` assessment-6 W-05/08-11/13 + G-13):** `133 PASS` — settings `0x0C`・NameItem/Beacon/PickItem/RecipeBook・OP gate + spectate・steer_boat/resource_pack/pong・login 5 種・名前検証 + 重複 kick・crafting live。Run `./build/test_wire_b6` (ctest `wire_b6`).
- **`test_seed_parity` (plan45 G-11, `tests/test_seed_parity.cpp`):** `201 PASS` — バイオーム 65・構造物 42+ (jar 検証)・同一 seed 同一ワールドの 3 層 parity。Run `./build/test_seed_parity` (ctest `seed_parity`).
- **`test_flood_net` / `test_recovery` / `test_rcon_multi`:** registered focused targets with prior evidence; no new result for these targets is asserted in the supplied final-gates record. Run them separately before treating their status as current.
- **`test_mining_full` / `test_block_hardness_full` / `test_mob_stats_full` / `test_redstone_engine_full` (plan44 G-02/03/05/12):** mining `38 PASS` / hardness 1095 mismatch 0 / mob_stats `131 PASS` (149 種 + followRange) / redstone `29 PASS` (7 カテゴリ engine 経由)。Run `ctest -R "mining_full|block_hardness_full|mob_stats_full|redstone_engine_full"`.
- **`test_fuzz` (fuzz 23 cases, `tests/test_fuzz.cpp` plan34):** `23 PASS 0 FAIL` — malformed varint/NBT/packet fuzz, no crash/UBSan. Run `./build/test_fuzz` (ctest `fuzz`, TIMEOUT 30).
- **`multi_client` / `bot_smoke` / `soak_bot`:** `multi_client` ALL PASS in 17.83s / `bot_smoke` ALL PASS in 20.65s / `soak_bot` 3/3 PASS (each KeepAlive 30, chunks 182, time updates 300, error counters 0). The short bot gate is closed.
- **`tests/soak_test.py --duration 300`:** `PASS` (150 keepalives, `0 disconnects`, actions 2932, post-fill RSS growth `7.6%`). This is not a 2-hour or 24-hour result.
- **`tests/soak_test.py --duration 600 --movement-range 3000`:** `PASS` (300 keepalives, `0 disconnects`, actions 5707, post-fill RSS growth `6.6%`; wide traversal).
- **`tests/soak_test.py --duration 1800 --movement-range 3000`:** `PASS` on the allocation-reuse baseline (900-second post-fill baseline `114504kB`, max `128868kB`, growth `12.5%`; 900 keepalives, 0 disconnects, 17493 actions). This is a 30-minute diagnostic, not a 2-hour/24-hour result.
- **`tests/soak_test.py --duration 7200 --movement-range 3000`:** **not accepted** — interrupted at the recorded `t=3361s` before completion; post-fill baseline `160388kB`, maximum `191612kB` (`+19.5%`), above the `15%` gate. The cache stayed bounded while the five-client spatial working set continued to grow.
- **view-distance-32 dry benchmark:** `PASS` in 1.74s for 4,225 chunks; p50 `0.108ms`, p95 `2.333ms`, peak RSS ~`95MB`, hit rate `84.6%`.
- **Audit/evidence baseline:** assessment-6 is **44/44 FIXED (HIGH 25/25)**. The earlier strict/deep and assessment-series evidence is indexed in [Audit and history](docs/audit/README.md); current test meanings and release gates live in [Verification](docs/VERIFICATION.md).

## Clean-room methodology (important)

1. Wire formats were taken from community-maintained protocol documentation
   (PrismarineJS `minecraft-data`) — not from decompiled code.
2. A real reference server was run locally purely as a **black box**, and its
   observable outputs were captured with our own Python client
   (`tools/`-era scripts, now `tests/`): registry blobs, tags, login success,
   join game, chunks, keepalives.
3. Those captures serve as (a) verbatim-replayable configuration payloads and
   (b) **golden test vectors** that our C++ serializer must reproduce exactly.
4. No decompilation, no Mojang source/assets, no obfuscation maps were used.

## Building

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/cppfm --port 25565 --view-distance 6
```

Optional sanitizer build:

```bash
cmake -B build-asan -G Ninja -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan
```

Assets: `assets/registry/*.bin` (12 registries + tags, captured) + `assets/data/{tags,loot_tables,structures,recipes,entities}` (JSON, data-driven) must exist next to `worldDir`. `StructurePlacer` loads `assets/data/structures/*.json` fallback to `Stronghold`/`Mineshaft`/`Village` defaults; `TagManager` 67/20, `LootTables` `fortune`/`silk_touch`, `EntityDataLoader` 10 `assets/entities/*.json` → `BehaviorTree`.

Configuration: vanilla `server.properties` subset
`server-port` `max-players` `view-distance` `simulation-distance` `motd` `spawn-protection` (16) `white-list` `online-mode` `level-seed` `level-type` `difficulty` `enforce-secure-profile` `pvp` `allow-flight` `hardcore` `max-players` enforced. `maxLoadedChunks` LRU is Chebyshev-distance sorted with burst 16/tick and forced/spawn ticket protection (not simple `clear()`). Signal handling (`SIGINT`/`SIGTERM` in `main.cpp`) is POSIX-dependent; Windows requires `SetConsoleCtrlHandler` for full support.
CLI flags override: `--port --view-distance --assets --world-dir --online-mode`.
`ops.json`/`ops.txt` → `isOp` for `spawn-protection` bypass, `level.dat` `DataVersion 4189` + `Difficulty`/`WorldBorder`/`Version` full, per-dim `DIM-1`/`DIM1` `region/` + `level.dat`.

Join with any 1.21.4 client in offline mode, e.g. a launcher profile pointing at
`127.0.0.1`. You spawn creative-mode on a grass superflat with a building hotbar.

## Testing (wire, gameplay, integration, and operations evidence)

```bash
# unit/golden (byte-exact vs reference captures)
./build/test_golden /path/to/captures

# integration (full join flow vs OUR server), multi-client, stress
python3 tests/integration_client.py     # env CPPFM_PORT
python3 tests/multi_client_test.py      # two bots, cross-broadcasts
python3 tests/stress_test.py            # N=32 concurrent joins (attach CPPFM_HOST/PORT)
# Load (final-gates run — spawns server, 120 clients wave ramp 10/2s)
timeout --foreground --kill-after=5 600 python3 tests/stress_test.py --clients 120 --binary ./build/cppfm
pgrep -a -f 'cppfm --por[t]' || true
# Soak 300s dry
timeout --foreground --kill-after=5 400 python3 tests/soak_test.py --duration 300 --binary ./build/cppfm
# Bench view32 scenario (65x65=4225 chunks p50/p95 + peak RSS)
python3 tools/bench_chunk_gen.py --view-distance 32 --chunks 4225 --dry --strict

# C++ self-test (spawns real server, no Python)
./build/test_native ./build/cppfm          # ALL PASS status/join/chunk/chat/persist/multi/stress + plan38 QC3/macro4/predicate16
# Results from the supplied final-gates run:
./build/test_smoke_80 ./build/cppfm        # 212 PASS 0 FAIL
./build/test_scoreboard_reset              # 22 PASS 0 FAIL
./build/test_spec_wire                     # 392 PASS 0 FAIL 0 SKIP
./build/test_fuzz                          # 23 PASS 0 FAIL
./build/test_wire_full                     # 405 PASS 0 FAIL 0 SKIP
./build/test_gameplay_full                 # 803 PASS / 1 intentional E-14 FAIL / 804 (exit 1)
./build/test_wire_b6                       # 133 PASS 0 FAIL
./build/test_seed_parity                   # 201 PASS 0 FAIL
./build/test_recipes_mirror                # 76 PASS 0 FAIL
python3 tools/bench_chunk_gen.py --view-distance 32 --chunks 4225 --dry --strict  # PASS, p50 0.108 p95 2.333 hit 84.6%
python3 tests/multi_client_test.py --binary ./build/cppfm   # ALL PASS in 17.83s
python3 tests/bot_smoke.py --binary ./build/cppfm --duration 30  # ALL PASS in 20.65s
python3 tests/soak_test.py --duration 300 --binary ./build/cppfm   # PASS: 150 keepalives, 0 disconnects, 2932 actions, RSS +7.6%
python3 tools/soak_bot.py --duration 300 --binary ./build/cppfm  # 3/3 PASS: KeepAlive 30, chunks 182, time updates 300 each
# Focused CTest: run targets individually; gameplay_full retains its intentional E-14 failure.
ctest -R soak2h --output-on-failure --timeout 8000                  # nightly 2h (LABELS nightly)
ctest -R soak_bot --output-on-failure --timeout 400                 # soak_bot 300s (LABELS soak)
# Nightly 24h soak (plan45 O-06 — ctest外、専用実行。gate: RSS <5%/24h warmup除外 + TPS 20 + NBT整合)
# nohup python3 tests/soak_test.py --duration 86400 --binary ./build/cppfm > /tmp/soak24h.log 2>&1 &
# 24hフルはnightlyのみ。現時点で受理済みの2h/24h artifactはない。後始末は `cppfm --por[t]` を確認して所有PIDだけを終了する。
```

The current runtime record at `17ab09f5220bf99203d2aea2b2c9d65f763f433b` keeps the only
allowed gameplay failure visible: `test_spec_wire` `392 PASS 0 FAIL 0 SKIP`,
`test_wire_full` `405 PASS 0 FAIL 0 SKIP`, `test_wire_b6` `133 PASS 0 FAIL`,
`test_gameplay_full` `803 PASS / 1 intentional E-14 FAIL / 804` (exit 1),
`test_server_full` `234 PASS 0 FAIL`, `test_seed_parity` `201 PASS 0 FAIL`,
`test_mining_full` `38/38`, `test_block_hardness_full` `16/16` with `1095 mismatch=0`,
`test_mob_stats_full` `131 PASS 0 FAIL`, `test_redstone_engine_full` `29 PASS 0 FAIL`,
and `test_recipes_mirror` `76 PASS 0 FAIL`. The 300-second bot gate and 10-minute
wide synthetic soak pass; the attempted 2-hour run was interrupted before completion
and did not pass its RSS gate. Publication remains `BLOCKED`; no accepted 2-hour/24-hour
run or current real-client/GUI artifact is claimed. For current interpretation, see [Verification](docs/VERIFICATION.md),
[Current state](docs/CURRENT_STATE.md), and the [audit history index](docs/audit/README.md).

### Reproducing the reference captures

`tools/capture.py` (kept for provenance) performs the same flow against a local
reference server and writes `captures/*.bin`: `login_success`,
`registry_*.bin` ×12, `cfg_tags`, `cfg_feature_flags`, `cfg_select_known_packs`,
`play_join_game`, `play_chunk_*`, plus misc play packets. The registry blobs and
tags blob are copied into `assets/registry/` and replayed verbatim by our server;
the rest become golden vectors.

## Empirical protocol findings (1.21.4 / 769)

Documented in detail in the [wire specification](docs/SPEC_WIRE.md).
Highlights:

- **Login Success has no trailing flags**: UUID ‖ name ‖ property array. Done.
- Join Game's `SpawnInfo.dimension type` is a **varint index into the synced
  `dimension_type` registry order** (overworld = 0), not a string.
- Paletted containers write a **long-count even for single-valued palettes**
  (`00 <value> 00`). Missing this desyncs every non-uniform section parse —
  discovered by byte-level diffing of flat-world captures.
- Biome ids in chunk sections index the **synced biome registry order**
  (plains = 40, desert = 14 in 1.21.4); block states use the global palette
  (air 0, grass_block 9, dirt 10, bedrock 85, sand 118 …).
- The server blocks in configuration until the client answers
  `select_known_packs`; replying an empty list yields the full registry dump.
- `Set Center Chunk` uses plain signed varints (not ZigZag).

## Architecture (post plan/ — data-driven, event-bus, modular)

```
src/
├── core/          ByteBuffer (BE varint), NBT, Json
├── proto/         Ids 769 (Login/Config/Play 0x00-0x7F)
├── net/           Connection (zlib + AES-CFB8), PacketBatcher (Bundle 0x00 / MultiBlockChange 0x4E), Crypto (RSA-SHA256 chat)
├── game/
│   ├── World (24×16³ Chunk + Light Nibble + ForcedChunks, shared_mutex, allChunkKeys/eraseChunk)
│   ├── WorldGen (MultiNoise 30 pts + DensityPipeline + StructurePlacer: Configured/PlacedFeature + Jigsaw)
│   ├── ChunkCodec (paletted + UpdateLight), Anvil (region *.mca), Persistence (level.dat DataVersion/Difficulty/WorldBorder)
│   ├── BlockTickScheduler (IBlockBehavior: Crop/Sapling/Stem/Farmland/Fire/SoulFire/Campfire/PortalAge) + ItemUseContext + Block Event Bus
│   ├── Fluids/Redstone/LightEngine (global BFS, LightUpdateQueue, comparator/observer/rails/piston MovingPiston)
│   ├── Entities (46 MobKind + MobStats), BehaviorTree (Selector/Sequence/Condition/Action), AiBrain (Brain+Goal+Sensor), Attributes (9 attrs, ARMOR sync)
│   ├── Items (data-components 6/10/21, durability, enchant), Containers (15 MenuType, CostCalculator), Recipes (Shaped/Shapeless/Smelting/Stonecutting)
│   ├── GameServer (split plan31: GameServer.cpp 35 + _tick 980 + _combat 455 + _items 1831 + _world 216 + _session 3805 + _core 492 = 7714 + Helpers 61 + StairsHelper 192 + Constants 41) / Session (HANDSHAKE→STATUS/LOGIN→CONFIG→PLAY, dimension worlds[3], tickOnce 20 TPS)
│   └── Helpers: StairsHelper (isStairsBlock/getPropStr/computeStairsShape), GameServerHelpers (nowMs/packUuidFromUrl/kKit/blockNameByState/savePlayerNBT...), Constants (kWorldBorderDiameter 59999968 / kTicketLevelSpawn 31 / kBlockBatchFlushMs 50)
│   └── Managers: WorldManager/EntityManager/InventoryController/NetworkManager (forwarders, plan6 §6)
├── worldgen/      DensityFunction, MultiNoise, StructureManager, StructurePlacer, PortalHandler (8× + findSafeSpawn 6-up/down + PortalAge + Abilities reset)
├── brigadier/     Tree (CommandNode 48 parsers, writeDeclareCommands 0x11)
└── generated/     kBlocks 1095, kItems 1385, kEntities 149 (prismarineJS)
```

One thread per connection; `World` `shared_mutex`; `LightEngine`/`FluidSim`/`Redstone` via `onBlockChanged`; `Persistence` 3s flush + 6000/1200t `level.dat`; `chunkCache` 1024 + LRU `chunksUnloadTick` + `simulationDistance` culling; `PacketBatcher` 50ms / 64-packet coalesce. `GameServer.cpp` split plan31 into 6 topic files (tick/combat/items/world/session/core) + 3 helper headers (Helpers/Stairs/Constants) — header `GameServer.hpp` single (1114) kept, cpp 7843→35 (99.5% dispersed). Data-driven: `EntityData` JSON → `BehaviorTree`, `TagManager` 67/20, `LootTables`, `DatapackManager` `assets/data`.

## Next steps

See [Current state](docs/CURRENT_STATE.md) for the measured backlog and [Verification](docs/VERIFICATION.md) for the release gates. The intentional E-14 boundary and future protocol/version work are tracked in the canonical specifications rather than duplicated here.

---
*Project disclaimer:* independent implementation for interoperability research;
not affiliated with Mojang or Microsoft; "Minecraft" is a trademark of Mojang AB.
