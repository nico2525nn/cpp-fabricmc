# AGENTS.md — cpp-fabricmc Workflow Handover

> **Purpose:** 次セッションのエージェント（人間/サブエージェント）が `cpp-fabricmc` (Fabric 1.21.4 / protocol 769) の改善ループを中断なく引き継ぐための必須情報。`docs/research-prompt.md` が正規手順。

## 1. Project Goal

- Minecraft Fabric 1.21.4 サーバーを C++ で非公式に完全再実装し、**完全な互換性** (protocol-compatible) を目指す。
- `docs/MISSING_FEATURES_1_21_4.md` の `PARTIAL/TODO` が 0 になるまでループ。`docs/COMPAT_AUDIT_1_21_4_STRICT.md` の厳密Wire監査 (78 gaps) も 0 まで。
- 現状: `plan/` に `plan.md`〜`plan28.md` が蓄積。最新は `plan/plan28.md` (718行, Score reset `0x49` 最終ハードニング)。`MISSING` は 80/80 DONE、strict 78/78 FIXED、deep 31/31 FIXED — **計109 gaps 全閉** (README 参照)。

## 2. Current State (2026-08-31 確認)

- **HEAD:** `77897ab` (`wt28` 6マージ完: world/block/entity/inventory/network/combat) → plan28 完遂。plan24〜28 で deep audit 31/31 FIXED に到達。**worktree は全て削除済み** (`git worktree list` は main のみ)。
- **Tests:** `cmake -B build -G Ninja && cmake --build build -j4` green。`./build/test_native ./build/cppfm` **ALL PASS (12/12)**。`./build/test_smoke_80` は 80/80 per taxonomy (最終確認ログ `/tmp/opencode/smoke80_plan28.log`)。
- **Docs:** `README.md` は strict 78/78 + deep 31/31 = 109 gaps closed に更新済み (2026-08-31 にTesting節の stale 記述も修正)。`docs/research-prompt.md` は `plan/` 一括無視のまま。
- **Git:** `plan/` フォルダは `.gitignore:10` で一括無視 (`plan/` 1行)。`plan/*.md` は追跡対象外。今後も `git add -f` 不要で無視される。
- **旧ブランチ:** `wt/blocktick` `wt/command` `wt/datapack` `wt/entity` `wt/inventory` `wt/light` `wt/network` `wt/placement` `wt/portal` が残存 (worktree 無し・削除しても可)。

## 3. Improvement Loop Workflow (厳守)

```
1. research-prompt.md をサブエージェントに投げる
   → サブエージェントが Web Search + Web Fetch 無制限で本家 Mojang/Fabric を検証し、
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

3. サブエージェントを分野別に同時に **バックグラウンド** 起動
   - 必ず `background: true` で起動:
     {
       "agent": "general",
       "description": "Impl wtX/entity ...",
       "prompt": "Implement planX §... in /tmp/opencode/wtX/entity ... Keep build green: cmake -B build && cmake --build build -j4. Commit \"planX entity: ...\". Work only in wtX/entity. Background.",
       "background": true
     }
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
   - `docs/COMPAT_AUDIT_1_21_4_STRICT.md` の厳密Wire監査を更新 (file:line 絶対パス)
   - `README.md` の What works / Testing — evidence を更新 (80 taxonomy vs 78 strict の数値を明確に)
   - `docs/PROTOCOL_NOTES.md` の新パケット (Bundle/MultiBlockChange/UpdateLight) を追記
   - `docs/MIGRATION_GUIDE.md` の Module map を更新

6. MISSING の内容がすべて DONE になるまでループ (厳密監査も 0 まで)。
```

**禁止事項:**
- `background: false` (デフォルト) でサブエージェントを起動しない。必ず `background: true`。
- 同一worktreeで2つのエージェントを同時に動かさない。
- 研究が終わる前に実装を開始しない。
- `plan/*.md` を `git add -f` で追跡しない (無視のまま)。

## 4. Plan Numbering

- `ls plan/plan*.md | sort -V | tail -1` で最大Xを取得 → 次は `X+1`。
- 現在最大: `plan/plan28.md` (718行, D26 Score reset 最終ハードニング)。次は `plan/plan29.md`。
- 生成後は `ls -lh plan/planX.md && wc -l plan/planX.md` で確認。

## 5. Testing (Evidence)

```bash
timeout --foreground --kill-after=5 120 cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
timeout 300 cmake --build build -j4          # -j2 if OOM on /tmp tmpfs
timeout --foreground --kill-after=5 60 ./build/test_native ./build/cppfm          # 12/12 PASS expected (50s)
timeout --foreground --kill-after=5 450 ./build/test_smoke_80 ./build/cppfm        # 80/80 per taxonomy, strict 78/78 + deep 31/31 FIXED (計109 gaps 全閉)
pkill -9 -f "cppfm --port" 2>/dev/null; sleep 1
ctest -R native --output-on-failure --timeout 60
```

`test_native` の `2 FAIL` (spawn-protection 0,-61,0) は `30,-61,0` 修正で解消済み (`a1dba28`)。現 HEAD (`77897ab`) で ALL PASS 確認済み。

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
- `Bundle` axis `ly<<8|lz<<4|lx` (not `lx<<8`)。
- `SlotComponent` ids `damage 3 repair_cost 17 trim 45` (not 6/7/42)。
- `DamageCalculator` armor `f=2+t/4 g=clamp(a-dmg/f, a*0.2,20)` caps 30/20。

## 8. Next Steps

1. **109 gaps は全閉済み (strict 78/78・deep 31/31・MISSING 80/80)。次の改善ループは「非80項・polish層」**:
   - `docs/MISSING_FEATURES_1_21_4.md` 末尾の polish 群 (Trial Chambers/Pale Garden/Creaking/Bundles 1.21.5 等)
   - `README.md` の "Polish-within-DONE" / `MISSING` 行の `polish:` 注記 (例: #84 hunger exhaustion の vanilla weight 差、#90 Levitation の gravity 簡略化)
   - 新しい監査観点 (例: 1.21.5 互換、datapack/function 網羅、perf) が必要なら `docs/research-prompt.md` を更新して `plan/plan29.md` を生成。
2. ループを回す場合は §3 の手順で `wt29/*` 6 worktree → バックグラウンド並行実装 → `git merge --no-ff` → ドキュメント更新。
3. 不要な旧ブランチ (`wt/*`) は `git branch -D` で削除可。

> このファイル自体が引き継ぎのエントリポイント。次回セッション開始時は本書の §3 のコマンドで worktree を再生成し、`plan/` の最新と `MISSING`/`COMPAT_AUDIT` を照合して再開すること。
