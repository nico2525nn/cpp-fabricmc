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

## Bundle & Block Changes (`BundleDelimiter 0x00` + `MultiBlockChange 0x4E`)

Empirical after plan12: vanilla coalesces same chunk-section `BlockUpdate 0x09`s into `MultiBlockChange 0x4E` (section pos `i64`, `varint count` + `varint packed = (blockId<<12)|(y<<8)|(z<<4)|x`, section-local), else wraps mixed packets in `BundleDelimiter 0x00` start/end (empty, id `0x00` both directions, play). Our `PacketBatcher` mirrors: `queueBlockChange` batches until `size>=64` or `50ms` timer, `tryFlushAsMultiBlockChange` dedup last-wins per `(x,y,z)` if all in same section → `MultiBlockChange`, else `BundleDelimiter` start + each `BlockUpdate` + end. Missing the `00` delimiter desyncs clients that wait for `Bundle` close.

- `MultiBlockChange` count is `varint`, each entry is `varint` packed `(state<<12) | ((y&15)<<8) | ((z&15)<<4) | (x&15)` with section origin implicit.
- `BundleDelimiter` is zero-length; both `0x00` wrappers must be sent even for single non-coalescable `BlockUpdate` if inside bundle window.

## Light Update (`UpdateLight 0x2B` / `LightUpdateQueue`)

- `UpdateLight` masks are `BitSet` of `int64` words; block-light for `glowstone` (emit 15) propagates via `LightEngine::drain` BFS 3×3 `expandedDirty` and `pendingSkyRebuild` 3×3 (post-plan12). Opacity for emissive `glowstone 0` after special-case, else `minecraft-data filter`.
- `LightUpdateQueue` (`LightEngine.hpp:20`) batches `addQueue`/`removeQueue` and defers sky rebuild until `drain()`; `serializeUpdateLightBody` builds `skyMask/blockMask/emptyMasks` from `chunk.blockLightNib`.

## Scoreboard (`ScoreboardObjective 0x64` / `ScoreboardScore 0x68` / `ResetScore 0x49` / `DisplayObjective 0x5C`)

- 1.21.4 (protocol 769): `packet_reset_score 0x49` and `packet_scoreboard_score 0x68` are split since 1.20.3 `23w46a` (Prismarine PR #806 `bf05291`). `ResetScore 0x49` = `string holder (entity_name 32767)` + `PrefixedOptional<string> objective_name` (`boolean present + string if present`); `present=false` is wildcard — deletes all objectives for holder (Yarn `ClientboundResetScorePacket` `writeNullable`). Old `packet_scoreboard_score` `action 0/1` was removed; sending `action 1` via `0x68` in 1.21.4 mis-parses as `itemName varint` and kicks.
- `ScoreboardScore 0x68` = `string holder` + `string objective` + `varint value` + `option<anonymousNbt> display_name` + `option<varint> number_format` (`write` via `NumberFormat::write` `boolean has + varint type + switch styling NBT` — `blank/styled/fixed`).
- `ScoreboardObjective 0x64` = `string name` + `i8 action 0 create /1 remove /2 update` + if `0/2`: `anonymousNbt displayName` + `varint type 0 integer 1 hearts` + `option number_format`. `DisplayObjective 0x5C` = `varint position 0 list/1 sidebar/2 below_name` + `string name` (empty clears).
- Server broadcast helpers: `GameServer::sendScoreAll 0x68`, `sendResetScoreAll 0x49` (`sendResetScoreAllWildcard` for `present=false`), `sendObjectiveAll 0x64`, `sendDisplayAll 0x5C`. Wire helpers `Scoreboard::writeResetScorePacket` (`b.string(holder); b.boolean(obj!=nullptr); if(obj) b.string(*obj)`) matches Prismarine `["string","option string"]`.
- Disconnect / `objectives remove` / `players reset` must send `0x49` (wildcard or with objective) to avoid ghost rows on `sidebar`/`below_name`/`list` (vanilla `ServerScoreboard.resetSingleScore` / `resetAllScores` / `removeObjective` → `ClientboundResetScorePacket(holder, objectiveOrNull)`). `BundleDelimiter 0x00` may wrap `0x49` with `0x68/0x64/0x5C` without desync (1-byte varint ids, zlib `dataLength 0 vs >0`, AES-CFB8 `0x80` byte-by-byte).

## Misc wire facts

- `Set Center Chunk`: plain signed varints (NOT ZigZag).
- Movement flags serverbound are a single u8 bitfield (`onGround`,
  `horizontalCollision`) — sending two bytes desyncs and eventually gets kicked.
- Every player action/use sequence must be answered with
  `Ack Block Change (0x05)` or clients stall their prediction queue.
- System chat content is anonymous-NBT text components; `{text:"…"}` suffices.
- `PlayerChat 0x3B` vs `SystemChat 0x73`: when `chatPubKey` valid, server verifies RSA-SHA256 and relays as `PlayerChat`; else `enforcesSecureChat:false` falls back to `SystemChat`. `MessageAck 0x04` is sunk.
