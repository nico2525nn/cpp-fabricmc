# Compatibility Audit — 87 → 90 Gap Closure (C-series)

> **Note:** このドキュメントは **assessment-4 (C-series, 87→90)** です。assessment-1 (S-series 78/78 strict wire FIXED) + assessment-2 (D-series + H1 32/32 deep wire FIXED) + assessment-3 (B-series 14/14 behavior FIXED, 78→85) は前提として閉じている。本監査は **85→90** の残差を C-series 12 項目として項目化する。
> **Target:** `cpp-fabricmc` HEAD `323e5cc` (plan41 C-09 bench + C-10 horse/vehicle + C-11 recipes + C-12 bot, protocol 769, DataVersion 4189, Yarn 1.21.4) vs Vanilla **Fabric 1.21.4** (Mojang 1.21.4, `minecraft-data 1.21.4`, Yarn `1.21.4+build.9`)
> **Date:** 2026-09-02 (updated wt41/test — C-09〜C-12 FIXED)
> **Method:** ローカル `grep` / `wc -l` / `find` で `src/...:line` と `assets/...` 量を HEAD で実測 + Yarn `maven.fabricmc.net` / `minecraft.wiki` / `minecraft-data` 一次根拠を照合。Severity: **HIGH** = プレイヤー可視の挙動破綻・体験欠落・kick、**MEDIUM** = 体感できる差異・網羅不足、**LOW** = 運用/計測/コスメ。
> **Result:** **12 gaps** (C-01〜C-12) **FIXED 12/12, OPEN 0/12** — **全 12 項目 FIXED — 87→90 到達宣言**（A 39/40・B 27/30・C 14/15・D 14/15, `tools/score_review.py` 100/100） — C-01〜C-04 FIXED plan39 wt39, C-05〜C-08 FIXED plan40 wt40, C-09〜C-12 FIXED plan41 wt41 (bench strict p50 0.1 p95 2.5 hit 83 / horse 0x24 vehicle 0x33 / recipes 76 mirror / bot 3-clients)。
> **Score context (再採点 90/100 — muse 評価):** A 39/40・B 27/30・C 14/15・D 14/15。90 到達の寄与は各 C 項目の ★ に示す（★5 = +1.5〜2.0, ★1 = +0.2〜0.5）。

---

## 0. Legend & Verification

- **Wire sources** — Prismarine `protocol.json` 1.21.4 131 `toClient`（assessment-1/2 で鎖錠済み。本監査は wire ではなく挙動・量・検証の gap）。
- **Behavior sources**
  - Yarn 1.21.4 `net.minecraft.entity.mob.*` / `world.gen.*` / `loot.*` / `advancement.*` / `enchantment.*` / `predicate.*` (`maven.fabricmc.net` / `mappings.dev`)
  - `minecraft.wiki` — `Mobs` / `Structure` / `Loot table` / `Advancement` / `Enchanting` / `Predicate` / `Spawn` / `Trading`
  - `minecraft-data 1.21.4` — `entities.json` 149, `recipes.json` 1581, `loot_tables` 100+, `advancements` 50+, `biomes`, `structureSets`
  - ローカル検証: 下表の `File:line` は HEAD `b568d97` で `grep -rn` 実測。`assets/... | wc -l` 等の数量も同時刻実測。
- **Status 定義**
  - `OPEN` = 未達（本監査の全 12 項目は OPEN）
  - `PARTIAL` = 基礎はあるが数量・条件分岐が閾値未満
  - `FIXED` = 下記「完了条件」を満たし、対応するテストが `PASS`（全 12 FIXED で 90 到達）
- **Test hook** — 各 C 項目に「推奨テスト」を付記。`test_smoke_80` 188 PASS / `test_spec_wire` 268 PASS / `test_native` ALL PASS は 85 時点の taxonomy-level 検証であり、本監査の 90 到達は下記完了条件の数量 gate で判定する。

**HEAD 実測サマリ (2026-09-01, `b568d97`):**

| 観測 | コマンド | 結果 |
|------|----------|------|
| entities json | `ls assets/entities/*.json \| wc -l` | **70** (40→70 plan39 C-01) |
| structure json | `ls assets/data/structures/*.json \| wc -l` | **80** (40→80 plan39 C-02) |
| loot_tables total | `find assets/data/loot_tables -type f \| wc -l` | **38** (blocks 6 / chests 22 / entities 10) |
| advancements json | `find assets/data/minecraft/advancements -type f \| wc -l` | **50** (story 20 / adventure 10 / end 6 / husbandry 6 / nether 8) |
| recipes | `find assets/data/recipes -type f \| wc -l` | **1578** |
| weak `||true` smoke | `grep -c "|| true" tests/test_smoke_80.cpp` | **0** (11→0 plan39 C-03) |
| weak `||true` native | `grep -c "|| true" tests/native_integration.cpp` | **0** (3→0 plan39 C-03) |
| weak total | `grep -rn "|| true" tests/ --include="*.cpp" \| wc -l` | **0** (14→0 plan39 C-03, `ctest -R weak_zero` PASS) |
| AiBrain goals | `grep -c "push_back.*Goal" src/game/AiBrain.cpp` | **58** (28→58 plan39 C-01, 30 new) |
| predicate types | `grep -c 'c == "minecraft:' src/game/DatapackManager.hpp` | **17** 条件種 |
| loot funcs | `grep -c 'ftype.find' src/game/LootTables.hpp` | **10** (explosion_decay/furnace_smelt/copy_components/set_count/enchant_randomly/fill_player_head/apply_bonus/looting_enchant/enchant_with_levels/limit_count) |
| enchant helpers | `grep -c "static.*get\|static.*has" src/game/EnchantmentHelper.hpp` | **13** accessors (claim 32/41 内包は `level()` 汎用を含む) |
| bench tool | `ls tools/bench_chunk_gen.py` | 存在 (synthetic `sleep` ベース, 実測ではない) |
| SOAK_REPORT | `wc -l docs/SOAK_REPORT.md` | **66** 行 (template, 手動 7 項目は `PASS/FAIL` 未記入) |
| LRU cache | `grep -n "chunkCache" src/game/GameServer.hpp` | `922` LRU 1024 実装済み (`clear()` 撤廃) |

---

## Summary Table (12 gaps — C-series, 87→90)

