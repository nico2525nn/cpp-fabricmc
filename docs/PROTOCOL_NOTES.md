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

Empirical after plan12: vanilla coalesces same chunk-section `BlockUpdate 0x09`s into `MultiBlockChange 0x4E` (section pos `i64`, `varint count` + `varint packed = (blockId<<12)|(x<<8)|(z<<4)|y`, section-local), else wraps mixed packets in `BundleDelimiter 0x00` start/end (empty, id `0x00` both directions, play). Our `PacketBatcher` mirrors: `queueBlockChange` batches until `size>=64` or `50ms` timer, `tryFlushAsMultiBlockChange` dedup last-wins per `(x,y,z)` if all in same section → `MultiBlockChange`, else `BundleDelimiter` start + each `BlockUpdate` + end. Missing the `00` delimiter desyncs clients that wait for `Bundle` close.

- `MultiBlockChange` count is `varint`, each entry is `varint` packed `(state<<12) | ((x&15)<<8) | ((z&15)<<4) | (y&15)` with section origin implicit (wiki `Update Section Blocks`: `blockStateId << 12 | (blockLocalX << 8 | blockLocalZ << 4 | blockLocalY)`; plan28 finish fixed the x/y swap that was `(y&15)<<8|(z&15)<<4|(x&15)`).
- `BundleDelimiter` is zero-length; both `0x00` wrappers must be sent even for single non-coalescable `BlockUpdate` if inside bundle window.
- **Non-confusion — packet `BundleDelimiter 0x00` vs Bundle *item*:** the play `BundleDelimiter 0x00` above bundles *packets* (`BundleDelimiter` start + `BlockUpdate`/`MultiBlockChange`/other + `BundleDelimiter` end; `protocol.json` `packet.bundle_delimiter` `0x00`, `SlotComponentType bundle_contents 40`). It is **unrelated** to the Bundle *item* (`minecraft:bundle` + 16 dyed `*_bundle`, component `minecraft:bundle_contents` `SlotComponent 40` `container { contents: Slot[] }`, experimental in 1.21.4 protocol 769, formal in 1.21.5 776; `minecraft-data` `items.json` `bundle 963`). Packet `0x00` wraps heterogeneous packets for one tick; item `bundle_contents` wraps `Slot`s inside `Slot` NBT — no wire coupling (see plan29 §4).

## Light Update (`UpdateLight 0x2B` / `LightUpdateQueue`)

- `UpdateLight` masks are `BitSet` of `int64` words; block-light for `glowstone` (emit 15) propagates via `LightEngine::drain` BFS 3×3 `expandedDirty` and `pendingSkyRebuild` 3×3 (post-plan12). Opacity for emissive `glowstone 0` after special-case, else `minecraft-data filter`.
- `LightUpdateQueue` (`LightEngine.hpp:20`) batches `addQueue`/`removeQueue` and defers sky rebuild until `drain()`; `serializeUpdateLightBody` builds `skyMask/blockMask/emptyMasks` from `chunk.blockLightNib`.

## UpdateAttributes (`0x7C` — H1 FIXED plan30)

Prismarine `minecraft-data 1.21.4` `protocol.json` `packet_entity_update_attributes`:
`{entityId varint, properties array<{key varint mapper 0-21, value f64, modifiers array<{uuid string 36 chars, amount f64, operation i8}>}>}`.
`key` is **varint mapper 0-21** (e.g. `generic.armor=0`, `generic.max_health=16`, `generic.movement_speed=9` etc. — 22 entries filtered for 1.21.4, burn-time etc. are 1.21.5+ skipped).
`modifiers[].uuid` is **string 36 chars** (`xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`) not 16-byte UUID.
Previous impl sent `string "minecraft:generic.armor"` + `uuid 16 bytes` — 10+20 bytes mismatch caused BufferUnderrun (H1 suspect, fixed `Attributes.hpp:224-250` `mapperId` + `string uuid`, `test_spec_wire` `[C2]` verifies `eid varint 1 + count 22 + first key varint 16 MAX_HEALTH`).
22 attributes are synced per `UpdateAttributes` broadcast (`AttributeManager::writeUpdate` `out.varint(eid) + varint 22 + {varint key + f64 value + varint n + {string uuid + f64 + i8}}`).

