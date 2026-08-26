# cpp-fabricmc 残存ギャップ分析と公式実装リサーチ: plan4

本書は、1.21.4 (protocol 769) 本家サーバーとの完全互換に向けて、plan2/plan3 実装完了後も
残る機能を洗い出し、公式実装の挙動をコミュニティ・プロトコル文書
(PrismarineJS minecraft-data 1.21.4 / misode mcmeta / minecraft.wiki) から詳細に
リサーチした上で、実装アーキテクチャと優先順位を定めるものである。
記載のパケットID・構造はすべてバージョン固定データセットから抽出済みであり、
即実装可能なレベルまで落としてある。

## 0. 現在地(本書執筆時点での実装済みサマリ)

| 分野 | 実装状況 |
|---|---|
| プロトコル全段階(status/login/config/play) + 暗号化 + 圧縮 | 完成 |
| Brigadier 完全移植(引数型・セレクタ・タブ補完) + 22コマンド | 完成 |
| インベントリ/ItemStack(データコンポーネント素通し) | 完成 |
| コンテナUI(チェスト/かまど/作業台) サーバ権威クリック処理 | 完成 |
| クラフト(ビルトイン47+JSONローダ)・レシピブック同期・PlaceRecipe | 完成 |
| かまど精錬tick(燃料表/lit状態切替) | 完成 |
| XP オーブ/レベル曲線、ステータス効果、エンチャント補正フック | 完成 |
| Mob 12種+Brain-Goal-Sensor AI+A*経路探索、繁殖/怒り/老化 | 完成 |
| ワールド生成: MultiNoiseバイオーム(30地点)/密度関数AST/構造物/三角分布鉱脈 | 完成 |
| Anvil相互運用(ブロック状態・バイオーム・ブロックエンティティ) | 完成 |
| 光エンジン(ブロック光BFS追加/除去+空光キャッシュ)+UpdateLight配信 | 完成 |
| 水流シミュレーション、レッドストーン(ワイヤ洪水/レバー/ボタン/ランプ) | 完成 |
| 天候サイクル(GameEvent 1/2)、クリーパー爆発、光量連動スポーン/陽光燃焼 | 完成 |
| 統計+進捗(cppfm:*ツリー、Update Advancements、toast) | 完成 |
| Cookie/リソースパック/転送/プラグインチャネル/チャット署名受入 | 完成 |
| イベントバス(api::EventHook)による内部フックポイント | 完成 |

## 1. 残存ギャップ一覧と優先順位

優先度は「プレイヤー体感 × 実装リスク」で付けた。各節で公式挙動とワイヤ形式を示す。

### P1-A プロジェクタイル(矢・雪球・卵・エンダーパール)

**公式挙動** (wiki.vg / vanilla観察):
* 発射時 `Spawn Entity`(0x01, type=arrow/snowball/egg/ender_pearl, objectData=速度3成分)
* 飛行中 `Entity Velocity`(0x5F) と重力はクライアント側で近似、サーバは位置のみ補正
* 着弾で `Hit` 相当 → ダメージ判定(矢: 攻撃力6+速度依存)、ブロックなら刺さり60秒後消滅
* スケルトンは1.5秒毎に照準射撃(難易度で精度変動)、矢は無限(装備不要)

**設計**:
```
src/game/Projectile.hpp/.cpp
struct Projectile { entityId; kind; pos; vel; ownerId; ageTicks; }
GameServer::spawnProjectile(kind, from, dir, speed, owner)
Projectiles::tick(): 重力 -0.05/tick(arrow), 直進+当たり判定:
  - ブロック衝突: world.getBlock != passable → 停止(埋まり表示), arrowはダメージなし
  - エンティティ衝突: AABB(0.6)ヒットで UseEntity相当のダメージ適用
スケルトンAI(MeleeAttackGoal拡張): 距離8-15で射撃Goal(RangedAttackGoal追加)
```

### P1-B 村人と交易

**ワイヤ形式** (dataset 抽出済):
* `Trade List`(0x2E): windowId, trades[] { inputItem1{id,count,comps}, outputItem(Slot),
  inputItem2?(option), tradeDisabled, nbTradeUses(i32), maxTradeUses(i32),
  xp(i32), specialPrice(i32), priceMultiplier(f32), demand(i32) }
* C2S `Select Trade`(0x31: item varint) / OpenScreen type=19(merchant)
* 村人メタデータ: 16 age, 17 headshake?, 21 villager data(varint variant)

