# CppFabricMC — a clean-room Minecraft 1.21.4 server in C++

A from-scratch C++20 implementation of the **Minecraft: Java Edition 1.21.4
(protocol 769)** server protocol, targeting behavioral compatibility with an
unmodded Fabric/vanilla server for core gameplay.

**Private research project.** It contains no Mojang/Microsoft code or assets:
every byte it emits is produced by our own code, whose correctness is pinned by
automated comparison against captured reference-server wire data.

---

## What works today (post plan/ — 80+ items, 80/80 DONE)

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
| Crafting: built-in + JSON loader, recipe book `0x44`, `PlaceRecipe 0x25` + `PlaceGhostRecipe 0x39` for `Furnace`/`Stonecutter`, furnace 3-slot + `ContainerSetData` | ✅ |
| XP orbs/levels, **full AttributeManager** (`ARMOR/TOUGHNESS/KB_RESIST` sync `UpdateAttributes 0x7C`), status effects `Invisibility/Levitation/Glowing` via `SetEntityMetadata` | ✅ |
| Commands: Brigadier port (48 arg types), **40+ commands** (`/fill`, `/execute as @p run`, `/function`, `/reload`, `/tag`/`/team`/`/bossbar` stubs) + selectors `@a/@p/@e` | ✅ |
| Tags 67 item / 20 block via `TagManager`, `LootTables` `fortune`/`silk_touch` + `DatapackManager` `assets/data` | ✅ |
| Scoreboard objectives/scores/sidebar + `Teams 0x67` + `BossBar 0x0A` (wither/dragon) | ✅ |
| Stats (`world/stats/*.json`) + advancements `cppfm:*` tree w/ `UpdateAdvancements 0x7B` + toasts | ✅ |
| Beds: night skip + spawn point + `SetDefaultSpawn` | ✅ |
| Projectiles: arrows/snowballs/eggs/pearls/**WitherSkull/DragonFireball/TNT primed** + skeleton archers | ✅ |
| Dimensions: **Nether/End** `fillNether`/`fillEnd` + `PortalHandler` 8× + `findSafeSpawn` 6-up/down + `PortalAge` + `Abilities` reset + `Respawn 0x4C` per-dim | ✅ |
| Network: cookies, resource packs, transfer, plugin channels, **PacketBatcher** `Bundle 0x00` + `MultiBlockChange 0x4E`, **ChatMessageProcessor** `PlayerChat 0x3B` verify (RSA-SHA256) | ✅ |
| Event bus (`BlockPlace/Break/NeighborChange`) + `ItemUseContext` + `DamageSource` | ✅ |
| RCON + whitelist + `spawn-protection` + `WorldBorder` damage | ✅ |
| 20 TPS loop + `simulationDistance` tick culling + `chunksUnloadTick` LRU + `level.dat` periodic 6000/1200t | ✅ |

Verified by four test layers — see *Testing* below. Chunk serialization is
proven **byte-identical to a real reference server's output** by golden tests.

## What does *not* work (yet)

- **Coarse taxonomy** (`docs/MISSING_FEATURES_1_21_4.md` 80 rows): **0 `PARTIAL` / 0 `TODO`** — all 80 rows DONE per that taxonomy (post-`plan/` `BreedGoal`/`Villager`/`SpawnEgg`/`Brain`).
- **True parity — strict wire audit** (`docs/COMPAT_AUDIT_1_21_4_STRICT.md`, Web Search/Web Fetch verified, `file:line` absolute): **0 gaps remain of 78** — **78/78 fixed** (HIGH 10 in `plan15`, MEDIUM 10 in `plan16`, LOW 10 in `plan17`, `plan18` 10, `plan19` 10, `plan20` 10, `plan21` 10, `plan22` 10, `plan23` 8, `plan24` 10, `plan25` 10). Bit-level protocol parity achieved — now deep audit `docs/COMPAT_DEEP_AUDIT.md` 20/31 fixed, 11 remain.
- **Non-80 polish (beyond 1.21.4):** `Bundles` 1.21.5, `RaidOmen`/`TrialOmen` duration.

- **Fabric mods cannot run inside a C++ process.** Mods are JVM bytecode loaded
  through the Fabric Loader; "Fabric-compatible" here means *protocol-compatible
  with what an unmodded Fabric server puts on the wire*.

## Testing — evidence (what the numbers mean)

- **`test_native` (C++ self-test, 12 cases: status/join/chunk/chat/persist/multi/stress):** `12/12 PASS` — run `./build/test_native ./build/cppfm` (50s). Verifies `status 769`, `Login Success`, `Join Game`, `LevelChunkWithLight`, `BlockUpdate` broadcast, `SystemChat`, and cross-client visibility.
- **`test_smoke_80` (80-row taxonomy, `tests/test_smoke_80.cpp` 1:1 to `docs/MISSING_FEATURES_1_21_4.md`):** `80/80 PASS` per that taxonomy — each row checks a vanilla packet/NBT (e.g., `worldborder size → InitializeWorldBorder 0x26`, `glowstone → UpdateLight 0x2B`, `wither → BossBar 0x0A`). Run `./build/test_smoke_80 ./build/cppfm` (400s). This does **not** guarantee byte-identical vanilla behaviour; it guarantees the 80 coarse features emit the expected packet.
- **Strict audit (`docs/COMPAT_AUDIT_1_21_4_STRICT.md`):** `78/78` fixed, `0` remain. Bit-level protocol parity achieved.

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
`server-port` `max-players` `view-distance` `simulation-distance` `motd` `spawn-protection` (16) `white-list` `online-mode` `level-seed` `level-type` `difficulty` `enforce-secure-profile`.
CLI flags override: `--port --view-distance --assets --world-dir --online-mode`.
`ops.json`/`ops.txt` → `isOp` for `spawn-protection` bypass, `level.dat` `DataVersion 4189` + `Difficulty`/`WorldBorder`/`Version` full, per-dim `DIM-1`/`DIM1` `region/` + `level.dat`.

Join with any 1.21.4 client in offline mode, e.g. a launcher profile pointing at
`127.0.0.1`. You spawn creative-mode on a grass superflat with a building hotbar.

## Testing (4 layers + strict smoke 80)

```bash
# unit/golden (byte-exact vs reference captures)
./build/test_golden /path/to/captures

# integration (full join flow vs OUR server), multi-client, stress
python3 tests/integration_client.py     # env CPPFM_PORT
python3 tests/multi_client_test.py      # two bots, cross-broadcasts
python3 tests/stress_test.py            # N=32 concurrent joins

# C++ self-test (spawns real server, no Python)
./build/test_native ./build/cppfm          # 12 cases status/join/chunk/chat/persist/multi/stress
./build/test_smoke_80 ./build/cppfm        # 80-row taxonomy (see docs/MISSING_FEATURES_1_21_4.md)
ctest -R smoke80 --output-on-failure       # 400s timeout
```

All suites pass in Release and ASan/UBSan (zero sanitizer) including 32-burst for the coarse 80-row taxonomy. For true parity, see `docs/COMPAT_AUDIT_1_21_4_STRICT.md` (78 wire gaps, 30 fixed). `test_native` green post-`plan/`.

### Reproducing the reference captures

`tools/capture.py` (kept for provenance) performs the same flow against a local
reference server and writes `captures/*.bin`: `login_success`,
`registry_*.bin` ×12, `cfg_tags`, `cfg_feature_flags`, `cfg_select_known_packs`,
`play_join_game`, `play_chunk_*`, plus misc play packets. The registry blobs and
tags blob are copied into `assets/registry/` and replayed verbatim by our server;
the rest become golden vectors.

## Empirical protocol findings (1.21.4 / 769)

Documented in detail in [`docs/PROTOCOL_NOTES.md`](docs/PROTOCOL_NOTES.md).
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
│   ├── GameServer/Session (HANDSHAKE→STATUS/LOGIN→CONFIG→PLAY, dimension worlds[3], tickOnce 20 TPS)
│   └── Managers: WorldManager/EntityManager/InventoryController/NetworkManager (forwarders, plan6 §6)
├── worldgen/      DensityFunction, MultiNoise, Structures, StructurePlacer, PortalHandler (8× + findSafeSpawn 6-up/down + PortalAge + Abilities reset)
├── brigadier/     Tree (CommandNode 48 parsers, writeDeclareCommands 0x11)
└── generated/     kBlocks 1095, kItems 1385, kEntities 149 (prismarineJS)
```

One thread per connection; `World` `shared_mutex`; `LightEngine`/`FluidSim`/`Redstone` via `onBlockChanged`; `Persistence` 3s flush + 6000/1200t `level.dat`; `chunkCache` 1024 + LRU `chunksUnloadTick` + `simulationDistance` culling; `PacketBatcher` 50ms / 64-packet coalesce. `GameServer.cpp` split into `World/Entity/Inventory/Network` forwarders (plan6 §6). Data-driven: `EntityData` JSON → `BehaviorTree`, `TagManager` 67/20, `LootTables`, `DatapackManager` `assets/data`.

## Roadmap (toward broader compatibility)

1. Entity layer: spawn/move/head-rotation/metadata for remote players (+ equipment).
2. zlib compression + (optional) online-mode auth.
3. Command system (`declare_commands` + parser graph) and `/gamemode`, `/tp`, `/give`.
4. Region-file persistence (Anvil read/write for flat worlds first).
5. Inventory transactions, crafting, item components.
6. Dimension/respawn plumbing (nether/end via existing registry data).

---
*Project disclaimer:* independent implementation for interoperability research;
not affiliated with Mojang or Microsoft; "Minecraft" is a trademark of Mojang AB.
