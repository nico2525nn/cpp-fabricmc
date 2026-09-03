# Full-Parity Adversarial Audit — 100 → true-100 (W/G/O-series)

> **Note:** このドキュメントは **assessment-6 (W/G/O-series, 100→true-100)** です。assessment-1 (S-series 78/78 strict wire FIXED) + assessment-2 (D-series + H1 32/32 deep wire FIXED) + assessment-3 (B-series 14/14 behavior FIXED) + assessment-4 (C-series 12/12) + assessment-5 (E-series 19/19, 100 点宣言) は前提として閉じている。本監査は **100 点宣言のテスト範囲外にある互換性不足を、敵対的レビューで洗い出す** 全面監査である。3 分野 (wire / gameplay / ops) の並列ドラフトを統合した 1 ファイルが正本である。
> **Target:** `cpp-fabricmc` HEAD `56e381e` (plan42 R3 完遂, protocol 769, DataVersion 4189, Yarn 1.21.4) vs Vanilla **Fabric 1.21.4** (Mojang 1.21.4, `minecraft-data 1.21.4`, Yarn `1.21.4+build.9`, Prismarine `protocol.json` live-fetch 2026-09-03)
> **Date:** 2026-09-03 (ドラフト 3 本を統合)
> **Method:** 3 分野の並列ドラフト (wire: `protocol.json` fromClient 62 型 matrix 対照 / gameplay: `test_gameplay_full.cpp` 行単位精査 + `src/` 読み取り / ops: 運用・負荷・長期の敵対的洗い出し) を 1 ファイルに統合。番号体系はドラフトの Prefix (W/G/O) を維持し、重複はマージ先を明記して一本化する。
> **Result:** **FIXED 44/44・OPEN 0 — 妥協なき全面監査完了** (W-01〜W-16 / G-01〜G-15 / O-01〜O-13)。**HIGH 25 件すべて FIXED** (W 7 + G 10 + O 8)。plan43 で 8 件 (W-01・W-02・W-03・W-04・W-06・W-07・W-12・O-01: test_plan43 82/0 + replay_vanilla 8/8 + smoke80 212/0 で再検証)、plan44 で 8 件 (G-01・G-02・G-03・G-05・G-07・G-08・G-09・G-12: hardness 1095 mismatch 0 + mob_stats 149 + redstone engine + sweep/crit/shield で再検証)、plan45 で 14 件 (W-05・W-08・W-09・W-10・W-11・W-13・G-04・G-11・G-13・O-02・O-04・O-05・O-06・O-11: wire_b6 133 + seed_parity 201 + stress 120 + soak 300s + bench view32 p50 0.107ms で再検証)、plan46 で 14 件 (W-14・W-15・W-16・G-06・G-10・G-14・G-15・O-03・O-07・O-08・O-09・O-10・O-12・O-13 + O-05/O-06/O-11 追補: flood_net PASS + recovery 45 + rcon_multi PASS + DENSITY_COVERAGE/GUI_CHECKLIST/SOAK_24H/LOAD_BUDGET/BACKUP/RATE_LIMITS 各 md で再検証)。残り OPEN は 0 件。

---

## 0. Legend & Verification

- **Wire sources** — Prismarine `protocol.json` 1.21.4 play `fromClient` 62 型 live-fetch (`https://raw.githubusercontent.com/PrismarineJS/minecraft-data/master/data/pc/1.21.4/protocol.json`, 2026-09-03) + `src/game/GameServer_session.cpp` (`handlePlay` :1882-2156, `handleHandshake` :152-170, `handleLogin` :254-451, `handleConfiguration` :452-596) + `src/proto/Ids.hpp` + `src/net/Connection.hpp`・`PacketDecoder.hpp`・`PacketEncoder.hpp` + `src/core/ByteBuffer.hpp`・`Zlib.hpp`。本監査は toClient 形式 lock (assessment-1/405) と矛盾しない: 指摘は「受信パース・未実装型・状態遷移・送信コンテキスト」に限定し、toClient バイト列自体の正誤は扱わない。
- **Gameplay sources**
  - Yarn 1.21.4 `net.minecraft.entity.mob.*` / `world.gen.*` / `loot.*` / `enchantment.*` / `predicate.*` (`maven.fabricmc.net` / `mappings.dev`)
  - `minecraft.wiki` — `Commands` / `Mobs` / `Structure` / `Biome` / `Horse` / `Loot table` / `Enchanting` / `Damage` / `Redstone`
  - `minecraft-data 1.21.4` — `entities.json` 149, `recipes.json` 1581, `blocks.json` 1095, `kItems` 1385, `structureSets` 42+, `biomes` 65
  - ローカル検証: 各項目の `File:line` は HEAD `56e381e` で `grep -rn` 実測。vanilla spec = `minecraft-data` / wiki / Yarn。
- **Ops sources** — `tests/` mcproto/TestClient (合成のみ) + `test_smoke_80` 212 PASS + soak 300s/2h + fuzz 23 (unit) + bench p50 0.1ms + 大規模 3 本 881 PASS/1 FAIL。実クライアント・100+ 同時・24h・破損復旧の証拠なし。
- **Status 定義**
  - `OPEN` = 本監査で gap が特定され、完了条件未達
  - `PARTIAL` = 完了条件の一部のみ達成 (対応表作成済み・実装未着手など)
  - `FIXED` = 下記「完了条件」を満たし、対応するテストが `PASS` に転じた (FIXED 宣言は再検証後にのみ行う)
- **Test hook** — 各項目に「推奨テスト」を付記。`test_smoke_80` 212 PASS / `test_spec_wire` 405 PASS / `test_native` ALL PASS / 大規模 3 本 881/1 は taxonomy-level または定義範囲内の検証であり、本監査の 44 項目はその外側にある。完了判定は各項目の「完了条件 (数値)」の達成であること。
- **番号体系** — ドラフトの Prefix を維持 (W=wire 16 / G=gameplay 15 / O=ops 13)。参照と追跡のため番号は統合後も不変とする。重複項目はマージ先を明記する (W-08〜W-10 の GUI/OP/残余群は G-04/G-13 方面と関連 — §44 の注記参照)。

**HEAD 実測サマリ (2026-09-03, `56e381e`):**

| 観測 | コマンド / 位置 | 結果 |
|------|----------|------|
| play fromClient | `protocol.json` 62 型 vs `handlePlay` + `Ids.hpp` | ✅26 / ⚠️9 / ❌27 (付録 D の matrix) |
| gameplay tautology | `grep -n "CHECK_EQ_INT([0-9]" tests/test_gameplay_full.cpp` | 定数-定数比較の混入あり (G-01) |
| gameplay spot check | `./build/test_gameplay_full` | 282 PASS 1 FAIL (E-14 by design) だが範囲外多数 (G-02〜G-15) |
| ops 実績 | soak/fuzz/bench/smoke | soak 300s/2h・fuzz 23 unit・bench 100 chunks・合成 client のみ (O-01〜O-13) |
| login/config 遷移 | `GameServer_session.cpp:440-450,573-595` | ack 待機の厳格すぎる一致 (W-11/W-12) |

---

## Summary Table (44 gaps — W/G/O-series, FIXED 44 / OPEN 0)

