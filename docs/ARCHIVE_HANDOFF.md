# CppFabricMC — アーカイブ引き継ぎと開発上の知見

- **記録日:** 2026-10-07
- **記録対象のGit基点:** <code>4756a5b0cc660de714f1971e8a6c080d1ea02640</code>
- **目的:** アーキテクチャの変遷と、後継プロジェクトに役立つ開発管理・運用上の知見を残す。

この文書は、バージョン管理された仕様書、ソースコード、監査記録、テストログの代わりではなく、それらへの案内と経験の要約である。リポジトリの状態記録は2026-10-04時点、本書は2026-10-07時点の未整理な作業ツリーをもとにしている。ブランチ、ファイル、検証結果を利用する前に、実際のチェックアウトを確認すること。独立した読み取り専用レビューは、全差分について結論を出す前に別の目的へ切り替わったため、最終レビュー判定には至っていない。本書作成のためのビルドやテストは実行していない。

## このプロジェクトについて

CppFabricMCは、Minecraft Java Edition 1.21.4専用サーバーのプロトコル（protocol version 769、world DataVersion 4189）をC++20で独立実装するプロジェクトである。Mojangサーバーのソースを移植したものではなく、Fabric LoaderやGameProviderの代替でもない。プロトコル処理とプレイ可能なサーバーから始まり、ワールドシミュレーション、永続化、コマンド、インベントリ、エンティティ、限定的なJNIランタイム、運用機能、広範なテストハーネスへと対象が拡大した。

過去の機能分類を完全互換の証明と混同しないこと。MISSING matrixの90項目における<code>DONE=90</code>と、過去の<code>assessment-1</code>にある「78 gaps」は、異なる時点・基準での数値である。現在の品質評価、リリース承認、バニラ互換率、任意のFabric Mod互換性を示すものではない。実際に確認された範囲は、更新される証拠台帳と<code>CURRENT_STATE.md</code>、<code>VERIFICATION.md</code>の制限事項で判断する。

## 継続的に参照する文書

それぞれの情報の正本は次のとおり。

| 確認したいこと | 参照先 |
|---|---|
| 現在のブランチ状態、日付つきのテスト証拠、未解決ゲート | [CURRENT_STATE.md](CURRENT_STATE.md) |
| ソース所有権、開発・調査手順、ロックと拡張時の注意 | [DEVELOPMENT.md](DEVELOPMENT.md) |
| パケットID、レイアウト、順序、wireのgolden contract | [SPEC_WIRE.md](SPEC_WIRE.md) |
| ゲームプレイの挙動と既知の近似 | [SPEC_GAMEPLAY.md](SPEC_GAMEPLAY.md) |
| ファイルシステム、上限、復旧、設定、運用のcontract | [SPEC_OPS.md](SPEC_OPS.md) |
| 再現手順と、証拠から言えること・言えないこと | [VERIFICATION.md](VERIFICATION.md) |
| 過去の90項目機能分類 | [MISSING_FEATURES_1_21_4.md](MISSING_FEATURES_1_21_4.md) |
| 厳密互換評価と個別監査 | [assessment-1.md](assessment-1.md)、[assessment-2.md](assessment-2.md)、[audit/](audit/) |
| 制約つきJVM/Mod境界 | [PLAN51_JVM.md](PLAN51_JVM.md) |

作業手順の入口は<code>AGENTS.md</code>である。<code>docs/research-prompt.md</code>は<code>DEVELOPMENT.md</code>内の正式な調査手順へ案内するためのもので、別個の正本ではない。機能表にある古い<code>GameServer.cpp:&lt;line&gt;</code>参照は、モジュール分割前の可能性がある。現在の配置は<code>DEVELOPMENT.md</code>のmodule mapから追うこと。

## 開発をどう管理したか

基本の流れは、調査 → 計画 → 実装 → 証拠取得 → レビュー → 統合だった。番号つき機能表で対象を整理し、調査計画では対応する未実装項目、対象バージョン、公開資料、ソース所有者、wireまたは観測可能な挙動、状態遷移、端境条件、スレッド安全性、性能、テスト、優先度を記録する運用を目指した。現行の計画書テンプレートは16観点で、13観点の旧計画書は過去の不完全な形式である。<code>plan/</code>以下の計画は意図的にGitの追跡対象外としたため、正本の文書や追跡対象のコード・テストへ結論を移す必要があった。結論を移さずに計画だけ消すと、調査成果も失われる。アーカイブ時には仕様やテストに残っていない独自の調査を明示的に保存すること。

