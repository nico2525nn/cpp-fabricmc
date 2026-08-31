# AGENTS.md — cpp-fabricmc Workflow Handover

> **Purpose:** 次セッションのエージェント（人間/サブエージェント）が `cpp-fabricmc` (Fabric 1.21.4 / protocol 769) の改善ループを中断なく引き継ぐための必須情報。`docs/research-prompt.md` が正規手順。**すべてのサブエージェントは muse (`agent: "muse"`) を使用する (グローバル `~/.config/opencode/AGENTS.md` の委任ルール準拠)。**

## 1. Project Goal

- Minecraft Fabric 1.21.4 サーバーを C++ で非公式に完全再実装し、**完全な互換性** (protocol-compatible) を目指す。
- `docs/MISSING_FEATURES_1_21_4.md` の `PARTIAL/TODO` が 0 になるまでループ。`docs/COMPAT_AUDIT_1_21_4_STRICT.md` の厳密Wire監査 (78 gaps) も 0 まで。
- 現状: `plan/` に `plan.md`〜`plan30.md` が蓄積。最新は `plan/plan30.md` (131 toClient wire 検証 10章 — 120 `test_spec_wire` cases)。`MISSING` は 80/80 DONE、strict 78/78 FIXED、deep 31/31 FIXED + plan30 `H1 UpdateAttributes 0x7C` varint mapper `32/32` — **計109 gaps 全閉 + plan30 wire lock** + plan29 polish 層マージ済み (README 参照)。次は `plan/plan31.md` リファクタ (本ファイル)。

## 2. Current State (2026-08-31 確認 — plan30 完遂)

- **HEAD:** `fc0e43e` (`wt30/*` 3 worktree マージ完: plan30 entity `H1 UpdateAttributes varint mapper + uuid string 36` + network `H3/M3/M4/M6/M7` + test `test_spec_wire 120 PASS` — 25+ wire cases bit-level lock) → plan30 マージ完 (H1 是正 `Attributes.hpp:224-250` count 22 + MAX_HEALTH mapper 16 + detector `third>0x15` fix)。plan24〜28 で deep audit 31/31 FIXED、plan29 polish 10章 + plan30 wire 10章 完了。**worktree は全て削除済み** (`git worktree list` は main のみ、`/tmp/opencode/wt31/refactor` 単一)。
- **Tests:** `cmake -B build -G Ninja && cmake --build build -j4` green。`./build/test_native ./build/cppfm` **ALL PASS (12/12)**。`./build/test_scoreboard_reset` **22 PASS 0 FAIL** (ctest `scoreboard_reset`, TIMEOUT 30, ResetScore `0x49` round-trip/wildcard/copy-before-erase)。`./build/test_spec_wire` **120 PASS 0 FAIL 0 SKIP** (ctest `spec_wire` TIMEOUT 60, `[C2] UpdateAttributes 0x7C` H1 varint mapper 0-21 + count 22 + MAX_HEALTH 16 lock)。`./build/test_smoke_80` は **69 PASS 0 FAIL**・exit 0・約7分 (450s 以内、省マシン負荷時は 600s 推奨、`=== SMOKE 80: 69 PASS 0 FAIL ===`、80-row taxonomy の各項目をカバー)。
- **Docs:** `README.md` は strict 78/78 + deep 31/31 (+ plan30 H1 → 32/32) = 109 gaps closed + `test_spec_wire 120 PASS` に更新済み。`docs/COMPAT_DEEP_AUDIT.md` は H1 UpdateAttributes varint mapper FIXED 追記、`docs/PROTOCOL_NOTES.md` は UpdateAttributes 0x7C wire 追記済み。`docs/research-prompt.md` は `plan/` 一括無視のまま。
- **Git:** `plan/` フォルダは `.gitignore:10` で一括無視 (`plan/` 1行)。`plan/*.md` は追跡対象外。今後も `git add -f` 不要で無視される。
- **旧ブランチ:** `wt/blocktick` `wt/command` `wt/datapack` `wt/entity` `wt/inventory` `wt/light` `wt/network` `wt/placement` `wt/portal` が残存 (worktree 無し・削除しても可)。`wt30/*` は削除済み。

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
   - `docs/COMPAT_AUDIT_1_21_4_STRICT.md` の厳密Wire監査を更新 (file:line 絶対パス)
   - `README.md` の What works / Testing — evidence を更新 (80 taxonomy vs 78 strict の数値を明確に)
   - `docs/PROTOCOL_NOTES.md` の新パケット (Bundle/MultiBlockChange/UpdateLight) を追記
   - `docs/MIGRATION_GUIDE.md` の Module map を更新

