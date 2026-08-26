# CppFabricMC — a clean-room Minecraft 1.21.4 server in C++

A from-scratch C++20 implementation of the **Minecraft: Java Edition 1.21.4
(protocol 769)** server protocol, targeting behavioral compatibility with an
unmodded Fabric/vanilla server for core gameplay.

**Private research project.** It contains no Mojang/Microsoft code or assets:
every byte it emits is produced by our own code, whose correctness is pinned by
automated comparison against captured reference-server wire data.

---

## What works today

| Area | Status |
|---|---|
| Server-list ping (status JSON, favicon, player sample, ping/pong) | ✅ |
| Login → Login Success → Login Ack; **online-mode auth + AES encryption** | ✅ |
| Configuration phase (brand, known-packs, 12 registries, tags) | ✅ |
| Join Game exact 1.21.4 layout, chunk streaming w/ batching, center-chunk | ✅ |
| Anvil persistence (`region/*.mca`) — blocks, per-cell biomes, block entities; vanilla interop both directions | ✅ |
| Worldgen v3: MultiNoise biomes (30 points), density-function AST, structures (villages/pyramids/outposts), triangle-distribution ores | ✅ |
| Light engine: block-light BFS add/remove + cached sky light + UpdateLight broadcast | ✅ |
| Fluid simulation (scheduled ticks), redstone (wires/levers/buttons/torches/lamps) | ✅ |
| Weather cycle + /weather; creeper explosions w/ terrain damage & knockback | ✅ |
| Mobs: 12 kinds, Brain-Goal-Sensor AI, A* pathfinding, breeding/aging, ranged skeletons, light-aware spawning & daylight burn | ✅ |
| Survival: hunger/regen/fall damage, mining timing w/ crack animation, item drops & pickup | ✅ |
| Inventory: ItemStack with data-component passthrough, chest/furnace/crafting menus, authoritative clicks, PlaceRecipe | ✅ |
| Crafting: built-in table + JSON loader, recipe book sync, furnace smelting | ✅ |
| XP orbs/levels, status effects (/effect), enchantment hooks | ✅ |
| Commands: Brigadier port (argument types, selectors, tab-completion), 25 commands | ✅ |
| Scoreboard objectives/scores/sidebar + kill/death counters | ✅ |
| Stats (world/stats/*.json) + advancements tree w/ toasts | ✅ |
| Beds: night skip + spawn point | ✅ |
| Projectiles: arrows/snowballs/eggs/pearls, skeleton archers | ✅ |
| Network extensions: cookies, resource packs, transfer, plugin channels, chat-session acceptance | ✅ |
| Event bus for internal hooks | ✅ |
| RCON + whitelist | ✅ |
| 20 TPS fixed game loop driving all systems | ✅ |

Verified by four test layers — see *Testing* below. Chunk serialization is
proven **byte-identical to a real reference server's output** by golden tests.

## What does *not* work (yet)

See [`plan4.md`](plan4.md) for the researched gap list and queue:
villager trading, hoppers/dispensers, nether/end dimensions, enchanting-table
logic, vehicles, maps/frames, fishing, spectator teleport, advanced redstone.

- **Fabric mods cannot run inside a C++ process.** Mods are JVM bytecode loaded
  through the Fabric Loader; "Fabric-compatible" here means *protocol-compatible
  with what an unmodded Fabric server puts on the wire*.

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

Assets: `assets/registry/*.bin` must exist next to the working directory
(loaded at startup). They are captured configuration payloads, not code.

Configuration accepts a vanilla-style `server.properties` subset:
`server-port`, `max-players`, `view-distance`, `simulation-distance`, `motd`.
CLI flags override: `--port --view-distance --assets --motd`.

Join with any 1.21.4 client in offline mode, e.g. a launcher profile pointing at
`127.0.0.1`. You spawn creative-mode on a grass superflat with a building hotbar.

## Testing

```bash
# unit/golden (byte-exact vs reference captures)
./build/test_golden /path/to/captures

# integration (full join flow vs OUR server), multi-client, stress
python3 tests/integration_client.py     # env CPPFM_PORT
python3 tests/multi_client_test.py      # two bots, cross-broadcasts
python3 tests/stress_test.py            # N=32 concurrent joins
```

All suites pass in both Release and ASan/UBSan builds (zero sanitizer reports),
including a 32-concurrent-client burst.

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

## Architecture

```
src/
├── core/       ByteBuffer (explicit big-endian, varint), NBT writer/reader
├── proto/      packet id tables for 769
├── net/        Connection: framing + serialized writes, RAII socket
├── game/       World (flat gen, chunk store), ChunkCodec (wire serialization),
│               EmbeddedData (registry replay + id derivation),
│               GameServer/Session (state machine, players, broadcasting)
└── generated/  block-state & item-id tables (generated from public datasets)
```

One thread per connection; world guarded by a shared mutex; chunk
serialization results cached per chunk and shared across players (invalidated on
edit). Designed so missing features slot in without restructuring: entities =
EntityManager + metadata writer; compression = one frame codec change;
persistence = swap World storage for region files.

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