独立した領域を分けるため、通常はworld、block、entity、inventory、network、combatなどのworktreeを作り、各worktreeに作業者を一人だけ割り当てた。ソースを読むだけで、別々の計画書へ書く調査なら並行できるが、対象の調査が済むまでは実装を始めない方針だった。統合時はブランチを確認し、正本のプロトコルcontractとテストに基づいて差分を解決した。編集衝突は減ったものの、共有ヘッダー、<code>GameServer</code>の構成、CMakeのソース一覧、共通データ構造は分野をまたぐため、統合後のビルドと回帰確認は欠かせなかった。worktreeは独立性の高い変更に有効だが、小さな変更でも必ず作る儀式ではない。

作業を委任する場合は、同じチェックアウトに複数の書き手を置かず、worktreeを切った範囲の明確なタスクにする。具体的な必要がない限り、サブエージェントのモデルは既定のままにする。実行中のエージェントが利用上限に達した場合は、同じエージェントを続行し、同じ作業に重複エージェントを起動しない。調査タスクは指定された計画書以外を変更せず、参照資料を確認する前に実装へ進まない。

失敗を受け、運用ルールは次第に厳格化した。

- 作業ブランチからPull Requestを出す。ローカルのマージやローカルでの成功だけを根拠に<code>main</code>へ統合しない。
- PRの正確なheadをレビューし、同じSHA上で必須のGitHub Actionsを成功させる。親コミットや古いheadの結果は過去の証拠である。
- 変更が小さいうちに対象テストを実行し、その後に広いCTest選択と運用確認を行う。CTestのlabelごとに対象が異なるため、通常suite、smoke test、package test、soak testを区別する。
- <code>CURRENT_STATE.md</code>には正確なソースsnapshot、コマンド、環境、結果、所要時間、除外範囲、残る制限を記録する。再現コマンドとゲートの定義は<code>VERIFICATION.md</code>に置き、contractごとに正本を一つにする。
- 過去の<code>DONE</code>行や一つのテスト成功を、より広い互換性の主張へ拡張しない。90項目の分類、厳密監査の件数、テスト件数、品質評価、リリース・公開状態は別々に扱う。

情報の正本を分ける運用は役に立った。<code>CURRENT_STATE.md</code>は変化する状態、<code>DEVELOPMENT.md</code>は所有関係と手順、<code>SPEC_*</code>はcontract、<code>VERIFICATION.md</code>は証拠の定義を担った。継続した問題は、ソースが状態台帳より速く変わったことだ。このチェックアウトで最後に記録された台帳は2026-10-04、本書は2026-10-07時点である。引き継ぎ前に台帳を実コードとテスト登録に照らし合わせること。

開発・テストのコマンドには時間上限が必要だった。Python、CJS、C++のハーネスが<code>cppfm</code>を子プロセスとして起動する場合、親ハーネスだけを停止するとサーバーが残ることがある。テストは所有するプロセスIDを保持し、期限つきで待ち、終了後に子プロセスを確認する。広すぎる<code>pkill</code>パターンは過去に無関係なプロセスを停止させたため、対象を確認し、PID所有権か実行ファイルの完全一致を使う。長いsoakやサーバーテストは、上限時間とkill graceなしに実行しない。

## アーキテクチャの変遷と設計上の経験

当初は比較的中央集権的なサーバー構成で、機能拡張に伴って扱う挙動が大きく広がった。その後、責務を明確な所有者へ分ける作業が進んだ。記録対象の基点（<code>4756a5b0</code>、PR #5）では、ディメンションごとの状態を<code>GameServer::DimensionRuntime</code>へまとめた。その後の未コミットの作業では、sessionとgameplay処理の<code>GameServer_*.cpp</code>への分割、プレイヤー受け入れ処理の<code>PlayerDirectory</code>への分離、クライアントごとのエンティティ可視性、block entityのcodec/projection/index化、段階的なclone transaction、chunk保存調整と復旧の形式化が進められた。

分割によって本物の境界ができる場合に、アーキテクチャ上の価値がある。

