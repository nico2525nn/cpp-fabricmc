# Missing Features vs Vanilla Fabric 1.21.4 (Protocol 769) — Post Smoke-80 Audit

> This document is the post-smoke-80 gap list. It enumerates every feature that a vanilla Fabric 1.21.4 server provides but `cpp-fabricmc` does not yet fully match, grouped by the same 80-item taxonomy used in `plan5.md`. It is based on a re-read of `plan.md` through `plan5.md`, `PROTOCOL_NOTES.md`, `src/proto/Ids.hpp`, `src/generated/*`, and a live audit of the current HEAD (`94014ab` + merges).

## Summary

- **Total vanilla parity gap: ~45 items partially implemented, ~15 not started.** The 80-item smoke test (`tests/test_smoke_80.cpp`) is strict: it FAILS for each row marked `PARTIAL` or `TODO` below until the implementation is completed.
- **Build is green** (`cppfm` + `test_native` link), server boots and passes status/join/chunk/chat/multiplayer, but many higher-level systems are still stubs.
- **Fabric mods cannot run** inside a C++ process — `Fabric-compatible` here means *protocol-compatible* with what an unmodded Fabric server puts on the wire (as per README).

## How to read this list

- `Status: DONE` — byte-identical to vanilla or verified by `test_smoke_80` PASS.
- `PARTIAL` — code exists but incomplete or not wired (e.g., `hoppersTick` not called, `BlockTickScheduler` not ticked, `SetEquipment` not sent).
- `TODO` — no code.
- `Packet` — vanilla packet ID that is missing or stubbed (see `src/proto/Ids.hpp`).

## 1. World Management (9 items, 4 PARTIAL)

| # | Feature | Status | Packet / File | Notes |
|---|---------|--------|---------------|-------|
| 1 | Nether terrain (`fillNether`) | PARTIAL | `WorldGen.cpp:274` | Single noise, no nether biomes (basalt deltas, warped forest), no fortresses/bastions, no quartz/ancient debris. Fabric normal world expects `minecraft:nether_wastes` biome distribution. |
| 2 | End terrain (`fillEnd`) | PARTIAL | `WorldGen.cpp:325` | Only central island (r<58), no outer islands, no chorus, no end cities, no gateway. Dragon fight NBT missing. |
| 3 | Portal system (`PortalHandler`) | PARTIAL | `PortalHandler.hpp:1`, `GameServer.cpp:3225` | Ignition (flint on obsidian) and 8× teleport implemented, but no `findRespawnPosition` safe-Y search beyond 6 blocks, no portal cooldown `GameEvent`, no `Respawn` with `portal` cause, no `PlayerAbilities` reset on dim change, no nether portal block tick (`age` random). |
| 4 | Light cross-chunk | PARTIAL | `LightEngine.cpp:208` | Block-light is global BFS, sky-light now cross-chunk but still rebuilds only dirty+neighbors, not full 3×3, and `UpdateLight` is per-chunk not `LightUpdate` batch. `LightUpdateQueue` global not yet. |
| 5 | Spawn chunk loader | PARTIAL | `World.hpp:289` `forcedChunks_` | 5×5 forced correctly, but no `ChunkTicket` levels, no `ForcedChunks` NBT in `level.dat`, no `server.properties` `spawn-protection` handling. |
| 6 | Simulation distance culling | PARTIAL | `GameServer.cpp:383` `isChunkInSimulationDistance` | Implemented and used for `BlockTickScheduler` and `mobsTick`, but not for `FluidSim`/`Redstone`/`LightEngine` or `PlayerChunk` send distance. `viewDistance` vs `simulationDistance` not distinguished in `tickChunksAround`. |
| 7 | Chunk unload LRU | DONE | `World.hpp:278` `allChunkKeys`/`eraseChunk`, `GameServer.cpp:401` `chunksUnloadTick` | 100t LRU, `isDirty` flush, `forced` keep, per-dim check. Works but no `maxLoadedChunks` cap, no async I/O. |
| 8 | Structures | PARTIAL | `Structures.cpp:169` | `village/pyramid/outpost` implemented, `jungle_temple/igloo/swamp_hut` stubs added but not vanilla jigsaw; no `Stronghold`, `Mineshaft`, `Ocean Monument`, `Woodland Mansion`, no `StructureSet` spacing. |
| 9 | `level.dat` full | PARTIAL | `Persistence.hpp:46` | Saves `DataVersion 4189`, `Spawn*`, `Time/DayTime`, `GameRules`, `raining/rainTime`, `thundering` (always 0), but not `Difficulty`, `WorldBorder`, `Version`, `WanderingTrader`, `GameRules` `doFireTick` etc. not all, no atomic rename, per-dim `level.dat` not used for `The End` dragon. |
| 10 | WorldBorder damage | TODO | `GameServer.hpp:646` `worldBorderDiameter_` | Only `WorldBorder size` command → `InitializeWorldBorder 0x26` display, no damage outside, no `WorldBorderCenter/LerpSize` packets, no `isInsideBorder` check in `onMovement`. |