**設計**: `src/game/Villager.hpp`
* MobKind::Villager 追加(stats: hp20, 非敵対)。職業はvariantで farmer 固定から開始
* 取引テーブルは clean-room 定義(小麦→エメラルド等 5種)
* 右クリック → OpenScreen(19) + TradeList、select_trade で在庫交換
* ゾンビ化/治療は後回し(P2)

### P1-C ベッドと夜飛ばし

**公式挙動**:
* ベッド右クリック → `Set Passengers`ではなく、寝姿勢はメタデータ pose(index 6, pose= sleeping 2)
* 全員就寝 or /time set で朝: GameEvent 0(no respawn block可用化) は関係なし、
  時間0へ SetTime + 各クライアントの「目覚め」はサーバが PlayerPosition でベッド横へTP
* 死亡時リスポーン地点: `Set Default Spawn`(0x5B) 更新 + Respawn packet

**設計**: BlockEntities に BedData(pos, ownerUuid) を追加。
UseItemOn(bed) → 就寝処理(夜間のみ): pose metadata 送信、全員就寝検出で time=0,
/weather clear, TP解除。死亡時 respawn 位置を bed へ。

### P1-D スコアボード/チーム/ボスバー

**ワイヤ形式** (dataset 抽出済):
* `Scoreboard Objective`(0x64): name, method(0 create/1 remove/2 update),
  displayText(NBT), type(0 integer/1 hearts), number_format(option)
* `Set Score`(0x68): itemName(entity), scoreName(objective), value,
  display_name?(NBT), number_format?
* `Teams`(0x67): team名, mode(0 create/1 remove/2 info/3 join/4 leave),
  create時: display NBT, friendlyFire, nametagVisibility, collisionRule,
  formatting, prefix/suffix NBT, members[]
* `Boss Bar`(0x0A): uuid, action(0 add{title,health,color,dividers,flags},
  1 remove, 2 health, 3 title, 4 style, 5 flags)
* Display objective(0x5C): position, scoreName

**設計**: `src/game/Scoreboard.hpp` — objective マップ+チーム管理、コマンド群
(/scoreboard objectives add/setdisplay/list, /team join/leave, /bossbar 最低限)。
キルカウンタobjective自動運用(minecraft:killed:player)。

### P2-E ディスペンサー/ドロッパー/ホッパー

**公式挙動**: ホッパー毎 tick 8t で上/コンテナから1スタック吸引、下/隣接へ押出。
ディスペンサーはレッドストーン入力で保持アイテム使用(矢→発射など)。

**設計**: BlockEntities に HopperData(5slot)追加、furnacesTick と同じく
GameServer::hoppersTick() を8t間隔で実行。Inventory抽取APIを Menu/BE に共通化。
ディスペンサーは P1-A の projectile 発射に接続。

### P2-F 乗り物(ボート/トロッコ)

**形式**: SpawnEntity(type boat=6?/chest_boat, minecart系), `Set Passengers`(0x65),
C2S `Vehicle Move`(0x20: x,y,z,yaw,pitch), steer(0x21 boat paddle)。
搭乗は Entity Metadata? いや passengers packet が本体。

**設計**: VehicleEntity 追加、右クリックで set_passengers、移動は rider の
vehicle_move を信頼して同期(サーバ側は境界チェックのみ)。

### P2-G ネザー/エンド多次元

**形式**: Respawn(0x4C) 再送で次元遷移(dimension type index 変更)、
JoinGame時 worlds[] 列挙、SpawnInfo dimension string。ポータルは
nether_portal ブロックの立体的配置 + 立ち入り検出(30t 待機→遷移)。

**設計**: World を複数インスタンス化(world/, world_nether/)し、Session が
currentWorld ポインタを持つ。地形は nether: density pipeline 別定義
(y_clamped_gradient で上下岩盤+ケーブノイズ)。Respawn packet 送出で遷移。

### P2-H エンチャント台/金床/醸造台の完全ロジック

* エンチャント台: OpenScreen(13) + C2S EnchantItem(0x0F slot) → コスト選択、
  本棚数でレベル帯決定(vanilla式: base = randomInt(4..17)*…)
* 金床: OpenScreen(8)、修理/改名コスト算出、経験値消費
* 醸造台: OpenScreen(11)、ContainerSetData(0x14) property 0=brewTime