6. MISSING の内容がすべて DONE になるまでループ (厳密監査も 0 まで)。
```

**禁止事項:**
- サブエージェントは **必ず muse** (`agent: "muse"`) を使う。`general` は使わない。
- `background: false` (デフォルト) でサブエージェントを起動しない。必ず `background: true`。
- 同一worktreeで2つのエージェントを同時に動かさない。
- 研究が終わる前に実装を開始しない。
- `plan/*.md` を `git add -f` で追跡しない (無視のまま)。

## 4. Plan Numbering

- `ls plan/plan*.md | sort -V | tail -1` で最大Xを取得 → 次は `X+1`。
- 現在最大: `plan/plan30.md` (131 toClient wire 検証 10章 + 付録A/B/C — `test_spec_wire` 設計)。次は `plan/plan31.md` (1135行, リファクタ計画 — 本ファイル)。
- 生成後は `ls -lh plan/planX.md && wc -l plan/planX.md` で確認。

## 5. Testing (Evidence)

```bash
timeout --foreground --kill-after=5 120 cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
timeout 300 cmake --build build -j4          # -j2 if OOM on /tmp tmpfs
timeout --foreground --kill-after=5 60 ./build/test_native ./build/cppfm          # 12/12 PASS expected (50s)
timeout --foreground --kill-after=5 30 ./build/test_scoreboard_reset               # 22/22 PASS expected (ctest scoreboard_reset TIMEOUT 30)
timeout --foreground --kill-after=5 60 ./build/test_spec_wire                     # 120 PASS 0 FAIL 0 SKIP (wire byte-identical lock, H1 32/32)
timeout --foreground --kill-after=5 450 ./build/test_smoke_80 ./build/cppfm        # 69 PASS 0 FAIL (80-row taxonomy covering 69 checks) — 450s (600s under load), strict 78/78 + deep 31/31 (+ H1 32/32) FIXED (計109 gaps 全閉 + plan30 wire)
pkill -9 -f "cppfm --port" 2>/dev/null; sleep 1
ctest -R "native|scoreboard_reset|spec_wire" --output-on-failure --timeout 60
ctest -R smoke80 --output-on-failure --timeout 450   # 600 under load
```

`test_native` の `2 FAIL` (spawn-protection 0,-61,0) は `30,-61,0` 修正で解消済み (`a1dba28`)。現 HEAD (`fc0e43e`, plan30 完遂) で `test_native` 12/12 + `test_scoreboard_reset` 22/22 + `test_spec_wire` 120 PASS 0 FAIL + `test_smoke_80` 69 PASS 0 FAIL ALL PASS 確認済み。

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
- `Bundle` axis `lx<<8|lz<<4|ly` (vanilla `state<<12|x<<8|z<<4|y`; NOT `ly<<8|lz<<4|lx` — x/y swap was plan28 finish fix, see COMPAT_AUDIT N7).
- `SlotComponent` ids `damage 3 repair_cost 17 trim 45` (not 6/7/42)。
- `DamageCalculator` armor `f=2+t/4 g=clamp(a-dmg/f, a*0.2,20)` caps 30/20。

## 8. Next Steps

1. **109 gaps は全閉済み (strict 78/78・deep 31/31・MISSING 80/80) + plan30 wire lock (H1 UpdateAttributes 0x7C varint mapper 32/32 + test_spec_wire 120 PASS) — plan29 polish 層 (`29abd26`) + plan30 wire 層 (`fc0e43e`: 3 worktree, H1 是正 + 120 wire cases) まで完遂。次は `plan/plan31.md` リファクタ (本 worktree `wt31/refactor` 単一エージェントで遂行中)**:
   - plan31: 大規模リファクタ — `GameServer.cpp` 8092→470 分割 (6ファイル) + デッドコード除去 + 重複共通化 + 定数集約 (wire 不変、plan31.md §3 フェーズ Phase0→1→2→3→4→5→6)。機能追加・ビヘイビア変更なし。
   - 残った polish: `Bundles` 1.21.5 / proto 776 時再設計 (§4 見送り)、boat buoyancy / ghost preview throttle 現状維持 (§10/§9 検証済み)、`BossBar` lerp はクライアント側 (§8 検証済み・修正不要)
   - 新しい監査観点 (例: 1.21.5 互換、datapack/function 網羅、perf) は plan31 リファクタ後に `docs/research-prompt.md` 更新 → `plan/plan32.md` で。
2. ループを回す場合は §3 の手順で `wt31/refactor` 単一 worktree (`/tmp/opencode/wt31/refactor` ブランチ `wt31/refactor`) で直列フェーズ実行 → `git log --oneline` Phase0a→Phase6 → docs 更新。**並行 worktree は使わない (plan31 は単一エージェント)**。
3. 不要な旧ブランチ (`wt/*`, `wt30/*`) は `git branch -D` で削除可。

> このファイル自体が引き継ぎのエントリポイント。次回セッション開始時は本書の §3 のコマンドで worktree を再生成し、`plan/` の最新と `MISSING`/`COMPAT_AUDIT` を照合して再開すること。サブエージェントはすべて muse を使うこと。