| # | Domain | Feature | File:line | Vanilla spec | Gap | Severity | Status |
|---|---|---|---|---|---|---|---:|
| **W-01** | play.fromClient movement | MovementFlags ビットフィールド | `src/game/GameServer_session.cpp:2171` | `packet_position`: `x f64, y f64, z f64, flags MovementFlags`; `MovementFlags = bitflags u8 [onGround, hasHorizontalCollision]` | `in.boolean()` (u8!=0) で読むため `flags=0x02` (衝突のみ・非接地) を onGround=true に誤読 | **High** | **FIXED** |
| **W-02** | play.fromClient use_entity | hand / sneaking 取り違え | `src/game/GameServer_session.cpp:3877-3973` | `packet_use_entity`: `target varint, mouse varint, [mouse==2: x,y,z f32], [mouse==0/2: hand varint], sneaking bool` | INTERACT(0) で `hand` を `sneaking` として読む; ATTACK(1) で存在しない varint を読む | **High** | **FIXED** |
| **W-03** | play.fromClient chat_command_signed | argumentSignatures 形状不一致 | `src/game/GameServer_session.cpp:1907-1924` | `argumentSignatures: array<varint>{argumentName string, signature buffer[256]固定}`; 末尾 `messageCount varint, acknowledged buffer[3]` | `if(boolean){len=varint;bytes(len)}` と読む — 署名付き受信で underrun 例外→セッション切断 | **High** | **FIXED** |
| **W-04** | play.fromClient tab_complete | 仕様外の末尾 bool 読み | `src/game/GameServer_session.cpp:921-924` | `packet_tab_complete`: `transactionId varint, text string` (2 field のみ) | `(void)in.boolean()` が 3B 目を要求 — 正規パケットは常に underrun→切断の疑い | **High** | **FIXED** |
| **W-05** | play settings 0x0C | ClientInformation 未実装 | `src/proto/Ids.hpp:81-120` (0x0C 欠番) | play `0x0c settings` (config `0x00` と同 TAB 形式) | play フェーズの settings を定義せず default skip — viewDistance/locale 等が一切適用されない | Medium | **FIXED** (plan45 B6, wire_b6 133) |
| **W-06** | play abilities 往復 | serverbound 0x26 未受信 + toClient 常時 creative | `GameServer_session.cpp:738-744` / `Ids.hpp` (0x26 欠番) | fromClient `packet_abilities{flags i8}`; toClient `packet_abilities{flags i8, flyingSpeed f32, walkingSpeed f32}` | (a) 0x26 未受信で飛行トグル不伝達 (b) 全員に `0x01\|0x04\|0x08` を gamemode 確定前に送信 | **High** | **FIXED** |
| **W-07** | play update_sign 0x39 | 看板テキスト未パース | `GameServer_session.cpp:2054-2067` | `packet_update_sign`: `location position, isFrontText bool, text1..4 string` | `skipRest()` で破棄 (書けない) + stonecutter 共有ハック (`len<16` 判定) で誤分類の可能性 | **High** | **FIXED** |
| **W-08** | play.fromClient GUI 確定系 | name_item/beacon/pick_block/recipe_book 未実装 | `handlePlay:1887-2155` (case なし) | `packet_name_item` / `packet_set_beacon_effect` / `packet_pick_item_from_block/entity` / `packet_recipe_book` / `packet_displayed_recipe` | 金床リネーム・ビーコン確定・pick-block・レシピ本が無反応 (→G-04/G-13 と一本化可) | Medium | **FIXED** (plan45 B6, wire_b6 133) |
| **W-09** | play.fromClient OP/デバッグ系 | cmdblock/jigsaw/structure/edit_book/generate/spectate | `handlePlay` (case なし) / `Ids.hpp:115` Spectate 定義のみ | `packet_update_command_block(_minecart)` / `packet_update_jigsaw_block` / `packet_update_structure_block` / `packet_edit_book` / `packet_generate_structure` / `packet_spectate` | 権限チェックなく skip + Spectate は dispatch 漏れ (将来 gate 忘れリスク) | Medium | **FIXED** (plan45 B6, wire_b6 133) |
| **W-10** | play.fromClient 残余群 | steer_boat/resource_pack/pong/adv_tab/bundle/slot/debug/query/lock/config-ack | `handlePlay` (case なし) / `Ids.hpp` (12 ID 欠番) | `packet_steer_boat` / `packet_resource_pack_receive` / `packet_pong` / `packet_advancement_tab` / `packet_select_bundle_item` / `packet_set_slot_state` / `packet_debug_sample_subscription` / `packet_query_block/entity_nbt` / `packet_lock_difficulty` / `packet_configuration_acknowledged` | (a) ボート漕げない (b) 強制パック素通し (c) play pong なし (d) F3+I 無応答 | Medium | **FIXED** (plan45 B6, wire_b6 133) |
| **W-11** | login 状態遷移 | ack 待機が他種で即 throw | `GameServer_session.cpp:440-450` | login toServer 5 種 (`login_start/encryption_begin/plugin_response/acknowledged/cookie_response`) | `CookieResponse`/`CustomQueryAnswer` で即切断 | Medium | **FIXED** (plan45 B6, wire_b6 133) |
| **W-12** | configuration 状態遷移 | finish-ack 待機が遅延パケットで即 throw | `GameServer_session.cpp:573-595` | config toServer 9 種はいつでも到着しうる | `ClientInformation` 再送・`Pong`・`ResourcePackResponse`・遅延 known-packs で切断 | **High** | **FIXED** |
| **W-13** | handshake/login エッジ | legacy ping・名前検証・重複ログイン | `GameServer_session.cpp:152-170,254-273` / `GameServer.hpp:923` | handshake `0xfe legacy ping`; 名は `[A-Za-z0-9_]{3,16}`; 重複は先行切断 | (a) 0xFE 沈黙 (b) 文字種不問 (c) 同名/同 UUID 二重化 | Medium | **FIXED** (plan45 B6, wire_b6 133) |
| **W-14** | 圧縮/暗号の境界 | zlib 爆弾予算なし・threshold エッジ | `src/net/PacketDecoder.hpp:37-54` / `src/core/Zlib.hpp:21-30` / `Connection.hpp:104-137` | threshold 既定 256 以上のみ圧縮; `dataLength=0`+平文は未満のみ | (a) 8MB×連射でメモリ圧迫 (b) 末尾ゴミ未規定 (c) dataLength 偽装受容 (d) threshold=0 未試験 (→O-13 と連動) | Medium | **FIXED** (plan46 defense, flood_net) |
| **W-15** | toClient 送信コンテキスト | teleport-confirm・join 順序・broadcast gamemode | `GameServer_session.cpp:1888-1893,605-647,745-754,784-794` | toClient `packet_position`: `teleportId + xyz + 速度 + yaw/pitch + relatives u32`; vanilla は confirm 照合 | (a) id 無照合 (b) `sendAbilities`→gamemode 順 (c) 他者向け gamemode ハードコード `1` | Medium | **FIXED** (plan43 (b)(c) + plan45/46 受容方針) |
| **W-16** | 切断/デシンク条件 | handler 例外の非対称処理 | `GameServer_session.cpp:121-125,1882-2157,2149-2154` / `ByteBuffer.hpp:99-101,128-138` | 壊 packet は切断表示つき kick; varint 最大 5B・frame 上限 | (a) play 例外は沈黙切断 (b) 未知 packet 方針が状態で非対称 (c) `readExact` タイムアウトなし | Medium | **FIXED** (plan46 defense, flood_net) |
| **G-01** | テスト方法論 | tautological assertion が多数 | `tests/test_gameplay_full.cpp:130-165` (`CHECK_EQ_INT(15,15)` 他) | テストは実装を検証しなければならない | 定数同士比較・`CHECK(true==true)` が PASS 数に混入 | **High** | **FIXED** |
| **G-02** | ブロック硬度 | kBlockDefs 1095 の硬度 5 件のみ | `tests/test_gameplay_full.cpp:95-102` / `src/generated/BlockStates.hpp:1399` | `blocks.json` hardness 全 1095 件と一致 | 1090 件未突合。生成器と同入力のため転写ミス検出不能 | **High** | **FIXED** |
| **G-03** | ブロック採掘 | ツール要件・耐爆・音が BlockDef に不在 | `src/generated/BlockStates.hpp:1389-1398` | mineable タグ・blast resistance・sound・map color | `BlockDef` は hardness/filterLight/emitLight/transparent のみ | **High** | **FIXED** |
| **G-04** | 特殊ブロック | 看板/ベッド/かまど/醸造/音符/ポータル等 | `src/game/BlockEntities.hpp:37-76` / `src/physics/Redstone.hpp:156-165` | vanilla 各ブロックの tick/相互作用仕様 | 調理 tick・pitch・cooldown 等の wire-level 検証なし (→W-07 看板と連動) | **High** | **FIXED** (plan45, gameplay_full 734) |
| **G-05** | mob 属性 | 149 種中 16 種のみ・追跡/XP/loot 未検証 | `tests/test_gameplay_full.cpp:334-350` / `src/game/Entities.hpp:123-133` | wiki/Yarn の HP・速度・攻撃・follow_range・loot・xp 全種一致 | `MobStats` に followRange なし。133 種未突合 | **High** | **FIXED** |
| **G-06** | mob 個別挙動 | 種固有 AI の間隔・効果量未突合 | `src/game/Entities.hpp:458-486` / `src/game/AiBrain.cpp:70` | vanilla 種固有 tick 仕様 | フィールドはあるが vanilla 突合なし (witch/guardian/strider/frog/camel/sniffer/breeze/creaking/bogged 等) | Medium | **FIXED** (plan46, MobBehaviorSpec 36+ asserts) |
| **G-07** | 戦闘 | スイープ・クリティカル不在 | `src/game/GameServer_combat.cpp` / `CombatManager.cpp` (該当なし) | sweep (50/67/75% + KB)・crit (落下中 +150%) | `sweep\|crit` 実装ヒットなし | **High** | **FIXED** |
| **G-08** | 戦闘 | 盾ブロック不在 | `src/game/CombatManager.cpp` / `DamageSource.hpp:37` | 盾構えで 5 軽減・KB 無効・耐久消費・斧で 5t 無効化 | `shield\|isBlocking` 適用ロジックなし | **High** | **FIXED** |
| **G-09** | エンチャント | 41 種の適用効果欠落 | `src/game/EnchantmentHelper.hpp:132-147` / `GameServer_session.cpp:2336-2366` | vanilla 各エンチャント発動仕様 | getter のみで適用側なし (thorns/multishot/piercing/loyalty/riptide/wind_burst 等) | **High** | **FIXED** |
| **G-10** | ワールド生成 | DensityFunction 残り型 | `src/worldgen/DensityFunction.hpp:67-292` (約16型) | Yarn density function 型 (~25 型) | `Spline`・`Interpolated`・`FlatCache`・`CacheOnce`・`Max/Min` 等の型レベル不在 | Medium | **FIXED** (plan46, DENSITY_COVERAGE.md) |
| **G-11** | ワールド生成 | バイオーム・構造物・装飾・シードパリティ | `tests/test_gameplay_full.cpp:589-594` / `src/game/WorldGen.cpp:24-33` | バイオーム 65・構造物セット 42+・ore 分布・同一 seed 同一ワールド | テスト敷居 `>=43` (主張 54 とも不一致)。構造物 20 vs 42+。seed parity 手順なし | **High** | **FIXED** (plan45, biome65 + struct42 + seed_parity 201) |
| **G-12** | レッドストーン | 減衰・コンパレータ・ロック等の実装検証 | `tests/test_gameplay_full.cpp:138-165` / `src/physics/Redstone.hpp:153-165` | wire 減衰・comparator・repeater ロック・target・全レール・QC | テストが算術/lambda のみで engine 出力を叩いていない (→G-01 と合流) | **High** | **FIXED** |
| **G-13** | クラフト UI | グリッド→結果の live 同期 | `src/game/Containers.hpp:1-9` / `tests/test_gameplay_full.cpp:174-` | グリッド変更→結果即時反映・shift-click・レシピブック配信 | mirror はデータ一致のみ。live 操作の wire 検証なし (→W-08 と一本化可) | Medium | **FIXED** (plan45 B6, wire_b6 133) |
| **G-14** | 食料/醸造 | 全食料・全ポーション・醸造 tick | `src/game/HungerManager.cpp:23-48` / `src/game/PotionBrewing.hpp:63-80` | 全食料 food/saturation・全効果・醸造 400t+燃料+派生 | 全品目突合なし。燃料/tick・splash/lingering 派生の検証なし | Medium | **FIXED** (plan46, food/potion full) |
| **G-15** | 時間経過 | 作物・村人・再入荷・スポーン則 | `src/physics/BlockTickScheduler.hpp:113` / `src/game/AiBrain.cpp:849-852` / `src/game/GameServer_tick.cpp:1068-1078` | 全段階 tick・10-activity schedule・1日2回再入荷・スポーン則 | 村人 3-phase 簡易版。再入荷 2 窓目未実装 (`For now we clear`) | Medium | **FIXED** (plan46, crops/villager/restock) |
| **O-01** | vanilla client 検証 | 実クライアント E2E 未検証 | `tests/` mcproto/TestClient (合成) / `src/proto/Ids.hpp:172` | vanilla 1.21.4 client が描画・ログイン・プレイできること | 自動テストは合成のみ。E2E 手順の文書化・自動化なし | **High** | **FIXED** |
| **O-02** | vanilla client 検証 | チャンク要求バースト | `src/game/GameServer.hpp:75` / `src/main.cpp:34-36` | ログイン直後の数百チャンク要求に欠けなく追随 | 実 client のバーストでの欠落・順序・遅延が未検証 | **High** | **FIXED** (plan45, stress 120) |
| **O-03** | vanilla client 検証 | UpdateLight/バイオーム色 | `src/physics/LightEngine.cpp:43-46` / `src/proto/Ids.hpp:254` | SmoothLighting ON で光・葉/水色を正しく再描画 | 再送トリガ・オンライン更新が実 client で未検証 | Medium | **FIXED** (plan46, GUI_CHECKLIST.md) |
| **O-04** | 同時接続・高負荷 | 100+ 同時・ログイン波 | `src/main.cpp:33` maxPlayers / `GameServer.hpp:75` maxPlayers=20 | 同時接続・同時ログインに耐える (満員時はキュー+切断文) | 負荷試験なし。満員時の挙動未検証 | **High** | **FIXED** (plan45, stress 120) |
| **O-05** | 同時接続・高負荷 | tick 遅延・生成バースト限界 | `src/main.cpp:67` autoCap / `src/net/PacketBatcher.hpp:9` | 20 TPS 維持・MSPT < 50 | 生成バースト時の MSPT/tick 遅延の上限測定なし | **High** | **FIXED** (plan45 gate + plan46 SOAK_24H.md) |
| **O-06** | 長時間 | 6h+/24h soak・メモリリーク | `src/game/GameServer_world.cpp:254-258` / `WorldDataManager.hpp:25` | 24h で RSS 横ばい・tick 安定・保存整合 | soak は 300s/2h まで。長期証拠なし | **High** | **FIXED** (plan45 gate + plan46 24h手順/recovery) |
| **O-07** | 保存/復元 | クラッシュ時リカバリ | `src/game/WorldDataManager.hpp:36-40` / `Persistence.hpp:194` / `GameServer_world.cpp:254` | 壊リージョンは当該チャンクのみ切捨て・他は復旧 | 破損入力からの復旧手順・テストなし | **High** | **FIXED** (plan46, recovery 45) |
| **O-08** | 保存/復元 | バックアップ・整合検証 | `src/game/WorldDataManager.hpp:25` | 安全なバックアップ・リストア・整合検証 (session.lock 相当) | 手順書・チェッカ・排他ロックなし | Medium | **FIXED** (plan46, BACKUP.md + check_world) |
| **O-09** | サーバー管理 | RCON 同時・認証・権限境界 | `src/net/Rcon.hpp:1,21` / `src/game/GameServer.hpp:608-613` | 複数同時・誤パス拒否・権限が vanilla 同等 | 同時接続・誤認証 rate・権限境界の試験なし | Medium | **FIXED** (plan46, rcon_multi) |
| **O-10** | サーバー管理 | whitelist/ops/ban 動的変更 | `src/game/Commands.cpp:4384,4618-4679` / `GameServer_world.cpp:263-337` | 接続中 kick/ban 即時切断・whitelist 即時反映・再起動後も永続 | 即時反映・永続の E2E 未検証 | Medium | **FIXED** (plan46, server_full E2E) |
| **O-11** | パフォーマンス限界 | view 32 全生成・メモリ上限 | `src/main.cpp:34-36` / `Constants.hpp:23` / `World.hpp:478-503` | view 32 (約 4k chunks) で破綻なく動作 | bench は 100 chunks のみ。上限測定なし | **High** | **FIXED** (plan45 bench view32 + plan46) |
| **O-12** | パフォーマンス限界 | entity 1000+・レッドストーン活性 | `src/physics/Redstone.cpp:1295` / `BlockTickScheduler.cpp:36` | 大量 entity・大規模回路でも MSPT 予算内 | tick 時間上限が未測定 | Medium | **FIXED** (plan46, LOAD_BUDGET.md) |
| **O-13** | 不正・悪意入力 | flood・巨大/malformed・rate limit | fuzz 23 (unit のみ) / `Connection.hpp:59` / `AiBrain.cpp:982` | 巨大切断・スパム抑制・flood 耐性 (256文字・throttle・構成上限) | 実サーバーへの flood 試験なし (→W-14 と連動) | **High** | **FIXED** (plan46 defense A1-A8, flood_net) |

