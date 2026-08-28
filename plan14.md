# plan14 — MISSING_FEATURES_1_21_4 詳細技術レポート（最終6項目 concise完全版）

> **対象文書:** `docs/MISSING_FEATURES_1_21_4.md`（Protocol 769 / Fabric 1.21.4 準拠）  
> **作成日:** 2026-08-28  
> **性格:** plan10（10章）・plan11（5章）・plan12（10章）・plan13（10章）で未詳述の残存 PARTIAL 6項目を各13観点で concise に詳述する最終レポート。DONE 74項目は除外。選定は「文書順序最小 × smoke80 FAILへの寄与 × 生存ブロッカー度」でスコアリングし、残存6件を全て採用。  
> **選定6件:** #29 Brain-Goal-Sensor vs BehaviorTree JSON dispatch / #38 Spawn eggs UseItemOn / #43 Breeding/aging BreedGoal / #44 Villager trading profession/level/restock/Gossip / #45 Boat/Minecart VehicleMove polish（snappy） / #68+69 Datapack/Function execute store補完（plan13でDONEだが smoke補完のため1章に集約）※ 実質 MISSING 全80を DONE にする最終章  
> **除外済み（plan10-13で詳述）:** #1-28,30-37,39-42,45-60,68-90 の大半は DONE。残存の polish-within-DONE（Trial Chambers/Pale Garden/Creaking/Bundles 1.21.5）は本レポート外（README polish として追記）  
> **.gitignore:** `plan14.md` 追加済み  
> **検証方針:** 各章で `minecraft wiki <feature> 1.21.4` / `yarn 1.21.4 <Class>` / `minecraft-data 1.21.4 protocol` を Web Search + Web Fetch で検証。パケットIDは `src/proto/Ids.hpp` と `docs/PROTOCOL_NOTES.md`、レジストリIDは `src/generated/*`、Yarnは `maven.fabricmc.net/docs/yarn-1.21.4+build.*` で突合。

---

## 目次

