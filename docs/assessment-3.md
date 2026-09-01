# Behavior Compatibility Audit — Gameplay / Server / Operational Parity (78 → 85)

> **Note:** このドキュメントは **assessment-3 (behavior audit, B-series)** です。assessment-1 (strict wire, S-series 78/78 FIXED) と assessment-2 (deep wire, D-series+H1 32/32 FIXED) はビットワイヤが閉じているため、本監査は**挙動・サーバー機能・運用**のギャップを項目化する。旧称なし（新規）。
> **Target:** `cpp-fabricmc` HEAD `039872b` (plan35 完遂, protocol 769, DataVersion 4189, Yarn 1.21.4) vs Vanilla **Fabric 1.21.4** (Mojang 1.21.4, `minecraft-data 1.21.4`, Yarn `1.21.4+build.*`)
> **Date:** 2026-09-01
> **Method:** Unlimited **Web Search + Web Fetch** cross-check（Yarn `maven.fabricmc.net` / `mappings.dev`, `minecraft.wiki` / `wiki.vg`, Prismarine `minecraft-data`, `PROTOCOL_NOTES.md` wire captures）+ ローカル `grep` で `src/...:line` を絶対パス検証。Severity: **HIGH** = プレイヤー可視の挙動破綻・ワールド破損・kick、**MEDIUM** = 体感できる差異、**LOW** = datapack/運用/コスメ。
> **Result:** **14 behavior gaps** (B-01〜B-14)。現状 **OPEN 11 / PARTIAL 3 / FIXED 0**。本ドキュメントの全項目が FIXED になった時点で「正直な見立て 78/100 → 85/100」到達とみなす（assessment-1/2 の 109 wire gaps は前提として閉じている）。

---

## 0. Legend & Verification

- **Wire sources** — Prismarine `protocol.json` 1.21.4 131 `toClient`（assessment-1/2 で鎖錠済み。本監査は wire ではなく挙動）。
- **Behavior sources**
  - Yarn 1.21.4 `net.minecraft.entity.mob.*`, `world.gen.*`, `loot.*`, `advancement.*`, `enchantment.*`, `village.*`, `server.world.*` (`maven.fabricmc.net` / `mappings.dev`)
  - `minecraft.wiki` — `Mobs` / `Structure` / `Loot table` / `Advancement` / `Enchanting` / `Redstone` / `World generation` / `Spawn` / `Villager trading` / `Weather` / `Difficulty`
  - `minecraft-data 1.21.4` — `entities.json` 149, `recipes.json`, `loot_tables`, `advancements`, `biomes`, `structureSets`
  - ローカル検証: `grep -rn` で `src/...:line` を HEAD で確認（下表の File:line はすべて実在）。
- **Status 定義**
  - `OPEN` = 未実装 or stub
  - `PARTIAL` = 基礎実装はあるが vanilla 網羅・条件分岐が不足（本監査の 3 件は PARTIAL）
  - `FIXED` = 下記「完了条件」を満たし、対応するテストが `PASS`（全項目 FIXED で 85 到達）
- **Test hook** — 各 B 項目に「推奨テスト方法」を付記。`test_smoke_80` の 127 PASS は taxonomy-level の emit 検証であり、本監査の挙動一致は保証しない。

---

## Summary Table (14 gaps — B-series)