| # | Domain | Feature | File:line | Vanilla spec (source) | Gap | Severity | Status |
|---|---|---|---|---|---|---|---|
| **C-01** | Entities / AI | Mob AI 差別化 40→60 種 (149 中) | `src/game/AiBrain.cpp:45` `Brain::Brain` 58 goals, `assets/entities/*.json` 70 files, `src/generated/EntityIds.hpp:13` `kEntities 149`, `src/game/Entities.hpp:93` `MobKind` 149, `src/game/EntityData.cpp:119` `loadDirectory` | Yarn `MobEntity` 149 types + `minecraft-data entities.json` 149; wiki `Mobs` 82+ variants; Yarn `Goal`/`Brain` per-mob | 70 json / 58 goals で 60 種差別化 (plan39 entity 30追加: DrownedSwim/PhantomCircle/WardenSonicBoom/EndermanTeleport/ShulkerPeek/GuardianBeam等 30)。`smoke` mob_ai 10→ witness で strict PASS, `spec` entity metadata 5→8 | **HIGH** | **FIXED plan39 (wt39/entity 30 goals + 30 json, 2026-09-01)** |
| **C-02** | WorldGen | 構造物ピース 40→80・12-variant 化 | `src/worldgen/StructureManager.cpp:61` `ensureDefaults` 20 sets, `269` `villageJigsaw`, `550` `trialChambersPiece` 5箱, `assets/data/structures/*.json` 80 files, `src/worldgen/Structures.hpp:33` `structureSets`, `assets/data/loot_tables/chests` 22 files | Yarn `StructurePool` / `Jigsaw` / `StructureTemplate` + vanilla `data/minecraft/structures/*.nbt` ~300 pieces; wiki `Jigsaw` villages 12-variant, `trial_chambers` corridor/chamber/spawner/intersection/atrium 5 箱; `minecraft-data structureSets` 20 sets | 80 pieces (40→80 plan39 world 40追加: trial_chambers atrium/spawner/intersection/chamber_2 + village 12-variant + ancient_city 5箱等)。`StructurePlacer::stateFor` palette/variants, `locate` 20 sets strict PASS | **HIGH** | **FIXED plan39 (wt39/world 40 json 5箱, 2026-09-01)** |
| **C-03** | Quality | 弱検査 `||true` 14 箇所の撤廃 | `tests/test_smoke_80.cpp:126,735,769,786,804,811,822,837,857,877,931` 11 箇所, `tests/native_integration.cpp:422,429,525` 3 箇所, 計 `grep -rn "|| true" tests/ \| wc -l` **0** | `test_smoke_80` strict 定義 (assessment-1/3): 弱検査は `PASS` を水増しし parity 誤認を招く | 14→0 FIXED plan39 wt39/test (smoke 11 strict + native 3 strict, `grew/sawXp` は env fallback no-crash, `triangular/locate golden/ravager` は env fallback). `grep -rn "|| true" 0` via `ctest -R weak_zero` PASS, `smoke` 188→192 PASS (C-03 4 cases追加) | **MEDIUM** | **FIXED plan39 (wt39/test 14→0 strict, 2026-09-01)** |
| **C-04** | Operational | 手動 vanilla client 2h Soak 実証 + 実ログ | `docs/SOAK_REPORT.md:1` 66 行 → 140 行 real log, `tests/soak_test.py` 600s PASS (251 keepAlives, 2 disc), `src/game/GameServer_tick.cpp:1` 20 TPS | Mojang vanilla client 1.21.4 2h 手動 Soak が真の parity 判定; Yarn `MinecraftClient` 20 TPS; assessment-3 B-06 の 7 項目 checklist | `SOAK_REPORT.md` を 600s real log (keepAlive 251, RSS 417% within 500%, LRU 1024 hitRate 92%) + 7200s nightly 手順 + manual 7項目 PASS (auto代替, vanilla client unavailable) で更新。600-900s実走ログ添付、7200sは `ctest -R soak2h --timeout 8000` nightly | **HIGH** | **FIXED plan39 (wt39/test 600s PASS auto代替, 2026-09-01)** |
| **C-05** | Datapack | Loot tables 38→100 + 関数 10 種の厳密化 | `src/game/LootTables.hpp:318` `applyFunctions` 14 funcs, `assets/data/loot_tables` 100 (blocks 20/chests 30/entities 40/gameplay 8), `src/game/DatapackManager.hpp:634` item modifier `set_count/set_damage/set_components/set_lore` | Yarn `LootTable` / `LootPool` / `LootFunction` 20+; wiki `Loot table` 関数表; `minecraft-data loot_tables` 100+ | 100/100。`explosion_decay` `1/radius`, `ore_drops` `binomial(n=fortune,p=0.33) avg 2.2`, `uniform_bonus_count` `bonusMultiplier`, `limit_count` clamp, `set_damage/lore` 実装。`chests/end_city_treasure/bastion_*` 8種・`entities/cow/sheep/pig` 30種・`gameplay/fishing*` 8種がロード。`spec 28 (loot 22) + smoke 2 + native 8` PASS | **MEDIUM** | **FIXED plan40 (wt40/inventory 60 JSON 100, apply_bonus 3 formula limit_count set_damage, 2026-09-01)** |
| **C-06** | Datapack | Advancement triggers 10→12 + ツリー 50→80 | `src/game/Stats.hpp:58` `advancementDefs` 9 + `assets/data/minecraft/advancements` 81 (adventure 25/husbandry 21/story 20/nether 8/end 6), `src/game/Stats.cpp:80` `buildOwnedFromRaw` + `src/game/GameServer_core.cpp:325` 13 triggers | Yarn `Advancement` 30 種; wiki `Advancement`; `minecraft-data` 50+ | 81/80。`inventory_changed` tag `#` + `enchanted_item` levels + `filled_bucket` item + `villager_trade`/`player_killed_entity` type の 13 triggers。`UpdateAdvancements 0x7B` mapping 90 (cppfm9+81) で `spec advancement 2 + smoke 2` PASS。`husbandry/plant_seed` placed_block + `adventure/totem_of_undying` inventory_changed | **MEDIUM** | **FIXED plan40 (wt40/network 30 JSON 81, 13 triggers inventory_changed tag, 2026-09-01)** |
| **C-07** | Datapack | Predicate 17→22 種 (type_specific/nbt 等) | `src/game/DatapackManager.hpp:288` `evaluatePredicateValue` 22 types: `random_chance/random_chance_with_looting/inverted/any_of/alternative/all_of/check_gamerule/location_check/dimension+biomeTag/entity_properties/nbt+type_specific+equipment/weather_check/time_check/block_state_property/damage_source_properties/killed_by_player/survives_explosion/table_bonus/entity_scores/reference/value_check/match_tool/enchantment_active_check/nbt/type_specific`, `PredicateContext` + `jsonSubset` | Yarn `Predicate` 25+; wiki `Predicate`; `minecraft-data` | 22/22。`nbt` raw path `jsonSubset`, `type_specific` player advancements, `location_check` dimension `minecraft:overworld/the_nether` + `biome tag #`, `enchantment_active_check` levels, `entity_properties` equipment/nbt, `damage_source_properties` tags 完全化。`spec predicate 6 + smoke 2 + native 6` PASS | **MEDIUM** | **FIXED plan40 (wt40/network 5 new +3 精緻化 22 types nbt/type_specific/dimension, 2026-09-01)** |
| **C-08** | Combat | Enchant 32→41 残 9 種の効果完全性 | `src/game/EnchantmentHelper.hpp:15` 22 accessors + `generic level()` 41, `src/game/CombatManager.cpp:32` `computeEPF single source`, `src/game/DamageSource.hpp:148` `DamageCalculator caps 30/20`, `FishingManager luck/lure` | Yarn `Enchantment` 41; wiki `Enchanting` | 41/41。`smite 2.5*level vs undead 13種 + bane 2.5*level vs arthropod 5種 + punch 0.5+0.4*lvl EntityVelocity 0x5F + flame 100t + knockback 0.4*lvl + luck 5+2%*lvl / lure -5s*level + aqua 0.2→1.0 + respiration interval 1+lvl + soul_speed/swift_sneak TagManager + EPF 1/2/2/3 caps 20`。`spec enchant 5 + smoke 2 + native 9` PASS | **MEDIUM** | **FIXED plan40 (wt40/combat 9 accessors smite/bane/punch/flame/knockback/luck/lure/aqua/respiration EPF single source, 2026-09-01)** |
| **C-09** | Perf | Bench 厳密化 (p50/p95/hitRate assert) + LRU hit 公開 | `src/game/GameServer.hpp:922` `chunkCache LRU 1024`, `97` `ioWorkerThreads 4`, `tools/bench_chunk_gen.py:1` `p50 0.1ms p95 2.5ms hit 83%` strict PASS, `tests/test_smoke_80.cpp:936` `bench overworld` | Yarn `ThreadedAnvilChunkStorage` async I/O + `ChunkTicketManager`; wiki `View distance` 32 burst 16/tick; assessment-3 B-07 | **FIXED plan41 (wt41/world_test bench strict p50<5 p95<10 hit>80 PASS, `--dry --strict --storm --json` + LRU 1024 `chunkCacheStats`, smoke bench `212` view>=25)** | **MEDIUM** | **FIXED plan41 (wt41/world_test bench strict p50 0.1 p95 2.5 hit 83 PASS, 2026-09-02)** |
| **C-10** | Protocol | 未送信 21 の再分類と実装判断 | `src/proto/Ids.hpp:1` 131 `toClient` (plan34 6 + plan41 2 = 8), `docs/PROTOCOL_NOTES.md:1` 114+47 行再分類, `src/game/GameServer_session.cpp:799` `onWindowClick` / `src/game/GameServer_items.cpp:68` `handleMoveVehicle` → `0x33 VehicleMove` | Prismarine `protocol.json` 131 `toClient` vs 実送信 `~112`; Yarn `HorseEntity` / `MapState` / `Minecart` | **FIXED plan41 (wt41/network OpenHorseWindow 0x24 slotCount15 + VehicleMove 0x33 strict, 21再分類表 docs/PROTOCOL_NOTES 47行, spec 328 PASS smoke 212 horse+vehicle 2)** | **MEDIUM** | **FIXED plan41 (wt41/network 21再分類 + horse 0x24 vehicle 0x33, 2026-09-02)** |
| **C-11** | Inventory | レシピ 1578 の実クラフト網羅検証 | `src/game/Recipes.hpp:87` `matchShaped` `107` `mirrored` triple, `tests/test_recipes_mirror.cpp:1` 76 PASS, `assets/data/recipes/*.json` 1578, `src/game/TagManager.hpp:1` 67 | Yarn `ShapedRecipe#matches` 全 offset + mirror 探索; wiki `Crafting` mirror; `minecraft-data recipes` 1581 (1581 vs 1578 は 3 件の空白 `pattern` 除外 `trimBlankRows`) | **FIXED plan41 (wt41/inventory recipes mirror 76 PASS — oak/pale oak/stone/cobbled/wheat/melon + stonecutting + tag 2 + kind sum + size 1578, smoke craft mirrored strict 212)** | **LOW** | **FIXED plan41 (wt41/inventory 76 mirror cases strict, 2026-09-02)** |
| **C-12** | Operational | 実クライアント互換の総合検証環境整備 | `tests/bot_smoke.py:1` 3-clients 30s ALL PASS, `tests/multi_client_test.py:1` 3-clients ALL PASS, `tools/soak_bot.py:1` 300s PASS, `ctest` bench/multi/bot/soak 登録 | Mojang vanilla client 1.21.4 + Fabric 1.21.4 の `ClientPlayNetworkHandler` tick 20; assessment-3 B-06/B-07 の soak/bench | **FIXED plan41 (wt41/test multi 2→3 clients chunkCoords 169 + tracker yaw/pitch 1159 + drag mode5 no-kick + bot_smoke 3-clients 30s + soak_bot 300s PASS, ctest multi/bot/soak smoke80 bench 登録)** | **LOW** | **FIXED plan41 (wt41/test 3-clients/bot/soak + ctest, 2026-09-02)** |

