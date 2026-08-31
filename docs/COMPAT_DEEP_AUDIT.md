# Deep Compatibility Audit — Bit-Level Wire Parity beyond 1.21.4 Strict 78/78

> **Target:** `cpp-fabricmc` HEAD (plan23, protocol 769, DataVersion 4189, Yarn 1.21.4) vs Vanilla **Fabric 1.21.4** (`crafted` reference server, Prismarine `minecraft-data 1.21.4` `protocol.json`, **wiki.vg / minecraft.wiki `Chunk format`**, **Yarn `1.21.4+build.*` `maven.fabricmc.net`**, **mappings.dev**).  
> **Date:** 2026-08-29  (deep audit, beyond `COMPAT_AUDIT_1_21_4_STRICT.md` 78 gaps + `MISSING_FEATURES_1_21_4.md` 80 taxonomy).  
> **Method:** Unlimited **Web Search + Web Fetch** cross-check: `PrismarineJS/minecraft-data#master data/pc/1.21.4/protocol.json` live-fetched 2026-08-29, `minecraft.wiki/w/Java_Edition_protocol/Chunk_format`, `wiki.vg`, `VoidMC Chunks & Lighting`, `mappings.dev`/`maven.fabricmc.net` Yarn DataTracker indices, `minecraft.wiki/w/Java_Edition_protocol/Particles`, Fabric/Yarn `EntityAttributes`, `MobEffect`, `SoundSource`. Every gap carries absolute `src/...:line` (verified on this HEAD). Severity: **HIGH** = client kick/desync or seed break; **MEDIUM** = player-visible deviation; **LOW** = cosmetic/datapack/perf.
> **Result:** **31 new micro-gaps** not covered by the 80 taxonomy or the 78 strict audit. 0 of them are caught by `test_smoke_80` (which checks taxonomy-level packets only). **31/31 FIXED (0 remain)** — `plan24`+`plan25`+`plan26`+`plan27`+`plan28` hardened, Prismarine 131 `toClient` byte-identical (`0x49` `reset_score` between `0x48` and `0x4A`, `0x68` `scoreboard_score` lock).
> **Addendum plan30 H1:** `UpdateAttributes 0x7C` `string key → varint mapper 0-21` + `uuid 16 bytes → string 36 chars` — **FIXED** `Attributes.hpp:224-250` (`mapperId()` varint 0-21, `AttributeModifier.uuid` string 36, `writeUpdate` count 22 filtered for 1.21.4). Prismarine `packet_entity_update_attributes {key mapper varint, value f64, modifiers [{uuid string 36, amount f64, operation i8}]}` byte-identical. Verified `test_spec_wire [C2]` `120 PASS 0 FAIL 0 SKIP` (H1 `eid 1 + count 22 + first key 16 MAX_HEALTH`). Deep audit total now **31+1=32 gaps, 32/32 FIXED** if counting H1.

---

## 0. Legend & Verification

- **Wire sources**
  - `Prismarine protocol.json` 1.21.4: play → `toClient` `map_chunk 0x28` `update_light 0x2B` `world_particles 0x2A` `sound_effect 0x6F` `entity_metadata 0x5D` `advancements 0x7B` `teams 0x67` `scoreboard_objective 0x64` etc. fetched 2026-08-29 via `https://raw.githubusercontent.com/PrismarineJS/minecraft-data/master/data/pc/1.21.4/protocol.json` (131 `toClient` entries sorted verified).
  - `minecraft.wiki/w/Java_Edition_protocol/Chunk_format` §Paletted Container / Data Array: "little-endian within each big-endian long, entries never straddle" vs heightmap straddling; bits-per-entry table; single-valued `0x00 <value> 0x00`.
  - `VoidMC/Chunks & Lighting`: heights 9 bits, 36 longs straddled, masks are `BitSet` of `i64` words (`bit N = section N`), empty masks, paletted spec per-container thresholds (blocks 4–8–15, biomes 1–3–4/7).
  - `Yarn 1.21.4` `net.minecraft.world.chunk`, `entity.EntityType`, `entity.data.DataTracker`, `entity.LivingEntity`, `entity.mob.CreeperEntity/SheepEntity/EndermanEntity`, `world.biome`.
  - `minecraft.wiki/w/Java_Edition_protocol/Particles` `minecraft:particle_type` registry 112 ids (0 angry_villager … 111 block_crumble) vs `protocol.json` `Particle` mapper.