## Scoreboard (`ScoreboardObjective 0x64` / `ScoreboardScore 0x68` / `ResetScore 0x49` / `ScoreboardDisplayObjective 0x5C`)

Prismarine `minecraft-data 1.21.4` `protocol.json` 131 `toClient` sorted hex verifies `0x49` between `0x48 RemoveMobEffect` and `0x4A CommandSuggestions`:
`packet_reset_score {entity_name string 32767, objective_name Prefixed Optional<string>}` → wire `string holder (varint len + bytes) + boolean present + string if present`.
`packet_scoreboard_score {itemName string, scoreName string, value varint, display_name Prefixed Optional<NBT>, number_format Prefixed Optional<varint>}` → no action byte (pre-1.20.3 `action i8 0/1` removed).
`packet_scoreboard_objective {objectiveName string, action i8 0 create /1 remove /2 update, displayName Chat, type varint 0 integer/1 hearts, number_format option varint}`.
`packet_scoreboard_display_objective {position varint 0 list/1 sidebar/2 below_name, scoreName string}` empty string clears slot.
Bundle coalescing: `0x49` and `0x68` are single-byte varint ids, `dataLength` <256 → `zlib` level 6 `0.01ms`, `BundleDelimiter 0x00` correctly wraps `0x49` with `0x68/0x64/0x5C`. Verified `Ids.hpp:147-152` `static_assert` 4-way lock byte-identical to Prismarine.

## Misc wire facts

- `Set Center Chunk`: plain signed varints (NOT ZigZag).
- Movement flags serverbound are a single u8 bitfield (`onGround`,
  `horizontalCollision`) — sending two bytes desyncs and eventually gets kicked.
- Every player action/use sequence must be answered with
  `Ack Block Change (0x05)` or clients stall their prediction queue.
- System chat content is anonymous-NBT text components; `{text:"…"}` suffices.
- `PlayerChat 0x3B` vs `SystemChat 0x73`: when `chatPubKey` valid, server verifies RSA-SHA256 and relays as `PlayerChat`; else `enforcesSecureChat:false` falls back to `SystemChat`. `MessageAck 0x04` is sunk.

## B6 remaining fromClient (plan45 — W-05/W-08/W-09/W-10/W-11/W-13, G-13 net side)

Shapes below are Prismarine `protocol.json` 1.21.4 live-fetch 2026-09-04
(play toServer 62 ids; `settings` maps to `packet_common_settings` in `types`).

- W-05 `settings 0x0C` = `packet_common_settings` (locale, viewDistance **i8**,
  chatFlags, chatColors, skinParts, mainHand, textFiltering, serverListing,
  particleStatus 0/1/2): parsed, viewDistance clamped 2..32, send radius =
  min(server view-distance, client viewDistance). Config `0x00` shares the shape.
- W-08 `name_item 0x2E{name}` → anvil `Menu::anvilRename` + output recompute
  (cost `ContainerSetData` + `SetSlot(2)` + full resync — G-13 joint).
  `set_beacon_effect 0x32{option,option}` → validity-checked
  (primary ∈ {1,3,11,8,5}, secondary 10 or primary for tier II) + `EntityEffect`
  30s grant. Beacon-block persistence is G-04 territory (no BE kind yet).
  `pick_item_from_block 0x22{position,includeData}` → block→item direct lookup
  + `addToInventory` + resync (BE-copy detail deferred). `pick_item_from_entity
  0x23{entityId,includeData}` → `<kind>_spawn_egg` when the id exists.
  `recipe_book 0x2C{bookId,bookOpen,filterActive}` / `displayed_recipe
  0x2D{recipeId}` → per-player display state kept (unlock distribution stays
  `UpdateRecipes`/`RecipeBookAdd`).
