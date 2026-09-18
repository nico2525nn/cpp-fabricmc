# CppFabricMC Handoff

> 次のセッションでこのプロジェクトを安全に引き継ぐためのスナップショット。
> 動的なテスト実績・HEAD・Next Stepsの正規参照は
> docs/CURRENT_STATE.md と docs/VERIFICATION.md。このファイルは、それらを
> 含む会話・判断・運用上の背景をまとめたもの。状態が変わったらこのファイルと
> CURRENT_STATE.mdの両方を更新すること。

## 0. 最初に読むもの

1. AGENTS.md — 正式な改善ループ、timeout、worktree、サブエージェント、Git、安全規則。
2. docs/CURRENT_STATE.md — 最新の測定値、publication status、残課題、Next Steps。
3. docs/VERIFICATION.md — どのPASSが何を証明し、何を証明しないか。
4. docs/DEVELOPMENT.md — 研究手順とcanonical module map。
5. README.md — 公開利用者向け説明。内部PlanやCodex運用はここへ追加しない。

## 1. プロジェクト識別情報

| 項目 | 現在値 |
|---|---|
| 作業ディレクトリ | /run/media/nico/d/学校/app/cpp-fabricmc |
| 対象 | Minecraft Java Edition 1.21.4 / Fabric-compatible server |
| protocol | 769 |
| DataVersion | 4189 |
| branch | main |
| HEAD | 9859e1d (final plan53/plan54 evidence documentation) |
| origin/main | local `main` is ahead by the integrated local commits; no push requested. Recheck before publishing |
| HEADのコミット | docs: pin final evidence snapshot |
| スナップショット | 2026-09-19 JST |
| JDK | OpenJDK 21をインストール済み。JNI/JVMビルド検出済み |
| publication status | BLOCKED — 下記の明示された境界が残る |

### 重要なGit状態

このスナップショットは統合済みmainと最終文書同期を基準にしている。既存の
ユーザー変更を捨てるためにreset --hard、checkout --、広範囲の削除を行ってはいけない。

余計なjar、zip、class、logはGit作業ツリーに残していない。build/はignoreされた
ローカル生成物であり、公開成果物ではない。公開前にstatus、diff-check、originとの差分を
再確認する。

## 2. ユーザーが決めた方針・前提

- 目標は、Minecraft Fabric 1.21.4 serverをC++で非公式再実装し、可能な限りvanilla/Fabricと
  互換にすること。完全互換を目指すが、証明できないものをPASSとは書かない。
- 対象はprotocol 769。以前誰かが追加したprotocol 776は対象外であり、1.21.4の話に混入した
  だけなので、今後の設計・評価・READMEで追わない。不要なら削除してよい。
- JVM/Fabric互換は、JDK/JNIがconfigure/build時に見つかるバイナリではデフォルトON。JNIなしで
  ビルドした既存バイナリに、後からJDKを置いてJVM機能が生えるわけではない。
- one-file配布を目標にする。LinuxではCPack ZIP内に実行ファイル1個を入れ、初回起動時に
  world/、mods/、config/、libraries/、logging、resourcepacks/、runtime cache、
  server.propertiesなどを作る。Windows/macOSは各ホストでnative buildが必要。
- README.mdは利用者向け公開文書であり、Plan番号、サブエージェント、内部会話などは書かない。
  それらはこのHandoff、AGENTS.md、docs/CURRENT_STATE.mdなど内部技術文書に分離する。
- 実際のクライアント・PrismLauncher・mc-pilot・Modrinth Modの動作を確認する。ただし、
  対応artifactがないものを無理にPASSに数えない。Createは今回のFabric 1.21.4 server-side
  対象artifactがロックできなかったため、runtime PASSに数えていない。
- 追加を重ねてコードを汚さない。実装はまとまり単位で行い、レビュー、テスト、ドキュメントを
  一緒に更新する。将来的に1万行削減を目指すが、featureや証跡を雑に削除して達成してはいけない。