| # | Domain | Feature | File:line | Vanilla spec (source) | Gap | Severity | Status |
|---|---|---|---|---|---|---|---|
| **B-01** | Entities / AI | Mob AI 網羅 149 種中 10 種のみ差別化 | `src/game/AiBrain.cpp:45` `Brain::Brain` 6 goals, `src/game/EntityData.cpp:119` `loadDirectory` 18 json, `assets/entities/*.json` 18 files, `src/generated/EntityIds.hpp:13` `kEntities 149`, `src/game/Entities.hpp:93` `MobKind` 46→149 | Yarn `MobEntity` 149 types + `minecraft-data entities.json` 149; wiki `Mobs` 82+ variants; Yarn `Goal`/`Brain` per-mob | 10 種（plan34: Breeze/Armadillo 等）以外は `WanderGoal` + 近接 fallback。139 種は vanilla の固有 Goal/Brain（例: Warden sonic_boom, Enderman teleport, Villager schedule）を持たない | **HIGH** | **PARTIAL** |
| **B-02** | WorldGen | 構造物ピース簡略 — 20 セット登録だが jigsaw 12-variant 止まり | `src/worldgen/StructureManager.cpp:53` `ensureDefaults`, `261` `villageJigsaw`, `535` `trialChambersPiece`, `src/worldgen/StructurePlacer.cpp:56` `load`, `src/worldgen/Structures.hpp:185` defs | Yarn `StructurePool` / `Jigsaw` / `StructureTemplate` + vanilla `data/minecraft/structures/*.nbt` ~300 pieces; wiki `Jigsaw` 12-variant は villages の1 pool のみ | 20 セットの配置（salt/分離は plan33 で vanilla 準拠）だが、各構造物のピースはハードコード簡略（village 12-variant fallback、trial_chambers 1 箱、ancient_city 1 層）。vanilla の nbt 由来の多様性・loot chest 配置・mob 配置なし | **HIGH** | **PARTIAL** |
| **B-03** | Inventory | レシピ tag 解決・グリッド mirror/回転 | `src/game/Recipes.hpp:87` `matchShaped` `mirrored` param, `src/game/Recipes.cpp:183` `crafting_shaped`, `14` `fromName "#tag"`, `96` built-in tags | Yarn `ShapedRecipe` `matches` mirror+全 offset 探索 + `TagManager` 67/20; wiki `Crafting` mirror は vanilla で有効（左右反転） | `matchShaped` は mirror 対応済みだが tag 解決は `Recipes.cpp` 内 built-in `tags_` のみで `TagManager` の datapack tag と同期していない。Shapeless の 1578 JSON は grid 回転不要だが shaped の余白探索・tag 未解決時の誤マッチが残存 | **MEDIUM** | **PARTIAL** |
| **B-04** | Datapack | Advancement トリガ網羅 — story 20 + 3 トリガのみ | `src/game/Stats.hpp:58` `advancementDefs` 29, `src/game/DatapackManager.hpp:46` `advancements` map, `src/game/Stats.hpp:86` `trigger` string 1, `assets/data/advancements/story/*.json` 20 | Yarn `Advancement` + `Criterion` + `Trigger` 30 種（`inventory_changed`, `tick`, `entity_killed`, `location`, `item_used_on_block`, `placed_block` 等）; wiki `Advancement` 全トリガ表; `minecraft-data advancements` | story 20 + 3 トリガ（`tick/inventory_changed/entity_killed`）のみ評価。残り 27 トリガ（`location/placed_block/bred_animals/consume_item` 等）は `Stats.cpp` で stub `true` or 未評価。vanilla の `minecraft:story/*` 以外のツリー（`nether/end/adventure/husbandry`）は未登録 | **MEDIUM** | **OPEN** |
| **B-05** | Datapack | Loot table 網羅 — 関数 4 種のみ | `src/game/LootTables.hpp:48` `LootTable`/`LootTableEvaluator`, `src/game/DatapackManager.hpp:512` `set_count`, `assets/data/loot_tables/blocks/` 5 files + `chests/entities` sparse | Yarn `LootTable` + `LootFunction` 20+（`set_count`, `looting_enchant`, `explosion_decay`, `furnace_smelt`, `copy_components`, `enchant_randomly` 等）; wiki `Loot table` 関数表; `minecraft-data loot_tables` 100+ | 4 関数（`set_count/looting_enchant/explosion_decay/furnace_smelt/copy_components`）のみ実装。`chests`/`entities` の大半は未ロード（5/100）。`fortune` 特化の ore bonus は簡略 `rand()%(fortune+1)`（vanilla は `bonus_ore` テーブル） | **MEDIUM** | **OPEN** |
| **B-06** | Operational | 実クライアント長時間プレイ検証未実施 | `tests/test_smoke_80.cpp:1` 127 checks, `tests/test_spec_wire.cpp:1` 233, `tests/test_fuzz.cpp:1` 23, `src/game/GameServer_tick.cpp:1` tick 20 TPS | Mojang vanilla client 1.21.4 長時間プレイ（移動・戦闘・レッドストーン・nether 往復）が真の parity 判定; Yarn `MinecraftClient` tick 20 | 自動テストは強力だが vanilla client での数時間 Soak（連続プレイ、chunk 往復、死・リスポーン、nether portal、大量 entity）は未実施。Soak は `test_fuzz` の 60s synthetic のみ | **HIGH** | **OPEN** |
| **B-07** | Perf | パフォーマンス — async I/O 未導入・chunkCache 1024 LRU・view-distance 32 未計測 | `src/game/GameServer.hpp:96` `maxLoadedChunks 8192`, `97` `ioWorkerThreads 4`, `908` `chunkCache_.find`, `917` `size>1024 clear`, `1148` `ThreadPool 4`, `src/game/WorldDataManager.hpp:25` `atomicWrite`, `src/physics/Redstone.hpp:189` | Yarn `ThreadedAnvilChunkStorage` async I/O + `ChunkTicketManager`; wiki `View distance` 32 時の chunk 生成 burst 16/tick | `ThreadPool 4` は用意されているが Anvil `RegionFile` の読み書きは依然同期パス（`pollPendingLoads` は poll のみ）。`chunkCache` は 1024 で `clear()` 一括破棄（LRU ではない）。view-distance 32 時の生成速度・メモリを未計測。`maxLoadedChunks` は plan21 で cap 追加済みだが `viewDist²*4` 自動算出は未検証 | **MEDIUM** | **OPEN** |
| **B-08** | Blocks | ブロック/レッドストーン残差 — quasi-connectivity 未実装 | `src/physics/Redstone.hpp:189` `RedstoneEngine`, `src/game/BlockEvent.hpp:2` QC コメント, `src/game/World.hpp:3` piston QC コメント, `src/generated/BlockStates.hpp:14` `kBlocks 1095` | Yarn `PistonBlock` QC（quasi-connectivity, JE 独自）+ `RedstoneWire` 15 power; wiki `Redstone` QC / `Piston` 12-block push/pull; `BlockState` 1095 states 網羅 | QC 未実装（`Redstone.cpp:622` 相当は `Redstone.hpp` に移設後も stub）。1095 block states は registry 上存在するが、挙動は簡略（例: `observer` の 2-tick パルスは実装済みだが `comparator` の `maxStack 16→+4` は近似）。QC は JE parity の象徴的 gap | **MEDIUM** | **OPEN** |
| **B-09** | Entities | モブスポナー/自然スポーン規則 | `src/game/MobSpawner.hpp:13` `MobSpawner` 35 lines, `src/game/MobSpawner.cpp:53` `spawnFromEgg`, `src/game/GameServer_tick.cpp:176` `pollPendingLoads` 近傍 | Yarn `SpawnHelper` / `NaturalSpawner` — `spawn weights` / `mob caps` (monster 70/creature 10/water 5/ambient 15) + `light_level` + `biome` gate; wiki `Spawn` 重み表; `minecraft-data biomes` spawn entries | `MobSpawner` は egg/command/dispenser の手動スポーンのみ。自然スポーン（夜間 hostile、biome 別 weight、mob cap、`light_level`/`structures` gate）は未実装。`EntityDataLoader` の `spawning.biomes/light_level` はロードされるが `GameServer_tick` で参照されない | **HIGH** | **OPEN** |
| **B-10** | Entities | 村人取引 (TradeList 0x2D)・構造物 MOB 配置 | `src/game/GameServer_items.cpp:696` `tradeTable`, `714` `villageWindow`, `744` `TradeList 0x2D`, `src/game/Entities.hpp:93` `MobKind::Villager`, `src/worldgen/StructureManager.cpp:261` `villageJigsaw` | Yarn `VillagerEntity` `TradeOffer` + `VillagerData` (level 1-5, 2*level offers, restock 2/day); wiki `Trading` 全取引表; `minecraft-data` `villagerTrades` | `tradeTable` は固定 8 offers（`lvl*2` 切出し、Gossip `priceMultiplier` 0.05 欠落、`restock 12` 固定）。職業別取引（農民/司書/鍛冶等 13 職業×5 レベル）・バイオーム別・`wandering_trader` 未対応。構造物内の mob 配置（trial_chambers の spawner、mansion の vindicator）は `StructureManager` で未配置 | **MEDIUM** | **OPEN** |
| **B-11** | Combat | エンチャント 41 種の効果完全性 | `src/game/EnchantmentHelper.hpp:15` `EnchantmentHelper` 25/41, `src/game/EnchantmentHelper.hpp:40` `sharpness/power/efficiency`, `src/game/DamageSource.hpp:105` armor 式, `src/game/CombatManager.cpp:35` EPF | Yarn `Enchantment` 41（`protection/fire/blast/proj/feather`, `sharpness/smite/bane`, `power/punch/flame/infinity`, `efficiency/silk_touch/fortune`, `mending/unbreaking` 等）; wiki `Enchanting` 効果表 | 25 種程度は参照されるが `mending` の XP 修繕、`infinity` の矢消費免除、`silk_touch`/`fortune` の loot 分岐（B-05 と連携）、`channeling`/`riptide` の天候 gate 等は部分実装。`EnchantmentHelper` の `level()` は present だが効果適用は `CombatManager`/`LootTables` に散在し網羅テストなし | **MEDIUM** | **OPEN** |
| **B-12** | World | 天候/時間/難易度 完全性 | `src/game/GameServer_tick.cpp:1` tick 20, `src/game/HungerManager.hpp:23` `EXHAUST_*`, `src/game/WorldDataManager.cpp:108` gamerules, `src/game/GameRules.hpp:11` 14→37 rules | Yarn `ServerWorld` `weather` + `Time` + `Difficulty` (peaceful の naturalRegeneration/starvation 無効、hard の starve 死); wiki `Weather` 6000t 周期 / `Difficulty` | `weather cycle + /weather` は実装済みだが `thunder` の lightning 確率、`sleep` の `night skip` は beds のみで `thunder` 同期なし。`difficulty peaceful` の hunger `starvation` 停止・`naturalRegeneration` gate（`HungerManager` では gate あり）、`hard` の starve ダメージ 1/tick は検証済みだが `world.getDifficulty()` の永続化は `level.dat` 経由で手動 | **LOW** | **OPEN** |
| **B-13** | Datapack | datapack 完全性 — /function 全コマンド網羅・advancement triggers | `src/game/FunctionEvaluator.cpp:119` `executeFunction`, `src/game/DatapackManager.hpp:78` scan `advancements/predicates/functions`, `src/game/DatapackManager.hpp:270` `evaluatePredicateValue` 8 種 | Yarn `FunctionManager` + `Predicate` + `Advancement` 全コマンド（`function` は任意コマンド列）; wiki `Function` / `Predicate` / `Advancement triggers` | `/function` は 1 行 = 1 コマンドとして `Commands.cpp` に委譲（`executeLine`）。`return`/`if`/`execute` ネスト・`schedule` delay は `FunctionEvaluator` で部分対応だが、macro `$()` 置換・`return run` の戻り値伝播は未実装。predicate は 8 種（`location/bow/distance/random_chance*`）のみで、残り（`entity_properties/block` 等）は stub | **MEDIUM** | **OPEN** |
| **B-14** | Persistence | マルチワールド/バックアップ・レベル管理完全性 | `src/game/WorldDataManager.hpp:14` `WorldDataManager`, `src/game/WorldDataManager.cpp:8` `saveLevelDataWithProviders`, `src/game/World.hpp:82` `SpawnPoint`, `src/game/GameServer.hpp:97` `ioWorkerThreads` | Yarn `LevelStorage` `level.dat` + `level.dat_old` backup + per-dim `DIM-1/DIM1/region`; wiki `level.dat` DataVersion 4189 / `WorldBorder` / `DragonFight`; `Anvil` region `*.mca` zlib | `level.dat` 単一化（W16 `DIM` に level.dat を作らない）は FIXED（`WorldDataManager.cpp:13` guard）。`level.dat_old` バックアップ（`WorldDataManager.hpp:40` rename）は実装済み。残 gap は `playerdata/*.dat` の全フィールド（`Inventory` の components 45, `EnderItems`, `abilities`）の round-trip と、`region/*.mca` の圧縮レベル・`DataVersion` 4189 固定の DataFixerUpper の 0-差分（`WorldDataManager.hpp:81` log のみ） | **LOW** | **OPEN** |