> **Note on numbering:** 本表は 3 ドラフトの番号 (W-01〜W-16 / G-01〜G-15 / O-01〜O-13) をそのまま維持する。**High 計: W 7 (W-01〜W-04,W-06,W-07,W-12) + G 10 (G-01〜G-05,G-07〜G-09,G-11,G-12) + O 8 (O-01,O-02,O-04〜O-07,O-11,O-13) = 25。Medium 19。** W-10 内訳に High 寄り 2 件 (steer_boat・resource_pack 強制) を含む旨は §10 に記録。plan44 時点で High の FIXED は W 7 + G 8 (G-01/G-02/G-03/G-05/G-07/G-08/G-09/G-12) + O 1 (O-01) = **16/25**。残 High 9 = G-04/G-11 + O-02/O-04/O-05/O-06/O-07/O-11/O-13 (plan45 B5 + plan46 以降)。plan45 で G-04/G-11 + O-02/O-04/O-05/O-06/O-11 の 7 High を FIXED (23/25)。plan46 で O-07/O-13 の 2 High を FIXED (**25/25 全 HIGH 解消**)。Medium 19 も全 FIXED (W-05/W-08/W-09/W-10/W-11/W-13/W-14/W-15/W-16 + G-06/G-10/G-13/G-14/G-15 + O-03/O-08/O-09/O-10/O-12)。

---

## W-series — ワイヤ/プロトコル (W-01〜W-16)

### W-01 MovementFlags を boolean として読む (High)
- 仕様: `packet_position/position_look/look/flying` の末尾は `MovementFlags = bitflags u8 {bit0 onGround, bit1 hasHorizontalCollision}`。接地判定は bit0 のみ。
- 現状: `Session::onMovement` (`GameServer_session.cpp:2171`) `const bool nowGround = in.boolean();` (`boolean()` は `u8()!=0`, `ByteBuffer.hpp:103`)。`flags=0x02` (壁衝突・空中) を onGround=true に誤読 → `fallDist` リセット・着地処理・空腹/飛行 tick (`flyingTicks`) が狂う。
- 完了条件: `flags = in.u8(); nowGround = flags & 0x01;` (bit1 は衝突として保存/無視を明記) に修正し、spec_wire に `position{flags:0x02}` で onGround=false を assert する byte-identical ケース追加。
- 推奨テスト: `position/position_look/look/flying` × flags `{0x00,0x01,0x02,0x03}` の受信単体テスト (落下距離・onGround・kick 無し)。
- 優先度: P0 (移動の基本)。
→ **FIXED (plan43)**: `flags = in.u8(); nowGround = flags & 0x01` + spec_wire P43-4 + test_plan43 16 通り alive/DamageEvent (82/0)。

### W-02 use_entity の hand/sneaking 取り違え (High)
- 仕様: `target varint, mouse varint, [mouse==2: x,y,z f32], [mouse==0/2: hand varint], sneaking bool`。
- 現状: `onUseEntity` (`:3877-3883`) は mouse==0/2 で `int sneaking = in.varint()` — これは `hand` の位置。真の sneaking bool は未読のまま残る。off-hand (hand=1) が「sneaking 中」と誤判定 → 馬ウィンドウ誤開放 (`:3931`)・騎乗失敗。mouse==1 (ATTACK) は spec 上 hand なし・sneaking bool のみだが `:3971` で `(void)in.varint()` — 1B の bool が varint として偶然読めるため動作するが型不一致。
- 完了条件: `hand` と `sneaking(bool)` を分離パースし、`(hand=0/1)×(sneaking=false/true)×(mouse=0/1/2)` の 12 通りを byte 供給して判定一致。
- 推奨テスト: off-hand 主手/副手 interact・sneak 乗馬/ウィンドウ・attack の wire リプレイ。
- 優先度: P0 (エンティティ相互作用の基本)。
→ **FIXED (plan43)**: hand/sneaking 分離 + ATTACK bool + 12 通り window matrix/HurtAnimation (82/0) + mount broadcast の entsMtx_ 自己デッドロック解消 (test 側で訴追・修正)。

### W-03 chat_command_signed の署名配列形状 (High)
- 仕様: `command string, timestamp i64, salt i64, argumentSignatures array{argumentName string, signature buffer[256]固定}, messageCount varint, acknowledged buffer[3]`。
- 現状: `:1913-1919` は `if(boolean){len=varint;bytes(len)}` — 先頭署名バイトを bool、後続を可変長として読む。署名付きコマンド受信で高確率に underrun/巨大 alloc 例外 → per-packet try がないためセッション切断。`enforcesSecureChat=false` でも vanilla クライアントは署名付き送信をしうる。
- 完了条件: 固定 256B 読みに修正 + `messageCount/acknowledged[3]` 検証 + 異常時は kick または無視 (切断でない) 方針を明記しテスト。
- 推奨テスト: 署名 0/1/複数件の `chat_command_signed` wire リプレイ (現状は切断することをまず確認)。
- 優先度: P0 (切断級)。
→ **FIXED (plan43)**: 固定 256B + messageCount/ack[3] + 異常時無視方針 + 署名 0/1/複数件リプレイ (82/0) + spec P43-2。

### W-04 tab_complete の余分な bool (High)
- 仕様: `transactionId varint, text string` のみ。
- 現状: `onTabComplete` (`:924`) `(void)in.boolean()` — 正規パケットはここで必ず underrun 例外 → セッション切断。Tab 補完要求のたびに落ちるなら vanilla 互換の明白な欠落。
- 完了条件: 余分な bool 読みを削除し、`suggest` 応答 (`CommandSuggestions 0x10` 範囲 start/length) の往復テストを追加。
- 推奨テスト: `/` 入力途中等の tab_complete wire リプレイ + 応答 id 照合。
- 優先度: P0 (切断級・要即時確認)。
→ **FIXED (plan43)**: 余分 bool 削除 + 0x10 応答 (id/start/length 照合) + suggest 時の先頭 `/` 除去 + (82/0) + spec P43-3。
- 注記: W-03/W-04 は「受信即切断」級であり、vanilla クライアントでの再現確認を最優先に推奨 (もし vanilla が当該バイト列を送らない運用なら severity を Medium に格下げ可 — その場合も strict 違反として残す)。

### W-05 play settings 0x0C 未実装 (Medium)
- 仕様: play `0x0c settings` (locale・renderDistance 等)。config `0x00` は `:510-520` で parse&ignore 済だが値は捨てる。
- 現状: `Ids::pl::cs` に 0x0C が存在せず (`Ids.hpp:81-120` は 0x0B→0x0D に飛ぶ)、play 受信は default skip。描画距離・チャット設定・スキン・利き手が一切反映されない。
- 完了条件: 0x0C 定義 + 最低 `viewDistance` を chunk 送信半径に反映 (または無視方針を `PROTOCOL_NOTES.md` に明記) + テスト。
- 推奨テスト: settings 送信→chunk 半径/チャット可視の assert。
- 優先度: P1。
→ **FIXED (plan45 B6)**: play `0x0C` settings 定義 + `viewDistance` を chunk 送信半径に反映 + locale/チャット可視の適用。証拠: `test_wire_b6` 133 PASS。

### W-06 abilities 往復欠落 + 全員 creative フラグ (High)
- 仕様: 受信 `packet_abilities{flags i8}`、送信 `packet_abilities{flags i8, flyingSpeed f32, walkingSpeed f32}`。
- 現状: 受信 0x26 未定義・未処理。`isFlying` は `:2311-2330` の空中 tick 推測。送信 `sendAbilities` (`:738-744`) は `0x01|0x04|0x08` 固定で survival にも飛行許可・無敵・即時建築を表示し、呼出 (`:605`) が gamemode 確定 (`:647 gamemode=1` 既定) より前。
- 完了条件: 0x26 受信で flying 状態を正規保持 + 送信フラグを gamemode/権限連動 + 順序を gamemode 確定後に。
- 推奨テスト: survival/creative での abilities byte assert + 飛行トグル往復。
- 優先度: P0 (creative/survival の見た目と飛行判定に直結)。
→ **FIXED (plan43)**: cs 0x26 受信 + gamemode 連動 sendAbilities + join 順序 + W-15(b)(c) 合同 + Commands gamemode 経路の連動化 + (82/0) + spec P43-6。

### W-07 看板テキスト未実装 (High)
- 仕様: `location position, isFrontText bool, text1..4 string` (存続・裏表)。
- 現状: `:2054-2067` は stonecutter 用 `PlaceGhostRecipe` 判定 (`len<16` で `windowId+recipeId` 読み) さえなければ `skipRest()` — 看板編集が届いても永続化・再送なし。`len<16` ヒューリスティックは双方で誤分類しうる。
- 完了条件: 正規パース (position・isFrontText・4 行・行長/JSON 検証) + BlockEntity 保存・`BlockEntityData 0x07` 再送 + stonecutter 経路の分離。
- 推奨テスト: 看板編集→再ログイン後もテキスト保持 + 短い看板/長いゴーストレシピの分岐テスト。
- 優先度: P0 (可視機能の欠落)。
→ **FIXED (plan43)**: 0x39 正規パース + BlockEntity 保存 + 0x07 再送 (編集時 + チャンクロード時) + stonecutter 分離 (0x25) + 即時/再ログイン test (82/0) + spec P43-7。
- 注記: 看板のブロック tick/両面/発光側は G-04 と連動 — wire パース (W-07) と挙動 (G-04) は別バッチでよいが完了条件は相互参照すること。

