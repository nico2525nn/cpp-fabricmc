# Protocol notes: Minecraft Java 1.21.4 (protocol 769)

Empirical findings from black-box observation of a reference server, cross-checked
against PrismarineJS `minecraft-data`. These are the details that cost us time —
recorded here so they never cost that time again.

## Login

`Login Success (0x02)` = UUID(16) ‖ name(string) ‖ properties(array).
**No trailing boolean/flags.** (Older docs mention a strict-error-handling byte;
it is not present in 769.) Captured size for name "RefBot": 24 bytes.

The server **blocks during configuration until the client answers**
`Select Known Packs`; an empty reply (`varint 0`) makes it send the full
registry dump. Ignoring the request stalls the connection.

## Configuration

Observed order from the reference server:
`custom_payload(minecraft:brand)` → `feature_flags["minecraft:vanilla"]` →
`select_known_packs{minecraft, core, 1.21.4}` → *wait for client answer* →
12 × `registry_data` → `tags` → `finish_configuration`.

Registry set synced in 769 (exactly these twelve; nothing else):
`worldgen/biome`(65), `chat_type`(7), `trim_pattern`(18), `trim_material`(11),
`wolf_variant`(9), `painting_variant`(50), `dimension_type`(4),
`damage_type`(49), `banner_pattern`(43), `enchantment`(42), `jukebox_song`(19),
`instrument`(8).

Note: `attribute`, `mob_effect`, `cat_variant`, `frog_variant`, `game_event`,
cow/pig/chicken variants are **not** synced in 1.21.4 (several arrive only in
1.21.5+). Guessing this set wrong breaks the join.

## Join Game / SpawnInfo

Layout (fully consumed = verified): entityId i32 ‖ hardcore ‖ worldNames[] ‖
maxPlayers ‖ viewDistance ‖ simulationDistance ‖ reducedDebug ‖ respawnScreen ‖
doLimitedCrafting ‖ then SpawnInfo: **dimensionType varint** (= index into the
synced dimension_type registry order; overworld = 0) ‖ dimensionName string ‖
hashedSeed ‖ gamemode i8 ‖ prevGamemode u8 (255 = none) ‖ isDebug ‖ isFlat ‖
hasDeathLocation ‖ [death…] ‖ portalCooldown ‖ seaLevel varint ‖ then
enforcesSecureChat.

Flat-world reference values: `seaLevel = -63`, spawn point long
`00 00 00 00 00 00 0f c4` = (0, -60, 0).

## Chunks (`LevelChunkWithLight`)

Per section: blockCount i16, blocks paletted container, biomes paletted container.
24 sections (min_y −64, height 384). Container encoding as observed:

```
single-valued : 00 <value varint> <longCount varint = 0>     ← count ALWAYS written
indirect      : <bits> <paletteLen> <ids…> <longCount> <packed longs>
direct        : <bits(>8)> <longCount> <packed global ids>
```

Missing the trailing `00` for single-valued containers desynchronizes every
non-uniform section parse. Confirmed against two independent flat worlds:
uniform-plains chunk tail = `00 28 00 | 00 00 | 00 | 00 | 00`-style 8-byte
sections; desert chunk = indirect biome container
(`01 02 28 0e 01 ff…ff`: palette {plains=40, desert=14}, all cells desert).

- Blocks: min indirect bits 4, ≤8 else direct/global.
- Biomes: min indirect bits 1 (no global fallback needed here).
- Packing: little-endian within each big-endian long, entries never straddle.
- Biome ids are indices into the synced registry order (plains 40, desert 14).
- Heightmaps NBT: anonymous compound, `MOTION_BLOCKING` then `WORLD_SURFACE`,
  37 longs each at 9 bits/entry.
- Light: masks are arrays of u64 words (bit N = section N); vanilla sends sky
  arrays only for transition sections, marks fully-solid sections in
  `emptySkyLightMask`, and leaves fully-open sections implicit (no mask bit).
  Block light was sent empty in all captures.

## Misc wire facts

- `Set Center Chunk`: plain signed varints (NOT ZigZag).
- Movement flags serverbound are a single u8 bitfield (`onGround`,
  `horizontalCollision`) — sending two bytes desyncs and eventually gets kicked.
- Every player action/use sequence must be answered with
  `Ack Block Change (0x05)` or clients stall their prediction queue.
- System chat content is anonymous-NBT text components; `{text:"…"}` suffices.