~~~mermaid
flowchart LR
    Core[core: bytes / NBT / JSON / compression] --> Net[net: connections / framing]
    Proto[proto: versioned IDs] --> Net
    Net --> Session[Session: per-connection protocol state]
    Session --> Server[GameServer: composition / authoritative dispatch]
    Directory[PlayerDirectory: roster / admission] <--> Server
    Server --> Runtime[DimensionRuntime]
    Runtime --> World[World and chunk state]
    Runtime --> BE[BlockEntityStore]
    Runtime --> Sim[Physics and gameplay services]
    Runtime --> Persist[Persistence]
    Server --> IO[ChunkSaveCoordinator / ChunkBodyCache]
    IO --> Persist
    Persist --> Region[Anvil / RegionFile / recovery]
    Region --> Disk[(World files)]
    Player[Player state] --> Tracking[ClientEntityTracking: sent chunks / mobs]
    Server --> Tracking
    JVM[src/jvm: bounded callback and runtime bridge] <--> Server
    Data[Generated registries / assets] --> Runtime
~~~

- **構成と寿命:** <code>GameServer</code>は今もアプリ全体を構成する。各<code>DimensionRuntime</code>はワールドとそのローカルサービスをまとめる。サービスから呼ばれるcallback先はサービスより長生きしなければならず、メンバー宣言順と破棄順も正しさの一部である。helperに分割しても<code>init()</code>は構成処理である。
- **Sessionとサーバー状態:** <code>Session</code>は接続ごとのプロトコル進行を、<code>GameServer</code>は共有される正本状態を持つ。<code>PlayerDirectory</code>は参加者一覧と受け入れ予約を所有する。重複login、quit callback、永続化、アドレス再利用の間で、<code>Player</code>の寿命とJVM handleの破棄時期を同期する必要がある。
- **実行状態と直列化表現:** chunk、block entity、item stack、playerは、保存形式やwireへの射影より豊かな状態を持ちうる。専用codecとprojection helperを設けると変換をレビューしやすい。可能な限り未知のNBTを保持し、wireに表現できないデータは黙って捨てずfail closedにする。
- **変更と公開:** gameplayの変更は、observerやclientへ通知する前に準備、検証、確定する。<code>CloneTransaction</code>やblock mutationの受け入れ確認はこの考え方の例である。rollbackの対象はブロック配列だけでなく、dirty generation、block entity、callback、ネットワーク通知にも及ぶ。
- **snapshotとI/O:** 権威を持つ側で不変のchunk bytesを採取し、その後のファイルI/Oをworkerで行う。ディメンションつきkey、chunkごとのgeneration、最新save ID、dirty versionは、古い非同期load/saveが新しい状態を上書きしないためにある。保存成功後も、対象snapshotが現在の版に一致する場合だけdirtyを解除する。
- **可視性とエンティティの存在:** 全員共通のentity一覧だけでは、各clientへ何を送るか決められない。clientが受信済みのchunk、追跡中のentity、dimension移動、despawn、chunk忘却前後のpacket順を管理する。ロード済みchunkを使うproxyは便利だが、バニラのentity trackingとは完全一致しない近似である。
- **拡張callbackとlock:** callbackからnative codeへ再入したり、server thread上の処理を待ったり、session threadとtick threadの間で重なったりすることがある。recursive simulation gateとscoped release helperはcallbackによるdeadlockを避けるためのものだが、任意のMod状態のthread safetyやFabricと同じcallback affinityまでは保証しない。
- **永続化と復旧:** parseに失敗したcandidate fileを即座に破棄しない。破損とI/O失敗を区別し、仮の状態へdecodeし、置き換えが永続化されるまで旧generationを残す。「renameは完了したがdirectory syncが失敗した」状態を不確実な結果として明示する。

主要な設計上の教訓は、行数ではなく、所有権と失敗境界に沿って分けることだ。関数を小さな翻訳単位へ移すだけでは設計改善にならない。大規模な分割は、call graph、破棄順、CMake登録、統合テストの確認負担を増やす。2026-10-04の台帳には、その時点以前の対象テストや広いゲートの成功とともに、完全な厳密レビューと正確なheadのCIが未完了と記録されている。この区別を守ること。テスト成功は実行した挙動の証拠だが、大規模リファクタ全体の保守性や全経路の正しさを証明しない。

## 後継案rs-fabricとの比較

2026-10-07に後継案の[<code>rs-fabric/plan.md</code>](../../rs-fabric/plan.md)を確認した。ファイル名の先頭は小文字である。確認時点では後継リポジトリにcommitがなく、計画書も未追跡だった。以下は設計案への考察であり、稼働するruntimeの実績ではない。