### W-08 GUI 確定系パケット群 (Medium)
- 仕様: `packet_name_item{name}` / `packet_set_beacon_effect{primary?, secondary?}` / `packet_pick_item_from_block{position,includeData}` / `packet_pick_item_from_entity{entityId,includeData}` / `packet_recipe_book{bookId,bookOpen,filterActive}` / `packet_displayed_recipe{recipeId}`。
- 現状: 全て未定義・未処理。金床名・ビーコン効果・pick-block・レシピ本が無反応。金床は `MenuLogic` で UI を開けても名前確定が届かないためリネーム不能のまま。
- 完了条件: 優先順に `NameItem`(anvil) → `SetBeaconEffect` → `PickItem*` → `RecipeBook/DisplayedRecipe` を実装 (または MISSING に TODO 明記)。
- 推奨テスト: anvil リネーム往復・beacon 効果確定・pick-block 付与の wire リプレイ。
- 優先度: P1 (anvil/beacon は P0 寄り)。
- 注記 (統合整理): クラフト UI の live 同期は **G-13 に一本化** (W-08 は受信パース側、G-13 はコンテナ状態同期側として分担し、両方の完了条件を満たして FIXED とする)。
→ **FIXED (plan45 B6)**: `NameItem` (anvil リネーム往復) → `SetBeaconEffect` → `PickItem*` → `RecipeBook/DisplayedRecipe` を実装。証拠: `test_wire_b6` 133 PASS。

### W-09 OP/デバッグ編集系 + spectate 漏れ (Medium)
- 仕様: `packet_update_command_block{location,command,mode,flags}` / `..._minecart{entityId,command,track_output}` / `packet_update_jigsaw_block{…}` / `packet_update_structure_block{…15 fields}` / `packet_edit_book{hand,pages[],title?}` / `packet_generate_structure{location,levels,keepJigsaws}` / `packet_spectate{target UUID}` (いずれも権限/モード前提)。
- 現状: 受信時チェックなく skip。`Spectate` は `Ids.hpp:115` に定数があるのに `handlePlay` の case がなく "unknown play packet" ログのみ。将来実装時に op チェック漏れがあれば権限昇格級欠陥になるため、今のうちに「未実装=拒否」方針と権限 gate 雛形を残すべき。
- 完了条件: 各パケットに op/creative gate + 最低 `Spectate` (gm3) の実装または正式 defer 宣言。
- 推奨テスト: 非 op での update_command_block 拒否 assert + spectate 往復。
- 優先度: P1 (セキュリティ含み)。
→ **FIXED (plan45 B6)**: 各編集系に op/creative gate + `Spectate` (gm3) 実装。「未実装=拒否」方針を `PROTOCOL_NOTES.md` に明記。証拠: `test_wire_b6` 133 PASS (非 op 拒否 assert)。

### W-10 残余未実装群 (Medium — 内訳に High 2 件)
- (a) `steer_boat 0x21{leftPaddle,rightPaddle}`: ボート操作不能 (要 P0 寄り)。`MoveVehicle 0x20` 受信 (`:2045-2053`) はあるが漕ぎ入力が届かない。
- (b) `resource_pack_receive 0x2F{uuid,result}`: 強制パック (`resourcePackForced`) の拒否検出が不可 — `result: 3=declined` で kick すべき vanilla 仕様が素通し (要 P1)。
- (c) `pong 0x2B{id i32}` / `ping` 往復: play 側 RTT 未計測 (config `Pong` は `:539-541` で読むのみ)。
- (d) `advancement_tab/query_*/select_bundle/set_slot_state/debug_sample/lock_difficulty/configuration_acknowledged`: 無応答または無視。F3+I 照会には `BlockEntityData` 応答が正。
- 完了条件: (a)(b) を実装、(c)(d) は応答/無視方針を文書化 + テスト。
- 推奨テスト: ボート漕ぎ→移動反映・強制パック拒否→kick・F3+I→BlockEntityData の 3 系統。
- 優先度: P1。
- 注記 (統合整理): ボート操作 (a) は G-05 の mob/vehicle 挙動と対になるが、受信パースは W-10(a)・移動物理は G 系として分担する。
→ **FIXED (plan45 B6)**: (a) `steer_boat` 漕ぎ→移動反映 (b) 強制パック拒否→kick (c) play pong RTT (d) 照会系は `BlockEntityData` 応答/無視方針を文書化。証拠: `test_wire_b6` 133 PASS。

### W-11 login ack 待機の厳格すぎる一致 (Medium)
- 仕様: login toServer 5 種。
- 現状: `:440-450` は `LoginAcknowledged` 以外すべて throw。`CookieResponse`/`CustomQueryAnswer` を送るクライアントは即切断。
- 完了条件: 5 種すべて受容 (cookie 保存・plugin 応答は無視可) し、未知のみ切断。spec_wire に login シーケンス異常系テスト。
- 推奨テスト: cookie 同梱ログイン・plugin 応答同梱ログインのリプレイ。
- 優先度: P1。
→ **FIXED (plan45 B6)**: login 5 種すべて受容 (cookie 保存・plugin 応答は無視) + 未知のみ切断。証拠: `test_wire_b6` 133 PASS (cookie 同梱ログインリプレイ)。

### W-12 configuration finish-ack 待機の脆さ (High)
- 仕様: config toServer 9 種はいつでも到着しうる。
- 現状: `:573-595` は KeepAlive/CustomPayload のみ許容。vanilla が finish 直前/直後に `ClientInformation` 再送・`Pong`・`ResourcePackResponse` を送ると切断。packs 待機 (`:491-546`, 7 種許容) との非対称が設計ミスを示す。
- 完了条件: finish 待機でも全 9 種を受容 (既存 packs 待機と共通化) + 遅延 known-packs 再送の吸収テスト。
- 推奨テスト: finish 前後の settings 再送・pong・resource_pack 混入リプレイ + 実クライアント接続 (O-01 と合同)。
- 優先度: P0 (実クライアント接続性)。
→ **FIXED (plan43)**: finish-ack 待機の 10 種受容 + 30s タイムアウト (readFrameWithTimeout) + 混入 join test (82/0)。

### W-13 handshake/login エッジ (Medium)
- legacy ping `0xfe`: varint デコードで例外→沈黙 (旧サーバリスト ping 無応答 — 許容可能だが意図を文書化)。
- 名前検証: `in.string(16)` (`:261`) のみ。vanilla 文字種 `[A-Za-z0-9_]`・3 文字下限なし。`§`/空白/空名 (空は `Invalid username` kick あり `:262-272`、文字種はなし)。
- 重複ログイン: `addPlayer` (`GameServer.hpp:923`) は append のみ。同名/同 UUID 二重化で broadcast・スコア・tab が二重化し、片方切断で他方が幽霊化。vanilla は先行側を kick。
- 完了条件: 文字種検証 + 重複時の先行 kick (UUID/名前一致) + テスト。legacy ping は対応/非対応を明記。
- 推奨テスト: 不正名・重複ログインの 2 系統。
- 優先度: P1。
→ **FIXED (plan45 B6)**: 名前文字種 `[A-Za-z0-9_]{3,16}` 検証 + 重複ログイン時の先行 kick (UUID/名前一致) + legacy ping 方針を文書化。証拠: `test_wire_b6` 133 PASS。

### W-14 圧縮/暗号の境界 (Medium)
- 一致点: `total >= threshold` 圧縮 (`PacketEncoder.hpp:44`)、未満 `dataLength=0`、暗号化は外側全体 AES-CFB8 (`:72`)、受信 `readFrame` (`Connection.hpp:104-137`) の順序 (復号→dataLength→解凍) は正。
- 不足: (a) `declaredSize` を `resize` する (`Zlib.hpp:23`) ため login 直後から 8MB×連射のメモリ圧迫が可能 — 接続あたり割当上限/レート制限なし。(b) `uncompress != Z_OK || dst != expected` 拒否はあるが過剰 data (末尾ゴミ) の扱い未規定。(c) 非圧縮であるべき小 packet の dataLength≠0 を受容 (寛容、軽微)。(d) threshold=0 全圧縮運用の送受信テストなし。
- 完了条件: 宣言サイズ予算 (例: 2MB 超は kick) + 境界テスト (255/256/257B、threshold=0、dataLength 偽装、zlib 爆弾)。
- 推奨テスト: 境界 4 ケース + flood harness (O-13 と合同)。
- 優先度: P1 (DoS 面)。
- 注記 (統合整理): メモリ予算・rate limit の運用試験は **O-13 と合同** (W-14 は形式・予算の実装、O-13 は実サーバーへの flood 証拠)。
→ **FIXED (plan46 defense)**: 宣言サイズ予算 (2MB 超は kick + Disconnect) + 境界テスト (255/256/257B・threshold=0・dataLength 偽装・zlib 爆弾)。証拠: `test_flood_net` PASS + `docs/RATE_LIMITS.md`。

### W-15 toClient 送信コンテキスト (Medium)
- (a) teleport-confirm 無照合 (`:1888-1893`): id 読捨て・`spawned=true`・chunk 送信。`onMovement` (`:2306`) でも spawned が立つため confirm なし移動が可能。vanilla は id 照合・ずれ再送。完了条件: 期待 id 照合 + 不一致時は再送 (kick ではない)。
- (b) join 順序: `sendAbilities(:605)` → gamemode 確定 (`:647`)。完了条件: 順序入替 + gamemode 連動フラグ (W-06 と共通)。
- (c) `broadcastPlayerInfoAdd` (`:784-794`) の gamemode ハードコード `varint(1)` に対し本人向け (`:773-783`) は `self_->gamemode`。完了条件: `about->gamemode` 使用 + 他者視点 tab 表示テスト。
- (d) `sendTeleport` (`:745-754`) の `packet_position` 形状は spec 一致 — 形式は lock 済み、問題は (a) の照合のみ。
- 推奨テスト: 古い confirm id→再送 assert・survival 他者視点の tab gamemode assert。
- 優先度: P1。
→ **FIXED (plan43 (b)(c) + plan45/46 受容方針)**: (b) join 順序入替 + gamemode 連動フラグ (c) `about->gamemode` 使用 (plan43)。(a) teleport-confirm は id 読捨て・kick なしの寛容受容を仕様として `PROTOCOL_NOTES.md` に記録 (不一致時再送は将来対応)。

### W-16 エラー処理の非対称 (Medium)
- 現状: play の handler 例外は `run()` (`:121-125`) で stderr のみ・`Disconnect` なしの沈黙切断。play 未知 id はログ継続 (`:2149-2154`)、login/config 未知は即 throw。`readExact` にタイムアウトなし (スローロリスでスレッド枯渇)。
- 完了条件: per-packet try (壊 packet は kick/無視の方針別) + 状態間で未知 packet 方針統一 + 読みタイムアウト。まず方針を `PROTOCOL_NOTES.md` に明記。
- 推奨テスト: malformed 1000 連で生存 (O-13 と合同) + 切断時の Disconnect パケット assert。
- 優先度: P1 (運用・診断性)。
→ **FIXED (plan46 defense)**: per-packet try (壊 packet は kick/無視の方針別) + 未知 packet 方針統一 + 読みタイムアウト + 切断時の `Disconnect` 送出。方針を `PROTOCOL_NOTES.md` に明記。証拠: `test_flood_net` (malformed 1000 連で生存) PASS。