- /tmp worktreeは必須ではない。並列作業で本当に隔離が必要な場合だけ、作成した正確なパスを
  後で削除する。/tmp全体、workspace root、$HOME相当を広く削除しない。

## 3. 目標と最終判断

直前のCodex Goalは、次を目的に設定された。

> 2系統の敵対的レビューを起点に、4〜5回の監査・修正・検証ループで、Fabric 1.21.4 / protocol 769
> の互換性とコード品質を改善する。通常/厳格コンパイル、テスト、サニタイザ、並行負荷、実クライアントを
> 実施し、JVM/Fabric境界、vanilla RNG、GUI、実Modなどの残る限界を公式資料と実験で調べ、解消可能な
> ものを実装し、解消不能または外部条件依存のものを根拠付きで明示する。

このGoalは、実装可能な範囲と検証可能な範囲を完了し、universal compatibilityを主張できない残課題を
ドキュメントへ明記した状態で完了扱いにした。Goal完了はGit commitやPushを意味しない。

厳しめの目安は次のとおり。

- 総合互換性: 7/10前後。protocol/login/basic gameplayの実証範囲は高いが、未証明の面積が大きい。
- protocol/wire: 8〜8.5/10。
- JVM境界: 6/10。tested bounded surfaceとしては動くが、任意Mod Loaderではない。
- 任意Fabric Mod: 3〜4/10。E-14を残す。
- full worldgen RNG/NBT parity: 5〜6/10。primitive/splitterは通るが、全call graphは未証明。
- コード品質: 8/10近辺。strict build、sanitizer、所有権、ドキュメント整合は改善したが、
  大きなdirty diffとheader-heavyなRNG実装、残る境界があるため9〜10とはしない。

## 4. 現在の実装・修正内容

### 4.1 コード品質・安全性

多数の既存変更を含むため、全変更ファイルの一覧はgit status --shortで確認すること。今回の
レビューで特に行ったことは次のとおり。

- C++の警告、型変換、const、符号付き整数、shadow、non-virtual destructorなどを整理。
- strict quality buildで次の警告群を有効化して全ターゲットをビルドした。
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor
- C++/Pythonのテストハーネスで、所有している子プロセスをterminate/wait/bounded escalation
  するように整理。親だけkillして子を孤児化する実装を避ける。
- timeout、readiness、stdout/stderr pipe、サーバー終了、disconnect後のinactive化を見直した。
- NBT、JSON、Region/Anvil、Persistence、World、WorldGen、AI、Redstone、Fluids、Commands、
  RCON、JVM runtime、Menu、Entity data等の警告・品質問題を横断的に整理。
- 変更後のgit diff --checkは通過済み。

### 4.2 実クライアントで発見・修正したもの

docs/MC_PILOT_REAL_TEST.mdに記録されている修正。

- Advancement update flagsをVarIntではなくfixed big-endian i32に修正。
- Declare Commandsのinteger min/maxとtime minimumをfixed i32に修正。
- score_holderのallow-multiple byteを追加。
- Heightmapの1.21.4 non-straddling packed layoutを修正（256個の9-bit valueを37 longs）。
- Entity spawn UUIDを有効・nonzero・entityごとにdistinctに修正。
- zero-weightのdecoration/display/projectile/vehicleをnatural mob selectionから除外。
- EntityTeleport producerを1.21.4の統一encoderへ集約。entity id、2つのVec3d、yaw/pitch f32、
  relative flags fixed i32、onGround booleanの形に修正。
- generated/default propertiesにlevel-type=normalを明示。flatは明示設定時だけ有効。
- --world-dirをworld save pathとして扱い、server propertiesをserver rootから読むように修正。

### 4.3 JVM/Fabric境界

- HotSpot/JNIを組み込むbounded server-side shadow ABI。
- selected callbacks、tick-thread executor、opaque native handles、event/command/registry/networking
  surface、version-locked transformerを実装・検証。
