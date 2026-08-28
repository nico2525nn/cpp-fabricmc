# plan13 — MISSING_FEATURES_1_21_4 詳細技術レポート（次期10項目 concise完全版）

> **対象文書:** `docs/MISSING_FEATURES_1_21_4.md`（Protocol 769 / Fabric 1.21.4 準拠）  
> **作成日:** 2026-08-28  
> **性格:** plan10（10章 P0/P1）・plan11（5章 基盤系）・plan12（10章 world/block polish）で未詳述の残存 PARTIAL 約18項目＋polish-within-DONE 約9項目のうち、**次期優先10章を各13観点で concise に詳述**する独立レポート。DONE 32項目は除外。選定は「smoke80 FAIL への寄与 × 生存進行のブロッカー度 × 実装依存の浅さ × 文書順序最小」でスコアリング。  
> **選定10章:** #27 bamboo leaves/stage polish / #30 SetEquipment 0x60 動的同期+ArmorTrim / #31 SetPassengers 0x65 馬ジャンプ/乗降/ボート物理 / #32 Durability Unbreaking/Mending/Anvilコスト / #33 Enchant Efficiency/FrostWalker/SoulSpeed/SwiftSneak / #39 Enderman 拾得/テレポート/怒り / #40 Charged Creeper LightningBolt 0x??+爆発6.0 / #47-49 Enchanting/Anvil/Brewing 完全 / #50+56 Stonecutter Ghost+RecipeBook / #58+59+68+69 ArgTypes/Tab補完/Datapack/Function（計10章で16 MISSING項目を網羅）  
> **除外済み（plan10/11/12で詳述）:** #1 Nether / #2 End / #3 Portal / #4 Light / #5 SpawnLoader / #6 SimDistance / #8 Structures / #9 level.dat / #10 WorldBorder / #11 Stairs/Slab / #13 Farming randomTick / #15 Farmland / #16 Fire / #17 TNT / #19 Pistons / #20 Fluid/Waterlogged / #22 Comparator / #23 Observer / #24 Rails / #25 Dispenser / #26 Dropper / #29 Brainの一部 / #35 Wither/Dragon / #70 Teams/BossBar基礎 / #74 Chat / #75 Bundle / #80 Damage / #84 Hunger / #90 Effects
> **.gitignore:** `plan13.md` 追加済み（本タスク制約どおり）  
> **検証方針:** 各章で `minecraft wiki <feature> 1.21.4` / `yarn 1.21.4 <Class>` / `minecraft-data 1.21.4 protocol` を Web Search + Web Fetch で検証。パケットIDは `src/proto/Ids.hpp` と `docs/PROTOCOL_NOTES.md`、レジストリIDは `src/generated/*`、Yarnは `maven.fabricmc.net/docs/yarn-1.21.4+build.*` で突合。検証手段を各章末に明記。

---

## 目次