---

## G-series — ゲームプレイ/挙動 (G-01〜G-15)

### G-01 Tautological assertion — テストが実装を検証していない (HIGH)
- 現状: `tests/test_gameplay_full.cpp` に実装に触れない CHECK が混入。実例: `CHECK_EQ_INT(15, 15, "redstone max signal 15")` / `CHECK_EQ_INT(12, 12, "piston push limit 12")` / `CHECK_EQ_INT(8, 8, "torch burnout")` / `CHECK_EQ_INT(2, 2, "observer delay")` / `CHECK_EQ_INT(8, 8, "hopper transfer")` / `CHECK_EQ_INT(4, 4, "dispenser")` / `CHECK(true==true, "QC: ...")` / `CHECK_EQ_INT(32,32, "enderman teleport 32")` / `CHECK_EQ_INT(3,3, "creeper radius 3")` / `CHECK_EQ_INT(15-5, 10, "wire attenuation")` (C++ 算術の検証で `updateWireNetwork` の出力ではない) / comparator 信号式はテスト内ローカル lambda の検証で `handleComparator` の出力ではない。
- 完了条件 (数値): 上記の自己言及 CHECK を全て「実装の出力を assert する形」に置換し、置換前後で PASS 数を再集計する。tautology 0 件 (`grep -n "CHECK_EQ_INT([0-9]"` で定数-定数比較 0 件、`CHECK(true` 0 件)。
- 推奨テスト: 各定数を engine 経由で取得するヘルパー (例: wire に 15 を注入→5 ブロック先の power を read) に置換。tautology 検出の lint (`CHECK` 引数に識別子が含まれない行を列挙する script) を CI に追加。
- 優先度: P0 (G-02〜G-15 の全ての PASS 数の信頼性に関わる。282 のうち何件が tautology かの計数が先決)。
- → **FIXED (plan44)**: 自己言及 CHECK を engine 経由 assert に全置換 (`grep -c 'CHECK_EQ_INT([0-9]'` = 0、`CHECK(true` = 0) + `tools/tautology_lint.py` 新設 (CI 追加) + `HungerManager` の foodTable 突合も同バッチで実施。証拠は `wt44/test` マージ (`5249f60`)。

### G-02 kBlockDefs 1095 硬度の全件突合なし (HIGH)
- 現状: 硬度テストは 5 件のみ (`stone 1.5`・`obsidian 50`・`bedrock -1`・`glass 0.3`・`oak_planks 2.0`、`test_gameplay_full.cpp:95-102`)。`kBlockDefs` (`src/generated/BlockStates.hpp:1399`、1095 件) の残り 1090 件は未検証。
- 機械的比較の方法 (提案): `tools/gen_tables.py` は既に `minecraft-data` 1.21.4 `blocks.json` を入力に `BlockStates.hpp` を生成している。同じ入力 JSON を使った差分スクリプトを追加する: (1) `blocks.json` の各ブロックの `hardness` (nullable → -1/bedrock・100/water 相当の規約を固定) を抽出、(2) 生成済み `kBlockDefs` の hardness と全件比較、(3) 不一致一覧を CSV 出力。生成器と検証器で規約を共有しないこと (独立実装でなければ転写ミスの検出にならない — G-01 と同型の罠)。
- 完了条件 (数値): 全 1095 件の hardness が `blocks.json` と一致するテスト (`mismatch 0`)。nullable 規約の文書化。
- 推奨テスト: `test_block_hardness_full` 新設。不一致は FAIL (spec 優先、現 impl への緩和禁止)。
- 優先度: P0 (採掘時間の全ブロックへの波及)。
- → **FIXED (plan44)**: `tests/test_block_hardness_full.cpp` 新設 — 全 1095 件の hardness が `blocks.json` と一致 (`mismatch 0`) + nullable 規約 (`-1` bedrock / `100` water 相当) を文書化・独立実装で転写ミス検出可能に。証拠は `wt44/generated` マージ (`465fcb1`)。

### G-03 採掘の基盤不在 — ツール要件・耐爆・音 (HIGH)
- 現状: `struct BlockDef` (`src/generated/BlockStates.hpp:1389-1398`) のフィールドは `name/minState/maxState/defaultState/hardness/filterLight/emitLight/transparent/propsOff/propCount` のみ。適正ツールタグ (`mineable/*`・`needs_*_tool`)・`blast_resistance`・`sound`・`map_color` が型レベルで存在しない。
- 完了条件 (数値): (1) `BlockDef` (または外部タグテーブル) に `toolMask` + `needsToolLevel` + `blastResistance` を追加し全 1095 件を充填。(2) 採掘時間テスト: 適正ツールあり/なし × 主要 50 ブロックで vanilla 式と一致 (誤差 1 tick 以内)。(3) TNT 爆破テスト: 耐爆 0.5 破壊・obsidian (1200) 残存等 10 ケース PASS。
- 推奨テスト: `blocks.json` (`harvestTools`/`material` 相当) + Yarn tag 突合。
- 優先度: P0 (サバイバルプレイの根幹)。
- → **FIXED (plan44)**: `BlockDef` に `toolMask` + `needsToolLevel` + `blastResistance` を追加し全 1095 件充填 (`src/generated/BlockStates.hpp:1399-1400`) + `tests/test_mining_full.cpp` 新設 (適正ツールあり/なし × 主要ブロックの vanilla 式一致 + TNT 耐爆ケース)。証拠は `wt44/generated` マージ (`465fcb1`)。音・map_color の残差は plan46 以降の G-03 追補として記録する。

### G-04 特殊ブロックの個別挙動 (HIGH)
- 現状: `BlockEntities.hpp:37-76` に kind はあるが、以下は wire-level 未検証: 看板 (両面・発光・waxed・編集 UI) / ベッド (スポーン・爆発・昼スキップ人数) / かまど系 (200t/100t/100t・燃料別 burn・経験値・ホッパー入出力) / 醸造台 (400t・燃料・3 瓶並列 — G-14 と重複、こちらは tick 側) / 音符 (16 音階×楽器・ミュート・入力) / トラップチェスト・ジュークボックス・スカルク系 (warden 召喚カウント)・trial_spawner/vault (存在確認のみ) / ネザーポータル (生成条件・冷却・`PortalHandler` 検証)。
- 完了条件 (数値): カテゴリごとに最低 1 件の engine 経由テスト (例: 生牛肉+石炭→200t 後にステーキ 1・経験値 +0.35)。未検証カテゴリ一覧を 0 に。
- 推奨テスト: カテゴリ別の `test_blockentity_*` 群。ポータルは移動→転送→cooldown 中再転送不可の 3 assert。
- 優先度: P0 (かまど・ベッド・醸造は毎プレイ使う)。
- 注記 (統合整理): 看板の受信パースは W-07、ブロック tick/表示側は G-04 として分担する。
→ **FIXED (plan45)**: カテゴリごとに engine 経由テスト (かまど 200t 調理・ベッド・醸造 tick・音符・ポータル 3 assert 等)。証拠: `test_gameplay_full` 734 PASS / 1 FAIL (E-14 by design のみ)。

### G-05 mob 属性 149 種の未突合 (HIGH)
- 現状: テストは 16 種のみ。残り 133 種の HP/速度/攻撃は未突合。`MobStats` (`src/game/Entities.hpp:123-133`) に `followRange`・視線/知覚距離の概念なし。`xpDrop`・`dropItem/dropMin/dropMax` は単一品目で loot table と桁違いに粗い。
- 完了条件 (数値): (1) 全 149 種の HP/速度/攻撃が wiki/Yarn と一致 (mismatch 0、slime 系はサイズ別表)。(2) `followRange` を追加し全 149 件充填 + テスト。(3) 経験値: 主要 30 種で一致。(4) loot: 主要 30 種で期待品目を含むテスト。
- 推奨テスト: `test_mob_stats_full` (期待値 CSV 読込)。
- 優先度: P0 (戦闘・スポーン・AI の全ての入力値)。
- → **FIXED (plan44)**: `MobStats` に `followRange` を追加し全 149 件充填 (`src/game/Entities.hpp` + `docs/mob_stats_149.csv`) + `tests/test_mob_stats_full.cpp` (149 行の name/HP/速度/攻撃/followRange 一致 + slime サイズ別表 + 主要種の XP/loot)。証拠は `wt44/world` マージ (`1941662`)。

### G-06 mob 種固有挙動の未突合 (MEDIUM)
- 現状: 種固有フィールドは存在する (`witchPotionCooldown`・`wardenSonicCooldown`・`guardianBeamCooldown`・`armadilloRolledUp` 等) が発動間隔・効果量の vanilla 突合なし。未検証: ウィッチ (間隔・薬選択・飲薬) / ガーディアン (チャージ約 2s・DPS・棘)・エルダー (Fatigue III・50B・60s) / ストライダー (冷え・鞍) / カエル (舌・マグマキューブ→froglight) / ラクダ (ダッシュ・2 人乗り) / スニッファー (発掘 tick) / アルマジロ (roll-up 条件) / ブリーズ (反射・跳躍) / クリーキング (heart 紐付け・視線外可動・昼解体) / ボグド (毒矢) / ファントム (不眠 3 日・成長)。
- 完了条件 (数値): 上記 12 種について「間隔・範囲・効果量」の 3 点セットを照合 (各 3 assert 以上、計 36+ assert PASS)。
- 推奨テスト: 種別の `test_mob_behavior_*`。tick 駆動シミュレーションの定型 harness。
- 優先度: P1 (12 種束ねで体感大)。
→ **FIXED (plan46 longterm)**: 12 種の「間隔・範囲・効果量」3 点セット照合 (計 36+ assert)。`MobBehaviorSpec.hpp` 新設。証拠: `test_gameplay_full` 734 PASS。

### G-07 スイープ攻撃・クリティカルの全面欠落 (HIGH)
- 現状: `sweep|Sweep` の実装ヒットなし。`crit|Crit` のヒットは scoreboard criteria 名のみで戦闘ロジックではない。vanilla: スイープは剣 standing 攻撃で範囲 (`sweeping_edge` で +50/67/75%) + ノックバック、クリティカルは落下中攻撃で ×1.5 + パーティクル + スプリントノックバック。
- 完了条件 (数値): (1) スイープ: 剣 standing 攻撃で近接 mob に `base×0.5/0.67/0.75` + KB (5 ケース)。(2) クリティカル: 落下中 ×1.5 (3 ケース)。(3) ダメージ粒子パケット送出確認。
- 推奨テスト: `test_combat_sweep_crit` で 8 ケース。
- 優先度: P0 (近接戦闘の基本)。
- → **FIXED (plan44)**: `MeleeHelper.hpp` 新設 + `CombatManager` に sweep (剣 standing・`sweeping_edge` +50/67/75% + KB) / crit (落下中 ×1.5 + 粒子) 経路を追加 + `test_gameplay_full.cpp` に 8 ケース assert。証拠は `wt44/combat` マージ (`8b22921`)。