- W-09 all debug editors parse the full shape behind `requireOp` (ops.json +
  creative; unimplemented = deny + log, so future features inherit the gate):
  `update_command_block 0x34` / `_minecart 0x35` / `update_jigsaw_block 0x37`
  (8 fields) / `update_structure_block 0x38` (16 fields) / `generate_structure
  0x19` — storage deferred (no command-block BE / structure-gen API yet).
  `edit_book 0x16{hand,pages[≤100,≤2MB],title?}` → recorded; signing a held
  writable book converts it to written + title. `spectate 0x3B{UUID}` →
  spectator-only teleport + `Camera`; mob UUIDs unsupported (mobs carry no UUID).
- W-10(a) `steer_boat 0x21` → held paddle state (physics stays G-05).
  (b) `resource_pack_receive 0x2F{uuid,result}`: result 1 declined / 2 failed +
  forced pack → play kick. (c) `pong 0x2B{i32}` → last-pong id/ms (RTT source
  shared with O-02). (d) `advancement_tab 0x30{action,tabId?}` → open echoes
  `SelectAdvancementTab 0x4F`; `query_block_nbt 0x01` → `TagQueryResponse 0x75`
  with real sign NBT, empty compound otherwise (no BE serializer yet);
  `query_entity_nbt 0x17` → empty compound echo (entity NBT deferred);
  `select_bundle_item 0x02` → cursor index kept; `set_slot_state 0x12` →
  parsed, no server state (client ghost hint); `debug_sample_subscription 0x15`
  → no-op; `lock_difficulty 0x1B` → op-only latch; `configuration_acknowledged
  0x0E` (empty) → future transfer hook.
- W-11 login ack wait accepts all 5 toServer kinds (cookie values kept, plugin
  answers tolerated); only unknown ids throw.
- W-13(a) legacy `0xFE` list ping → `0xFF`/UCS-2
  `§1\0proto\0ver\0motd\0online\0max` reply (unframed). (b) names enforced
  `[A-Za-z0-9_]{3,16}` ("Invalid username" kick). (c) duplicate UUID/name →
  older session gets a play Disconnect + registry drop (vanilla behavior).
- G-13 net side: `PlaceRecipe 0x25` windowId is ContainerID(**varint**), u8
  fallback kept for proxies; grid clicks already re-run `refreshCraftResult` +
  full resync (live result reflection), shift-click via `quickMove` —
  `test_wire_b6` locks 10 round-trips + batch + separation at Menu level
  (live 2-client `test_crafting_live` stays inventory worktree).

## Unsent play toClient reclassification (plan41 C-10 — 21 before, 19 after horse/vehicle)

`minecraft-data 1.21.4 protocol.json` 131 `toClient` vs `Ids.hpp` mapper. Plan34 sent 6 (`0x18 ChatSuggestions, 0x20 SyncEntityPosition, 0x25 HurtAnimation, 0x50 ServerData, 0x51 ActionBar, 0x6E EntitySoundEffect`), plan41 sent 2 (`0x24 OpenHorseWindow, 0x33 VehicleMove`) → 131-104 sent=27 before plan34, 27-6=21 before plan41, 21-2=19 after plan41. Prismarine `protocol.json` types cited for each.

Tri-classification (plan41 §2): (a)implemented/now sent, (b)omitted/confirmed — alternative exists, not sent `not sent, see …`, (c)future/deferred — feature not yet implemented, deferred to 91, not sent.

