# Migration & Know-How Guide — cpp-fabricmc 1.21.4 (plan14)

> How to move a vanilla Fabric feature or a new mod idea into this C++ server without touching `GameServer.cpp` monolith. Data-driven first, then event-bus, then packet.

## 1. Mental model (data-driven, not hard-coded)

Vanilla Java loads most gameplay from JSON/NBT at runtime: `worldgen/biome`, `structures/*.json`, `loot_tables/blocks/*.json`, `recipes/*.json`, `tags/item/*.json`, `entities/*.json` (our `assets/entities`). `cpp-fabricmc` mirrors that: C++ is the *engine*, JSON is the *content*. Prefer adding a JSON file and a `IBlockBehavior`/`BehaviorNode` to editing a giant `if` chain.

```
assets/data/{tags,loot_tables,structures,recipes,entities}  ← edit here, no recompile for balance
src/game/{TagManager, LootTables, StructurePlacer, EntityDataLoader, RecipeManager}  ← loaders
src/physics/{BlockTickScheduler, Redstone, Fluids, LightEngine}  ← engines
src/game/{World, Chunk, BlockEntities, GameServer}  ← stores + tickOnce 20 TPS
```

## 2. Module map (where to touch)

| Task | Primary files | Secondary | Tick |
|------|---------------|-----------|------|
| New block (stairs/slab/ore) | `generated/BlockStates.hpp:1` (gen), `World.hpp:241` `getBlock`/`setBlock`, `physics/BlockTickScheduler.hpp:81` `behaviorFor` | `ChunkCodec.hpp:57` `writePalettedContainer` | `BlockTickScheduler::tick` 5t random |
| New item | `generated/ItemIds.hpp:1`, `Items.hpp:19` `ItemStack` components 6/10/21 | `Recipes.cpp:1` `tags_` | `GameServer.cpp:6944` `onUseItemOn` |
| New entity/mob | `Entities.hpp:52` `MobKind` 46, `EntityData.hpp:1` JSON, `AiBrain.hpp:16` `Goal`, `BehaviorTree.hpp:1` `BehaviorNode` | `GameServer.cpp:2690` `spawnMobByTypeName` generic 0-45, `mobsTick:588` | `mobsTick` + `Brain::tick` + `BehaviorTree::tick` |
| New command | `Commands.cpp:142` `initCommands` Brigadier `literal/argument`, `brigadier/Tree.hpp:1` `writeDeclareCommands`, `Arguments.hpp:1` 48 parsers | `Ids.hpp:187` `DeclareCommands 0x11` | `dispatchCommand` |
| Worldgen tweak | `World.cpp:314` `fillNether`/`546 fillEnd`, `worldgen/StructureManager.hpp:1` `shouldGenerate`, `StructurePlacer.hpp:1` `load`, `MultiNoise.hpp:1` | `worldgen/ImprovedNoise.hpp:1` | `World::generateChunkIfMissing` |
| Network packet | `proto/Ids.hpp:1` 769, `ByteBuffer.hpp:1` BE varint, `net/Connection.hpp:29` zlib/AES, `net/PacketBatcher.hpp:1` Bundle + `MultiBlockChange 0x4E` | `GameServer.cpp:727` `broadcastBlockChange` → `queueBlockChange` | `tickOnce:606` `flushBlockBatches` 50ms/64 |
| Persistence | `Anvil.hpp:1` `chunkToNBT`/`chunkFromNBT`, `RegionFile.hpp:1`, `WorldDataManager.hpp:26` `atomicWrite`, `Persistence.hpp:31` `setWorldBorder`/`setDifficulty` | `World.hpp:82` `SpawnPoint`, `GameData.hpp:1` `idOf` | `Persistence::loop` 3s + `tickOnce` 6000/1200t `saveLevelData` |
| Inventory | `Containers.hpp:29` 15 `MenuType`, `MenuInteraction.cpp:1` `ClickLogic` 0-6, `BlockEntities.hpp:50` `GenericContainerData` | `GameServer.cpp:2588` `openMenuAt` | `hoppersTick` 8t |

## 3. Add a block with behavior (example: `minecraft:pointed_dripstone`)