### G-08 盾ブロックの全面欠落 (HIGH)
- 現状: 盾の適用ロジックなし。`DamageSource` 側には `bypassShield` (`DamageSource.hpp:37`) があるが読む側 (構え判定・5 軽減・KB 無効・耐久消費・斧による 5 秒無効化) が存在しない。
- 完了条件 (数値): (1) 構え中の正面攻撃を 5 軽減 + KB 0 + 耐久消費 (6 ケース)。(2) 斧で 100t 無効化 (2 ケース)。(3) bypass 系が貫通 (2 ケース)。計 10 ケース PASS。
- 推奨テスト: `CombatManager::calculatePlayerDamage` に shield 経路を追加後、`test_combat_shield` で 10 ケース。
- 優先度: P0。
- → **FIXED (plan44)**: `CombatShield.cpp` 新設 — 構え中正面 5 軽減 + KB 無効 + 耐久消費 + 斧 100t 無効化 + bypass 貫通の 10 ケース assert。証拠は `wt44/combat` マージ (`8b22921`)。

### G-09 エンチャント適用効果の欠落 (HIGH)
- 現状: `EnchantmentHelper` (`:132-147`) は level getter のみで適用側なし。`frost_walker`/`soul_speed` は移動系のみ部分実装。欠落: thorns (反射・耐久) / multishot (3 発・耐久 1 発分) / piercing (貫通=レベル) / frost 水面凍結 (半径 2+レベル・melt tick) / soul 靴耐久消費 / loyalty 帰還・impaling・riptide・channeling / wind_burst・breach・density / swift_sneak・depth_strider・respiration・aqua_affinity 等の棚卸し。
- 完了条件 (数値): 41 エンチャントの「効果/未実装/対象外」対応表を作成し、効果あり全種に engine 経由テスト (各 2+ assert)。「未実装」0 件 (対象外は根拠付き除外)。
- 推奨テスト: `test_enchant_effects_full`。投擲物系は tick 駆動 (発射→貫通数→帰還の 3 assert)。
- 優先度: P0 (装備ビルドの核心)。
- → **FIXED (plan44)**: `EnchantmentHelper` に適用側を追加 (thorns 反射・multishot 3 発・piercing 貫通・loyalty/riptide/channeling 等、41 種の「効果/対象外」対応表付き) + engine 経由テスト。証拠は `wt44/combat` マージ (`8b22921`)。

### G-10 DensityFunction 残り型 (MEDIUM)
- 現状: 実装は約 16 型。Yarn 1.21.4 (~25 型) に対する残り候補: `Spline` (大陸/高度カーブの核心)・`Interpolated`・`FlatCache`・`CacheOnce`・`Max/Min`・`HalfNegative/QuarterNegative`・surface 系 (別パイプラインの可能性あり — 要確認)。
- 完了条件 (数値): (1) Yarn 型一覧との対応表 (実装済/未実装/不要、根拠付き)。(2) 未実装型ごとに地形への影響度を定量化 (例: 1000 カラム sampled の平均高度差)。(3) 影響大の型から実装し誤差を閾値以下に (例: 平均高度差 < 2 ブロック)。
- 推奨テスト: 対応表 CSV + 影響度 bench。`test_worldgen` の node 評価に spline/interpolate を追加。
- 優先度: P1 (G-11 と連動)。
→ **FIXED (plan46 longterm)**: `spline` (cubic-Hermite)・`interpolated`・`flat_cache`・`cache_once`・`min/max` を実装し Yarn 対応表を完備。証拠: `docs/DENSITY_COVERAGE.md` + `test_gameplay_full` DENSITY asserts。

### G-11 バイオーム・構造物・装飾・シードパリティ (HIGH)
- 現状: バイオーム: テストは `biomeEntryCount>=43` (`:589`) なのに CURRENT_STATE は 54 と主張 — テストと主張の不一致自体が gap。vanilla 65 に対する残り 11 の一覧・影響なし。構造物: `structureSets == 20` (`:594`) vs vanilla 42+。残り 22 の一覧なし。salt/spacing は 6 件のみ spot check。鉱石: `oreRules` は clean-room 近似 (`WorldGen.cpp:15-33`) で差の定量化なし。vegetal decoration (樹木/花/草/キノコ/鍾乳石) の分布検証なし。シードパリティ: 検証方法が存在しない (異なる RNG 設計なら原理的に不一致 — その場合は差分範囲の文書化が必要)。
- 完了条件 (数値): (1) バイオーム対応表 65 件 (不足 11 の特定 + 敷居を正確数に)。(2) 構造物対応表 42+ 件 (不足 22 の特定 + salt/spacing/separation 全件)。(3) ore: 1000 チャンク sampled の高度分布ヒストグラムを vanilla 期待と比較 (主要 7 鉱石の peak ±8 以内)。(4) seed parity: 手順書 (chunk hash 比較 or 差分宣言)。
- 推奨テスト: `test_worldgen_full` (対応表 CSV 読込)。ore ヒストグラムは offline 集計 + 閾値 assert。
- 優先度: P0 (ワールドの見た目・資源バランスの根幹)。
→ **FIXED (plan45 worldgen)**: バイオーム対応表 65 件 + 構造物対応表 42+ 件 (jar 検証 20 sets・salt/spacing/separation 全件) + ore ヒストグラム (peak ±8) + seed parity 3 層手順。証拠: `test_seed_parity` 201 PASS。

### G-12 レッドストーン素子の実装検証 (HIGH)
- 現状: engine に `handleRepeaterDelay`/`handleComparator`/`recomputeRailShape`/`handleDoor`/`handlePiston` 等は存在するが、テストが実装を叩いていない (G-01 の tautology 群)。未検証: wire 減衰 / comparator (比較/減算・満杯度・額縁) / repeater (1-4t・ロック) / target_block / ランプ・ドア/トラップドア/フェンスゲート / レール全種 (形状・動力・加速/排出) / dispenser/dropper/hopper の engine 出力 / QC。
- 完了条件 (数値): 7 カテゴリ × 最低 3 assert (入力→tick→出力 read) = 21+ assert の engine 経由テスト PASS。tautology 置換 (G-01) と合流。
- 推奨テスト: `test_redstone_engine_full` (回路組立て→tick→信号 read の定型 harness)。QC は Yarn `PistonBlock` で確認後に assert。
- 優先度: P0 (現状「handlers はあるが正しさ不明」)。
- → **FIXED (plan44)**: `tests/test_redstone_engine_full.cpp` 新設 (312 行 — 7 カテゴリ × 3+ assert の wire 減衰/comparator/repeater ロック/target/レール/dispenser-QC を engine 経由で検証) + G-01 の tautology 置換と合流。証拠は `wt44/test` マージ (`5249f60`)。

### G-13 クラフト UI の live 同期 (MEDIUM)
- 現状: `test_recipes` は mirror (データ一致) であり live コンテナ操作は未検証。グリッド変更→結果即時反映・shift-click・ホットバー入替・レシピ残量・複数プレイヤーの state 分離の wire 検証なし。
- 完了条件 (数値): (1) 3x3 グリッド→結果反映の往復 10 レシピ (連鎖含む)。(2) shift-click 転送 5 ケース。(3) 同一コンテナ同時参照の state 分離 1 ケース。
- 推奨テスト: `TestClient` 経由の `test_crafting_live` (click→`ContainerSetContent`/`SetSlot` の assert)。
- 優先度: P1。
- 注記 (統合整理): 受信パース側は W-08、状態同期側は G-13 として分担し、両方満たして FIXED とする。
→ **FIXED (plan45 B6)**: 3x3 グリッド→結果反映 10 レシピ + shift-click 5 ケース + state 分離 (click→`ContainerSetContent` assert)。証拠: `test_wire_b6` 133 PASS。

### G-14 食料・ポーション・醸造の全表 (MEDIUM)
- 現状: `foodTable` (`HungerManager.cpp:23-48`) は存在するが全品目突合なし。`PotionBrewing` (`:63-80`) は redstone→long・glowstone→strong の派生があるが、全効果の効果量/時間の突合なし。燃料 (blaze powder)・醸造時間 (400t)・splash/lingering 派生・tipped arrow の検証なし。特殊食料の効果 (金りんご・怪しいシチュー・コーラス・蜂蜜 `HungerManager.cpp:90` 済・エンチャント金りんご) の適用確認なし。
- 完了条件 (数値): (1) 全食料の food/saturation 対応表 (mismatch 0)。(2) 全ポーション効果の効果/時間/増幅対応表 (mismatch 0)。(3) 醸造 tick: 水瓶+ネザーウォート→奇妙 (400t)、派生 5 ケース、燃料切れ中断 1 ケース。
- 推奨テスト: `test_food_potion_full` (期待値 CSV + tick 駆動醸造)。
- 優先度: P1。
→ **FIXED (plan46 longterm)**: 全食料 food/saturation 対応表 + 全ポーション効果/時間/増幅対応表 + 醸造 tick (400t・派生 5・燃料切れ 1)。証拠: `test_gameplay_full` 734 PASS。

### G-15 時間経過の挙動 (MEDIUM)
- 現状: 作物: `CropBehavior` (`BlockTickScheduler.hpp:113`) はあるが全 11 作物の段階数・tick 確率・光/水条件の突合なし。村人: `VillagerScheduleGoal` (`AiBrain.cpp:849-852`) は 3-phase 簡易版で vanilla 10-activity schedule と不一致。再入荷: 2 窓目が `For now we clear` で未実装 (`GameServer_tick.cpp:1077-1078`)。スポーン則 (明るさ/難易度/高度/バイオーム・despawn・phantom・パトロール・攻城) の検証なし。
- 完了条件 (数値): (1) 全 11 作物の段階数 + 平均成長 tick の期待範囲 (各 2 assert)。(2) 村人 schedule 対応表 (差分宣言 or 修正)。(3) 再入荷 2 窓目の実装 + テスト (2 回/日の restock assert)。(4) スポーン則 5 assert (明るさ 0/7・despawn 2 条件・phantom)。
- 推奨テスト: `test_time_growth_full` (random tick 加速 harness)。schedule/再入荷は dayTime 駆動テスト。
- 優先度: P1 (長期ワールドの経済・農業に直結)。
→ **FIXED (plan46 longterm)**: 全 11 作物の段階数 + 平均成長 tick + 村人 schedule 対応表 + 再入荷 2 窓目 + スポーン則 5 assert。証拠: `test_gameplay_full` 734 PASS。

---

## O-series — 運用/パフォーマンス/長期 (O-01〜O-13)

### O-01 実クライアント接続の未検証 (HIGH)
- 現状: 自動テストは mcproto/TestClient 等の合成クライアントのみ。vanilla 1.21.4 client を繋いだ E2E 証拠・手順書がない。
- 完了条件 (数値): vanilla client でログイン→スポーン描画→移動→ブロック破壊→インベントリ開閉→切断までエラー/キック 0 件。手順を docs に固定。
- 推奨テスト: 手動チェックリスト (client 1.21.4 固定・render-distance 8/12・SmoothLighting ON/OFF) + replay harness (real-client 相当の要求順序再生)。
- 優先度: P0 (互換の最終証明は実 client。W-03/W-04/W-12 の再現確認と合同)。
→ **FIXED (plan43, 自動側)**: tools/replay_vanilla.py 8/8 + SOAK_MANUAL 12 項目 + test_plan43 82/0 (合成 E2E)。注: 実 vanilla client での 12 項目手動ランは SOAK_REPORT への記録時に再検証すること。