- official Loader/Knot/Mixinのoffline/embedded probeを持つが、Mojang GameProvider/server jarを同梱しない。
- 25/25のhistorical JVM fixture corpus、Shadow ABI、transformer、handles、native bridge、runtimeを検証済み。
- arbitrary Fabric JVM Modとuniversal bytecode compatibilityはE-14として明示的に未達。
- jvm_runtime timeoutは30秒固定から180秒に変更。環境によるJVM startup遅延を誤FAILにしないため。

#### Sidecarとプロセス内JVMの設計判断

過去の議論で「五感を持つ」ように、Modやゲーム状態へできるだけ深くアクセスしたいという目標が確認された。

- JVM Sidecarは、C++サーバーとJavaプロセスを別プロセスにする方式。クラッシュ隔離・再起動・依存分離には
  有利だが、ゲーム状態やポインタを直接共有できず、IPC/RPC、serialization、event ordering、latencyの
  境界が新たに生じる。公式Fabric LoaderやGameProviderをそのまま同一プロセスへ持ち込むものでもない。
- プロセス内JVM埋め込みは、C++プロセス内でJNI Invocation API/HotSpotを起動し、native handleとcallbackを
  直接接続する方式。現在のPlan51系の実装はこちらであり、Sidecarではない。状態アクセス、tick-thread境界、
  callback順序、opaque handleを同一プロセス内で扱える反面、JVMクラッシュやABI不整合がサーバー全体へ影響する。
- 現在の実装は、プロセス内JVMを選んだが、公式Java serverを丸ごと再現したわけではない。shadow API、selected
  events、bounded transformer、tested fixtureを提供する限定的なFabric-compatible extension surfaceである。
- この選択によってGUI、公式client、任意Mod、Mojang GameProvider、全Fabric Loader behaviorまで自動的に解決される
  わけではない。これらは引き続きE-14/declared limitationとして扱う。

### 4.4 配布・properties

- JDK/JNI/Pythonが揃えばJava shadow classesとrepository-owned resourcesを実行ファイルへ埋め込む。
- CPack one-file ZIPはLinux x86-64で確認済み。ZIPにはcppfm executable 1個だけ。
- clean extraction、sentinel user file preservation、embedded classes/assets、strict default-on JVM startup、
  test_server_fullを検証済み。
- 通常のproduction defaultはnormal terrain。superflat/flatはserver.propertiesでlevel-type=flatを明示。

## 5. Vanilla RNG実装の引き継ぎ事項

実装場所はsrc/core/Random.hpp、独立テストはtests/test_rng_parity.cpp。CTest登録は
CMakeLists.txtの707行付近。

### 5.1 実装済み

- VanillaLocalRandom
  - Java 48-bit LCG
  - multiplier 25214903917
  - increment 11
  - nextBits、unbounded/bounded nextInt、nextLong、boolean、float、double、Gaussian
  - split、nextSplitter
- VanillaXoroshiro128PlusPlus
  - xoroshiro128++ transition
  - GOLDEN_RATIO_64 = 0x9E3779B97F4A7C15
  - SILVER_RATIO_64 = 0x6A09E667F3BCC909
  - Stafford-13 seed mixing
  - upper-bit float/double、low-bit boolean、unsigned multiply/rejection bounded int
  - Gaussian cache、skip、split、nextSplitter
- VanillaXoroshiroSplitter
  - long、coordinate、String/MD5 split
- rng_detail
  - dependency-free MD5
  - big-endian 64-bit word extraction
  - Java UTF-16 semanticsを考慮したString hash
  - 1.21.4 MathHelper.getSeed(x,y,z)相当のcoordinate hash
- process-wide nextRandom/seedRandom
  - atomic stateのJava LCG化、nonnegative 31-bit stream。
  - ただし、これだけで全worldgen call siteがvanillaと同一になるわけではない。

### 5.2 実装上の注意

- cppfm::detailというnamespace名は既存のcppfm::worldgen::detailと衝突したため、helperは
  cppfm::rng_detailである。戻さないこと。