この案は、本プロジェクトより早い段階で大きな構成判断を示している。Pumpkin由来のRustサーバーを既定の実行エンジンとし、互換性とJava依存の処理には本物のFabric Loader/Mixinを動かすJVMを残す。起動時にJavaが必要となりうる範囲（Java Envelope）と、runtimeで実際にJavaへ渡す処理を分ける。さらにsafepointでの状態遷移、objectの寿命中は原則一方向の所有権昇格、Rust/Java間の一括bridge呼び出し、runtime計測、Create着手前の小さなPoCを提案している。これらはCppFabricMCがたどった経緯への有効な対策になりうる。こちらはプロトコル中心の実装から大きな機能表へ拡大した後に、ownership、clientごとの可視性、永続化調整、復旧、session境界を抽出していった。機能台帳は抜けを見つける一方、runtime境界や証拠が固まる前に項目の完了数を増やす圧力にもなりうる。

引き継ぐ価値が特に高いのは、Modなし、起動処理だけを行うFabric Mod、tick eventを一つ登録するModの順で試すPoC、CPU比率と状態所有権とJVM常駐を別々に測る方針、粗いJava islandが動くまではCreateのnative化を後回しにする判断である。計画書も性能割合を測定値ではなく目標として正しく扱っている。再現可能な計測を固定したbaselineと比較するまでは、目標のまま表示すること。

最も重要な未決事項は、**Minecraftのobjectと変更可能なワールド状態の正本をどちらのruntimeが所有するのか**である。JVM上で本物のMinecraft server objectを使えば、Java ModやMixinは期待されるidentityやfieldを観測できる。しかしRustはJava所有状態の周辺で加速処理をする側となるため、同期、callbackコスト、Rust所有にできる範囲を正確に定義する必要がある。Rustがワールドを所有する場合は、Java向けwrapperやmaterializeしたobjectが、任意のModから観測されるidentity、直接のfieldアクセス、reflection、Mixin追加状態、monitor、threadの挙動を保たなければならない。選択したfieldだけをコピーするwrapperでは、一般にはこれらの意味を保てない。「Full Java fallback」を成立させるには、Javaが同期済みcopyを常に持つのか、起動時にJava所有を選べるのか、稼働中に完全な状態を移譲できるのかを決める必要がある。bridge両側を実装する前に正本モデルを決定すること。そうしないと、設計上はfallbackがあっても戻すべき正しい状態がない。

計画書にある複数の概念も、実装前に一つの運用モデルへまとめたい。実行状態（<code>NATIVE</code>、<code>GUARDED</code>、<code>HYBRID</code>、<code>JAVA</code>）はsemantic operation単位で説明され、所有権昇格はobject単位、contaminationはcode/type/spatial/temporal/subsystemの各軸にまたがる。これらをどう統合するかを定義すること。state domainごとの正本、昇格の条件、参照や副作用を通じたcontaminationの伝播、domainを解放できる時点、処理中の仕事を排出してから遷移する方法を決める。callback再入、error伝播、lock順序、thread affinity、永続化もcontractに含める。safepointは遷移競合を減らすが、任意のMod threadや共有objectを安全にするものではない。

Post-Mixin bytecode fingerprintは、既知でレビュー済みのnative specializationを有効にする条件としては合理的である。fingerprintが変わった場合、その最適化を無効にして互換経路へ進める。ただしhash一致が示すのはbytecodeの版であって、native実装が意味を保つ証明ではない。未知のbytecodeを安全に扱うには、正しいobjectと状態で実行できる本物のJava経路が必要になる。静的依存解析とoptional nativeizationは、明示的な非対応ケースを持つ最適化として扱い、任意のModの意味をすべて捉えた証明と混同しないこと。

過去のfeature-firstな拡大を繰り返さず、この計画を実行可能にするための提案は次のとおり。