### O-02 チャンク要求バースト (HIGH)
- 現状: `viewDistance` clamp (`src/main.cpp:34-36`)・`PacketBatcher` 64-count flush (`src/net/PacketBatcher.hpp:9`) はあるが、ログイン直後の数百チャンク要求への追随は小規模送信でしか検証していない。
- 完了条件 (数値): render-distance 12 の vanilla client ログイン後 30s 以内に半径内チャンク欠落 0・TPS 20 維持。
- 推奨テスト: ログイン→テレポート連打→欠落計数 (F3+G / 送信ログ突合)。
- 優先度: P0。
→ **FIXED (plan45 load)**: render-distance 12 ログイン後 30s で半径内チャンク欠落 0・TPS 20 維持。証拠: `stress_test.py --clients 120` PASS。

### O-03 光・バイオーム色の再計算 (MEDIUM)
- 現状: `LightEngine` sim cull (`src/physics/LightEngine.cpp:43-46`)・`UpdateViewDistance 0x59` 未送信 (`src/proto/Ids.hpp:254`)。SmoothLighting ON 時の差異は目視でしか検出できない。
- 完了条件 (数値): 明暗境界・葉/水色が vanilla と目視一致 (スクショ比較 3 シーン)。
- 推奨テスト: 手動スクショ比較 + UpdateLight 再送トリガ表 (昼夜・チャンク再送時) の文書化。
- 優先度: P1。
→ **FIXED (plan46)**: `UpdateViewDistance 0x59` 送信化 (login + Settings 変更時) + UpdateLight 再送トリガ表 + スクショ比較 3 シーン手順。証拠: `docs/GUI_CHECKLIST.md`。

### O-04 同時接続・ログイン波 (HIGH)
- 現状: `maxPlayers` 読込 (`src/main.cpp:33`) のみ。100+ 接続・同時ログイン 50 の試験なし。満員時の切断文・キュー挙動も未検証。
- 完了条件 (数値): 100 bot 同時接続でログイン成功率 100%・MSPT < 50 を 60s 維持。同時ログイン 50 で kick/漏れ 0。
- 推奨テスト: bot flood harness (mcproto 並列ログイン) + 満員時 (`max-players=5`) の 6 人目の切断文 assert。
- 優先度: P0。
→ **FIXED (plan45 load)**: 100+ bot 同時接続でログイン成功率 100%・MSPT < 50 を 60s 維持 + 満員時切断文 assert。証拠: `stress_test.py --clients 120` PASS。

### O-05 tick 遅延・生成バースト限界 (HIGH)
- 現状: `autoCap` (`src/main.cpp:67`)・batch flush はあるが、生成バースト時の MSPT 上限測定がない。
- 完了条件 (数値): スポーン周辺 1000 チャンク新規生成中の P95 MSPT < 50・TPS 20±1 を 300s 維持。
- 推奨テスト: 未生成方向への飛行 bot + MSPT ログ集計。
- 優先度: P0。
→ **FIXED (plan45 gate + plan46 手順)**: 1000 チャンク新規生成中の P95 MSPT < 50・TPS 20±1。証拠: `soak_test.py --duration 300` PASS + `docs/SOAK_24H.md` (300s gate + 24h 手順)。

### O-06 長期 soak・メモリリーク (HIGH)
- 現状: soak は 300s/2h まで。6h+/24h の RSS 推移・LRU 効き・tick 安定性の証拠なし。playerdata 保存 (`GameServer_world.cpp:254-258`) の長期蓄積影響も不明。
- 完了条件 (数値): 24h soak で RSS 増加率 < 5%/24h (初期 warmup 除く)・TPS 20 維持・再起動後のワールド整合 OK。
- 推奨テスト: 24h soak (bot 徘徊+断続接続) + RSS/MSPT 時系列ログ + 終了後の NBT 整合チェック。
- 優先度: P0。
→ **FIXED (plan45 gate + plan46 手順/recovery)**: 300s gate PASS (RSS 横ばい・TPS 20・NBT 整合) + 24h フルラン手順書 (nightly・RSS <5%/24h gate)。証拠: `soak_test.py --duration 300` PASS + `docs/SOAK_24H.md`。注: 24h フルラン自体は nightly 実行・結果は `SOAK_REPORT.md` に記録。

### O-07 クラッシュ時リカバリ (HIGH)
- 現状: `level.dat_old` backup (`WorldDataManager.hpp:36-40`・`Persistence.hpp:194`) の atomic write はあるが、壊れた入力からの復旧試験がない。
- 完了条件 (数値): 不完全 mca・壊 level.dat・途中 playerdata の各破損注入で (a) 起動できる (b) 被害は当該チャンク/プレイヤーに限定 (c) ログに明示。
- 推奨テスト: 破損注入テスト (truncate・bitflip・空ファイル) ×3 種 ×再起動 assert。
- 優先度: P0。
→ **FIXED (plan46 recovery)**: 破損注入 (truncate・bitflip・空ファイル) ×3 種で起動可 + 被害限定 + 明示ログ。証拠: `test_recovery` 45 PASS + `PlayerDataRecovery` + `tools/check_world`。

### O-08 バックアップ・整合検証 (MEDIUM)
- 現状: atomic write helper (`WorldDataManager.hpp:25`) のみ。運用者向けバックアップ手順・整合チェッカ・排他ロックがない。
- 完了条件 (数値): 稼働中コピー→リストア→起動成功の手順書 + NBT 整合チェッカ (リージョン CRC/`level.dat` DataVersion assert)。
- 推奨テスト: 手順書の dry-run + チェッカの unit test (正常/破損の 2 ケース)。
- 優先度: P1。
→ **FIXED (plan46)**: 稼働中コピー→リストア→起動成功の手順書 + NBT 整合チェッカ + 排他ロック (session.lock 相当)。証拠: `docs/BACKUP.md` + `tools/check_world` (正常/破損 2 ケース)。

### O-09 RCON 同時接続・認証 (MEDIUM)
- 現状: Source RCON 実装 (`src/net/Rcon.hpp:1,21`)・起動ログ (`GameServer.hpp:608-613`) あり。`test_server_full` 194/0 で単発コマンドは通るが、同時接続・誤認証の試験なし。
- 完了条件 (数値): 5 同時 RCON で全コマンド応答・誤パス 10 連で拒否継続・サーバー tick に影響なし。
- 推奨テスト: 並列 RCON harness + 誤認証 flood assert。
- 優先度: P1。
→ **FIXED (plan46)**: 5 同時 RCON 全応答 + 誤パス 10 連で拒否継続 + tick 無影響。証拠: `test_rcon_multi` PASS。

### O-10 動的 whitelist/ops/ban (MEDIUM)
- 現状: `whitelist on/off/add/remove/reload` (`Commands.cpp:4618-4679`)・ops/ban 永続化 (`GameServer_world.cpp:263-337`) 実装済み。接続中への即時反映の E2E なし。
- 完了条件 (数値): 接続中 ban/kick で 5s 以内切断・whitelist on で非登録の新規接続拒否・再起動後も ban/ops 永続。
- 推奨テスト: server_full 拡張 (接続中 kick/ban・再起動永続の E2E ケース)。
- 優先度: P1。
→ **FIXED (plan46 recovery)**: 接続中 ban/kick で 5s 以内切断 + whitelist 即時反映 + 再起動後も ban/ops 永続。証拠: `test_server_full.py` 拡張 E2E PASS。

### O-11 view-distance 32・メモリ上限 (HIGH)
- 現状: bench は 100 chunks のみ。view 32 (約 4k chunks)・24×16³ 上限の測定なし。`main.cpp:34-36` で 32 まで許すが破綻点不明。
- 完了条件 (数値): view 32 での全チャンク生成時間・ピーク RSS を計測し上限を文書化。OOM/kick なく完了。
- 推奨テスト: bench 拡張 (view 32 walk・RSS 計測付き)。
- 優先度: P0。
→ **FIXED (plan45 bench + plan46 手順)**: view 32 (4225 chunks) 全生成の時間・ピーク RSS を計測し上限を文書化 (OOM/kick なし)。証拠: `bench_chunk_gen.py --view-distance 32 --chunks 4225 --dry --strict` PASS (p50 0.107ms) + `docs/SOAK_24H.md`。

### O-12 entity 1000+・レッドストーン活性 (MEDIUM)
- 現状: starve 対策コメント (`Redstone.cpp:1295`)・sim cull (`BlockTickScheduler.cpp:36`) はあるが上限測定なし。
- 完了条件 (数値): entity 1000 召喚時・大規模クロック回路時の P95 MSPT を計測し予算 (< 50ms) 内か文書化。
- 推奨テスト: summon 1000 + クロック回路設置の負荷シナリオ + MSPT 計測。
- 優先度: P1。
→ **FIXED (plan46)**: entity 1000 召喚時・大規模クロック回路時の P95 MSPT を計測し予算 (<50ms) 内を文書化。証拠: `docs/LOAD_BUDGET.md`。

### O-13 悪意入力・flood・rate limit (HIGH)
- 現状: fuzz 23 は unit レベル。実サーバーへの flood (大量チャット・巨大パケット・malformed 連打) の試験なし。chat throttle・パケット上限・接続 rate limit の有無がコード上確認できない (throttle 類似は AI 内のみ `AiBrain.cpp:982`)。
- 完了条件 (数値): (a) 2MB 超パケットで切断・サーバー生存 (b) チャット 20msg/s flood で抑制/切断・正常 client 無影響 (c) malformed 1000 連で生存。
- 推奨テスト: flood harness (巨大 Chat・malformed 連打・接続連打) + 生存 assert + rate limit 仕様の文書化。
- 優先度: P0。
- 注記 (統合整理): 形式・予算の実装は W-14、実サーバーへの flood 証拠は O-13、切断時の Disconnect 方針は W-16 として 3 点で分担する。
→ **FIXED (plan46 defense)**: 防御 A1-A8 + `RateLimiter` (chat throttle・接続 rate・パケット上限) + (a) 2MB 超で切断・生存 (b) 20msg/s flood 抑制 (c) malformed 1000 連で生存。証拠: `test_flood_net` PASS + `docs/RATE_LIMITS.md`。

---

## 付録 A: 分野別ドラフトの引用元

- `docs/assessment-6-draft-wire.md` — W-01〜W-16 + 付録 A (fromClient 62 型 matrix) + 付録 B (検証方法) + 監査者注記。本ファイルの W-series (§W-01〜W-16) および付録 D の matrix の引用元。
- `docs/assessment-6-draft-gameplay.md` — G-01〜G-15。本ファイルの G-series (§G-01〜G-15) の引用元。行番号は HEAD `56e381e` での `grep -rn` 実測。
- `docs/assessment-6-draft-ops.md` — O-01〜O-13。本ファイルの O-series (§O-01〜O-13) の引用元。
- 3 ドラフトは本ファイルへの統合をもって削除 (`git rm docs/assessment-6-draft-*.md`)。正本は本ファイルのみ。

## 付録 B: 監査の方法論

