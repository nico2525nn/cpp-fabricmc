# MISSING_FEATURES_1_21_4 — stable numbered gap/status matrix

> This is the stable matrix for Minecraft Java 1.21.4 (protocol 769). It preserves
> the original taxonomy IDs without renumbering: base rows **#1–#80** and extension
> rows **#81–#90**. The restored matrix is a historical taxonomy baseline; it is not
> a release gate and does not make the current cleanup or operations audit green.
>
> Current implementation/evidence baseline: integrated runtime snapshot
> `17ab09f5220bf99203d2aea2b2c9d65f763f433b`.

## Summary

- `base_rows: 80` (`#1–#80`); `extension_rows: 10` (`#81–#90`); `numbered_rows: 90`.
- `historical_matrix_status_counts: DONE=90, PARTIAL=0, TODO=0` at this taxonomy
  granularity. These values describe the archived gap classification, not universal
  vanilla internals, current long-run reliability, or Fabric JVM-mod support.
- `strict_assessment_1_gap_count: 78` is a separate historical audit label. Its
  archive result is not a current aggregate, and it must not be added to or
  substituted for the 90-row taxonomy count.
- Current publication state is `BLOCKED` only by declared boundaries: three integrated
  `tools/soak_bot.py --duration 300` runs passed, while the attempted 7200-second soak
  was interrupted above its RSS gate; accepted 2-hour/24-hour and current real-client
  evidence remain absent. `test_gameplay_full` retains its one intentional
  E-14 expected failure. See [CURRENT_STATE.md](CURRENT_STATE.md) and
  [VERIFICATION.md](VERIFICATION.md) for gate semantics.

## Machine-readable status rules

Every numbered table row has one ASCII `Status` token; compound values such as
`DONE+hardened` are not used.