1. **IDs**: `python3 tools/generate.py` or edit `generated/BlockStates.hpp` manually — add `{"minecraft:pointed_dripstone", 12345}` to `kBlocks`, `blockByState`, `kPropDefs` for `thickness`/`vertical_direction`.
2. **Behavior**: `BlockTickScheduler.hpp:103` add `class DripstoneBehavior : public IBlockBehavior { void tick(...) override; bool isFlammable(...) const override; }`, implement in `BlockTickScheduler.cpp:275` (check `below` is `dripstone_block`, `age` 0-3 drip, `getSpreadChance` 0).
3. **Register**: `GameServer.hpp:312` `blockTicks_->registerBehavior("minecraft:pointed_dripstone", std::make_unique<DripstoneBehavior>());`
4. **Place context**: `GameServer.cpp:3683` `kPropDefs` scan — add `thickness` `tip_merge`/`tip`/`frustum`/`middle`/`base` via `ItemUseContext` `cursor.y` + `face` + `world.getBlock` below.
5. **Test**: `tests/test_smoke_80.cpp:1` add `SECTION` `setblock 50 -60 0 minecraft:pointed_dripstone[vertical_direction=up,thickness=tip]` + `waitBlockUpdate`, `ctest -R smoke80`.

## 4. Add a mob with BehaviorTree (example: `minecraft:warden` already in `assets/entities/warden.json:1`)

1. **Stats**: `Entities.hpp:80` add `MobKind::Warden` 500 HP, `mobStats` entry, `gen::entityTypeIdByName` already has `warden 135`.
2. **JSON**: `assets/entities/warden.json`:
```json
{
  "type": "minecraft:warden",
  "attributes": {"max_health": 500, "movement_speed": 0.07, "attack_damage": 30},
  "spawning": {"biomes": ["minecraft:deep_dark"], "light_level": {"max": 0}},
  "brain": {"behaviors": [{"type": "minecraft:warden_sonic_boom", "priority": 1}]},
  "equipment": {"mainhand": "minecraft:sculk_sensor"}
}
```
3. **Node**: `BehaviorTree.hpp:379` `createNodeForType` already maps `warden_sonic_boom` → `WanderAction` fallback; add `class WardenSonicBoomAction : public BehaviorNode { BTStatus tick(...) override; }` that checks `IsPlayerInRange 15` + `IsHurt` then `srv->broadcastSound("minecraft:entity.warden.sonic_boom")` + `applyDamage` 10 + `sonicBoomCooldown`.
4. **Tree**: `buildWitherTree` style: `buildWardenTree()` `Selector` → `WardenSonicBoom` → `MoveToPlayer` → `Wander`. In `EntityDataLoader::loadDirectory`, if `type==warden` set `def.behaviors` to that tree via `buildBehaviorTreeFromTypes`.
5. **Spawn**: `GameServer.cpp:2690` `spawnMobByTypeName` already generic 0-45, so `/summon minecraft:warden` works. `trySpawnMobs` will auto-spawn in `deep_dark` light 0 via `EntityDataDef.biomes`.

## 5. Add a command (example: `/locate biome minecraft:desert`)

1. **Tree**: `Commands.cpp:142` inside `initCommands()`:
```cpp
auto locate = CommandNode::literal("locate");
auto biomeArg = CommandNode::argument("biome", args::resourceLocation());
biomeArg->executable = true;
biomeArg->action = [this](CommandContext& c){
    std::string b = c.arg("biome").asStr();
    // brute force search 1000..1000 using biomeSource_->sample
    for(int r=0;r<100;r++) for(int dx=-r;dx<=r;dx++) for(int dz=-r;dz<=r;dz++){
        int wx = int(c.srcX)+dx*16, wz = int(c.srcZ)+dz*16;
        if(biomeSource_->sample(wx,64,wz)==b) { sendFeedback(c.source.player, "Found at "+std::to_string(wx)+" "+std::to_string(wz)); return 1; }
    }
    sendFeedback(c.source.player, "Not found"); return 0;
};
locate->then(biomeArg); d.root->then(locate);
```
2. **Suggest**: `biomeArg->suggestions = [](...){ return gameData_.order("minecraft:worldgen/biome"); };`
3. **Declare**: `writeDeclareCommands` auto-flattens, no extra work. Test via `tests/test_smoke_80.cpp` `sendChatCommand("locate biome minecraft:desert")` + `waitChat("Found")`.

## 6. Data-driven worldgen tweak (example: add `minecraft:cherry_grove` plateau)