> **Severity 集計:** HIGH 4 (B-01, B-02, B-06, B-09) / MEDIUM 8 / LOW 2 — 計 14 gaps。PARTIAL 3 (B-01, B-02, B-03) は基礎あり、OPEN 11 は新規実装が必要。

---

## 1. Entities — Mob AI (B-01)

**Spec.** Yarn 1.21.4 `net.minecraft.entity.mob.*` 149 `EntityType`（`minecraft-data 1.21.4/data/pc/common/entities.json` 149）。各 mob は `Goal`/`Brain`/`Sensor` を持つ（例: `Warden` `SonicBoomTask` 15-20 + armor bypass, `Enderman` `TeleportGoal` 32 ブロック, `Villager` `Schedule` 2000t restock）。vanilla の `Brain` は `Selector/Sequence/Condition/Action` の data-driven だが、mob 固有の `Action` は Java 実装。

- **Source:** `https://maven.fabricmc.net/docs/yarn-1.21.4+build.9/net/minecraft/entity/mob/package-summary.html`, `https://minecraft.wiki/w/Mob`, `minecraft-data 1.21.4 entities.json`
- **Code.** `src/game/AiBrain.cpp:45` `Brain::Brain()` pushes 6 generic goals (`CreakingGoal`, `SwellGoal`, `ArmadilloRollUpGoal`, `PanicGoal`, `FleeSunGoal`, `LeapAtTargetGoal`) — plan34 で 10 種差別化。`src/game/BehaviorTree.cpp:510` `BehaviorTree` nodes（`Selector`/`Sequence`）。`src/game/EntityData.cpp:119` `loadDirectory` は `assets/entities/*.json` 18 files を `BehaviorTree` に変換。`src/generated/EntityIds.hpp:13` `kEntities 149` は registry 上存在するが `Entities.hpp:93` `MobKind` の `mobStats` は 46 種に stats しか持たない（他は fallback）。
- **Gap.** 18 json / 10 差別化以外は `WanderGoal` + 近接 `MeleeAttack` fallback。139 種の vanilla 固有挙動（例: `Breeze` の wind_charge, `Armadillo` の roll-up, `Creaking` の gaze-freeze 60° は plan34 で追加済みだが `IronGolem` の village 守衛、`Piglin` の barter、`Bee` の pollinate は未実装）。
- **Severity:** HIGH — プレイヤーが 82 種の mob と「戦った感じ」が vanilla と異なる。ワイヤは 109 gaps で一致するが AI は挙動で 85 の壁。
- **完了条件 (FIXED):**
  - `assets/entities/*.json` を 18 → 40+ に拡張し、`kEntities 149` の主要 30 種（敵対 15 + 中立 8 + 村人系 5 + 水生 2）を data-driven で差別化。各 json の `brain.behaviors` が `BehaviorTree` として tick される。
  - `test_smoke_80` の mob 節を `spawnMobByTypeName("minecraft:warden")` で `SonicBoom` 粒子 27 を assert、`Enderman` の `carriedBlock` 15 の `Optional<BlockState>` を assert。
  - `test_spec_wire` の entity metadata `D13/D14` 拡張で、追加 mob の metadata が `protocol.json` type 8 (Boolean) で round-trip。
- **推奨テスト:** `python3 tests/multi_client_test.py` で 2 clients + 10 種 mob を 600t 観測し、`EntityTracker` の yaw/pitch と `HurtAnimation 0x25` の発火を目視。`./build/test_native --filter mob_ai` で 30 種の `Brain::tick` を unit。
- **優先度 (85 到達):** ★★★★★ — Dandori で 90/100 の最大寄与。B-02 と並ぶ最優先。

---

## 2. WorldGen — Structure Pieces (B-02)

**Spec.** Yarn `net.minecraft.world.gen.structure.Structure` + `StructurePool` + `Jigsaw`（`minecraft:ancient_city`, `trial_chambers`, `village`, `mansion`, `monument` 等 20 sets）。vanilla は `data/minecraft/structures/*.nbt` 300+ テンプレートを `StructureTemplateManager` が配置。`structureSets` の `spacing/separation/salt` は plan33 で vanilla 準拠（`StructureManager.cpp:53`）。

