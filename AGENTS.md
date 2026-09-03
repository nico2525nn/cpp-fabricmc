# AGENTS.md — cpp-fabricmc Workflow Handover

> **Purpose:** 次セッションのエージェント（人間/サブエージェント）が `cpp-fabricmc` (Fabric 1.21.4 / protocol 769) の改善ループを中断なく引き継ぐための必須情報。`docs/research-prompt.md` が正規手順。**すべてのサブエージェントは muse (`agent: "muse"`) を使用する (グローバル `~/.config/opencode/AGENTS.md` の委任ルール準拠)。** 動的状態 (HEAD・テスト実績・plan 番号・Next Steps) は `docs/CURRENT_STATE.md` を参照。

## 1. Project Goal

- Minecraft Fabric 1.21.4 サーバーを C++ で非公式に完全再実装し、**完全な互換性** (protocol-compatible) を目指す。
- `docs/MISSING_FEATURES_1_21_4.md` の `PARTIAL/TODO` が 0 になるまでループ。`docs/assessment-1.md` の厳密Wire監査 (78 gaps) も 0 まで。

## 2. Current State

> **動的状態 (HEAD・テスト実績・plan 番号・Next Steps) は docs/CURRENT_STATE.md を参照。ループ完了ごとに更新すること。**


## 3. Improvement Loop Workflow (厳守)

```
1. research-prompt.md を **muse サブエージェント** (`agent: "muse"`, グローバル `~/.config/opencode/AGENTS.md` の委任ルール準拠) に投げる
   → muse が Web Search + Web Fetch 無制限で本家 Mojang/Fabric を検証し、
     plan/planX.md (X = max(plan/plan*.md)+1) を作成。
     各章13観点 (機能概要/本家仕様/クラス・データ構造・パケット・イベント・状態遷移/
     実装フロー/C++設計例/クラス構成/モジュール分割/注意点/パフォーマンス/
     スレッドセーフティ/エッジケース/テスト方法/実装優先度) を必ず含める。
     各章冒頭で `docs/MISSING_FEATURES_1_21_4.md` の対象項目番号を明記。
     ファイル変更は plan/planX.md のみ (.gitignore は plan/ のため追加不要)。

2. おおよそ6分野に分けて worktree を作成
   mkdir -p /tmp/opencode/wtX
   for n in world block entity inventory network combat; do
     git worktree add -b wtX/$n /tmp/opencode/wtX/$n HEAD
   done

3. サブエージェント (**すべて muse**) を分野別に同時に **バックグラウンド** 起動
   - 必ず `background: true` かつ `agent: "muse"` で起動:
     {
       "agent": "muse",
       "description": "Impl wtX/entity ...",
       "prompt": "Implement planX §... in /tmp/opencode/wtX/entity ... Keep build green: cmake -B build && cmake --build build -j4. Commit \"planX entity: ...\". Work only in wtX/entity. Background.",
       "background": true
     }
   - `general`/`explore` は使わない (グローバル AGENTS.md: 手を動かす作業は必ず muse に委任)。
   - 同一worktreeでの重複起動は絶対に避ける (被ると競合)。
   - 研究完了前に実装を開始しない。
    - **すべてのコマンドに確実なタイムアウトを付与** (`test_smoke_80` は子プロセス `cppfm` を fork するため親だけを殺すと孤児化)。例: `timeout --foreground --kill-after=5 120 cmake -B build -G Ninja` / `timeout 300 cmake --build build -j2` / `timeout --foreground --kill-after=5 450 ./build/test_smoke_80 ./build/cppfm 2>&1; echo EXIT:$?; pkill -9 -f "cppfm --port"`。`ctest -R smoke80 --timeout 450` でも可。

4. 並行で開発させ、すべてが終わったあと diff をレビューしマージ
   - `git diff main --stat` と `git log --oneline --graph` で確認
   - 競合は `plan/` の内容を保持しつつ `src/` の正しいwireを優先して解消
   - マージは `git merge --no-ff wtX/<name> -m "merge wtX/<name>: planX <name>"` で履歴に痕跡を残す
   - マージ後は `cmake --build build -j4` と `./build/test_native` で green を確認

5. 随時すべてのドキュメントに漏れがないように完璧に更新し続ける
   - `docs/MISSING_FEATURES_1_21_4.md` の Status を DONE/PARTIAL/TODO で正確に更新
   - `docs/assessment-1.md` の厳密Wire監査を更新 (file:line 絶対パス)
   - `README.md` の What works / Testing — evidence を更新 (80 taxonomy vs 78 strict の数値を明確に)
   - `docs/PROTOCOL_NOTES.md` の新パケット (Bundle/MultiBlockChange/UpdateLight) を追記
   - `docs/MIGRATION_GUIDE.md` の Module map を更新

6. MISSING の内容がすべて DONE になるまでループ (厳密監査も 0 まで)。
```