| # | Packet | Id | Class | Vanilla 可視性 | 90必須? | 備考 / alternative |
|---|--------|----|-------|---------------|---------|---------------------|
| 1 | ChunkBiomes | 0x0E | (b) omitted | 低 | No | not sent, see LevelChunkWithLight 0x28 biomes paletted (included) |
| 2 | DebugSample | 0x1B | (b) omitted | 低 (debug) | No | not sent, see alternative none (debug sample, no player-visible) |
| 3 | HideMessage | 0x1C | (b) omitted | 低 (chat hide) | No | not sent, see SystemChat 0x73 |
| 4 | ProfilelessChat | 0x1E | (b) omitted | 低 | No | not sent, see SystemChat 0x73 (enforcesSecureChat false) |
| 5 | **OpenHorseWindow** | **0x24** | **(a) implemented** | **高: horse/乗馬でwindow** | **Yes (90 plan41)** | **implemented plan41: windowId varint + slotCount varint + entityId varint** |
| 6 | MapData | 0x2D | (c) future | 高: filled_map 白紙 | No (91) | not sent — map item (MapState) not yet implemented, see ContainerSetContent 0x13 (give filled_map gives empty) |
| 7 | MoveMinecart | 0x31 | (c) future | 中: minecart旋回補間 | No (91) | not sent, see VehicleMove 0x33 alternative (lerpSteps varint + pos + yaw/pitch/headYaw) |
| 8 | **VehicleMove** | **0x33** | **(a) implemented** | **高: boat/minecart滑らか** | **Yes (90 plan41)** | **implemented plan41: x f64 + y f64 + z f64 + yaw f32 + pitch f32 (server→client)** |
| 9 | OpenBook | 0x34 | (b) omitted | 中: book open | No | not sent, see alternative none (item use) |
| 10 | OpenSignEntity | 0x36 | (b) omitted | 中: sign edit | No | not sent, see BlockEntityData 0x07 |
| 11 | EndCombatEvent | 0x3C | (b) omitted | 低: combat | No | not sent, see HurtAnimation 0x25 + DamageEvent 0x1A |
| 12 | EnterCombatEvent | 0x3D | (b) omitted | 低 | No | not sent, see HurtAnimation 0x25 + DamageEvent 0x1A |
| 13 | DeathCombatEvent | 0x3E | (b) omitted | 低 | No | not sent, see HurtAnimation 0x25 + DamageEvent 0x1A |
| 14 | FacePlayer | 0x41 | (b) omitted | 中: /face | No | not sent — future `/face` command, 91 |
| 15 | PlayerRotation | 0x43 | (b) omitted | 中: rotation | No | not sent, see PlayerPosition 0x42 + EntityLook 0x32 |
| 16 | PlayRemoveResourcePack | 0x4A | (b) omitted | 低: pack | No | not sent, see cf:sc RemoveResourcePack 0x08 (configuration) |
| 17 | PlayAddResourcePack | 0x4B | (b) omitted | 低 | No | not sent, see cf:sc AddResourcePack 0x09 (configuration) |
| 18 | SelectAdvancementTab | 0x4F | (b) omitted | 低: UI tab 演出 | No | not sent, see UpdateAdvancements 0x7B (tab is client UI) |
| 19 | UpdateViewDistance | 0x59 | (b) omitted | 中: F3 viewDistance | No | not sent, see Login 0x2C viewDistance + SimulationDistance 0x69 (F3 dummy) |
| 20 | AttachEntity | 0x5E | (b) omitted | 中: leash | No | not sent, see SetPassengers 0x65 (holdingId + passengers[]) |
| 21 | SetPlayerInventory | 0x66 | (b) omitted | 低: slot | No | not sent, see ContainerSetContent 0x13 windowId 0 |
| 22 | StartConfiguration | 0x70 | (b) omitted | 低: config | No | not sent, see Transfer 0x7A (configuration) |
| 23 | SetTikingState | 0x78 | (b) omitted | 低: 20t fixed | No | not sent, 20t fixed (GameServer_tick.cpp 50ms) |
| 24 | StepTick | 0x79 | (b) omitted | 低 | No | not sent, 20t fixed |
| 25 | SetProjectilePower | 0x80 | (c) future | 中: 弓引き演出 | No (91) | not sent — bow pull power, deferred to 91 (EntityVelocity 0x5F etc) |
| 26 | CustomReportDetails | 0x81 | (b) omitted | 低: report | No | not sent |
| 27 | ServerLinks | 0x82 | (b) omitted | 低: links | No | not sent, see ServerData 0x50 |