- 実装に入る前に、runtimeの正本とfallback、object identityと変更規則、thread model、対応範囲を短いdecision recordにする。Minecraft、mapping、Java、Loader、Fabric API、Pumpkinのrevision、Createとaddonの版・hashを固定する。旧プロジェクトの1.21.4 / protocol-769の事実を、後継の1.20.1 / Java-17へ流用しない。
- 最初の3つのPoCに観測可能な合格条件をつける。callbackを呼び出せたかだけではなく、JVMの起動条件、event順序と実行thread、重複しない一回の配信、変更の可視性、例外動作、終了・再起動、Modなしのhot path overheadを確認する。
- Create着手前に、再現可能なreference buildと挙動baselineを用意する。その後の各phaseを小さくし、fixture、合格条件、commitと対応づく結果を残す。Createはまず大きなJava islandとして動かし、identity、同期、保存・再読込、境界をまたぐ問い合わせが成立してからtypeまたは空間単位に狭める。
- Rust CPU時間、Java CPU時間、bridge overhead、Rust所有状態、Java所有状態、JVM常駐を別々に測る。同一workloadとhardwareで固定したvanilla/Pumpkin baselineと比較する。「native率」一つにまとめて遅いbridgeや二重管理を見えなくしない。
- 正本の状態・証拠台帳を一つに保ち、最後のソース変更後に更新する。今回の巨大な作業ツリーのようにruntime変更、テスト、生成物、文書を混在させると、どの証拠が有効か判断しにくい。正確なコマンド、ソースrevision、除外事項、失敗も残し、中断・部分実行を成功扱いしない。

プロセス全体の教訓は、実装を計画書で置き換えることではない。大きな再設計を招く少数の決定を先に明確にし、その判断を狭く証拠に基づくprototypeで早期に検証する。後継プロジェクトには、互換性の範囲が膨らむ前に正本モデルと検証規律を確立できる余地がある。

## 続ける価値のあったこと、手戻りを生んだこと

**引き継ぎたい実践:**

- 調査と互換性の主張には、対象ゲーム、protocol、dataの版を固定する。版つきの公開API資料、wire vector、実測した挙動はそれぞれ別の根拠として明記する。
- 各機能にソース所有者、behavior contract、個別の回帰テストを持たせる。単体テストで確認できない影響は実protocol clientで検証し、packageやsoakのテストは実際に確認した範囲だけを証明に使う。
- 新しい状態が安全に確定するまで、古いversion、復旧candidate、不透明データ、旧sessionを残す。旧状態を保持できず新状態の正しさも証明できない場合はfail closedにする。
- harnessが起動するserver/client processを自分で所有させる。正しいoutput streamの完全な行を証拠にし、固定sleepを成功条件にせずprotocol acknowledgementを待つ。
- 失敗結果も正確に記録する。7200秒のsoak試行はmemory増加のgateを超えたため中断され、合格ではない。GUIや認証済みclientの証拠がないことも、headless Mineflayer testの成功とは分けて記録する。

**手戻りや誤った確信につながったこと:**

- 対象範囲が大きくなりすぎた。runtime、互換性、テスト、生成データ、文書を混ぜた大差分では、どの証拠がどのソースsnapshotを対象とするか追いにくい。PRを小さくし、最後のソース変更後すぐに状態台帳を更新する。
- test harness自体にも実際の不具合があった。別streamの断片からmarkerが誤って合成された、可変entity handleが別entityのunloadを観測した、startup容量テストがConfiguration前の参加受付を仮定した、固定の移動待ち時間では観測条件を安定して越えられなかった、などである。システムだけでなく証拠を作る仕組みも検証する。
- ソースとテストの期待値がずれることがある。古いguardやfixtureの仮定が原因の場合も、実製品のregressionの場合もあった。意図した親revisionと現行ソースで再現し、reference contractが裏付ける場合に限りassertionを更新する。
- 個別テスト成功は過去の全suite結果を復活させない。記録上の83 test選択、個別の<code>smoke80</code>、後続変更後の個別テストは、最終ソースに対する一回の検証ではなかった。それぞれの範囲を別に報告する。
- 数値の品質目標は、未追跡ファイルを含む正確な全差分をレビューして初めて意味を持つ。過去の厳格な評価は6/10未満であり、2026-10-07の全差分レビューも完了しなかった。目標達成済みと表現しない。
- C++の寿命問題は一見無害なhelperに潜む。過去の<code>/give</code>失敗は、条件式で作った<code>std::initializer_list</code>のbacking arrayが期限切れになった後に範囲走査したことが原因だった。スコープを越えるcollectionには所有されたcontainerか直接のrangeを使う。

## アーカイブ時点と正確な状態

記録対象のbaseは<code>4756a5b0cc660de714f1971e8a6c080d1ea02640</code>で、作業ブランチ<code>codex/deep-refactor-quality</code>はこのcommitから始まっていた。このアーカイブcommitが記録するのは引き継ぎ文書のみであり、他のソース・文書変更や作業ツリー全体がcommit済み、統合済み、リリース済みであることを意味しない。cleanupの前に、未追跡ファイルと利用者が所有するファイルを含め、残す作業状態を確認すること。