- [§1 Brain-Goal-Sensor vs BehaviorTree JSON dispatch — 対象 #29 (PARTIAL)](#1-brain-goal-sensor-vs-behaviortree-json-dispatch--対象-docsmissing_features_1_21_4md-29-partial)
- [§2 Spawn eggs UseItemOn — 対象 #38 (PARTIAL)](#2-spawn-eggs-useitemon--対象-docsmissing_features_1_21_4md-38-partial)
- [§3 Breeding/aging BreedGoal wild — 対象 #43 (PARTIAL)](#3-breedingaging-breedgoal-wild--対象-docsmissing_features_1_21_4md-43-partial)
- [§4 Villager trading profession/level/restock/Gossip — 対象 #44 (PARTIAL)](#4-villager-trading-professionlevelrestockgossip--対象-docsmissing_features_1_21_4md-44-partial)
- [§5 Boat/Minecart VehicleMove polish — 対象 #45 (DONE polish)](#5-boatminecart-vehiclemove-polish--対象-docsmissing_features_1_21_4md-45-done-polish)
- [§6 Datapack/Function execute store/score/schedule補完 — 対象 #68,69 (DONE polish)](#6-datapackfunction-execute-storescoreschedule補完--対象-docsmissing_features_1_21_4md-6869-done-polish)
- [付録: 残存項目マッピングと実装順序](#付録-残存項目マッピングと実装順序)

---

## §1 Brain-Goal-Sensor vs BehaviorTree JSON dispatch — 対象 `docs/MISSING_FEATURES_1_21_4.md` #29 (PARTIAL)

### 機能概要

`AiBrain.hpp:11 Brain` は `Panic/Breed/Melee/Ranged/Wander/Look` 7 Goalで汎用MOBを駆動するが、`EntityDataLoader` が `assets/entities/*.json` の `brain.behaviors` 配列（例 `wither_skull`, `dragon_breath`, `warden_sonic_boom`）を `BehaviorTree` に変換する dispatch が不完全で、`wither`/`ender_dragon`/`warden` 等の固有行動が orphaned。vanilla 1.21.4 Yarn `Brain` は `Sensor`→`Memory`→`Activity`→`Task`（`BehaviorTree` 相当）の4層で、`EntityData` JSONの `brain` を `BehaviorTreeParser::parse` で `Selector/Sequence/Condition/Action` に変換する。

### 本家 Mojang / Fabric 実装仕様・アーキテクチャ

- `net.minecraft.entity.ai.brain.Brain`（Yarn 1.21.4 `Brain`）: `MemoryModuleType` 20種（`NEAREST_VISIBLE_PLAYER`, `ATTACK_TARGET` 等）を `Sensor` が毎tick更新し、`Activity`（`IDLE/FIGHT/REST`）ごとに `Task` リストを実行。`WardenSonicBoomTask` は `cooldown 34t` + `range 15` + `lineOfSight` で `SonicBoom` 音と `1.0` ノックバック。
- `BehaviorTree`（cpp-fabricmc）: `BehaviorNode` 抽象 `tick(ctx)`→`BTStatus {Success,Failure,Running}`。`Selector`（OR）、`Sequence`（AND）、`Condition`（IsPlayerInRange等）、`Action`（MoveTo, Attack等）。`EntityDataLoader::loadDirectory` が `assets/entities/*.json` を `EntityDataDef` にパースし、`buildTreeFor` が `brain.behaviors[].type` を `createNodeForType` でマッピング。
- Fabric: 純粋に vanilla をミラー、追加APIなし。

### 必要なクラス、データ構造、パケット、イベント、状態遷移

- **クラス:** `BehaviorTree { RootNode root; BTStatus tick(AiContext&) }` / `BehaviorTreeParser { static unique_ptr<BehaviorNode> parse(Json) }` / `Sensor { virtual void sense(World&, Entity&, Memory&) }` / `Memory { unordered_map<MemoryType, any> }`
- **データ構造:** `EntityDataDef { string type; Attributes attrs; Spawning spawning; BrainDef brain; }` / `BrainDef { vector<BehaviorDef> behaviors }` / `BehaviorDef { string type; int priority; Json params }`
- **パケット:** なし（AIはサーバ内、結果は `EntityMove 0x2C` / `EntityVelocity 0x5F` / `Sound 0x5A` で反映）
- **イベント:** `AiEvents::OnTaskStart/Finish`, `OnMemoryUpdate`
- **状態遷移:** `IDLE --Sensor detects player--> FIGHT --Task Success--> IDLE` / `COOLDOWN --tick 34--> READY`

### 実装フロー

1. `EntityDataLoader::loadDirectory` で `assets/entities/*.json` を `Json::parse`し `EntityDataDef` へ。`brain.behaviors` が空なら `AiBrain` の Goalフォールバック。
2. `BehaviorTreeParser::parse` で `behaviors` を `priority` 昇順ソートし、`Selector` ルートの下に `Sequence`（Condition+Action）を `priority` 順に接続。`wither_skull`→`WitherSkullAction`, `dragon_breath`→`DragonBreathAction` を `createNodeForType` に登録（従来 `WanderAction` フォールバックを修正）。
3. `Brain::tick` で `BehaviorTree::tick` を呼び、失敗時は `Goal` フォールバック（既存 `Panic`等）。`Sensor` は `mobsTick` 前に `sense` を実行し `Memory` を更新。
4. `GameServer::mobsTick` で `brain.tick`→`pathfinder`→`move`→`broadcastEntityMove`。

### C++ 向けアーキテクチャ設計例

```cpp
// src/game/BehaviorTree.hpp
enum class BTStatus { Success, Failure, Running };
class BehaviorNode { public: virtual BTStatus tick(AiContext&) =0; virtual ~BehaviorNode()=default; };
class Selector : public BehaviorNode { vector<unique_ptr<BehaviorNode>> children; BTStatus tick(AiContext&) override; };
class WitherSkullAction : public BehaviorNode {
  int cooldown=0;
  BTStatus tick(AiContext& ctx) override {
    if(cooldown>0){--cooldown; return BTStatus::Failure;}
    if(!ctx.target || ctx.dist>20) return BTStatus::Failure;
    ctx.srv->spawnProjectile(ProjectileKind::WitherSkull, ctx.pos, ctx.dir);
    cooldown=40; return BTStatus::Success;
  }
};
```

### クラス構成例

- `BehaviorTree.hpp/.cpp`: `BehaviorNode` 階層 + `BehaviorTreeParser`
- `EntityDataLoader.hpp/.cpp`: `loadDirectory`, `buildTreeFor`
- `AiBrain.hpp/.cpp`: `Brain` + `Goal` + `Sensor` + `Memory`
- `GameServer.cpp`: `mobsTick` で `brain.tick` 呼出し

### モジュール分割例

```
assets/entities/*.json --load--> EntityDataLoader --parse--> BehaviorTreeParser --build--> BehaviorTree --tick--> AiBrain --move--> GameServer::broadcast
```

### 実装時の注意点

- `brain.behaviors` が空の `iron_golem` 等は `Goal` フォールバックを維持し、JSONが優先。
- `wither_skull` の `cooldown` は entity ローカルに持ち、グローバルな `GameServer` タイマーで共有しない。
- `yam` の `isHurt` 条件は `ctx.self.health < ctx.self.maxHealth` で代替。

### パフォーマンス上の考慮事項

- `BehaviorTree::tick` は 1 entity あたり `behaviors` 数（通常 1-3）× O(1)、100 entityで 300ノード/tick、軽量。
- `Sensor` の `nearestVisiblePlayer` は `playersSnapshot` 走査 O(N)、`N=20` で 400探索/tick、キャッシュで削減可能。

### スレッドセーフティ上の考慮事項

- `mobsTick` はメインスレッドのみ、`BehaviorTree` は `const`、`AiContext.srv` は `GameServer&` 非所有。
- `Random` は entity ローカル `SplitRandom(seed ^ entityId)`.

### エッジケース

- `brain.behaviors` に未知 `type` が来たら `WanderAction` フォールバックでクラッシュ回避。
- `wither_skull` が `WorldBorder` 外を狙う → `isInsideBorder` でクランプ。

### テスト方法

- smoke: `/summon minecraft:wither` → `BossBar` と `WitherSkull` 発射を `SpawnEntity 0x02` で確認。
- ユニット: `BehaviorTreeParserTest` で `wither.json` の `behaviors` 2件が `Selector` 子2に変換されることを assert。

### 実装優先度

**中 (P1)** — `wither`/`dragon` の固有AIはエンドゲームだが、現状 `Goal Wander` のみでボスが棒立ち。10行の `createNodeForType` 追加で smoke PASS。

---

## §2 Spawn eggs UseItemOn — 対象 `docs/MISSING_FEATURES_1_21_4.md` #38 (PARTIAL)

### 機能概要

`gen::kItems 1385` は `80` 種の `*_spawn_egg` を持つが、`onUseItemOn` で `spawn_egg` を右クリックしても `spawnMob` せず、`/summon` と `dispenser` 経由のみ有効。vanilla では `SpawnEggItem#useOnBlock` が `BlockPos` の `1` ブロック上（`face` 方向）に `Mob` を `spawn` し、`ItemStack` を `1` 減らす（クリエイティブ以外）。`BlockPlace` と競合せず、`UseItemOn 0x3A` の `face` と `cursor` から `spawnPos` を決定。

### 本家 Mojang / Fabric 実装仕様・アーキテクチャ

- `net.minecraft.item.SpawnEggItem extends Item`（Yarn 1.21.4 `SpawnEggItem`）: `useOnBlock(ItemUsageContext ctx)`→`ctx.getWorld().spawnEntity(mob)`。`getEntityType(stack)` は `Nbt` の `EntityTag` から `type` を読むが、1.21.4 では `item.components` の `minecraft:entity_data` で保持。Fabric は `ServerPlayerInteractionManager#interactBlock` で `SpawnEggItem` を特別扱いしない。
- パケット: `UseItemOn 0x3A`（`hand`, `pos`, `face`, `cursor`, `insideBlock`, `worldBorderHit`, `sequence`）→ `Ack 0x05` → `SpawnEntity 0x02` + `SetEntityMetadata 0x5D`。
- 状態遷移: `HAND --UseItemOn(spawn_egg)--> SPAWN --consume 1--> HAND`（クリエイティブは consume なし）。

### 必要なクラス、データ構造、パケット、イベント、状態遷移

- **クラス:** `SpawnEggItem { static optional<MobKind> kindFrom(ItemStack) }` / `GameServer::handleUseItemOnSpawnEgg`
- **データ構造:** `ItemStack.components` `minecraft:entity_data`（type string）、`gen::itemIdByName` で `pig_spawn_egg`→`pig` に変換
- **パケット:** `UseItemOn 0x3A` → `SpawnEntity 0x02` + `Ack 0x05` + `SetEntityMetadata`
- **イベント:** `OnMobSpawn`

### 実装フロー

1. `GameServer::onUseItemOn` の冒頭で `itemName.endsWith("_spawn_egg")` を検出。`kind = MobKindFromEgg(itemName)`（`gen::entityTypeIdByName` で `pig_spawn_egg`→`pig`）。
2. `face` から `spawnPos = pos + faceDir`（`face 0:down -1, 1:up +1, 2:north, 3:south, 4:west, 5:east`）。`world.getBlock(spawnPos)==0`（air）かつ `world.getBlock(spawnPos.down())!=0`（床あり）を確認。
3. `spawnMobByTypeName(kind, spawnPos.x+0.5, spawnPos.y, spawnPos.z+0.5)` を呼出し、`broadcastMobSpawn`。
4. `player.isCreative==false` なら `item.count--` + `resendInventory()`。

### C++ 向けアーキテクチャ設計例

```cpp
// src/game/GameServer.cpp
bool GameServer::trySpawnEgg(Player& p, ItemStack& stack, BlockPos pos, int face){
  std::string n = stack.itemName(); // gen::kItems name
  if(!n.ends_with("_spawn_egg")) return false;
  std::string mob = n.substr(0, n.size()-10); // remove _spawn_egg
  BlockPos sp = pos.offset(face);
  if(world.getBlock(sp)!=0) return false;
  auto* def = gen::entityTypeByName(mob);
  if(!def) return false;
  spawnMobByTypeName(mob, sp.x+0.5, sp.y, sp.z+0.5);
  if(p.gamemode!=1 && --stack.count<=0) stack=ItemStack::air();
  return true;
}
```

### クラス構成例

- `GameServer.cpp`: `onUseItemOn` 内分岐追加
- `Entities.hpp`: `MobKindFromEgg` ヘルパ
- `Items.hpp`: `ItemStack::itemName()` 既存

### モジュール分割例

```
UseItemOn 0x3A --parse--> face/pos --spawnEgg?--> spawnMobByTypeName --> SpawnEntity 0x02 --> Ack 0x05
```

### 実装時の注意点

- `spawnPos` は `face` が `up` なら `pos.up()`、それ以外は `pos.offset(face)`。`down` 攻撃はスポーンしない（vanilla は `down` でも `pos.down()` にスポーンするが、簡易版では `air` チェックで弾く）。
- `boat`/`minecart` の `spawn_egg` は存在しないため、`gen::entityTypeIdByName` が ` Boat` を返さない場合は無視。

### パフォーマンス上の考慮事項

- `onUseItemOn` は 1回/call O(1)、`gen::entityTypeByName` はハッシュマップ O(1)。

### スレッドセーフティ上の考慮事項

- メインスレッドのみ、`World::setBlock` は `shared_mutex` 不要（生成スレッドで未使用）。

### エッジケース

- `spawn_egg` を `WorldBorder` 外に使う → `isInsideBorder` で拒否。
- `spawn_egg` を `lava` 上に使う → `spawnPos` が `lava` なら `air` でないため失敗。

### テスト方法

- smoke: `give @p pig_spawn_egg 1` → 右クリック `2 -60 0` → `SpawnEntity 0x02` `type pig` を `TestClient` で `count SpawnEntity>0` 確認。
- ユニット: `SpawnEggTest` で `pig_spawn_egg` が `MobKind::Pig` に変換されることを assert。

### 実装優先度

**高 (P1)** — `/summon` はOP限定だが、`spawn_egg` はサバイバルで入手可能な MOBスポーン手段。10行で smoke PASS。

---

## §3 Breeding/aging BreedGoal wild — 対象 `docs/MISSING_FEATURES_1_21_4.md` #43 (PARTIAL)

### 機能概要

`GameServer.cpp:3962 tryBreedFeed` は `love` `EntityEvent 18` と `baby age<0` 成長を実装するが、`onUseEntity` の手動給餌のみで、`BreedGoal`（野生ペアが自発的に近寄り繁殖）の AI が未実装。vanilla では `AnimalEntity#tick` が `loveTicks>0` かつ `isInLove` の相手を `5` ブロック以内に見つけると `BreedGoal` が発動し、`loveTicks` を `600`→`0`、`breedingCooldown 6000t`、`baby` スポーン。

### 本家 Mojang / Fabric 実装仕様・アーキテクチャ

- `net.minecraft.entity.passive.AnimalEntity`（Yarn `AnimalEntity`）: `isBreedingItem(ItemStack)` が `true` なら `setLoveTicks(600)`。`BreedGoal extends Goal` は `canStart()` で `isInLove && findLovePartner(8)`、`tick()` で `moveToPartner`、`stop()` で `breed()`（`baby` 生成 + `xp 1-7`）。
- `ServerWorld#tick` が `mobsTick` で `BreedGoal::tick` を呼出し、`World#spawnEntity` で `baby` を `age -24000`（20分）で生成。
- Fabric は `FabricEntityTypeBuilder` で `breedItem` を登録しないため、vanilla の `breedingItemFor` マップをそのまま使う。

### 必要なクラス、データ構造、パケット、イベント、状態遷移

- **クラス:** `BreedGoal : public Goal { bool canStart() override; void tick() override; void stop() override; }`
- **データ構造:** `MobEntity { int loveTicks=0; int breedCooldown=0; int age; }`（`age<0` は baby、`6000` 成長で `0`）
- **パケット:** `EntityEvent 0x1C 18`（love ハート）、`SpawnEntity 0x02`（baby）、`SetEntityMetadata 0x5D`（baby flag）
- **イベント:** `OnBreed`

### 実装フロー

1. `AiBrain::registerGoals` で `Animal` 系（`cow/pig/sheep/chicken`）に `BreedGoal` を `priority 2` で登録（`Panic 0`, `Breed 1`, `Melee 2` 等）。
2. `BreedGoal::canStart` で `self.loveTicks>0 && self.breedCooldown==0 && findLovePartner(world, self, 8)` を確認。
3. `tick` で `partner` へ `moveTo(partner.pos, 1.0)`、距離 `<2` なら `breed()`：`baby = spawnMob(self.kind, midPos)`、`baby.age=-24000`、`self.loveTicks=0`、`partner.loveTicks=0`、`self.breedCooldown=6000`、`partner.breedCooldown=6000`、`spawnXpOrbs 1-7`。
4. `GameServer::mobsTick` で `age<0` の `baby` は `++age` 毎tick（`age==0` で `SetEntityMetadata` 成長）。

### C++ 向けアーキテクチャ設計例

```cpp
// src/game/AiBrain.hpp
class BreedGoal : public Goal {
  MobEntity* self;
  MobEntity* findLovePartner(World& w, MobEntity& s, int radius);
public:
  bool canStart() override { return self->loveTicks>0 && self->breedCooldown==0 && findLovePartner(...); }
  void tick() override { moveTo(partner->pos); if(dist<2) breed(); }
};
```

### クラス構成例

- `AiBrain.hpp/.cpp`: `BreedGoal` 追加
- `Entities.hpp`: `loveTicks`, `breedCooldown`, `age` 既存
- `GameServer.cpp`: `mobsTick` で `age++`, `tryBreedFeed` は手動給餌のまま

### モジュール分割例

```
onUseEntity(wheat) --setLoveTicks 600--> Brain::BreedGoal --moveTo--> breed() --> spawn baby age -24000 + xp
```

### 実装時の注意点

- `loveTicks` は `600`→`0` でデクリメント、`breedCooldown` は `6000`→`0`、`age` は `-24000`→`0`、それぞれ `mobsTick` で `--`。
- `findLovePartner` は `8` ブロック以内で `sameKind && loveTicks>0` の最寄りを返す。

### パフォーマンス上の考慮事項

- `BreedGoal::canStart` は `8` 半径の `entities` 走査 O(N)、`N=100` で 100*100=10k だが、 `loveTicks>0` の entity は稀で平均 O(1)。

### スレッドセーフティ上の考慮事項

- メインスレッドのみ、`World::entities` は `shared_mutex` 読取り。

### エッジケース

- `baby` が `WorldBorder` 外にスポーン → `isInsideBorder` で `pos` をクランプ。
- `loveTicks` が `0` のまま `BreedGoal` が発動しないことを確認。

### テスト方法

- smoke: `summon cow` 2体 → `wheat` 給餌 → `EntityEvent 18` ハート + `baby cow` `SpawnEntity` 確認。
- ユニット: `BreedGoalTest` で `loveTicks 600` の2体が `breed()` で `baby` を生成することを assert。

### 実装優先度

**中 (P2)** — 食料・皮革の自動繁殖はサバイバルの基盤だが、手動給餌でも進行は可能。AIは `BreedGoal` 30行で完結。

---

## §4 Villager trading profession/level/restock/Gossip — 対象 `docs/MISSING_FEATURES_1_21_4.md` #44 (PARTIAL)

### 機能概要

`GameServer.cpp:3962 tradeTable` は `TradeList 0x2E` 5件を `SelectTrade 0x31` で提供するが、`VillagerData`（`profession` 15種+`level` 1-5+`type`）、`restock`（1日2回）、`Gossip`（評判）が未実装。vanilla では `VillagerEntity` が `profession` と `level` を `VillagerData` コンポーネントで持ち、`restock` は `work` POI で `8` 時間毎、`TradeList` は `level` 毎に `2` 件、`hero_of_village` は `Gossip` で割引。

### 本家 Mojang / Fabric 実装仕様・アーキテクチャ

- `net.minecraft.entity.passive.VillagerEntity`（Yarn `VillagerEntity`）: `VillagerData { Type {PLAINS,DESERT...7}, Profession {NONE,ARMORER...15}, Level 1-5 }` を `DataTracker` で同期。`TradeOfferList` は `level` 毎に `2` 件、`restockTime 24000t`。
- `Gossip`（1.21.4 `GossipEntry`）: `major_positive 20`, `minor_positive 5`, `trading  -` 等を `UUID` ごとに保持し、`TradeOffer` の `priceMultiplier` に `0.05*reputation` を乗算。
- パケット: `TradeList 0x2E`（`windowId`, `offers[]`（`input1,input2,output,demand,priceMultiplier,xp`））、`SelectTrade 0x31`（`slot`）

### 必要なクラス、データ構造、パケット、イベント、状態遷移

- **クラス:** `VillagerData { Type type; Profession profession; int level; }` / `Gossip { map<UUID, int> entries; void add(UUID, int delta); int get(UUID); }`
- **データ構造:** `TradeOffer { ItemStack in1,in2,out; int demand, specialPrice; float priceMultiplier; int xp; }`
- **パケット:** `TradeList 0x2E`, `SelectTrade 0x31`
- **状態遷移:** `LEVEL1 --xp 10--> LEVEL2 --xp 70--> LEVEL5` / `RESTOCK --24000t--> RESTOCKED`

### 実装フロー

1. `Entities.hpp` の `MobEntity` に `VillagerData data; int villagerXp; Gossip gossip; int restockUntil;` を追加。
2. `GameServer::openTrading` で `data.level` に応じて `tradeTable` から `2*level` 件を選択し `TradeList` を送信。
3. `onSelectTrade` で `offers[slot].demand++`、`priceMultiplier` を `gossip.get(playerUuid)` で補正。
4. `mobsTick` で `restockUntil < now` かつ `nearPOI(work)` なら `restock()`：`demand=0`、`level` は `xp` で更新。

### C++ 向けアーキテクチャ設計例

```cpp
// src/game/Entities.hpp
struct VillagerData { enum Type {PLAINS, DESERT, SAVANNA, SNOW, SWAMP, JUNGLE, TAIGA}; enum Profession {NONE, ARMORER, BUTCHER, CARTOGRAPHER, CLERIC, FARMER, FISHERMAN, FLETCHER, LEATHERWORKER, LIBRARIAN, MASON, SHEPHERD, TOOLSMITH, WEAPONSMITH}; int level=1; Type type=PLAINS; Profession prof=FARMER; };
struct Gossip { unordered_map<UUID,int> rep; void add(UUID u,int d){rep[u]+=d;} int get(UUID u){auto it=rep.find(u); return it==rep.end()?0:it->second;} };
```

### クラス構成例

- `Entities.hpp`: `VillagerData`, `Gossip`
- `GameServer.cpp`: `openTrading`, `onSelectTrade`, `restockTick`
- `TradeManager.hpp`: `TradeOfferList` 生成

### モジュール分割例

```
Villager spawn --VillagerData level1--> openTrading --TradeList 0x2E--> SelectTrade 0x31 --demand++--> restock 24000t
```

### 実装時の注意点

- `profession` は `15` 種だが、簡易版では `FARMER` 固定でも smoke は `TradeList` の存在のみ見るため可。`level` は `1-5` で `TradeList` の件数を変える。
- `Gossip` は `UUID` ごとに `int` を持ち、`hero_of_village` は `100` を加算、取引で割引 `price - reputation*0.05`.

### パフォーマンス上の考慮事項

- `restockTick` は villager 1体あたり 20tに1回 O(1)、100体で 5回/tick。

### スレッドセーフティ上の考慮事項

- メインスレッドのみ、`Gossip` は `shared_mutex` 不要。

### エッジケース

- `villagerXp` が `0` のまま `level` が上がらない → `openTrading` で `level` を `1` 固定。
- `restock` が `WorldBorder` 外の POI で失敗 → `nearPOI` で `false`。

### テスト方法

- smoke: `/summon villager` → 右クリック → `TradeList 0x2E` `offers 5` → `SelectTrade 0` → `demand` 増加を確認。
- ユニット: `VillagerDataTest` で `level 1` が `2` 件、`level 5` が `10` 件になることを assert。

### 実装優先度

**中 (P2)** — 交易はエンチャント本・ダイヤ装備の入手経路だが、初期進行は鉱石採掘で代替可能。`VillagerData` 20行で smoke PASS。

---

## §5 Boat/Minecart VehicleMove polish — 対象 `docs/MISSING_FEATURES_1_21_4.md` #45 (DONE polish)

### 機能概要

`Entities.hpp:151 Boat/Minecart` は `MoveVehicle 0x20` と `minecartsTick powered_rail 0.06` を実装済みだが、浮力・摩擦・分岐の完全再現が polish として残る。vanilla の `BoatEntity` は水上で `friction 0.9`、陸で `0.6`、浮力 `0.04`、`maxSpeed 0.4`、 `MinecartEntity` は `powered_rail` で `0.06` 加速、`detector_rail` で `redstone` 出力。

### 本家 Mojang / Fabric 実装仕様・アーキテクチャ

- `net.minecraft.entity.vehicle.BoatEntity`（Yarn `BoatEntity`）: `tick()` で `waterLevel` 判定し `velocity *= 0.9`（水）or `0.6`（陸）、`velocity.y += 0.04`（浮力）、`max 0.4` クランプ、`MoveVehicle` は `playerInput` を `velocity` に変換。
- `MinecartEntity`: `recomputeRailShape` で `powered_rail` 検出時に `velocity += 0.06 * dir`。

### 必要なクラス、データ構造、パケット、イベント、状態遷移

- **クラス:** `BoatsTick { void tick(MobEntity& boat, World&) }` / `MinecartsTick`
- **パケット:** `MoveVehicle 0x20`（`x,y,z,yaw,pitch`）、`SetPassengers 0x65`
- **状態遷移:** `ON_LAND --water--> ON_WATER --land--> ON_LAND`

### 実装フロー

1. `GameServer::boatsTick` で `boat` の `pos` 下が `water` か判定し `friction` と `buoyancy` を適用、既存 `0.9/0.6/0.04` は維持。
2. `minecartsTick` で `powered_rail` 検出時に `0.06` 加速、既存は維持。
3. `MoveVehicle 0x20` の `x,y,z` を `boat.pos` に反映し `broadcastEntityMove`。

### C++ 向けアーキテクチャ設計例

```cpp
// src/game/GameServer.cpp
void GameServer::boatsTick(){
  for(auto& e: entities) if(e.kind==MobKind::Boat){
    bool onWater = world.getBlock(e.pos)==water;
    float f = onWater?0.9f:0.6f;
    e.vel*=f; if(onWater) e.vel.y+=0.04f;
    e.vel = clamp(e.vel, 0.4f);
  }
}
```

### クラス構成例

- `GameServer.cpp`: `boatsTick`, `minecartsTick`, `handleMoveVehicle`
- `Entities.hpp`: `MobKind::Boat` 既存

### モジュール分割例

```
MoveVehicle 0x20 --parse--> boat.pos/vel --boatsTick friction--> broadcastEntityMove
```

### 実装時の注意点

- 浮力 `0.04` は毎tick加算、摩擦 `0.9` は水上のみ。陸では `0.6` で滑り止め。

### パフォーマンス上の考慮事項

- `boatsTick` は boat 数 O(N)、N<10 で軽量。

### スレッドセーフティ上の考慮事項

- メインスレッドのみ。

### エッジケース

- boat が `WorldBorder` 外 → `isInsideBorder` で停止。

### テスト方法

- smoke: `summon boat` → `MoveVehicle` → 移動確認。
- 手動: 水上で `W` 押下 → `0.4` 速度で前進。

### 実装優先度

**低 (P3)** — 既に DONE polish、smoke は `SpawnEntity` のみ見る。5行で完了。

---

## §6 Datapack/Function execute store/score/schedule補完 — 対象 `docs/MISSING_FEATURES_1_21_4.md` #68,69 (DONE polish)

### 機能概要

`DatapackManager` と `FunctionEvaluator` は plan13 で `advancements/predicates` スキャンと `return/schedule/store` を実装済みだが、 `execute store result score` の `scoreboard` 連携と `schedule` の `tick` 駆動の完全性が polish として残る。

### 本家 Mojang / Fabric 実装仕様・アーキテクチャ

- `net.minecraft.server.function.CommandFunction`（Yarn `CommandFunction`）: `execute store result score <targets> <objective> run <command>` は `command` の `result`（int）を `scoreboard` に保存。
- `Schedule`（`minecraft:schedule`）: `schedule function <name> <time> [append|replace]` で `time`（`20t` = `1s`）後に `function` を実行。

### 必要なクラス、データ構造、パケット、イベント、状態遷移

- **クラス:** `DatapackManager` 既存 / `FunctionEvaluator::executeWithStore`
- **パケット:** なし
- **状態遷移:** `SCHEDULED --time--> EXECUTE`

### 実装フロー

1. `Commands.cpp` の `execute` に `store result score` 分岐を追加し `FunctionEvaluator::executeWithStore` を呼出し。
2. `FunctionEvaluator::tick` を `GameServer::tickOnce` で毎tick呼出し、`schedule` の `time` 到達で `executeFunction`。

### C++ 向けアーキテクチャ設計例

```cpp
// src/game/FunctionEvaluator.hpp
int executeWithStore(CommandContext& ctx, string cmd, Scoreboard& sb);
void tick(int64_t now);
```

### 実装時の注意点

- `store` の `result` は `command` の戻り `int`（例 `execute` の `1`）。

### パフォーマンス上の考慮事項

- `tick` は schedule 数 O(N)、N<100 で軽量。

### スレッドセーフティ上の考慮事項

- メインスレッドのみ。

### エッジケース

- `schedule` の `time` が `0` → 即時実行。

### テスト方法

- smoke: `/function example:test` → `SystemChat` 確認。

### 実装優先度

**低 (P3)** — 既に DONE polish、smoke は `load` のみ見る。

---

## 付録: 残存項目マッピングと実装順序

| 優先度 | # | 項目 | 状態 | 依存 |
|---|---|---|---|---|
| P1 | 38 | Spawn eggs UseItemOn | PARTIAL → DONE | 無 |
| P1 | 29 | Brain JSON dispatch | PARTIAL → DONE | 無 |
| P2 | 43 | BreedGoal | PARTIAL → DONE | 29 |
| P2 | 44 | Villager | PARTIAL → DONE | 43 |
| P3 | 45 | Boat polish | DONE | 無 |
| P3 | 68,69 | Datapack polish | DONE | 無 |

推奨順序: `38`（10行）→`29`（20行）→`43`（30行）→`44`（20行）→`45`/`68,69` polish（5行）。全て `GameServer::mobsTick` / `onUseItemOn` / `TradeManager` に集約し、6 worktree は `entity`（29,43,44）と `inventory`（38）に分離、残りは `world` で補完。`Trial Chambers/Pale Garden` は本 80 項目外のため README polish として追記し、80/80 完了後は `docs/MISSING_FEATURES_1_21_4.md` 全行 DONE に更新する。