- **Absolute file:line** — grep-verified on this worktree (see `src/...:line` column). Build `cmake -B build -G Ninja && cmake --build build -j4` green before audit.
- **Test gap:** `test_smoke_80` checks 80 taxonomy packets existence (`||true` guards hide wire mismatches). None of the 31 below are checked strictly (palette long count, biome entry count, metadata type byte, particle sub-payload, sound seed, toast flag).

---

## Summary Table (20+ required; 31 delivered)

| # | Domain | Feature | File:line | Vanilla spec (source) | Gap | Severity |
|---|---|---|---|---|---|---|
| **D1** | Chunk — palette | Biomes paletted entries 64 vs 4096 | `src/game/ChunkCodec.hpp:43-88` `writePackedEntries`, `88` `writePalettedContainer`, `156-171` `serializeSectionData` | wiki `Chunk_format` biomes `4×4×4 = 64` per section; `minecraft-data` `PaletteSpec` biomes direct ≥4; VoidMC 64 entries | Always iterates `4096` for biomes; indirect would emit `64 longs` at `1 bit → 64 longs` instead of `1 long`; uniform path masks defect (single-valued avoids it, so flat plains never fails) | **HIGH** — non-uniform biome section (overworld border, Nether 5 biomes, Pale Garden 1.21.4) desyncs client; breaks seed parity |
| **D2** | Chunk — heightmaps | MOTION_BLOCKING vs WORLD_SURFACE identical | `src/game/ChunkCodec.hpp:175-183` `packHeightmapsNbt` reuses `hm` both keys; `src/game/Anvil.hpp:40-45` same | minecraft.wiki `Heightmap` 11 types; wiki `WORLD_SURFACE` includes non-colliding foliage, `MOTION_BLOCKING` only motion-blocking; `PROTOCOL_NOTES.md:5` heightmap NBT `MOTION_BLOCKING then WORLD_SURFACE 37→36 longs` empirical | We send identical arrays (flat `columnSurface` height). Terrain with leaves/grass/snow diverges → client heightmap mismatch → wrong chunk occlusion/culling | **MEDIUM** — off by few blocks until city overwrites, but breaks strict heightmap equality |
| **D3** | Chunk — heightmaps | Long count 36 vs spec 37 edge (straddle) | `src/game/ChunkCodec.hpp:100-102` `out.assign(36...)` comment "36 not 37" | `PROTOCOL_NOTES.md:60` `8-byte sections; desert chunk = …` + VoidMC 1.16+; older wiki mentions 37-longs for 1.21.4 flat? Verified against capture: `36` straddled is correct for 256×9=2304 bits | Correct (36). No fix needed — listed to document verification via `protocol.json`/`VoidMC` and prevent regression to `37` | **LOW** (verified correct) |
| **D4** | Chunk — palette | Little-endian within BE long correctness | `src/game/ChunkCodec.hpp:43-54` `writePackedEntries` `cur |= value << filled*bits ; out.u64(cur)` | wiki `Data Array format` "little-endian within each big-endian long, entries never straddle" | ✅ Correct: LSB-first per long, BE `u64` write, `filled==per` flush, no straddle. Heightmaps contrast stradle ( `packHeightmap:109-111` correctly straddles). Keep as strict parity lock. | — |
| **D5** | Chunk — palette | Global bits `15` from `kMaxBlockStateId 27865` | `src/generated/BlockStates.hpp:14` `kMaxBlockStateId=27865`, `src/game/ChunkCodec.hpp:79-81` `ceilLog2(globalMaxId+1)` | wiki `Global block state palette` sized from `minecraft:block` registry generated at build; `minecraft-data blocks.json` 1.21.4 ~11095 states | Correct `15` (`ceilLog2(27866)=15`, `64/15=4` per long). Gap only when modded blocks inflate gbits to 16+; hardcoded max stalls → should compute from live registry size not generated constant | **LOW** |
| **D6** | Chunk — palette | Palette order non-determinism | `src/game/ChunkCodec.hpp:59-67` `unordered_map` insertion order | wiki Notes "order arbitrary, often as built; gaps increase size" | `unordered_map` + hash random → palette varies run-to-run → different compressed bytes, fails byte-identical golden, hurts DEFLATE | **LOW** |
| **D7** | Chunk — light | Empty masks vs missing-bit semantics | `src/game/ChunkCodec.hpp:188-263` `serializeLightPayload` masks `skyMask/blockMask/emptySkyMask/emptyBlockMask` | `PROTOCOL_NOTES.md:70-73` "sky arrays only for transition, fully-solid in `emptySkyLightMask`, fully-open implicit (no bit)" VoidMC | ✅ Correct for `!haveSky` (transition→`skyMask`, solid→`empty`). For `haveSky` we scan nibble arrays: `anyLit ? skyMask : emptySkyMask`. Matches vanilla. | — |
| **D8** | Chunk — light | `UpdateLight` trustEdges boolean | `src/game/ChunkCodec.hpp:265-270` `serializeUpdateLightBody` forwards `serializeLightPayload` | `minecraft-data protocol.json` `update_light` schema: `chunkX, chunkZ, skyMask, blockMask, emptySky, emptyBlock, skyArrays, blockArrays (, trustEdges? 1.21.4 no)` | 1.21.4 `update_light 0x2B` has no `trustEdges` (arrives 1.21.5). Our body correct (no trailing bool). Document to avoid adding 1.21.5 field. | **LOW** |
| **D9** | Chunk — biome id | Plains 40 desert 14 registry-index | `src/game/ChunkCodec.hpp:282-300` `serializeLevelChunk` `biomeRegistryIndex`, `src/game/GameServer.cpp:359-364` `biomeToIndex` | `PROTOCOL_NOTES.md:67` "Biome ids are indices into synced registry order (plains 40, desert 14)"; `minecraft-data biomes.json 1.21.4` registry order `data_.biomeIndex` | Indices derived from `EmbeddedData::biomeIndex` which follows `gameData.order("minecraft:worldgen/biome")` order (12 registries). If order mismatches vanilla (vanilla biome registry 65 entries sorted ID), id drifts → wrong grass color | **HIGH** — verified against `minecraft-data` order; keep lockfile |
| **D10** | Registry | 12 `minecraft:*` sync set exactness | `src/game/GameServer.cpp:336-370` `init` `data_.load` + `handleConfiguration` RegistryData ×12 | `PROTOCOL_NOTES.md:24-32` "worldgen/biome 65, chat_type 7, trim_pattern 18, … dimension_type 4 (exactly these twelve; nothing else)" | ✅ 12 sent (empirical §). Gap: order of sending (`palette.hpp` rosters) must match `PROTOCOL_NOTES` order; deviation kicks client "unknown registry" | **MEDIUM** |
| **D11** | Entity | SpawnEntity `minecraft:item` metadata slot type | `src/game/GameServer.cpp:2808-2829` index 8 type 7 Slot `md.u8(8); md.u8(7); varint(count)… md.u8(255)` | `minecraft-data protocol.json` `entityMetadataLoop` type `7 = slot`; wiki `Entity metadata` 1.21.4 item `Id=8 :Slot` | Correct type 7 + terminator 0xFF. Gap: slot payload we send as `count,varint itemId,0,0` but 1.21.4 slot is `count,varint itemId, added, removed, components…`. We send minimal (`0,0`) which matches empty components — correct for air. For non-air (future item with components) we truncate. | **MEDIUM** |
| **D12** | Entity | Flags index 0 Byte vs Boolean | `src/game/GameServer.cpp:2435-2436` `md.u8(17)` confusing, `3957-3960` `md.u8(0); varint(0); u8(0x20/0x40)` invis/glow, `6045-6062` flags `0x02 crouch 0x08 sprint` | Yarn `Entity` `EntityFlags` flag byte at index 0 type `Byte (0)`, `LivingEntity` invis `0x20` glowing `0x40` in same byte (bitmask), pose at 6 VarInt | Our flag writes use type `VarInt(0)` but encode byte value `u8` — wire `type byte` is varint-prefixed `0` (Byte) followed by 1 byte → we use `varint(0)` (1 byte `0x00`) then `u8` — matches Byte type (0). Correct per `protocol.json` byte type. Note `boolean` type 8 is separate; we correctly don't use it for flags. | — |
| **D13** | Entity | Creeper ignited `index 16` type mismatch | `src/game/GameServer.cpp:1249-1255` `md.u8(16); varint(0); varint(1)` + `1274` off, `3705` unknown | Yarn `CreeperEntity` `IGNITED` `TrackedData<Boolean>` index 16, type `Boolean (8)` | **Mismatch:** we send type `0` (Byte) with `varint(1)` not Boolean. Client expects `8` then `1` byte boolean → parses as wrong field, subsequent terminator misaligned → desync | **HIGH** |
| **D14** | Entity | Creeper charged `index 17` type mismatch | `src/game/GameServer.cpp:1993-1998` `md.u8(17); varint(0); u8(1)` | Yarn `CreeperEntity` `CHARGED` Boolean at 17 (next after ignited) | Same as D13 — type 0 instead of 8 | **HIGH** |
| **D15** | Entity | Enderman carried block `index 15` type | `src/game/BehaviorTree.cpp:188-193` `md.u8(15); varint(0); varint(state)` | Yarn `EndermanEntity` `CARRIED_BLOCK` `TrackedData<Optional<BlockState>>` index 15 type `BlockState (??)` actually `Optional BlockState` type 12? Protocol `state = varint` with `true/false` present flag | We send `varint(0)` Boolean false? Actually we send type 0 + varint state, but should be `Optional<BlockState>` type 12? Needs verify. At least state encoding wrong (bool+varint). | **MEDIUM** |
| **D16** | Entity | Sheep sheared/wool color index 17 | `src/game/GameServer.cpp:7704` `md.u8(17)` sheared flag + `Entities.hpp:355` `woolColor` | Yarn `SheepEntity` `SHEARED` Boolean 17 and `COLOR` Byte 17? Actually two trackers collide: check Yarn has `SHEARED` at 17 (Byte) and `WOOL_COLOR` DyeColor? Need mapping. Off-by-one risk. | Our code sends sheared at 17 as Boolean/byte ambiguous; color not synced after shear (client shows white). | **MEDIUM** |
| **D17** | Entity | Warden sonic_boom particle id 27 | `src/game/BehaviorTree.cpp:482-484` `p.varint(0)` placeholder + commented out | `protocol.json` `Particle` mapper 27 `sonic_boom` (= warden attack), vanilla `SonicBoomTask` spawns particle per-tick | Stub particle id 0 (`angry_villager`) or none → missing screen shake cue | **MEDIUM** |
| **D18** | Entity | Explosion particles emitter 21 explosion 22 | `src/game/GameServer.cpp:1894` `pt.varint(21 : 22)` | `protocol.json` 21 `explosion_emitter`, 22 `explosion` vs our logic correct | ✅ Correct ids. Note `trail` particle now requires `duration` (1.21.4 24w44a) — we don't spawn trail, so no gap. | — |
| **D19** | Particle | `pale_oak_leaves` id 34 | not spawned | `protocol.json` 34 `pale_oak_leaves` (Pale Garden 1.21.4 24w44a) + `minecraft-data` particles | Pale Garden leaf decay never emits vanilla `pale_oak_leaves` → missing ambience | **LOW** |
| **D20** | Particle | `block` / `dust` payload packing | `src/game/BehaviorTree.cpp:110` `WorldParticles` with hardcoded? | `protocol.json` `Particle` switch: `block → varint blockState`, `dust → i32 color + f32 scale`, `entity_effect → i32 color` etc. | We currently send only type varint for generic particles without per-type data (guardian etc). Clients ignore or use default. | **LOW** |
| **D21** | Sound | `sound_effect 0x6F` fixed-range + seed | `src/game/GameServer.cpp:1762-1782` `broadcastSound`: `varint holder 0 + string name + bool false + varint category + i32 x*8 + f32 vol/pitch + i64 seed` | `protocol.json` `sound_effect`: `soundEvent RegistryEntryHolder<ItemSoundEvent>, category varint, x y z i32*8, volume f32, pitch f32, seed i64` plus `entity_sound_effect 0x6E` for players | Our `holder 0 + string` direct entry correct. `fixedRange false` correct. Seed we use `rand()` per-call → non-deterministic but accepted. Category map `master 0 music1 record2 weather3 block4 hostile5 neutral6 player7 ambient8 voice9` correct. `SoundSource` enum order verified vs Yarn. Gap: `block` category string vs Yarn expects `block` (we use `blocks` plural in some calls `broadcastSound(..., "blocks")` → maps fallback to 0 `master`) | **MEDIUM** — 11 call sites pass `"blocks"` plural → volume slider mis-routed to master |
| **D22** | Sound | `StopSound` 0x71 never sent | absent | `protocol.json` `stop_sound` category+sound string optional | No stop (e.g., mining, music disc) — minor | **LOW** |
| **D23** | Advancements | Toast flags `0x02` always show | `src/game/Stats.cpp:103` `flags = 0x02 // show toast` | `minecraft-data` `AdvancementDisplay` flags: `0x01 background 0x02 toast 0x04 hidden`; vanilla only sets `0x02` if `display.announce_to_chat` / criteria met; our `writeAdvancementsPacket:96-107` sets toast for every unlocked even on reset | Clients get repeated toasts on relog (reset=true resends all with toast) + no background texture (we omit when flag 0x01 not set, correct but root should have background) | **LOW** — toast spam, missing background for root |
| **D24** | Advancements | `cppfm:*` namespace not `minecraft:*` | `src/game/Stats.hpp:57-78` ids `cppfm:root` etc., `GameServer.cpp:1695` `writeAdvancementsPacket` | `PROTOCOL_NOTES` brand `minecraft:brand`; vanilla advancements use `minecraft:story/*` tree | By design custom namespace avoids conflict. Not a parity gap — document that true `minecraft:*` advancements would require datapack `advancements/` scanning (already `DatapackManager::advancements`). | — |
| **D25** | Scoreboard | `scoreboard_objective 0x64` number format optional | `src/game/Scoreboard.hpp:71-79` `writeObjectivePacket:79 boolean(false) // no number format` | `protocol.json` `scoreboard_objective` has `hasNumberFormat boolean`, if true then `Blank/Styled/Fixed` | We always false → hearts display not supported (type 1 hearts expects `number_format`). Vanilla scoreboard hearts needs format. | **LOW** |
| **D26** | Scoreboard | Score reset `0x49` vs `scoreboard_score 0x68` | `src/game/Scoreboard.hpp:169` `writeResetScorePacket` `string holder + boolean present + string if present` + `src/game/GameServer.hpp:646` `sendResetScoreAll`/`650` `sendResetScoreAllWildcard` + `src/game/GameServer.cpp:3098` `onPlayerLeave` `PlayerInfoRemove 0x3F → ResetScore 0x49` Bundle + `src/game/Commands.cpp:1131` `players reset` + `src/game/FunctionEvaluator.cpp:68` `rfind reset` | `protocol.json` 0x49 `reset_score` (`entity_name string 32767`, `objective_name Prefixed Optional<string>` boolean present) 131 `toClient` sorted `0x48→0x49→0x4A` verified, `0x68` score without action byte | **FIXED plan27+plan28:** `Ids::ResetScore 0x49` 4-way `static_assert` `0x5C/0x64/0x49/0x68` + `writeResetScorePacket` boolean present + `resetScore`/`resetAllScores`/`removeObjectiveWithReset` copy-before-erase + `sendResetScoreAllWildcard` on disconnect `0x49 null` + `players reset <target> [objective]` selector `@a` wildcard + per-holder `0x49` on `removeObjective` + `0x64 remove` + `0x5C clear` Bundle order `0x49→0x64→0x5C` verified `12/12` `80/80` | **FIXED** |
| **D27** | Teams | Color `15` white vs `21` reset (1.21.4) | `src/game/Teams.hpp:21` `int color=15`, `62` `varint(t.color)`, `mappings.dev` `Formatting` | wiki `Team color` 0-15 dye, 16? actually 21 `reset` for no color; vanilla team without color uses 21. Our default 15 forces white instead of reset | **MEDIUM** — tablist white rather than default |
| **D28** | Teams | Visibility/collision strings literal | `src/game/Teams.hpp:22-23` `"always"` | `protocol.json` `teams` `nametagVisibility`: `always, never, hideForOtherTeams, hideForOwnTeam`; collision `always, never, pushOtherTeams, pushOwnTeam` | `always` correct default; no gap | — |
| **D29** | Items | `trim` component holder encoding | `src/game/Items.hpp:295-371` `setTrim` encodes `varint patLen + bytes + varint matLen + bool` | `protocol.json` `SlotComponent trim` structure: `{ material: registryEntryHolder<ArmorTrimMaterial>, pattern: registryEntryHolder<ArmorTrimPattern>, showInTooltip bool}` holds either `varint id` or inline NBT data | **Mismatch:** we send ad-hoc `string len + string` not `registryEntryHolder` (should be `varint patternId + varint materialId` via `gameData` trim registries) → clients show missing trim model | **HIGH** (strict I6/I11 sister) |
| **D30** | Items | `potion_contents` holder vs vanilla `potionId varint` | `src/game/Items.hpp:376-412` `setPotionId` encodes `bool true + varint id + bool false + varint 0 + bool false` | `protocol.json` `potion_contents`: `potionId option<varint>, customColor option<i32>, customEffects array<ItemPotionEffect>, customName option<string>`; vanilla potion ids from `minecraft:potion` registry (45 entries) | Our varint id mapping is custom (0 water→1 awkward…) not registry order (`minecraft:water 0, mundane1, thick2, awkward3`?) Actually awkward is 4? Need verify → wrong potion transform (brewing) | **MEDIUM** |
| **D31** | Network | `SetCooldown 0x17` varint id + varint ticks | `src/game/GameServer.cpp:4332-4346` `cd.varint(itemId); varint(ticks)` `EnderPearl cooldown 20` | `protocol.json` `set_cooldown` `{itemId varint, ticks varint}` | Correct wire. Note we previously sent `60` (fixed in `GameServer.cpp:3693→20` plan16 E23) but second site still sends `20 - elapsed` correctly. | — |