最後に記録された台帳には、<code>smoke80</code>と<code>soak2h</code>を除外した83/83のCTest選択、別々に行われた<code>smoke80</code>とMineflayer実行、JVM corpus、storage/item-codecの確認などが記録されている。そのCTest選択にはほかのlabelも含まれていたため、コマンドと除外項目を正確に保つこと。各結果は特定snapshotとコマンド選択に対するものなので、後続変更で古くなる。最後の記録時点では完全な厳密品質レビュー、PR、headに対するGitHub Actionsが残っていた。以前の厳格評価も6/10未満だった。広いCTestとtargetedなlive-client確認は、全変更行のレビューや正確なheadのCIを置き換えない。

最後に記録された制限には次がある。

- バニラと同じworld-generation RNG呼び出し順とstructure NBTの一致は証明されていない。seedやRNGの基本vectorだけでは十分でない。
- 埋め込みHotSpot/JNI経路は限定的である。build時にJNIが必要で、記録された1.21.4経路には対応するJava runtime（21以降）が必要だった。native-only binaryをbuildした後からJDKを導入してもJVM機能は追加されない。
- このruntimeはMojangのGameProviderではない。選択したcallbackと固定Mod corpusでの確認は、任意のFabric Mod互換性を示さない。Mod callbackはsession threadとtick threadの間で重なる場合があり、native変更はserver threadへ送るものの、Fabricと完全に同じcallback affinityは再現していない。
- 2時間または24時間soakの合格artifactは記録されていない。7200秒の試行はRSS増加が設定gateを超えて中断されたため、合格と呼ばない。
- headless protocol/Mineflayer検査は、Microsoft認証済みloginや公式GUI clientの証拠ではない。最後のhost確認では、vanilla clientとdisplayが見つからなかった。
- <code>/place feature</code>は固定patternに対する安全なmutation経路の限定実装であり、configured-feature registryの実行やバニラと同じfeature生成ではない。

最新の作業中ソースには、entity tracking、段階的clone transaction、block entity codec/projection分割、chunk save/load調整、region復旧も含まれていた。再利用する場合は現行コードとテストを確認すること。名前や設計意図だけでは、すべての失敗経路が正しいと証明できない。

## アーキテクチャ概要

大まかな層構造は、<code>src/core</code>がbyte buffer、JSON、NBT、UTF-8、圧縮、<code>src/proto</code>がprotocol ID、<code>src/net</code>がframing、暗号化、packet batching、RCON、<code>src/game</code>がsession、player状態、gameplay、inventory、command、永続化を担当する。<code>src/physics</code>と<code>src/worldgen</code>がシミュレーションと生成を持ち、<code>src/jvm</code>と<code>jvm/</code>が任意で使える限定的なJava境界を実装する。<code>src/generated</code>以下のregistryと<code>assets</code>内の版つきdataは入力であり、手書きの正本テーブルではない。

<code>GameServer</code>が構成のrootである。ディメンションごとの状態は<code>DimensionRuntime</code>にまとめる。<code>init()</code>はcallback先とserviceを結びつけるため、objectの宣言順と破棄順が重要である。永続化serviceは、呼び出し先callbackが破棄される前に停止しなければならない。現在の実装は<code>GameServer_core.cpp</code>、<code>GameServer_tick.cpp</code>、<code>GameServer_combat.cpp</code>、<code>GameServer_items.cpp</code>、<code>GameServer_world.cpp</code>、<code>GameServer_session*.cpp</code>へ分割されている。<code>PlayerDirectory</code>は参加者一覧と受け入れ予約、<code>Session</code>は接続ごとのprotocol状態を所有する。実行中のblock entity状態は<code>BlockEntities.hpp</code>にあり、codec/projection/index/wire helperが直列化境界を担う。<code>ChunkSaveCoordinator</code>と<code>ChunkBodyCache</code>は、save/loadのgeneration管理とencode済みbody cacheという別の責務を持つ。

実装で得た境界上のルール:

- 各挙動・contractに正本となる所有者を一つだけ設ける。ソース分割では処理を移動し、二重の実装主体を作らない。
- extension codeの呼び出し中や、server threadを待つ可能性のあるI/O中にworld mutation gateを保持しない。callbackが同期的な処理をenqueueする必要がある場合は、recursive simulation gateの全取得をいったん解放してから呼び出し側のlock境界を戻す。
- lock順序を定めて文書化する。world/chunk、entity、block entity、player directory、publication、command、simulationのlockはcallbackを通じて連動しうる。広い状態lockを保持したままユーザー/JVM hookを呼ばない。
- 非同期chunk I/Oには不変なbyte snapshotを使う。dimensionと座標をkeyにし、generationで古いload/saveを無効化する。保存したsnapshotが現在のdirty versionと一致するときだけdirty状態を解除する。
- entityの可視性は全体ではなくviewerごとに管理する。clientが受け取ったchunkと追跡中entityを記録し、chunk忘却、dimension移動、entity削除の前後でspawn/update/remove packetを安定した順序で送る。
- login置換は単純な一覧上書きではなくhandoffである。identityを予約し、旧sessionをretireし、必要なcleanup/persistenceを待ち、失敗したidentityをfenceしてから新sessionを公開する。
- 復旧candidateは昇格が永続化されるまで残す。壊れたdataと通常のI/O失敗を区別し、安全に読み取れなかったfileを上書きして再生成しない。

## protocolとgameplayで間違えやすい点

ここに挙げるのは実際のbugやレビューの反復を招いた点である。最終的な正本はwire仕様とテストであり、この一覧は注意喚起用である。

- protocol IDは版と方向で異なる。1.21.4の表ではPlay S→Cに<code>OpenScreen 0x35</code>、<code>ContainerSetContent 0x13</code>、<code>TradeList 0x2E</code>、<code>KeepAlive 0x27</code>があり、Play C→Sの<code>KeepAlive</code>は<code>0x1A</code>である。近いpacketや別版ではなく、方向・stateの表で確認する。
- chunk sectionの単一値paletteでも<code>longCount</code>は0をencodeする。block-state packingは<code>state&lt;&lt;12 | x&lt;&lt;8 | z&lt;&lt;4 | y</code>、bundle block-position packingは<code>lx&lt;&lt;8 | lz&lt;&lt;4 | ly</code>である。x/y swapは過去に再発した。
- world borderの直径は59,999,968で、29,999,984は半径である。補間更新は50 ms tick timingを使う。simulation distanceはChebyshev距離（<code>max(abs(dx), abs(dz))</code>）であり、Euclidean距離ではない。
- よく使うslot component IDは<code>damage=3</code>、<code>repair_cost=17</code>、<code>trim=45</code>である。trim payloadの順序はmaterial、pattern、tooltip flag。保存時の未知item NBTは保持し、wire表現にできない場合はfail closedにする。opaque componentを黙って除くと利用者dataまたはclient状態を壊す。
- armorによるdamage軽減は、バニラ形式の中間値<code>f=2+toughness/4</code>を求め、<code>armor - damage/f</code>を<code>[armor*0.2, 20]</code>にclampする。armor/toughness上限とprotection enchantment処理は独立したvectorで確認する。
- 負座標、offhandの<code>-106</code>を含む負のitem slot alias、空NBT list、optional NBT、signed VarInt/VarLong、UTF-8/UTF-16変換は個別に検証する価値がある。
- registry IDはcontextごとに異なる。block、entity type、sound、effect、potion、enchantment、trimの生成IDを混同したり、item IDから推測したりしない。

## 保存、parse、失敗経路の知見

- fileとpacket lengthはすべて信頼しない。可能な限りreserve/resizeより前に、圧縮後・展開後サイズ、nesting深度、要素数、文字列長、累積allocationを制限する。allocation後の上限確認では大量確保を防げない。
- Anvil external chunkは<code>.mca</code>のlocation stubと座標名の<code>.mcc</code> sidecarを組み合わせる。write/recoveryはcrash後も組を保ち、対応するsidecarなしでstaged <code>.mca.new</code>だけを昇格させない。現在のtransaction protocolとfault-injection testを確認すること。read-onlyだった旧段階の文書に依存しない。
- atomic renameだけでは永続化されない。必要に応じてfileと親directoryをsyncし、rename後のdirectory sync失敗は不確実な結果として表す。通常のrollbackや完全成功として扱わない。
- recoveryと利用者指定pathには、exclusiveな同階層temporary fileとregular-file/no-follow確認を使う。symlinkをたどる、FIFOで停止する、未知のrecovery markerを上書きする、現在のtransactionが所有しないfileを削除することを避ける。
- parseと状態公開はtransactionとして扱う。まず一時領域へdecodeし、必要な検証がすべて終わってからworld/player状態を公開する。変更開始後に例外が起きたら中断し、変更済みlive stateに別のrecovery candidateを適用しない。
- ItemStackの永続化ではopaque NBTを値または不変templateとして保持する。copy/splitはstackingに必要なidentityを維持し、component編集では既知fieldだけを更新して未知の兄弟要素を落とさない。wire化に影響する未知componentを扱えない場合は、lossyなstackを生成せず接続・操作を明示的に拒否する。

