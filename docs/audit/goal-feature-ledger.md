# Goal feature coverage ledger

## Status semantics

This current-evidence ledger covers every row in `docs/MISSING_FEATURES_1_21_4.md`
`#1–#90`. `historical` preserves the source matrix token (`DONE` here) and is
not copied into `coverage`. `PASS` requires a named focused, wire, or gameplay
result in current documentation. `PARTIAL` records an explicit approximation or
omission. `TODO` records no implementation/evidence. `UNVERIFIED` means the row is
historically closed but lacks a row-specific named result strong enough to upgrade
it. Unverified is never treated as pass. The current adjudication uses the
real-entry artifacts in `goal-live-features.md`, `goal-live-interactions.md`,
`goal-live-remaining.md`, and `goal-gui-soak.md`.

Several source references retain historical pre-split names such as
`GameServer.cpp` and `World.cpp` from the source matrix. Current implementation
files are split under `src/game/GameServer_*.cpp` and related modules; those
historical references do not imply that a file with the old path still exists.

## Numbered matrix

| # | feature | historical | coverage | source reference | evidence/limitation |
|---:|---|---|---|---|---|
| 1 | Nether terrain (`fillNether`) | DONE | **UNVERIFIED** | `World.cpp:314` + `StructureManager.cpp:62` | **plan12 §1 DONE:** 5 biomes via 4 `ImprovedNoise` (`nether_wastes 36.3%/crimson 22.2%/soul_sand_valley 17.1%/basalt_deltas 15.9%/warped 8.5%`), bedrock roof/floor `0-4/123-127`... |
| 2 | End terrain (`fillEnd`) | DONE | **UNVERIFIED** | `World.cpp:546` | **plan12 §2 DONE:** outer islands `1/14 +1/4 duplicate` `sin(r/80)` ring `r>1000`, `Highlands/Midlands/Barrens/SmallIslands` biomes, chorus `0-2/chunk Y65-75`, `EndCity spacing ... |
| 3 | Portal system (`PortalHandler`) | DONE | **PARTIAL** | `PortalHandler.hpp:81`, `GameServer.cpp:6229` | The owned interaction fixture placed a real Nether portal block and observed `Respawn 0x4C`, `PlayerPosition 0x42`, and portal-cooldown `GameEvent`; frame construction and round-trip/safe-spawn parity remain unverified. |
| 4 | Light cross-chunk | DONE | **UNVERIFIED** | `LightEngine.cpp:81` + `LightEngine.hpp:20` | **DONE:** block-light global BFS + sky-light BFS with `LightUpdateQueue`, `drain()` expands `dirtyChunks` to 3×3 (182-191), per-chunk `UpdateLight 0x2B` via `serializeUpdateLigh... |
| 5 | Spawn chunk loader | DONE | **UNVERIFIED** | `World.hpp:347` `ChunkTicket.hpp:1` | 5×5 `ForcedChunks` with `ChunkTicketType::SPAWN level 31` + `level.dat: ForcedChunks long[]` + `server.properties spawn-protection 16` + `isSpawnProtected` gate + `/forceload` s... |
| 6 | Simulation distance culling | DONE | **UNVERIFIED** | `World.hpp:422` `isChunkInSimulationDistance` | `viewDistance` vs `simulationDistance` distinguished; `Fluids.cpp:77`, `Redstone.cpp:370`, `LightEngine.cpp:46`, `BlockTickScheduler.cpp:59` all gated by `isChunkInSimulationDis... |
| 7 | Chunk unload LRU | DONE | **UNVERIFIED** | `World.hpp::allChunkKeys/eraseChunk`, `GameServer_tick.cpp::chunksUnloadTick`, `GameServer_world.cpp::saveChunkAsync` | 100t LRU, dirty flush, forced/spawn-ticket protection, per-dimension checks, `maxLoadedChunks` cap, and Chebyshev-distance eviction. Async save snapshots NBT/wire data without c... |
| 8 | Structures | DONE | **PARTIAL** | `StructureManager.cpp:66` + `StructurePlacer.cpp:18` + `WorldGen.cpp:fillTerrainV3` | **plan12 §3 DONE; plan29 §1/§2 polish DONE; plan32 world DONE:** `StructureSet {spacing,separation,salt,Linear/Triangular}` — `stronghold 32/5`, `mineshaft 10/5`, `monument 32/5... |
| 9 | `level.dat` full | DONE | **PARTIAL** | `WorldDataManager.hpp:26` + `Persistence.hpp:69` | Live feature entry wrote a non-trivial `level.dat` (`>=64` bytes); full NBT/DataVersion/dimension-field oracle remains unverified. |
| 10 | WorldBorder damage | DONE | **PARTIAL** | `GameServer.hpp:852` `isInsideBorder` | The remaining-entry fixture observed `InitializeWorldBorder 0x26`, center/size packets `0x52/0x54`, then controlled outside-border `SetHealth 0x62`/`DamageEvent 0x1A`; warning-distance and all gamerule/dimension semantics remain unverified. |
| 11 | Stairs/slab placement context | DONE | **UNVERIFIED** | `GameServer.cpp:6944` + `BlockTickScheduler.hpp:190` | **plan12 §4 DONE:** `ItemUseContext` + `FluidSim::getFluidState().isWater()` → `waterlogged`, `computeStairsShape` 5-value `inner_left/right/outer_left/right`, `half` via `face`... |
| 12 | Door two-block | DONE | **UNVERIFIED** | `GameServer.cpp:3612` | `lower/upper` with `facing/half/open/hinge`, toggle preserves `facing/hinge`. / |
| 13 | Farming `randomTickSpeed` | DONE | **UNVERIFIED** | `BlockTickScheduler.cpp:48` | **plan12 §5 DONE:** `randomTickSpeed 0` gate + `isChunkInSimulationDistance` + `light>=9` + `growthSpeed 1-3×` + `Cocoa age2 jungle_log 1/5` + `SweetBerry 1/3→1/2` + `NetherWart... |
| 14 | BoneMeal `fertilize` | DONE | **UNVERIFIED** | `GameServer.cpp:3531` | `bone_meal` on `wheat/potatoes/carrots/beetroots/sapling` → max age / tree, sound, consume. / |
| 15 | Farmland trample | DONE | **UNVERIFIED** | `GameServer.cpp:6031` + `BlockTickScheduler.cpp:306` | **plan12 §6 DONE:** `fallDistance>0.5` `prob=fallDist-0.5` `!isSneaking` + `mobGriefing` gate, `LevelEvent 2001`, `moisture 0-7` + `isNearWater 9×9×2` scan, `moisture==0 && !has... |
| 16 | Fire `FireBehavior` | DONE | **PARTIAL** | `BlockTickScheduler.cpp:543` | **plan12 §7 DONE:** `FlammableRegistry` `{planks 5/20, leaves 30/60…}` + `SoulFire` `soul_sand/soil` only + `Campfire lit` gate + `fire shape north/south/east/west/up` via `isFl... |
| 17 | TNT ignition | DONE | **UNVERIFIED** | `Entities.hpp:67` + `GameServer.cpp:587` | `TntEntity fuse 80` `primedTntsTick` + `SpawnEntity 0x01` `minecraft:tnt`, `dispenser tnt → primed` + `flint_and_steel` ignite `tnt[unstable]`. / |
| 18 | Buckets | DONE | **UNVERIFIED** | `GameServer.cpp:3531` | `water_bucket`/`lava_bucket` ↔ `bucket` + `water`/`lava[level=0]` source, `level 0` source check, sound, `applyDamage` for flint. / |
| 19 | Pistons | DONE | **PARTIAL** | `Redstone.cpp:597` | `MovingPiston` 2-tick + `isStickyBlock` + `sticksTogether slime≠honey` + 12-block BFS + `isUnpushable` + `PistonMove` sound; **plan29 §9 verified:** existing BFS 6-dir / 12-bloc... |
| 20 | Fluid solidify | DONE | **PARTIAL** | `Fluids.cpp` + `BlockTickScheduler.cpp` + `GameServer.hpp` | **review pass:** explicit source/flowing/falling levels (`0..7`/`8`), directional water/lava interaction (`falling lava` downward stone; horizontal/top obsidian/cobblestone), co... |
| 21 | Hopper `hoppersTick` | DONE | **UNVERIFIED** | `GameServer.cpp:372` (50b) `hoppersTick` 8t | Pull from `y+1`, item entity pickup, push down, edge-trigger, redstone lock `isPoweredHere` for hopper + dispenser. / |
| 22 | Comparator | DONE | **UNVERIFIED** | `Redstone.cpp:270` | `mode compare/subtract` + side power `max(0,out-side)` + `analogOutputForContainer` 0-15; tick. / |
| 23 | Observer | DONE | **UNVERIFIED** | `Redstone.cpp:384` | `facing` 6-dir check `ox+fdx==x` + 2-tick pulse `powered true queue now+2`, `UpdateLight` via `onBlockChanged`. / |
| 24 | Rails | DONE | **PARTIAL** | `Redstone.cpp:597` + `GameServer.cpp:4655` | `recomputeRailShape` now evaluates all four horizontal neighbors, same/upper/lower slope candidates, endpoint-height alignment, deterministic straight/curve/ascending selection,... |
| 25 | Dispenser per-item | DONE | **UNVERIFIED** | `GameServer.cpp:1739` | **plan12 §9 DONE:** 9-slot edge-trigger `facing` 6-dir; `arrow/snowball/egg/pearl/fire_charge/tnt→explodeAt` + `bucket water/lava/powder_snow pickup/dispense` + `potion splash/l... |
| 26 | Dropper | DONE | **UNVERIFIED** | `GameServer.cpp:1770` + `BlockEntities.hpp:64` | **plan12 §10 DONE:** `Kind::Dropper` 9 slots + `facing` dispense: `doDropperInsert` try `containerAt` `canInsert` → `insert 1` else `spawnItemDrop` (never projectile). / |
| 27 | Cactus/sugar cane growth | DONE | **UNVERIFIED** | `BlockTickScheduler.cpp:234` + `BlockStates.hpp:132` | **plan13 §1 DONE:** `StemBehavior` + `BambooBehavior` `stage 0→1` + `age thick >=4` + `bambooUpdateLeaves h=1→16` `leaves none/small/large` top3, `GrassBlockBehavior` snowy `sno... |
| 28 | MobKind 46 | DONE | **UNVERIFIED** | `Entities.hpp:52` | **plan25 E1 DONE:** 149 kinds (up from 13→46→86→149) with `MobStats` 300/200/500 etc., all `typeId` via `gen::entityTypeIdByName` (kEntities 149, armadillo/bogged/breeze/creakin... |
| 29 | Brain-Goal-Sensor vs BehaviorTree | DONE | **PARTIAL** | `BehaviorTree.hpp:204` + `AiBrain.cpp:142` | **plan14 §1 DONE:** `BehaviorTreeParser` maps `wither_skull/dragon_breath/warden_sonic_boom` → `WitherSkullAction/DragonBreathAction` + `BreedGoal` wild, and `Brain` builds per-... |
| 30 | `SetEquipment 0x60` | DONE | **PARTIAL** | `EquipmentComponent.hpp:1` + `GameServer.cpp:3202` | The owned interaction fixture replaced a helmet and parsed a real `SetEquipment 0x60` entity id plus head slot marker; full item-component, trim, and drop-chance parity remains unverified. |
| 31 | `SetPassengers 0x65` riding | DONE | **PARTIAL** | `GameServer.cpp:5145` + `BehaviorTree.cpp:70` | Live `UseEntity` against a real summoned pig kept the connection alive, but emitted no `SetPassengers`; horse/boat/minecart mount state remains unverified. |
| 32 | Durability | DONE | **UNVERIFIED** | `Items.hpp:84` + `DamageComponent.hpp:14` + `CostCalculator.hpp:36` | **plan13 §4 DONE:** `Unbreaking 1/(l+1)` + `Mending` XP `repair/2` + `Anvil` `Too Expensive >=40` `nextRepairCost` + `CustomName` `MC/ItemName`. / |
| 33 | Enchant effects | DONE | **UNVERIFIED** | `EnchantmentHelper.hpp:50` + `Attributes.hpp:66` | **plan13 §5 DONE:** `Efficiency 1+lvl²` mining speed + `FrostWalker radius 2+lvl` `frosted_ice` + `SoulSpeed 0.105*lvl` + `SwiftSneak 0.15*lvl` attribute sync `0x7C`. / |
| 34 | Slime/MagmaCube split | DONE | **UNVERIFIED** | `GameServer.cpp:588` `slimeSize` | Death of `Slime/MagmaCube` size>0 spawns 2-4 babies size-1 with half health, `broadcastMobSpawn`. / |
| 35 | Wither/Dragon boss AI | DONE | **UNVERIFIED** | `BossAI.hpp:19` + `GameServer.hpp:868` | **plan25 E3 DONE:** `WitherSkull` 40t loop **3-burst** (central + 2 side heads spread 0.35, charged blue at ≤150 HP) + `Dragon phases circling/approach/perch` + `BossBar ADD/HEA... |
| 36 | Wool shear | DONE | **UNVERIFIED** | `GameServer.cpp:3964` | `shears` on `Sheep` `!sheared` → `sheared=true` + `woolColor` drop 1-3, `SetEntityMetadata` index 17, damage shears. / |
| 37 | EnderPearl teleport | DONE | **PARTIAL** | `GameServer.cpp:2556` `projectilesTick` | The remaining-entry fixture sent a real pearl `UseItem 0x3D`, matched projectile type `42`, and parsed exact `SetCooldown 0x17` item `1042`, duration `20`; block-hit teleport/unsafe-space behavior remains unverified. |
| 38 | Spawn eggs | DONE | **PARTIAL** | `GameServer.cpp:6327` + `GameServer.hpp:182` | The remaining-entry fixture used a pig spawn egg through `UseItemOn 0x3C` and matched `SpawnEntity 0x01` entity type `94`; broader egg roster, consumption, and obstruction semantics remain unverified. |
| 39 | Enderman | DONE | **UNVERIFIED** | `BehaviorTree.cpp:70` | **plan13 §6 DONE:** `TeleportRandomAction` 32-block `EntityTeleport 0x77` + `PickupBlockAction` `grass/dirt/sand` 1/1000 `BlockUpdate` + `StareAction` dot `>0.985` pumpkin guard. / |
| 40 | Charged Creeper | DONE | **UNVERIFIED** | `GameServer.cpp:3737` + `Ids.hpp:74` | **plan13 §7 DONE:** `LightningBolt 0x74` `SpawnEntity` + `channeling trident` thunder check + `creeperCharged` `SetEntityMetadata 17` + `explodeAt 6.0` vs `3.0`. / |
| 41 | XP orbs | DONE | **PARTIAL** | `GameServer.cpp:372` `xpOrbsTick` | Live `/xp add` produced the experience wire consequence, but no XP-orb spawn/pickup assertion was made. |
| 42 | Projectiles tick | DONE | **PARTIAL** | `GameServer.cpp:372` `projectilesTick` | A real bow `UseItem 0x3D` produced `SpawnEntity 0x01` and repeated `MoveEntityPosRot 0x30` packets for the same projectile id; per-kind metadata, swept collision, impact effects, and pickup remain unverified. |
| 43 | Breeding/aging | DONE | **UNVERIFIED** | `AiBrain.cpp:142` + `BehaviorTree.cpp:278` | **plan14 §3 DONE:** `BreedGoal` `loveTicks 600` `findLovePartner 8` + `breed()` `baby age -24000` `breedCooldown 6000` + `EntityEvent 18` + `xp 1-7`. / |
| 44 | Villager trading | DONE | **UNVERIFIED** | `Entities.hpp:89` + `GameServer.cpp:2037` | **plan14 §4 DONE:** `VillagerData` `Type 7` `Profession 15` `level 1-5` + `Gossip` `rep` + `TradeList 0x2E` `level*2` + `SelectTrade` `demand` + `restock 24000t` + `priceMultipl... |
| 45 | Boat/Minecart | DONE | **PARTIAL** | `Entities.hpp:151` + `GameServer.cpp:4655` | `MobKind::Boat/Minecart 6HP` + `MoveVehicle 0x20` + `minecartsTick powered_rail 0.06` + `boat` spawn via `bucket`/`dispenser`; polish: buoyancy/friction simplified, no `VehicleM... |
| 46 | `MenuType` `Barrel/ShulkerBox` | DONE | **PARTIAL** | `Containers.hpp:29` `Barrel, ShulkerBox` + `totalSlots 27+36` | The remaining-entry fixture opened both blocks over `UseItemOn`, receiving `OpenScreen 0x35` and `ContainerSetContent 0x13` with `63` slots; item mutation, lock/color, and persistence semantics remain unverified. |
| 47 | Enchanting table | DONE | **PASS** | `MenuLogic.cpp/.hpp` + `CostCalculator.hpp` + `GameServer.cpp` | `countBookshelves` air-gap max 15 and shelf-derived costs remain; `EnchantmentMenuLogic` now deterministically rebuilds three bounded offers, validates the selected button/input... |
| 48 | Anvil | DONE | **PARTIAL** | `CostCalculator.hpp:36` + `GameServer.cpp:4882` | The remaining-entry fixture opened menu type `8` through `UseItemOn`, parsed `OpenScreen 0x35`, `ContainerSetContent 0x13` with `39` slots, and one `ContainerSetData 0x14`; rename, cost, and result-click semantics remain unverified. |
| 49 | Brewing stand | DONE | **PARTIAL** | `GameServer.cpp:3156` | The remaining-entry fixture opened menu type `11`, parsed `OpenScreen 0x35`, `ContainerSetContent 0x13` with `41` slots, and two `ContainerSetData 0x14` updates; ingredient/fuel/brew-result semantics remain unverified. |
| 50 | Stonecutter ghost | DONE | **PARTIAL** | `GameServer.cpp:4780` + `GameServer.hpp:920` | **plan13 §9 DONE:** `PlaceGhostRecipe 0x39` throttle `5t` + `ContainerSetSlot 0x15` + `ghost` echo; polish: preview throttle still simplified 5t. / |
| 51 | Creative `SetCreativeModeSlot 0x36` | DONE | **PARTIAL** | `GameServer.cpp:3112` | The remaining-entry fixture sent a real `SetCreativeModeSlot 0x36` helmet stack and parsed `SetEquipment 0x60` marker `5` (head); cursor `-1`, stack limits, and malicious slot/component cases remain unverified. |
| 52 | Drag `mode5` | DONE | **UNVERIFIED** | `MenuInteraction.cpp:229` | `button 0/4 start` `dragSlots`, `1/5 addSlot`, `2/6 end` distribute left/even vs right 1-per-slot, `maxStackFor` respect. / |
| 53 | `WindowClick` authoritative | DONE | **PARTIAL** | `MenuInteraction.cpp:1` `ClickLogic` | The owned interaction fixture sent a valid and a stale `WindowClick 0x10`; both received authoritative content/resync behavior, while all click modes and item mutation semantics remain unverified. |
| 54 | `ContainerSetContent 0x13`/`SetSlot 0x15` | DONE | **PARTIAL** | `GameServer.cpp:1246` `sendMenuContent` | A real chest open parsed `windowId=1`, `stateId=3`, and `slotCount=63`; stale-click resync emitted `ContainerSetContent 0x13`, while every item-stack/component and `SetSlot` branch remains unverified. |
| 55 | Hopper menu 5/9 slots | DONE | **PARTIAL** | `Containers.hpp:29` `Hopper` 5+36 / `Dispenser` 9+36 | The remaining-entry fixture opened a hopper through `UseItemOn`, parsed `OpenScreen 0x35` and authoritative `ContainerSetContent 0x13` with `41` slots; transfer, redstone, and dispenser/dropper click semantics remain unverified. |
| 56 | Recipe book `0x44` + `PlaceRecipe 0x25` | DONE | **PASS** | `GameServer.cpp:4780` | **plan13 §9 DONE; plan32 inventory DONE (1578 recipes):** `PlaceRecipe 0x25` + `PlaceGhostRecipe 0x39` + `RecipeBook` `Furnace`/`Stonecutter` ghost via `RecipeBook` `type 6/10` ... |
| 57 | Brigadier foundation | DONE | **PARTIAL** | `brigadier/Tree.hpp:1` `CommandNode`/`CommandDispatcher` `writeDeclareCommands` 0x11 | Live login received `DeclareCommands` and tab completion returned; every parser/redirect/suggestion semantic is not independently decoded. |
| 58 | Arg types 48 | DONE | **UNVERIFIED** | `brigadier/Arguments.hpp:12` | **plan13 §10 DONE:** `BlockState 12` + `BlockPredicate 13` + `ItemPredicate 15` + `Nbt 19/20/21` + `NbtPath 22` + `Objective 23` + `Team 31` + suggestions. / |
| 59 | Tab completion | DONE | **PARTIAL** | `Tree.hpp:206` + `Commands.cpp:1243` | Live `/gi` returned a non-truncated `TabComplete` response; the suggestion contents and all argument-specific lists remain unverified. |
| 60 | `/give`/`/summon`/`/setblock` | DONE | **PASS** | `Commands.cpp:261` | Real play-session feedback passed for `/give`, `/summon`, and `/setblock`; the transcript also observed the resulting `SetSlot`, `SpawnEntity`, and block-update packets. |
| 61 | `/fill` | DONE | **PASS** | `Commands.cpp:880` | Real `/fill 42 -60 42 44 -60 44 minecraft:glass` returned `Filled 9 blocks` and section/block update packets. |
| 62 | `/execute as @p run` | DONE | **PARTIAL** | `Commands.cpp:900` | Single-level `execute as <entity> run <command>` via `resolveSelector` + re-dispatch with target source, but no `at/positioned/anchored/run` branches. / |
| 63 | `/function` | DONE | **PARTIAL** | `Commands.cpp:920` | `namespace:path` → `assets/data/<ns>/functions/<path>.mcfunction` read lines → `dispatchConsole` each, but no `minecraft:tick` tag auto-run, no recursion limit. / |
| 64 | `/reload` | DONE | **PASS** | `Commands.cpp:940` | Real datapack session returned `Reload complete` after function/schedule commands; the command path and feedback were exercised through TCP. |
| 65 | Tags 67 item / 20 block | DONE | **PARTIAL** | `TagManager.hpp:1` `loadDirectory` `pendingRefs_` + `ensureItemDefaults` 67, `GameServer.cpp:296` wired | Live `/tag` and `/team` feedback passed, but the complete 67-item/20-block tag contents and ingredient resolution were not parsed. |
| 66 | `Ingredient` tag resolve | DONE | **UNVERIFIED** | `Recipes.cpp:1` | `tags_` map now 67, `TagManager::applyToRecipeTags` at init. / |
| 67 | `LootTableEvaluator` | DONE | **PASS** | `LootTables.hpp:1` `evaluate(bn,tool)` | `pools/rolls/weight/set_count`, `fortune` bonus, `silk_touch` via `tool.hasSilkTouch()` → drop block itself; **plan35 polish:** `set_count`/`looting_enchant`/`enchant_randomly`/... |
| 68 | `DatapackManager` | DONE | **PARTIAL** | `DatapackManager.hpp:12` | Live datapack `list`, `/function goal:entry`, `/schedule function`, and `/reload` feedback passed; enable/disable and every resource directory remain unverified. |
| 69 | `FunctionEvaluator` `/function` | DONE | **UNVERIFIED** | `FunctionEvaluator.hpp:1` + `Commands.cpp:1243` | **plan13 §10 DONE:** `executeFunction` recursion 10 + `return` + `execute store result/success score` + `schedule function` `append/replace` + `tick()` from `GameServer::tickOnc... |
| 70 | `/tag` `/team` `/bossbar` | DONE | **PARTIAL** | `Commands.cpp:872` + `Ids.hpp:129` | **plan32 DONE (30+ commands):** `/team add/remove/join/leave` + `Teams 0x67` + `/bossbar add/remove/set` + `BossBar 0x0A ADD/HEALTH` + `/tag add/remove/list`; plus **plan32 30+ ... |
| 71 | VarInt/VarLong, big-endian, Position | DONE | **PARTIAL** | `ByteBuffer.hpp:1` | Real protocol login/config/play framing and command/entity VarInts passed; no independent boundary-vector oracle covers every VarLong, signed Position, or endian field. |
| 72 | Handshake → Status/Login/Config/Play | DONE | **PASS** | `GameServer.cpp:1420` `handleHandshake` | Owned client completed status, offline login, configuration, and play for protocol 769; `DeclareCommands`, `UpdateTime`, Join Game, and non-empty play packets were observed. |
| 73 | Compression/Encryption | DONE | **PARTIAL** | `Connection.hpp:29` | Real login/play session negotiated and used compression threshold `256`; encrypted online-mode/RSA/AES path was not exercised. |
| 74 | Chat signing `PlayerChat 0x3B` | DONE | **PASS** | `GameServer.cpp:3046` + `net/Crypto.hpp` | `ChatMessageProcessor::verify RSA-SHA256` `ChatMessage 0x07 timestamp/salt/signature` + `MessageAck 0x04` + `shouldUsePlayerChat` → `PlayerChat 0x3B` when key valid else `System... |
| 75 | Bundle `0x00` + `MultiBlockChange 0x4E` | DONE | **PASS** | `PacketBatcher.cpp:17` | `queuePacket`/`flush` true `BundleDelimiter 0x00 start/end` + `MultiBlockChange 0x4E` coalescing per chunk-section `tryFlushAsMultiBlockChange` dedup last-wins; grouped by secti... |
| 76 | KeepAlive S→C `0x27` / C→S `0x1A`, `Cookie`, `ResourcePack` | DONE | **PARTIAL** | `GameServer.cpp:80` | `KeepAlive` 10s send `i64`, 30s timeout `Disconnect Timed out`, 60s idle sweep, `StoreCookie 0x0A/0x72` + `CookieRequest 0x00/0x16` with `world/data/cookies` persistence, `AddRe... |
| 77 | `DeclareCommands 0x11` | DONE | **PASS** | `Tree.hpp:193` `writeDeclareCommands` | Owned login required and received a non-empty `DeclareCommands 0x11` packet before command dispatch; packet presence is proven, while full tree parity remains a separate concern. |
| 78 | `LevelChunkWithLight 0x28` + `UpdateLight 0x2B` | DONE | **PASS** | `ChunkCodec.hpp:182` | `writePalettedContainer` longCount even for single palette, `biome` 40/desert 14 etc., `serializeUpdateLightBody`; current IDs are owned by SPEC_WIRE and the wire vectors. / |
| 79 | `BossBar 0x0A` / `Teams 0x67` | DONE | **PASS** | `Ids.hpp:129` + `Teams.hpp:1` + `Scoreboard.hpp:169` | `ScoreboardObjective 0x64`/`Score 0x68`/`Reset 0x49`/`Display 0x5C` + `BossBar 0x0A ADD/HEALTH/TITLE` (`BossAI` `wither/dragon`) + `Teams 0x67 create/remove/join` via `Commands.... |
| 80 | `DamageCalculator` + `AttributeManager` | DONE | **PARTIAL** | `Attributes.hpp:87` + `DamageSource.hpp:98` | `ARMOR/TOUGHNESS/KB_RESIST` sync `UpdateAttributes 0x7C` (`ARMOR` done) + `DamageCalculator::calculate` `applyArmorReduction` + `Protection EPF fire_protection/feather_falling` ... |
| 81 | Air/drown `airTicks 300` | DONE | **UNVERIFIED** | `GameServer.cpp:333` `survivalTick` | Head `y+1.62` water check `water[level=0]`, decrement 300→0, `drowningDamage` gamerule, `1 dmg/20t` `drown`, `WaterBreathing` exempt. / |
| 82 | Freeze `freezeTicks` powder snow | DONE | **UNVERIFIED** | `GameServer.cpp:333` | `powder_snow` foot block `freezeTicks` 0→300, `>=140 && freezeDamage && tick%20==0` `freeze` dmg, `freezeDamage` gamerule. / |
| 83 | Fire `fireTicks` lava | DONE | **PARTIAL** | `GameServer.cpp:333` | The remaining-entry fixture moved a survival player into a real fire block and required reduced `SetHealth 0x62` plus `DamageEvent 0x1A` targeting that player; lava, fire resistance, extinguishing, and gamerule permutations remain unverified. |
| 84 | Hunger `saturation/food/exhaustion` | DONE | **UNVERIFIED** | `GameServer.cpp:6173` + `HungerManager.cpp:1` | `exhaustion>=4→saturation/food--` + `food>=18 tick80 regen` + `food==0 starve` + `sprint 0.1/swim 0.01/jump 0.2/attack 0.3` exhaustion + `cake/stew` saturation `useFood` sync `S... |
| 85 | Fall `water/slime` mitigation | DONE | **UNVERIFIED** | `GameServer.cpp:3275` `onMovement` | Landing `fallDist` `>3` `floor(fallDist-3)` dmg, but if landing block `water/slime_block/honey_block/hay_block` or `powder_snow+SlowFalling` → `fallDist=0`, `fallDamage` gamerul... |
| 86 | Sneak pose `EntityAction 0x28` | DONE | **PASS** | `GameServer.cpp:3359` | The owned interaction fixture sent `EntityAction 0/1` and parsed `SetEntityMetadata 0x5D` pose index 6 values `5` then `0` plus flags index 0 values `0x02` then `0`. |
| 87 | PVP knockback `EntityVelocity 0x5F` | DONE | **PARTIAL** | `GameServer.cpp:3857` | The remaining-entry fixture attacked a real pig and required the target id across `HurtAnimation 0x25` and `EntityVelocity 0x5F`; player-victim, shield, armor, critical, and exact knockback-vector parity remain unverified. |
| 88 | Persistence `playerdata` `stats` `advancements` | DONE | **UNVERIFIED** | `GameServer.cpp:1389` `savePlayerNBT` | `playerdata/*.dat` `Position/Inventory/Health/Food`, `world/stats/*.json` `play_time`, `advancements` `cppfm:root→diamonds` 9 entries → **plan35 20 entries story 20** (`assets/d... |
| 89 | XP `SetExperience 0x61` | DONE | **PARTIAL** | `GameServer.cpp:372` `xpOrbsTick` | Live `/xp add @s 5 points` produced the `SetExperience` wire consequence; orb entity sizes, pickup, and kill drops remain unverified. |
| 90 | Effects `EntityEffect 0x7D` | DONE | **PARTIAL** | `MobEffects.hpp:14` + `GameServer.cpp:2265` | Live `/effect give @s speed 5` produced the entity-effect wire consequence; modifier values and metadata semantics remain unparsed. |

Summary: **PASS=13, PARTIAL=39, TODO=0, UNVERIFIED=38** (90 rows). The source summary remains `DONE=90, PARTIAL=0, TODO=0`; that taxonomy is not current coverage.

## Residual boundaries

| boundary | coverage | basis |
|---|---|---|
| Accepted 2-hour/24-hour soak | **TODO** | The requested 7200-second run stopped at `t=1200s` with server exit `-9`; see `goal-gui-soak.md` and the retained log. |
| Arbitrary Fabric JVM/GameProvider compatibility (E-14) | **PARTIAL** | Bounded shadow/fixture and selected mod probes pass; arbitrary Loader/GameProvider compatibility is excluded. |
| Vanilla world-generation L3 call-order/structure-NBT parity | **PARTIAL** | RNG primitives/splitters pass; full worldgen call order and structure NBT parity remain unproven. |
| Real-client GUI, first-login, retained release artifact | **PARTIAL** | Capability probe found no vanilla client/display; protocol evidence is retained but rendering/first-login/release-artifact coverage is absent. |
| Signed command argument transcript | **PARTIAL** | Enforced secure-chat path fails closed instead of dispatching unverified arguments. |
| Moving-piston transient NBT persistence | **PARTIAL** | Pending commits flush before snapshots; transient piston NBT is intentionally omitted. |
| Complete marker/comment inventory | **UNVERIFIED** | Legacy-reference grep was zero, but a complete inventory was not proven. |

## Evidence routing

- Matrix: `docs/MISSING_FEATURES_1_21_4.md`.
- Gate semantics/counts: `docs/VERIFICATION.md`.
- Publication/limitations: `docs/CURRENT_STATE.md` and `Handoff.md`.
- Protected bytes/hashes: `docs/audit/goal-protected-manifest.txt`.