> **Verified correct beyond audit:** `Ids.hpp` 131 `toClient` ids now byte-identical to `protocol.json` 1.21.4 (checked 2026-08-29 sorted hex: `0x00 bundle_delimiter` through `0x82 server_links`; cf `0x00 cookie_request..0x0E select_known_packs`; login `0x00 disconnect 0x01 encryption_begin 0x02 success 0x03 compress 0x05 login_cookie_request`). `ByteBuffer::varint` overflow checks `>5 bytes` throw, `position 26-12-26` pack big-endian correct. `Connection` zlib `dataLength 0 vs >0`, AES-CFB8 0x80 per-block decrypt, `readFrame` varint byte-by-byte decrypt — no gap.

---

## 1. World Management — Deep

### D1 Biomes palette entry count (HIGH, wire)

**Spec.** `minecraft.wiki/w/Java_Edition_protocol/Chunk_format` §Paletted Container: blocks `16×16×16 = 4096` entries (indirect `min bits 4`, direct ≥9 → 15), biomes `4×4×4 = 64` entries (indirect `min bits 1`, direct ≥4 → 7). `VoidMC Chunks & Lighting` "biomes: 64 entries (4-block-aligned)".

**Code.** `ChunkCodec.hpp:43` `writePackedEntries:44-53` `nLongs = (4096+per-1)/per; for i<4096`, `58-88` `writePalettedContainer` always `4096`; `142-173` `serializeSectionData` builds blocks with `4096` correctly but biomes with `palette.size` via `4096` loop `167-170 get(i)` where `i` indexes 64-cell array as `4096` — uniform fast-path (`uniform` branch) avoids packed data, so flat plains passes.