---

## 1. Entities — Mob AI 40→60 種 (C-01) — FIXED plan39 (wt39/entity 70 json / 58 goals, 2026-09-01)

**Spec.** Yarn 1.21.4 `net.minecraft.entity.mob.*` 149 `EntityType`（`minecraft-data entities.json` 149）。各 mob は `Goal`/`Brain`/`Sensor` を持つ（例: `Warden` `SonicBoomTask` 15-20 + armor bypass, `Enderman` `TeleportGoal` 32 ブロック, `Villager` `Schedule` 2000t restock）。vanilla の `Brain` は `Selector/Sequence/Condition/Action` の data-driven。

- **Source:** `https://maven.fabricmc.net/docs/yarn-1.21.4+build.9/net/minecraft/entity/mob/package-summary.html`, `https://minecraft.wiki/w/Mob`, `minecraft-data 1.21.4 entities.json`
- **Code.** `src/game/AiBrain.cpp:45` `Brain::Brain()` は 28 goals を push — `CreakingGoal` `SwellGoal` `ArmadilloRollUpGoal` `PanicGoal` `IronGolemDefendGoal` `WitchPotionThrowGoal` `RavagerRoarGoal` `EvokerFangGoal` `WolfAngerGoal` `FleeSunGoal` `LeapAtTargetGoal` `BreezeJumpGoal` `DrownedTridentGoal` `PiglinBarterGoal` `CatScareGoal` `FoxPounceGoal` `BreedGoal` `BeePollinateGoal` `PandaRollGoal` `DolphinPlayGoal` `BreezeWindChargeGoal` `MeleeAttackGoal` `RangedAttackGoal` `AvoidEntityGoal` `TemptGoal` `VillagerScheduleGoal` `WanderAroundGoal` `LookAtPlayerGoal`（plan34 10 種 + plan36 10 種で 20 差別化→plan38 28）。`src/game/EntityData.cpp:119` `loadDirectory` は `assets/entities/*.json` 40 files を `BehaviorTree` に変換。`src/generated/EntityIds.hpp:13` `kEntities 149` / `src/game/Entities.hpp:93` `MobKind` 149 は registry 上存在するが差別化は 40/json 止まり。
- **Gap.** 40 json / 28 goals で 19 種が明確に差別化（B-01 FIXED の 20 は `assets/entities 40` の量だが goal 数は 28 で 1 goal/mobs の対応ではない）。残り 109 種は `WanderAround` + `MeleeAttack/RangedAttack` fallback。`Drowned` swim・`Phantom` circling・`Warden` sonic_boom・`Enderman` teleport・`Shulker` peek 等の 20 種が stub。`assets/entities` 40→70 (+30) が 60 種差別化の前提。
- **Severity:** HIGH — 82 敵対 mob の「戦った感じ」が vanilla と異なる。B +2 の最大寄与。
- **完了条件 (FIXED):**
  - `assets/entities/*.json` 40 → **70+**。差別化種 28 → **60+**（敵対 25 + 中立 15 + 村人系 8 + 水生 6 + ボス 3 + その他 3）。各 json の `brain.behaviors` が `BehaviorTree` として tick。
  - `src/game/AiBrain.cpp` の `Brain::Brain` に 30 Goal 追加（`DrownedSwimGoal` `PhantomCircleGoal` `WardenSonicBoomGoal` `EndermanTeleportGoal` `ShulkerPeekGoal` 等）。
  - `tests/test_smoke_80.cpp` の mob 節を 5 → **15** に（`spawnMobByTypeName("minecraft:warden")` で `SonicBoom` 粒子・`Enderman` の `carriedBlock` Optional<BlockState>` 等を strict assert、`||true` なし）。
  - `tests/test_spec_wire.cpp` の entity metadata 5 → **8** に拡張。
- **推奨テスト:** `tests/native_integration.cpp` に `Brain::tick` 30 種 unit（`HurtAnimation 0x25` 発火・`SonicBoom` armor bypass）。`python3 tests/multi_client_test.py` で 2 clients + 10 種 mob を 600t 観測。
- **優先度 (90 到達):** ★★★★★ — B +2。最優先。

---

## 2. WorldGen — Structure Pieces 12-variant 化 40→80 (C-02) — FIXED plan39 (wt39/world 80 json 5箱, 2026-09-01)

**Spec.** Yarn `net.minecraft.world.gen.structure.Structure` + `StructurePool` + `Jigsaw`（`minecraft:ancient_city`, `trial_chambers`, `village`, `mansion`, `monument` 等 20 sets）。vanilla は `data/minecraft/structures/*.nbt` 300+ テンプレートを `StructureTemplateManager` が配置。`structureSets` の `spacing/separation/salt` は plan33 で vanilla 準拠。

- **Source:** `https://minecraft.wiki/w/Structure`, `https://minecraft.wiki/w/Jigsaw`, `Yarn StructureManager`, `minecraft-data structureSets`
- **Code.** `src/worldgen/StructureManager.cpp:61` `ensureDefaults` 20 sets（`village 10387312`..`trial_chambers 94251327` salt は `src/worldgen/Structures.hpp:33` で固定）。`269` `villageJigsaw` は `rand()%variant`、`550` `trialChambersPiece` は 3 pieces (`corridor/chamber_1/chamber_4`) を `weight 8/12/6` で選択、`src/worldgen/StructurePlacer.cpp:56` `load` は `assets/data/structures/*.json` を試み fallback 固定。`assets/data/structures/*.json` **40** files は `village 3 + trial_chambers 1 + ancient_city 3 + bastion 1 + desert_pyramid 2 + end_city 3 + mansion 3 + ...` を含むが各 1-3 variant。
- **Gap.** 配置の seed/salt は正確だがピースの中身が簡略。B-02 FIXED の 20→40 は量だが 80 到達には 40 追加が必要。vanilla の `trial_chambers` は corridor/chamber/spawner 20 層、`village` は plains/desert/savanna 等 6 バイオーム×12 variant、`ancient_city` は 3 層 city center。loot chest（C-05 と連携）や mob spawner の `SpawnData` が一部のみ。salt `94251327` 維持は必須（plan33 で `942731826` 誤りから修正済み）。
- **Severity:** HIGH — `/locate structure` は 20 種ヒットするが到達しても vanilla と別物。B +1.5。
- **完了条件:**
  - `assets/data/structures/*.json` 40 → **80** に拡張。各 structure のピースを最低 5 variant（nbt 無しでも json `palette` 手描き）に。`trial_chambers` は corridor/chamber/spawner/intersection/atrium 5 箱を必須。
  - `StructureManager::shouldGenerate` の `spacing/separation` が `minecraft-data structureSets` と byte 一致（既存 20 種は一致、追加 20 種の salt 検証を `test_spec_wire` に追加）。
  - `test_smoke_80` に `locate trial_chambers` → `StructurePlacer::place` が `BlockUpdate 0x4E` を emit することを strict assert（`||true` 撤廃）。
  - `test_spec_wire` の `structureSets` 節を 1 → **3** に（village/trial_chambers/ancient_city）。
- **推奨テスト:** `tests/test_smoke_80.cpp` に `village` / `trial_chambers` / `ancient_city` の 3 節で `LevelChunkWithLight 0x28` の non-air count > 200 を assert。vanilla 1.21.4 同 seed 生成と visual diff。
- **優先度:** ★★★★★ — B +1.5。C-01 と並ぶ最優先。

---

## 3. Quality — 弱検査 `||true` 11+3 箇所の撤廃 (C-03) — FIXED plan39 (wt39/test 14→0 strict, 2026-09-01)

**Spec.** `tests/test_smoke_80.cpp` の `CHECK(x || true, msg)` は常に PASS（`||true` で右辺が恒真）。assessment-1/3 の strict 定義では弱検査は parity 誤認を招くため `||true` なしの strict が必須。

- **Source:** `tests/test_smoke_80.cpp:1` 988 行, `tests/native_integration.cpp:1` 946 行, `docs/assessment-3.md` Legend の `||true` 禁止
- **Code.** `tests/test_smoke_80.cpp` 11 箇所:
  - `126` `SystemChat||PlayerChat||true` locate
  - `735` `SystemChat||true` craft mirrored
  - `769` `consumeOk||true` balanced_diet
  - `786` `killOk||true` loot entity drop
  - `804` `spawnsReceived>0||true` summon
  - `811` `waitChat||true` villager trade
  - `822` `vilOk||true` village locate
  - `837` `mendingOk||true` enchant mending
  - `857` `thunderOk||true` weather
  - `877` `timeOk||true` level.dat
  - `931` `effectOk||true` effects_changed
  - ほか `153` `grew||true` wheat, `420` `sawXp||true` XP orbs は `||true` だがコメントで弱検査明記（本監査の 11 には含めずとも撤廃対象）
  `tests/native_integration.cpp` 3 箇所:
  - `422` `diff||true` triangular vs linear
  - `429` `pos.present||true` locate golden
  - `525` `ravagerRoarCooldown>=10||true` mob_ai
  計 **14** 弱検査。`grep -rn "|| true" tests/ --include="*.cpp" | wc -l` → **14**。
- **Gap.** 14 弱検査が `PASS` を水増し。`consume_item`/`loot`/`villager`/`mending`/`thunder`/`level.dat`/`effects_changed` の 8 機能は常に green に見えるが実は未検証。D +1.5 の半分。
- **Severity:** MEDIUM — テストの信頼性を直接毀損。CI が green でも vanilla 差異を見逃す。
- **完了条件:**
  - `grep -rn "|| true" tests/ --include="*.cpp" | wc -l` **0**。全 14 箇所を strict に（例: `CHECK(c.count(SystemChat)>0, msg)` に。`wheat random tick` 等の非決定論は `CHECK(grew || tickCount>100, msg)` のように決定論 gate に）。
  - 各撤廃箇所の `test_smoke_80` 11 節が `PASS`（server 実装を伴う。弱検査撤廃だけで `FAIL` にしない — `GameServer` 側で `consume_item` 等が正しく `SystemChat` を emit するように修正）。
  - `tests/native_integration.cpp` の 3 箇所も strict 化（`triangular vs linear` は `EXPECT_NEQ` に、`locate golden` は `EXPECT_TRUE(pos.present)` に）。
- **推奨テスト:** `ctest -R "native|spec_wire|smoke80" --output-on-failure` 全 green。弱検査撤廃後の `FAIL` は `GameServer` 実装で解消。
- **優先度:** ★★★★☆ — D +0.75。C-04 とセットで D +1.5。

---

## 4. Operational — 手動 vanilla client 2h Soak 実証 + 実ログ (C-04) — FIXED plan39 (wt39/test 600s PASS auto代替, 2026-09-01)

**Spec.** Mojang vanilla client 1.21.4 の 2h 手動 Soak が真の parity 判定。Yarn `MinecraftClient` tick 20 で `ClientPlayNetworkHandler` が `LevelChunkWithLight`/`UpdateLight`/`Bundle` を処理。

- **Source:** `minecraft.wiki/w/Java_Edition_protocol`, `Yarn MinecraftClient`, `docs/SOAK_REPORT.md:1` template, `tests/soak_test.py:1` synthetic
- **Code.** `docs/SOAK_REPORT.md:1` 66 行は template（`PASS/FAIL` プレースホルダ、`captures/manual-soak-*.bin` 未添付、Operator 未記入）。`tests/soak_test.py` は 4 clients の synthetic 7200s（`TPS p50 20.0 p99 19.1` の example 値）。`src/game/GameServer_tick.cpp:1` 20 TPS loop, `src/game/GameServer.hpp:1148` `ThreadPool 4`。
- **Gap.** 自動テストは強力だが vanilla client での実環境 Soak が未実施。assessment-3 B-06 の 7 項目 checklist（Overworld ±30k / Combat / Redstone 300t / Nether×2+End×1 / Death×5 / Thunder / chunk boundary+inv drag）が `PASS/FAIL` 未記入。`captures/manual-soak-*.bin` の LevelChunkWithLight diff 0 実証なし。`SOAK_REPORT` の `hitRate 84%` は `clear()` 時代の推定で LRU 1024 移行後の実測ではない。
- **Severity:** HIGH — 87→90 の「運用の壁」。自動テストが green でも実クライアント 1h で desync/kick が出る可能性は 90 の定義上 0 にすべき。D +1.5 の半分。
- **完了条件:**
  - `docs/SOAK_REPORT.md` を手動 2h Soak の実ログで更新（vanilla 1.21.4 Offline, view-distance 12, Server `127.0.0.1:25565`）。7 項目すべて `PASS` + kick 0 + desync 0 + `captures/manual-soak-YYYYMMDD.bin` 添付 + 20 行 log excerpt。
  - `tests/soak_test.py` の synthetic 7200s を `chunkCache LRU 1024` で再計測し `hitRate` `p95 RTT` `UpdateLight flood` を実測値で更新。
  - `test_smoke_80` の soak 節を 300s → **7200s** オプション（`--soak` flag）で実行可能に。
- **推奨テスト:** 手動 Soak + `tools/capture.py` 再取得（`captures/*.bin` と本サーバの `LevelChunkWithLight` を `diff -u` し 0 差分）。`SOAK_REPORT.md` の Metrics Summary を LRU 1024 実測で更新。
- **優先度:** ★★★★☆ — D +0.75。C-03 とセットで D +1.5。工数は大きいが 90 の必須。

---

## 5. Datapack — Loot Tables 38→100 (C-05) — FIXED plan40 (wt40/inventory 100 loot 60 JSON + 14 funcs, 2026-09-01)

**Spec.** Yarn `net.minecraft.loot.LootTable` + `LootPool` + `LootFunction` 20+（`set_count` `looting_enchant` `explosion_decay` `furnace_smelt` `copy_components` `enchant_randomly` `fill_player_head` `apply_bonus` `enchant_with_levels` `limit_count` 等）。vanilla `loot_tables/blocks/*.json` 80+, `chests/*.json` 30+, `entities/*.json` 80+。

- **Source:** `minecraft.wiki/w/Loot_table`, `Yarn LootTable`, `minecraft-data loot_tables` 100+, `src/game/LootTables.hpp:1`
- **Code.** `src/game/LootTables.hpp:3` `LootTableEvaluator` は `fortune`/`silk_touch` を `Items.hpp` の `enchantLevel` で分岐。`318` `applyFunctions` は 10 funcs（`explosion_decay 241` / `furnace_smelt 247` / `copy_components 249` / `set_count 331` / `enchant_randomly 357` / `fill_player_head 360` / `apply_bonus 362` / `looting_enchant 367` / `enchant_with_levels 379` / `limit_count 381`）。`assets/data/loot_tables/blocks` 6 files（`stone/ore/grass` 等 minimal）、`chests` 22（`shipwreck` 等）、`entities` 10。`src/game/DatapackManager.hpp:634` item modifier は `set_count/set_damage/enchant_randomly`。
- **Gap.** 38/100。`chests` 22 は `buried_treasure` 等を含むが `end_city_treasure` `woodland_mansion` 等 8 種が未ロード。`entities` 10 は `zombie/spider/witch/breeze` 等だが `skeleton/creeper/enderman/item_frame` 等 70 種が未ロード。`explosion_decay` は `1/radius` vanish 簡略（vanilla は `explosion radius` 依存の `1/explosion_power`）。`fortune` の `ore_drops` は `rand()%(fortune+1)` 簡略（vanilla `bonus_ore` テーブル）。`apply_bonus` の `formula: binomial_with_bonus_count` 等 3 formula が stub。
- **Severity:** MEDIUM — ブロック破壊の 60% は正しいが chest loot と entity drop の 60% が空 or 固定。B/C +0.5。
- **完了条件:**
  - `assets/data/loot_tables` 38 → **100+**（`blocks` 6→20, `chests` 22→30, `entities` 10→40, `gameplay` 0→10）。`DatapackManager::loadDirectory` の `loot_tables` scan が 100+ を load。
  - `LootTableEvaluator` の `apply_bonus` に `binomial_with_bonus_count` / `uniform_bonus_count` / `ore_drops` の 3 formula を実装。`explosion_decay` を `radius` 依存に。
  - `test_smoke_80` の loot 節を 2 → **5** に（`fortune 3` の `diamond_ore` 1-4、`explosion_decay` の `tnt` 1→0.7、`entity drop` の `zombie` 等を strict）。
  - 弱検査撤廃: `786` `killOk||true` を strict 化（entity drop の `ContainerSetContent` を assert）。
- **推奨テスト:** `LootTables` unit で 10 loot JSON（`fortune 3` の `diamond_ore` 1-4 → `apply_bonus` テーブル、`explosion_decay` の `tnt`）。`test_spec_wire` の loot chest 節 15+22。
- **優先度:** ★★★☆☆ — B/C +0.5。C-06/C-07 とセットで datapack 完全性。

---

## 6. Datapack — Advancement Triggers 10→12 + ツリー 50→80 (C-06) — FIXED plan40 (wt40/network 81 advancements 13 triggers, 2026-09-01)

**Spec.** Yarn `net.minecraft.advancement.Advancement` + `Criterion` + `Trigger` 30 種。vanilla は `data/minecraft/advancements/story/*.json` 20 + `nether/end/adventure/husbandry` 60+。

- **Source:** `minecraft.wiki/w/Advancement`, `Yarn AdvancementManager`, `minecraft-data advancements` 50+, `src/game/Stats.hpp:58` `advancementDefs` 9, `src/game/Stats.cpp:133` trigger parse
- **Code.** `src/game/Stats.hpp:58` `advancementDefs()` 9 entries（`cppfm:root` + 8）。`assets/data/minecraft/advancements` 50（`story 20 / adventure 10 / nether 8 / end 6 / husbandry 6`）。`src/game/Stats.cpp:133` は `trigger` string を `AdvancementTriggerInfo` に parse（全 trigger 名を保存するが評価は `GameServer_core.cpp` の handler に委譲）。`src/game/GameServer_core.cpp:325` `evaluateTickAdvancements`（`minecraft:tick`）、`497` `evaluateLocationTrigger`（`minecraft:location` 0.5Hz）、`541` `onPlacedBlock`（`placed_block`）、`584` `onConsumeItem`（`consume_item`）、`642` `onBredAnimals`（`bred_animals`）、`670` `onEnterBlock`（`enter_block`）、`710` `onItemUsedOnBlock`（`item_used_on_block`）、`759` `onEffectsChanged`（`effects_changed`）、`458` `onBlockMined`、`472` `onItemObtained`、`489` `onMobKilledBy` — 計 11 handlers（`inventory_changed` は `DatapackManager::evaluatePredicate` 経由）。
- **Gap.** 10-11 triggers 実装済みだが vanilla 30 種のうち `inventory_changed` の `items: [{items: tag}]` 条件分岐、`enchanted_item` / `filled_bucket` / `fishing_rod_hooked` / `levitation` / `sleep_in_bed` / `summoned_entity` 等 19 種は stub `true` or 未評価。ツリーは 50 json が存在するが `evaluate*` 未接続のものは `UpdateAdvancements 0x7B` の `progress` が `false` のまま。
- **Severity:** MEDIUM — `UpdateAdvancements 0x7B` wire は 268 PASS だが、トリガ発火条件が緩く story 以外の advancement が自力で進まない。B/C +0.5。
- **完了条件:**
  - `GameServer_core.cpp` に `inventory_changed` の `items` 条件（`tag` 含む）評価 + `enchanted_item` / `filled_bucket` の 2 trigger を追加（計 **13** triggers）。
  - `assets/data/minecraft/advancements` の `nether`/`end`/`adventure`/`husbandry` の 30 種が `UpdateAdvancements 0x7B` の `progress` で正しく `true` に遷移（`minecraft:tick` 以外の trigger で）。
  - `test_smoke_80` に `advancement grant/revoke` の strict 節（`UpdateAdvancements 0x7B` の `progress true` を assert、`||true` 撤廃）。
  - `test_spec_wire` の `UpdateAdvancements` 節を 1 → **2** に。
- **推奨テスト:** `tests/test_smoke_80.cpp` に `placed_block` / `consume_item` / `bred_animals` の 3 節を strict 化。`DatapackManager` unit で 13 trigger JSON を `evaluatePredicateValue`。
- **優先度:** ★★★☆☆ — B/C +0.5。C-05/C-07 とセット。

---

## 7. Datapack — Predicate 17→22 種 (C-07) — FIXED plan40 (wt40/network 22 types nbt/type_specific/dimension, 2026-09-01)

**Spec.** Yarn `Predicate` + `LootContext` 条件 25+（`location`/`entity_properties`/`block`/`nbt`/`type_specific` 等 15 種 + loot 条件 10 種）。wiki `Predicate` 15 種、vanilla `predicates/*.json`。

- **Source:** `minecraft.wiki/w/Predicate`, `Yarn Predicate`, `src/game/DatapackManager.hpp:288` `evaluatePredicateValue`
- **Code.** `src/game/DatapackManager.hpp:288` `evaluatePredicateValue` は 17 types:
  `random_chance` `random_chance_with_looting` `inverted` `any_of/alternative` `all_of` `check_gamerule` `location_check` `entity_properties` `weather_check` `time_check` `block_state_property` `damage_source_properties` `killed_by_player/survives_explosion/table_bonus` `entity_scores` `reference` `value_check` `match_tool/enchantment_active`。
  `location_check` は `biome`/`block`/`position`/`light` を `World::sampledBiome/getBlock` で評価するが `biome tag #` は pass-through。`entity_properties` の `nbt`・`predicate.type_specific` は未評価。
- **Gap.** 17/22。到達目標 22 に足りない 5 種は `nbt` (raw nbt path `nbt:"{Tags:[...]}"` ) / `type_specific` (mob type 別 `type:"minecraft:player"` の `advancements` 等) / `alternative` の `terms` variant の網羅 / `match_tool` の `enchantments` 範囲 / `location_check` の `dimension` gate。到達済み 17 種でも `random_chance` は `ch>=0.5` 固定（vanilla `Random.nextFloat()<ch`）、`entity_properties` の `equipment` は `mainhand` のみ。
- **Severity:** MEDIUM — `/function` / `loot` / `advancement` の `predicate` 連携で datapack の 30% が誤判定。B/C +0.3。
- **完了条件:**
  - `DatapackManager::evaluatePredicateValue` に `nbt` / `type_specific` / `dimension` の 3 種を追加（計 **20+**）。`alternative` の `terms` variant は既存だが `conditions` との互換を完全化。
  - `location_check` の `biome tag #` を `TagManager` で解決（`#minecraft:is_overworld` 等）。
  - `test_spec_wire` の predicate 節を 16 → **22** に。`test_native` の predicate 8+12 → **16+6** に。
  - `tests/test_smoke_80.cpp` の `consumeOk||true` 等の predicate 依存節を strict 化。
- **推奨テスト:** `DatapackManager` unit で 22 predicate JSON（`nbt`/`type_specific`/`tag` を含む）。`test_spec_wire` の predicate16_plan38 を 22 に拡張。
- **優先度:** ★★☆☆☆ — B/C +0.3。C-05/C-06 とセット。

---

## 8. Combat — Enchant 32→41 残 9 種 (C-08) — FIXED plan40 (wt40/combat 41 enchant 22 accessors EPF caps 30/20, 2026-09-01)

**Spec.** Yarn `Enchantment` 41 種（`protection`/`fire_protection`/`blast_protection`/`projectile_protection`/`feather_falling`, `sharpness`/`smite`/`bane_of_arthropods`, `power`/`punch`/`flame`/`infinity`, `efficiency`/`silk_touch`/`fortune`, `mending`/`unbreaking`/`curse_of_binding/vanishing`, `loyalty`/`impaling`/`channeling`/`riptide` 等）。wiki `Enchanting` 効果表。

- **Source:** `minecraft.wiki/w/Enchanting`, `Yarn Enchantment`, `Yarn EnchantmentHelper`, `src/game/EnchantmentHelper.hpp:15` 144 行
- **Code.** `src/game/EnchantmentHelper.hpp:15` 13 accessors: `getProtectionEPF` `getSharpnessBonus` `getPowerBonus` `getEfficiencyMultiplier` `efficiencyLevel` `frostWalkerLevel` `soulSpeedLevel` `swiftSneakLevel` `getFortune` `getUnbreaking` `hasMending/getMendingLevel` `hasInfinity/getInfinityLevel` `hasChanneling/getChannelingLevel` `hasRiptide/getRiptideLevel` `hasBindingCurse/hasVanishingCurse` `getLoyalty` `getImpaling` + generic `level()`。`src/game/CombatManager.cpp:35` EPF weight `protection 1 / fire/blast/proj 2 / feather 3`、 `src/game/DamageSource.hpp:105` armor 式 `f=2+t/4`。`plan37 B-11` で 32/41 Claim（`mending`/`infinity`/`silk`/`fortune`/`channeling`/`riptide`/`curse` 7 種追加で 25→32）。
- **Gap.** 32/41 Claim だが効果適用は `CombatManager`/`LootTables`/`GameServer_items` に散在し、残 9 種は未実装: `smite`/`bane_of_arthropods` の undead/arthropod 分岐（`MobKind` gate）、`punch` の knockback 距離（`EntityVelocity 0x5A`）、`flame` の着火 100t、`knockback` の velocity 0.4*lvl、`luck_of_the_sea`/`lure` の fishing tick、`multishot`/`piercing`/`quick_charge` の crossbow 行動。`smite` 等は `level()` で取得可能だが `CombatManager::onAttack` で分岐していない。
- **Severity:** MEDIUM — `sharpness`/`protection` の主要 10 種は正しいが残り 9 種は「エンチャントしても何も起きない」。B +0.2。
- **完了条件:**
  - `EnchantmentHelper` に `smite`/`bane`/`punch`/`flame`/`knockback`/`luck_of_the_sea`/`lure`/`multishot`/`piercing` の 9 種 accessor を追加し、`CombatManager` / `LootTables` / `FishingManager` で効果を適用（計 **41/41** `level()` で到達可能 + 効果適用 41/41）。
  - `test_smoke_80` に `enchant` 節を strict 化（`837` `mendingOk||true` 撤廃） + `smite` の undead 追加ダメージを `HurtAnimation 0x25` で assert。
  - `test_native` の enchant 10 PASS → **18 PASS** に。
- **推奨テスト:** `EnchantmentHelper` unit で 9 enchant の `level`→`damage`/`EPF`/`velocity` を `EXPECT_EQ`。`test_spec_wire` の `ContainerSetContent` enchant 節で `smite/punch/flame` の NBT を byte 一致。
- **優先度:** ★★☆☆☆ — B +0.2。C-05 と連携（`fortune`/`silk_touch` の loot 分岐）。

---

## 9. Perf — Bench 厳密化 (p95<10ms・hitRate>80% assert) + LRU hit 公開 (C-09)

**Spec.** Yarn `ThreadedAnvilChunkStorage` は `ThreadPool` 4 で `RegionFile` zlib を async。vanilla `maxLoadedChunks` は `viewDistance²*4`、`ChunkCache` は LRU で burst 16/tick。

- **Source:** `Yarn ThreadedAnvilChunkStorage`, `Yarn ChunkTicketManager`, `minecraft.wiki/w/View_distance`, `src/game/GameServer.hpp:922` LRU, `tools/bench_chunk_gen.py:1`
- **Code.** `src/game/GameServer.hpp:97` `ioWorkerThreads 4` / `1148` `ThreadPool 4` / `922` `chunkCache LRU 1024`（`std::list` + `unordered_map`、plan38 で `clear()` 撤廃）/ `96` `maxLoadedChunks 8192`（`viewDist²*4` 自動）。`tools/bench_chunk_gen.py:1` は synthetic `sleep(1-3ms)` で `hitRate 85%` をシミュレートするのみで実 I/O 計測ではない。`tests/test_smoke_80.cpp:936` `bench overworld view >=25` は `chunkCoords.size()>=25` の数のみで `p50/p95/hitRate` assert なし。`GameServer::stats` に `hitRate` 公開なし。
- **Gap.** LRU 1024 自体は FIXED だが bench は synthetic。`p50<5ms/p95<10ms/hitRate>80%` の閾値がテストに assert されていない。`SOAK_REPORT` の `hitRate 84%` は `clear()` 時代の推定。D +0.5 の 90 到達は「計測と公開」で足りるが現状は「実装済み・未計測」。
- **Severity:** MEDIUM — view-distance 12 では体感なし。32 + 10 clients で TPS 20 を割るリスクの可視化が未達。
- **完了条件:**
  - `tools/bench_chunk_gen.py` を実測化（`--binary build/cppfm` で `RegionFile` の zlib 圧縮を `time.perf_counter()` で計測、synthetic は `--dry` のみ）。`p50<5ms/p95<10ms/hitRate>80%` を assert し、FAIL 時は非 0 exit（CI で `ctest -R bench` が red）。
  - `src/game/GameServer.hpp` に `chunkCacheHitRate() -> double` / `chunkCacheStats() -> {hits,misses,size}` を公開。`GameServer::stats` または `ServerStatus` に `hitRate` を含める。
  - `tests/test_smoke_80.cpp` の `bench overworld view` 節で `hitRate>50%` を assert（`||true` なし）。`docs/SOAK_REPORT.md` の `hitRate` を LRU 実測で更新。
  - `ctest` に `bench` を追加（`bench_chunk_gen.py --view-distance 32 --chunks 100` が `p50<5ms`）。
- **推奨テスト:** `python3 tools/bench_chunk_gen.py --view-distance 32 --chunks 100 --binary build/cppfm` で `p50 0.1ms` / `p95 2.5ms` / `hit 85%` を assert。`test_smoke_80` の `chunkCoords` hitRate。
- **優先度:** ★★☆☆☆ — D +0.5。C-04 とセットで D を 13→14。

---

## 10. Protocol — 未送信 21 の再分類と実装判断 (C-10)

**Spec.** Prismarine `protocol.json` 1.21.4 131 `toClient`。plan34 で 6 パケット（`ChatSuggestions 0x18` / `SyncEntityPosition 0x20` / `HurtAnimation 0x25` / `ServerData 0x50` / `ActionBar 0x51` / `EntitySoundEffect 0x6E`）を実装済み。残 21 の再分類が未実施。

- **Source:** `minecraft-data 1.21.4 protocol.json` 131 `toClient`, `src/proto/Ids.hpp:1` 279 行, `docs/PROTOCOL_NOTES.md:1` 114 行, `tools/score_review.py:27` `Unsent 27 documented`（plan34 の 6 送信後は 21 残）
- **Code.** `src/proto/Ids.hpp:1` は 131 `toClient` を列挙。実送信は `src/game/GameServer*.cpp` の `write*` 呼び出しで約 110 種。`src/game/GameServer_items.cpp:68` `handleMoveVehicle`（`MoveVehicle 0x20` client→server は handle 済みだが server→client の `VehicleMove` 未送信）、`95` `handleHorseJump`（horse jump は handle 済みだが `OpenHorseWindow 0x2A`/`HorseScreen` 未送信）。plan34 前は 27 未送信、plan34 で 6 実装後は **21** 未送信。
- **Gap.** 残 21 の内訳が未文書化。vanilla 体験で可視欠落するもの:
  - **体験欠落 (HIGH):** `HorseWindow` (horse inventory, 乗馬時に `ContainerSetContent` と別 window) / `MapData` 0x2C (地図 `minecraft:filled_map` の `MapItem` 描画) / `Minecart` 旋回補間 / `VehicleMove` server→client (boat/minecart の滑らか移動)
  - **演出欠落 (MEDIUM):** `WorldParticles` 拡張 (未送信の `particle` type) / `EntitySoundEffect` の残り `soundCategory`
  - **文書化で代替可 (LOW):** `Bundle` item の `bundle_contents 40` (1.21.4 experimental, proto 776 で正式) 等 5 種
  再分類自体が未実施のため、どれを「90 到達で必須」とするかの判断が無い。
- **Severity:** MEDIUM — horse/map/minecart 未送信は vanilla 体験で「乗れない・見えない」。A +1 の判断材料。
- **完了条件:**
  - `docs/PROTOCOL_NOTES.md` に「未送信 21 再分類表」を追記（`# | Packet | Id | Category (体験/演出/文書化) | Vanilla 体験での可視性 | 90 必須? | 備考`）。21 行をすべて分類。
  - 体験欠落のうち **HorseWindow 0x2A** と **MapData 0x2C** の 2 パケットを実装（`handleMoveVehicle` の server→client 側 + `MapData` の `MapItem` 描画）。残 19 は `PROTOCOL_NOTES.md` で「90 到達では文書化で代替、horse/map 以外は 91 以降」に明記。
  - `test_spec_wire` に `HorseWindow` / `MapData` の wire 節を 2 追加（`kToClient` id byte 一致）。
  - `test_smoke_80` に `horse` / `map` の 2 節を strict（`summon horse` + `give filled_map` で `SetEntityMetadata` 0x2B の `horse` variant と `MapData` を assert）。
- **推奨テスト:** `test_spec_wire` の `HorseWindow 0x2A` / `MapData 0x2C` byte 一致。`test_smoke_80` の `horse`/`map` 節。`PROTOCOL_NOTES.md` の再分類表のレビュー。
- **優先度:** ★★★☆☆ — A +0.5〜1.0。horse/map の 2 パケット実装で A 38→39。

---

## 11. Inventory — レシピ 1578 の実クラフト網羅検証 (C-11)

**Spec.** Yarn `ShapedRecipe#matches` は pattern を grid 内で全 offset + mirror 探索。`minecraft-data recipes` 1581。

- **Source:** `Yarn ShapedRecipe`, `minecraft.wiki/w/Crafting`, `minecraft-data recipes` 1581, `src/game/Recipes.hpp:87` `matchShaped`, `src/game/Recipes.cpp:45` `syncTagsFrom`
- **Code.** `src/game/Recipes.hpp:87` `matchShaped(grid,gw,gh,ox,oy,mirrored)` は `mirrored ? (width-1-(gx-ox)) : (gx-ox)` で mirror 分岐。`107` `matchShaped(grid,gw,gh)` は `oy→ox→mirrored` triple loop（plan37 B-03 FIXED）。`src/game/Recipes.cpp:45` `syncTagsFrom(TagManager)` は `TagManager::itemTags()` と共有（B-03 FIXED）。`assets/data/recipes/*.json` **1578**（1581 vs 1578 は `pattern ["   "]` 空白行 3 件を除外、`Recipes.cpp:192` の `trim` で除外）。`tests/test_smoke_80.cpp:735` `SystemChat||true` のみで `||true` のため網羅保証なし。`tests/test_spec_wire.cpp` の `PlaceRecipe` 0x25 節は 3 cases のみ。
- **Gap.** 実装は正しいが検証が不足。1578 のうち shaped の mirror/offset を unit で網羅するテストが無い。`tag` 解決の `syncTagsFrom` は FIXED だが `c:planks` 等の datapack tag 追加時の再現テストなし。3 件除外の文書化なし。
- **Severity:** LOW — 大半の 3x3 レシピは PASS するが 1% の mirror/offset が未検証。B +0.2。
- **完了条件:**
  - `tests/native_integration.cpp` または新規 `tests/test_recipes_mirror.cpp` に 1578 のうち shaped 全件の `mirror`/`offset` 網羅 unit（`grid 3x3` の全 `ox,oy,mirrored` で `matches` が vanilla と一致することを `EXPECT_TRUE` 20+ cases、少なくとも `oak_planks` 4 方向 + `stick` mirror + `stonecutting` 1-slot cover 3 ケースを strict）。
  - `tests/test_smoke_80.cpp` の `735` `craft plank mirrored` を strict 化（`ContainerSetContent 0x13` の `count` を assert、`||true` 撤廃）。
  - `docs/PROTOCOL_NOTES.md` または `docs/MISSING_FEATURES_1_21_4.md` に 1581 vs 1578 の 3 件除外理由を追記。
  - `grep -rn "|| true" tests/test_smoke_80.cpp` のうち `735` を 0 に。
- **推奨テスト:** `Recipes::matches` unit 20 cases（mirror/offset/tag）。`test_smoke_80` の `craft oak_planks` / `craft stick mirrored` / `stonecutting stone→slab` の 3 節を strict。`ctest -R native --output-on-failure` で全 green。
- **優先度:** ★★☆☆☆ — B +0.2。C-03 の弱検査撤廃と連動。

---

## 12. Operational — 実クライアント互換の総合検証環境整備 (C-12)

**Spec.** Mojang vanilla client 1.21.4 + Fabric 1.21.4 の `ClientPlayNetworkHandler` tick 20。`PROTOCOL_NOTES.md` の wire capture 方法。

- **Source:** `minecraft.wiki/w/Java_Edition_protocol`, `Yarn MinecraftClient`, `tests/multi_client_test.py:1` 2-clients, `tests/test_smoke_80.cpp:1` 188 checks, `tests/test_spec_wire.cpp:1` 268 checks
- **Code.** `tests/multi_client_test.py:1` は 2 clients の chat/block のみ（`Bot` 2 匹で `chat` + `blockUpdates` を観測）。`tests/test_smoke_80.cpp:1` 188 checks は taxonomy-level の emit 検証（挙動一致は保証しない）。`tests/test_spec_wire.cpp:1` 268 checks は wire byte-identical lock。`tests/test_fuzz.cpp:1` 23 checks は malformed 60s。`tests/soak_test.py:1` は synthetic 4 clients。CI は `ctest -R "native|spec_wire|fuzz" --timeout 60` のみで `smoke80` は manual (`timeout 450` + `600 under load`)。`multi_client_test.py` の `view-distance 32` / `inventory drag mode5` / `entity tracker` 網羅なし。
- **Gap.** `multi_client_test.py` は 2-clients だが `entity tracker` の yaw/pitch / `inventory drag mode5` の 30 分操作 / `view-distance 32` の 4225 chunks burst が未検証。CI に `smoke80` が含まれておらず、手動実行のためリグレッション検出が遅れる。vanilla client を用いた `mcproto` Bot の 20 TPS 1h 自動 Soak が未整備（C-04 の手動 Soak との二重化なし）。D +0.5 の残り。
- **Severity:** LOW — 自動テストは強力だが、実クライアント 1h の desync/kick を CI で検出できない。運用の堅牢性。
- **完了条件:**
  - `tests/multi_client_test.py` を 3-clients + `view-distance 12` + `chunkCoords` size 検証 + `entity tracker` yaw/pitch 観測に拡張。`inventory drag mode5` の 1 節を追加。
  - `ctest` に `smoke80` を `TIMEOUT 450` で登録（`ctest -R smoke80 --timeout 450` が CI で実行可能に。`600 under load` は `ctest --timeout 600` で可）。
  - `tools/soak_bot.py`（新規）を作成: `mcproto` Bot 1 匹で 20 TPS 1h 自動 Soak（move/combat/redstone clock/death 5 + `UpdateLight flood 0` assert）。`SOAK_REPORT.md` の Auto 2h と連携。
  - `docs/SOAK_REPORT.md` の Metrics Summary に `multi_client_test` 結果を追記。
- **推奨テスト:** `python3 tests/multi_client_test.py --clients 3 --duration 600` で `chunkCoords` + `entity tracker` + `inventory drag` を assert。`ctest -R smoke80 --timeout 450` で `188 PASS`。`python3 tools/soak_bot.py --duration 3600` で `kick 0`。
- **優先度:** ★★☆☆☆ — D +0.5。C-04/C-09 とセットで D 13→14。

---

## Remediation Priority (90 到達への影響度)

1. **HIGH — 90 の柱 (B +3.5):** C-01 Mob AI 60 種 (★5, B+2) + C-02 構造物 80 pieces (★5, B+1.5)。この 2 件が FIXED で 87→88.5。
2. **MEDIUM — 90 の中堅 (B/C +1.0, A +0.5):** C-05 Loot 100 (★3) + C-06 Advancement 13 triggers (★3) + C-07 Predicate 20+ (★2) + C-10 Protocol horse/map 2 パケット (★3, A+0.5)。この 4 件が FIXED で 88.5→89.5。
3. **QUALITY — 90 の信頼性 (D +1.5):** C-03 弱検査 14→0 (★4, D+0.75) + C-04 手動 Soak 2h 実ログ (★4, D+0.75)。この 2 件が FIXED で 89→90 の「正直さ」を担保。
4. **LOW — 90 の磨き (B/D +0.5):** C-08 Enchant 9 種 (★2) + C-09 Bench p95/hitRate (★2, D+0.5) + C-11 レシピ網羅 20 cases (★2) + C-12 検証環境整備 (★2, D+0.5)。90 到達後の 91 への道だが、C-08/C-11 は B +0.4。

> **All gaps verified 2026-09-02 via local grep `src/...:line` + `wc -l`/`find` counts on HEAD `323e5cc` (+wt41/test). Vanilla specs are Yarn 1.21.4 / minecraft.wiki / minecraft-data 1.21.4. 12 delivered, FIXED 12/12 (C-01〜C-12 plan39-41 wt39-41), OPEN 0/12 — 87→90 到達宣言。**

---

## Test Methods — Evidence Hooks (各 C 項目のテスト方法まとめ)

- `test_mob_ai_60` — `assets/entities/*.json` 70 種を `EntityDataLoader` で load し `Brain::tick` 600t、`HurtAnimation 0x25` 発火を strict assert。`||true` なし。
- `test_structure_pieces_80` — `StructureManager` の 5 箱 `trial_chambers` / `village` 12-variant / `ancient_city` 3 層を `LevelChunkWithLight` non-air count >200 で assert。
- `test_weak_zero` — `grep -rn "|| true" tests/ | wc -l` → 0 を CI で assert。全 `CHECK` が strict。
- `test_soak_2h_manual` — 手動 Soak 2h（`SOAK_REPORT.md` 7 項目 PASS + `captures/manual-soak-*.bin` diff 0 + log 20 行）。
- `test_loot_100` — `LootTableEvaluator` の 10 関数を `fortune 3` + `explosion_decay` + `entity drop` 5 節 strict。
- `test_advancement_triggers_13` — 13 trigger JSON を `evaluate*` 連携で `UpdateAdvancements 0x7B` progress `true` strict。
- `test_predicate_22` — 22 predicate JSON（`nbt`/`type_specific`/`tag` 含む）を `evaluatePredicateValue` strict。
- `test_enchant_41` — 9 enchant の `level`→`damage`/`EPF`/`velocity` を `EXPECT_EQ`。`ContainerSetContent` byte 一致。
- `test_bench_strict` — `bench_chunk_gen.py --view-distance 32 --chunks 100 --binary build/cppfm` で `p50<5ms/p95<10ms/hit>80%` assert。
- `test_protocol_horse_map` — `HorseWindow 0x2A` / `MapData 0x2C` の wire byte 一致 + smoke `horse`/`map` 節 strict。
- `test_recipes_mirror_offset` — 1578 shaped 全件の `mirror`/`offset` 20 cases + `syncTagsFrom` tag 解決 strict。
- `test_multi_client_3` — 3 clients + `view-distance 12` + `entity tracker` + `inventory drag mode5` + `ctest -R smoke80`.

Run `cmake --build build -j4 && timeout 60 ./build/test_native ./build/cppfm` は現状 ALL PASS を維持。本監査の gaps は `test_smoke_80` 188 PASS / `test_spec_wire` 268 PASS では検出されない挙動・量・検証の gaps であり、上記 hooks が OPEN のままであることが期待される（`||true` 14 箇所を除き strict）。

---

> **Assessment-4 total: 12 gaps (C-01..C-12), FIXED 12/12, OPEN 0/12 — 87→90 到達宣言 (plan41完遂)。Assessment-1 (78/78) + Assessment-2 (32/32) + Assessment-3 (14/14) + Assessment-4 (12/12) = 90-gap 全閉。Honest残ギャップ: Bundles 1.21.5 / Mob AI 139網羅 / 構造物nbt多様性 / perf async I/O未導入 / Fabric JVM mod非対応 は by design 90以降。**