**計画の並列化 (パイプライン・時間短縮 — 2026-09-01 追記):**
- **研究と実装のパイプライン化**: planN の実装フェーズ中に planN+1 の研究をバックグラウンドで同時起動してよい (研究は読み取り + `plan/planX.md` 書き込みのみで src/ や worktree と競合しない)。例: `wt40/*` 実装中に `plan/plan41.md` 研究を並行起動。
- **複数 plan の同時研究**: 依存のないアイテム群は複数の研究エージェントを並列起動して、複数の plan ファイルを並行生成してよい (plan ファイルは別名なので安全)。研究のファイル参照先 (HEAD) が変わっても影響しないよう「最新 HEAD をその都度確認」をプロンプトに含める。
- **1 plan に複数アイテム群**: 1 つの plan に独立なアイテム群を複数含め、6 worktree で並列実装する (既存の §3 step 2-3)。
- **制約**: 同一 worktree の重複実装は禁止 (既存)。研究は常に「読み取り + plan/ への書き込みのみ」であること。実装は常に「研究完了後」 (禁止事項の「研究が終わる前に実装を開始しない」は各アイテム群に対して有効)。

**禁止事項:**
- サブエージェントは **必ず muse** (`agent: "muse"`) を使う。`general` は使わない。
- `background: false` (デフォルト) でサブエージェントを起動しない。必ず `background: true`。
- 同一worktreeで2つのエージェントを同時に動かさない。
- 研究が終わる前に実装を開始しない。
- `plan/*.md` を `git add -f` で追跡しない (無視のまま)。
- **曖昧な `pkill` は禁止** (2026-09-03 事故: `pkill -9 c++` が「c」を含む全プロセスを kill)。`pkill` は完全名 (`pkill -x cppfm`) または明確なフルコマンドパターン (`pkill -9 -f "cppfm --por[t]"` — **自己非マッチ化推奨**: `--port` のままのパターンは実行シェル自身にマッチし自滅する事故が 2026-09-04 に実地確認済み)。kill 前に対象確認 (`pgrep -a`) を必ず行うこと。

## 4. Plan Numbering

- `ls plan/plan*.md | sort -V | tail -1` で最大Xを取得 → 次は `X+1`。
- 現在最大・次番号は `docs/CURRENT_STATE.md` §3 を参照。
- 生成後は `ls -lh plan/planX.md && wc -l plan/planX.md` で確認。

## 5. Testing (Evidence)

> テスト実績 (PASS/FAIL 数) は `docs/CURRENT_STATE.md` §2 を参照。以下は実行手順 (静的)。