- XoroshiroのsplitとnextSplitterでは、C++関数引数の評価順序が未規定なので、seedLoを先に引き、
  次にseedHiをローカルへ保存してからchildを構築する。これを一つのconstructor callの引数に
  戻すとJavaのdraw orderを壊す。
- Java signed bit patternはstd::bit_cast helperで復元している。通常のsigned conversionへ戻さない。
- coordinate hashでは最初のx * 3129871がJava int overflowしてからlongへ拡張される点に注意。
- full worldgenのrandom call order、structure placement、structure NBT parityはまだ証明していない。

### 5.3 RNGの証跡

test_rng_parityは25 PASS / 0 FAIL。

- LocalRandom seed 123456789/1234のnextInt、bounded int、nextLong、float、double、next(31)
- Xoroshiro unmixed/mixed seed vectors
- nextLong sequence
- float/double/bounded int
- long/coordinate/MD5 string splitter

公式参照:

- Yarn LocalRandom:
  https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.1/net/minecraft/util/math/random/LocalRandom.html
- Yarn Xoroshiro128PlusPlusRandom:
  https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.1/net/minecraft/util/math/random/Xoroshiro128PlusPlusRandom.html
- Yarn RandomSeed:
  https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.1/net/minecraft/util/math/random/RandomSeed.html
- Yarn RandomSplitter:
  https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.4/net/minecraft/util/math/random/RandomSplitter.html
- xoroshiro128++ public-domain reference:
  https://prng.di.unimi.it/xoroshiro128plusplus.c

## 6. 最終検証証跡

### 6.1 Build / CTest / sanitizer

| 検証 | 結果 |
|---|---:|
| 通常build | 成功。途中で600秒枠が100/111付近で期限切れになったが、900秒枠で残り11タスクを再開して成功 |
| strict quality build | 266/266 tasks成功、上記warning set、Werror構成 |
| 全non-nightly CTest | 45/45 PASS、395.76秒 |
| smoke80 | PASS、175.02秒。全体の一テストであり特別扱いしない |
| test_rng_parity | 25 PASS / 0 FAIL |
| quality/tautology/mcproto | 4/4 PASS |
| strict selected CTest | native/spec_wire/fuzz/core_safety/rng_parity 5/5 PASS |
| ASan/UBSan key regression set | core_safety/spec_wire/fuzz/gameplay_full 4/4 PASS、reportなし |
| test_spec_wire | 417 PASS / 0 FAIL |
| test_wire_full | 399 PASS / 0 FAIL |
| test_gameplay_full | 806 PASS / 0 FAIL |
| properties / lifecycle_matrix | 33 PASS / 0 FAIL; 8/8 PASS |
| test_seed_parity | 201 PASS |
| test_mining_full | 59/59 PASS |
| block hardness | 1095 mismatch=0 |
| mob stats | 131 PASS / 0 FAIL |
| redstone | 42 PASS / 0 FAIL |
| fluids | 23 PASS / 0 FAIL |
| menu logic | 41 PASS / 0 FAIL |
| recipes mirror | 76 PASS / 0 FAIL |
| recovery | 54 PASS / 0 FAIL |

### 6.2 JVM / Mod / package

- jvm_handles、jvm_native_bridge、jvm_runtime、jvm_transformer、jvm_access_widener、jvm_api PASS。
- jvm_compatibility: 25/25 fixture、functional API fixture、three consecutive direct reruns PASS。
- Shadow ABI: 906 source classes、763 class files、8,297 audited members。
- official Loader/Knot probe: expected markers PASS。ただしofficial Mojang GameProviderは未同梱。
- Lithium、FerriteCore、CarpetをJava 21で個別・combined bootstrap/clean shutdown PASS。
- 12-entry Modrinth candidate screenは8 target-compatible runtime pass、4 explicit non-target/invalid。
- Createは対応Fabric 1.21.4 server artifactが確認できずruntime PASSに数えていない。
- Linux CPack ZIP（ignored local output）の記録:
  - archive: build/packages/cppfabricmc-1.21.4-Linux-x86_64.zip
  - size: 54377042 bytes
  - SHA-256: 07cbccb4552b50003eec71ef827c22435a6b6442d1039458df598e1de0a0d588
  - contents: cppfm executable 1個
  - clean extractionのtest_server_full: 234 PASS / 0 FAIL
  - package_jvm_smoke: strict default-on JVM startup、1,457 embedded class files、registry assets、owned shutdown PASS