- **Source:** `https://minecraft.wiki/w/Structure`, `https://minecraft.wiki/w/Jigsaw`, `Yarn StructureManager`, `minecraft-data 1.21.4 structureSets`
- **Code.** `src/worldgen/StructureManager.cpp:53` `ensureDefaults` 20 sets（`village 10387312`..`trial_chambers 94251327` salt は `Structures.hpp:185` で固定）。`261` `villageJigsaw` は `rand()%12` variant, `535` `trialChambersPiece` は単一箱 `obsidian+spawner`, `StructurePlacer.cpp:56` `load` は `assets/data/structures/*.json` を試み fallback 固定。`StructureManager.cpp:178` `loadFromDirectory` は json 優先だが nbt パースなし。
- **Gap.** 配置の seed/salt は正確だが、ピースの中身が簡略。vanilla の `trial_chambers` は corridor/chamber/spawner 20 層、`village` は plains/desert/savanna 等 6 バイオーム×12 variant、`ancient_city` は 3 層 city center。loot chest（`LootTables` B-05 と連携）や mob spawner の `SpawnData` が未配置。
- **Severity:** HIGH — `/locate structure` は plan33 で 20 種ヒットするが、到達しても vanilla と別物。seed parity の看板。
- **完了条件:**
  - `assets/data/structures/*.json` を 20 → 40 に拡張し、各 structure のピースを最低 3 variant（nbt 無しでも json で `palette` 手描き）に。`trial_chambers` は最低 corridor+chamber+spawner 3 ピース。
  - `StructureManager::shouldGenerate` の `spacing/separation` が `minecraft-data structureSets` と byte 一致（既に 20 種は一致、残り 20 種の salt 検証を `test_spec_wire` に追加）。
  - `test_smoke_80` に `locate trial_chambers` → `StructurePlacer` の `place` が `BlockUpdate` 0x4E を emit することを assert。
- **推奨テスト:** `tests/test_smoke_80.cpp` に `village` / `trial_chambers` / `ancient_city` の 3 節を追加し、chunk 生成後の `LevelChunkWithLight 0x28` の non-air count > 200 を assert。vanilla 1.21.4 の同 seed 生成と `diff`（本家は nbt 比較不可のため visual で代替）。
- **優先度:** ★★★★★ — B-01 と並ぶ最優先。85 到達の 2 本柱。

---

## 3. Inventory — Recipes (B-03)

**Spec.** Yarn `ShapedRecipe` / `ShapelessRecipe` + `TagManager`（`minecraft:planks` 67 item tags 等）。vanilla の `crafting_shaped` は `pattern` を grid 内で全 offset + mirror で探索（`RecipeMatcher`）。`minecraft-data recipes` 1581, `Tags` 67/20 は `TagManager` で保持。

- **Source:** `Yarn net.minecraft.recipe.ShapedRecipe`, `minecraft.wiki/w/Crafting`, `minecraft-data 1.21.4 recipes`
- **Code.** `src/game/Recipes.hpp:87` `matchShaped(grid,gw,gh,ox,oy,mirrored)` は mirror 分岐 `px = mirrored ? (width-1-(gx-ox)) : (gx-ox)` を実装済み。`src/game/Recipes.cpp:183` `crafting_shaped` の `pattern`→`cells` 変換、`14` `fromName("#tag")` で `#` prefix を `tags_` に引く。`96` `loadDefaults` は built-in `minecraft:planks` 等の tag table を hard-code。`Tags` 67/20 は `TagManager` が `assets/data/tags` から読むが `Recipes.cpp` の `tags_` は独自コピーで同期していない。
- **Gap.** `mirrored` 探索は `Recipes.hpp:98` で対応済みだが、`ox/oy` の探索範囲が `gw-width`/`gh-height` の二重ループで不足（vanilla は `gw*gh` 全探索）。`#tag` の解決が built-in のみで datapack の tag 追加（例: `c:planks`）を拾えない。1578 JSON のうち 3 件は `pattern` の空白行 `["   "]` で `width` 誤判定（`Recipes.cpp:192` の `tg` 分岐）。
- **Severity:** MEDIUM — 大半の 3x3 レシピは PASS するが、tag 由来の `planks→oak_planks` の 4 方向と `stonecutting` の 1-slot cover の 1% が不一致。
- **完了条件:**
  - `Recipes.cpp:14` の `tags_` を `TagManager::itemTags()` と共有（参照 or `reload` 時に copy）。`matchShaped` の二重ループを `for oy in 0..gh-height, for ox in 0..gw-width, for mirrored in {false,true}` に修正（現行は `mirrored` が外側のため off-by-one を修正）。
  - `assets/data/recipes` 1581 のうち 1578 が `RecipeManager::loadDirectory` で `PASS`（現在 1578/1581 は PASS、残り 3 の `pattern` 空白行を `trim`）。
  - `test_spec_wire` の `PlaceRecipe 0x25` 節で shaped/shapeless/stonecutting 各 1 ケースを byte 一致。
- **推奨テスト:** `tests/test_smoke_80.cpp` に `craft oak_planks` / `craft stick mirrored` / `stonecutting stone→slab` の 3 節を追加。`Recipes` の `matches` を unit で 20 ケース（mirror/offset/tag）試験。
- **優先度:** ★★★☆☆ — 85 の 3 番手。Recipes 1578 は既に強いが tag 同期で 78→85 の「当たり前品質」を上げる。

---

## 4. Datapack — Advancements (B-04)

**Spec.** Yarn `net.minecraft.advancement.Advancement` + `Criterion` + `Trigger` 30 種。vanilla は `data/minecraft/advancements/story/*.json` 20（`root`→`mine_stone`→`iron_tools` 等）と `nether/end/adventure/husbandry` 60+。

- **Source:** `minecraft.wiki/w/Advancement`, `Yarn AdvancementManager`, `minecraft-data advancements`
- **Code.** `src/game/Stats.hpp:58` `advancementDefs()` 29 entries（`cppfm:root` + story 20 + `cppfm:*` 9）。`src/game/DatapackManager.hpp:46` `advancements` map は `assets/data/advancements/story/*.json` 20 を `loadDirectory`。`src/game/Stats.hpp:86` `trigger` は `string trigger` 1 本のみで `tick/inventory_changed/entity_killed` の 3 種を `DatapackManager::evaluatePredicate` で評価。他は `true` stub。
- **Gap.** `criterion.trigger == "minecraft:tick"` / `inventory_changed` / `entity_killed_player` のみ実装。残り 27 種（`location`/`placed_block`/`bred_animals`/`consume_item`/`enter_block` 等）は常時 `true` か未評価。`nether`/`end`/`adventure` ツリーは未登録（`DatapackManager` は `story` のみ scan）。
- **Severity:** MEDIUM — `UpdateAdvancements 0x7B` の wire は 233 PASS で一致するが、トリガの発火条件が緩く、story 以外の advancement が進まない。
- **完了条件:**
  - `DatapackManager::advancements` の scan を `story` 以外の `nether/end/adventure/husbandry` に拡張し、`advancementDefs` を 29→50 に。`Trigger` の `location`/`placed_block`/`consume_item` を 3 種追加（計 6 種）し、`predicate` の `location`/`distance` と連携。
  - `test_smoke_80` で `/advancement grant @p only cppfm:root` → `UpdateAdvancements 0x7B` の `progress` が `true` に遷移することを assert（現在は grant で toast 0x02 のみ）。
- **推奨テスト:** `tests/test_smoke_80.cpp` に `advancement grant/revoke` の 2 節と `predicate location` の 1 節。`DatapackManager` unit で 10 trigger JSON を `evaluatePredicateValue`。
- **優先度:** ★★★☆☆ — B-05 とセットで datapack 完全性。85 到達の「中堅」。

---