```bash
timeout --foreground --kill-after=5 120 cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
timeout 300 cmake --build build -j4          # -j2 if OOM on /tmp tmpfs
timeout --foreground --kill-after=5 60 ./build/test_native ./build/cppfm          # ALL PASS (12+31+10) expected (50s)
timeout --foreground --kill-after=5 30 ./build/test_scoreboard_reset               # 22/22 PASS expected (ctest scoreboard_reset TIMEOUT 30)
timeout --foreground --kill-after=5 60 ./build/test_spec_wire                     # 328 PASS 0 FAIL 0 SKIP (wire byte-identical lock; 実績は CURRENT_STATE §2)
timeout --foreground --kill-after=5 30 ./build/test_fuzz                          # 23 PASS 0 FAIL (fuzz 23 cases, TIMEOUT 30)
timeout --foreground --kill-after=5 450 ./build/test_smoke_80 ./build/cppfm        # 212 PASS 0 FAIL (実績は CURRENT_STATE §2) — 450s (600s under load)
pkill -9 -f "cppfm --port" 2>/dev/null; sleep 1
timeout --foreground --kill-after=5 600 python3 tests/stress_test.py --clients 120 --binary ./build/cppfm  # plan45 O-04 dry 120 PASS
pkill -9 -f "cppfm --port" 2>/dev/null; sleep 1
timeout --foreground --kill-after=5 400 python3 tests/soak_test.py --duration 300 --binary ./build/cppfm    # plan45 O-05/O-06 short gate
timeout --foreground --kill-after=5 60 python3 tools/bench_chunk_gen.py --view-distance 32 --chunks 4225 --dry --strict  # plan45 O-11 view32
# nightly 24h (plan45 O-06 — ctest外): nohup python3 tests/soak_test.py --duration 86400 --binary ./build/cppfm > /tmp/soak24h.log 2>&1 &
ctest -R "native|scoreboard_reset|spec_wire|fuzz" --output-on-failure --timeout 60
ctest -R smoke80 --output-on-failure --timeout 450   # 600 under load
```

## 6. Architecture Quick Map

- `src/core/` ByteBuffer/NBT/Json
- `src/proto/Ids.hpp` 769
- `src/net/` Connection(zlib/AES-CFB8) PacketBatcher(Bundle/MultiBlockChange) Crypto
- `src/game/` World(24×16³) WorldGen(MultiNoise/Density/StructurePlacer) ChunkCodec Anvil Persistence level.dat BlockTickScheduler Fluids/Redstone/LightEngine Entities(86) BehaviorTree AiBrain Attributes Items Containers Recipes GameServer Session
- `src/worldgen/` DensityFunction MultiNoise Structures StructurePlacer PortalHandler
- `src/brigadier/` Tree Arguments
- `src/generated/` kBlocks 1095 kItems 1385 kEntities 149

## 7. Common Pitfalls

- `ChunkCodec` single-valued `longCount 0` 必須。
- `WorldBorder` diameter `59999968` (not `29999984`), lerp `tickWorldBorder()` 要 `50ms` 補間。
- `SimulationDistance` は Chebyshev `max(|dx|,|dz|)` (not Euclidean)。
- `Ids` off-by-one: `OpenScreen 0x34` `ContainerSetContent 0x12` `TradeList 0x2D` `KeepAlive 0x26`。
- `Bundle` axis `lx<<8|lz<<4|ly` (vanilla `state<<12|x<<8|z<<4|y`; NOT `ly<<8|lz<<4|lx` — x/y swap was plan28 finish fix, see assessment-1 S07 (old N7)).
- `SlotComponent` ids `damage 3 repair_cost 17 trim 45` (not 6/7/42)。
- `DamageCalculator` armor `f=2+t/4 g=clamp(a-dmg/f, a*0.2,20)` caps 30/20。

## 8. Next Steps

> **Next Steps (進行中作業・次の目標) は docs/CURRENT_STATE.md §4・§5 を参照。ループ完了ごとに更新すること。**

> このファイル自体が引き継ぎのエントリポイント。次回セッション開始時は本書の §3 のコマンドで worktree を再生成し、`plan/` の最新と `MISSING`/`assessment-1/2` を照合して再開すること。サブエージェントはすべて muse を使うこと。