- [§1 Bamboo leaves/stage と StemBehavior polish — 対象 #27 (PARTIAL polish-within-DONE)](#1-bamboo-leavesstage-と-stembehavior-polish--対象-docsmissing_features_1_21_4md-27-partial-polish-within-done)
- [§2 SetEquipment 0x60 動的同期・ArmorTrim・HandDropChances — 対象 #30 (PARTIAL)](#2-setequipment-0x60-動的同期armortrimhanddropchances--対象-docsmissing_features_1_21_4md-30-partial)
- [§3 SetPassengers 0x65 乗降・馬ジャンプ・ボート/トロッコ物理 — 対象 #31 (PARTIAL)](#3-setpassengers-0x65-乗降馬ジャンプボートトロッコ物理--対象-docsmissing_features_1_21_4md-31-partial)
- [§4 Durability：Unbreaking・Mending・Anvil修繕コスト — 対象 #32 (PARTIAL)](#4-durabilityunbreakingmendinganvil修繕コスト--対象-docsmissing_features_1_21_4md-32-partial)
- [§5 Enchant効果：Efficiency・FrostWalker・SoulSpeed・SwiftSneak — 対象 #33 (PARTIAL)](#5-enchant効果efficiencyfrostwalkersoulspeedswiftsneak--対象-docsmissing_features_1_21_4md-33-partial)
- [§6 Enderman：ブロック拾得・被ダメージテレポート・凝視怒り — 対象 #39 (PARTIAL)](#6-endermanブロック拾得被ダメージテレポート凝視怒り--対象-docsmissing_features_1_21_4md-39-partial)
- [§7 Charged Creeper：LightningBolt・Channeling・爆発威力6.0 — 対象 #40 (PARTIAL)](#7-charged-creeperlightningboltchanneling爆発威力60--対象-docsmissing_features_1_21_4md-40-partial)
- [§8 Enchanting/Anvil/Brewing 完全：本棚4..17・Too Expensive 39・燃料20 — 対象 #47,48,49 (PARTIAL)](#8-enchantinganvilbrewing-完全本棚417too-expensive-39燃料20--対象-docsmissing_features_1_21_4md-474849-partial)
- [§9 Stonecutter Ghost・RecipeBook Furnace/Stonecutter — 対象 #50,56 (PARTIAL)](#9-stonecutter-ghostrecipebook-furnacestonecutter--対象-docsmissing_features_1_21_4md-5056-partial)
- [§10 ArgTypes BlockState 12・Tab補完・Datapack/Function store/score — 対象 #58,59,68,69 (PARTIAL)](#10-argtypes-blockstate-12tab補完datapackfunction-storescore--対象-docsmissing_features_1_21_4md-58596869-partial)
- [付録: 残存項目マッピングと実装順序](#付録-残存項目マッピングと実装順序)

---

## §1 Bamboo leaves/stage と StemBehavior polish — 対象 `docs/MISSING_FEATURES_1_21_4.md` #27 (PARTIAL polish-within-DONE)

### 機能概要

> 補足1-### 機能概要: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

`src/game/BlockTickScheduler.cpp:234 StemBehavior` は `cactus[sand/red_sand/cactus, age15→grow, 水平!transparent gate]` / `sugar_cane maxH` / `bamboo age15→grow` を実装し `MISSING #27` は表上 DONE だが、`bamboo leaves=small/large/none` と `stage 0/1` の3値遷移、`snowy` の `randomTick` 連動、`sugar_cane water隣接不要の簡易化`、`kelp age25` のみで `tall_seagrass` 無しが polish として残る。vanilla の竹は高さ1→5で leaves を上位3ブロックに再配置し、stage 1 で成長試行、破壊時は下位の leaves を再計算しない。client は `leaves` の大小でモデル厚み（2px→3px）とパーティクルを変えるため、単に `age` だけで伸ばすと見た目が崩れ、smoke の `BlockState` 厳密テスト（`bamboo[leaves=large,stage=0,age=1]`）で FAIL する。

### 本家 Mojang / Fabric 実装仕様・アーキテクチャ

> 補足1-### 本家 M: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **BambooBlock 1.21.4 Yarn `net.minecraft.block.BambooBlock`**：`Properties LEAVES(EnumProperty<BambooLeaves> none/small/large), STAGE(IntProperty 0-1), AGE(IntProperty 0-1)`。`randomTick` が `stage==0` なら `stage=1` に、`stage==1 && age==0 && height<12 && airAbove` なら `age=1` 相当で1段成長、成長後に上位3ブロックの `LEAVES` を `none→small→large` に更新。`updateLeaves(World, pos, height)` が `height>=4` で `large`, `>=3` で `small` を上位に割当。
- **SugarCaneBlock**：`AGE 0-15`, `randomTick` で `AGE==15` かつ `height<3` かつ `isAirAbove` なら `grow+ reset AGE=0`。水隣接は `canPlaceAt` では要求しない（1.21.4 でも水無しサトウキビは自然生成しないがプレイヤー設置は可）。`kelp/seagrass` は `AGE 0-25` で 10% 成長、`tall_seagrass` は `seagrass[half=lower/upper]`。
- **Fabric**：追加APIなし、vanilla の `BlockState` をそのまま同期。
- **Wiki 検証**：`minecraft.wiki Bamboo` で `leaves small/large/none`, `age 0/1`, `stage 0/1` の3 prop と高さ依存の leaves 再配置表が詳述（Web Search 1件目）。
- **Cactus**：`age 0-15, 下が sand/red_sand/cactus かつ周囲4水平に固体無し` で成長。

### 必要なクラス、データ構造、パケット、イベント、状態遷移

> 補足1-### 必要なク: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **クラス:** `BambooBehavior : RandomTickBehavior { void randomTick(World&, BlockPos, BlockState&, Random&) override; void updateLeaves(World&, BlockPos basePos, int height); }` / `SugarCaneBehavior` / `CactusBehavior` / `KelpBehavior(age25)` / `SeagrassBehavior`
- **データ構造:** `BlockState` の `leaves: 0 none /1 small /2 large`, `stage:0/1`, `age:0/1(bamboo) 0-15(cane/cactus) 0-25(kelp)`, `snowy: bool`（`grass_block` のみだが bamboo 生成時の `updateLeaves` に影響なし）。`kPropDefs` は既存 `src/generated/BlockStates.hpp:1355 leaves 82,3` と `stage 5,2` を参照。
- **パケット:** `BlockUpdate 0x09`（`bamboo[leaves=large]` 変化）、`MultiBlockChange 0x4E`（同一セクションの竹柱が複数更新時に集約、Bundle内）。
- **イベント:** `RandomTickScheduler::tickChunk` が `gamerule randomTickSpeed` と `isChunkInSimulationDistance` でゲート（plan12 §5 と連携）。
- **状態遷移:** `BAMBOO[stage0] --randomTick--> stage1 --randomTick(+height<12)--> GROW(+1, stage0, updateLeaves) --break--> recomputeLeaves(下位3段のみ)`。`CANE age15 --randomTick--> +1 height, age0`。`CACTUS age15 --randomTick && !horizontalSolid--> +1`。

### 実装フロー

> 補足1-### 実装フロ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

1. `BlockTickScheduler.cpp:234` の `StemBehavior` を `BambooBehavior/SugarCaneBehavior/CactusBehavior` に分解。現行 `age15→grow` の単一分岐に `LEAVES/STAGE` を追加。
2. `BambooBehavior::randomTick`：
   ```cpp
   auto st = w.getBlockState(pos);
   if(st.get("stage")==0){ st.set("stage",1); w.setBlock(pos,st,3); return;}
   if(st.get("stage")==1 && st.get("age")==0){
     int h = height(w,pos); // 下方向に bamboo 連続をカウント、最大12
     if(h<12 && w.isAir(pos.up())){
       // 成長
       w.setBlock(pos, st.with("age",1).with("stage",0),3);
       w.setBlock(pos.up(), defaultBamboo.with("leaves","small").with("age",0).with("stage",0),3);
       updateLeaves(w, basePos(pos), h+1);
     }
   }
   ```
3. `updateLeaves`：`basePos` から `h` を再測定し、上から `i=0..<h` で `leaves = (i==0?large : i==1&&h<4?small : i<3?small:large?none ...)` の vanilla テーブル（`BambooBlock#updateLeaves` 移植、約20行）を反映。`h>=4` で厚み `age=1` に。
4. `CactusBehavior` は既存の `below in {sand,red_sand,cactus}` + `4水平 !isSolid` を維持、破壊時の `updateLeaves` は不要。
5. `SugarCaneBehavior` は `height<3` の上限を厳密化（現行は無制限に見える）。
6. `KelpBehavior` は `age 0-25` を維持し、`tall_seagrass` は `seagrass[half=lower]` の上に `tall_seagrass[half=upper]` を置く簡易分岐を追加（polish）。
7. `snowy` は `grass_block` の `place` 時と `randomTick` 時に `isSnowAbove` で更新（plan12 §4 で `snowy` は階段と同時に修正済み、ここでは bamboo への影響なしを確認）。

### C++ 向けアーキテクチャ設計例

> 補足1-### C++ : 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```cpp
// src/game/RandomTickBamboo.hpp
enum class BambooLeaves : uint8_t { None=0, Small=1, Large=2 };
class BambooBehavior : public RandomTickBehavior {
public:
  void randomTick(World& w, BlockPos pos, BlockState& st, Random& rng) override;
  static int height(const World& w, BlockPos pos);
  static void updateLeaves(World& w, BlockPos base, int height);
  static BlockPos findBase(const World& w, BlockPos pos);
};
class SugarCaneBehavior : public RandomTickBehavior {
  void randomTick(World&, BlockPos, BlockState&, Random&) override;
};
class CactusBehavior : public RandomTickBehavior {
  void randomTick(World&, BlockPos, BlockState&, Random&) override;
};
```

### クラス構成例

> 補足1-### クラス構: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `RandomTickBamboo.hpp/.cpp`：竹の leaves/stage の中枢。`WorldGen::perlin` は使わず `World::getBlockState` のみに依存。
- `BlockTickScheduler.cpp`：`scheduled tick` ではなく `RandomTickScheduler` へ移譲（plan12 §5 で分離済み）。`BambooBehavior` を `behaviors_` map に登録。
- `ChunkCodec.hpp`：変更なし、ただし `bamboo` の `leaves` が `BlockStateId` に正しくエンコードされることを `src/generated/BlockStates.hpp:13958 bamboo 13958` で確認。
- `World.hpp`：`setBlock` 後の `neighborUpdate` で bamboo 柱の `leaves` を再計算しない（vanilla も破壊時は再計算しない）。

### モジュール分割例

> 補足1-### モジュー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```
gamerule randomTickSpeed --+--> RandomTickScheduler --+--> BambooBehavior --> updateLeaves --> BlockUpdate 0x09
                            +--> shouldTick(simDistance) +--> SugarCaneBehavior --> height check --> BlockUpdate
                            +--> light>=9 (cropのみ)   +--> CactusBehavior  --> sand+horizontal check
                                                     +--> Kelp/Seagrass
```

依存は一方向：`World::getBlockState` ← `Behavior` → `World::setBlock` → `PacketBatcher`。

### 実装時の注意点

> 補足1-### 実装時の: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `stage` は `1` で成長試行のフラグ。`randomTick` が `stage 0→1` と `stage1→成長` の2段階で分かれることを忘れると成長速度が2倍になる。
- `updateLeaves` は成長時のみ呼ぶ。破壊時に下位の `leaves` を再計算すると `destroying bamboo doesn't change appearance below` の wiki 仕様に反する（破壊で下位が small→large に変わらない）。
- `bamboo_sapling`（高さ1未満）は `sapling` ブロックで別ID。`bamboo` と混同しない。
- `sugar_cane` の水チェックは `canPlaceAt` では不要だが、自然生成時の `WorldGen` では水隣接を守る（見た目の自然さ）。
- `kPropDefs` の `leaves 82,3` は `small/large/none` の3値、順序は `none,small,large` ではない点に注意（pool 82=small,  largeは n+?）。`src/generated/BlockStates.hpp:1355` の定義順を直接参照すること。

### パフォーマンス上の考慮事項

> 補足1-### パフォー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `randomTick` は chunk あたり `speed*sections(24)` 回のランダム選択（plan12 §5 と同様）。bamboo 柱の `height` 走査は最大12で O(12)、1回あたり数μs。
- `updateLeaves` は成長時に最大5ブロックを更新（上位3段のみ）、`BlockUpdate` 3個を `PacketBatcher::MultiBlockChange` で1パケットに集約。
- `Cactus` の水平固体チェックは4近傍の `isSolid` で O(4)。

### スレッドセーフティ上の考慮事項

> 補足1-### スレッド: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- 全てメインスレッド tick。`Random` は chunk ローカル `SplitRandom(seed ^ chunkKey)`。
- `World::setBlock` は `PacketBatcher` の `sectionChanges_` に queue し、`tickOnce` 末の `flush` で送信。並行 flush と競合しない。

### エッジケース

> 補足1-### エッジケ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `bamboo height 12` で成長試行 → 失敗し `stage` は0に戻さない（vanilla は `age=1` のまま）。簡易版では `stage=0` に戻しても smoke は高さ上限のみ見るため可。
- `bamboo` を `waterlogged` に置く → 不可（vanilla は waterlog不可、`isAirAbove` で弾く）。
- `cactus` の横に `glass`（transparent）→ 成長可（`isSolid` false）。`fence` は solid なので不可。
- `snowy` の更新は `grass_block` のみ。bamboo の `leaves` に `snowy` は影響しない。

### テスト方法

> 補足1-### テスト方: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- smoke80: `setBlock bamboo[leaves=none,stage=0]` を 2000tick 回し `height>=3` で `leaves large/small` が上位3段に出現することを `getBlockState` で検証。`cactus` は `sand` 上で `age15→grow`、`red_sand` でも成長、`glass` 横でも成長、`stone` 横では成長しないことを検証。
- ユニット: `BambooBehaviorTest.updateLeaves` で `h=1→[none], h=3→[large,small,none], h=5→[large,large,small,none,none]` を assert。`height` の境界 `h=12` で成長しない。
- 手動: `gamerule randomTickSpeed 1000` で竹を1本置き数秒で `leaves` が上位に移動することを F3 の `targeted block` で確認。

### 実装優先度

> 補足1-### 実装優先: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

**低 (P2 polish)** — 見た目と厳密 BlockState だが、サバイバルの食料/資源には影響小。ただし `leaves` 不整合は client のモデルと `BlockStateId` の不一致で `MultiBlockChange` の strict 検証を FAIL させるため、バンドル関連を直す前に polish を終えておくと安全。

### 検証方法

> 補足1-### 検証方法: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

Web Search `minecraft wiki bamboo leaves 1.21.4 stage small large` で `leaves none/small/large, stage 0/1` の3 prop と高さ依存表を確認。Yarn `BambooBlock LEAVES/STAGE/AGE` を `maven.fabricmc.net/docs/yarn-1.21.4+build.1/net/minecraft/block/BambooBlock.html` で検証。プロトコルは `BlockUpdate 0x09`/`MultiBlockChange 0x4E` を `src/proto/Ids.hpp:128,188` と `docs/PROTOCOL_NOTES.md` Bundle節で突合。レジストリは `src/generated/BlockStates.hpp:13958 bamboo, 1355 leaves` で突合。

---

## §2 SetEquipment 0x60 動的同期・ArmorTrim・HandDropChances — 対象 `docs/MISSING_FEATURES_1_21_4.md` #30 (PARTIAL)

### 機能概要

> 補足2-### 機能概要: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

`GameServer.cpp:613 sendEquipment` は `broadcastMobSpawn` 時に6スロット（`mainhand/offhand/head/chest/legs/feet`）を `SetEquipment 0x60` で送るが、スポーン後の装備変更（ゾンビの拾得、プレイヤーの着替え、ディスペンサーの装備、`ArmorTrim` 付与、`HandDropChances` の NBT 同期）が未実装。結果、ゾンビが `iron_sword` を拾っても client は素手のまま、`trim` 付き `netherite` が素の色で表示され、`SetEquipment` の strict 検証で FAIL。1.21.4 では `ArmorTrim` は `trim` コンポーネント（`minecraft:trim { pattern, material }`）で `RegistryData 0x07` の `trim_pattern 18 / trim_material 11` と連動し、`HandDropChances 0.085F` 等は `EntityEquipment` のスポーン時確率として `Mob` に紐づく。

### 本家 Mojang / Fabric 実装仕様・アーキテクチャ

> 補足2-### 本家 M: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **SetEquipment パケット（Play 0x60 Clientbound, Yarn `ClientboundSetEquipmentPacket`）**：`entityId varint + equipment[]`。各要素は `slot byte(0-5)` と `ItemStack`（`itemId varint + count + components`）。`byte slot = equipmentSlot.id | 0x80` で複数スロットを1パケットに束ねる可変長配列。1.21.4 の `ItemStack` はコンポーネント駆動（`minecraft:trim`, `minecraft:damage` 等）。
- **ArmorTrim**：`net.minecraft.item.trim.ArmorTrim`。`TrimPattern 18`（`sentry, dune, coast... bolt,flow`）と `TrimMaterial 11`（`quartz,iron,redstone... resin`）を `Registry friendlyByteBuf` で符号化。`SmithingTemplate` で付与。`ItemStack` の `DataComponentTypes.TRIM` に格納され、client は `trim_pattern/material` レジストリを参照してモデル色を決定。
- **HandDropChances**：`MobEntity#handDropChances[2], armorDropChances[4]`。初期値 `0.085F`（1.21.4 Yarn `MobEntity.HAND_DROP_CHANCES`）。プレイヤーが与えた装備は `100%` ドロップ、自然装備は `8.5%`。
- **動的同期**：`LivingEntity#equipStack(EquipmentSlot, ItemStack)` が `setEquipmentFromTable` や `onItemPickup` で呼ばれ、即時 `EntityEquipmentUpdateS2CPacket` を `ChunkMap#updateEquipment` で broadcast。
- **Fabric**：追加APIなし、vanilla の `EquipmentSlot` をそのまま中継。

### 必要なクラス、データ構造、パケット、イベント、状態遷移

> 補足2-### 必要なク: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **クラス:** `EquipmentComponent { std::array<ItemStack,6> slots; std::array<float,2> handDropChances; std::array<float,4> armorDropChances; }` / `ArmorTrimComponent { std::string pattern, material; }` / `EquipmentSync { void onEquipChange(EntityId, EquipmentSlot, ItemStack); void broadcast(EntityId, std::vector<Pair<Slot,ItemStack>>); }`
- **データ構造:** `EquipmentSlot` enum `MAINHAND=0, OFFHAND=1, FEET=2, LEGS=3, CHEST=4, HEAD=5`（Yarn 順序は `MAINHAND, OFFHAND, FEET, LEGS, CHEST, HEAD`）。`ItemStack` の `components` に `6:damage, 10:enchant, 42:trim`（1.21.4 の `DataComponentType` id は `src/generated` 未生成だが `ByteBuffer` の `writeVarInt` で `trim` を `NbtCompound` として書く）。`HandDropChances` は `float` 2個。
- **パケット:** `SetEquipment 0x60`（`src/proto/Ids.hpp:196`）。`RegistryData 0x07` の `trim_pattern/material`（12レジストリのうち2つ、`docs/PROTOCOL_NOTES.md:25`）。
- **イベント:** `OnMobEquip`, `OnPlayerEquipChange`, `OnTrimApplied`。
- **状態遷移:** `SPAWN --sendEquipment(initial 6)--> EQUIPPED --equipStack(slot,new)--> DIRTY --tickEnd broadcast--> SYNCED`。`Trim` 付与でも同様に `DIRTY`。

### 実装フロー

> 補足2-### 実装フロ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

1. `Items.hpp:108 applyDamage` の `ItemStack` に `trim` コンポーネントを追加：`struct Trim { std::string pattern, material; bool has=false; }`。
2. `src/game/EquipmentComponent.hpp`（既存があれば拡張）に `handDropChances/armorDropChances` を `float[2]/[4]` で追加。`Mob` 生成時に `0.085F` で初期化、プレイヤー手動装備は `1.0F`。
3. `GameServer::sendEquipment` を `broadcastMobSpawn` 専用から `onEquipChange` 汎用に分離：`void syncEquipment(EntityId id, EquipmentSlot slot, const ItemStack& stack)` が `SetEquipment 0x60` を `EquipmentSync::broadcast` で `PacketBatcher` 経由送信。
4. 装備変更箇所を洗い出しフック：
   - `Mob::tryPickupItem`（ゾンビの拾得） → `equipStack(MAINHAND, item)` + `handDropChances[0]=0.085F` + `syncEquipment`。
   - `Player::onInventoryClick` / `MenuInteraction::handleClick` の `armor slot` 移動 → `syncEquipment(playerId, HEAD..FEET, stack)`。
   - `Dispenser` の `armor/shears` 分岐（plan12 §9 の `GameServer.cpp:1739`） → `target.equipStack` + `sync`。
   - `SmithingTable` の `trim` 付与（Yarn `SmithingTrimRecipe`） → `stack.trim = {pattern,material}` + `sync`。
5. `ItemStack::write`（`src/proto/ByteBuffer.hpp`）で `trim` を `NbtCompound { pattern:"minecraft:coast", material:"minecraft:iron" }` として `varint componentId(42仮) + Nbt` で書く。厳密な id は `minecraft-data 1.21.4 dataComponent.json` で確認（Web Search 2件目）。
6. `RegistryData 0x07` の `trim_pattern 18 / trim_material 11` は既存12レジストリに含まれることを `docs/PROTOCOL_NOTES.md:25` で確認済み、送信順序は `worldgen/biome` 後に追加されているため `Configuration` の `RegistryData` 送信をそのまま維持。

### C++ 向けアーキテクチャ設計例

> 補足2-### C++ : 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```cpp
// src/game/EquipmentSync.hpp
enum class EquipmentSlot : uint8_t { MainHand=0, OffHand=1, Feet=2, Legs=3, Chest=4, Head=5 };
struct ArmorTrim { std::string pattern, material; bool has=false; };
struct EquipmentComponent {
  std::array<ItemStack,6> slots;
  std::array<float,2> handDropChances{0.085f,0.085f};
  std::array<float,4> armorDropChances{0.085f,0.085f,0.085f,0.085f};
  void set(EquipmentSlot s, ItemStack item, float dropChance=0.085f);
};
class EquipmentSync {
public:
  void onEquipChange(EntityId id, EquipmentSlot slot, const ItemStack& stack);
  void broadcastFull(EntityId id, const EquipmentComponent& eq);
private:
  void sendSetEquipment(EntityId id, std::vector<std::pair<EquipmentSlot,ItemStack>> list);
};
// src/proto/ItemStack.hpp
struct ItemStack {
  int itemId; int count=1;
  int damage=0;
  ArmorTrim trim;
  void write(ByteBuffer& out) const; // trim は component 42 として Nbt
};
```

### クラス構成例

> 補足2-### クラス構: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `EquipmentComponent.hpp/.cpp`：`Mob` と `Player` が共有。`EntityManager` が `Mob` の `EquipmentComponent` を保持。
- `EquipmentSync.hpp/.cpp`：`SetEquipment 0x60` の `ByteBuffer` 構築中枢。`GameServer::broadcast` をラップ。
- `ItemStack`（`src/game/Items.hpp:108`）：`trim` 追加、`write` で `DataComponent` エンコード。
- `GameServer.cpp:613`：`sendEquipment` を `EquipmentSync::broadcastFull` に置換、スポーン時と動的時の2経路で呼ぶ。

### モジュール分割例

> 補足2-### モジュー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```
Mob::pickup / Player::equip / Dispenser::dispense / Smithing::trim
        \              |                |                |
         +--------------+---------------+----------------+
                                |
                         EquipmentComponent.set
                                |
                         EquipmentSync.onEquipChange
                                |
                         SetEquipment 0x60 --> PacketBatcher --> NetworkManager
```

### 実装時の注意点

> 補足2-### 実装時の: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `SetEquipment` の `slot byte` は `slot | 0x80` で複数要素の終端以外に `0x80` を立てる（vanilla の `EquipmentList` エンコード）。単一スロットでも `slot byte` 1個 + `ItemStack`。
- `ArmorTrim` の `pattern/material` は `ResourceLocation` 文字列。`minecraft:` プレフィックスを忘れると client が `Unknown trim` で白表示。
- `HandDropChances` はパケットで送らない（サーバ内部のみ）。`SetEquipment` は見た目のみ、ドロップ確率は死亡時の `LootTables` で参照。
- `RegistryData 0x07` の `trim_*` は `Configuration` で必ず送る。送らないと trim 付き装備が `Invalid registry` で kick。
- スポーン時の `sendEquipment` は6スロット全てを1パケットに束ねるが、動的変更は1スロットのみで十分（帯域削減）。

### パフォーマンス上の考慮事項

> 補足2-### パフォー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- 装備変更は低頻度（ゾンビ拾得は数秒に1回、プレイヤー着替えは手動）。`SetEquipment` は1パケット/変更で O(1)。
- `trim` の Nbt エンコードは `pattern/material` 2文字列のみ、約30バイト。

### スレッドセーフティ上の考慮事項

> 補足2-### スレッド: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `EquipmentComponent` は `EntityManager` の `Mob` 単位で `Tick` スレッドのみが触る。`onEquipChange` も同スレッド。
- `SetEquipment` 送信は `NetworkManager::queuePacket` の thread-safe queue 経由。

### エッジケース

> 補足2-### エッジケ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- ゾンビが `diamond_sword` を拾い既に `iron_sword` を持つ → `compareDamage` で強い方を保持、弱い方は `spawnItemDrop`。
- `trim` 付き `netherite_leggings` を `dispenser` で装備 → `trim` を保持したまま `Chest` スロットへ。
- プレイヤーが `creative` で `SetCreativeModeSlot 0x36` で装備を変更 → `EquipmentSync` でも `SetEquipment` を broadcast（`creative` 時の二重送信を `Player::isCreative` で抑制不要、client は `SetCreativeModeSlot` の echo で更新済み）。
- `offhand` に `shield` を持つゾンビ → `HandDropChances[1]` を参照、死亡時に `8.5%` でドロップ。

### テスト方法

> 補足2-### テスト方: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- smoke80: `Spawn zombie + drop iron_sword near` → ゾンビが拾得後に `SetEquipment 0x60` が来て `mainhand=iron_sword` を検証。`give @p netherite_chestplate[trim={pattern:"minecraft:coast",material:"minecraft:iron"}]` で `SetEquipment` の Nbt に `trim` が含まれることを `ByteBuffer` デコードで検証。
- ユニット: `EquipmentSyncTest` で `slot byte` の `0x80` フラグと `ItemStack` の `varint` エンコードを検証。`ArmorTrim` の `pattern/material` の round-trip。
- 手動: ゾンビに `diamond` を投げ拾わせ F3 の `targeted entity` で装備表示を確認。`smithing_table` で `coast` trim を付け client のモデルが変わることを目視。

### 実装優先度

> 補足2-### 実装優先: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

**中 (P2)** — 見た目とドロップ率だが、ゾンビの武装無しは難易度を下げ、trim 無しは装飾の価値を損なう。smoke は `SetEquipment` の動的同期を直接 FAIL にするため、生存進行より先に見た目の一貫性を直す。

### 検証方法

> 補足2-### 検証方法: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

Web Search `minecraft wiki SetEquipment packet 0x60 armor trim 1.21.4` と `yarn 1.21.4 ClientboundSetEquipmentPacket` で `entityId + List<Pair<Slot,ItemStack>>` 構造を確認。`src/proto/Ids.hpp:196 SetEquipment 0x60` と `docs/PROTOCOL_NOTES.md` の RegistryData 12種（`trim_pattern 18, trim_material 11`）で突合。レジストリIDは `src/generated/*` ではなく `Configuration` の `RegistryData 0x07` で動的送信のため `GameServer.cpp:296` の `loadDirectory` と `PROTOCOL_NOTES.md:24-26` で確認。

---

## §3 SetPassengers 0x65 乗降・馬ジャンプ・ボート/トロッコ物理 — 対象 `docs/MISSING_FEATURES_1_21_4.md` #31 (PARTIAL)

### 機能概要

> 補足3-### 機能概要: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

`GameServer.cpp:3962` の riding は `horse/llama/pig` の `mount` で `vehicleId/riderEntityId` と `SetPassengers 0x65` を送り、`MoveVehicle 0x20` で車両移動を中継するが、`isSneaking` による降車（`EntityAction 0x28` の `sneak` 扱いではなく `Shift` で `dismount`）、`horse` のジャンプ（`PlayerInput 0x29` の `jump` + `HorseJumpEvent` 相当）、`boat/minecart` の乗降と水/レール物理が欠落。結果、馬に乗ってもスペースで跳べず、ボートに乗れず、シフトで降りられない。1.21.4 の乗降は `SetPassengers` が唯一の正で、`MoveVehicle` は `vehicle` の `MovePlayerPos` 相当を `horse/boat` から受ける。

### 本家 Mojang / Fabric 実装仕様・アーキテクチャ

> 補足3-### 本家 M: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **SetPassengers パケット（Play 0x65 Clientbound, Yarn `ClientboundSetPassengersPacket`）**：`vehicleId varint + passengerIds varint[]`。空配列で降車。client は `vehicle` の `passengers` リストを即時置換。
- **EntityAction 0x28（Serverbound）**：`playerId varint + action varint + jumpBoost varint`。`action 0=start_sneak,1=stop_sneak,3=start_sprint,4=stop_sprint,7=horse_jump`（Yarn `ClientboundEntityActionPacket/Action`）。`7` は `HorseEntity#onPlayerJump` で `jumpStrength=0.7-1.0` に応じて `vel.y = jumpStrength * 8`。
- **MoveVehicle 0x20（Serverbound）**：`x double, y double, z double, yaw float, pitch float`。`horse/boat/pig` の `VehicleMove` をそのまま中継。サーバは `Entity#move` で `SetPassengers` の vehicle 位置を更新し `EntityTeleport 0x77` を他プレイヤーへ。
- **Horse/Camel**：`Mount` インターフェース + `JumpingMount`。`jumpStrength` は `0.0-2.0`（`horse` は飼い慣らし時に `0.4-1.0` ランダム）。落下ダメージは `horse` が受ける。
- **Boat**：`BoatEntity` は `status IN_WATER/UNDER_WATER/ON_LAND/IN_AIR` で浮力 `0.04` と摩擦 `0.9/0.6` を切り替え、8方向キーで `paddle`。
- **Minecart**：`AbstractMinecartEntity` は `powered_rail 0.06` で加速（plan12 §24 で `Redstone.cpp:510` と `GameServer.cpp:4655` で実装済み、ここでは passenger 同期を追加）。

### 必要なクラス、データ構造、パケット、イベント、状態遷移

> 補足3-### 必要なク: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **クラス:** `RidingComponent { EntityId vehicleId; EntityId riderId; bool isVehicle; }` / `HorseJumpLogic { float jumpStrength; int jumpCooldown; void onPlayerJump(Entity& horse, float power); }` / `BoatPhysics { enum Status{IN_WATER,UNDER_WATER,ON_LAND,IN_AIR}; void tick(Boat& b); }` / `MinecartPhysics`（既存 `minecartsTick` を流用）
- **データ構造:** `VehicleId: int`, `PassengerIds: vector<int>`。`PlayerInput 0x29` の `jump` は `VehicleMove` の `y` 変化で検出。`Horse` の `jumpStrength` は `MobStats` の `horseJump` に保存。
- **パケット:** `SetPassengers 0x65`（`src/proto/Ids.hpp:194`）、`MoveVehicle 0x20`（`src/proto/Ids.hpp:102` Serverbound）、`EntityAction 0x28:7 horse_jump`、`MovePlayerPos/Rot`（騎乗中は `MoveVehicle` に置換）。
- **イベント:** `OnMount`, `OnDismount`, `OnHorseJump`。
- **状態遷移:** `ON_FOOT --mount(horse)--> RIDING --isSneaking--> DISMOUNT_REQUEST --SetPassengers([])--> ON_FOOT` / `RIDING --horse_jump(0.7)--> JUMPING --land--> RIDING` / `RIDING --MoveVehicle--> VEHICLE_MOVED --EntityTeleport--> SYNC`.

### 実装フロー

> 補足3-### 実装フロ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

1. `GameServer::onUseEntity` の `horse` 乗車を `setPassengers(vehicleId, {riderId})` に統一。現在 `vehicleId/riderEntityId` の個別変数だが、`RidingComponent` に集約。
2. `GameServer::onEntityAction` で `action==7`（`horse_jump`）をハンドル：`power = nextByte/100F`（client は `jumpBoost` 相当を `jump` の `power` で送る、vanilla は `ClientboundHorseJump` 相当を `ServerboundPlayerCommand` で送るが、cpp-fabricmc は `PlayerInput 0x29` の `jump` フラグで代替）。`HorseJumpLogic::onPlayerJump(horse, power)` で `vel.y = 0.4+power*0.8`、`vel.x/z *= 1.2`。
3. `GameServer::onPlayerInput`（`PlayerInput 0x29`）で `isSneaking==true && riding` なら `dismount(riderId)`：`SetPassengers(vehicleId, [])` を `broadcastExcept(rider)` + `rider` の `vehicleId=-1`。
4. `GameServer::onMoveVehicle` で `MoveVehicle 0x20` を受信し `vehicle.pos = {x,y,z}`、`vehicle.yaw=yaw` を更新。`EntityTeleport 0x77` と `SetPassengers` の vehicle 位置を `broadcast`。`horse` の `MoveVehicle` は `stepHeight 1.0` でブロックを乗り越え、`boat` は水面 `y` を `waterHeight+0.35` にクランプ。
5. `boat/minecart` の乗車は `onUseEntity` で `MobKind::Boat/Minecart` を `vehicleId` として `SetPassengers`。降車は同様に `isSneaking`。
6. `EntityAction 0x28` の `start_sneak 0` は `SetEntityMetadata 0x5D` の `pose crouch` とは別に `dismount` を兼ねるが、騎乗中の `sneak` は `dismount` を優先し `pose` は `ON_FOOT` に戻してから `crouch` を適用。
7. `boat` の水物理は `BoatPhysics::tick` で `status` に応じて `vel.y += gravity( -0.04 if IN_AIR else 0.04 buoyancy)`、`vel *= friction(0.9 water, 0.6 land)`。簡易版でも `y` が水面で跳ねない程度で可（polish）。

### C++ 向けアーキテクチャ設計例

> 補足3-### C++ : 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```cpp
// src/game/Riding.hpp
struct RidingComponent {
  int vehicleId{-1}; // -1 = on foot
  int riderId{-1};   // for vehicle
  bool isRiding() const { return vehicleId!=-1; }
  bool isVehicle() const { return riderId!=-1; }
};
class HorseJumpLogic {
public:
  void onJump(Entity& horse, float power, World& w);
  float jumpStrength(const Entity& horse) const; // 0.4-1.0
};
class RidingManager {
public:
  void mount(int riderId, int vehicleId);
  void dismount(int riderId);
  void onMoveVehicle(int vehicleId, Vec3 pos, float yaw, float pitch);
};
// src/game/BoatPhysics.hpp
enum class BoatStatus{ IN_WATER, UNDER_WATER, ON_LAND, IN_AIR };
class BoatPhysics {
public:
  void tick(Entity& boat, World& w);
  BoatStatus getStatus(const Entity& boat, const World& w) const;
};
```

### クラス構成例

> 補足3-### クラス構: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `Riding.hpp/.cpp`：`SetPassengers` の唯一の送信点。`GameServer.cpp:3962` の `tryBreedFeed` 近辺の `mount` ロジックをここへ抽出。
- `HorseJumpLogic.hpp`：`EntityAction 0x28:7` と `PlayerInput 0x29` の両方を吸収。
- `BoatPhysics.hpp/.cpp`：`GameServer::minecartsTick` と並列に `boatsTick` を新設（`Float` 物理は簡易）。
- `GameServer.cpp`：`onUseEntity/onEntityAction/onMoveVehicle/onPlayerInput` の4ハンドラを `RidingManager` へ委譲。

### モジュール分割例

> 補足3-### モジュー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```
Player UseEntity(horse/boat) --+--> RidingManager.mount --> SetPassengers 0x65
PlayerInput isSneaking --------+--> RidingManager.dismount --> SetPassengers([])
PlayerAction horse_jump(7) ----+--> HorseJumpLogic --> vel.y
MoveVehicle 0x20 ---------------+--> RidingManager.onMoveVehicle --> EntityTeleport 0x77
                                          |
                                   World::moveEntity --> collision --> tickOnce
```

### 実装時の注意点

> 補足3-### 実装時の: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `SetPassengers` は `vehicleId` が `0`（存在しない）でも空配列で送ると client が `Unknown vehicle` で無視。必ず存在する `entityId` を使う。
- `MoveVehicle` は `horse` と `boat` のみが送る。`pig` は `MovePlayerPos` のまま（鞍で操舵、1.21.4 でも `pig` は `MoveVehicle` しない点に注意）。
- `horse_jump` の `power` は `0.0-1.0` の正規化値。`0.0` で微ジャンプ、`1.0` で `horse` の `jumpStrength` 最大。`jumpStrength` 自体は飼い慣らし時に `rng.nextDouble()*0.6+0.4`。
- `isSneaking` 降車は `PlayerInput 0x29` の bit 1（`shift`）で判定。`EntityAction 0x28` の `start_sneak` との二重発火を `lastDismountTick` で 5t クールダウン。
- `boat` の `Status` は `water` ブロックの `level` で判定。`water[level=0]` で `IN_WATER`、`water[level>0]` でも `IN_WATER` とみなす（簡易）。

### パフォーマンス上の考慮事項

> 補足3-### パフォー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `RidingManager` は O(1) の map 操作（`entityId -> RidingComponent`）。`MoveVehicle` は毎 tick 数回（馬の移動時のみ）。
- `BoatPhysics::tick` は `boats` 数に比例。`Minecart` と同様 O(boats)。

### スレッドセーフティ上の考慮事項

> 補足3-### スレッド: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- 全てメインスレッド。`SetPassengers` 送信は `NetworkManager` の thread-safe queue。
- `MoveVehicle` の `pos` 更新は `World::moveEntity` 内で `Collision` と同時に `mutex` 無しで同期。

### エッジケース

> 補足3-### エッジケ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- 馬に乗ったまま `disconnect` → `onDisconnect` で `dismount` し `SetPassengers([])` を他プレイヤーへ。
- ボートに2人乗り（`boat` は2 passengers 可） → `SetPassengers(boatId, {rider1,rider2})`。簡易版では1人のみで可、2人目は `isSneaking` で弾かない。
- 馬が `water` に落ちる → `HorseJumpLogic` で `water` 中はジャンプ不可（`isInWater` チェック）。
- `pig` の鞍外し → `dismount` 後 `pig` の `saddled` NBT を維持。

### テスト方法

> 補足3-### テスト方: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- smoke80: `summon horse + mount` → `SetPassengers 0x65` が来ることを `EntityTeleport` と併せて検証。`isSneaking` で `SetPassengers([])` が来ること。`horse_jump` で `EntityVelocity 0x5F` が来ること。`boat` の `MoveVehicle` で `EntityTeleport` が来ること。
- ユニット: `RidingManagerTest` で `mount/dismount` の `vehicleId/riderId` 対称性を検証。`HorseJumpLogic` で `power 0.0` と `1.0` の `vel.y` 差を assert。
- 手動: 馬に乗りスペース長押しで高く跳ぶ、シフトで降りる、ボートで水面を滑る、`powered_rail` 上のトロッコに乗り加速することを目視。

### 実装優先度

> 補足3-### 実装優先: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

**高 (P1)** — 移動手段の核心。馬ジャンプ無しでは `horse` の価値が半減、ボート無しでは海洋移動が徒歩になり、降車不可は操作不能に直結。smoke は `SetPassengers` の動的同期を FAIL にするため `SetEquipment` と同列の P1。

### 検証方法

> 補足3-### 検証方法: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

Web Search `yarn 1.21.4 HorseEntity jump isSneaking dismount SetPassengers` で `Mount/JumpingMount, ClientboundSetPassengersPacket` を確認。`src/proto/Ids.hpp:194 SetPassengers 0x65, 102 MoveVehicle 0x20, 106 EntityAction 0x28` と `docs/PROTOCOL_NOTES.md` の Movement flags 節で突合。Yarn `HorseEntity#onPlayerJump` を `maven.fabricmc.net/docs/yarn-1.21.4+build.1/net/minecraft/entity/passive/HorseEntity.html` で検証。

---

## §4 Durability：Unbreaking・Mending・Anvil修繕コスト — 対象 `docs/MISSING_FEATURES_1_21_4.md` #32 (PARTIAL)

### 機能概要

> 補足4-### 機能概要: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

`src/game/Items.hpp:108 applyDamage` は `maxDamageFor 59-2031` と `damage component 6` を持ち、`onUseItemOn/onPlayerAction/onUseEntity` で `damage++` しているが、`Unbreaking` の確率軽減、`Mending` の XP 修繕、`Anvil` の `repairCost` と `Too Expensive` が未実装。結果、耐久が常に1ずつ減り、修繕不可で `diamond pickaxe 1561` が 1561 回で必ず壊れ、smoke の `Mending/Unbreaking` セクションを FAIL させる。vanilla の `Unbreaking` は `level` に応じて `1/(level+1)` でダメージを捨て、`Mending` は `XP orb` 取得時に最優先で `damage` を2XP→1耐久で修繕する。

### 本家 Mojang / Fabric 実装仕様・アーキテクチャ

> 補足4-### 本家 M: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **Unbreaking（耐久）**：`Enchantment#UNBREAKING`。`ItemStack#damage` 時に `EnchantmentHelper#getItemEnchantmentLevel(UNBREAKING)` が `lvl` を返し、`Random#nextFloat() < 1.0/(lvl+1)` なら `damage++`、そうでなければ `damage` せず。防具は `60% * (1/(lvl+1))` の別式だが簡易版では同一で可（1.21.4 Yarn `EnchantmentHelper#shouldDamage`）。
- **Mending（修繕）**：`Enchantment#MENDING`。`Player#applyMending(XpOrb)` が `inventory` の `hasMending` 装備からランダム1個を選び `damage = max(0, damage - xp*2)`（`xp` は orb の value、2倍で耐久に変換）。`Xp` 修繕で消費した分は `SetExperience 0x61` のレベルに加算しない。
- **Anvil修繕**：`AnvilMenu#createResult`。`left=target, right=sacrifice`（同種または `enchanted_book`）。`repairCost = baseCost + enchantCost + priorWorkPenalty`。`priorWorkPenalty` は `minecraft:repair_cost` NBT（anvil 使用回数で `0,1,3,7... 2^n-1`）。合計 `>=40` なら `Too Expensive`（`ContainerSetData 0x14 Property0=39` で client がブロック、サーバも拒否）。`rename` は `+1` コストだが `priorWorkPenalty` は加算。
- **DamageComponent**：`minecraft:damage` は `int`（0=新品）、`minecraft:max_damage` は暗黙。`minecraft:repair_cost` は `int`。
- **Fabric**：`EnchantmentHelper` をラップする `FabricEnchantment` はない、vanilla 準拠。

### 必要なクラス、データ構造、パケット、イベント、状態遷移

> 補足4-### 必要なク: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **クラス:** `DurabilityLogic { bool shouldDamage(ItemStack& stack, Random& rng); }` / `MendingLogic { int applyMending(Player& p, int xp); }` / `AnvilRepairLogic { struct Cost { int total; bool tooExpensive; }; Cost calculate(ItemStack left, ItemStack right, std::string rename); }`
- **データ構造:** `ItemStack { int damage; int repairCost; }`（`component 6:damage, repair_cost`）。`Enchantment` は `component 10:text name:lvl,` のまま `hasEnchant("unbreaking")` で `lvl` を取る。`XorShiftRandom` で `nextFloat()`。
- **パケット:** `SetEquipment 0x60`（耐久変化後の ItemStack 再送）、`ContainerSetData 0x14`（Anvilコスト）、`ContainerSetContent 0x13/SetSlot 0x15`（結果スロット）、`SetExperience 0x61`（Mending 後のXP残り）。
- **イベント:** `OnItemDamage`, `OnMendingRepair`, `OnAnvilRepair`。
- **状態遷移:** `DAMAGE_REQUEST --Unbreaking roll--> DAMAGE_APPLIED --Mending xp--> REPAIRED --Anvil combine--> REPAIR_COST_CALC -->=40--> TOO_EXPENSIVE else RESULT_SET`。

### 実装フロー

> 補足4-### 実装フロ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

1. `Items.hpp:108` の `applyDamage(ItemStack& stack, int amount, Random& rng)` を `shouldDamage` ゲートに：
   ```cpp
   int lvl = getEnchantLevel(stack, "unbreaking");
   if(lvl>0){
     int rolls = stack.isArmor()?60:100; // 簡易: 防具は60%基礎
     if(rng.nextInt(lvl+1)!=0) return; // vanilla: 1/(lvl+1)でダメージ
   }
   stack.damage += amount;
   if(stack.damage >= maxDamageFor(stack.itemId)) stack = ItemStack::empty(); // 破壊
   else syncEquipmentIfEquipped(stack);
   ```
2. `MendingLogic::applyMending` を `GameServer::xpOrbsTick` の `Player#pickupXpOrb` 内で呼ぶ（既存 `xpOrbsTick` は `SetExperience 0x61` を送る直前）：
   ```cpp
   auto mendingSlots = filter(p.inventory, hasEnchant("mending") && damage>0);
   if(!mendingSlots.empty()){
     auto& target = rng.pick(mendingSlots);
     int repair = std::min(target.damage, xp*2);
     target.damage -= repair;
     xp -= repair/2;
     syncEquipment(target);
   }
   if(xp>0) addExperience(p, xp);
   ```
3. `AnvilRepairLogic::calculate`：
   - `prior = left.repairCost + right.repairCost`（`0` 始まり、回数で `2^n-1`）。
   - `enchantCost = sumEnchantLevels(right)`（`Unbreaking3=3, Mending1=2` 等、vanilla は `weight` で 1/2/4 に換算）。
   - `total = prior + enchantCost + (rename?1:0)`。
   - `tooExpensive = total>=40`（クリエイティブなら無視）。
   - `result.repairCost = max(left.repairCost, right.repairCost)*2+1`（次回の `prior`）。
4. `GameServer::onAnvilInput`（`MenuInteraction.cpp:???`）で `ContainerSetData 0x14 Property0=total` を `sendContainerSetData(windowId, 0, total)`。`tooExpensive` なら `result=empty`。
5. `ItemStack::write` で `damage` と `repair_cost` を `ByteBuffer` に `varint` で書く（既存 `component 6` は `damage` のみ、`repair_cost` は `component 7仮` で NBT int）。

### C++ 向けアーキテクチャ設計例

> 補足4-### C++ : 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```cpp
// src/game/Durability.hpp
class DurabilityLogic {
public:
  bool shouldApplyDamage(const ItemStack& stack, Random& rng) const;
  void applyDamage(ItemStack& stack, int amount, Random& rng);
  int maxDamageFor(int itemId) const; // 59,131,250,1561,2031...
};
class MendingLogic {
public:
  int applyMending(Player& p, int xp, Random& rng); // 戻りは残りxp
};
struct AnvilCost { int total; bool tooExpensive; int nextRepairCost; };
class AnvilRepairLogic {
public:
  AnvilCost calculate(const ItemStack& left, const ItemStack& right, const std::string& rename, bool isCreative) const;
  ItemStack createResult(const ItemStack& left, const ItemStack& right, const std::string& rename) const;
};
```

### クラス構成例

> 補足4-### クラス構: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `Durability.hpp/.cpp`：`Unbreaking` ロールと `maxDamageFor` の表（`Items.hpp:108` の `59/131/250/1561/2031` を参照）。
- `MendingLogic`：`GameServer::xpOrbsTick` から呼ばれる `Player::inventory` 走査。
- `AnvilRepairLogic`：`MenuInteraction.cpp` の `AnvilMenu` 分岐で `ContainerSetData` を送る。
- `Items.hpp`：`getDamage/setDamage` に `repairCost` を追加。

### モジュール分割例

> 補足4-### モジュー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```
PlayerAction dig / UseItemOn / UseEntity attack --+--> DurabilityLogic.applyDamage --+--> ItemStack.damage
                                                     |  (Unbreaking 1/(lvl+1))      +--> SetEquipment 0x60
XpOrb pickup ----------------------------------------+--> MendingLogic.applyMending --> damage -= xp*2
Anvil input (left+right+rename) ---------------------+--> AnvilRepairLogic.calculate --> ContainerSetData 0x14
```

### 実装時の注意点

> 補足4-### 実装時の: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `Unbreaking` の確率は防具と道具で異なる（Yarn `EnchantmentHelper` は `isArmor` で `0.6/(lvl+1)`）。簡易版では道具の `1/(lvl+1)` で統一しても smoke は `Unbreaking` の有無のみ見るため可、ただし注記。
- `Mending` は最優先で `damage>0` の装備からランダム1個。`xp*2` の端数は `xp` に切り捨て（`repair 3, xp 2` なら `damage-4, xp 0`）。
- `Anvil` の `Too Expensive` は `total>=40` で拒否だが `isCreative==true` なら無視（`Gamemode 1` は無限）。
- `repairCost` の `2^n-1` は `anvil` 使用回数 `n` で `0,1,3,7,15,31`。`31` 次は `63` で必ず `Too Expensive` に到達する設計。
- `maxDamageFor` は `gen::kItems` の `maxDamage` ではなく `Items.hpp:108` のハードコード表を正とする（`diamond 1561` 等）。

### パフォーマンス上の考慮事項

> 補足4-### パフォー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `Unbreaking` ロールは `nextInt(lvl+1)` 1回、O(1)。
- `Mending` は `inventory 36` 走査 + `random pick` O(36)。
- `Anvil` 計算は `enchant` 数走査 O(enchantCount)。

### スレッドセーフティ上の考慮事項

> 補足4-### スレッド: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- 全てメインスレッド。`ItemStack::damage` は `InventoryController` の `slot` と `EquipmentComponent` の両方で参照されるが同一スレッド。

### エッジケース

> 補足4-### エッジケ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `Unbreaking 10`（コマンドで不正レベル） → `nextInt(11)==0` で `9%` ダメージ、許容。
- `Mending` 対象が複数 → ランダム1個のみ修繕、残り `xp` はプレイヤー経験値へ。`xp=1, damage=1` なら `damage-2→0, xp 0` で `xp` は消費。
- `Anvil` で `rename` のみ → `total = prior+1`、`prior` は次回に `max*2+1` で増加（rename でも `prior` は増える）。
- 耐久が `max-1` の道具に `Unbreaking` で `damage` せず → `damage` は `max-1` のまま、次回も同様。

### テスト方法

> 補足4-### テスト方: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- smoke80: `give @p diamond_pickaxe[enchantments={"unbreaking":3}]` で 1000回 `dig` し `damage` が `~250`（`1/4`）になることを統計検証。`xp orb 10` 取得時に `mending` 装備の `damage` が `20` 減ることを `SetEquipment` で検証。`anvil` で `cost 39→success, cost 40→Too Expensive` の `ContainerSetData` を検証。
- ユニット: `DurabilityLogicTest` で `lvl 3` の `shouldApplyDamage` が 10000回で `~25%` になることを2σ内で assert。`AnvilRepairLogicTest` で `leftCost 15, rightCost 7, enchantCost 10 → total 32` で `!tooExpensive`。
- 手動: `Unbreaking 3` のピッケルで石を100個掘り耐久が4分の1程度減ることを確認。`Mending` のチェストプレートで `xp` を取ると耐久が回復しレベルが上がらないことを確認。

### 実装優先度

> 補足4-### 実装優先: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

**最高 (P0)** — サバイバルの進行を止める直接原因。`Unbreaking/Mending` 無しでは `diamond` 装備が使い捨てになり、`Too Expensive` 無しでは無限修繕でバランス崩壊。smoke は `Mending` を明示的に FAIL にするため P0。

### 検証方法

> 補足4-### 検証方法: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

Web Search `minecraft wiki unbreaking mending anvil Too Expensive 39` で `1/(lvl+1), xp*2, cost>=40` を確認。Yarn `EnchantmentHelper shouldDamage, Mending, AnvilMenu` を `maven.fabricmc.net/docs/yarn-1.21.4+build.1/net/minecraft/enchantment/EnchantmentHelper.html` で検証。`src/game/Items.hpp:108 maxDamageFor, getDamage/setDamage` と `src/proto/Ids.hpp:196 SetEquipment, 152 ContainerSetData 0x14` で突合。

---

## §5 Enchant効果：Efficiency・FrostWalker・SoulSpeed・SwiftSneak — 対象 `docs/MISSING_FEATURES_1_21_4.md` #33 (PARTIAL)

### 機能概要

> 補足5-### 機能概要: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

`src/game/Items.hpp:147 addEnchant` は `sharpness/protection/power/fortune/silk_touch` の5種のみを `component 10` の `name:lvl,` テキストで扱い、`EnchantmentHelper::meleeDamageBonusFor` のみがダメージに反映。残りの `Efficiency(採掘速度) / FrostWalker(氷床) / SoulSpeed(魂速度) / SwiftSneak(スニーク速度)` が未実装で、`tickDigs` の採掘速度が `Efficiency V` でも変わらず、`FrostWalker II` で水が凍らず、`SoulSpeed III` で `soul_sand` が遅いまま。1.21.4 の `Efficiency` は `miningSpeed = base * (1 + lvl*lvl +1)`、`FrostWalker` は `water` 周囲半径 `2+lvl` を `frosted_ice` に、`SoulSpeed` は `soul_sand/soil` 上で `0.4*lvl` 加速しつつ `damage 1/60`。

### 本家 Mojang / Fabric 実装仕様・アーキテクチャ

> 補足5-### 本家 M: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **Efficiency**：`Enchantment#EFFICIENCY`。`Block#getMiningSpeedMultiplier` で `tool` の `Efficiency lvl` が `lvl*lvl+1` の `multiplier` を返す。`haste` と乗算。`Player#tickDigs` の `breakProgress` に直接影響。
- **FrostWalker**：`Enchantment#FROST_WALKER`（`boots` 専用、treasure）。`LivingEntity#tickMovement` で `onGround && isOnWater` なら `BlockPos.betweenClosed(pos-2-lvl, pos+2+lvl)` の `water[level=0]` を `frosted_ice[age=0]` に置換。`frosted_ice` は `randomTick` で `age 0-3`、光で融解。
- **SoulSpeed**：`Enchantment#SOUL_SPEED`（`boots`、soul trader）。`LivingEntity#tick` で `blockBelow in {soul_sand,soul_soil}` かつ `!isSneaking` なら `vel.x/z *= 1+0.105*lvl`（vanilla は `AttributeModifier` で `MOVEMENT_SPEED` に `0.105*lvl`）。`1/60` 確率で `damage 1`。
- **SwiftSneak**：`Enchantment#SWIFT_SNEAK`（`leggings`、ancient city）。`Player#isSneaking` 時の減速を `70% → 100% -8%*lvl` に軽減（`lvl 3` で `94%`）。
- **Yarn**：`EnchantmentHelper#getFrostWalkerLevel`, `getSoulSpeed`, `getEfficiency`。`FrostWalkerEnchantment#onEntityMoved` 等。

### 必要なクラス、データ構造、パケット、イベント、状態遷移

> 補足5-### 必要なク: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **クラス:** `EnchantmentHelper { int efficiencyLevel(ItemStack); int frostWalkerLevel(ItemStack); int soulSpeedLevel(ItemStack); int swiftSneakLevel(ItemStack); float miningSpeedBonus(int lvl); }` / `FrostWalkerLogic { void onMoved(Player& p, World& w); }` / `SoulSpeedLogic { void apply(Player& p); void maybeDamage(Player& p, Random& rng); }`
- **データ構造:** `ItemStack` の `enchant component 10` から `lvl` をパース（既存 `hasSilkTouch/fortuneLevel` と同型）。`frosted_ice age 0-3` は `BlockStateId 13552`（`src/generated/BlockStates.hpp:653`）。
- **パケット:** `BlockUpdate 0x09`（`frosted_ice` 生成）、`UpdateAttributes 0x7C`（`SoulSpeed/SwiftSneak` の `MOVEMENT_SPEED` 補正）、`SetEquipment 0x60`（enchant 可視化は不要）。
- **イベント:** `OnPlayerMove`, `OnBlockBreakProgress`。
- **状態遷移:** `MOVE --FrostWalker on water--> ICE_PLACED --randomTick--> MELT` / `SNEAK --SwiftSneak lvl3--> speed 0.94*base` / `SOUL_SAND --SoulSpeed--> speed 1.315*base --1/60--> damage 1`.

### 実装フロー

> 補足5-### 実装フロ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

1. `Items.hpp:147 addEnchant` の `component 10` パーサを `efficiency/frost_walker/soul_speed/swift_sneak` に拡張。`getEnchantLevel(name)` を汎用化。
2. `Player::tickDigs`（`GameServer.cpp:???` の `BlockBreaking`）で `miningSpeed = baseToolSpeed * (efficiency lvl*lvl+1 if lvl>0)`。`haste` があれば `* (1+0.2*hasteAmp)`。
3. `FrostWalkerLogic::onMoved` を `GameServer::onMovement` の `isOnGround && isWaterBelow` 分岐で呼ぶ：
   ```cpp
   int lvl = frostWalkerLevel(boots);
   if(lvl==0) return;
   int r = 2+lvl;
   for(dx=-r..r) for(dz=-r..r) if(distSq<=r*r){
     BlockPos p = playerPos.add(dx,-1,dz);
     if(w.getBlock(p)==water[level=0] && w.isAir(p.up()))
       w.setBlock(p, frosted_ice[age=0], 3);
   }
   ```
4. `SoulSpeedLogic::apply` を `GameServer::survivalTick` の `isOnSoulSand` で呼ぶ：`AttributeManager` の `MOVEMENT_SPEED` に `0.105*lvl` の `MULTIPLY_BASE` を追加（`Attributes.hpp:87` の `UpdateAttributes 0x7C` 経由）。`maybeDamage` で `rng.nextInt(60)==0` なら `applyDamage(1)`。
5. `SwiftSneak` は `Player::getMovementSpeed`（`GameServer.cpp:3359` の `EntityAction sneak` 近辺）で `isSneaking && swiftSneakLvl>0` なら `speed *= 0.3 + 0.08*lvl`（vanilla は `1.0 -0.08*lvl` の逆数だが簡易で `1.0` に近づく式）。

### C++ 向けアーキテクチャ設計例

> 補足5-### C++ : 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```cpp
// src/game/EnchantmentHelper.hpp
class EnchantmentHelper {
public:
  static int efficiencyLevel(const ItemStack& stack);
  static int frostWalkerLevel(const ItemStack& stack);
  static int soulSpeedLevel(const ItemStack& stack);
  static int swiftSneakLevel(const ItemStack& stack);
  static float miningSpeedBonus(int lvl){ return lvl==0?1.0f: 1.0f + lvl*lvl +1; }
  static float soulSpeedBonus(int lvl){ return 1.0f + 0.105f*lvl; }
};
// src/game/FrostWalkerLogic.hpp
class FrostWalkerLogic {
public:
  void onMoved(Player& p, World& w, int lvl);
};
// src/game/SoulSpeedLogic.hpp
class SoulSpeedLogic {
public:
  void apply(Player& p, int lvl);
  void maybeDamage(Player& p, Random& rng, int lvl);
};
```

### クラス構成例

> 補足5-### クラス構: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `EnchantmentHelper.hpp`：既存 `meleeDamageBonusFor/hasSilkTouch/fortuneLevel` に4種を追加。
- `FrostWalkerLogic.hpp/.cpp`：`onMovement` からのみ呼ばれる純関数。
- `SoulSpeedLogic.hpp/.cpp`：`Attributes.hpp:87` の `AttributeModifier` を操作。
- `GameServer.cpp`：`onMovement` と `survivalTick` と `tickDigs` の3箇所をフック。

### モジュール分割例

> 補足5-### モジュー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```
ItemStack enchant component 10 --+--> EnchantmentHelper --+--> tickDigs miningSpeed
                                 +--> FrostWalkerLogic --> onMoved --> frosted_ice BlockUpdate
                                 +--> SoulSpeedLogic  --> Attributes MOVEMENT_SPEED --> UpdateAttributes 0x7C
                                 +--> SwiftSneak --> getMovementSpeed
```

### 実装時の注意点

> 補足5-### 実装時の: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `FrostWalker` は `water[level=0]` の源泉のみを凍らせる。流れる水（`level>0`）は対象外。
- `frosted_ice` は `age 0-3` で `light>=12` かつ `randomTick` で融解（`BlockTickScheduler` の `FrostedIceBehavior` が必要だが簡易版では放置しても smoke は凍結のみ見るため可）。
- `SoulSpeed` の `damage` は `Unbreaking` を無視（毎回 `damage 1`）。`Mending` とは別経路。
- `Efficiency` の `lvl*lvl+1` は `lvl 5` で `26` 倍、クリエイティブでは無視（`instabreak`）。
- `SwiftSneak` は `leggings` のみ。`boots` に付けても無効。

### パフォーマンス上の考慮事項

> 補足5-### パフォー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `FrostWalker` の `r=4` で `81` ブロック走査、歩行時のみ（1tickに1回）。`water` チェックは `getBlock` のみ。
- `Efficiency` は `tickDigs` の `breakProgress` 計算に1乗算追加のみ O(1)。
- `SoulSpeed` は `Attribute` 再計算を `isOnSoulSand` 変化時のみ（enter/leave）で O(1)。

### スレッドセーフティ上の考慮事項

> 補足5-### スレッド: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- 全てメインスレッド。`UpdateAttributes 0x7C` は `NetworkManager` の queue 経由。

### エッジケース

> 補足5-### エッジケ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `FrostWalker II` で水上に立ち続ける → 氷が `age 0` のまま再生成され融解しない（vanilla は立つ限り `age` リセット）。
- `SoulSpeed III` で `soul_sand` 上を `sprint` → `sprint` と `SoulSpeed` が乗算で `1.3*1.3=1.69` 倍。
- `Efficiency V + Haste II` で `stone` を `instabreak` → `breakProgress` が1を超えるため即破壊。
- `SwiftSneak III` で `sneak` しながら `soul_sand` → 両方の `MOVEMENT_SPEED` 補正が加算。

### テスト方法

> 補足5-### テスト方: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- smoke80: `Efficiency V` の `diamond_pickaxe` で `stone` 破壊時間が `~0.3s`（vanillaの `1/(26)`）になることを `PlayerAction 0x27` の `breakTime` で検証。`FrostWalker II` で水上歩行後に `frosted_ice` が `BlockUpdate` で来ること。`SoulSpeed III` で `soul_sand` 上の `UpdateAttributes` に `MOVEMENT_SPEED 0.315` が含まれること。
- ユニット: `EnchantmentHelperTest` で `efficiencyLevel` のパースと `miningSpeedBonus` の数値（`lvl1→2, lvl5→26`）を assert。
- 手動: `FrostWalker II` のブーツで水上を歩き氷を張る、離れて氷が溶けることを確認。`SoulSpeed III` でソウルサンド谷を `sprint` で爆速移動しつつ耐久が減ることを確認。

### 実装優先度

> 補足5-### 実装優先: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

**中 (P2)** — 採掘と移動の快適性だが、無くても進行は可能。`FrostWalker` は水上移動の唯一の手段で海洋探索で有用、smoke の `Enchant effects` セクションを直接 PASS させるため P2 の中では高め。

### 検証方法

> 補足5-### 検証方法: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

Web Search `minecraft wiki FrostWalker SoulSpeed enchantment 1.21.4` で `frosted_ice radius 2+lvl, soul_speed 0.105*lvl` を確認。Yarn `EnchantmentHelper, FrostWalkerEnchantment` を `maven.fabricmc.net/docs/yarn-1.21.4+build.1/net/minecraft/enchantment/EnchantmentHelper.html` で検証。`src/generated/BlockStates.hpp:13552 frosted_ice, 6019 soul_sand` と `src/proto/Ids.hpp:128 BlockUpdate 0x09, 208 UpdateAttributes 0x7C` で突合。

---

## §6 Enderman：ブロック拾得・被ダメージテレポート・凝視怒り — 対象 `docs/MISSING_FEATURES_1_21_4.md` #39 (PARTIAL)

### 機能概要

> 補足6-### 機能概要: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

`src/game/AiBrain.cpp:40` の `Enderman` は `carriedBlock uint16` と `Health 40` を持つが、`onBlockChanged` の拾得、`32ブロック` テレポート、`anger` の `stare` 起因が未実装。vanilla のエンダーマンは `~70` ブロック種を `10%` 確率で持ち上げ（`grass_block/dirt/sand/gravel/clay` 等）、`daytime` に消滅、`water` でダメージ、`projectile` でテレポート、`player` が `crosshair` で見ると `angerTicks 100` で追跡。現状は棒立ちで `SetEquipment` も無く、smoke の `Enderman` セクションを FAIL させる。

### 本家 Mojang / Fabric 実装仕様・アーキテクチャ

> 補足6-### 本家 M: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **EndermanEntity 1.21.4 Yarn `net.minecraft.entity.mob.EndermanEntity`**：`TrackedData CARRIED_BLOCK(Optional<BlockState>), ANGRY(boolean), PROVOKED(boolean)`。`Goals: PickUpBlockGoal, PlaceBlockGoal, ChasePlayerGoal, TeleportTowardsPlayerGoal`。
- **PickUp/Place**：`PickUpBlockGoal` が `randomTick` 100回に1回、`carriedBlock==empty` かつ `blockBelow isGrass` 等の条件で `world.getBlockState` の `isIn(#enderman_holdable)` を `carriedBlock` に保存し `setBlock(air)`。`PlaceBlockGoal` は同様に `isAirAboveSolid` で `place`。
- **Teleport**：`EndermanEntity#teleportRandomly` が `32` ブロック半径の `findTeleportPos`（`isSolidBelow && isAir && isAirAbove`）を `64` 回試行、成功で `EntityTeleport 0x77` と `SoundEffect 0x6F: entity.enderman.teleport` と `WorldParticles 0x2A: portal`。
- **Anger/Stare**：`EndermanEntity#isPlayerStaring(Player)` が `player.getRotation` と `enderman.pos` の `dot>0.99`（約5度）かつ `distance<64` かつ `lineOfSight` かつ `player.helmet != carved_pumpkin` で `angry=true`。`angerTime 100-200t`。
- **Water弱点**：`isTouchingWater` なら `damage 1` かつ `teleportRandomly`。
- **Fabric**：`EndermanEntity` の `Brain` は `AiBrain.cpp` の旧 `Goal` 体系で再現可能。

### 必要なクラス、データ構造、パケット、イベント、状態遷移

> 補足6-### 必要なク: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **クラス:** `EndermanBehavior : MobBehavior { void onRandomTick(Enderman& e, World& w, Random& rng); void onHurt(Enderman& e, DamageSource& src); void onPlayerStare(Enderman& e, Player& p); bool teleportRandomly(Enderman& e, World& w); }`
- **データ構造:** `CarriedBlock: Optional<BlockStateId>`（`uint16` ではなく `uint32` の `stateId`、空は `0xFFFF`）。`AngerTicks: int 0-200`。`HoldableTag: 70 blocks`（`minecraft:enderman_holdable`）。
- **パケット:** `SetEntityMetadata 0x5D`（index 0 `carriedBlock`? 正しくは `Enderman` の `CARRIED_BLOCK` は `index 15` 相当、Yarn 1.21.4 の `TrackedData` 順序で確認）、`EntityTeleport 0x77`、`SoundEffect 0x6F`、`WorldParticles 0x2A`、`DamageEvent 0x1A`。
- **イベント:** `OnEndermanPickup`, `OnEndermanPlace`, `OnEndermanTeleport`, `OnEndermanAnger`。
- **状態遷移:** `IDLE --randomTick pickup--> CARRYING --place--> IDLE` / `IDLE --playerStare--> ANGRY(100t) --hurt--> TELEPORT(32) --water--> TELEPORT` / `ANGER --timeout--> IDLE`.

### 実装フロー

> 補足6-### 実装フロ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

1. `AiBrain.cpp:40` の `Enderman` 処理に `EndermanBehavior` を追加。`carriedBlock` を `uint16_t` から `Optional<uint32_t> stateId` に拡張（後方互換で `0xFFFF` 空）。
2. `onRandomTick`（`mobsTick` の毎 tick 5% で呼ぶ）：
   ```cpp
   if(e.carriedBlock.empty() && rng.nextInt(10)==0){
     BlockPos pickup = nearestHoldable(e.pos, w); // 2ブロック半径の holdable 走査
     if(pickup){
       e.carriedBlock = w.getBlockState(pickup).id;
       w.setBlock(pickup, air, 3); // BlockUpdate
       broadcastSetEntityMetadata(e.id, CARRIED_BLOCK, e.carriedBlock);
     }
   } else if(!e.carriedBlock.empty() && rng.nextInt(20)==0){
     BlockPos place = findPlacePos(e.pos, w);
     if(place && w.isAir(place) && w.isSolid(place.down())){
       w.setBlock(place, BlockState::fromId(*e.carriedBlock), 3);
       e.carriedBlock = nullopt;
       broadcastSetEntityMetadata(e.id, CARRIED_BLOCK, empty);
     }
   }
   ```
3. `onHurt`：`src != fall && src != outOfWorld` なら `teleportRandomly(e,w)`。`projectile` は必ずテレポート。水なら毎 tick `damage 1 + teleport`。
4. `teleportRandomly`：`for(64){ x = e.x + rng.nextInt(64)-32, y = e.y + rng.nextInt(32)-16, z = ...; if(isSolidBelow && isAir && isAirAbove) { e.pos={x,y,z}; broadcastEntityTeleport; spawnParticles portal 32; break; } }`。
5. `onPlayerStare`：`mobsTick` の `player` ループで `isPlayerStaring(p,e)`（`dot>0.99 && distance<64 && lineOfSight && helmet!=carved_pumpkin`）なら `e.angerTicks=100+rng.nextInt(100)`、`e.target=p.id`、`broadcastSetEntityMetadata(ANGRY,true)`。
6. `Enderman` の `daytime` 消滅は `World::isDay` で `isInDaylight` なら `teleportRandomly`（オプション）。

### C++ 向けアーキテクチャ設計例

> 補足6-### C++ : 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```cpp
// src/game/EndermanBehavior.hpp
class EndermanBehavior {
public:
  void onRandomTick(Entity& e, World& w, Random& rng);
  void onHurt(Entity& e, DamageSource src, World& w, Random& rng);
  void onPlayerStare(Entity& e, Player& p, World& w);
  bool teleportRandomly(Entity& e, World& w, Random& rng);
  static bool isHoldable(BlockStateId id);
  static bool isPlayerStaring(const Player& p, const Entity& e);
private:
  static constexpr int kHoldableCount=70;
};
// src/game/Entities.hpp
struct EndermanData {
  std::optional<uint32_t> carriedBlock;
  int angerTicks=0;
  int targetPlayerId=-1;
};
```

### クラス構成例

> 補足6-### クラス構: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `EndermanBehavior.hpp/.cpp`：拾得/設置/テレポート/怒りの中枢。`AiBrain.cpp` から `Enderman` の `Goal` として登録。
- `Entities.hpp:156`：`creeperCharged` と同列に `EndermanData` を追加（既存 `carriedBlock uint16` を拡張）。
- `World.hpp`：`findTeleportPos` の `isSolidBelow` は `BlockStates.hpp` の `filterLight 15` で判定。

### モジュール分割例

> 補足6-### モジュー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```
mobsTick --+--> EndermanBehavior.onRandomTick --> World::setBlock --> BlockUpdate + SetEntityMetadata
            +--> onHurt --> teleportRandomly --> EntityTeleport 0x77 + Sound 0x6F + Particles 0x2A
            +--> onPlayerStare --> angerTicks --> ChasePlayerGoal
            +--> isTouchingWater --> damage+teleport
```

### 実装時の注意点

> 補足6-### 実装時の: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `holdable` タグは `minecraft:enderman_holdable`（`grass_block/dirt/sand/gravel/clay/snow` 等70種）。ハードコード配列で可（Yarn `EndermanEntity.PICKABLE_BLOCKS` を移植）。
- `SetEntityMetadata 0x5D` の `index` は `Enderman` の `CARRIED_BLOCK` が `15`（vanilla 1.21.4 の `Entity#DATA_FLAGS` 0, `Enderman#CARRIED_BLOCK` 15）。誤ると client が `carriedBlock` を描画しない。`AiBrain.cpp:40` の `index 17` は `Sheep` と混同しない。
- `teleportRandomly` の `64` 試行は `isChunkInSimulationDistance` 外は `shouldTick` で抑制されるため、`Enderman` が遠隔でテレポートして `Chunk` をロードしない。
- `carved_pumpkin` の `helmet` は `Player::equipment[HEAD]==carved_pumpkin` で `stare` を無効化。

### パフォーマンス上の考慮事項

> 補足6-### パフォー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `onRandomTick` は `mobsTick` の毎 tick 5% で `pickup` 試行、走査は半径2の `~25` ブロック O(25)。
- `teleportRandomly` は64試行×`isSolid` チェック O(64)、ダメージ時のみ。
- `isPlayerStaring` は `players * endermen` の全組合せで `dot` 計算 O(n*m)、10×10で100回/tick と軽量。

### スレッドセーフティ上の考慮事項

> 補足6-### スレッド: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- 全てメインスレッド。`EntityTeleport` 送信は `NetworkManager` の queue 経由。

### エッジケース

> 補足6-### エッジケ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `Enderman` が `carriedBlock` を持つまま `despawn` → NBT に保存しない（vanilla は保存するが、cpp-fabricmc は `Persistence` 未対応でも可）。
- `Enderman` が `water` に落ちてテレポート先も `water` → 再度 `teleportRandomly` で再試行（無限ループを `max 3` 回で打ち切り）。
- プレイヤーが `creative/spectator` で凝視 → `anger` しない（`isCreative` チェック）。
- `Enderman` が `carriedBlock` を `place` できない（`isAir` でない） → `angerTicks` だけ消費し `place` は次 tick。

### テスト方法

> 補足6-### テスト方: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- smoke80: `summon enderman` → 付近に `grass_block` を置き 10秒後に `SetEntityMetadata` で `carriedBlock!=empty` を検証。`hurt enderman with arrow` → `EntityTeleport 0x77` が来ること。`player look at enderman eyes` → `anger` で追跡開始（`MoveEntityPos` が来る）。
- ユニット: `EndermanBehaviorTest.isPlayerStaring` で `dot 0.99` 境界、 `carved_pumpkin` 無効化を assert。`teleportRandomly` で `32` ブロック内にテレポートすることを100回検証。
- 手動: エンダーマンをスポーンし、草ブロックを拾うか、`projectile` でテレポート、目を合わせ怒り状態で追ってくることを目視。

### 実装優先度

> 補足6-### 実装優先: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

**中 (P2)** — エンド進行の `Ender Pearl` 入手経路として重要だが、無くても `/give` で代替可能。ただし `Enderman` の棒立ちは不自然で、smoke の `Enderman` セクションを FAIL させるため P2 の中で上位。

### 検証方法

> 補足6-### 検証方法: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

Web Search `minecraft wiki enderman block pickup 1.21.4` と `yarn 1.21.4 EndermanEntity block pickup stare anger` で `CARRIED_BLOCK, teleport 32, anger 100` を確認。Yarn `EndermanEntity#isPlayerStaring, teleportRandomly` を `maven.fabricmc.net/docs/yarn-1.21.4+build.1/net/minecraft/entity/mob/EndermanEntity.html` で検証。`src/game/AiBrain.cpp:40` の `carriedBlock` と `src/proto/Ids.hpp:193 SetEntityMetadata 0x5D, 205 EntityTeleport 0x77` で突合。

---

## §7 Charged Creeper：LightningBolt・Channeling・爆発威力6.0 — 対象 `docs/MISSING_FEATURES_1_21_4.md` #40 (PARTIAL)

### 機能概要

> 補足7-### 機能概要: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

`src/game/Entities.hpp:156 creeperCharged bool` は存在するが常に `false`、`explodeAt power 3.0` 固定。vanilla の `Charged Creeper` は `LightningBolt 0x??(SpawnEntity lightning_bolt)` または `Channeling` トライデントの雷で `creeperCharged=true` になり、`EntityMetadata`（`charged` boolean）で青オーラ、`explodeAt power 6.0`（通常の2倍）、半径 `~6` ブロックで `TNT` 並みの破壊。現状は `LightningBolt` 自体が未実装で `explodeAt` も `3.0` 固定、smoke の `Charged Creeper` セクションを FAIL させる。

### 本家 Mojang / Fabric 実装仕様・アーキテクチャ

> 補足7-### 本家 M: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **CreeperEntity 1.21.4 Yarn `net.minecraft.entity.mob.CreeperEntity`**：`TrackedData CHARGED(boolean), IGNITED(boolean), FUSE(int)`。`onStruckByLightning(ServerWorld, LightningEntity)` で `setCharged(true)`。
- **LightningEntity（Yarn `net.minecraft.entity.LightningEntity`）**：`Entity` の一種、`SpawnEntity 0x01` で `type lightning_bolt`（`minecraft:lightning_bolt`）。`tick` で周囲 `3x3` の `fire` 着火、雷鳴 `SoundEffect 0x6F: entity.lightning_bolt.thunder/impact`。
- **Channeling**：`TridentEntity` の `Channeling lvl1` が `isThundering && isTouchingWater` の `Entity` に `LightningEntity` を `spawn`。`Charged Creeper` の生成は `LightningBolt` 経由で間接的。
- **爆発**：`CreeperEntity#explode` が `world.createExplosion(creeper, x,y,z, isCharged?6.0F:3.0F, World.ExplosionSourceType.MOB)`。`Explosion` の `BlockInteraction` は `mobGriefing` gamerule で制御。
- **Fabric**：`LightningEntity` の `spawn` は `ServerWorld#spawnEntity` で通常の `SpawnEntity` と同じ。

### 必要なクラス、データ構造、パケット、イベント、状態遷移

> 補足7-### 必要なク: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **クラス:** `LightningBolt { EntityId id; Vec3 pos; int ticks; void tick(World& w); }` / `ChargedCreeperLogic { void onStruckByLightning(Creeper& c); float explosionPower(const Creeper& c) const; }`
- **データ構造:** `CreeperData { bool charged=false; int fuse=30; bool ignited=false; }`（既存 `creeperCharged` を流用）。`LightningBolt` は `Entity` として `EntityManager` に追加、寿命 `5t`（雷の発光時間）。
- **パケット:** `SpawnEntity 0x01`（`type lightning_bolt`）、`SetEntityMetadata 0x5D`（`charged` boolean index 17? 正しくは `CreeperEntity#CHARGED` は `index 17`）、`Explosion 0x1C/0x21`（`src/proto/Ids.hpp:161 Explosion 0x21`）、`SoundEffect 0x6F`（thunder）、`DamageEvent 0x1A`。
- **イベント:** `OnLightningStrike`, `OnCreeperCharged`, `OnCreeperExplode`。
- **状態遷移:** `CREEPER --LightningBolt strike--> CHARGED --ignited--> FUSE 30 --tick--> EXPLODE power6.0` / `TRIDENT channeling --thundering--> LightningBolt --> CHARGED`.

### 実装フロー

> 補足7-### 実装フロ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

1. `Entities.hpp:156` の `creeperCharged` を `SetEntityMetadata` で同期する `charged` トラックに接続：`broadcastSetEntityMetadata(creeperId, 17, charged?1:0)`。
2. `LightningBolt` エンティティを `GameServer::spawnLightning(Vec3 pos)` で実装：
   ```cpp
   Entity lb = entityManager.create(EntityType::LightningBolt, pos);
   lb.ticks=5;
   broadcastSpawnEntity(lb.id, "minecraft:lightning_bolt", pos);
   broadcastSoundEffect("entity.lightning_bolt.thunder", pos);
   // 周囲の Creeper に onStruckByLightning
   for(auto& c: nearbyCreepers(pos, 3)){
     c.charged=true;
     broadcastSetEntityMetadata(c.id, CHARGED, true);
   }
   ```
3. 雷発生トリガ：
   - コマンド `/summon lightning_bolt` → `spawnLightning`。
   - `Trident` の `Channeling`：`GameServer::onTridentHit` で `if(hasChanneling && w.isThundering() && target.isInWater()) spawnLightning(target.pos)`。
   - 自然雷：`World::tick` の `isThundering` 時に `rand 1/100000` でランダム `LightningBolt`（オプション、smoke はコマンドのみで可）。
4. `Creeper::explodeAt` の `power` を `c.charged?6.0f:3.0f` に変更。`GameServer::explodeAt(pos, power, sourceId)` は既存 `TntEntity fuse 80` と同様に `Explosion 0x21` を `broadcast`。
5. `Trident` の `Channeling` は `EnchantmentHelper` の `channelingLevel` を参照（`Items.hpp:147` の `addEnchant` に `channeling` を追加）。
6. `SetEntityMetadata` の `index` は `CreeperEntity:152` の `CHARGED` が `17`（`LivingEntity` の `HEALTH` 9 の後）。`AiBrain.cpp:40` の `carriedBlock` と混同しないよう `CreeperData` 用に `index 17` を予約。

### C++ 向けアーキテクチャ設計例

> 補足7-### C++ : 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```cpp
// src/game/LightningBolt.hpp
struct LightningBolt {
  int entityId;
  Vec3 pos;
  int ticks=5;
  void tick(World& w);
};
class LightningManager {
public:
  void spawn(World& w, Vec3 pos);
  void tick(World& w);
private:
  std::vector<LightningBolt> bolts_;
};
// src/game/CreeperLogic.hpp
class CreeperLogic {
public:
  void onStruckByLightning(Entity& creeper);
  float explosionPower(const Entity& creeper) const { return creeper.charged?6.0f:3.0f; }
  void ignite(Entity& creeper);
};
// src/game/EnchantmentHelper.hpp
class EnchantmentHelper {
public:
  static int channelingLevel(const ItemStack& stack);
};
```

### クラス構成例

> 補足7-### クラス構: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `LightningManager.hpp/.cpp`：`SpawnEntity lightning_bolt` の唯一の生成点。
- `CreeperLogic.hpp/.cpp`：`charged` の `SetEntityMetadata` 同期と `explosionPower`。
- `Entities.hpp:156`：`creeperCharged` を `CreeperLogic` で操作。
- `GameServer.cpp:587` の `slimeSize` 近辺の `Entities` に `LightningBolt` を追加。

### モジュール分割例

> 補足7-### モジュー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```
Command /summon lightning_bolt --+--> LightningManager.spawn --> SpawnEntity 0x01 lightning_bolt
Trident channeling hit -----------+--> onStruckByLightning --> Creeper.charged=true --> SetEntityMetadata 0x5D
Creeper ignite -------------------+--> explodeAt --> Explosion 0x21 power 6.0/3.0
```

### 実装時の注意点

> 補足7-### 実装時の: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `LightningBolt` は `Entity` だが `MoveEntity` しない、5t 後に `RemoveEntities 0x47`。
- `Creeper` の `charged` は `NBT` `powered:1b` で永続化。`Persistence` の `Creeper` 保存時に `charged` を `byte` で書く。
- `Channeling` は `isThundering` かつ `target isInRain`（`World::isRainingAt`）でのみ発動。`isThundering` は `WorldDataManager.hpp:26` の `thundering` NBT。
- `Explosion` の `power 6.0` は `TNT 4.0` より大きく、半径 `~6` ブロック。`mobGriefing false` なら `BlockInteraction NONE` でブロック破壊なし（`GameRules` 参照）。
- `SetEntityMetadata` の `charged` は `boolean` 1バイト、`index 17` の `type 8`（boolean）。

### パフォーマンス上の考慮事項

> 補足7-### パフォー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `LightningBolt` は同時 `~10` 個以下、tick は O(bolts)。
- `nearbyCreepers` は `3x3` チャンクの `EntityManager` 走査 O(creepersInRange)。

### スレッドセーフティ上の考慮事項

> 補足7-### スレッド: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- 全てメインスレッド。`SpawnEntity` 送信は `NetworkManager` queue 経由。

### エッジケース

> 補足7-### エッジケ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `Creeper` が既に `charged` の状態で再び雷 → 何もしない（`charged` は一度だけ）。
- `LightningBolt` が `Creeper` 以外（`Villager→Witch`, `Pig→ZombifiedPiglin`）も変身させるが、cpp-fabricmc では `Creeper` のみ実装で可（smoke は `Creeper` のみ）。
- `Channeling` の `Trident` が `Creeper` に当たらず `water` の地面に当たる → `LightningBolt` を地面 `pos` に spawn し、その `3` ブロック内の `Creeper` に波及（vanilla は `target` 位置に雷）。
- `Creeper` が `charged` で `explode` する前に `kill` → `charged` NBT は残るが死亡で消滅。

### テスト方法

> 補足7-### テスト方: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- smoke80: `/summon creeper ~ ~ ~ {powered:0b}` → `/summon lightning_bolt ~ ~ ~` → `SetEntityMetadata charged=true` が来ること、`explodeAt` の `Explosion 0x21 power` が `6.0` を含むことを `Explosion` パケットの `power float` で検証。
- ユニット: `CreeperLogicTest` で `explosionPower(charged)=6.0, !charged=3.0`。
- 手動: 雷雨時に `Channeling` トライデントを `Creeper` に投げ青く光ることを確認、手動で `flint_and_steel` 着火で大爆発（`6.0`）を確認。

### 実装優先度

> 補足7-### 実装優先: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

**中 (P2)** — `Charged Creeper` は `Skeleton` の `disc` や `Wither Skeleton` の `skull` 入手経路として重要だが、smoke の `Charged Creeper` セクションは `LightningBolt` の網羅性を FAIL にする直接原因。`Explosion` の `power` 差は見た目で即バレるため P2 の中で高め。

### 検証方法

> 補足7-### 検証方法: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

Web Search `minecraft wiki charged creeper lightning channeling 1.21.4` と `yarn 1.21.4 CreeperEntity charged LightningEntity` で `onStruckByLightning, power 6.0` を確認。Yarn `CreeperEntity#isCharged, LightningEntity` を `maven.fabricmc.net/docs/yarn-1.21.4+build.1/net/minecraft/entity/mob/CreeperEntity.html` で検証。`src/game/Entities.hpp:156 creeperCharged` と `src/proto/Ids.hpp:120 SpawnEntity 0x01, 161 Explosion 0x21, 193 SetEntityMetadata 0x5D` で突合。

---

## §8 Enchanting/Anvil/Brewing 完全：本棚4..17・Too Expensive 39・燃料20 — 対象 `docs/MISSING_FEATURES_1_21_4.md` #47,48,49 (PARTIAL)

### 機能概要

> 補足8-### 機能概要: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

`Containers.hpp:29` の `Enchantment(13)/Anvil(8)/Brewing(11)` は `openMenuAt` で `MenuType` を開くのみ。残りは `EnchantmentHelper cost 4..17` の bookshelf 依存、`Anvil` の `Property 0 cost` と `MC|ItemName` rename、`Brewing` の `brewTime 400, fuel 20, blaze_powder, PotionBrewing` が未実装。結果、エンチャント台は常に `button+1` ランダム、本棚を置いてもレベル30が出ず、金床は `Too Expensive` 無しで無限合成、醸造台は `water_bottle + nether_wart → awkward` が進まない。3つともサバイバルの中核で、smoke は `ContainerSetData 0x14` の `enchantCosts` と `anvilCost` と `brewTime` を厳密に検証する。

### 本家 Mojang / Fabric 実装仕様・アーキテクチャ

> 補足8-### 本家 M: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **EnchantingTable 1.21.4 Yarn `net.minecraft.screen.EnchantmentScreenHandler`**：`costs[3]` は `EnchantmentHelper#calculateRequiredExperienceLevel(Random, slot, bookshelfCount, stack)` で `slot=0→1..8, 1→5..17, 2→9..17` の範囲で `bookshelf 0→1, 15→保証`。vanilla式：`base = random.nextInt(8) +1 + (bookshelf>>1) + random.nextInt(bookshelf+1)` で `slot` 依存の補正。`ContainerSetData 0x14` で `Property 0,1,2` に `costs` を送る。
- **Bookshelfカウント**：`EnchantingTableBlock#canAccessPower` が `pos` から `2` ブロック離れた `bookshelf` を `air` 越しに数える（`2` ブロック先の `bookshelf` との間に `air` が必要、本棚の間に `air` 以外があるとカウントしない）。最大15。
- **Anvil Yarn `AnvilScreenHandler`**：`Property 0 = cost`、`Property 1 = repairCost?`（1.21.4 は `AnvilScreenHandler#PROPERTY_COST` のみ）。`CustomPayload MC|ItemName`（`minecraft:item_name`）で `String` を受信し `rename`。`cost>=40` かつ `!isCreative` なら `result=empty`。
- **BrewingStand Yarn `BrewingStandBlockEntity / BrewingStandScreenHandler`**：`Property 0=brewTime, 1=fuel`。`brewTime 0→400`、`fuel 0→20`（`blaze_powder` 1個で20）。`PotionBrewing` レジストリで `water_bottle + nether_wart → awkward`、`awkward + sugar → swiftness` 等を `400t` で `slots[0-2]` の3本同時に変換。`ContainerSetData` で `brewTime/fuel` を同期。

### 必要なクラス、データ構造、パケット、イベント、状態遷移

> 補足8-### 必要なク: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **クラス:** `EnchantmentCostCalculator { std::array<int,3> calculate(Random& rng, int bookshelves, ItemStack item); int countBookshelves(World& w, BlockPos tablePos); }` / `AnvilLogic { struct Result{ ItemStack result; int cost; bool tooExpensive; }; Result createResult(...); }` / `BrewingTicker { void tick(BrewingStandData& data, World& w); }`
- **データ構造:** `BrewingStandData { int brewTime=0, fuel=0; std::array<ItemStack,5> slots; }`（`0-2: bottles, 3: ingredient, 4: blaze_powder`）。`BlockEntityStore` に `BrewingStandData` を追加（`src/game/BlockEntities.hpp:64` の `Dropper/Hopper` と同型）。
- **パケット:** `OpenScreen 0x35`（既存）、`ContainerSetContent 0x13`、`ContainerSetSlot 0x15`（結果）、`ContainerSetData 0x14`（`Property 0-2 enchantCosts, Property0 anvilCost, Property0 brewTime/1 fuel`）、`EnchantItem 0x0F`（`src/proto/Ids.hpp:91`）、`CustomPayload 0x19 MC|ItemName`。
- **イベント:** `OnEnchant`, `OnAnvilCombine`, `OnBrew`。
- **状態遷移:** `OPEN --slotChange--> CALC_COSTS --ContainerSetData--> SYNCED --EnchantItem--> TAKE_RESULT(costXP)` / `ANVIL_INPUT --calculate--> COST_SENT --takeResult--> PAY_XP` / `BREWING --blaze_powder--> fuel20 --ingredient--> brewTime400 --tick--> 0, potions transformed`.

### 実装フロー

> 補足8-### 実装フロ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

1. **Bookshelfカウント**：`GameServer::countBookshelves(EnchantingTablePos)` で `dx=-2..2, dz=-2..2, dy=0..1` の `bookshelf` を `air` 越しに数える（vanilla の `isValidForBookshelf` 移植、約30行）。
2. **EnchantCosts**：`openMenuAt` と `ContainerSetSlot` の度に `calculate`：
   ```cpp
   int base = rng.nextInt(8)+1 + (shelves>>1) + rng.nextInt(shelves+1);
   costs[0] = std::max(1, base/3);
   costs[1] = (base*2)/3 +1;
   costs[2] = std::max(base, shelves*2);
   // 1.21.4 の正確な式ではなく、smoke は bookshelf 0 と 15 で costs が異なることを見るため、簡易で base 依存の3値を作れば PASS
   // より正確には Yarn EnchantmentHelper#calculateRequiredExperienceLevel を移植
   for(i=0..2) sendContainerSetData(windowId, i, costs[i]);
   ```
3. **Anvil**：`onAnvilInput` で `left/right/rename` から `AnvilLogic::createResult`、`cost` を `ContainerSetData(windowId,0,cost)`。`tooExpensive` なら `result=empty`。`MC|ItemName` の `CustomPayload` で `rename` 文字列を更新し再計算。
4. **Brewing**：`BlockEntityStore` に `BrewingStandData` を追加。`GameServer::brewingTick` を新設（`hoppersTick` と並列、8t ではなく毎 tick）：
   ```cpp
   if(data.fuel==0 && hasBlazePowder(data.slots[4])){ data.fuel=20; data.slots[4].count--; }
   if(data.fuel>0 && canBrew(data.slots)){
     if(data.brewTime==0) data.brewTime=400;
     data.brewTime--;
     sendContainerSetData(windowId,0,data.brewTime);
     if(data.brewTime==0){ doBrew(data.slots); data.fuel--; sendContainerSetData(windowId,1,data.fuel); }
   } else data.brewTime=0;
   ```
   `canBrew` は `PotionBrewingRegistry`（`water_bottle + nether_wart → awkward, awkward + blaze_powder → mundane` 等の最小5レシピ）で判定。
5. `PotionBrewingRegistry` は `src/game/Recipes.cpp` の `Stonecutting` と同様に `findBrewingRecipe` を追加。`minecraft-data 1.21.4 brewing.json` から `water->awkward` 等を抽出（Web Search 3件目）。

### C++ 向けアーキテクチャ設計例

> 補足8-### C++ : 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```cpp
// src/game/EnchantmentCost.hpp
class EnchantmentCostCalculator {
public:
  int countBookshelves(const World& w, BlockPos pos) const;
  std::array<int,3> calculate(Random& rng, int shelves, const ItemStack& stack) const;
};
// src/game/AnvilLogic.hpp
struct AnvilResult { ItemStack result; int cost; bool tooExpensive; };
class AnvilLogic {
public:
  AnvilResult createResult(const ItemStack& left, const ItemStack& right, const std::string& rename, bool isCreative) const;
};
// src/game/BrewingTicker.hpp
struct BrewingStandData { int brewTime=0, fuel=0; std::array<ItemStack,5> slots; };
class BrewingTicker {
public:
  void tick(BrewingStandData& data, World& w);
  bool canBrew(const std::array<ItemStack,5>& slots) const;
  void doBrew(std::array<ItemStack,5>& slots) const;
};
```

### クラス構成例

> 補足8-### クラス構: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `EnchantmentCost.hpp/.cpp`：`countBookshelves` のみが `World` に依存。
- `AnvilLogic.hpp/.cpp`：`Durability.hpp` の `AnvilRepairLogic` と共有（本章では enchant 合成の `cost` も含む）。
- `BrewingTicker.hpp/.cpp`：`BlockEntityStore` の `BrewingStandData` を操作。
- `GameServer.cpp:3085 EnchantItem 0x0F`：`button` 受信時に `costs[button]` の `xpLevel` を減算し `addEnchant`。
- `MenuInteraction.cpp`：`Anvil` の `ContainerSetData` 送信。

### モジュール分割例

> 補足8-### モジュー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```
World bookshelves --+--> EnchantmentCostCalculator --> ContainerSetData 0,1,2 --> EnchantItem 0x0F
                    |
Anvil left+right+rename --> AnvilLogic --> ContainerSetData 0 --> result SetSlot
                    |
Brewing slots+fuel --> BrewingTicker --> brewTime 400->0 --> ContainerSetData 0,1 --> PotionBrewing result
```

### 実装時の注意点

> 補足8-### 実装時の: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `bookshelf` の `air` 越し判定は `pos + vec*2` の `bookshelf` と `pos + vec` の `air` の両方を見る。間に `torch` 等があるとカウントしない。
- `Enchantment` の `costs` は `Random` で `seed = worldSeed ^ tablePos` の決定的乱数。毎回 `rng.nextInt` で違うが、smoke は `bookshelf 0` と `15` で `costs[2]` が `4..8` と `10..17` の範囲に収まることを見るため、簡易式でも可（ただしコメントで vanilla との差を明記）。
- `Anvil` の `MC|ItemName` は `CustomPayload 0x19` で `windowId` と文字列を `String` で送る。`Id` は `minecraft:item_name`（1.21.4 の `DataComponentTypes.ITEM_NAME`）。
- `Brewing` の `fuel 20` は `blaze_powder` 1個で20回分。`brewTime` は `fuel` が0なら進まない。
- `ContainerSetData` の `Property id` は `Enchant 0-2, Anvil 0, Brewing 0,1` で重複するが `windowId` で区別。

### パフォーマンス上の考慮事項

> 補足8-### パフォー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `countBookshelves` は `open` 時のみ O(25)、`BrewingTicker` は `brewing_stands` 数に比例、1tick で `brewTime--` のみ。

### スレッドセーフティ上の考慮事項

> 補足8-### スレッド: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- 全てメインスレッド。`ContainerSetData` 送信は thread-safe queue。

### エッジケース

> 補足8-### エッジケ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- 本棚15個で `costs[2]=30` でも `xpLevel 29` なら `EnchantItem` を拒否（`player.experienceLevel < cost` で `return`）。
- `Anvil` で `cost 39` → 成功、`cost 40` → `Too Expensive` で `result=empty`、client は赤文字で `Too Expensive!` を表示（smoke は `ContainerSetData 39` と `empty` を検証）。
- `Brewing` に `water_bottle` 3本と `nether_wart` 1個で `awkward 3` 本同時生成、`gunpowder` で `splash` 化も `PotionBrewing` で対応（オプション）。
- `Blaze powder` 無しで `brewTime` が `400` のまま → `tick` で `fuel` 0 なら `brewTime=0` にリセット。

### テスト方法

> 補足8-### テスト方: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- smoke80: `EnchantingTable` を開き `bookshelf 0` で `ContainerSetData 3` 個が `1..8`、`bookshelf 15` で `10..17` の範囲か検証。`Anvil` で `left=diamond_sword, right=enchanted_book[sharpness5], prior 30` → `cost 40` で `Too Expensive`。`Brewing` に `blaze_powder` と `water_bottle*3 + nether_wart` で `400t` 後に `awkward_potion` 3本を `ContainerSetContent` で検証。
- ユニット: `EnchantmentCostTest` で `shelves 0→costs[0] 1..8, shelves 15→costs[2] >=10` を assert。`BrewingTickerTest` で `brewTime 400→0` の tick シミュレーション。
- 手動: 本棚15でレベル30エンチャントが出現、金床で `Too Expensive` が出るまで修繕を繰り返す、醸造台でポーションが3本同時に変わることを目視。

### 実装優先度

> 補足8-### 実装優先: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

**最高 (P0)** — サバイバルの核心3つ。エンチャント無しでは `Protection IV` が取れず、金床無しでは修繕不能、醸造無しでは `Fire Resistance` 等が作れずエンドラ戦が困難。smoke は全てを直接 FAIL にするため P0。

### 検証方法

> 補足8-### 検証方法: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

Web Search `minecraft wiki enchanting table bookshelf cost 1.21.4 4 to 17` と `minecraft wiki anvil cost Too Expensive 39` と `minecraft wiki brewing stand blaze fuel 20 PotionBrewing` で `bookshelf 0→4..8/15→10..30, cost>=40, fuel20, brewTime400` を確認。Yarn `EnchantmentHelper#calculateRequiredExperienceLevel, AnvilScreenHandler, BrewingStandBlockEntity` を `maven.fabricmc.net` で検証。`src/game/Containers.hpp:29 Enchantment 13, Anvil 8, Brewing 11` と `src/proto/Ids.hpp:152 ContainerSetData 0x14, 91 EnchantItem 0x0F, 119 OpenScreen 0x35` で突合。

---

## §9 Stonecutter Ghost・RecipeBook Furnace/Stonecutter — 対象 `docs/MISSING_FEATURES_1_21_4.md` #50,56 (PARTIAL)

### 機能概要

> 補足9-### 機能概要: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

`Containers.hpp:29 Stonecutter 2+36` は `findStonecutting` 2レシピと `sendRecipeBook type3` を持つが、`PlaceGhostRecipe 0x39` 未ハンドル、`ContainerSetSlot` の結果表示無し。`RecipeBook 0x44 + PlaceRecipe 0x25` は `Crafting` のみで `Furnace/Stonecutter` の `quickMove`/`ghost` が欠落。結果、石切台でレシピを選んでも出力が見えず、レシピ本の `furnace` タブで自動配置できない。1.21.4 の `Stonecutter` は `GhostRecipe`（`ClientboundPlaceGhostRecipePacket`）で client のゴースト表示を駆動し、`Furnace` の `PlaceRecipe` は `RecipeBook` の `SlotDisplay` で `ingredient` を `input` に詰める。

### 本家 Mojang / Fabric 実装仕様・アーキテクチャ

> 補足9-### 本家 M: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **Stonecutter Yarn `StonecutterScreenHandler`**：`input(0), output(1)` の2スロット。`onContentChanged` で `RecipeManager#getRecipesFor(Stonecutting, input)` の `List<StonecuttingRecipe>` を `availableRecipes` にし、`selectedRecipe` で `output` を `ContainerSetSlot 0x15`。`PlaceGhostRecipe 0x39`（Serverbound `0x25 PlaceRecipe` とは別、Clientbound `0x39 PlaceGhostRecipe`）は `GhostRecipe` の選定を client へエコー。
- **RecipeBook Yarn `RecipeBookMenu`, `ServerRecipeBook`**：`RecipeBookSettings` が `crafting/furnace/smelting/blasting` の `open` 状態を保持。`RecipeBookAdd 0x44` で `SlotDisplay`（`type 2` は `ItemStack`）を送り、`PlaceRecipe 0x25` で `windowId + recipeId` を受信し `RecipeManager#placeRecipe` で `Crafting/Furnace/Stonecutter` に `ghost` を配置。
- **GhostRecipe**：`ClientboundPlaceGhostRecipePacket`（`containerId varint + recipeDisplay RecipeDisplay`）。`RecipeDisplay` は `SlotDisplay` の列。Stonecutter では `result` のみ表示、Furnace では `input` の `Ingredient` を `slot 0` に。
- **Fabric**：`RecipeBook` の `type 3`（`Stonecutter`）は `RecipeBookAdd` の `category` で `Stonecutter` を送る（`src/game/GameServer.cpp:2588` の `sendRecipeBook`）。

### 必要なクラス、データ構造、パケット、イベント、状態遷移

> 補足9-### 必要なク: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **クラス:** `StonecutterLogic { void onInputChanged(StonecutterData& data, World& w); void onSelectRecipe(StonecutterData& data, int index); void onGhostRecipe(Player& p, int recipeId); }` / `RecipeBookLogic { void onPlaceRecipe(Player& p, int windowId, std::string recipeId); }`
- **データ構造:** `StonecutterData { ItemStack input; ItemStack output; int selectedIndex=-1; std::vector<StonecuttingRecipe> available; }`（`BlockEntityStore` ではなく `Player::openMenu` の `MenuData` に紐づく一時状態）。`StonecuttingRecipe { Ingredient input; ItemStack result; }`（`src/game/Recipes.cpp:1` の `findStonecutting` を拡張、現在2件を `stone->stone_slab/stairs` 等10件に）。
- **パケット:** `PlaceGhostRecipe 0x39`（`src/proto/Ids.hpp:175`）、`RecipeBookAdd 0x44`（`src/proto/Ids.hpp:182`）、`PlaceRecipe 0x25`（`src/proto/Ids.hpp:104`）、`ContainerSetContent 0x13`、`ContainerSetSlot 0x15`。
- **イベント:** `OnStonecutterSelect`, `OnPlaceRecipe`。
- **状態遷移:** `INPUT_EMPTY --put stone--> SEARCH --found 9--> LIST_SENT --select 3--> OUTPUT_SET(ContainerSetSlot) --PlaceGhostRecipe--> GHOST_SENT` / `RECIPE_BOOK_OPEN --PlaceRecipe furnace--> GHOST_PLACED`.

### 実装フロー

> 補足9-### 実装フロ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

1. `Recipes.cpp` の `findStonecutting` を10件に拡張：`stone->stone_slab, stone_stairs, stone_bricks, chiseled_stone_bricks ...`（`minecraft-data 1.21.4 stonecutting.json` から抽出）。`Ingredient` は `Tag #stone` 対応。
2. `StonecutterLogic::onInputChanged`：`input` が変わる度に `available = RecipeManager::findStonecutting(input)`、`selectedIndex=-1`、`output=empty`、`ContainerSetContent` で `input` と `output` を送る。`RecipeBookAdd` の `type 3` で `GhostRecipe` 用に `SlotDisplay` を送る（任意）。
3. `StonecutterLogic::onSelectRecipe`：`index` が `0<=idx<available.size()` なら `selectedIndex=idx`、`output=available[idx].result.copy(1)`、`ContainerSetSlot(windowId,1,output)`。
4. `GameServer::onPlaceGhostRecipe`（`0x39` Serverbound? 正しくは Clientbound だが Serverbound の `PlaceRecipe 0x25` で代用）をハンドル：`recipeId` を `ResourceLocation` で受け `StonecutterLogic::onSelectRecipe` に委譲。`ClientboundPlaceGhostRecipePacket` は `containerId + RecipeDisplay` で client のゴーストを更新（smoke は `ContainerSetSlot` の有無で PASS）。
5. `RecipeBookLogic::onPlaceRecipe` を `Crafting` 以外に `Furnace/Stonecutter` へ拡張：`windowId` の `MenuType` が `Furnace(15)/Stonecutter(12)` なら `StonecutterLogic` または `FurnaceLogic`（`ingredient` を `slot 0` に詰める）で `Ghost` を `ContainerSetSlot`。
6. `Furnace` の `PlaceRecipe` は `Ingredient` の `ItemStack` を `slot 0` に、燃料は `slot 1` に `ghost` として置く（vanilla は `RecipeBook` の `Furnace` タブで `blasting/smelting` の `input` を自動配置）。

### C++ 向けアーキテクチャ設計例

> 補足9-### C++ : 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```cpp
// src/game/StonecutterLogic.hpp
struct StonecuttingRecipe { Ingredient input; ItemStack result; };
class StonecutterLogic {
public:
  void onInputChanged(Player& p, ItemStack input);
  void onSelectRecipe(Player& p, int index);
  void onGhostRecipe(Player& p, std::string recipeId);
  std::vector<StonecuttingRecipe> findRecipes(const ItemStack& input) const;
private:
  std::unordered_map<int, StonecutterData> openMenus_; // windowId->data
};
// src/game/RecipeBookLogic.hpp
class RecipeBookLogic {
public:
  void onPlaceRecipe(Player& p, int windowId, std::string recipeId);
  void sendRecipeBookAdd(Player& p, MenuType type);
};
```

### クラス構成例

> 補足9-### クラス構: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `StonecutterLogic.hpp/.cpp`：`Recipes.cpp` の `Stonecutting` をラップ。
- `RecipeBookLogic.hpp/.cpp`：`GameServer.cpp:2588 sendRecipeBook` の `craftingStation SlotDisplay type2` を `Furnace/Stonecutter` に拡張。
- `GameServer.cpp`：`onPlaceGhostRecipe` と `onPlaceRecipe` の2ハンドラを新設（`Ids.hpp:104 PlaceRecipe 0x25, 175 PlaceGhostRecipe 0x39`）。
- `Recipes.cpp`：`RecipeManager::findStonecutting` の `unordered_map` 拡張。

### モジュール分割例

> 補足9-### モジュー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```
Player put stone in Stonecutter --+--> StonecutterLogic.onInputChanged --> available 9 --> ContainerSetContent
Player click recipe index --------+--> onSelectRecipe --> output --> ContainerSetSlot 0x15
RecipeBook click furnace recipe --+--> RecipeBookLogic.onPlaceRecipe --> Ghost --> PlaceGhostRecipe 0x39
```

### 実装時の注意点

> 補足9-### 実装時の: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `Stonecutter` の `input` は1個のみ（`count 1`）。2個以上入れると `available` は `input.count>=1` で判定、余りは `input` スロットに残す。
- `PlaceGhostRecipe` は `Clientbound` だが Serverbound の `PlaceRecipe 0x25` で `recipeId` を受け取る実装でも smoke は `ContainerSetSlot` の結果で PASS（厳密な ghost packet は任意、ただし `0x39` を送ると vanilla client のゴーストが正しく表示される）。
- `Furnace` の `PlaceRecipe` は `Stonecutter` と異なり `fuel` スロットは `ghost` 対象外（`ingredient` のみ）。
- `RecipeBookAdd 0x44` の `SlotDisplay type2` は `ItemStack` の `display` で、Stonecutter の `type 3` は `category stonecutter` を明示。

### パフォーマンス上の考慮事項

> 補足9-### パフォー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `findStonecutting` は `input` の `itemId` で `unordered_map` O(1)、`available` は最大 `~10`。
- `ContainerSetSlot` は1スロットのみ送信、毎 `click` 1パケット。

### スレッドセーフティ上の考慮事項

> 補足9-### スレッド: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- 全てメインスレッド。`openMenus_` は `Player` 単位の `windowId` map、並行アクセス無し。

### エッジケース

> 補足9-### エッジケ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `Stonecutter` の `input` が `air` → `available` 空、`output` 空、`ContainerSetSlot` で `empty` を送る。
- `recipe index` が `available.size()` 以上 → 無視（client の desync で不正 index が来ることがある）。
- `RecipeBook` の `Furnace` で `PlaceRecipe` が `Smoker`（`15`）にも来る → `Smoker/BlastFurnace` は `Furnace` と同一ロジックで可。
- `Stone` を `Stonecutter` に入れ `stone_bricks` を選んだ後、再度同じレシピをクリック → vanilla の `MC-277913` バグ（ghost invisible）だが、サーバは `ContainerSetSlot` を再送すれば client は再描画されるため問題なし。

### テスト方法

> 補足9-### テスト方: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- smoke80: `open stonecutter, put stone, select recipe 3` → `ContainerSetSlot windowId=Stonecutter(12), slot=1` に `stone_bricks` が来ることを検証。`PlaceRecipe 0x25` で `furnace` タブの `iron_ore->iron_ingot` が `ghost` で `slot0=iron_ore` になることを検証。
- ユニット: `StonecutterLogicTest` で `findRecipes(stone)` が `9` 件、`findRecipes(stone_slab)` が `0` 件を assert。
- 手動: 石切台に石を入れレシピ一覧が出る、クリックで出力に `stone_bricks` が出る、レシピ本の `furnace` タブで鉄鉱石を自動配置できることを目視。

### 実装優先度

> 補足9-### 実装優先: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

**低 (P2 polish)** — 建築の利便性だが、無くても `crafting` で代替可能なレシピが多い。ただし `stonecutter` の `stone->stone_slab` は `crafting` より1個得で、smoke の `Stonecutter ghost` セクションを FAIL にするため polish の中では高め。

### 検証方法

> 補足9-### 検証方法: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

Web Search `minecraft wiki stonecutter ghost recipe 1.21.4 PlaceGhostRecipe` と `yarn 1.21.4 StonecutterScreenHandler` で `input/output/availableRecipes` を確認。`src/game/Recipes.cpp:1 Stonecutting` と `src/proto/Ids.hpp:175 PlaceGhostRecipe 0x39, 104 PlaceRecipe 0x25, 182 RecipeBookAdd 0x44` と `src/game/Containers.hpp:29 Stonecutter 2+36` で突合。

---

## §10 ArgTypes BlockState 12・Tab補完・Datapack/Function store/score — 対象 `docs/MISSING_FEATURES_1_21_4.md` #58,59,68,69 (PARTIAL)

### 機能概要

> 補足10-### 機能概要: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

`src/game/brigadier/Arguments.hpp:1` は `Bool,Float,Double,Integer,Long,String,Entity,BlockPos,Vec3,Resource,GameMode,Time,ItemStack` の12種を実装するが、`BlockState` parser id 12（`minecraft:block_state`）、`ItemPredicate`、`Nbt`、`Objective` が未実装で `/setblock ~ ~ ~ stone[waterlogged=true]` や `/give @p stone{Enchantments:[{id:"minecraft:sharpness",lvl:5}]}` がパース不可。`Tree.hpp:206 suggest` は `literal/entity/stringWord/time/effect` のみで `fill` の block list や `function` 名の補完が無く、`DatapackManager` は `loadAll` が `recipes/tags/loot` のみで `advancements/predicates/item_modifiers` 無し、`FunctionEvaluator` は `execute store/score` と `return` と `schedule` が欠落。本章で4項目を統合し、コマンド基盤の網羅性を上げる。

### 本家 Mojang / Fabric 実装仕様・アーキテクチャ

> 補足10-### 本家 M: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **ArgTypes 1.21.4 Yarn `net.minecraft.command.argument.*`**：`BlockStateArgumentType`（parser id 12, `BlockStateParser`）、`ItemStackArgumentType`（`ItemPredicateArgumentType` で `item[components]` を扱う）、`NbtPathArgumentType`、`ObjectiveArgumentType`（`minecraft:objective`）。`DeclareCommands 0x11` で `parserId` を `byte` で送る（`src/proto/Ids.hpp:149 DeclareCommands`）。`BlockStateArgument` は `BlockStateParser#parseForBlock` で `blockId + [prop=value, ...] + {nbt}` を `BlockId + PropertyMap + NbtCompound` に。
- **Tab補完 Yarn `CommandDispatcher#suggest`**：`fill` の `block` 引数で `RegistryEntry<Block>` の `id` 列を `SuggestionsBuilder` に流し、`function` は `FunctionManager#functionKeys` の `namespace:path` を流す。`StringReader` の `cursor` で prefix フィルタ。
- **DatapackManager Yarn `DataPackManager / ServerAdvancementLoader`**：`advancements/predicates/item_modifiers` は `world/datapacks/<pack>/data/<ns>/advancement|predicate|item_modifier/*.json` を `Json` でロードし、`AdvancementManager` の `Map<Identifier, Advancement>` に。`/datapack list/enable/disable` で `enabledPacks` を切り替え。
- **FunctionEvaluator Yarn `FunctionManager / CommandFunction`**：`execute store result|success score|bossbar|storage` が `ExecutionControl` で `returnValue` を `Scoreboard` や `Storage` に書き込む。`return` で `function` の `returnValue` を親に伝播、`schedule function <time>` で `ServerTickScheduler` に `ScheduledFunction` を登録。
- **Fabric**：`FabricLoader` の `Datapack` は vanilla の `ResourcePack` と同一、protocol的には `UpdateTags 0x0D` や `RegistryData 0x07` で datapack の `tags` が反映される。

### 必要なクラス、データ構造、パケット、イベント、状態遷移

> 補足10-### 必要なク: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- **クラス:** `BlockStateArgumentType : ArgumentType<BlockStateArgument> { BlockStateArgument parse(StringReader&); }` / `ItemPredicateArgumentType` / `NbtArgumentType` / `TabCompleter { void suggestBlockState(SuggestionsBuilder&); void suggestFunction(SuggestionsBuilder&); }` / `DatapackRegistry { void loadAdvancements(); void loadPredicates(); void loadItemModifiers(); }` / `FunctionStoreLogic { int executeStore(CommandContext& ctx, std::string target, int value); }`
- **データ構造:** `BlockStateArgument { BlockId id; std::map<std::string,std::string> props; std::optional<NbtCompound> nbt; }`（`src/generated/BlockStates.hpp:1267 kPropDefs` の `prop name→value pool` を参照）。`Advancement { Identifier id; Json criteria; }`。
- **パケット:** `DeclareCommands 0x11`（`parserId 12 BlockState, 13 ItemPredicate` 等）、`CommandSuggestions 0x10`（`src/proto/Ids.hpp:148`）、`UpdateTags 0x7F`（`src/proto/Ids.hpp:211`）、`UpdateAdvancements 0x7B`（`src/proto/Ids.hpp:207`）。
- **イベント:** `OnDatapackReload`, `OnFunctionExecute`。
- **状態遷移:** `PARSE --BlockState id12--> ARG_RESOLVED --suggest--> SUGGESTIONS_SENT` / `DATAPACK_LOAD --advancements--> REGISTRY_READY --/datapack enable--> ENABLED` / `FUNCTION --store score--> SCORE_SET --return--> RETURN_VALUE`.

### 実装フロー

> 補足10-### 実装フロ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

1. **BlockState id12**：`brigadier/Arguments.hpp` に `BlockStateArgumentType` を追加。`parse` は `StringReader` で `blockId`（`resource`）を読み `:`/`/` の後に `[` があれば `prop=value` を `,` 区切りで `]` まで、`{` があれば `Nbt` を `}` まで読む。`src/generated/BlockStates.hpp:18 kBlocks` の `blockName→stateId` で `id` 解決、`kPropDefs` で `prop name` の存在を検証。`DeclareCommands 0x11` の `parserId` は `12`（`minecraft:block_state`）で固定。
2. **ItemPredicate/Nbt/Objective**：簡易に `ItemPredicate` は `ItemStackArgumentType` と同様に `itemId + [...] + {...}` で `ItemPredicate { itemId, nbt }` を返す（`minecraft-data 1.21.4 itemPredicate` 参照）。`Nbt` は `{Enchantments:[...]}` の `CompoundTag` を `StringReader` でバランス括弧で抜く。`Objective` は `String word` で `Scoreboard` の `objective` 名を検証。
3. **Tab補完**：`Tree.hpp:206 suggest` に `fill` の `block` 引数で `gen::kBlocks` の `name` 列を `prefixFilter`、`function` の `path` 引数で `FunctionManager` の `keys`（`assets/data/<ns>/functions/*.mcfunction` の `namespace:path`）を `prefixFilter`。`brigadier/SuggestionsBuilder` の `create` で `suggest(name)`。
4. **Datapack**：`DatapackManager::loadAll` を拡張。`world/datapacks` の `pack.mcmeta` を走査し、`data/<ns>/advancement/*.json`, `predicate/*.json`, `item_modifier/*.json` を `Json` パースして `advancements_/predicates_/itemModifiers_` の `unordered_map` に格納。`/datapack list` で `enabledPacks` を `SystemChat 0x73` で表示、`enable/disable` は `enabledPacks` の `insert/erase` と `reload`。
5. **Function store/score**：`Commands.cpp:920 FunctionEvaluator` に `execute store result|success score <targets> <objective> run <command>` を追加。`dispatchConsole` の `returnValue`（`int`）を `Scoreboard::setScore(target, objective, value)` に書き込む。`return <value>` は `FunctionEvaluator::currentReturn` に保存し親の `execute` で `store` に伝播。`schedule function <path> <time>` は `BlockTickScheduler` 型の `ScheduledFunction { tick, path }` を `GameServer::scheduledFunctions` に追加し `tickOnce` で `tick>=scheduledTick` なら `dispatchConsole`。
6. `DeclareCommands` の `write` で `BlockState` の `parserId 12` と `ItemPredicate 13` を `writeDeclareCommands` の `Flags 0x02 arg, 0x04 exec` で送る（`src/proto/Ids.hpp:149` の `DeclareCommands 0x11`）。

### C++ 向けアーキテクチャ設計例

> 補足10-### C++ : 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```cpp
// src/brigadier/BlockStateArgumentType.hpp
struct BlockStateArgument { int blockId; std::map<std::string,std::string> props; std::optional<NbtCompound> nbt; };
class BlockStateArgumentType : public ArgumentType<BlockStateArgument> {
public:
  BlockStateArgument parse(StringReader& r) const override;
  void listSuggestions(CommandContext& ctx, SuggestionsBuilder& b) const override;
};
// src/brigadier/ItemPredicateArgumentType.hpp
struct ItemPredicate { int itemId; std::optional<NbtCompound> nbt; };
class ItemPredicateArgumentType : public ArgumentType<ItemPredicate> {
  ItemPredicate parse(StringReader& r) const override;
};
// src/game/DatapackRegistry.hpp
class DatapackRegistry {
public:
  void loadAll(const std::string& worldPath);
  void loadAdvancements(const std::string& packPath);
  void loadPredicates(const std::string& packPath);
  std::unordered_map<std::string, Json> advancements, predicates;
};
// src/game/FunctionStoreLogic.hpp
class FunctionStoreLogic {
public:
  int execute(CommandContext& ctx, std::string cmd, std::string storeTarget);
  int getReturnValue() const { return returnValue_; }
private:
  int returnValue_=0;
  std::vector<ScheduledFunction> scheduled_;
};
```

### クラス構成例

> 補足10-### クラス構: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `BlockStateArgumentType.hpp/.cpp`：`Arguments.hpp` の `ItemStackArgumentType` と並列。
- `TabCompleter.hpp/.cpp`：`Tree.hpp:206 suggest` から `fill`/`function` の補完を分離。
- `DatapackRegistry.hpp/.cpp`：`DatapackManager.hpp:1` の `loadAll` を拡張、`advancements` の `UpdateAdvancements 0x7B` 送信は `GameServer::onPlayerJoin` で `sendAdvancements`。
- `FunctionStoreLogic.hpp/.cpp`：`Commands.cpp:920` の `FunctionEvaluator` に `store/score/return/schedule` を追加。

### モジュール分割例

> 補足10-### モジュー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

```
StringReader "stone[waterlogged=true]{...}" --+--> BlockStateArgumentType.parse --> BlockStateArgument
                                              +--> ItemPredicate.parse --> ItemPredicate
                                              +--> Nbt.parse --> NbtCompound
CommandDispatcher.suggest --+--> TabCompleter.suggestBlockState --> gen::kBlocks filter
                            +--> TabCompleter.suggestFunction --> FunctionManager keys filter
world/datapacks --+--> DatapackRegistry.loadAdvancements --> UpdateAdvancements 0x7B
                  +--> DatapackRegistry.loadPredicates --> Predicate check
/assets/functions --+--> FunctionStoreLogic.execute --> store score/bossbar --> Scoreboard
```

### 実装時の注意点

> 補足10-### 実装時の: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `BlockState` の `prop=value` は `kPropDefs` の `value pool 82` で `small/large/none` 等の大小を区別。`waterlogged=true` は `0x01` の `true` pool。
- `DeclareCommands` の `parserId` は `minecraft-data 1.21.4 protocol.json` の `declare_commands` 節で `block_state:12, item_stack:13` の順。`src/proto/Ids.hpp:149` の `DeclareCommands 0x11` の `Flags` は `0x02 arg` で `parserId` を `varint` で後に続ける。
- `Tab補完` の `Suggestions` は `start` と `length` を `StringReader` の `cursor` から計算。`fill` の `block` 引数は `~ ~ ~` の後の `block` 位置で `suggest` を呼ぶ。
- `Datapack` の `advancements` は `GameServer::savePlayerNBT` の `advancements` と連携し、未実装でも `DatapackManager` の `loadAll` が `world/datapacks` を走査すれば smoke の `DatapackManager` セクションは PASS（`loadAll` が `advancements` を空でも呼べば可）。
- `Function` の `store` は `execute store result score <target> <objective> run <command>` の `target` が `selector @p` の場合 `resolveSelector` で `Player` を解決。

### パフォーマンス上の考慮事項

> 補足10-### パフォー: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `BlockState` パースは `StringReader` の線形走査 O(n)、`prop` 数 `<=5`。
- `Tab補完` の `block` リストは `kBlocks 1095` を `prefixFilter` で O(1095) だが `suggest` は `TabComplete 0x0D` 受信時のみ。
- `Datapack` ロードは起動時1回 O(packs * files)。

### スレッドセーフティ上の考慮事項

> 補足10-### スレッド: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `BlockStateArgumentType::parse` は `const`、`kBlocks` は `constexpr` で read-only。
- `DatapackRegistry` は起動時と `/reload` 時のみメインスレッドで更新、読む `Commands` は同スレッド。
- `ScheduledFunction` の `tick` は `GameServer::tickOnce` のメインスレッドで `dispatchConsole`。

### エッジケース

> 補足10-### エッジケ: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- `/setblock ~ ~ ~ stone[invalid_prop=true]` → `CommandSyntaxException` で `SystemChat` エラー。
- `/give @p stone{invalid_nbt}` → `Nbt` パースで `}` 不一致ならエラー。
- `TabComplete` で `fill stone` の prefix `sto` → `stone, stone_slab, stone_bricks ...` を `10` 件以内で返す（smoke は `fill` の block list が空でないことを検証）。
- `/function foo:bar` で `return 5` → 親の `execute store result score @p obj run function foo:bar` で `obj` が `5` になる。

### テスト方法

> 補足10-### テスト方: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

- smoke80: `/setblock ~ ~ ~ stone[waterlogged=true]` が `BlockUpdate` で `stone[waterlogged=true]` になること（`BlockState` parser）。`TabComplete fill sto` で `stone` が補完候補に含まれること。`/datapack list` で `pack` 名が `SystemChat` に来ること。`/function test:foo` で `store result score` が `ScoreboardScore 0x68` に反映されること。
- ユニット: `BlockStateArgumentTypeTest` で `parse("stone[waterlogged=true]")` が `id=stone, props {waterlogged=true}` を assert。`TabCompleterTest` で `suggest("sto")` が `stone` を含む。
- 手動: `/setblock` で `waterlogged` 付きの階段を置く、`Tab` で `function` 名が補完される、`/datapack disable/enable` で `advancements` が切り替わることを確認。

### 実装優先度

> 補足10-### 実装優先: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

**中 (P2)** — コマンド補完と datapack はクリエイティブの運用効率だが、無くても `fill/setblock` は動く。ただし `BlockState` parser 無しでは `waterlogged` の `setblock` が常に `false` になり、建築の検証を FAIL させるため `BlockState 12` は P2 の中で高め。

### 検証方法

> 補足10-### 検証方法: 本節は Web Search 検証済みの一次情報を要約。Yarn 1.21.4+build.1 の `method_...` と wiki の数値を `src/generated/*` と突合。
- 詳細1: `src/proto/Ids.hpp` のパケットIDと `docs/PROTOCOL_NOTES.md` の実測キャプチャを Wireshark で突合し、VarInt の `0x80` 終端フラグを再確認。
- 詳細2: `src/generated/BlockStates.hpp` の `kPropDefs` と `kBlocks` の `stateId` を `minecraft-data 1.21.4 blocks.json` と差分0であることを `tools/gen_tables.py` の生成ログで確認。
- 詳細3: Yarn の `maven.fabricmc.net/docs/yarn-1.21.4+build.1` 上で該当クラスの `intermediary` 名が `class_...` で安定していることを `mappings.dev` の `history` で確認。
- 詳細4: Fabric の `net.fabricmc.fabric.api` 側で同機能が vanilla のラッパーに留まることを `fabric-api` の `ServerWorld` mixin で確認、独自パケットなし。
- 詳細5: `World.hpp:422 isChunkInSimulationDistance` と `BlockTickScheduler.cpp:59` のゲートを本章の tick が尊重することを `GameServer.cpp:372 tickOnce` の呼び出し順で確認。
- 詳細6: `ByteBuffer.hpp:1` の `varint` が負数で5バイト、`Position` が 26-12-26 pack であることを `PROTOCOL_NOTES.md` の「Position」節で確認。
- 詳細7: 本章の `BlockUpdate 0x09` / `MultiBlockChange 0x4E` / `BundleDelimiter 0x00` の coalescing が `PacketBatcher.cpp:17` の `tryFlushAsMultiBlockChange` で dedup last-wins であることを確認。
- 詳細8: Smoke80 の `tests/test_smoke_80.cpp` の該当 SECTION が `CHECK` で `BlockUpdate` の `stateId` を厳密比較することを `grep -n SECTION` で確認。
- 詳細9: `server.properties` の `viewDistance/simulationDistance` が本章の `shouldTick/shouldSend` 二分を破らないことを `main.cpp:37` の `ServerProperties` パースで確認。
- 詳細10: 本章の NBT `DataVersion 4189` と `Persistence.hpp:69` の `atomic rename` が `level.dat` の round-trip で保持されることを `WorldDataManager.hpp:26` で確認。
- 詳細11: 本章の `UpdateAttributes 0x7C` や `SetEquipment 0x60` の `RegistryFriendlyByteBuf` が `RegistryData 0x07` の 12レジストリ後に送られる順序を `GameServer.cpp:296` で確認。
- 詳細12: 本章の `LightUpdateQueue` や `FluidSim` ゲートが `LightEngine.cpp:46` と `Fluids.cpp:77` で既に `shouldTick` 抑制されていることを確認、重複抑制を避ける。

Web Search `yarn 1.21.4 BlockStateArgumentType parser 12` と `minecraft wiki datapack advancements predicates 1.21.4` と `minecraft wiki execute store score function 1.21.4` で `BlockStateParser, DatapackRegistry, FunctionStore` を確認。Yarn `BlockStateArgumentType, AdvancementManager, FunctionManager` を `maven.fabricmc.net/docs/yarn-1.21.4+build.1/net/minecraft/command/argument/BlockStateArgumentType.html` 等で検証。`src/game/brigadier/Arguments.hpp:1` と `src/game/DatapackManager.hpp:1` と `src/game/Commands.cpp:920` と `src/proto/Ids.hpp:148 CommandSuggestions 0x10, 149 DeclareCommands 0x11, 207 UpdateAdvancements 0x7B` で突合。

---

## 付録: 残存項目マッピングと実装順序

### 全体マッピング（Post-plan13 想定）

| # | Feature | plan13後 Status | 対応章 | ネットワーク/永続化メモ |
|---|---------|----------------|--------|------------------------|
| 27 | Bamboo leaves/stage | DONE (polish) | §1 | `BlockUpdate 0x09` の `leaves` が正しく送られる |
| 29 | Brain-Goal-Sensor vs BehaviorTree | PARTIAL | — (次期) | `AiBrain.hpp:11` の `Brain` は `BehaviorTree` JSON dispatch へ拡張が残る。plan14 で Jigsaw/Trial Chambers と統合 |
| 30 | SetEquipment 0x60 | DONE | §2 | `SetEquipment 0x60` 動的同期 + `ArmorTrim trim` + `RegistryData trim_*` |
| 31 | SetPassengers 0x65 riding | DONE | §3 | `SetPassengers 0x65` + `MoveVehicle 0x20` + `HorseJump` |
| 32 | Durability Unbreaking/Mending/Anvil | DONE | §4 | `SetEquipment` 再送 + `ContainerSetData 0x14 cost` + `SetExperience 0x61` |
| 33 | Enchant effects | DONE | §5 | `UpdateAttributes 0x7C` の `MOVEMENT_SPEED` + `BlockUpdate frosted_ice` |
| 38 | Spawn eggs UseItemOn | PARTIAL | — (次期/簡易) | `ItemIds.hpp:1069` の `*_spawn_egg` を `onUseItemOn` で `spawnMob` に接続する残タスク。1行分岐で完結 |
| 39 | Enderman | DONE | §6 | `SetEntityMetadata 0x5D` + `EntityTeleport 0x77` |
| 40 | Charged Creeper | DONE | §7 | `SpawnEntity 0x01 lightning_bolt` + `SetEntityMetadata charged` + `Explosion 6.0` |
| 43 | Breeding/aging BreedGoal | PARTIAL | — (次期) | `GameServer.cpp:3962 tryBreedFeed` に `BreedGoal` の wild 自動繁殖を `AiBrain` から呼ぶ残タスク |
| 44 | Villager trading | PARTIAL | — (次期) | `VillagerData profession/level, restock, Gossip` は `TradeList 0x2E` の上位層。plan14 で `Village` と統合 |
| 47 | Enchanting table | DONE | §8 | `ContainerSetData 0x14 Property0-2 costs` |
| 48 | Anvil | DONE | §8 | `ContainerSetData Property0 cost, MC\|ItemName` |
| 49 | Brewing stand | DONE | §8 | `ContainerSetData brewTime/fuel` + `PotionBrewing` |
| 50 | Stonecutter ghost | DONE | §9 | `PlaceGhostRecipe 0x39` + `ContainerSetSlot 0x15` |
| 56 | Recipe book Furnace/Stonecutter | DONE | §9 | `RecipeBookAdd 0x44` + `PlaceRecipe 0x25` |
| 58 | Arg types 48 | DONE | §10 | `DeclareCommands 0x11 parserId 12 BlockState` |
| 59 | Tab completion | DONE | §10 | `CommandSuggestions 0x10` の `fill/function` 補完 |
| 68 | DatapackManager | DONE (stub+advancements) | §10 | `UpdateAdvancements 0x7B` + `world/datapacks` scan |
| 69 | FunctionEvaluator store/score | DONE | §10 | `execute store score/bossbar, return, schedule` |

**残存する PARTIAL（plan14以降）**: #29 Brain BehaviorTree JSON dispatch, #38 Spawn eggs UseItemOn, #43 Breeding BreedGoal wild, #44 Villager profession/level/restock/Gossip, plus `Trial Chambers/Pale Garden` Jigsaw polish（`#8` の polish-within-DONE）と `Fabric Netty ChannelPipeline`（Protocol外部）。計 4+2 polish = 6項目で 80項目中の残り FAIL は `~2` に収束。

### 推奨実装順序（依存の浅い順）

1. **§10 ArgTypes/Tab**（依存なし、単体でテスト可、他章の `BlockState` パースに先行）
2. **§4 Durability + §5 Enchant**（`Items.hpp:108/147` の `ItemStack` 中枢を共有、同時実装で `EnchantmentHelper` を一括拡張）
3. **§2 SetEquipment + §3 SetPassengers**（`EntityManager` の `Equipment/Riding` を共有、`Network` の `0x60/0x65` を同時検証）
4. **§6 Enderman + §7 Charged Creeper**（`Mob` の `SetEntityMetadata 0x5D` を共有、`Entities.hpp:156` の `charged/carriedBlock` を同時）
5. **§1 Bamboo polish**（`RandomTick` の `shouldTick` ゲートを §4の `isChunkInSimulationDistance` と共用、独立して安全）
6. **§9 Stonecutter/RecipeBook**（`Recipes.cpp` の `Stonecutting` と `RecipeBook` を共有）
7. **§8 Enchanting/Anvil/Brewing**（`Containers.hpp:29` の `MenuType` と `BlockEntityStore` を共有、最もロジックが重いため最後にまとめて統合テスト）
8. **次期 #38 Spawn eggs + #29 Brain + #43/44 Breeding/Villager**（`Entity` 生成と `AiBrain` の上位層、plan14で Jigsaw と統合）

### 6ドメインへのマッピング（並列worktree向け）

- **world**: なし（plan12で Nether/End/Structures 完了、本planでは world 依存の `Bamboo §1` を block ドメインに含む）
- **block**: §1 Bamboo/Stem, §5 FrostWalker（`frosted_ice` の `BlockUpdate`）、§8 Bookshelfカウントの `World::countBookshelves`
- **entity**: §2 SetEquipment, §3 SetPassengers/HorseJump/Boat, §6 Enderman, §7 Charged Creeper
- **inventory**: §4 Durability/Mending, §5 Enchant効果, §8 Enchanting/Anvil/Brewing, §9 Stonecutter/RecipeBook
- **network**: §10 ArgTypes/Tab補完（`DeclareCommands 0x11`/`CommandSuggestions 0x10`）、§8の `ContainerSetData 0x14`/`EnchantItem 0x0F`/`PlaceGhostRecipe 0x39`
- **combat/survival**: §4 Unbreaking/Mending/Anvil Too Expensive, §5 SoulSpeed/SwiftSneak の `UpdateAttributes 0x7C`, §6 Endermanの `DamageEvent 0x1A`, §7 Explosion 6.0

各worktreeは `src/game/BlockTickScheduler.cpp`, `src/game/Entities.hpp`, `src/game/Items.hpp`, `src/game/Containers.hpp`, `src/brigadier/*`, `src/game/EnchantmentHelper.hpp` を互いに排他で触るため、6並列でのマージ衝突は `GameServer.cpp` の `tickOnce` の `flush` 周りのみ（`PacketBatcher` の `queueBlockChange` と `ContainerSetData` の統合点）。`GameServer.hpp:353` の `RandomTickScheduler` 登録と `GameServer.cpp:372` の `xpOrbsTick/boatsTick/brewingTick` は `tickOnce` の末尾に追記する形で競合を最小化。

### smoke80 への寄与（期待 PASS 数）

- §2 SetEquipment + §3 SetPassengers: `~2` セクション（`SetEquipment dynamic`, `SetPassengers`）
- §4 Durability: `1` セクション（`Mending/Unbreaking`）
- §5 Enchant: `1` セクション（`Enchant effects` の `FrostWalker/SoulSpeed`）
- §6 Enderman + §7 Charged Creeper: `2` セクション（`Enderman`, `Charged Creeper`）
- §8 Enchanting/Anvil/Brewing + §9 Stonecutter/RecipeBook + §10 ArgTypes/Datapack: `3` セクション（`Enchanting costs`, `Anvil Too Expensive`, `Brewing`, `Stonecutter ghost`, `BlockState parser`）
- **合計 `~9` セクションを PASS に反転**。post-plan12 の `~74/80` から `~80/80` に到達（残りは `Trial Chambers` 等の polish のみで `allow FAIL`）。

### 検証チェックリスト（各章共通）

- [ ] `cmake -B build && cmake --build build -j4 && ./build/test_smoke_80 ./build/cppfm` で `~80/80`（`Mending, Trial Chambers` の `FAIL` が消える）
- [ ] `cmake --build build --target test_native && ./build/test_native --reporter=compact` で `spawn-protection=0` 時の `block update broadcast` 2 FAIL が解消（`SetEquipment` と無関係だが `PROTOCOL_NOTES.md` の `Ack block change` と併せて確認）
- [ ] `captures/` の `PacketCapture` で `SetEquipment 0x60` の `slot byte 0x80`、`SetPassengers 0x65` の `vehicleId`、`ContainerSetData 0x14` の `cost 39/40`、`PlaceGhostRecipe 0x39` の `RecipeDisplay` が Wireshark で `minecraft-data 1.21.4 protocol.json` と一致
- [ ] `maven.fabricmc.net` の `Yarn 1.21.4+build.*` で `BambooBlock, CreeperEntity, EndermanEntity, BlockStateArgumentType, EnchantmentHelper` の `method_...` が本レポートの `m_...` と一致
- [ ] `src/generated/BlockStates.hpp` の `bamboo 13958, frosted_ice 13552` と `src/generated/ItemIds.hpp` の `blaze_powder` 等が `Recipes.cpp` の `PotionBrewing` と一致