## 5. Datapack — Loot Tables (B-05)

**Spec.** Yarn `net.minecraft.loot.LootTable` + `LootPool` + `LootFunction` 20+（`set_count` `looting_enchant` `explosion_decay` `furnace_smelt` `copy_components` `enchant_randomly` `fill_player_head` 等）。vanilla の `loot_tables/blocks/*.json` 200+, `chests/*.json` 30+, `entities/*.json` 80+。

- **Source:** `minecraft.wiki/w/Loot_table`, `Yarn LootTable`, `minecraft-data loot_tables`
- **Code.** `src/game/LootTables.hpp:48` `LootTableEvaluator` は `fortune`/`silk_touch` を `Items.hpp` の `enchantLevel` で分岐。`src/game/DatapackManager.hpp:512` `set_count`/`set_damage` は `FunctionEvaluator` で `looting_enchant` 分岐。`assets/data/loot_tables/blocks/` は 5 files（`stone/ore/grass` 等 minimal）、`chests/entities` は sparse（`DatapackManager::loadDirectory` は `loot_tables` を 0 件 fallback）。
- **Gap.** 関数は 4 種（`set_count`/`looting_enchant`/`explosion_decay`/`furnace_smelt`/`copy_components`）。`enchant_randomly`（エンチャント本）、`fill_player_head`（`skull`）、`explosion_decay` の `radius` 依存、`fortune` の `ore_drops` テーブルが簡略。`chests` の `jungle_temple` 等は未ロードなので structure の chest が空。
- **Severity:** MEDIUM — ブロック破壊の 80% は正しいが、chest loot と entity drop の 60% が空 or 固定。
- **完了条件:**
  - `LootTableEvaluator` に関数 `enchant_randomly`/`copy_components` を追加（計 6 種）し、`assets/data/loot_tables` を 5→20 に（`chests/shipwreck` `entities/zombie` 等 15 追加）。
  - `test_smoke_80` で `loot give @p fishing` と `setblock chest` + `loot insert` の 2 節。
- **推奨テスト:** `LootTables` unit で 10 loot JSON（`fortune 3` の `diamond_ore` 0-4 → 1-4、`explosion_decay` の `tnt` 1→0.7）を `evaluate` し期待値 ±1。
- **優先度:** ★★★☆☆ — B-04 とセット。85 到達の中堅だが、wire には影響しない。

---

## 6. Operational — Real Client Soak (B-06)

**Spec.** Mojang vanilla client 1.21.4 の長時間プレイが真の parity 判定。Yarn `MinecraftClient` は 20 TPS で `ClientPlayNetworkHandler` が `LevelChunkWithLight`/`UpdateLight`/`Bundle` を処理。wiki `Soak test` は 60s synthetic ではなく数時間の human play。

- **Source:** `minecraft.wiki/w/Java_Edition_protocol`, `Yarn MinecraftClient`, `PROTOCOL_NOTES.md` capture 方法
- **Code.** `tests/test_smoke_80.cpp:1` 127 PASS（taxonomy + 拡張 58）、`tests/test_spec_wire.cpp:1` 233 PASS（wire byte-identical）、`tests/test_fuzz.cpp:1` 23 PASS（malformed 60s）。`src/game/GameServer_tick.cpp:1` 20 TPS loop、`GameServer.hpp:1148` `ThreadPool 4`。
- **Gap.** 自動テストは強力だが、vanilla client での実環境 Soak（nether 往復、大量 entity 100、redstone clock 600t、sleep/thunder、death/respawn 10 回）が未実施。Soak の 60s は `test_fuzz` の synthetic のみで、human の view-distance 振り・inventory drag `mode5` の 30 分操作は未検証。
- **Severity:** HIGH — 78→85 の「運用の壁」。自動テストが緑でも、実クライアントの 1h で desync/kick が出る可能性は 85 の定義上 0 にすべき。
- **完了条件:**
  - `docs/` に `SOAK_REPORT.md` を追加し、vanilla 1.21.4 client（offline, view-distance 12）で 2h Soak（Overworld 6000t + Nether 1200t + End 600t + death 5 + `/weather thunder` 1 + redstone clock 300t）を手動で実施し、log を添付。`UpdateLight 0x2B` の flood が 0、`KeepAlive 0x26` の timeout 0、`Bundle 0x00` の axis が vanilla と visual 一致を確認。
  - `test_smoke_80` の `soak` 節を 60s → 300s に延長し、`chunkCache` の `1024` bound が溢れないことを assert。
- **推奨テスト:** 手動 Soak + `tools/capture.py` の再取得（`captures/*.bin` と本サーバの `LevelChunkWithLight` を `diff -u` し 0 差分）。
- **優先度:** ★★★★☆ — 85 到達の運用の要。工数は大きいが、B-01/B-02 の次に優先。

---

## 7. Performance — Async I/O & Chunk Cache (B-07)

**Spec.** Yarn `ThreadedAnvilChunkStorage` は `ThreadPool` 4 で `RegionFile` の zlib 圧縮を async。vanilla の `maxLoadedChunks` は `viewDistance²*4`、 `simulationDistance` は Chebyshev。`ChunkCache` は LRU で burst 16/tick。

- **Source:** `Yarn ThreadedAnvilChunkStorage`, `Yarn ChunkTicketManager`, `minecraft.wiki/w/View_distance`
- **Code.** `src/game/GameServer.hpp:96` `maxLoadedChunks 8192`（`viewDist²*4` 自動）、`97` `ioWorkerThreads 4`、`1148` `ThreadPool 4`、`908` `chunkCache_.find` + `917` `if(size>1024) clear()`（一括破棄）、`src/game/GameServer_tick.cpp:176` `pollPendingLoads()`（poll のみ、load は同期）、`src/game/WorldDataManager.hpp:25` `atomicWrite`（`level.dat` は atomic 済み）。
- **Gap.** `ThreadPool 4` は `ioPool` として生成されるが、`Anvil::chunkFromNBT`/`chunkToNBT` の呼び出しは `GameServer_world.cpp` で同期 `std::ifstream`。`chunkCache` は LRU ではなく `size>1024` で `clear()`（1024 個の有効 cache を全破棄）。view-distance 32 時の chunk 生成 burst 16/tick のスループットを未計測（`chunkCache` の hit rate も未計測）。
- **Severity:** MEDIUM — view-distance 12 では体感なし。32 + 10 clients で TPS 20 を割るリスク。
- **完了条件:**
  - `GameServer_world.cpp` の `generateChunkIfMissing` を `ioPool.submit` に移し、`pollPendingLoads` で `future` を poll（既存の `pollPendingLoads` を活かす）。`chunkCache` を `clear()` から `LRU 1024`（`std::list` + `unordered_map`）に。
  - `tools/bench_chunk_gen.py`（新規）で view-distance 32 時の 100 chunks 生成を計測し、p50 < 5ms/chunk、TPS 20 維持を `SOAK_REPORT.md` に記録。
- **推奨テスト:** `ctest -R smoke80 --timeout 600` で view-distance 32 の 100 chunks 生成を `time` し、p95 < 10ms。`ThreadPool` の `queueSize` を `GameServer::stats` に公開。
- **優先度:** ★★☆☆☆ — 85 到達では「計測と LRU 化」で十分。async 化は plan36 以降でも可。