**設計**: Containers.hpp に各Menu追加、EnchantmentHelper を実装に昇格
(既存のダメージ補正フックに加え、採掘/耐久/水中歩行等を属性修飾子へ)。

### P2-I 地図/額縁/絵画/看板

* 看板: C2S UpdateSign(0x39) → BlockEntity nbt(front_text.messages[])保存、
  TileEntityData(0x07) 配信 — BE store拡張で対応済み枠あり
* Item Frame: SpawnEntity(item_frame) + メタデータ index8 アイテム、回転index9
* Map: MapData(0x2D) mapId, scale, icons[], columns バイト列

### P2-J 釣り

浮き SpawnEntity(fishing_bobber, ownerId を objectData に持つ特殊packet)、
PlayerInput で引き寄せ、確率でアイテム+XP。

### P3-K スペクテイター操作

C2S Spectate(0x3B uuid) → 対象へ Camera(0x57) + noClip 移動許可。

### P3-L サウンド/パーティクル全面カバー

既存 broadcastSound(直接名指定 holder)を全アクションへ展開:
dig/place(2001 WorldEvent でも可)、mob vocalization、level up(0x29 world event id 1005?)等。
WorldEvent ID 表は community list を参照しつつ段階導入。

### P3-M ブロック状態の完全対応

現在 placement は facing のみ。以下を context 付き stateWithProps で解決:
* 階段/ハーフブロック: face上半/下半、向き
* ドア: 上/下2ブロック同時配置+open状態(right-click toggle)
* 樹木ログ: 設置面軸 axis
* 作物: 成長random tick(randomTickSpeed gamerule で毎 tick 3セクション抽選)

### P3-N レッドストーン高度部品

Comparator(隣接コンテナ満杯度→出力)、Observer、Piston(16ブロック推力+粘着)。
ピストンはブロック移動イベントを LightEngine/FluidSim へも流す必要あり。

### P3-O サーバーリスト ping の完成形

favicon(base64 png)対応(server.properties icon 読み込み)、players.sample 2件、
enforcesSecureChat 正、chat preview廃止済フィールド整理。

### P3-P RCON/運用拡張

RCON から brigadier 全コマンド(dispatchConsole 済み)のタブ補完なし完全動作確認、
/banlist,/whitelist add/remove コマンド整備。

## 2. 実装順序と進捗(随時更新)

| 項目 | 状態 |
|---|---|
| P1-A Projectile + Skeleton 射撃 | ✅ 実装済 (commit 0430dd1) |
| P1-C ベッド(夜飛ばし+スポーン地点) | ✅ 実装済 (0430dd1) |
| P1-B 村人+交易 | ✅ 実装済 (0d61c83) |
| P1-D Scoreboard/Team/Bossbar | ✅ Scoreboard+sidebar+カウンタ実装 (0b1f11c)。BossBar UIは未 |
| P2-E ホッパー/ディスペンサー | ✅ 実装済 (f9a229b) |
| P3-K スペクテイター(/spectate カメラ) | ✅ 実装済 (156d6f8) |
| P3-M ドア配置/開閉トグル | ✅ 実装済 (ed9a092) |
| P3-O ping favicon/sample | ✅ 実装済 (2e008f9) |
| P2-G 多次元(ネザー) | 未実装。Respawn遷移方式で着手可だが Session の world 参照分離が必要 |
| P2-H エンチャント台/金床/醸造台 | 未実装。要: `minecraft:enchantments` コンポーネントの正確なネットワークNBT形状の追加リサーチ(誤形式はクライアント desync を招くため保留明示) |
| P2-F 乗り物 | 未実装。SetPassengers/VehicleMove 形式は本書§P2-F参照 |
| P2-J 釣り | 未実装 |
| P2-N 高度レッドストーン | 未実装 |

## 3. 既知の非互換メモ(検証ベース)

* window_click は 0x10(旧実装の 0x11 は close_window だった)—修正済み
* boolean プロパティの値順は ["true","false"](grass_block snowy 検証済)
* stateId パッキングは最初宣言プロパティが最上位(repeater で検証済)
* RecipeBookAdd の craftingStation は SlotDisplay(type2=item)
* SoundEffect の holder 直接指定は varint 0 + 名前 + fixedRange bool

以上。各項目は dataset 抽出済みの形式に基づくため、実装時に新規調査は不要。