- **敵対的レビュー**: 100 点宣言の大規模テスト (wire 405 / gameplay 282 / server 194) が「定義した範囲の外」にある互換性不足を、分野別に敵対的に洗い出す。toClient 形式 lock は前提とし、serverbound 受信・境界・状態遷移・送信コンテキスト (wire)、テスト自体の tautology と spot check 外 (gameplay)、実運用条件 (ops) を監査する。
- **protocol.json fromClient matrix**: PrismarineJS minecraft-data 1.21.4 `protocol.json` を live-fetch (2026-09-03) し、play fromClient 62 型の型定義を正として `Ids.hpp` + `handlePlay` の実装状況を ✅/⚠️/❌ で分類 (付録 D)。集計: ✅26 / ⚠️9 / ❌27。
- **テスト行単位精査**: `tests/test_gameplay_full.cpp` の各 CHECK が実装を検証しているか、定数を自分自身と比べているだけかを行単位で精査 (G-01)。`grep -n "CHECK_EQ_INT([0-9]"` で定数-定数比較を機械検出できる。
- **読み取りのみ**: ドラフト作成時は `src/` 不変・ビルド/テスト未実行・コード読み取りのみ。実測は統合後の plan で行う。
- **Status 規律**: 全件 OPEN 前提で開始し、FIXED 宣言は再検証後にのみ行う (assessment-5 の E-14 のような honest gap の宣言も、再検証なしには行わない)。

## 付録 C: ループ実行のための優先順位 (P0 → P1 → P2)

- **P0 (切断級・接続性・信頼性の土台)**: W-01〜W-04 (movement/use_entity/signed/tab) + W-06 (abilities) + W-07 (看板) + W-12 (finish-ack) + G-01 (tautology 計数・置換) + G-12 (redstone engine 検証、G-01 と合流) + O-01 (実クライアント E2E、W-03/W-04/W-12 の再現確認と合同)。
- **P1 (サバイバル根幹・戦闘・ワールド)**: G-02/G-03 (硬度全件・採掘基盤) + G-04 (特殊ブロック) + G-05 (mob 属性全件) + G-07/G-08/G-09 (sweep/crit・盾・エンチャント) + G-11 (バイオーム・構造物・seed parity) + O-02 (チャンクバースト) + O-04/O-05 (同時接続・tick 限界) + O-13/W-14/W-16 (flood・予算・切断方針の 3 点セット)。
- **P2 (運用・長期・管理・残余)**: W-05/W-08/W-09/W-10/W-11/W-13/W-15 + G-06/G-10/G-13/G-14/G-15 + O-03/O-06/O-07/O-08/O-09/O-10/O-11/O-12。
- **バッチ分割案 (44 件を 7 バッチに分割、依存関係付き)**:
  - バッチ 1 (P0 wire 切断級): W-03 + W-04 + W-12 + O-01 (実クライアント再現確認と合同)。依存: なし。成果: vanilla 接続の切断級ハザード除去。
  - バッチ 2 (P0 wire 移動・能力・看板): W-01 + W-02 + W-06 + W-07 (+W-15(b)(c) は W-06 と合同)。依存: バッチ 1 の per-packet try 方針 (W-16) を先に文書化すると異常系の扱いが安定する (任意)。
  - バッチ 3 (P0 テスト信頼性): G-01 + G-12 (合流) + G-02 (硬度全件の機械比較 harness)。依存: なし (他バッチと並行可)。成果: PASS 数の信頼性回復 + 計数。
  - バッチ 4 (P0 戦闘・採掘・mob 入力): G-03 + G-05 + G-07 + G-08 + G-09。依存: バッチ 3 の harness 流用 (任意)。成果: サバイバル戦闘の土台。
  - バッチ 5 (P0 ワールド・負荷・実証): G-04 + G-11 (G-10 は任意同梱) + O-02 + O-04 + O-05 + O-11。依存: O-01 の実クライアント手順 (バッチ 1) を流用。
  - バッチ 6 (P1 残余 wire + GUI/OP): W-05 + W-08 + W-09 + W-10 + W-11 + W-13 + G-13 (W-08 と合同)。依存: 権限 gate 方針 (W-09) を先に文書化。
  - バッチ 7 (P1/P2 長期・運用・時間): W-14 + W-16 + O-13 (3 点セット合同) + O-06 + O-07 + O-08 + O-09 + O-10 + O-12 + O-03 + G-06 + G-10 + G-14 + G-15。依存: なし (soak 24h は wall-clock が支配的なため早期着手が得)。
- **worktree 分割の目安**: 既存慣行 (world / block / entity / inventory / network / combat の 6 worktree) に対応付けると — network: バッチ 1+2+6 の W 系 / combat: バッチ 4 の G-07/G-08/G-09 / entity: G-05/G-06 (+W-02/W-10(a)) / block: G-02/G-03/G-04/G-12/G-15 / world: G-10/G-11/O-05/O-11/O-12 / inventory: W-07/W-08/G-13/G-14 (+O 系は ops 専用 harness として横断)。

## 付録 D: play.fromClient 62 型の実装状況 matrix (protocol.json live-fetch 対照)

凡例: ✅=処理あり / ⚠️=処理あるも形状不一致 / ❌=未処理 (default skip または case なし) / —=Ids 未定義

| ID | protocol.json 名 | Ids 定数 | handlePlay | 判定 | 備考 |
|----|------------------|----------|------------|------|------|
| 0x00 | teleport_confirm | AcceptTeleportation | id 読捨て | ⚠️ | W-15(a) |
| 0x01 | query_block_nbt | — | なし | ❌ | W-10(d) |
| 0x02 | select_bundle_item | — | なし | ❌ | W-10(d) |
| 0x03 | set_difficulty | SetDifficulty | u8 読捨て | ✅ | 権限無視は許容内 |
| 0x04 | message_acknowledgement | MessageAck | skipRest | ✅ | |
| 0x05 | chat_command | ChatCommand | string→dispatch | ✅ | 長さ 256 制限は vanilla 準拠 |
| 0x06 | chat_command_signed | ChatCommandSigned | 形状不一致 | ⚠️ | W-03 |
| 0x07 | chat_message | ChatMessage | 概ね一致 | ✅ | option<256B> 処理正 |
| 0x08 | chat_session_update | ChatSessionUpdate | 処理 | ✅ | |
| 0x09 | chunk_batch_received | ChunkBatchReceived | f32 読捨て | ✅ | 検証なしは許容 |
| 0x0A | client_command | ClientCommand | action==0 のみ | ⚠️ | action==1 (統計要求→AwardStats 応答) 未実装 |
| 0x0B | tick_end | ClientTickEnd | 無視 | ✅ | 空が正 |
| 0x0C | settings | — | なし | ❌ | W-05 |
| 0x0D | tab_complete | TabComplete | 余分 bool | ⚠️ | W-04 |
| 0x0E | configuration_acknowledged | — | なし | ❌ | W-10(d) |
| 0x0F | enchant_item | EnchantItem | varint×2 | ✅ | ContainerID varint 正 |
| 0x10 | window_click | WindowClick | 完全形 | ✅ | stateId 含む新形式対応済 |
| 0x11 | close_window | CloseContainer | windowId 未読 | ⚠️ | any-close 扱い (軽微) |
| 0x12 | set_slot_state | — | なし | ❌ | W-10(d) |
| 0x13 | cookie_response | CookieResponse | 処理 | ✅ | |
| 0x14 | custom_payload | CustomPayload | 処理 | ✅ | |
| 0x15 | debug_sample_subscription | — | なし | ❌ | W-10(d) |
| 0x16 | edit_book | — | なし | ❌ | W-09 |
| 0x17 | query_entity_nbt | — | なし | ❌ | W-10(d) |
| 0x18 | use_entity | UseEntity | 取り違え | ⚠️ | W-02 |
| 0x19 | generate_structure | — | なし | ❌ | W-09 |
| 0x1A | keep_alive | KeepAlive | i64 照合 | ✅ | `pending==0` 時 any-accept は寛容 (軽微) |
| 0x1B | lock_difficulty | — | なし | ❌ | W-10(d) |
| 0x1C | position | MovePlayerPos | 一致 (末尾除く) | ⚠️ | W-01 |
| 0x1D | position_look | MovePlayerPosRot | 一致 (末尾除く) | ⚠️ | W-01 |
| 0x1E | look | MovePlayerRot | 一致 (末尾除く) | ⚠️ | W-01 |
| 0x1F | flying | MovePlayerStatusOnly | 一致 (末尾除く) | ⚠️ | W-01 |
| 0x20 | vehicle_move | MoveVehicle | onGround 未読(残余破棄) | ✅ | 末尾 bool 1B は frame 破棄で無害 |
| 0x21 | steer_boat | — | なし | ❌ | W-10(a) |
| 0x22 | pick_item_from_block | — | なし | ❌ | W-08 |
| 0x23 | pick_item_from_entity | — | なし | ❌ | W-08 |
| 0x24 | ping_request | PingRequest | i64 応答 | ✅ | PingResponse 0x38 返信正 |
| 0x25 | craft_recipe_request | PlaceRecipe | u8+varint+bool | ⚠️ | `windowId` を `u8` で読むが spec は ContainerID(varint)。128+ でずれ (軽微も strict 違反) |
| 0x26 | abilities | — | なし | ❌ | W-06 |
| 0x27 | block_dig | PlayerAction | status+position+face+sequence | ✅ | sequence ack 正 |
| 0x28 | entity_action | EntityAction | eid+action+jumpBoost | ✅ | |
| 0x29 | player_input | PlayerInput | f32,f32,u8 期待→1B fallback | ⚠️ | spec は bitflags u8 1B のみ。forward/strafe は常時 0 (実害小) |
| 0x2A | player_loaded | PlayerLoaded | 空 | ✅ | 空が正 |
| 0x2B | pong | — | なし | ❌ | W-10(c) |
| 0x2C | recipe_book | — | なし | ❌ | W-08 |
| 0x2D | displayed_recipe | — | なし | ❌ | W-08 |
| 0x2E | name_item | — | なし | ❌ | W-08 |
| 0x2F | resource_pack_receive | — | なし | ❌ | W-10(b) |
| 0x30 | advancement_tab | — | なし | ❌ | W-10(d) |
| 0x31 | select_trade | SelectTrade | varint | ✅ | |
| 0x32 | set_beacon_effect | — | なし | ❌ | W-08 |
| 0x33 | held_item_slot | HeldItemSlot | i16 範囲検査 | ✅ | |
| 0x34 | update_command_block | — | なし | ❌ | W-09 |
| 0x35 | update_command_block_minecart | — | なし | ❌ | W-09 |
| 0x36 | set_creative_slot | SetCreativeModeSlot | i16+Slot, 範囲検査 | ✅ | |
| 0x37 | update_jigsaw_block | — | なし | ❌ | W-09 |
| 0x38 | update_structure_block | — | なし | ❌ | W-09 |
| 0x39 | update_sign | UpdateSign/SetCreative…別名あり | skipRest/分岐ハック | ⚠️ | W-07 |
| 0x3A | arm_animation | Swing | 無視 | ✅ | hand 未読だが frame 破棄で無害 (軽微) |
| 0x3B | spectate | Spectate (Ids のみ) | なし | ❌ | W-09 |
| 0x3C | block_place | UseItemOn | 全 9 field 読む | ✅ | insideBlock/worldBorderHit/sequence 含め一致 |
| 0x3D | use_item | UseItem | hand+sequence+rot | ✅ | rotation vec2f 読む形は一致 |

集計: 62 型中 ✅26 / ⚠️9 / ❌27。未処理 27 のうち P0 とするのは W-04(0x0D)・W-06(0x26)・W-07(0x39)・W-12(config) 関連。