- explicit no-JNI configure/build: `CPPFM_ENABLE_JNI_FALLBACK=OFF` and
  Java/JNI package discovery disabled; native-only `cppfm` build PASS.

### 6.3 Client / load / soak

- mc-pilot managed Fabric 1.21.4 client: offline login、configuration、world join、chat、say、
  position、block get/break/get、status、screenshot PASS。1分以上接続維持。
- PrismLauncher 11.1.0: existing authenticated accountを使ったFabric 1.21.4 CLI launch/join PASS。
- fresh no-account profileで--offlineがアカウントを生成しないことも確認。normal playにはaccountが必要。
- 120 synthetic clients: 120/120 join、最新手動rerun 68.0秒、終了後online 0、孤児cppfm 0。
- multi-client: 17.60秒 PASS。
- bot smoke: 21.09秒 PASS。
- soak 60秒: 0 disconnect、30 keepalives、590 actions、RSS +1.0%。
- soak 300秒: 0 disconnect、150 keepalives、2932 actions、RSS +7.6%。
- soak 600秒 wide movement: 0 disconnect、300 keepalives、5707 actions、RSS +6.6%。
- soak 1800秒: 900 keepalives、0 disconnect、17493 actions、RSS +12.5%。
- 7200秒試行: t=3361sで中断、RSS 160388→191612kB、+19.5%、15% gate超過。正式PASSではない。
- 2時間/24時間のaccepted artifactは存在しない。

### 6.4 Plan54 adversarial cleanup ledger

- Baseline: clean checkpoint `65a7c69`; primary scope is `src/`, `tests/`, and
  `tools/` with C++/header/Python/Java suffixes.
- `293 files / 96,654 lines` → `298 files / 98,587 lines`.
- Protected manifest: `80 files / 8,939 lines`; all protected hashes are unchanged.
- Mutable scope: `87,715` → `89,648`, net **+1,933 (+2.20%)**. The strict
  `18,341` reduction gate is `PARTIAL`, not a pass.
- Accepted net reductions: items `-5`, native process harness `-81`, session
  login paths `-13`, commands `-3`; JVM bridge `+12`. Configuration/properties
  evidence added `+671`, lifecycle evidence added `+1,352`.
- No fixture, generated input, expected byte, assertion, or negative case was
  removed or weakened. Python consolidation was rejected because its helper made
  the net scope larger.

## 7. 残課題・互換性の境界

これらを「未修正なのに隠している」と扱わないこと。現在の実装または証跡で、完全達成を主張できない
ため、DECLARED-LIMITATION/BLOCKEDとして明示している。

1. E-14 arbitrary Fabric JVM mods / GameProvider
   - bounded shadow ABIとtested callbacksのみ。
   - arbitrary Mod、任意bytecode、公式Mojang server runtime、client-side Mod、GUIは未達。
2. Full worldgen RNG L3
   - primitive、seed expansion、splitterは25/25。
   - 全random call order、structure NBT、全worldgen pathのbyte parityは未証明。
3. GUI/全client behavior
   - 実クライアントの限定probeはPASS。
   - 全menu、全entity、全dimension、全launcher configuration、全GUIは未証明。
4. Microsoft初回ログイン
   - existing authenticated accountのrefresh/joinはPASS。
   - first-time interactive loginは未検証。
5. Long-run evidence
   - accepted 2h/24h artifactなし。
   - 7200秒試行はRSS gate超過のためPASSに昇格しない。
6. Cross-platform release
   - Linux x86-64 packageのみ実証。Windows/macOSはnative host build/testが必要。