Already sent (plan34, not in 21 strict but in 33 mapper):
| | ChatSuggestions | 0x18 | (a) sent | — | — | sent plan34: chat_suggestions action+entries |
| | SyncEntityPosition | 0x20 | (a) sent | — | — | sent plan34: sync_entity_position 10 fields |
| | HurtAnimation | 0x25 | (a) sent | — | — | sent plan34: hurt_animation eid+yaw |
| | ServerData | 0x50 | (a) sent | — | — | sent plan34: server_data motd+iconBytes |
| | ActionBar | 0x51 | (a) sent | — | — | sent plan34: action_bar anonymousNbt |
| | EntitySoundEffect | 0x6E | (a) sent | — | — | sent plan34: entity_sound_effect |

Strict unsent after plan41: **19** (27 rows -2 implemented -6 already sent =19 strict; 21 before plan41). `grep -c "not sent, see" docs/PROTOCOL_NOTES.md` should be 19+ uses → 21 before including horse/vehicle now sent are excluded.
Wire: `OpenHorseWindow 0x24 {windowId varint, slotCount varint, entityId varint}` / `VehicleMove 0x33 {x double, y double, z double, yaw float, pitch float}` per `minecraft-data 1.21.4 protocol.json`.

## Seed parity / worldgen RNG (plan45 G-11 — L3 difference declaration)

Vanilla 1.21.4 worldgen RNG is **Xoroshiro128++ / LegacyRand** (`WorldgenRandom`,
`LegacyRand`). Ours is **xorshift64** (`WorldGen.cpp rng64`: `s^=s<<13; s^=s>>7;
s^=s<<17`) + splitmix-style `smStructureHash` (`StructureManager.hpp:34-42`).
Therefore **the same seed does NOT produce vanilla-identical terrain/structure
placement** — this is an honest, declared gap (same class as E-14), NOT a bug.
Per plan45 §1 the xorshift implementation is intentionally kept (L3 declaration,
no Xoroshiro replacement).

What IS guaranteed (regression-locked by `tests/test_seed_parity` + `tools/seed_parity_check.py`):
- **L1 self-consistency (15+4 cases):** `smStructureHash`/`triangularOffsetRaw`/
  `smStructureAtChunk` outputs equal the independent Python hand-calculation
  (no shared code — G-01 tautology guard). 5 sets x 3 seeds exact origins +
  pillager frequency raw hash + triangular spot.
- **L2 determinism (50 chunks):** same seed → byte-identical biome sampling
  (overworld/nether/end emit paths) and structure placement across two
  independent `MultiNoiseBiomeSource`/`StructureManager` instances.

## Biome / structure-set correspondence (plan45 G-11 — jar-verified)

Vanilla 1.21.4 client jar (`data/minecraft/worldgen/`, piston-meta 1.21.4,
sha1-verified 2026-09-04): **65 biome files, 20 structure_set files,
34 structure files**. Our `MultiNoise` table is 65 (54 overworld + 5 nether +
5 end + `the_void` registry-only); `StructureManager` keeps 20 sets with
plan33 singular統合名 mapping 1:1 to jar plural ids (`village`→`villages`,
`desert_pyramid`→`desert_pyramids`, `jungle_temple`→`jungle_temples`
(structure `jungle_pyramid`), `monument`→`ocean_monuments`,
`mansion`→`woodland_mansions`, `ruined_portal`→`ruined_portals` (7 structures),
`shipwreck`→`shipwrecks` (2), `ocean_ruins`→`ocean_ruins` (cold+warm),
`mineshaft`→`mineshafts` (2), `stronghold`→`strongholds`,
`ancient_city`→`ancient_cities`, `trail_ruins`→`trail_ruins`,
`trial_chambers`→`trial_chambers`, `nether_complexes`→`nether_complexes`
(fortress w2 / bastion_remnant w3), `nether_fossil`→`nether_fossils`,
`end_city`→`end_cities` (triangular), `pillager_outpost`→`pillager_outposts`
(freq 0.2), `buried_treasure`→`buried_treasures` (freq 0.01),
`igloo`→`igloos`, `swamp_hut`→`swamp_huts`). All spacing/separation/salt/
spread/frequency values verified equal to jar JSON. 34 structures each map to
a set (locked in `test_gameplay_full`). Remaining honest gap: nether
fortress/bastion share placeholder pieces (weights 2:3 applied), not vanilla
jigsaw pools.