| token | meaning in this matrix |
|---|---|
| `DONE` | the feature was closed in the historical numbered taxonomy (#1–#80 plus #81–#90); this is not a universal parity claim |
| `PARTIAL` | implementation exists but the numbered feature remains incomplete |
| `TODO` | the numbered feature was not started |
| `DECLARED-LIMITATION` | intentionally unsupported or not independently verified; not counted as `DONE` |
| `HISTORICAL` | retained evidence/context only; not a current implementation result |

The numbered rows retain the source/file and notes from the original matrix. The
separate limitation sections below are deliberately not folded into a `DONE` row.
Canonical behavior, wire ownership, and evidence interpretation live in
[SPEC_GAMEPLAY.md](SPEC_GAMEPLAY.md), [SPEC_WIRE.md](SPEC_WIRE.md),
[SPEC_OPS.md](SPEC_OPS.md), and [VERIFICATION.md](VERIFICATION.md).

## Base taxonomy #1–#10: World Management (10 rows)

| # | Feature | Status | Packet / File | Notes |
|---|---------|--------|---------------|-------|
| 1 | Nether terrain (`fillNether`) | DONE | `World.cpp:314` + `StructureManager.cpp:62` | **plan12 §1 DONE:** 5 biomes via 4 `ImprovedNoise` (`nether_wastes 36.3%/crimson 22.2%/soul_sand_valley 17.1%/basalt_deltas 15.9%/warped 8.5%`), bedrock roof/floor `0-4/123-127` 50%, `ancient_debris` Y8-119 2 veins, `quartz` 12 uniform, `nether_gold/magma/glowstone` blobs, bastion/fortress via `StructureSet`. |
| 2 | End terrain (`fillEnd`) | DONE | `World.cpp:546` | **plan12 §2 DONE:** outer islands `1/14 +1/4 duplicate` `sin(r/80)` ring `r>1000`, `Highlands/Midlands/Barrens/SmallIslands` biomes, chorus `0-2/chunk Y65-75`, `EndCity spacing 20 sep 11` 9×9 tower `purpur/end_bricks/chest`, 12 `end_gateway` ring R96 Y75 + outer return gateway. |
| 3 | Portal system (`PortalHandler`) | DONE | `PortalHandler.hpp:81`, `GameServer.cpp:6229` | 8× teleport + `findSafeSpawn` 6 up/down + `PortalAge` `BlockTickScheduler` + `Respawn 0x4C` per-dim + `PlayerAbilities` reset done; polish: `GameEvent` cooldown 90t `portalCooldownUntilTick` present, `age` random tick implemented. |
| 4 | Light cross-chunk | DONE | `LightEngine.cpp:81` + `LightEngine.hpp:20` | **DONE:** block-light global BFS + sky-light BFS with `LightUpdateQueue`, `drain()` expands `dirtyChunks` to 3×3 (182-191), per-chunk `UpdateLight 0x2B` via `serializeUpdateLightBody`; see `PROTOCOL_NOTES.md` empirical §. |
| 5 | Spawn chunk loader | DONE | `World.hpp:347` `ChunkTicket.hpp:1` | 5×5 `ForcedChunks` with `ChunkTicketType::SPAWN level 31` + `level.dat: ForcedChunks long[]` + `server.properties spawn-protection 16` + `isSpawnProtected` gate + `/forceload` support. |
| 6 | Simulation distance culling | DONE | `World.hpp:422` `isChunkInSimulationDistance` | `viewDistance` vs `simulationDistance` distinguished; `Fluids.cpp:77`, `Redstone.cpp:370`, `LightEngine.cpp:46`, `BlockTickScheduler.cpp:59` all gated by `isChunkInSimulationDistance`; `PlayerChunk` send uses `shouldSend` (view), tick uses `shouldTick` (sim). |
| 7 | Chunk unload LRU | DONE | `World.hpp::allChunkKeys/eraseChunk`, `GameServer_tick.cpp::chunksUnloadTick`, `GameServer_world.cpp::saveChunkAsync` | 100t LRU, dirty flush, forced/spawn-ticket protection, per-dimension checks, `maxLoadedChunks` cap, and Chebyshev-distance eviction. Async save snapshots NBT/wire data without copying a full `Chunk`; the post-fix wide soak is recorded separately. |
| 8 | Structures | DONE | `StructureManager.cpp:66` + `StructurePlacer.cpp:18` + `WorldGen.cpp:fillTerrainV3` | **plan12 §3 DONE; plan29 §1/§2 polish DONE; plan32 world DONE:** `StructureSet {spacing,separation,salt,Linear/Triangular}` — `stronghold 32/5`, `mineshaft 10/5`, `monument 32/5 salt 10387313`, `mansion 80/20 salt 10387319`, `end_city 20/11`; template pieces `portal_room/corridor/prismarine 58×58×23/mansion 40×40`; **plan29 §1:** `trial_chambers` `spacing 34 separation 12 salt 942731826` (8→12 fix) + jigsaw fallback 12 variants (corridor/end, straight slices 10+8, chamber 18/14/22, intersection 8×8, atrium 13×13, hallway, trial_spawner/vault/dispenser/copper) + `deep_dark` origin reject (biome gate); **plan29 §2:** `Pale Garden` decoration in `fillTerrainV3` — `pale_oak` 2-4/chunk (20% `creaking_heart` in trunk), `pale_hanging_moss` under leaves, 5×5 `pale_moss_block/carpet` patches + 5% `eyeblossom`; **plan32 world:** `/locate structure/biome/poi` + `/place feature/structure/jigsaw` + `/spreadplayers` (Commands.cpp:3243 locate via `StructureManager tmpMgr(seed)` + biome gate); **plan33 scheduled: 10→20 sets (spacing/salt parity), triangular/concentric, max_distance_from_center footprint, /locate 20-set** (see `plan/plan33.md`). MultiNoise climate Yarn-identical (no change, plan33 §3 isosceles). |
| 9 | `level.dat` full | DONE | `WorldDataManager.hpp:26` + `Persistence.hpp:69` | `DataVersion 4189` + `Difficulty` + `WorldBorder` + `Version` + `ForcedChunks` atomic `rename`, per-dim `DIM-1/DIM1 level.dat` `dragonFight` NBT `Gateways` 12. |
| 10 | WorldBorder damage | DONE | `GameServer.hpp:852` `isInsideBorder` | `InitializeWorldBorder 0x26` + `WorldBorderCenter/LerpSize`, `isInsideBorder` check in `onMovement` + per-tick `damageOutsideBorder` 0.2/half-sec outside `diameter*0.5`, `damageAmount` `Buffer`. |

## Base taxonomy #11–#27: Block Behaviors & Redstone (17 rows)

| # | Feature | Status | File | Notes |
|---|---------|--------|------|-------|
| 11 | Stairs/slab placement context | DONE | `GameServer.cpp:6944` + `BlockTickScheduler.hpp:190` | **plan12 §4 DONE:** `ItemUseContext` + `FluidSim::getFluidState().isWater()` → `waterlogged`, `computeStairsShape` 5-value `inner_left/right/outer_left/right`, `half` via `face` + `hitY>0.5`, slab `double` + `waterlogged=false`, `snowy` via above `snow`, neighbor `shape` update, `scheduleFluidTick` if waterlogged. |
| 12 | Door two-block | DONE | `GameServer.cpp:3612` | `lower/upper` with `facing/half/open/hinge`, toggle preserves `facing/hinge`. |
| 13 | Farming `randomTickSpeed` | DONE | `BlockTickScheduler.cpp:48` | **plan12 §5 DONE:** `randomTickSpeed 0` gate + `isChunkInSimulationDistance` + `light>=9` + `growthSpeed 1-3×` + `Cocoa age2 jungle_log 1/5` + `SweetBerry 1/3→1/2` + `NetherWart soul_sand 1/10` + `ChorusFlower age<5 1/5` + `Kelp age25 1/10`; registers in `GameServer.hpp:353`. |
| 14 | BoneMeal `fertilize` | DONE | `GameServer.cpp:3531` | `bone_meal` on `wheat/potatoes/carrots/beetroots/sapling` → max age / tree, sound, consume. |
| 15 | Farmland trample | DONE | `GameServer.cpp:6031` + `BlockTickScheduler.cpp:306` | **plan12 §6 DONE:** `fallDistance>0.5` `prob=fallDist-0.5` `!isSneaking` + `mobGriefing` gate, `LevelEvent 2001`, `moisture 0-7` + `isNearWater 9×9×2` scan, `moisture==0 && !hasCrop && !hasWater → dirt` via `randomTick`. |
| 16 | Fire `FireBehavior` | DONE | `BlockTickScheduler.cpp:543` | **plan12 §7 DONE:** `FlammableRegistry` `{planks 5/20, leaves 30/60…}` + `SoulFire` `soul_sand/soil` only + `Campfire lit` gate + `fire shape north/south/east/west/up` via `isFlammableAt`; polish: tag-driven `minecraft:soul_fire_base_blocks` not yet datapack. |
| 17 | TNT ignition | DONE | `Entities.hpp:67` + `GameServer.cpp:587` | `TntEntity fuse 80` `primedTntsTick` + `SpawnEntity 0x02` `minecraft:tnt`, `dispenser tnt → primed` + `flint_and_steel` ignite `tnt[unstable]`. |
| 18 | Buckets | DONE | `GameServer.cpp:3531` | `water_bucket`/`lava_bucket` ↔ `bucket` + `water`/`lava[level=0]` source, `level 0` source check, sound, `applyDamage` for flint. |
| 19 | Pistons | DONE | `Redstone.cpp:597` | `MovingPiston` 2-tick + `isStickyBlock` + `sticksTogether slime≠honey` + 12-block BFS + `isUnpushable` + `PistonMove` sound; **plan29 §9 verified:** existing BFS 6-dir / 12-block limit / `sticksTogether slime≠honey` is plan29 §9-compliant — no change required (multi-block sticky retract already compliant). |
| 20 | Fluid solidify | DONE | `Fluids.cpp:13` + `BlockTickScheduler.cpp:453` + `GameServer.hpp:386` | **plan12 §8 DONE; plan29 §10 polish DONE:** `water+lava→cobble/obsidian/stone` + `WaterloggableHelper` `stairs/slab/fence/wall/trapdoor` (double slab `!waterloggable`) + `kindAt` waterlogged→`Water 0` + `KelpBehavior age25 10%` + `seagrass` fallback; **plan29 §10:** `registerBehavior("minecraft:seagrass", SeagrassBehavior)` (`GameServer.hpp:386`) enables `tall_seagrass` bonemeal path; `soul_fire` tag-driven already FIXED; boat buoyancy / ghost preview throttle unchanged (verified as-is). |
| 21 | Hopper `hoppersTick` | DONE | `GameServer.cpp:372` (50b) `hoppersTick` 8t | Pull from `y+1`, item entity pickup, push down, edge-trigger, redstone lock `isPoweredHere` for hopper + dispenser. |
| 22 | Comparator | DONE | `Redstone.cpp:270` | `mode compare/subtract` + side power `max(0,out-side)` + `analogOutputForContainer` 0-15; tick. |
| 23 | Observer | DONE | `Redstone.cpp:384` | `facing` 6-dir check `ox+fdx==x` + 2-tick pulse `powered true queue now+2`, `UpdateLight` via `onBlockChanged`. |
| 24 | Rails | DONE | `Redstone.cpp:510` + `GameServer.cpp:4655` | `recomputeRailShape` `north_south→ascending/east_west`, `powered_rail` boost `0.06`, `detector_rail` redstone output, `activator_rail` eject, `minecartsTick` physics. |
| 25 | Dispenser per-item | DONE | `GameServer.cpp:1739` | **plan12 §9 DONE:** 9-slot edge-trigger `facing` 6-dir; `arrow/snowball/egg/pearl/fire_charge/tnt→explodeAt` + `bucket water/lava/powder_snow pickup/dispense` + `potion splash/lingering` + `spawn_egg/boat/minecart/armor/shears/flint/bonemeal` + fallback `spawnItemDrop`. |
| 26 | Dropper | DONE | `GameServer.cpp:1770` + `BlockEntities.hpp:64` | **plan12 §10 DONE:** `Kind::Dropper` 9 slots + `facing` dispense: `doDropperInsert` try `containerAt` `canInsert` → `insert 1` else `spawnItemDrop` (never projectile). |
| 27 | Cactus/sugar cane growth | DONE | `BlockTickScheduler.cpp:234` + `BlockStates.hpp:132` | **plan13 §1 DONE:** `StemBehavior` + `BambooBehavior` `stage 0→1` + `age thick >=4` + `bambooUpdateLeaves h=1→16` `leaves none/small/large` top3, `GrassBlockBehavior` snowy `snow/snow_block` above `randomTick`; `cactus 3→4` + `sugar_cane 3` maxH. |

## Base taxonomy #28–#45: Entities & Mobs (18 rows)

| # | Feature | Status | File | Notes |
|---|---------|--------|------|-------|
| 28 | MobKind 46 | DONE | `Entities.hpp:52` | **plan25 E1 DONE:** 149 kinds (up from 13→46→86→149) with `MobStats` 300/200/500 etc., all `typeId` via `gen::entityTypeIdByName` (kEntities 149, armadillo/bogged/breeze/creaking etc 101 missing fixed). |
| 29 | Brain-Goal-Sensor vs BehaviorTree | DONE | `BehaviorTree.hpp:204` + `AiBrain.cpp:142` | **plan14 §1 DONE:** `BehaviorTreeParser` maps `wither_skull/dragon_breath/warden_sonic_boom` → `WitherSkullAction/DragonBreathAction` + `BreedGoal` wild, `Brain` builds from `brain.behaviors` via `buildTreeFor`. |
| 30 | `SetEquipment 0x60` | DONE | `EquipmentComponent.hpp:1` + `GameServer.cpp:3202` | **plan13 §2 DONE:** `ArmorTrim` `trim_pattern 18`/`trim_material 11` + `HandDropChances 0.085/1.0` + dynamic `sendEquipmentSlot`/`broadcastPlayerEquipment`/`syncEquipmentOnChange` on inventory/creative. |
| 31 | `SetPassengers 0x65` riding | DONE | `GameServer.cpp:5145` + `BehaviorTree.cpp:70` | **plan13 §3 DONE:** `horse` jump `EntityAction 0x28:7` + `PlayerInput 0x29 shift` dismount + `MoveVehicle 0x20` + `boat` buoyancy `0.04` friction `0.9` + `minecart` `0.4` max. |
| 32 | Durability | DONE | `Items.hpp:84` + `DamageComponent.hpp:14` + `CostCalculator.hpp:36` | **plan13 §4 DONE:** `Unbreaking 1/(l+1)` + `Mending` XP `repair/2` + `Anvil` `Too Expensive >=40` `nextRepairCost` + `CustomName` `MC|ItemName`. |
| 33 | Enchant effects | DONE | `EnchantmentHelper.hpp:50` + `Attributes.hpp:66` | **plan13 §5 DONE:** `Efficiency 1+lvl²` mining speed + `FrostWalker radius 2+lvl` `frosted_ice` + `SoulSpeed 0.105*lvl` + `SwiftSneak 0.15*lvl` attribute sync `0x7C`. |
| 34 | Slime/MagmaCube split | DONE | `GameServer.cpp:588` `slimeSize` | Death of `Slime/MagmaCube` size>0 spawns 2-4 babies size-1 with half health, `broadcastMobSpawn`. |
| 35 | Wither/Dragon boss AI | DONE | `BossAI.hpp:19` + `GameServer.hpp:868` | **plan25 E3 DONE:** `WitherSkull` 40t loop **3-burst** (central + 2 side heads spread 0.35, charged blue at ≤150 HP) + `Dragon phases circling/approach/perch` + `BossBar ADD/HEALTH 0x0A` `wither 300HP` `dragon 200HP`; polish: `BossBar TITLE` lerp interpolation pending. |
| 36 | Wool shear | DONE | `GameServer.cpp:3964` | `shears` on `Sheep` `!sheared` → `sheared=true` + `woolColor` drop 1-3, `SetEntityMetadata` index 17, damage shears. |
| 37 | EnderPearl teleport | DONE | `GameServer.cpp:2556` `projectilesTick` | `EnderPearl` block hit → owner `PlayerPosition 0x42` + `EntityTeleport`, `applyDamage 5`, `lastEnderPearlTick` cooldown, `SetCooldown`. |
| 38 | Spawn eggs | DONE | `GameServer.cpp:6327` + `GameServer.hpp:182` | **plan14 §2 DONE:** `onUseItemOn` `*_spawn_egg` → `trySpawnEgg` `pos.offset(face)` `air` check `spawnMobByTypeName` + consume. |
| 39 | Enderman | DONE | `BehaviorTree.cpp:70` | **plan13 §6 DONE:** `TeleportRandomAction` 32-block `EntityTeleport 0x77` + `PickupBlockAction` `grass/dirt/sand` 1/1000 `BlockUpdate` + `StareAction` dot `>0.985` pumpkin guard. |
| 40 | Charged Creeper | DONE | `GameServer.cpp:3737` + `Ids.hpp:74` | **plan13 §7 DONE:** `LightningBolt 0x74` `SpawnEntity` + `channeling trident` thunder check + `creeperCharged` `SetEntityMetadata 17` + `explodeAt 6.0` vs `3.0`. |
| 41 | XP orbs | DONE | `GameServer.cpp:372` `xpOrbsTick` | Sizes `{1,3,7,17,37,73,149,307,617,1237}`, gravity, `SetExperience 0x5B`. |
| 42 | Projectiles tick | DONE | `GameServer.cpp:372` `projectilesTick` | `Arrow/Snowball/Egg/EnderPearl/WitherSkull/Fireball` gravity 0.05, block hit → stuck vs despawn, entity hit radius 0.55, `DamageEvent`. Now called in `tickOnce`. |
| 43 | Breeding/aging | DONE | `AiBrain.cpp:142` + `BehaviorTree.cpp:278` | **plan14 §3 DONE:** `BreedGoal` `loveTicks 600` `findLovePartner 8` + `breed()` `baby age -24000` `breedCooldown 6000` + `EntityEvent 18` + `xp 1-7`. |
| 44 | Villager trading | DONE | `Entities.hpp:89` + `GameServer.cpp:2037` | **plan14 §4 DONE:** `VillagerData` `Type 7` `Profession 15` `level 1-5` + `Gossip` `rep` + `TradeList 0x2E` `level*2` + `SelectTrade` `demand` + `restock 24000t` + `priceMultiplier`. |
| 45 | Boat/Minecart | DONE | `Entities.hpp:151` + `GameServer.cpp:4655` | `MobKind::Boat/Minecart 6HP` + `MoveVehicle 0x20` + `minecartsTick powered_rail 0.06` + `boat` spawn via `bucket`/`dispenser`; polish: buoyancy/friction simplified, no `VehicleMove` water physics. |

## Base taxonomy #46–#56: Inventory & UI (11 rows)

| # | Feature | Status | File | Notes |
|---|---------|--------|------|-------|
| 46 | `MenuType` `Barrel/ShulkerBox` | DONE | `Containers.hpp:29` `Barrel, ShulkerBox` + `totalSlots 27+36` | `BlockEntityStore` `writeChunkNbt` `barrel`/`shulker_box` id. |
| 47 | Enchanting table | DONE | `CostCalculator.hpp:24` + `GameServer.cpp:410` | **plan13 §8 DONE:** `countBookshelves` air-gap max 15 + `enchantingCostsForShelves` `base 1-8+bs/2` trio `1..30`. |
| 48 | Anvil | DONE | `CostCalculator.hpp:36` + `GameServer.cpp:4882` | **plan13 §8 DONE:** `anvilCost` + `Too Expensive >=40` `Property 0` `ContainerSetData 0x14` + `MC|ItemName` `minecraft:item_name` rename. |
| 49 | Brewing stand | DONE | `GameServer.cpp:3156` | **plan13 §8 DONE:** `brewTime 0→400` + `fuel 20` `blaze_powder` + `canBrew`/`doBrew` `PotionBrewing` mix `nether_wart/sugar` + `ContainerSetData` sync. |
| 50 | Stonecutter ghost | DONE | `GameServer.cpp:4780` + `GameServer.hpp:920` | **plan13 §9 DONE:** `PlaceGhostRecipe 0x39` throttle `5t` + `ContainerSetSlot 0x15` + `ghost` echo; polish: preview throttle still simplified 5t. |
| 51 | Creative `SetCreativeModeSlot 0x36` | DONE | `GameServer.cpp:3112` | Checks `gamemode==1`, `slot 0-45` `maxStackFor`, echoes `SetSlot`/`SetCursorItem`, handles `-1` cursor. |
| 52 | Drag `mode5` | DONE | `MenuInteraction.cpp:229` | `button 0/4 start` `dragSlots`, `1/5 addSlot`, `2/6 end` distribute left/even vs right 1-per-slot, `maxStackFor` respect. |
| 53 | `WindowClick` authoritative | DONE | `MenuInteraction.cpp:1` `ClickLogic` | Modes 0 pickupPlace,1 quickMove,2 swapWithHotbar,4 throw,6 pickupAll for all `totalSlots-36` containers. |
| 54 | `ContainerSetContent 0x13`/`SetSlot 0x15` | DONE | `GameServer.cpp:1246` `sendMenuContent` | `windowId/stateId/totalSlots` + each `ItemStack::write` + `cursorItem`, `SetCursorItem 0x5A` sync. |
| 55 | Hopper menu 5/9 slots | DONE | `Containers.hpp:29` `Hopper` 5+36 / `Dispenser` 9+36 | `BlockEntityStore` `GenericContainerData` 5/9, `openMenuAt` `hopper/dispenser` branches, `containerAt` now includes `Barrel/Shulker`. |
| 56 | Recipe book `0x44` + `PlaceRecipe 0x25` | DONE | `GameServer.cpp:4780` | **plan13 §9 DONE; plan32 inventory DONE (1578 recipes):** `PlaceRecipe 0x25` + `PlaceGhostRecipe 0x39` + `RecipeBook` `Furnace`/`Stonecutter` ghost via `RecipeBook` `type 6/10` + `ContainerSetContent`; **plan32:** `assets/data/recipes/*.json` 1581 files → `Recipes::loadDirectory` 1578 loaded (cf913af filler removed), `Recipes.cpp` JSON-driven `Shaped/Shapeless/Smelting/Stonecutting/Smithing` + `TagManager 67 item tags` — **plan41 C-11 1581 vs 1578 = `Recipe::trimBlankRows(["   "])` removes 3 blank-pattern debug recipes (pattern `["   ","   ","   "]` after trim → empty, skipped) — smithing 3 are kept as `Smithing` kind (not `crafting`), remainder `shaped 1285 + shapeless 200 + stonecutting 80 + smelting 13 = 1578`; `tests/test_recipes_mirror.cpp` 20+ mirror/offset strict lock. |

## Base taxonomy #57–#70: Commands & Datapack (14 rows)

| # | Feature | Status | File | Notes |
|---|---------|--------|------|-------|
| 57 | Brigadier foundation | DONE | `brigadier/Tree.hpp:1` `CommandNode`/`CommandDispatcher` `writeDeclareCommands` 0x11 | Flags `0x00 root,0x01 literal,0x02 arg,0x04 exec,0x08 redirect`, `StringReader` backtrack, `suggest` prefix filter. |
| 58 | Arg types 48 | DONE | `brigadier/Arguments.hpp:12` | **plan13 §10 DONE:** `BlockState 12` + `BlockPredicate 13` + `ItemPredicate 15` + `Nbt 19/20/21` + `NbtPath 22` + `Objective 23` + `Team 31` + suggestions. |
| 59 | Tab completion | DONE | `Tree.hpp:206` + `Commands.cpp:1243` | **plan13 §10 DONE:** `fill` block list `gen::kBlocks` + `function` names `DatapackManager::getFunctionIds()` + `scoreboard`/`team` live suggestions. |
| 60 | `/give`/`/summon`/`/setblock` | DONE | `Commands.cpp:261` | 23 top-level literals, vanilla IDs via `gen::itemIdByName`/`entityTypeIdByName`, `BlockPos` `~` relative. |
| 61 | `/fill` | DONE | `Commands.cpp:880` | `from BlockPos to BlockPos block` volume clamp 32768, `setBlock` loop + `broadcastBlockChange` or `queueBlockChange`. |
| 62 | `/execute as @p run` | DONE | `Commands.cpp:900` | Single-level `execute as <entity> run <command>` via `resolveSelector` + re-dispatch with target source, but no `at/positioned/anchored/run` branches. |
| 63 | `/function` | DONE | `Commands.cpp:920` | `namespace:path` → `assets/data/<ns>/functions/<path>.mcfunction` read lines → `dispatchConsole` each, but no `minecraft:tick` tag auto-run, no recursion limit. |
| 64 | `/reload` | DONE | `Commands.cpp:940` | Re-loads `recipes/tags/loot` via same `loadDirectory` + `applyToRecipeTags`, feedback `SystemChat`. |
| 65 | Tags 67 item / 20 block | DONE | `TagManager.hpp:1` `loadDirectory` `pendingRefs_` + `ensureItemDefaults` 67, `GameServer.cpp:296` wired | `planks/logs/coals/wool` + 33 `extras` (fishes, slabs, stairs...), `Ingredient` now resolves `#minecraft:planks` correctly (was `planks/logs/stone` only). |
| 66 | `Ingredient` tag resolve | DONE | `Recipes.cpp:1` | `tags_` map now 67, `TagManager::applyToRecipeTags` at init. |
| 67 | `LootTableEvaluator` | DONE | `LootTables.hpp:1` `evaluate(bn,tool)` | `pools/rolls/weight/set_count`, `fortune` bonus, `silk_touch` via `tool.hasSilkTouch()` → drop block itself; **plan35 polish:** `set_count`/`looting_enchant`/`enchant_randomly`/`apply_bonus` loot functions 評価追加, `explosion_decay`/`furnace_smelt`/`copy_components` は簡略 (smoke 127 で loot give 検証済み)。 |
| 68 | `DatapackManager` | DONE | `DatapackManager.hpp:12` | **plan13 §10 DONE; plan35 polish:** scans `assets/data/<ns>/{advancements,predicates,item_modifiers,functions}` + `world/datapacks/*/data` + `listAvailable/enable/disable` for `/datapack`; **plan35:** `PredicateEvaluator` 8種 (`location/bow/distance/...`) + `advancements` story 20 JSON 駆動 (`assets/data/advancements`) + `loot` reload。 |
| 69 | `FunctionEvaluator` `/function` | DONE | `FunctionEvaluator.hpp:1` + `Commands.cpp:1243` | **plan13 §10 DONE:** `executeFunction` recursion 10 + `return` + `execute store result/success score` + `schedule function` `append/replace` + `tick()` from `GameServer::tickOnce`. |
| 70 | `/tag` `/team` `/bossbar` | DONE | `Commands.cpp:872` + `Ids.hpp:129` | **plan32 DONE (30+ commands):** `/team add/remove/join/leave` + `Teams 0x67` + `/bossbar add/remove/set` + `BossBar 0x0A ADD/HEALTH` + `/tag add/remove/list`; plus **plan32 30+ commands:** `/execute as/at/positioned/anchored/in/dimension/rotated/facing/if/unless run` modifiers (3af3b98) + `/data get/merge/remove` + `/clone` + `/loot` + `/place feature/structure/jigsaw` + `/locate structure/biome/poi` + `/spreadplayers` (9838189) + `/enchant`/`/attribute`/`/trigger` (2ec9050) + `/ban`/`/op`/`/whitelist`/`/kick` admin (8ddc338) + `/advancement`/`/recipe`/`/item`/`/me`/`/msg` (34ae053) + `/fill`/`/give`/`/summon`/`/setblock`/`/function`/`/reload`; polish: `BossBar TITLE` lerp remains client-side (§8 verified). |

## Base taxonomy #71–#79: Network & Protocol (9 rows)

| # | Feature | Status | Packet | Notes |
|---|---------|--------|--------|-------|
| 71 | VarInt/VarLong, big-endian, Position | DONE | `ByteBuffer.hpp:1` | `varint` max 5b for negative, `i16/u16/i32/u32/i64` big-endian, `position(x,y,z)` 26-12-26 pack `((x&0x3FFFFFF)<<38)|((y&0xFFF)<<12)|(z&0x3FFFFFF)`. |
| 72 | Handshake → Status/Login/Config/Play | DONE | `GameServer.cpp:1420` `handleHandshake` | `protocol 769` gate `Outdated client`, `Status` JSON `enforcesSecureChat:false`, `favicon`, `Login` RSA 1024 `EncryptionRequest 0x01` + `mcSha1Hex` + `MojangAuth` curl, `Configuration` `SelectKnownPacks` `RegistryData 0x07` ×12 + `UpdateTags 0x0D` + `FinishConfiguration 0x03`. |
| 73 | Compression/Encryption | DONE | `Connection.hpp:29` | `setCompression 256` `zlib` `dataLength 0` vs `>0` decompress, `readFrame` length varint byte-by-byte decrypt via `AesCfb8 0x80`, `setSendTimeout 15`. |
| 74 | Chat signing `PlayerChat 0x3B` | DONE | `GameServer.cpp:3046` + `net/Crypto.hpp` | `ChatMessageProcessor::verify RSA-SHA256` `ChatMessage 0x07 timestamp/salt/signature` + `MessageAck 0x04` + `shouldUsePlayerChat` → `PlayerChat 0x3B` when key valid else `SystemChat 0x73`; `enforcesSecureChat:false` verified. |
| 75 | Bundle `0x00` + `MultiBlockChange 0x4E` | DONE | `PacketBatcher.cpp:17` | `queuePacket`/`flush` true `BundleDelimiter 0x00 start/end` + `MultiBlockChange 0x4E` coalescing per chunk-section `tryFlushAsMultiBlockChange` dedup last-wins; grouped by section, solo `BlockUpdate 0x09` still via `Bundle`. |
| 76 | KeepAlive S→C `0x27` / C→S `0x1A`, `Cookie`, `ResourcePack` | DONE | `GameServer.cpp:80` | `KeepAlive` 10s send `i64`, 30s timeout `Disconnect Timed out`, 60s idle sweep, `StoreCookie 0x0A/0x72` + `CookieRequest 0x00/0x16` with `world/data/cookies` persistence, `AddResourcePack 0x09` `url/sha1/forced` (SHA1 not verified, forced kick not). |
| 77 | `DeclareCommands 0x11` | DONE | `Tree.hpp:193` `writeDeclareCommands` | Flatten DFS, flags `0x01 literal,0x02 arg,0x04 exec,0x08 redirect`, `parserId` 0-48. |
| 78 | `LevelChunkWithLight 0x28` + `UpdateLight 0x2B` | DONE | `ChunkCodec.hpp:182` | `writePalettedContainer` longCount even for single palette, `biome` 40/desert 14 etc., `serializeUpdateLightBody`; current IDs are owned by SPEC_WIRE and the wire vectors. |
| 79 | `BossBar 0x0A` / `Teams 0x67` | DONE | `Ids.hpp:129` + `Teams.hpp:1` + `Scoreboard.hpp:169` | `ScoreboardObjective 0x64`/`Score 0x68`/`Reset 0x49`/`Display 0x5C` + `BossBar 0x0A ADD/HEALTH/TITLE` (`BossAI` `wither/dragon`) + `Teams 0x67 create/remove/join` via `Commands.cpp:872` + `GameServer.hpp:586` sync on join + `ResetScore 0x49 wildcard` via `Commands.cpp:1131` `players reset` + `GameServer.cpp:3098` `onPlayerLeave` + `FunctionEvaluator.cpp:68`. |

## Base taxonomy #80: Combat & Survival (1 row)

| # | Feature | Status | File | Notes |
|---|---------|--------|------|-------|
| 80 | `DamageCalculator` + `AttributeManager` | DONE | `Attributes.hpp:87` + `DamageSource.hpp:98` | `ARMOR/TOUGHNESS/KB_RESIST` sync `UpdateAttributes 0x7C` (`ARMOR` done) + `DamageCalculator::calculate` `applyArmorReduction` + `Protection EPF fire_protection/feather_falling` categories + `Resistance 0.2*(amp+1)`; polish: `DamageSource isFire/isFall` simplified but PASS smoke. |

## Extension rows #81–#90: Combat & Survival (10 rows)

| # | Feature | Status | File | Notes |
|---|---------|--------|------|-------|
| 81 | Air/drown `airTicks 300` | DONE | `GameServer.cpp:333` `survivalTick` | Head `y+1.62` water check `water[level=0]`, decrement 300→0, `drowningDamage` gamerule, `1 dmg/20t` `drown`, `WaterBreathing` exempt. |
| 82 | Freeze `freezeTicks` powder snow | DONE | `GameServer.cpp:333` | `powder_snow` foot block `freezeTicks` 0→300, `>=140 && freezeDamage && tick%20==0` `freeze` dmg, `freezeDamage` gamerule. |
| 83 | Fire `fireTicks` lava | DONE | `GameServer.cpp:333` | `isFireOrLavaAt` `lava/fire/soul_fire/magma_block/campfire`, `FireResistance` exempt, `doFireTick` gate, `fireTicks 160` on contact, `1 dmg/20t` `onFire`, water extinguish. |
| 84 | Hunger `saturation/food/exhaustion` | DONE | `GameServer.cpp:6173` + `HungerManager.cpp:1` | `exhaustion>=4→saturation/food--` + `food>=18 tick80 regen` + `food==0 starve` + `sprint 0.1/swim 0.01/jump 0.2/attack 0.3` exhaustion + `cake/stew` saturation `useFood` sync `SetHealth 0x5B`; **plan29 §6 polish DONE:** `EXHAUST_BOW 0.01→0.0`, `EXHAUST_BLOCK_BREAK 0.005` added (`HungerManager.cpp`) + `onBlockBreak`/`onDamageTaken` hooks + hunger-effect exhaustion `×20` overcount fixed to `0.005*(amp+1)/tick`. |
| 85 | Fall `water/slime` mitigation | DONE | `GameServer.cpp:3275` `onMovement` | Landing `fallDist` `>3` `floor(fallDist-3)` dmg, but if landing block `water/slime_block/honey_block/hay_block` or `powder_snow+SlowFalling` → `fallDist=0`, `fallDamage` gamerule guarded. |
| 86 | Sneak pose `EntityAction 0x28` | DONE | `GameServer.cpp:3359` | `start_sneak 0/stop 1/start_sprint 3/stop 4`, broadcasts `SetEntityMetadata 0x5D` index 6 pose `5 crouch/0 stand` + index 0 flags `0x02`, via `broadcastPacketExcept`. |
| 87 | PVP knockback `EntityVelocity 0x5F` | DONE | `GameServer.cpp:3857` | `dx*norm*400, dy300, dz*norm*400` for `Player` victim and `Mob` victim, plus `DamageEvent 0x1A` `damage_type` via `gameData.idOf`. |
| 88 | Persistence `playerdata` `stats` `advancements` | DONE | `GameServer.cpp:1389` `savePlayerNBT` | `playerdata/*.dat` `Position/Inventory/Health/Food`, `world/stats/*.json` `play_time`, `advancements` `cppfm:root→diamonds` 9 entries → **plan35 20 entries story 20** (`assets/data/advancements` JSON-driven) `UpdateAdvancements 0x7B` + toasts + `PredicateEvaluator` 8 + loot functions。 |
| 89 | XP `SetExperience 0x5B` | DONE | `GameServer.cpp:372` `xpOrbsTick` | `SpawnExperienceOrb 0x02` sizes `{1,3,7,17...}`, `mobsTick` `spawnXpOrbs` on kill, `sendSetExperience` level curve. |
| 90 | Effects `EntityEffect 0x5E` | DONE | `MobEffects.hpp:14` + `GameServer.cpp:2265` | `Speed/Slowness→MOVEMENT_SPEED` + `HealthBoost→MAX_HEALTH` + `Invisibility/Glowing/Levitation` via `SetEntityMetadata 0x5D` flags + `UpdateAttributes 0x7C` 20t `applyEffectModifiers`; **plan29 §7 polish DONE:** `Levitation` `vy += (0.05*(amp+1)-vy)*0.2`, `fallDistance=0`, `swimming/vehicle` gate (`GameServer.cpp`); §5 `MobEffects.hpp` adds `bad_omen/raid_omen/trial_omen` alias + `TRIAL_OMEN_PER_LEVEL 18000` / `RAID_OMEN_DURATION 600`. |

## Current residuals and publication boundary (outside #1–#90)

These rows are current residuals or evidence blockers. They do not alter the
numbered taxonomy status and must not be converted to PASS by documentation edits.

| item | status | current record / next owner |
|---|---|---|
| `tools/soak_bot.py --duration 300` | `RESOLVED` | three integrated main runs passed; each had KeepAlive `30`, chunks `182`, time updates `300`, all error counters `0`, and cleanup PASS; plan49 §1 |
| accepted 2-hour/24-hour run | `INTERRUPTED / ABSENT` | the 7200-second synthetic attempt was interrupted at recorded `t=3361s`; post-fill RSS was `160388→191612kB` (`+19.5%`), above the `15%` gate; no accepted 2-hour/24-hour artifact exists; plan51 keeps this boundary explicit |
| current real-client/GUI capture | `ABSENT` / `DECLARED-LIMITATION` | no current official-client artifact; bot/synthetic evidence is separate; plan51 JVM boundary does not provide a GUI/client artifact |
| `wt48/cleanup` | `DIRTY` / `PRESERVE-REVIEW` | branch `wt48/cleanup`, HEAD `5f82ac0b4448f76f98753d18c83bbcd9736da61c`, 19 changed paths, `+74/-840`; plan49 §6 safety review |

## Declared limitations (outside #1–#90; not counted as `DONE`)

These are intentional boundaries or unverified claims. They are kept in a separate
table rather than being hidden inside a numbered `DONE` row.

| Feature / boundary | Status | Notes |
|---|---|---|
| Fabric `Netty` `ChannelPipeline` `Codec` abstraction | DECLARED-LIMITATION | The implementation uses manual `WriteBuffer`/`ReadBuffer`; it is not a JVM Netty channel pipeline. |
| Fabric Loader JVM mods and Fabric event-bus bytecode | DECLARED-LIMITATION | plan51 adds an opt-in embedded HotSpot/JNI compatibility layer with a dependency-free shadow ABI, fallback metadata loader, selected events, and a bounded Mixin hook shell. It does not embed official Fabric Loader/Knot, transform arbitrary bytecode, or make arbitrary mods compatible; the E-14 boundary remains. |
| Vanilla Xoroshiro seed parity at L3 | DECLARED-LIMITATION | L1/L2 determinism is covered, but exact vanilla RNG byte parity is not independently proven. |
| Real-client GUI and 24-hour/nightly evidence | DECLARED-LIMITATION | Procedures and bot/synthetic evidence do not substitute for a retained current real-client or long-run artifact. |
| Session mining versus `MiningCalculator` | IMPLEMENTED | plan49 unifies session start/finish and tick completion through shared context/results; `test_mining_full` `59/59` plus live smoke/server paths pass. |
| `MobBehaviorSpec` live coverage | IMPLEMENTED-PARTIAL | plan49 wires 12 descriptor rows into live AI and gameplay assertions; broader species-wide vanilla equivalence remains a declared boundary. |
| Retained marker/comment inventory | DECLARED-LIMITATION | The legacy-reference grep was zero, but a complete zero-marker inventory was not proven. |

## Non-numbered supported capabilities (separate from limitations)

| Feature | Status | Notes |
|---|---|---|
| RCON `whitelist` | DONE | `RconServer` dispatches Brigadier commands and persists whitelist state. |
| `server.properties` subset | DONE | Includes spawn protection, whitelist, online mode, secure profile, view/simulation distance, MOTD, seed, level type, difficulty, resource pack, PVP, flight, hardcore, and max players. |

## Test and gate mapping

`tests/test_smoke_80.cpp` exercises the base taxonomy and its historical extension
checks. A test result is evidence for a named run, not a replacement for the matrix
status. Current named wire counts are `test_spec_wire` `392 PASS 0 FAIL 0 SKIP`,
`test_wire_full` `405 PASS 0 FAIL 0 SKIP`, and `test_wire_b6` `133 PASS 0 FAIL`;
the old handover value `328` is stale. `test_native` remains `ALL PASS` without an
invented aggregate count. The final-gates record includes `test_smoke_80` `212 PASS 0 FAIL`,
`test_gameplay_full` `803 PASS / 1 intentional E-14 FAIL / 804` (exit 1), a passing
`tests/soak_test.py --duration 300` run, and three passing `tools/soak_bot.py
--duration 300` runs. No unexpected executable failure remains; the remaining
publication limitations are recorded separately below.

| evidence class | status | interpretation |
|---|---|---|
| historical numbered-matrix baseline | HISTORICAL | 90 rows classified `DONE` at taxonomy granularity; not a current release result |
| `tools/soak_bot.py` 300-second runs | PASS | 3/3 integrated runs; each KeepAlive 30, chunks 182, time updates 300, all error counters 0, cleanup PASS |
| `tests/soak_test.py` 300-second run | PASS | 150 keepalives, 0 disconnects, 2932 actions, post-fill RSS growth 7.6%; not a 24-hour substitute |
| `tests/soak_test.py` 1800-second run | PASS | allocation-reuse baseline `17ab09f`; 900 keepalives, 0 disconnects, 17493 actions, post-fill RSS growth 12.5%; not a 2-hour/24-hour substitute |
| `tests/soak_test.py` 7200-second attempt | NOT-ACCEPTED | interrupted at recorded `t=3361s`; post-fill RSS `160388→191612kB` (`+19.5%`), above the `15%` gate |
| E-14 gameplay assertion | EXPECTED-FAIL-E14 | Intentional JVM-mod boundary; keep the failure visible |
| nightly/24-hour and real-client evidence | DECLARED-LIMITATION | No accepted current artifact is recorded here |

The stable fixture is `docs/mob_stats_149.csv`; it is required to contain exactly 149
data rows and 11 columns and must remain byte-identical to the archived source used
for restoration. The migration tracker is [CURRENT_STATE.md](CURRENT_STATE.md).