7. Modrinth coverage
   - bootstrap/clean shutdown中心。gameplay、registry、rendering、client、arbitrary-mod parityを意味しない。

## 8. timeout / process ownershipの引き継ぎ

「timeoutがクラッシュしました」と見える現象について、直前の調査で次を確認した。

- 通常buildは600秒枠で100/111付近まで進んだあと124になった。コンパイル失敗ではなく時間切れ。
- ASan buildも600秒枠で39/65で時間切れ。900秒で残り26/26を再開して完了。
- smoke80などは子のcppfmをforkするため、親プロセスだけをkillするとpipeを子が保持し、
  失敗やハングに見える。
- readiness/output loopが子の実状態を見ずにtimeoutする経路もあった。
- 現在のharnessはprocess group、monotonic deadline、owned childのterminate/reap、bounded escalation、
  cleanup failureの明示を行う。
- disconnect pathは遅いpersistence/hookの前にplayerをinactive化する。

安全な確認方法:

~~~bash
pgrep -a -f 'cppfm --por[t]' || true
~~~

対象が実際に表示された場合だけ、自己非マッチ化された明確なpatternで回収する。

~~~bash
timeout --foreground --kill-after=5 10 pkill -9 -f 'cppfm --por[t]' 2>/dev/null || true
~~~

pkill -9 c++、pkill -9 cpp、compiler名を含む曖昧なpatternは禁止。テストハーネス内ではPopenの
所有PIDを直接terminate/waitし、他のテストを巻き込むpkillを使わない。

## 9. 次セッションの再開手順

### 9.1 まず状態を確認

~~~bash
cd /run/media/nico/d/学校/app/cpp-fabricmc
git status --short
git log -1 --format='%H%n%ad%n%s' --date=iso
git diff --check
sed -n '1,280p' docs/CURRENT_STATE.md
sed -n '1,220p' Handoff.md
~~~

既存の変更を勝手に捨てない。特にsrc/core/Random.hpp、docs、JVM、test harnessの変更を、自分の変更ではない
という理由だけで戻さない。

### 9.2 通常ビルドと基本回帰

すべてのコマンドにtimeoutを付ける。

~~~bash
timeout --foreground --kill-after=10 180 \
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
timeout --foreground --kill-after=10 900 \
  cmake --build build -j2
timeout --foreground --kill-after=10 1200 \
  ctest --test-dir build -LE 'nightly|package' --output-on-failure --timeout 600
timeout --foreground --kill-after=10 30 \
  ./build/test_rng_parity
~~~

smoke80単独を実行する場合:

~~~bash
timeout --foreground --kill-after=10 600 \
  ctest --test-dir build -R smoke80 --output-on-failure --timeout 600
pgrep -a -f 'cppfm --por[t]' || true
~~~

### 9.3 strict quality build

新しい一時build directoryを作る場合は、workspaceや$HOMEを消さず、明示的なunique pathを使う。
既存の最終strict buildは/tmp/cppfm-quality-build-gZ0ivD、CMake generatorはNinja、
RelWithDebInfo、compilerは/usr/bin/c++、flagsは次のとおり。

~~~text
-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor
~~~

同じbuildが残っていれば:

~~~bash
timeout --foreground --kill-after=10 1200 \
  cmake --build /tmp/cppfm-quality-build-gZ0ivD -j2
timeout --foreground --kill-after=10 240 \
  ctest --test-dir /tmp/cppfm-quality-build-gZ0ivD \
  -R '^(native|spec_wire|fuzz|core_safety|rng_parity)$' \
  --output-on-failure --timeout 60
~~~

### 9.4 ASan/UBSan

最終選抜buildは/tmp/cppfm-asan-build、RelWithDebInfo、Ninja、flags:
-fsanitize=address,undefined -fno-omit-frame-pointer。