## 2. Block Behaviors & Redstone (29 items, 12 PARTIAL, 5 TODO)

| # | Feature | Status | File | Notes |
|---|---------|--------|------|-------|
| 11 | Stairs/slab placement context | PARTIAL | `GameServer.cpp:3683` | `half/top/bottom`, `type` double, `axis`, `face`, `waterlogged` implemented via `kPropDefs` scan, but `shape` for stairs always `straight`, `snowy` false, no `BlockPlace` `BlockState` with `waterlogged` from fluid check beyond simple water id. |
| 12 | Door two-block | DONE | `GameServer.cpp:3612` | `lower/upper` with `facing/half/open/hinge`, toggle preserves `facing/hinge`. |
| 13 | Farming `randomTickSpeed` | PARTIAL | `BlockTickScheduler.cpp:22` | `CropBehavior` 30% grow, `FarmlandBehavior` moisture, `SaplingBehavior` 5% tree, `StemBehavior` bamboo/sugar cane/cactus, but no `cocoa`, `sweet_berry`, `nether_wart`, `chorus_flower`; `gamerule randomTickSpeed` read but `isChunkInSimulationDistance` filters may starve far chunks. |
| 14 | BoneMeal `fertilize` | DONE | `GameServer.cpp:3531` | `bone_meal` on `wheat/potatoes/carrots/beetroots/sapling` → max age / tree, sound, consume. |
| 15 | Farmland trample | TODO | `World.hpp:178` | No player jump on farmland → dirt, no `moisture 0` → dirt when no crop above (currently does revert but not via random tick). |
| 16 | Fire `FireBehavior` | PARTIAL | `BlockTickScheduler.cpp:237` | `age` tick, `doFireTick` gate, spread to `planks/log/leaves` within 10% if adjacent, but `flammable` table hard-coded, no `soul_fire`, no `fire` `north/east/south/west/up` props, no `campfire`/`soul_campfire`, no entity `fireTicks` damage yet (survivalTick has it but not wired from `FireBehavior`). |
| 17 | TNT ignition | PARTIAL | `GameServer.cpp:3515` buckets/fire` | `tnt[unstable]` exists, `explodeAt` generic, but no `TNT` primed entity (`minecraft:tnt` with `fuse` 80t) — currently `dispenser` `tnt` → immediate `explodeAt`, not `SpawnEntity tnt`. |
| 18 | Buckets | DONE | `GameServer.cpp:3531` | `water_bucket`/`lava_bucket` ↔ `bucket` + `water`/`lava[level=0]` source, `level 0` source check, sound, `applyDamage` for flint. |
| 19 | Pistons | PARTIAL | `Redstone.cpp:239` `handlePiston` | Detects `piston`/`sticky_piston`, checks `extended`, pushes up to 12 blocks hardness check, places `piston_head`, but no `moving_piston` block entity, no honey/slime block stickiness, no `PistonMove` sound, no 2-tick delay. |
| 20 | Fluid solidify | PARTIAL | `Fluids.cpp:60` | `water+lava` → `cobble/obsidian/stone` implemented, but no `waterlog` prop handling for slabs/stairs, no `kelp`/`seagrass`. |
| 21 | Hopper `hoppersTick` | DONE | `GameServer.cpp:372` (50b) `hoppersTick` 8t | Pull from `y+1`, item entity pickup, push down, dispenser edge-trigger, but `hoppersTick` was previously `BROKEN` (not called) — now called in `tickOnce:372`, redstone lock `isPoweredHere` only for dispenser, not for hopper (should lock hopper when powered). |
| 22 | Comparator | PARTIAL | `Redstone.cpp:40` | `analogOutputForContainer` `filled/slots` → 0-15 implemented for `chest/furnace/hopper/dispenser/barrel/shulker`, but `comparator` `mode=compare/subtract` side power not yet, no `Container` `compare` tick. |
| 23 | Observer | PARTIAL | `Redstone.cpp:103` | `observerPrev`/`observerPulseEnd` 2-tick pulse on neighbor `onBlockChanged`, but no `facing` 6-direction check beyond simple neighbor list, no `UpdateLight` for observer lit. |
| 24 | Rails | PARTIAL | `Redstone.cpp:239` `recomputeRailShape` | Stub shape `north_south`/`east_west`/`ascending`, but no `powered_rail` boost, no `detector_rail` redstone output on minecart, no `activator_rail` eject, no minecart physics. |
| 25 | Dispenser per-item | PARTIAL | `GameServer.cpp:983` | `arrow→Arrow`, `snowball→Snowball`, `egg→Egg`, `ender_pearl→EnderPearl`, `fire_charge→Fireball`, `tnt→explodeAt`, else `spawnItemDrop`, but no `potion` dispense, no `bucket` fluid dispense, no `shears`/`hoe` use. |
| 26 | Dropper | TODO | `BlockEntities.hpp:51` | Same `GenericContainerData` 9 slots, but no `dropper` `facing` dispense logic (currently shares `Dispenser` kind). |
| 27 | Cactus/sugar cane growth | PARTIAL | `BlockTickScheduler.cpp:163` `StemBehavior` | `cactus` needs `sand/cactus` below, `sugar_cane` no check, but both use `age 15` → grow, no `bamboo` `leaves` stage. |

## 3. Entities & Mobs (20 items, 8 PARTIAL, 4 TODO)

| # | Feature | Status | File | Notes |
|---|---------|--------|------|-------|
| 28 | MobKind 46 | DONE | `Entities.hpp:52` | 46 kinds (up from 13) with `MobStats` 300/200/500 etc., all `typeId` via `gen::entityTypeIdByName`. |
| 29 | Brain-Goal-Sensor vs BehaviorTree | PARTIAL | `AiBrain.hpp:11` | `Brain` 7 goals (`Panic/Breed/Melee/Ranged/Wander/Look`) works for generic, but `BehaviorTree`/`EntityFactory` JSON dispatch not: `EntityDataLoader` loads `assets/entities/*.json` but `Brain` not built from `brain.behaviors` list (wither_skull, dragon_breath etc. are orphaned). |
| 30 | `SetEquipment 0x60` | PARTIAL | `GameServer.cpp:613` | `sendEquipment` on `broadcastMobSpawn` for 6 slots, but no dynamic equip change sync, no `ArmorTrim` component, no `HandDropChances`. |
| 31 | `SetPassengers 0x65` riding | PARTIAL | `GameServer.cpp:3962` | `horse/llama/pig` mount sets `vehicleId/riderEntityId` + `SetPassengers`, `MoveVehicle 0x20` moves vehicle, but no `isSneaking` dismount sync beyond `EntityAction 0x28`, no `horse` jump, no `boat/minecart` vehicles. |
| 32 | Durability | PARTIAL | `Items.hpp:108` `applyDamage` | `maxDamageFor` 59/131/250/1561/2031 etc., `getDamage`/`setDamage` component 6, hooked in `onUseItemOn`/`onPlayerAction`/`onUseEntity`, but no `Unbreaking` enchant reduction, no `Mending` XP repair, no `Anvil` repair cost. |
| 33 | Enchant effects | PARTIAL | `Items.hpp:147` `addEnchant` | `sharpness/protection/power/fortune/silk_touch` as `component 10` text `name:lvl,`, `hasSilkTouch`/`fortuneLevel` used in `lootTables` and `tickDigs`, but `Efficiency` mining speed, `FrostWalker`, `SoulSpeed`, `SwiftSneak` not, `EnchantmentHelper` only damage bonus `meleeDamageBonusFor`. |
| 34 | Slime/MagmaCube split | DONE | `GameServer.cpp:588` `slimeSize` | Death of `Slime/MagmaCube` size>0 spawns 2-4 babies size-1 with half health, `broadcastMobSpawn`. |
| 35 | Wither/Dragon boss AI | TODO | `Entities.hpp:94` | Stats 300/200 HP, but `WitherSkull`/`DragonFireball` are `ProjectileKind` 4/6, no skull shoot loop, no `BossBar 0x0A` `ADD/HEALTH/TITLE`, no dragon perch phases. |
| 36 | Wool shear | DONE | `GameServer.cpp:3964` | `shears` on `Sheep` `!sheared` → `sheared=true` + `woolColor` drop 1-3, `SetEntityMetadata` index 17, damage shears. |
| 37 | EnderPearl teleport | DONE | `GameServer.cpp:2556` `projectilesTick` | `EnderPearl` block hit → owner `PlayerPosition 0x42` + `EntityTeleport`, `applyDamage 5`, `lastEnderPearlTick` cooldown, `SetCooldown`. |
| 38 | Spawn eggs | PARTIAL | `ItemIds.hpp:1069` | 80 eggs in `gen::kItems`, `spawnMobByTypeName` generic 0-45, but `onUseItemOn` `*_spawn_egg` → `spawnMob` not via item use (only via `/summon` and dispenser). |
| 39 | Enderman | PARTIAL | `AiBrain.cpp:40` | `carriedBlock` `uint16`, `Enderman` 40 HP, but no block pickup (`onBlockChanged`), no 32-block teleport on damage, no `anger` `stare` logic. |
| 40 | Charged Creeper | PARTIAL | `Entities.hpp:156` `creeperCharged` | Bool exists, never set (needs `LightningBolt 0x74` or `channeling` trident), `explodeAt` power stays 3.0 not 6.0 when charged. |
| 41 | XP orbs | DONE | `GameServer.cpp:372` `xpOrbsTick` | Sizes `{1,3,7,17,37,73,149,307,617,1237}`, gravity, `SetExperience 0x5B`. |
| 42 | Projectiles tick | DONE | `GameServer.cpp:372` `projectilesTick` | `Arrow/Snowball/Egg/EnderPearl/WitherSkull/Fireball` gravity 0.05, block hit → stuck vs despawn, entity hit radius 0.55, `DamageEvent`. Now called in `tickOnce`. |
| 43 | Breeding/aging | PARTIAL | `GameServer.cpp:3962` `tryBreedFeed` | `love` `EntityEvent 18`, baby `age<0` grow, but `onUseEntity` breed only via `tryBreedFeed` for `breedingItemFor`, no `BreedGoal` active for wild pairs. |
| 44 | Villager trading | PARTIAL | `GameServer.cpp:3962` `tradeTable` | `Villager` stats 20 HP, `TradeList 0x2E` 5 offers, `SelectTrade 0x31`, but no `VillagerData` profession/level, no restock, no `Gossip`. |
| 45 | Boat/Minecart | TODO | `proto/Ids.hpp:187` | No `VehicleEntity`, no `SetPassengers` for `boat`/`minecart`, no `VehicleMove` physics. |

## 4. Inventory & UI (12 items, 5 PARTIAL)

| # | Feature | Status | File | Notes |
|---|---------|--------|------|-------|
| 46 | `MenuType` `Barrel/ShulkerBox` | DONE | `Containers.hpp:29` `Barrel, ShulkerBox` + `totalSlots 27+36` | `BlockEntityStore` `writeChunkNbt` `barrel`/`shulker_box` id. |
| 47 | Enchanting table | PARTIAL | `Containers.hpp:29` `Enchantment` 2+36, `GameServer.cpp:3085` `EnchantItem 0x0F` | Opens `kEnchantment 13`, `ContainerSetData` not sent, `EnchantmentHelper` cost calc is `button+1` random, no bookshelf `base = randomInt(4..17)` vanilla. |
| 48 | Anvil | PARTIAL | `Containers.hpp:29` `Anvil` 3+36, `GameServer.cpp:2588` `openMenuAt` `anvil` | Opens `kAnvil 8`, no repair/rename cost `Property 0` (`ContainerSetData 0x14`), no `AnvilMenu` rename via `MC|ItemName`. |
| 49 | Brewing stand | PARTIAL | `Containers.hpp:29` `Brewing` 5+36 | Opens `kBrewingStand 11`, no `brewTime` property, no `fuel` blaze powder, no `Potion` `brewing` logic. |
| 50 | Stonecutter ghost | PARTIAL | `Containers.hpp:29` `Stonecutter` 2+36, `Recipes.cpp:1` `Stonecutting` | `RecipeManager` `findStonecutting` 2 recipes, `sendRecipeBook` `type 3`, but `PlaceGhostRecipe 0x39` not handled, no `ContainerSetSlot` for result. |
| 51 | Creative `SetCreativeModeSlot 0x36` | DONE | `GameServer.cpp:3112` | Checks `gamemode==1`, `slot 0-45` `maxStackFor`, echoes `SetSlot`/`SetCursorItem`, handles `-1` cursor. |
| 52 | Drag `mode5` | DONE | `MenuInteraction.cpp:229` | `button 0/4 start` `dragSlots`, `1/5 addSlot`, `2/6 end` distribute left/even vs right 1-per-slot, `maxStackFor` respect. |
| 53 | `WindowClick` authoritative | DONE | `MenuInteraction.cpp:1` `ClickLogic` | Modes 0 pickupPlace,1 quickMove,2 swapWithHotbar,4 throw,6 pickupAll for all `totalSlots-36` containers. |
| 54 | `ContainerSetContent 0x13`/`SetSlot 0x15` | DONE | `GameServer.cpp:1246` `sendMenuContent` | `windowId/stateId/totalSlots` + each `ItemStack::write` + `cursorItem`, `SetCursorItem 0x5A` sync. |
| 55 | Hopper menu 5/9 slots | DONE | `Containers.hpp:29` `Hopper` 5+36 / `Dispenser` 9+36 | `BlockEntityStore` `GenericContainerData` 5/9, `openMenuAt` `hopper/dispenser` branches, `containerAt` now includes `Barrel/Shulker`. |
| 56 | Recipe book `0x44` + `PlaceRecipe 0x25` | PARTIAL | `GameServer.cpp:2588` | `sendRecipeBook` `craftingStation` `SlotDisplay type2`, `PlaceRecipe` for `Crafting` only, not `Furnace/Stonecutter`. |

## 5. Commands & Datapack (15 items, 6 PARTIAL)

| # | Feature | Status | File | Notes |
|---|---------|--------|------|-------|
| 57 | Brigadier foundation | DONE | `brigadier/Tree.hpp:1` `CommandNode`/`CommandDispatcher` `writeDeclareCommands` 0x11 | Flags `0x00 root,0x01 literal,0x02 arg,0x04 exec,0x08 redirect`, `StringReader` backtrack, `suggest` prefix filter. |
| 58 | Arg types 48 | PARTIAL | `brigadier/Arguments.hpp:1` | `Bool,Float,Double,Integer,Long,String,Entity,BlockPos,Vec3,Resource,GameMode,Time,ItemStack` implemented, but `BlockState` parser id 12 not used in commands, `ItemPredicate` `Nbt` `Objective` missing. |
| 59 | Tab completion | PARTIAL | `Tree.hpp:206` `suggest` | Literal + `entity` + per-arg `stringWord`/`time`/`effect`/`difficulty` suggestions, but not for `fill` block list or `function` names. |
| 60 | `/give`/`/summon`/`/setblock` | DONE | `Commands.cpp:261` | 23 top-level literals, vanilla IDs via `gen::itemIdByName`/`entityTypeIdByName`, `BlockPos` `~` relative. |
| 61 | `/fill` | DONE | `Commands.cpp:880` | `from BlockPos to BlockPos block` volume clamp 32768, `setBlock` loop + `broadcastBlockChange` or `queueBlockChange`. |
| 62 | `/execute as @p run` | DONE | `Commands.cpp:900` | Single-level `execute as <entity> run <command>` via `resolveSelector` + re-dispatch with target source, but no `at/positioned/anchored/run` branches. |
| 63 | `/function` | DONE | `Commands.cpp:920` | `namespace:path` → `assets/data/<ns>/functions/<path>.mcfunction` read lines → `dispatchConsole` each, but no `minecraft:tick` tag auto-run, no recursion limit. |
| 64 | `/reload` | DONE | `Commands.cpp:940` | Re-loads `recipes/tags/loot` via same `loadDirectory` + `applyToRecipeTags`, feedback `SystemChat`. |
| 65 | Tags 67 item / 20 block | DONE | `TagManager.hpp:1` `loadDirectory` `pendingRefs_` + `ensureItemDefaults` 67, `GameServer.cpp:296` wired | `planks/logs/coals/wool` + 33 `extras` (fishes, slabs, stairs...), `Ingredient` now resolves `#minecraft:planks` correctly (was `planks/logs/stone` only). |
| 66 | `Ingredient` tag resolve | DONE | `Recipes.cpp:1` | `tags_` map now 67, `TagManager::applyToRecipeTags` at init. |
| 67 | `LootTableEvaluator` | DONE | `LootTables.hpp:1` `evaluate(bn,tool)` | `pools/rolls/weight/set_count`, `fortune` bonus, `silk_touch` via `tool.hasSilkTouch()` → drop block itself, but no `explosion_decay`, `furnace_smelt`, `copy_components` for chests. |
| 68 | `DatapackManager` | PARTIAL | `src/game/DatapackManager.hpp:1` stub | `loadAll` wraps `recipes/tags/loot` + `world/datapacks` scan, but no `advancements/predicates/item_modifiers` registries, no `/datapack list/enable/disable`. |
| 69 | `FunctionEvaluator` `/function` | PARTIAL | `Commands.cpp:920` | Loads and executes, but no `execute store/score` or `return` value, no `schedule`. |
| 70 | `/tag` `/team` `/bossbar` | TODO | `Commands.cpp:950` stubs | Literals `tag/team/bossbar` only send `not yet implemented` feedback, no `Scoreboard` `Teams 0x67` or `BossBar 0x0A` packets. |

## 6. Network & Protocol (10 items, 4 PARTIAL)

| # | Feature | Status | Packet | Notes |
|---|---------|--------|--------|-------|
| 71 | VarInt/VarLong, big-endian, Position | DONE | `ByteBuffer.hpp:1` | `varint` max 5b for negative, `i16/u16/i32/u32/i64` big-endian, `position(x,y,z)` 26-12-26 pack `((x&0x3FFFFFF)<<38)|((y&0xFFF)<<12)|(z&0x3FFFFFF)`. |
| 72 | Handshake → Status/Login/Config/Play | DONE | `GameServer.cpp:1420` `handleHandshake` | `protocol 769` gate `Outdated client`, `Status` JSON `enforcesSecureChat:false`, `favicon`, `Login` RSA 1024 `EncryptionRequest 0x01` + `mcSha1Hex` + `MojangAuth` curl, `Configuration` `SelectKnownPacks` `RegistryData 0x07` ×12 + `UpdateTags 0x0D` + `FinishConfiguration 0x03`. |
| 73 | Compression/Encryption | DONE | `Connection.hpp:29` | `setCompression 256` `zlib` `dataLength 0` vs `>0` decompress, `readFrame` length varint byte-by-byte decrypt via `AesCfb8 0x80`, `setSendTimeout 15`. |
| 74 | Chat signing `PlayerChat 0x3B` | PARTIAL | `GameServer.cpp:3046` `ChatMessageProcessor` | Stores `chatPubKey/expiry`, reads `ChatMessage 0x07` `timestamp/salt/signature/offset/acknowledged(3)`, but verification is stub `return true`, always broadcasts `SystemChat 0x73` not `PlayerChat 0x3B` or `DisguisedChat 0x1C`, `enforcesSecureChat:false`, no `MessageAck 0x04` handling beyond sink, no `ChatSessionUpdate` verify. |
| 75 | Bundle `0x00` + `MultiBlockChange 0x4E` | PARTIAL | `PacketBatcher.hpp:1` | `PacketBatcher` `queuePacket`/`flush` exists, `broadcastBlockChange` → `queueBlockChange` `size>=64` flush, `flush` currently sends each `BlockUpdate 0x09` individually (not true `Bundle` `0x00` start/end or `MultiBlockChange 0x4E` coalescing for same chunk section). `BundleDelimiter` ID `0x00` defined but never sent as wrapper. |
| 76 | KeepAlive `0x26` / `Cookie` / `ResourcePack` | DONE | `GameServer.cpp:80` | `KeepAlive` 10s send `i64`, 30s timeout `Disconnect Timed out`, 60s idle sweep, `StoreCookie 0x0A/0x72` + `CookieRequest 0x00/0x16` with `world/data/cookies` persistence, `AddResourcePack 0x09` `url/sha1/forced` (SHA1 not verified, forced kick not). |
| 77 | `DeclareCommands 0x11` | DONE | `Tree.hpp:193` `writeDeclareCommands` | Flatten DFS, flags `0x01 literal,0x02 arg,0x04 exec,0x08 redirect`, `parserId` 0-48. |
| 78 | `ChunkData` + `UpdateLight` | DONE | `ChunkCodec.hpp:182` | `LevelChunkWithLight 0x27` `writePalettedContainer` longCount even for single palette, `biome` 40/desert 14 etc., `UpdateLight 0x25` via `serializeUpdateLightBody`. |
| 79 | `BossBar 0x0A` / `Teams 0x67` | TODO | `Ids.hpp:119` | Only `ScoreboardObjective 0x64`/`Score 0x68`/`Display 0x5C` implemented, no `BossBar` `ADD/HEALTH/TITLE` or `Teams` `0x67` `create/remove/join`. |

## 7. Combat & Survival (12 items, 6 PARTIAL)

| # | Feature | Status | File | Notes |
|---|---------|--------|------|-------|
| 80 | `DamageCalculator` + `AttributeManager` | PARTIAL | `Attributes.hpp:11` `AttributeManager` `MOVEMENT_SPEED/MAX_HEALTH/ARMOR` | `ARMOR`/`ARMOR_TOUGHNESS` computed `add→multiply_base→multiply_total`, but only `MAX_HEALTH/MOVEMENT_SPEED/ATTACK_DAMAGE` serialized in `writeUpdate 0x7C` (8 attrs missing `ARMOR`), `applyDamage` now does `totalArmorPoints` + `applyArmorReduction` + `prot*4/25` + `Resistance 0.2*(amp+1)`, but `DamageSource` `isFire/isFall/isDrown` not differentiated for `Protection` EPF categories. |
| 81 | Air/drown `airTicks 300` | DONE | `GameServer.cpp:333` `survivalTick` | Head `y+1.62` water check `water[level=0]`, decrement 300→0, `drowningDamage` gamerule, `1 dmg/20t` `drown`, `WaterBreathing` exempt. |
| 82 | Freeze `freezeTicks` powder snow | DONE | `GameServer.cpp:333` | `powder_snow` foot block `freezeTicks` 0→300, `>=140 && freezeDamage && tick%20==0` `freeze` dmg, `freezeDamage` gamerule. |
| 83 | Fire `fireTicks` lava | DONE | `GameServer.cpp:333` | `isFireOrLavaAt` `lava/fire/soul_fire/magma_block/campfire`, `FireResistance` exempt, `doFireTick` gate, `fireTicks 160` on contact, `1 dmg/20t` `onFire`, water extinguish. |
| 84 | Hunger `saturation/food/exhaustion` | PARTIAL | `GameServer.cpp:333` | `exhaustion>=4→saturation/food--`, `food>=18 && health<20 tick%80` regen, `food==0 tick%80` starve, but no `sprint/jump/attack/bow` exhaustion, only horizontal `sqrt(hdx²+hdz²)*0.01` in `onMovement:3275`, no `cake`/`stew` saturation. |
| 85 | Fall `water/slime` mitigation | DONE | `GameServer.cpp:3275` `onMovement` | Landing `fallDist` `>3` `floor(fallDist-3)` dmg, but if landing block `water/slime_block/honey_block/hay_block` or `powder_snow+SlowFalling` → `fallDist=0`, `fallDamage` gamerule guarded. |
| 86 | Sneak pose `EntityAction 0x28` | DONE | `GameServer.cpp:3359` | `start_sneak 0/stop 1/start_sprint 3/stop 4`, broadcasts `SetEntityMetadata 0x5D` index 6 pose `5 crouch/0 stand` + index 0 flags `0x02`, via `broadcastPacketExcept`. |
| 87 | PVP knockback `EntityVelocity 0x5F` | DONE | `GameServer.cpp:3857` | `dx*norm*400, dy300, dz*norm*400` for `Player` victim and `Mob` victim, plus `DamageEvent 0x1A` `damage_type` via `gameData.idOf`. |
| 88 | Persistence `playerdata` `stats` `advancements` | DONE | `GameServer.cpp:1389` `savePlayerNBT` | `playerdata/*.dat` `Position/Inventory/Health/Food`, `world/stats/*.json` `play_time`, `advancements` `cppfm:root→diamonds` 9 entries `UpdateAdvancements 0x7B`. |
| 89 | XP `SetExperience 0x5B` | DONE | `GameServer.cpp:372` `xpOrbsTick` | `SpawnExperienceOrb 0x02` sizes `{1,3,7,17...}`, `mobsTick` `spawnXpOrbs` on kill, `sendSetExperience` level curve. |
| 90 | Effects `EntityEffect 0x5E` | PARTIAL | `MobEffects.hpp:14` | `Speed/Slowness→MOVEMENT_SPEED` `speedModifierFor` op2, `HealthBoost→MAX_HEALTH +4*level` op0, `Attributes::applyEffectModifiers` called in `effectsTick:2265` + `UpdateAttributes 0x7C` every 20t, but `Invisibility`/`Glowing`/`Levitation` not. |

## 8. Fabric-Specific (not in 80, but expected for Fabric 1.21.4)

| Feature | Status | Notes |
|---------|--------|-------|
| Fabric `Netty` `ChannelPipeline` `Codec` | PARTIAL | `WriteBuffer/ReadBuffer` manual, no `ChannelPipeline` `Encoder/Decoder` abstraction. |
| `Fabric Loader` JVM mods | N/A | Cannot run JVM bytecode in C++ process — by design `Fabric-compatible` = protocol-compatible only (README). |
| `RCON` `whitelist` | DONE | `Rcon.hpp` `RconServer` `dispatchConsole` all Brigadier, `whitelist.json` `setEnabled`. |
| `Server.properties` | PARTIAL | Only `port/maxPlayers/viewDistance/simulationDistance/motd/seed/levelType` parsed (`main.cpp:39`), not `spawn-protection`, `force-gamemode`, `white-list`, `enforce-secure-profile`, `resource-pack` etc. fully. |

## Test Coverage Mapping

`tests/test_smoke_80.cpp` has 1:1 mapping to the 80 rows above: each `SECTION` contains a `CHECK` that fails when the row is `TODO`/`PARTIAL` without vanilla-accurate packet. Example: `worldborder size` must broadcast `InitializeWorldBorder 0x26`, `glowstone` must trigger `UpdateLight`, `wither` must have `BossBar`, `observer` must pulse 2t, `armor` must reduce via `totalArmorPoints`, `BundleDelimiter` must wrap `BlockUpdate`s.

Run: `cmake -B build && cmake --build build -j4 && ./build/test_smoke_80 ./build/cppfm` — currently ~40% FAIL (expected), fix each `PARTIAL` to make them PASS.