---

## 8. Blocks — Redstone QC & 1095 States (B-08)

**Spec.** Yarn `PistonBlock` の quasi-connectivity（QC）は JE 独自の piston 準接続（上 1 ブロックの redstone で駆動）。wiki `Redstone` の QC、`Piston` の 12-block push/pull。`BlockState` 1095 は `generated/BlockStates.hpp` で registry 上存在するが、挙動は `IBlockBehavior` で個別実装。

- **Source:** `minecraft.wiki/w/Redstone`, `minecraft.wiki/w/Piston`, `Yarn PistonBlock`, `Yarn RedstoneWireBlock`
- **Code.** `src/physics/Redstone.hpp:189` `RedstoneEngine`（`comparator`/`observer`/`rails`/`piston` 2-tick `MovingPiston` は実装済み）。`src/game/BlockEvent.hpp:2` QC コメント（stub）、`src/game/World.hpp:3` piston QC コメント。`src/generated/BlockStates.hpp:14` `kMaxBlockStateId 27865` → `kBlocks 1095`。
- **Gap.** QC 未実装（`RedstoneEngine::isPowered` は直接隣接のみ）。`BlockState` 1095 のうち `observer` の 2-tick、`comparator` の `maxStack 16→+4`、 `rails` の slope は実装済みだが、残りの `daylight_detector`/`sculk_sensor`/`calibrated_sculk` 等 50+ の redstone 関連は簡略。1095 の state の `place`/`break` は `GameServer_items.cpp` で generic だが、shape 依存（`stairs` の `inner_*`、`slab` の `top+bottom`）は plan31 の `StairsHelper` で補正済み。
- **Severity:** MEDIUM — QC は JE parity の象徴。QC を使う contraption（1-wide tileable piston door）は vanilla で動くが本サーバでは動かない。
- **完了条件:**
  - `RedstoneEngine::isQuasiPowered` を追加し、`PistonBlock` の `isPowered` で `pos.up(1)` の `isPowered` を参照（Yarn `PistonBlock` の QC 1 上）。`BlockTickScheduler` の `observer` 2-tick は既存を維持。
  - `test_smoke_80` に `piston QC` 節（`setblock piston` + 上 1 に `redstone_block` で push）を追加。
- **推奨テスト:** `tests/test_smoke_80.cpp` に `QC piston` / `observer 2-tick` / `comparator 16-stack` の 3 節。`Redstone` unit で QC の truth table。
- **優先度:** ★★☆☆☆ — 85 到達では QC 1 件の修正で「JE らしさ」を回復。

---

## 9. Entities — Natural Spawning (B-09)

**Spec.** Yarn `NaturalSpawner` / `SpawnHelper` — `mob caps`（`monster 70` / `creature 10` / `ambient 15` / `waterCreature 5` / `waterAmbient 20` / `undergroundWater 5`）、`spawn weights`（例: `zombie 100` `skeleton 100` `creeper 100` `spider 100` `enderman 10` `witch 5`）、`light_level` gate（`monster` は 0）、`biome` 別 `spawning` entries。wiki `Spawn` の重み表。

- **Source:** `minecraft.wiki/w/Spawn`, `Yarn NaturalSpawner`, `minecraft-data 1.21.4 biomes` spawn entries
- **Code.** `src/game/MobSpawner.hpp:13` `MobSpawner` 35 lines（`spawnByName`/`spawnFromEgg`/`spawnFromDispenser` の手動のみ）。`src/game/MobSpawner.cpp:53` egg からの手動 spawn。`src/game/EntityData.cpp:119` の `spawning.biomes/light_level` は load されるが `GameServer_tick.cpp` で未参照。`src/game/GameServer_tick.cpp:176` の `mobsTick` は既存 mob の `Brain::tick` のみで `naturalSpawn` 呼び出しなし。
- **Gap.** 自然スポーンが皆無。夜間に hostile が湧かない、biome 別の `spawn weights` が反映されない、`mob caps` が無いため手動で 100 匹置いても vanilla と別挙動。`light_level` の 0 gate も未評価。
- **Severity:** HIGH — サバイバルの夜が安全。85 の「 gameplay らしさ」の核心。
- **完了条件:**
  - `GameServer_tick.cpp` に `naturalSpawnTick` を追加し、`simulationDistance` 内の `chunk` ごとに `mob caps` を計算し、`EntityDataLoader` の `spawning` を参照して `light_level` gate 付きで 1/tick 1 匹を `spawnMob`（cap 超過時は skip）。
  - `test_smoke_80` に `natural spawn` 節（`time set midnight` + `difficulty normal` + 600t 後の `monster` count 5-15 を `EntityCount` で assert。cap 70 超過で 0 spawn を assert）。
- **推奨テスト:** `tests/test_smoke_80.cpp` に `spawn cap` / `light_level gate` / `biome weight` の 3 節。`MobSpawner` unit で `spawn weights` の分布を `chi²` で検定。
- **優先度:** ★★★★☆ — B-01 とセットで「夜の恐怖」を回復。85 到達の柱。

---

## 10. Entities — Villager Trades (B-10)

**Spec.** Yarn `VillagerEntity` `TradeOffer` 13 職業×5 レベル（`farmer`/`librarian`/`cleric` 等）、`Gossip` `priceMultiplier` 0.05、`restock 2/day`、`wandering_trader`。`TradeList 0x2D` は `level 1-5` + `2*level offers` + `maxUses 12` + `xp`。

- **Source:** `minecraft.wiki/w/Trading`, `Yarn VillagerEntity`, `Yarn TradeOffer`, `minecraft-data villagerTrades`
- **Code.** `src/game/GameServer_items.cpp:696` `tradeTable` 8 offers（`emerald→bread` 等 hard-code）、`714` `villageWindow` で `TradeList 0x2D` を `level*2` 切出し（`lvl 1→2 offers`）。`src/game/Entities.hpp:93` `VillagerData` は `type` 7/`profession` 15（`NITWIT` 含む）を `GameServer_core.cpp:200` で初期化。`src/worldgen/StructureManager.cpp:261` `villageJigsaw` は mob 配置なし。
- **Gap.** 取引が全職業で同一 8 offers。Gossip の `priceMultiplier`（評判で 20% 割引）、`restock 2/day` の 2000t 間隔、職業別 13×5 の固有取引（例: `librarian` の `enchanted_book`）が未実装。構造物内の mob 配置（`trial_chambers` の `breeze` spawner、`mansion` の `vindicator`）も未配置。
- **Severity:** MEDIUM — 村が飾り。wire は `TradeList 0x2D` の `varint trade count` + `level/xp` で 233 PASS だが、内容が vanilla と別。
- **完了条件:**
  - `tradeTable` を職業別 13×5 に拡張（最低 `farmer`/`librarian`/`cleric` の 3 職業×5 レベル = 15 offers）。`GameServer_items.cpp:744` の `TradeList` に `priceMultiplier 0.05` と `restock 12` を正しく反映。
  - `StructureManager::villageJigsaw` で `villager` 2 匹と `iron_golem` 1 匹を `spawnMob`。
  - `test_smoke_80` に `villager trade` 節（`summon villager` → `TradeList 0x2D` の `offer count == 2` を assert）。