~~~bash
timeout --foreground --kill-after=10 900 \
  cmake --build /tmp/cppfm-asan-build \
  --target cppfm test_native test_core_safety test_fuzz test_rng_parity \
  test_jvm_handles test_jvm_native_bridge -j2
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
timeout --foreground --kill-after=10 300 \
  ctest --test-dir /tmp/cppfm-asan-build \
  -R '^(native|jvm_handles|jvm_native_bridge|fuzz|core_safety|rng_parity)$' \
  --output-on-failure --timeout 120
~~~

### 9.5 負荷・実クライアント

~~~bash
timeout --foreground --kill-after=10 700 \
  python3 tests/stress_test.py --clients 120 --binary ./build/cppfm
pgrep -a -f 'cppfm --por[t]' || true
timeout --foreground --kill-after=10 700 \
  python3 tests/soak_test.py --duration 300 --binary ./build/cppfm
timeout --foreground --kill-after=10 60 \
  python3 tools/bench_chunk_gen.py --view-distance 32 --chunks 4225 --dry --strict
~~~

mc-pilot/PrismLauncher手順と、アカウント名・tokenを記録しない方針は
docs/MC_PILOT_REAL_TEST.mdを参照。既存authenticated accountの情報をHandoffへ書かない。
初回Microsoft loginを自動化する場合は、ユーザーの明示的な認証方針なしに進めない。

## 10. 設計・実装のクイックマップ

- src/core/ — ByteBuffer、NBT、JSON、Random、RuntimeLayout、ThreadPool
- src/proto/Ids.hpp — protocol 769 packet IDs
- src/net/ — Connection、compression、AES-CFB8、PacketBatcher、Bundle、MultiBlockChange、Crypto、RCON
- src/game/ — World、WorldGen、ChunkCodec、Anvil/Persistence、BlockTickScheduler、Fluids、Redstone、
  LightEngine、Entities、AI、Attributes、Items、Containers、Recipes、GameServer、Session、Menus
- src/worldgen/ — DensityFunction、MultiNoise、Structures、StructureManager、StructurePlacer、PortalHandler
- src/brigadier/ — command tree and argument parser
- src/jvm/ — JvmRuntime、handles、routing、embedded HotSpot bridge
- src/generated/ — kBlocks 1095、kItems 1385、kEntities 149
- jvm/java/ — embedded shadow API/classes
- tests/ — native、wire、gameplay、JVM、integration、stress/soak harness
- tools/ — replay、package、Modrinth/linkage、benchmark、process cleanup helpers

既知の実装上の落とし穴:

- ChunkCodecのsingle-valued paletteはlongCount 0が必要。
- WorldBorder diameterは59999968、tickWorldBorder()のlerpは50ms補間。
- SimulationDistanceはEuclideanではなくChebyshev max(|dx|, |dz|)。
- Play S→Cの重要ID: OpenScreen 0x35、ContainerSetContent 0x13、TradeList 0x2E、
  KeepAlive 0x27。Play C→S KeepAliveは0x1A。
- Bundle axisはlx<<8|lz<<4|ly。ly<<8|lz<<4|lxへ戻さない。
- SlotComponent IDsはdamage 3、repair_cost 17、trim 45。
- DamageCalculator armorはf=2+t/4; g=clamp(a-dmg/f, a*0.2, 20)で、capsは30/20。

## 11. ドキュメントの役割

| ファイル | 役割 |
|---|---|
| README.md | 公開利用者向け概要、公開できる機能・制限、基本テスト |
| docs/README.md | 公開ドキュメントindexとsupported target |
| docs/CURRENT_STATE.md | 最新の動的状態、evidence、publication status、Next Steps |
| docs/VERIFICATION.md | 検証契約、証跡、PASSの意味、境界 |
| docs/SPEC_WIRE.md | packet/wire canonical contract |
| docs/SPEC_GAMEPLAY.md | gameplay/world/behavior canonical contract |
| docs/SPEC_OPS.md | operations、resource、recovery、load、RCON |
| docs/DEVELOPMENT.md | module map、research workflow、contribution rules |
| docs/MISSING_FEATURES_1_21_4.md | numbered matrix #1–#90とdeclared residuals |
| docs/PLAN51_JVM.md | JVM boundaryの履歴・制限。READMEには転載しない |
| docs/MC_PILOT_REAL_TEST.md | real client / mc-pilot / PrismLauncherのlocal evidence |
| AGENTS.md | エージェント運用・worktree・timeout・安全ルール |
| Handoff.md | 会話背景、判断、Codex固有メモ、次セッションの入口 |