1. **Noise**: `World.cpp:314` `fillNether` already shows 4 noises; add `ImprovedNoise cherryNoise(seed ^ 0x43484552)`, sample `x*0.008, z*0.008` and if `>0.6` set surface to `cherry_leaves` + `dirt` depth.
2. **Biome**: `MultiNoise.hpp:1` `BiomeSource` already samples 30 points; add new point `cherry_grove` with `temperature 0.5 humidity 0.4 continentalness 0.2`.
3. **Structure**: `assets/data/structures/CherryVillage.json` with `spacing 34`, `StructureManager`/`StructurePlacer` will load it; `World::generateChunkIfMissing` will call `StructureManager::generate` for it.

## 7. Networking: add a packet (example: `SetCooldown 0x17`)

1. **Ids**: `proto/Ids.hpp:1` add `constexpr uint8_t SetCooldown = 0x17;` (already `0x17` per 1.21.4).
2. **Write**: `GameServer.cpp:2556` pearl `EnderPearl` already does `SetCooldown`: `WriteBuffer cd; cd.varint(itemId); cd.varint(ticks); conn->sendPacket(pl::sc::SetCooldown, cd);`
3. **Batch**: `PacketBatcher::flush` already coalesces `BlockUpdate` 0x09 → `MultiBlockChange 0x4E` same chunk section, else `BundleDelimiter 0x00` start/end. For new packet, just `batcher_.queuePacket(id,body)`.

## 8. Persistence: add a field to `level.dat`

1. **Write**: `Persistence.hpp:44` `saveLevelData` add `data.set("CustomField", nbt::Value::makeInt(123));` after `Version`.
2. **Read**: `loadLevelData` add `if(auto* cf=data.get("CustomField")) custom_=cf->i;`
3. **Wire**: `GameServer.hpp:341` `persist_->setCustomField` setter, `init` after `loadLevelData` syncs `custom_`.

## 9. Testing: run before you push

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j4
./build/test_golden /path/to/captures          # byte-identical chunks
./build/test_native ./build/cppfm              # status/join/build/chat/multi/stress x12 (spawn-protection=0 if needed)
./build/test_smoke_80 ./build/cppfm            # 80 strict — expect ~6 FAIL until polish TODOs done
ctest -R native --output-on-failure            # 300s
ctest -R smoke80 --output-on-failure           # 400s
```

`test_smoke_80` is the gate for plan12: each `FAIL` maps to a row in `docs/MISSING_FEATURES_1_21_4.md`. Fix the `PARTIAL` row, rebuild, re-run smoke, watch `ok` increment.

## 10. Common pitfalls

- **LongCount for single palette**: `ChunkCodec.hpp:57` must write `varint(0)` after `varint(value)` even for `bits==0` single-valued, else client desyncs every non-uniform section.
- **Dimension type varint index**: `JoinGame` `dimension type` is `gameData_.idOf("minecraft:dimension_type",key)` not string.
- **Shared mutex**: `World::getBlock` is `shared_lock`, `setBlock` is `unique_lock` + `onBlockChanged` hook; don't hold `entsMtx_` while calling `world->setBlock` (deadlock).
- **Chunk cache**: `invalidateChunkCache` + `clearChunkCache` on dimension switch, else old dimension's bytes sent for new dim.
- **BehaviorTree incomplete type**: `AiBrain.hpp:114` forward `BehaviorTree`, `Brain` ctor/dtor defined in `AiBrain.cpp:4` with `#include "BehaviorTree.hpp"` (circular). Don't include `GameServer.hpp` in `BehaviorTree.hpp` inline bodies — move to `BehaviorTree.cpp`.
- **Portal BlockPos redefinition**: `PortalHandler.hpp:14` removed duplicate, use `GameServer.hpp:169` `BlockPos`.

## 11. Where to look for vanilla behavior

- `minecraft-data` 1.21.4 `protocol.json` for packet IDs, `blocks.json` for `kBlocks` 1095, `items.json` 1385, `entities.json` 149.
- `wiki.vg` / `minecraft.wiki` for `Position` pack `((x&0x3FFFFFF)<<38)|((y&0xFFF)<<12)|(z&0x3FFFFFF)` and `VarInt` ZigZag vs max-bytes.
- Captured `assets/registry/*.bin` 12 blobs replayed verbatim in `Configuration` (`RegistryData 0x07`).

This guide plus `docs/MISSING_FEATURES_1_21_4.md` and `tests/test_smoke_80.cpp` are the three entry points for any migration. Start from JSON, add a `BehaviorNode`/`IBlockBehavior`, wire in `GameServer::init`, add a `CHECK` in smoke, run `ctest`.