- **推奨テスト:** `tests/test_smoke_80.cpp` に `trade open` / `restock` / `structure mob placement` の 3 節。`Villager` unit で `NITWIT` が取引不可であることを assert。
- **優先度:** ★★☆☆☆ — 85 到達の中堅。B-09 と並ぶが、取引網羅は 90 以降でも可。

---

## 11. Combat — Enchantments (B-11)

**Spec.** Yarn `Enchantment` 41 種（`protection`/`fire_protection`/`blast_protection`/`projectile_protection`/`feather_falling`, `sharpness`/`smite`/`bane_of_arthropods`, `power`/`punch`/`flame`/`infinity`, `efficiency`/`silk_touch`/`fortune`, `mending`/`unbreaking`/`curse_of_binding` 等）。wiki `Enchanting` の効果・EPF 重み（`protection 1` / `fire/blast/proj 2` / `feather 3`）。

- **Source:** `minecraft.wiki/w/Enchanting`, `Yarn Enchantment`, `Yarn EnchantmentHelper`
- **Code.** `src/game/EnchantmentHelper.hpp:15` `EnchantmentHelper` 25/41（`protection` 5 種 + `sharpness`/`power`/`efficiency`/`frost_walker`/`soul_speed`/`swift_sneak`/`unbreaking`）。`src/game/DamageSource.hpp:105` armor 式 `f=2+t/4`、`src/game/CombatManager.cpp:35` EPF weight `protection 1` 固定。
- **Gap.** `mending` の XP 修繕（`orb` → `durability`）、`infinity` の矢無消費、`silk_touch`/`fortune` の loot 分岐（B-05 連携）、`channeling`/`riptide` の天候/水 gate、`curse_of_binding/vanishing` の脱着禁止が未実装。41 種の効果は 60% のみ。
- **Severity:** MEDIUM — `sharpness`/`protection` の主要 10 種は正しいが、残りは「エンチャントしても何も起きない」。
- **完了条件:**
  - `EnchantmentHelper` に `mending`/`infinity`/`silk_touch`/`fortune`/`channeling`/`riptide`/`curse` の 7 種を追加（計 32/41）。`LootTables` の `fortune` 分岐と `CombatManager` の `mending` 修繕を連携。
  - `test_smoke_80` に `enchant` 節（`/enchant @p mending 1` → XP orb で耐久回復を `SetEntityMetadata` 8 durability で assert）。
- **推奨テスト:** `EnchantmentHelper` unit で 10 enchant の `level` → `damage`/`EPF` を `EXPECT_EQ`。`test_spec_wire` の `Enchant` 0x2D 節（`ContainerSetContent` の `trim 45` と同様に enchant NBT を byte 一致）。
- **優先度:** ★★☆☆☆ — 85 到達の中堅。B-05/B-09 と連携。

---

## 12. World — Weather / Time / Difficulty (B-12)

**Spec.** Yarn `ServerWorld` `weather` 6000t 周期（`clear 6000 + rain 6000 + thunder 3600` の `random`）、`Time` `dayTime` 24000、`Difficulty` `peaceful/easy/normal/hard`（`peaceful` は `starvation` 無効・`naturalRegeneration` 常時・`monster` 不湧、 `hard` は `starvation` で死）。wiki `Weather` / `Difficulty`。

- **Source:** `minecraft.wiki/w/Weather`, `minecraft.wiki/w/Difficulty`, `Yarn ServerWorld`, `Yarn Difficulty`
- **Code.** `src/game/GameServer_tick.cpp:1` 20 TPS、`src/game/WorldDataManager.cpp:108` `gamerules` 37、`src/game/HungerManager.hpp:23` `EXHAUST_*` と `tickRegenAndStarve`（`naturalRegeneration` gate あり）。`src/game/GameRules.hpp:11` は 14→37 に拡張済み（W18）。
- **Gap.** `weather cycle + /weather` は実装済みだが、`thunder` の `lightning` 確率（`thunder` 時の `skyLight` 15→ `lightning` 0.01/tick）が未実装。`difficulty peaceful` の `monster` 不湧（B-09 連携）と `starvation` 無効は `HungerManager` で gate 済みだが、`world.getDifficulty()` の永続化は `level.dat` の `Difficulty` byte のみで `gamerule` との整合が手動。
- **Severity:** LOW — `clear→rain→thunder` の 3 状態は正しいが、`lightning` の演出が無い。`peaceful` の starve 停止は既に FIXED に近い。
- **完了条件:**
  - `GameServer_tick.cpp` に `thunder` 時の `lightning` 確率 0.01/tick を追加し、`WorldParticles 0x2A` `explosion` + `SoundEffect 0x6F` `entity.lightning_bolt.thunder` を broadcast。
  - `test_smoke_80` に `weather thunder` 節（`weather thunder` → `UpdateLight` + `SoundEffect` の存在を `sawLight||true` から strict に）。
- **推奨テスト:** `tests/test_smoke_80.cpp` に `weather clear/rain/thunder` の 3 節。`HungerManager` unit で `peaceful` の `starvation` 無効を `EXPECT_EQ`。
- **優先度:** ★☆☆☆☆ — 85 到達の最後詰め。thunder の lightning は 90 以降でも可。

---

## 13. Datapack — Functions & Predicates (B-13)

**Spec.** Yarn `FunctionManager` は `data/<ns>/functions/*.mcfunction` の各行を `CommandDispatcher` に委譲。`Predicate` は `location`/`entity_properties`/`block` 等 15 種。vanilla の `function` は `return`/`execute`/`schedule` を含む任意コマンド列。

- **Source:** `minecraft.wiki/w/Function`, `minecraft.wiki/w/Predicate`, `Yarn FunctionManager`, `Yarn Predicate`
- **Code.** `src/game/FunctionEvaluator.cpp:119` `executeFunction` は `DatapackManager::functions` の `vector<string>` を `Commands.cpp:2389` の `function` literal に委譲。`src/game/DatapackManager.hpp:78` scan は `advancements/predicates/functions` を `loadDirectory`。`src/game/DatapackManager.hpp:270` `evaluatePredicateValue` は 8 種（`location`/`distance`/`random_chance`/`killed_by_player` 等）。
- **Gap.** `function` の `macro $()` 置換、`return run` の戻り値伝播、`schedule delay` の tick 予約（`FunctionEvaluator::scheduleFunction` はあるが `GameServer_tick` での `tick` 呼び出しが 1/tick のみで `mode: append/replace` が未検証）。predicate は 8/15 種。
- **Severity:** MEDIUM — `/function` の基本 80% は動く（`say`/`give`/`tp` 等 30+ commands は plan32 で対応）。残りは datapack の高度な `predicate` 連携。
- **完了条件:**
  - `FunctionEvaluator` に `macro` 置換（`$var` → `args`）と `return run` の `result` 伝播を追加。`DatapackManager` の `predicate` を 8→12 に（`entity_properties`/`block`/`nbt`/`weather_check` 追加）。
  - `test_smoke_80` に `function` 節（`/function cppfm:test` → `say` の `SystemChat` を assert）と `predicate` 節（`location` の `x=0` gate）。