## テストと運用の知見

- <code>CMakeLists.txt</code>のCTest登録がtarget名、label、環境、timeoutの正本である。個別targetの成功はそのassertionだけを意味する。source tree内のtestはpackage testではなく、300秒soakは2時間soakではなく、protocol clientはGUI clientではない。
- configure/build、個別unitまたはwire test、live protocol/gameplay統合、永続化・復旧fault、package展開、process cleanup、長時間loadを別々の証拠として保つ。正確なcommit/head、コマンド、環境、所要時間、除外範囲を記録する。
- testが所有するserver/clientは所有PIDまたはprocess handleで終了させ、終了を待つ。広い<code>pkill</code>は過去に別processを巻き込んだ。手作業で回収する場合は完全なcommand lineを確認し、実行ファイル名の完全一致か、自分自身に一致しない完全なpatternを使う。
- 長くなる可能性がある全commandに外側のtimeoutとkill graceを設定する。harnessが<code>cppfm</code>を起動する場合、Python/Node親だけを停止するとserverが孤児化する。失敗後も所有する子processの終了を確認する。
- 可能なら固定sleepではなくprotocol状態を待ってtestを決定的にする。movement observation threshold、JVM初回起動の遅さ、server起動中のadmission phase、非同期output順などが実際のtiming要因だった。時間待ちが避けられない場合は理由を記録し、十分な上限を設定する。
- stdoutとstderrは別streamである。複数streamの断片を連結し、片方のstreamには存在しないfixture markerを誤認しない。正しい一つのstreamで完全な行を照合し、diagnostic tailを切り詰める前に証拠を記録する。
- C++の寿命bugは簡単なhelperにも潜む。過去の<code>/give</code> crashは、条件式で生成された<code>std::initializer_list</code>のbacking arrayが期限切れになった後の範囲走査が原因だった。保持・返却するcollectionには所有container、寿命の明確なcompile-time array、または直接のindex rangeを使う。
- ignore対象の<code>build/</code>にあるbuild/test結果はローカルの証拠であり、公開release artifactではない。アーカイブに必要ならlogとartifactを保存する。追跡対象のartifactがなければ、別の開発者が同じ結果を独立して再現できない。

## 後継プロジェクトで作業を再開する場合

1. clean clone、branch作成、アーカイブ化の前に、元のcheckoutと未追跡ファイルを含む全作業状態を保存する。<code>git status --short --untracked-files=all</code>で確認し、未追跡ファイルを含まない<code>git diff</code>だけで判断しない。
2. 合意されたbase SHAを明示的に確認する。指定されたbaseの代わりにローカル<code>main</code>やremote-tracking refを使わない。
3. <code>AGENTS.md</code>、本書、<code>CURRENT_STATE.md</code>、<code>DEVELOPMENT.md</code>、対象領域のcontractを読む。状態記録の方が作業対象ソースより新しいかも確認する。
4. 観測可能な挙動、バニラと対象versionのreference、ownership境界、失敗経路、証拠を明記した小さなissue/planを作る。調査はread-onlyとし、reference contractが確定してから実装する。
5. 回帰テストを伴う最小の変更を行う。無関係なdirty fileを保ち、利用者所有のartifactを「掃除」しない。
6. 対象テストから実行し、その後に設定済みの広いgateを実行する。kill graceつきの時間制限コマンドを使い、process所有者を確認し、logを保存する。実行後にソースsnapshotが変わったなら、その結果を変更後snapshotの証拠と主張しない。
7. 新規・未追跡ファイルを含む正確な全差分について独立レビューを受けてから準備完了と判断する。重要な指摘を修正し、影響するgateを再実行する。
8. GitHubで公開する場合は、そのrepositoryのPRと正確なheadに対するActions方針に従う。ローカルbuildだけでmergeしない。repository自体をアーカイブする場合は、未commitの作業と証拠を残すかどうか、残す場所を明記する。

このプロジェクトを再利用する際は、過去の機能数を完成度の証拠として持ち込むのではなく、contract、証拠の扱い、復旧に関する知見を引き継ぐこと。