Numbered matrixのDONE=90と、歴史的なstrict wire audit 78 gapsは異なる指標。どちらもuniversal
compatibilityの証明ではない。

## 12. Codex固有の注記

この章はプロジェクト仕様ではなく、Codex/サブエージェントを使って引き継ぐ場合の運用メモ。

### 12.1 Goal状態

- 直前のCodex Goalは完了扱いになった。
- Codex側の最終使用量は1,269,742 tokens、経過時間は13,572秒（約3時間46分）。
- このメタデータはGitの状態やプロジェクトのrelease statusではない。次のセッションで再度Goalを作る場合は、
  現在のGit状態を確認してから設定する。

### 12.2 サブエージェント

- ユーザーは、互換性と品質の敵対的レビューを2並列で行い、4〜5回程度ループする方針を指定した。
- 過去の実装フェーズで複数のgeneral agent起動を試したが、報告なしで停止したものがある。共有workspaceに
  変更が残っている可能性を前提に、Git diffとbuildを必ず確認する。
- 直前の最終レビューでも2本を並列起動したが、内容のline-level監査前に停止指示へ反応し、どちらも
  判定不能と返した。したがって、サブエージェント報告は合格証跡として扱わず、実ファイル差分と
  実行結果を根拠にする。
- 新しいagentを起動するなら、同一worktreeで重複させない。読み取り専用レビューなら変更範囲を明記し、
  コーディングならdisjointなworktree/write scopeを与える。
- AGENTS.mdにあるとおり、UsageLimitで既存agentのresumeが失敗した場合は、同じagent IDへsend_inputを
  送り、duplicate agentを作らない。agentが明示的にclosed/interruptedならresumeを先に行い、それ以外は
  既存agentへ直接send_inputする。
- send_inputの正式な名前はsend_input。この環境ではCodexのagent管理ツールとして提供される。
- research agentはweb search/fetch後、原則plan/planX.mdだけを書き、implementation agentはresearch完了後に
  src/testを変更する。plan/はignore対象なのでgit add -fしない。

### 12.3 Codexツール・web調査

- 直前の調査では公式Yarn docs、公式/公開xoroshiro reference、Fabric docs、公式1.21.4 server bytecodeを照合した。
- web検索を再度行う場合、変化し得る仕様、公式URL、Modrinth artifact、Fabric docsは必ず最新確認する。
- webを使った回答では、最終回答に直接URLのMarkdown citationを付ける。内部検索ref IDはユーザーへ出さない。
- CodexのSkillはこの作業では使用していない。Skillを追加する必要はない。
- /tmp/cppfm-quality-build-gZ0ivDと/tmp/cppfm-asan-buildは一時build。次の環境に存在する保証はなく、
  削除する場合はその正確なパスだけを対象にする。
- 生成物・ダウンロード物を消す前に、対象をgit status/find等で特定する。workspace rootや/tmpを再帰削除しない。

### 12.4 最終的な引き継ぎ判断

次に行うべきことは、まずこのHandoffとCURRENT_STATE.mdの整合を確認し、必要なら実測値だけを更新すること。
直ちに新機能追加を重ねない。新たな実装を始める場合は、
docs/research-prompt.md、最新plan番号、MISSING matrix、strict assessment、既存dirty diffを確認し、
研究→分離実装→review→build→CTest→docsの順序を守る。

universal compatibility、protocol 776、任意Fabric JVM Mod、未実施の2h/24h PASSを、このファイルやREADMEへ
誤って「完了」と追記しないこと。