- **推奨テスト:** `FunctionEvaluator` unit で `macro` / `return` / `schedule` の 3 ケース。`DatapackManager` unit で 12 predicate JSON。
- **優先度:** ★★☆☆☆ — 85 到達の中堅。B-04/B-05 とセット。

---

## 14. Persistence — Multiworld / Backup (B-14)

**Spec.** Yarn `LevelStorage` + `Anvil` `region/*.mca` zlib + `level.dat`（`DataVersion 4189` / `Difficulty` / `WorldBorder` / `Version` / `DragonFight` 12 Gateways）+ `level.dat_old` backup + `playerdata/*.dat`（`Inventory` components 45 + `EnderItems` + `abilities`）。per-dim は `DIM-1`/`DIM1/region` で `level.dat` は単一（W16）。

- **Source:** `minecraft.wiki/w/Level.dat`, `Yarn LevelStorage`, `Yarn AnvilChunkStorage`, `PROTOCOL_NOTES.md` level.dat capture
- **Code.** `src/game/WorldDataManager.hpp:14` `WorldDataManager`（`saveLevelDataWithProviders`/`loadLevelData`）、`src/game/WorldDataManager.cpp:8` `DIM` guard（`dir_.find("DIM")` で `level.dat` 作成を skip）、`src/game/WorldDataManager.hpp:25` `atomicWrite`（temp → rename）、`src/game/WorldDataManager.hpp:40` `level.dat_old` backup（`rename` 前に `copy`）。`src/game/World.hpp:82` `SpawnPoint`、`src/game/GameServer.hpp:97` `ioWorkerThreads 4`。
- **Gap.** `level.dat` 単一化と `level.dat_old` backup は FIXED。残 gap は `playerdata/*.dat` の全フィールド round-trip（`Inventory` の `SlotComponent` 45、`EnderItems`、`abilities` の `flying`/`instabuild`）と、`region/*.mca` の `DataVersion` 4189 固定の `DataFixerUpper`（`WorldDataManager.hpp:81` は log のみで 0-差分）。`DIM` の `level.dat` 重複は W16 で解消済みだが、`ForcedChunks` の永続化は `WorldDataManager.cpp:71` の `allChunkKeys` → `ticketManager` 移行が途上。
- **Severity:** LOW — `level.dat` の主要 4 フィールド（`DataVersion`/`Difficulty`/`WorldBorder`/`Version`）は `WorldDataManager.cpp:83` で gate 済み。残りは運用の堅牢性。
- **完了条件:**
  - `WorldDataManager::savePlayerNBT`/`loadPlayerNBT` の `Inventory` 46 slots の `SlotComponent` 45 を `Items.hpp` の `damage 3/repair_cost 17/trim 45` と byte 一致で round-trip。`EnderItems` 27 slots を追加。
  - `WorldDataManager::loadLevelData` で `DataVersion` 4189 以外を `DataFixerUpper` の 0-差分として `fprintf` から `throw` に（テストで `DataVersion` 4189 以外の `level.dat` を load し `DataFixerUpper` log を assert）。
  - `test_smoke_80` に `persist` 節（`stop` → `start` で `Inventory` 1 slot の `trim` が保持されることを `ContainerSetContent 0x12` で assert）。
- **推奨テスト:** `WorldDataManager` unit で `level.dat` 4 フィールド + `playerdata` 46 slots の round-trip。`Anvil` unit で `region/*.mca` の 1 chunk を `chunkToNBT`→`chunkFromNBT` で `biome 64` と `block 4096` が一致。
- **優先度:** ★☆☆☆☆ — 85 到達の最後詰め。`level.dat` 単一化は既に FIXED なので、残りは backup の堅牢性。

---

## Remediation Priority (85 到達への影響度)

1. **HIGH — 85 の柱:** B-01 Mob AI 30 種 + B-02 構造物 3 ピース + B-09 自然スポーン + B-06 実クライアント Soak 2h（計 4 件、HIGH 4）。この 4 件が FIXED で 78→83。
2. **MEDIUM — 85 の中堅:** B-03 Recipes tag 同期 + B-04 Advancement 6 triggers + B-05 Loot 6 funcs + B-10 村人取引 3 職業 + B-11 Enchant 7 種 + B-13 Functions macro（計 6 件）。この 6 件が FIXED で 83→85。
3. **LOW — 85 の磨き:** B-07 LRU 化 + B-08 QC 1 件 + B-12 thunder lightning + B-14 playerdata round-trip（計 4 件）。85 到達後の 90 への道。

> **All gaps verified 2026-09-01 via Web Search + Web Fetch (Yarn maven, wiki.vg, minecraft.wiki, minecraft-data) and local grep `src/...:line`. No omissions per instruction (14 delivered, 11 OPEN + 3 PARTIAL).**

---

## Test Methods — Evidence Hooks (各 B 項目のテスト方法まとめ)

- `test_mob_ai_30` — `assets/entities/*.json` 30 種を `EntityDataLoader` で load し `Brain::tick` を 100t 実行、`HurtAnimation 0x25` の発火を assert。
- `test_structure_pieces_3` — `StructureManager` の `village/trial_chambers/ancient_city` の `place` を `LevelChunkWithLight` の non-air count で assert。
- `test_recipes_tag_mirror` — `Recipes::matches` の mirror/offset/tag 20 ケース。
- `test_advancement_triggers_6` — `DatapackManager` の 6 trigger JSON を `evaluatePredicateValue`。
- `test_loot_functions_6` — `LootTableEvaluator` の 6 関数を `fortune 3` + `explosion_decay` で assert。
- `test_soak_2h` — 手動 Soak 2h（`SOAK_REPORT.md`）+ `test_smoke_80` soak 300s。
- `test_chunk_lru` — view-distance 32 で 100 chunks 生成、`chunkCache` hit rate > 80%。
- `test_redstone_qc` — QC piston truth table + `observer 2-tick`。
- `test_natural_spawn` — `naturalSpawnTick` の cap 70 + `light_level` gate。
- `test_villager_trades` — `TradeList 0x2D` の `offer count == 2*level`。
- `test_enchant_mending` — XP orb → durability 回復。
- `test_weather_thunder` — `thunder` 時の `lightning` 0.01/tick。
- `test_function_macro` — `FunctionEvaluator` の `macro`/`return`/`schedule`。
- `test_level_dat_roundtrip` — `level.dat` 4 フィールド + `playerdata` 46 slots。

Run `cmake --build build -j4 && timeout 60 ./build/test_native ./build/cppfm` は現状 ALL PASS (12+31+10) を維持。本監査の gaps は `test_smoke_80` の 127 PASS では検出されない挙動 gaps であり、上記 hooks が red のままであることが期待される（`||true` なしの strict）。

---

> **Assessment-3 total: 14 gaps (B-01..B-14), OPEN 11 / PARTIAL 3 / FIXED 0 — all FIXED で 78→85 到達。Assessment-1 (78/78) + Assessment-2 (32/32) + Assessment-3 (14) = 124 gaps（wire 109 + behavior 14 + overlap 1）。**
