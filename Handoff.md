# cpp-fabricmc — 現在作業 Handoff

> 更新: 2026-09-22 JST
> 対象: Minecraft Java Edition 1.21.4 / Fabric-compatible C++ server / protocol 769
> 用途: 次のエージェントが、過去の議論・実装・検証・未達境界を混同せず引き継ぐための内部文書。

## 最新セッション追補（2026-09-22）

この節を最初に確認すること。旧節には過去時点の測定値が残っているため、状態判断はこの節、
`docs/CURRENT_STATE.md`、GitHub Actionsの実行結果を優先する。

- 作業ブランチは `chore/goal-cleanup-next`、HEADは `d875bd657c4eb3306d7145e2306e6311a8ba7b1a`（`test: assert live completion and gameplay fields`）。`origin/chore/goal-cleanup-next`と一致し、確認時の作業ツリーはclean。
- GitHub PRは [#1](https://github.com/nico2525nn/cpp-fabricmc/pull/1)（base `main`）でOPEN。実装・テスト・文書の変更はこのPRへ積み、`main`へ直接pushしない。
- 今回のコミットは `tests/test_goal_live_features.py` と、`docs/audit/goal-feature-ledger.md`、`docs/audit/goal-followup-evidence-ledger.md` の3ファイル。既存のowned live fixtureで、`/gi`のsuggestions packet（transaction/start/length/match/tooltip）、`/effect give @s speed 5`（EntityEffectのentity/effect/amplifier/duration/flags）、`/xp add @s 5 points`（progress/level/total）を厳密にdecode/assertした。
- 台帳の主張は保守的に `PASS=13 / PARTIAL=39 / UNVERIFIED=38` のまま。#59、#89、#90は観測したフィールドだけを記録し、全引数候補、XP orb、全effect/removal semanticsは未証明としてPARTIALを維持する。
- ローカルの `py_compile`、feature live fixture、remaining live fixtureはPASSし、fixtureのowned server cleanupも確認済み。scratchログはリポジトリへ追加していない。
- HEADのpushでGitHub Actionsが新規発火している。実行IDはpushごとに変わるため固定値を記録せず、`gh run list --branch chore/goal-cleanup-next --limit 1 --json databaseId,headSha,status,conclusion` でHEAD `af8cb09a`に対応するrunを特定する。その前の試行ではSmoke80の起動競合/timeoutとremaining fixtureの一時的なchat raceで失敗したため、最新runの最終 `success` を確認するまでCI PASSと記載しない。
- Actionsが失敗した場合でも、テストのassertを弱めて通してはいけない。失敗ログを保存し、必要なら同じrunを一度だけ再実行し、startup raceか実装回帰かを分離する。
- ユーザー指定により、この引き継ぎ以降はSwarmを使わず、サブエージェントは原則2体程度まで。今回の追補では新規サブエージェントを起動していない。
- 10%/20%削減や1万行削減の数字合わせのため、feature・protected test・fixture・assertion・evidenceを削除しない。現在もその目標は未達で、callsite-zeroの公開APIはABI審査なしに削除しない。

この文書は公開READMEではない。Plan番号、サブエージェント、CodexのGoal、未コミット差分、
検証の限界などをここに記録する。公開利用者向けの説明はREADME.mdとdocs/README.mdを使う。

## 0. 最初に読むファイル

1. `AGENTS.md` — worktree、研究、サブエージェント、timeout、プロセス回収、Git安全規則。
2. `docs/CURRENT_STATE.md` — 現在の測定値、plan履歴、残課題、公開状態の正規tracker。
3. `docs/VERIFICATION.md` — 各PASSが証明する範囲と証明しない範囲。
4. `docs/DEVELOPMENT.md` — canonical module mapとresearch workflow。
5. `docs/MISSING_FEATURES_1_21_4.md` — numbered matrixとdeclared limitation。
6. `docs/audit/adversarial-review-2026-09-19.md` — 直近hardening passの敵対的レビュー。
7. `docs/MC_PILOT_REAL_TEST.md` — mc-pilot / PrismLauncherのローカル実機証跡。

## 1. 現在の状態

| 項目 | 状態 |
|---|---|
| 対象 | Fabric 1.21.4 / DataVersion 4189 |
| protocol | 769 |
| branch | `chore/goal-cleanup-next` |
| 現在のHEAD | `d875bd657c4eb3306d7145e2306e6311a8ba7b1a` — `test: assert live completion and gameplay fields` |
| origin branch | `origin/chore/goal-cleanup-next` とHEADが一致（確認時点） |
| tracker上の実装baseline | `335fca5`（plan53/54の統合基準） |
| JDK | OpenJDK 21。JNI/JVM検出済みのビルド環境 |
| publication status | `BLOCKED` |
| settings/security/authority/lifecycle pass | 実装・検証済み。関連コミットはPR #1へpush済み |

確認時点の作業ツリーはclean。既存のPR差分を分類せずに `git reset --hard`、`git checkout --`、
`git clean`、広範な削除を実行してはならない。次のHandoff更新も作業ブランチでcommitし、PRのActionsを通す。

## 2. 結論を先に

このプロジェクトは、protocol 769のログイン・wire・基本gameplay・設定・認証・権限・イベント・
永続化・負荷・JVM境界の限定された面を、かなり広く実装し、名前付きテストで検証できる状態にある。
直近のsettings/security/authority/lifecycle hardeningは、記録されたgateをすべて通過している。

しかし、これは「Minecraft全機能の完全互換」や「任意Fabric modが動く」ことの証明ではない。
公開状態はBLOCKEDのままであり、少なくとも次の境界が残る。

- 受入済みの2時間/24時間soak証跡がない。
- 任意のFabric JVM mod、公式GameProvider、universal bytecode互換ではない。
- vanilla worldgenの全Xoroshiro call orderとstructure NBT parityは未証明。
- すべてのreal client/GUIとすべてのModrinth artifactを検証したわけではない。
- signed commandのargument transcriptは復元せず、enforced secure chatではfail closed。
- moving pistonのtransient NBTをそのまま永続化していない。

## 3. ユーザーが決めた重要な前提

- 対象は1.21.4 / protocol 769。以前混入したprotocol 776は対象外で、設計・評価・READMEで追わない。
- Fabric互換のJava/JVM機能は、configure/build時にJDK/JNIが見つかるバイナリではdefault-on。
  JNIなしでビルドしたバイナリへ、後からJDKを置いてJVM機能を追加することはできない。
- 「五感を持つ」ための実装方針は、Sidecarではなくプロセス内HotSpot/JNIを選んだ。
  直接callback、opaque handle、tick-thread routingを得られる一方、JVMクラッシュはサーバーに影響し、
  任意Fabric Loaderや公式GameProviderを提供するものではない。
- one-file配布を目標にする。LinuxではCPack ZIPに実行ファイル1個を入れ、初回起動時に
  `world/`、`mods/`、`config/`、`libraries/`、`logging/`、`resourcepacks/`、runtime cache、
  `server.properties`などを作る。Windows/macOSは各ホストnative buildが必要。
- READMEは公開利用者向け文書。Plan、Goal、サブエージェント、内部会話はREADMEに書かない。
- Smoke80は特別な合格扱いではなく、通常の回帰CTestの一つ。現在の結果は224ケース。
- 実Modは、対象artifactと実行条件が揃った範囲だけPASSとする。対象外artifactを無理に互換性の証拠にしない。
- 1万行削減・20%削減は品質目標であって、feature、fixture、assertion、証跡を削除して数字だけ合わせてはいけない。
- すべての長いbuild/test/benchmarkはtimeout付きで実行し、終了後に所有プロセスが残っていないことを確認する。

## 4. ここまでの作業の流れ

### 4.1 以前の実装・統合

- protocol 769のwire、login/configuration/play、compression、AES-CFB8、RCON、chunk、NBT、Anvil、
  world、worldgen、blocks、entities、AI、items、containers、recipes、menus、commands、
  redstone、fluids、light、persistenceなどを段階的に実装した。
- plan49ではmining、mob behavior、world/block/entityなどの統合とcleanupを進め、`Structures.hpp`の
  legacy APIを削除した。これはstructure generation parityそのものの証明ではない。
- plan51ではembedded JVM boundaryを実装した。これは任意の公式Fabric runtimeを丸ごと埋め込むものではなく、
  dependency-free shadow ABIとselected integration surfaceである。
- plan53では設定・properties・再起動・復旧・lifecycleのmatrixを追加した。
- plan54ではsettings/security/authority/lifecycleのhardening、writer/dispatch/persistence barrier、
  process ownership、敵対的レビュー、CIを統合した。

### 4.2 20% / 1万行削減について

安全なcleanupの最終測定は、clean `65a7c69` baselineに対するもの。

| 測定 | 結果 |
|---|---:|
| baseline | 96,654行 / 293ファイル |
| current measurement | 98,648行 / 298ファイル |
| mutable lines | 87,715 → 89,709 |
| net | `+1,994`行（`+2.28%`） |
| strict reduction target | 18,341行削減、`PARTIAL` |
| protected manifest | 80ファイル / 8,939行、hash drift 0 |

設定証跡とlifecycle matrixを増やしたため、削除より追加が多くなった。20%削減は未達であり、
1万行削減も達成済みではない。数字のためにテスト・fixture・機能・証拠を消すのは禁止。

## 5. 現在の実装内容

### 5.1 設定・認証・secure chat

- `ServerConfig`にdifficulty、seed、resource-pack、secure-profile、secure-chatなどの型付き設定を追加。
- textual seedはJava `String.hashCode()`、数値seedは符号付きint64として扱う。
- secure profileとenforced secure chatを分離。
- Mojang profile certificateはRSA-SHA1、chat message signatureはRSA-SHA256。
- signed chat transcript、LastSeen offset、ChatSessionUpdateのstrict failure pathを実装。
- signed command argument transcriptは再構築せず、enforced secure chat下ではfail closed。
- resource-pack UUID、hash、forced、finish-ackを検証。
- server.propertiesとCLIの優先順位、不正値、範囲、unknown key、help/versionの副作用をmatrix化。

### 5.2 権限・gameplay・event

- root commandを正規化し、非OP許可リストを分離。
- gamemode、pick-block/entity range、beacon payment/effect/range、sign protection/dirtyを検証。
- block place/break/click callbackをmutation前に実行し、キャンセル可能にした。
- callback後にdoor、slab、generic placement、creative breakの対象状態を再取得して再検証。
- scoped event callbackのremoveはin-flight callbackと同期。
- item command sourceは元のdimensionを保持。

### 5.3 tick・永続化・ネットワーク・ライフサイクル

- mutatingなtick/session/console/handler遷移をrecursive simulation dispatch gate下に統一。
- pistonのsource/destination pending commitを、同期・worker snapshot前にflush。
- 接続ごとのbounded writer queue、4 MiB/client cap、graceful close 100 ms、overflow closeを実装。
- frameはsimulation gate中にencodeし、writer lifecycle lockでenqueue/join raceを防止。
- encrypted AES-CFB8 frameはlow-priority reorderを禁止し、状態依存FIFOを維持。
- chunk-dependent broadcastはlow priority化。
- teardownはgraceful close、即時異常系はabort。
- Python/C++ harnessはowned PID/groupだけをterminate/wait/reapし、別テストをkillしない。

### 5.4 JVM/Fabric境界

- process-internal HotSpot/JNI。Sidecarではない。
- opaque native handle、generation-safe invalidation、tick-thread executor、selected event/command/
  registry/networking callback、version-locked pre-definition transformer、selective routingを実装。
- shadow ABIは906 source classes、763 class files、8,297 declared/audited membersを対象に検査。
- embedded packageには1,457 class filesを含むJVM smoke gateがある。
- historical plan51 fixture corpusは25/25。official Loader/Knot/Mixinのoffline/embedded probeも通過。
- ただしMojang GameProvider/server jarを同梱せず、任意Fabric modをロードするuniversal Loaderではない。
- JNI-capable binaryにはruntime JDKと互換classesが必要。JNIなしbinaryはnative-only。

### 5.5 vanilla RNG

- Java LocalRandomの48-bit LCG、Minecraft Xoroshiro128++、seed expansion、bounded primitive、
  splitters、long/coordinate/string vectorsを実装。
- `test_rng_parity`は25 PASS。
- これはprimitive/splitter parityの証明であり、全worldgen call order、消費順、structure NBT parity、
  vanillaとのL3完全一致を証明しない。

### 5.6 package・実client・実Mod

- Linux CPack ZIPは実行ファイル`cppfm`一つを含む形で作成・抽出・起動を検証。
- 直近のlocal package evidenceはサイズ54,999,329 bytes、SHA-256
  `61b19c83100b755b06431c2568e5277e4251867b4b25df98c27ab44118d84b8b`。
- package evidenceはignored build outputであり、tracked/public release assetではない。
- package JVM smokeではdefault-on JVM startup、1,457 embedded classes、registry assets、clean shutdownを確認。
- mc-pilotでFabric 1.21.4 clientのlogin、world join、chat、command、position、block get/break、
  status、screenshot、1分超のstabilityを確認。
- PrismLauncher 11.1.0では既存authenticated accountを使いFabric 1.21.4 clientを起動してcppfmへjoin。
  初回Microsoft loginや全instance構成を証明したものではない。
- Lithium、FerriteCore、Carpetはserver-side bounded corpusとして個別・combined bootstrap/clean shutdown PASS。
- Modrinth候補12件は8件がtarget-compatible/runtime pass、Create 2件はtarget-incompatible、
  C2MEはJava 22+要求、Debugifyはinvalid metadata。Createを互換性PASSに数えない。

## 6. 最終検証証跡

以下は過去に記録されたcurrent working-tree evidenceであり、ここに列挙した全テストを今回再実行したという意味ではない。
今回の2026-09-22 live fixture結果とActions状態は先頭の「最新セッション追補」を正とする。詳細は`docs/CURRENT_STATE.md`。

| gate | 記録された結果 |
|---|---|
| configure / full Ninja build | `RC=0` |
| non-nightly CTest | `46/46 PASS`、394.82秒 |
| settings matrix | `25 PASS / 0 FAIL` |
| properties | `33 PASS / 0 FAIL` |
| lifecycle matrix | `8/8 PASS` |
| recovery | `55 PASS / 0 FAIL` |
| core safety | `45 PASS / 0 FAIL` |
| Plan43 | `87 PASS / 0 FAIL` |
| Smoke80 | `224 PASS / 0 FAIL` |
| full live matrix | `240 PASS / 0 FAIL / 240 total` |
| test_spec_wire | `417 PASS / 0 FAIL` |
| test_wire_full | `399 PASS / 0 FAIL` |
| test_wire_b6 | `136 PASS / 0 FAIL` |
| test_gameplay_full | `806 PASS / 0 FAIL` |
| test_seed_parity | `201 PASS / 0 FAIL` |
| test_rng_parity | `25 PASS / 0 FAIL` |
| test_mining_full | `59/59` |
| test_block_hardness_full | `16/16`, 1095 mismatch=0 |
| test_mob_stats_full | `131 PASS / 0 FAIL` |
| test_redstone_engine_full | `42 PASS / 0 FAIL` |
| test_fluids | `23 PASS / 0 FAIL` |
| test_menu_logic | `41 PASS / 0 FAIL` |
| test_recipes_mirror | `76 PASS / 0 FAIL` |
| no-JNI build + regression | `42/42 PASS` |
| ASan/UBSan key set | `4/4 PASS`、sanitizer reportなし |
| 120-client stress | `120/120 joined`、68.5秒 |
| 300-second soak | 150 keepalives、0 disconnect、2,899 actions、RSS +0.2% |
| view-distance 32 dry benchmark | 4,225 chunks、p50 0.107ms、p95 2.332ms、OOM/kick 0 |
| multi-client | ALL PASS、17.48秒 |
| bot smoke | ALL PASS、20.82秒 |
| Python harness compile | `RC=0` |
| git diff --check | `RC=0` |
| latest goal-live feature fixture | local `PASS`（suggestions/effect/XPのrow-specific assertionsを含む） |
| latest goal-live remaining fixture | local `PASS`（既存のremaining assertions） |
| PR Actions for current HEAD | 最新runを `gh run list` で特定すること。追補作成時はpending/in progress、最終PASS未確認 |

補足:

- package extracted-directoryのhistorical `test_server_full`は234/234。source-treeの240/240とは別証跡。
- `tools/soak_bot.py --duration 300`は3/3 PASS。各回KeepAlive 30、chunks 182、time updates 300、
  kicks/EOF/server-exit/transport/protocol errors 0。
- 1800秒wide soakはPASS（900 keepalives、0 disconnect、17,493 actions、RSS +12.5%）。
- 7200秒attemptはt=3361秒で中断・不受理。RSS `160388→191612kB`、+19.5%で15% gate超過。
- 2時間/24時間のaccepted artifactは存在しない。

## 7. 敵対的レビューと評価の読み方

`docs/audit/adversarial-review-2026-09-19.md`のcurrent hardening scopeに対する記録:

- feature adversarial review: **10/10**、未解決P0/P1/P2なし。
- code-quality adversarial review: **9.5/10**、未解決P0/P1なし。
- residual advisory: `DOS-CONSOLE-001`。authenticated RCONの`/reload`と`/function`がserialized
  simulation domain内で直列化される。競合・非決定性を避ける意図的trade-offであり、レビュー範囲では
  correctness/security bypassではない。

このスコアは今回レビューしたhardening scopeの評価であり、Minecraft全体の互換性スコアではない。
過去の総合目安は互換性約7/10、wire 8〜8.5/10、JVM boundary 6/10、任意Fabric mod 3〜4/10、
full worldgen RNG/NBT parity 5〜6/10。コード全体の以前の保守的自己評価は約8/10で、9.5/10を
リポジトリ全体の無条件な品質保証として扱わない。

## 8. 明示的な未達・境界

次の項目は、テスト手順が存在しても完了扱いにしてはいけない。

1. **任意Fabric JVM mod / official GameProvider**
   embedded shadow ABI、Loader/Knot probe、3つのlocked server-side modはbounded evidence。
   任意modの全bytecode、Mixin target、registry、client/render/GUI、公式providerを保証しない。
2. **vanilla RNG L3**
   primitiveとsplitterは通るが、全worldgen call graphとstructure NBTのvanilla byte parityは未証明。
3. **長時間運転**
   300秒、600秒wide、1800秒diagnosticはPASS。accepted 2h/24hはない。
4. **real client / official GUI**
   ローカルmc-pilot/PrismLauncherの限定probeはPASS。ログ・スクリーンショットをrelease artifactとして
   保持しておらず、全画面・全操作・初回認証・全instanceを網羅していない。
5. **signed command**
   inbound signed chat verificationは実装済み。argument transcriptを再構築できないため、
   enforced secure chatでは`ChatCommandSigned`をfail closed。
6. **moving piston persistence**
   snapshot barrierでpending source/destination commitを先にmaterializeするが、transient NBTそのものは保存しない。
7. **publication**
   ignored build/package evidence、local account、temporary logs/screenshots、未コミット差分をもって公開releaseとはしない。

## 9. timeoutとプロセス管理の調査結果

以前「timeoutがクラッシュしました」と見えていた問題には、少なくとも次の要因があった。

- 親processだけをkillすると、子cppfmがpipeを保持して外側timeoutが終わらない。
- readiness/output loopが、実際のowned child stateをdeadline内に観測できない場合があった。
- build自体も、時間制限が短いとcompiler/linkerの途中でRC 124になることがあった。これは必ずしもruntime crashではない。

対策済みの内容:

- Python harnessはprocess group、monotonic deadline、実サーバーstatus probe、bounded terminate/wait/reap。
- C++ `ServerProcess`はowned PIDの`kill`、`waitpid`、temporary-world removalを検査し、cleanup失敗を隠さない。
- disconnected sessionは遅いpersistence/hook前にinactive化。
- writer queue、graceful close、overflow closeをbounded化。

許可される手動確認:

~~~bash
pgrep -a -f 'cppfm --por[t]' || true
~~~

対象が実際に表示されたときだけ、自己非マッチ化patternで回収する。曖昧な`pkill -9 c++`、
`pkill cppfm`、広いpatternは絶対に使わない。

## 10. 文書・CI・公開状態

直近のhardening passで更新された主なファイル:

- `docs/CURRENT_STATE.md`
- `docs/MISSING_FEATURES_1_21_4.md`
- `docs/README.md`
- `docs/DEVELOPMENT.md`
- `docs/VERIFICATION.md`
- `docs/audit/README.md`
- `docs/audit/adversarial-review-2026-09-19.md`
- `.github/workflows/ci.yml`
- `tests/test_settings_matrix.cpp`
- `tests/test_goal_live_features.py`
- `docs/audit/goal-feature-ledger.md`
- `docs/audit/goal-followup-evidence-ledger.md`

CIはpush、pull request、manual dispatchでconfigure/build/focused gates/non-nightly CTestをtimeout付きで実行する。
今回のHEADはPR #1へpush済みだが、対応するActions runは追補作成時点でpending/in progressであり、成功結果が出るまで公開済みPASSとは扱わない。

## 11. 次に再開するエージェントへの手順

作業を再開する場合は、まず未確定のActions結果を確認し、次の順序を守る。

1. `AGENTS.md`、このHandoff、`docs/CURRENT_STATE.md`、`docs/VERIFICATION.md`を読む。
2. `git status --short --branch`、`git log --oneline --decorate -5`、`gh pr view 1`を確認する。
3. `gh run list --branch chore/goal-cleanup-next --limit 1 --json databaseId,headSha,status,conclusion` でcurrent HEADのActions runを特定し、`gh run view <databaseId> --json status,conclusion,jobs` で確認する。失敗ならログの原因を分離する。
4. sourceを変更した場合は、focused test → `test_native` → 必要なintegration/CTestの順で確認する。
5. 数値、Status、制限を変えたらCURRENT_STATE、MISSING、README/docs、VERIFICATION、auditを同期する。
6. 長いコマンドは必ず `timeout --foreground --kill-after=...` で包む。Smoke80など親子processを作るものは
   親だけkillしない。
7. すべての変更は作業ブランチでcommitし、PR #1へpushしてActions成功を確認する。`main`へ直接pushしない。

### サブエージェントを使う場合

- 同じworktreeへ重複担当を置かない。並列実装はdisjointな/tmp worktreeを使い、完了後は正確なpathだけ整理する。
- research agentはWeb/公式資料の確認と`plan/planX.md`だけ。研究完了前にimplementationを始めない。
- plan番号は `ls plan/plan*.md | sort -V | tail -1` で確認し、`plan/`を`git add -f`しない。
- ユーザー指定によりSwarmは使わず、サブエージェントは原則2体程度までにする。モデルを明示指定しない。
- UsageLimitでresumeが失敗したら、同じagent IDへ`send_input`を送り、duplicate agentを起動しない。
- 明示的にclosed/interruptedならresumeを先に試し、それ以外は既存agentへ直接`send_input`する。

## 12. 重要なwire / logic不変条件

- ChunkCodecのsingle-valued paletteは`longCount=0`。
- WorldBorder diameterは`59999968`。lerpは`50ms` tick補間。
- SimulationDistanceはEuclideanではなくChebyshev `max(abs(dx), abs(dz))`。
- Play S→C: OpenScreen `0x35`、ContainerSetContent `0x13`、TradeList `0x2E`、KeepAlive `0x27`。
- Play C→S KeepAliveは`0x1A`。
- Bundle axisは`lx<<8 | lz<<4 | ly`。vanilla state indexは`state<<12 | x<<8 | z<<4 | y`。
- SlotComponent IDsはdamage `3`、repair_cost `17`、trim `45`。
- Armor damage formulaは`f=2+t/4`、`g=clamp(a-dmg/f, a*0.2, 20)`、caps `30/20`。
- AES-CFB8 encrypted frameはconnection state依存のため、priority queueで順序を変えない。
- mutation callback後は対象stateを再取得してからcommitする。

## 13. Codex固有の注記

- 以前のCodex Goalは完了扱いになった。最終使用量は1,269,742 tokens、経過時間13,572秒。
  これはGit status、release status、互換性証明ではない。
- ユーザーは過去に「2本の敵対的レビューを並列で走らせ、4〜5ループする」方針を指定した。
  以前の一部agent起動は報告なし・停止・判定不能で、独立した合格証跡として扱えない。
  現在の根拠は、実ファイル差分、記録されたtest output、`docs/audit/adversarial-review-2026-09-19.md`。
- 直近セッションでは、owned live fixtureのfocused実行、テスト/台帳のcommit `d875bd65`、PRブランチへのpushを実施した。
  Actionsの最終結果はこのHandoff追補時点で未確定なので、成功と記録しない。
- account名、token、Microsoft認証情報、temporary local screenshot/logはHandoffに記録しない。

## 14. 最後に

現在の正しい要約は次の一文である。

> 設定・認証・権限・イベント・tick・永続化・ネットワーク・JVM境界を含む広い範囲で実装と回帰検証を完了し、直近hardening scopeの敵対的レビューも通過したが、20%/1万行削減、任意Fabric mod、vanilla RNG L3、accepted 2h/24h、全real-client/GUI、公開releaseの完全証明は未達である。

この境界を保ったまま、次の依頼に応じて作業すること。