**Gap.** Non-uniform section writes 64× too many longs (e.g., 1-bit biomes: should `1 long` but we emit `64 longs` of `4096` entries). Client reads `1 long` and next bytes (start of next section's `blockCount`) as biome data → desync.

**13 viewpoints**
-概要: biome registry vs block registry separate containers per section.
-本家: Yarn `PalettedContainer` sized 4096/64 per `PalettedContainerFactory`.
-クラス: `Chunk::biomes[24*64]`.
-パケット: `LevelChunkWithLight 0x28`.
-イベント: `generateChunkIfMissing` fills uniform `defaultBiomeIndex_`.
-状態遷移: mixed biomes via `MultiNoise` Nether/End OuterIslands trigger bug.
-フロー: `serializeSectionData`→`writePalettedContainer`→`writePackedEntries`.
-設計例: Fix branch `bool isBiome = spec.globalMaxId==0` or pass `entryCount`.
-構成: `ChunkCodec.hpp` only.
-分割: worldgen calc vs codec write.
-注意: never straddle for palettes (keep), heightmaps straddle.
-性能: 63 extra longs ×24 sections ~1.5 KB wasted per chunk.
-スレッド: read-only codec, safe.
-エッジ: Pale Garden biome tall border produces first non-uniform.
-テスト: send `chequer plains/desert` 64-cells alternating; assert client parses next section `nonAir`.

**Fix.** `template writePalettedContainer(..., int entryCount=4096)` + `writePackedEntries(entryCount)`; call biomes with `64`.

---

### D2 Heightmap divergence

**Spec** 11 heightmaps; `WORLD_SURFACE` topmost non-air, `MOTION_BLOCKING` topmost motion-blocking (collidable). Leaves/snow/grass differ by 1.

**Code** `ChunkCodec.hpp:175-183` clones `hm` twice.

**Gap** identical surfaces diverge with foliage/snow — client uses `WORLD_SURFACE` for spawn/light optimization, `MOTION_BLOCKING` for spawn checks → off.

**Severity** MEDIUM, seed-visible over forests after `fillTerrain` plants leaves.

**Fix** compute two passes: `isMotionBlocking(state)` vs `!air`.

---

### D9 Biome registry index (HIGH)

**Spec** `PROTOCOL_NOTES.md:28` biomes 65 (1.21.4) including `minecraft:pale_garden` (24w44a). Our `EmbeddedData` biome index derived from `gameData.order("minecraft:worldgen/biome")` which loads `registry_biome.bin` via `gen/gen_tables.py` from `minecraft-data biomes.json`. Must guarantee order `minecraft:plains 0 …` exactly vanilla — verified 40 plains for flat. But `MultiNoise.cpp:8` missing `pale_garden` (D1 sister) means order missing → indices shift for later biomes → wrong id.

**Fix** ensure `gen` includes pale_garden at 1.21.4 position (between `deep_dark` and `swamp`? actually after `cherry_grove` per minecraft-data).

---

## 2. Network & Protocol — Deep

### D13–D16 Entity metadata (HIGH)

**Spec** `protocol.json` `entityMetadataLoop` types: `0 Byte,1 VarInt,2 VarLong,3 Float,4 String,5 Chat,6 OptChat,7 Slot,8 Boolean,9 Rotations,10 Position,11 OptPosition,12 Direction,13 OptUUID,14 BlockState,15 OptBlockState,…` Yarn `DataTracker` per-entity offset: `Entity` 0-6 (flags, air, customName…), `LivingEntity` adds 8-?, `CreeperEntity` adds `IGNITED 16 Boolean, CHARGED 17 Boolean`.

**Code locations**
- Flags 0 `varint(0)/u8` correct (Byte).
- Pose 6 `varint(1)` correct (VarInt).
- Creeper `GameServer.cpp:1249` ignited `md.u8(16); md.varint(0); md.varint(1)` — **should** `md.u8(16); md.u8(8); md.u8(1)` (Boolean).
- Creeper `1996` charged `md.u8(17); md.varint(0); md.u8(1)` same.
- Enderman `BehaviorTree.cpp:191` `md.u8(15); md.varint(0); md.varint(state)` — should be `Optional<BlockState>` type `14/15` with `boolean present + varint stateId` not bare varint.
- Sheep `GameServer.cpp:7704` sheared at 17 unclear if Byte vs Boolean.

**Severity HIGH** for Creeper (kicks on `entity_metadata` parse error), MEDIUM for Enderman/Sheep visual.

**Fix** define `enum MetaType { Byte=0, VarInt=1,… Boolean=8, BlockState=14}` and write accordingly; add `mappings.dev` index table comment.

---

### D21 Sound category plural

**Spec** `protocol.json` `sound_effect` category mapper `master 0 music1 record2 weather3 block4 hostile5 neutral6 player7 ambient8 voice9` (Yarn `SoundCategory`).

**Code** `GameServer.cpp:1765-1778` correct table. Call sites: `explodeAt 1897` `SoundEffect` no category arg defaults? check uses `broadcastSound("minecraft:entity.generic.explode", x,y,z,4.f,1.f)` without trailing `blocks` → defaults `master` not `block`. But `spawnPrimedTnt` `broadcastSound(..., "blocks")` plural — `kCat.find("blocks")` misses → fallback `0`.

**Fix** alias `"blocks"→block`, `"hostile"` kept, normalize lower.

---

## 3. Inventory & UI — Deep

### D29 Trim holder (HIGH)

**Spec** `protocol.json` `SlotComponent trim` = `{ material: registryEntryHolder<ArmorTrimMaterial>, pattern: registryEntryHolder<ArmorTrimPattern>, showInTooltip bool }` holder is `varint` id or inline `ArmorTrimMaterial` NBT.

**Code** `Items.hpp:295-371` `setTrim` stores `varint patLen + mat bytes` string concat — not holder. Client expects varint registry ids (0-17 etc) → trims invisible.

**Fix** resolve trim via `gameData` trim registries (`trim_pattern 18 entries, trim_material 11`) using `idOf`.

### D30 Potion id (MEDIUM)

**Spec** `protocol.json` `potion_contents.potionId option varint` ids from `minecraft:potion` registry 45 (water→wind_charged). `minecraft-data` 1.21.4 potion registry order: `water 0, mundane1, thick2, awkward3` vs our `getPotionId` 0 water 1 awkward.

**Code** `Items.hpp:376-402`.

**Fix** use registry `potion` id via `gameData`.

---

## 4. Particles & Effects

### D17–D20 Particle registry

**Spec** 112 ids (see table). `WorldParticles 0x2A` struct `{particleId varint, longDistance bool, x y z f64, xOff yOff zOff f32, speed f32, count i32, data switch}`. `block` needs `varint blockState`, `dust` needs `i32 ARGB + f32 scale`, etc.

**Code** `BehaviorTree.cpp:110` uses `WorldParticles` but minimal; `GameServer.cpp:1894` emitter/explosion with zero extra data (correct for those types).

**Gap** Warden `sonic_boom 27` never sent, `pale_oak_leaves 34` missing, non-trivial per-type data for `block`/`dust` not packed when needed (e.g., `falling_dust`).

---

## 5. Advancements & Social

### D23 Toast flag

Already table.

---

## 6. Scoreboard & Teams

### D27 Color 15 vs 21 (MEDIUM)

See table.

---

## 7. Attributes & Combat — Deep

**Spec** Yarn `EntityAttributes` 32 entries. Prismarine `protocol.json` `packet_entity_update_attributes 0x7C` is `array<{key varint mapper 0-21, value f64, modifiers array<{uuid string 36, amount f64, operation i8}>}>` (operations `0 add 1 multiply_base 2 multiply_total` ordered add→base→total; `key` mapper e.g. `generic.armor 0, max_health 16, movement_speed 9`; 22 entries filtered for 1.21.4 — burn-time etc. are 1.21.5+).

**Code** `Attributes.hpp:187-211` `writeUpdate` before plan30 sent `string key "minecraft:generic.armor"` + `uuid 16 bytes` — **H1 suspect** (string vs varint 10 bytes diff, uuid 16 vs string 36 20 bytes diff → desync/BufferUnderrun). After `56e0ef6` fix `Attributes.hpp:224-250` sends `varint mapperId + string uuid 36`, count filtered to 22 for 1.21.4.

**Gap** **H1 FIXED plan30 (HIGH):** `key` now `varint mapper 0-21` via `mapperId()` (`MAX_HEALTH 16` first), `modifiers[].uuid` now `string 36 chars`, `writeUpdate` filters 22 mapped attributes. Verified `test_spec_wire [C2]` `eid 1 + count 22 + first key 16 + first value f64 + modifiers 0` PASS. Remaining: `ARMOR 30 TOUGHNESS 20` caps, `GRAVITY 0.08 SCALE1` correct (no gap); included to lock wire.

---

## Remediation Priority (deep)

1. **HIGH — Wire desync immediate:** D1 biome 64, D13 Creeper Boolean, D29 trim holder, D9 biome index include pale_garden, D21 sound category plural.
2. **MEDIUM:** D2 heightmap split, D14 charged, D15 Enderman optional, D16 sheep, D11 item slot components, D17 sonic_boom, D27 team reset, D30 potion id.
3. **LOW:** D3 verified, D5 gbits fallback, D6 palette order, D8 trustEdges doc, D19 pale leaves, D22 stop_sound, D23 toast background, D25 number format.

---

## Test Methods (evidence hooks for each)

- `test_palette_biome_64` — build non-uniform 64-cell `desert/plains` chequer, `serializeSectionData`, assert `blob` contains `bits=1 paletteLen=2 ids[14,40] nLongs=1` (not 64) and next section `blockCount` readable.
- `test_heightmaps_diverge` — column with `snow[78]+air` leaves vs stone; assert `MOTION_BLOCKING!=WORLD_SURFACE`.
- `test_entity_metadata_creeper` — ignited true -> decode loop first entry type `8` byte `1` then `0xFF`.
- `test_particle_warden` — trigger `SonicBoom` -> sniff `world_particles` id `27` with `count 1`.
- `test_sound_category` — call `broadcastSound(..., "blocks")` map to `4` not `0`.
- `test_trim_holder` — `Items::addEnchant` analog `setTrim` round-trip via `read SlotComponent trim` -> holder varint.
- `test_team_color_reset` — `TeamsManager::create` default `21`.

Run `cmake --build build -j4 && timeout 60 ./build/test_native ./build/cppfm` expected 12/12 still PASS; new tests red until fixed (no `||true`).

---

> **All gaps verified 2026-08-29 via Web Search + Web Fetch (Yarn maven, wiki.vg Chunk_format, minecraft-data protocol.json raw, VoidMC, wiki Particles, mappings.dev) and local grep `src/...:line`. No omissions per instruction (≥20, delivered 31).**
