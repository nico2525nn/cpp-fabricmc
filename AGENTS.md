# AGENTS.md — cpp-fabricmc Workflow Handover

> **Purpose:** 次セッションのエージェント（人間/サブエージェント）が `cpp-fabricmc` (Fabric 1.21.4 / protocol 769) の改善ループを中断なく引き継ぐための必須情報。`docs/research-prompt.md` が正規手順。**すべてのサブエージェントは muse (`agent: "muse"`) を使用する (グローバル `~/.config/opencode/AGENTS.md` の委任ルール準拠)。**

## 1. Project Goal

- Minecraft Fabric 1.21.4 サーバーを C++ で非公式に完全再実装し、**完全な互換性** (protocol-compatible) を目指す。
- `docs/MISSING_FEATURES_1_21_4.md` の `PARTIAL/TODO` が 0 になるまでループ。`docs/COMPAT_AUDIT_1_21_4_STRICT.md` の厳密Wire監査 (78 gaps) も 0 まで。
- 現状: `plan/` に `plan.md`〜`plan35.md` が蓄積。最新は `plan/plan35.md` (812行, datapack+finish)。`MISSING` は 80/80 DONE、strict 78/78 FIXED、deep 31/31 FIXED + plan30 `H1` `32/32` + plan30-35 wire/機能拡張 (spec_wire 233・smoke 127) — **計109 gaps 全閉 + plan30-35 wire/機能拡張 + plan31 refactor (GameServer.cpp 7843→35 99.5% dispersed, 6 files + Helpers/Stairs/Constants) + plan32 recipes 1578 JSON-driven (assets/data/recipes 1581) + 30+ vanilla commands (execute modifiers/data/clone/loot/place/locate/spreadplayers + ban/op/whitelist + enchant/attribute/trigger + advancement/recipe/item/me/msg) + plan33 WorldGen (Density 7型・MultiNoise isosceles・Structures 20) + plan34 Mob AI 10種/Breeze/Armadillo + 未送信6パケット (ActionBar 0x51/ServerData 0x50/HurtAnimation 0x25/EntitySoundEffect 0x6E/ChatSuggestions 0x18/SyncEntityPosition 0x20) + plan35 datapack (advancements story 20 + loot functions + predicate 8 + server.properties) (README What works 更新)**。次は `plan/plan36.md` (任意継続) へ。

## 2. Current State (2026-09-01 確認 — plan35 完遂 HEAD 9cba7f4)

- **HEAD:** `9cba7f4` (plan35 完遂: advancements story 20 + loot functions + predicate 8 + server.properties pvp/flight/hardcore/max-players + test 127 — plan32 539cd20 recipes 1578 + plan33 WorldGen Density 7型/MultiNoise isosceles/Structures 20/trial_chambers salt 94251327 + plan34 Mob AI 10種/Breeze/Armadillo + 未送信6パケット ActionBar 0x51/ServerData 0x50/HurtAnimation 0x25/EntitySoundEffect 0x6E/ChatSuggestions 0x18/SyncEntityPosition 0x20 + Fuzz 23/Soak 60s/弱検査撤廃 15/16)。plan31 は `36611b7` で完 (GameServer.cpp 7843→35 99.5% dispersed, 6 files 7714 + Helpers/Stairs/Constants, total src 40124)。**worktree は全て削除済み** (`git worktree list` は main のみ)。
- **Tests:** `cmake -B build -G Ninja && cmake --build build -j4` green。`./build/test_native ./build/cppfm` **ALL PASS (12+31+10)**。`./build/test_scoreboard_reset` **22 PASS 0 FAIL** (ctest `scoreboard_reset`, TIMEOUT 30)。`./build/test_spec_wire` **233 PASS 0 FAIL 0 SKIP** (ctest `spec_wire` TIMEOUT 60, H1 32/32 + plan34-35 6パケット/predicate 8 含む)。`./build/test_fuzz` **23 PASS 0 FAIL** (ctest `fuzz`, 23 cases)。`./build/test_smoke_80` は **127 PASS 0 FAIL**・exit 0・約7分 (450s, 600s under load, `=== SMOKE 80: 127 PASS 0 FAIL ===`) — plan32-35 拡張後 69→127 へ拡張、wire 非破壊 (spec 233 維持)。
- **Docs:** `README.md` は plan35 What works (advancements story 20・loot functions・predicate・server.properties) + Testing (spec 233/fuzz 23/smoke 127) 更新済み。`docs/COMPAT_DEEP_AUDIT.md` は H1 FIXED + plan31 split 追記済み、`docs/PROTOCOL_NOTES.md` は UpdateAttributes 0x7C + plan34 6パケット追記済み。`AGENTS.md` は 本State + §4 + §8 を plan35 完遂に更新。`docs/research-prompt.md` は `plan/` 一括無視のまま。
- **Git:** `plan/` フォルダは `.gitignore:10` で一括無視 (`plan/` 1行)。`plan/*.md` は追跡対象外。今後も `git add -f` 不要で無視される。
- **旧ブランチ:** `wt/*` 30+ が残存 (worktree 無し・削除しても可)。`wt31/refactor` / `wt32/*` 6-way / `wt33/worldgen` / `wt34/*` / `wt35/*` は main にマージ済み・削除済み (worktree なし)。

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
- 現在最大: `plan/plan35.md` (812行, datapack+finish)。次は `plan/plan36.md` (任意継続時)。
- 生成後は `ls -lh plan/planX.md && wc -l plan/planX.md` で確認。

## 5. Testing (Evidence)

```bash
timeout --foreground --kill-after=5 120 cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
timeout 300 cmake --build build -j4          # -j2 if OOM on /tmp tmpfs
timeout --foreground --kill-after=5 60 ./build/test_native ./build/cppfm          # ALL PASS (12+31+10) expected (50s)
timeout --foreground --kill-after=5 30 ./build/test_scoreboard_reset               # 22/22 PASS expected (ctest scoreboard_reset TIMEOUT 30)
timeout --foreground --kill-after=5 60 ./build/test_spec_wire                     # 233 PASS 0 FAIL 0 SKIP (wire byte-identical lock, H1 32/32 + plan34-35 6パケット/predicate 8)
timeout --foreground --kill-after=5 30 ./build/test_fuzz                          # 23 PASS 0 FAIL (fuzz 23 cases, TIMEOUT 30)
timeout --foreground --kill-after=5 450 ./build/test_smoke_80 ./build/cppfm        # 127 PASS 0 FAIL (80-row taxonomy + plan32-35 拡張) — 450s (600s under load), strict 78/78 + deep 31/31 (+ H1 32/32) FIXED (計109 gaps 全閉 + plan30-35 wire)
pkill -9 -f "cppfm --port" 2>/dev/null; sleep 1
ctest -R "native|scoreboard_reset|spec_wire|fuzz" --output-on-failure --timeout 60
ctest -R smoke80 --output-on-failure --timeout 450   # 600 under load
```

`test_native` の `2 FAIL` (spawn-protection 0,-61,0) は `30,-61,0` 修正で解消済み (`a1dba28`)。現 HEAD (`9cba7f4`, plan35 完遂) で `test_native` ALL PASS (12+31+10) + `test_scoreboard_reset` 22/22 + `test_spec_wire` 233 PASS 0 FAIL + `test_fuzz` 23 PASS + `test_smoke_80` 127 PASS 0 FAIL ALL PASS 確認済み。

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

1. **100点化ループ plan32-35 完遂 (HEAD 9cba7f4) — 評価 73/100 → 約90/100 (A 38-39/40・B 26-27/30・C 13-14/15・D 14/15, score_review.py 100/100フレーム)。109 gaps 全閉 (strict 78/78・deep 31/31・MISSING 80/80) + plan30-35 wire/機能拡張 (spec_wire 233・smoke 127・fuzz 23・Soak 60s・弱検査撤廃15/16)**:
   - plan32: recipes 1578 JSON-driven (assets/data/recipes 1581) + 30+ vanilla commands (execute modifiers/data/clone/loot/place/locate/spreadplayers + ban/op/whitelist + enchant/attribute/trigger + advancement/recipe/item/me/msg) — wire 非破壊。
   - plan33: WorldGen parity — DensityFunction 7型 (Beardifier/OldBlended/Blend*/EndIslands/WeirdScaled/ShiftA/B) + MultiNoise isosceles + Structures 20セット (trial_chambers salt 94251327 修正) + /locate 20セット。
   - plan34: Mob AI 10種差別化 + Breeze/Armadillo + 未送信6パケット実装 (ActionBar 0x51・ServerData 0x50・HurtAnimation 0x25・EntitySoundEffect 0x6E・ChatSuggestions 0x18・SyncEntityPosition 0x20) + Fuzz 23 + Soak 60s + 弱検査撤廃 15/16。
   - plan35: datapack finish — advancements story 20 + loot functions (set_count/looting_enchant/… ) + predicate 8種評価 + server.properties (pvp/flight/hardcore/max-players) + spec 120→233 / smoke 69→127 拡張 + score_review.py 100/100。
   - 残ギャップ (honest): `Bundles` 1.21.5 (proto 776 時再設計, 769 experimental のため deferred) / Mob AI 139種の data-driven 網羅 (10種済み, 残りは BehaviorTree JSON 追加) / 構造物ピースの簡略 (jigsaw 12-variant 止まり) / perf (chunkCache 1024 LRU, async I/O 未導入) / Fabric JVM mod は by design 非対応。
2. ループを回す場合は §3 の手順で `wt36/*` 6 worktree → バックグラウンド並行実装 → `git merge --no-ff` → ドキュメント更新。**plan36 候補:** Mob AI 追加差別化 (10→30種) / 構造物ピース拡張 (20→40) / Bundles 以外の polish (BossBar lerp は client-side のため現状維持) / perf (async Anvil, chunkCache 調整) — plan36 は通常の6 worktree並行に戻す。
3. 不要な旧ブランチは削除済み: `wt29/*` `wt30/*` `wt31/refactor` `wt32/*` `wt33/worldgen` `wt34/*` `wt35/*` は main にマージ後削除済み (worktree なし)。残存は古い `wt/*` (wt6-wt28 等) のみで `git branch -D` で削除可 (main には影響なし)。

> このファイル自体が引き継ぎのエントリポイント。次回セッション開始時は本書の §3 のコマンドで worktree を再生成し、`plan/` の最新と `MISSING`/`COMPAT_AUDIT` を照合して再開すること。サブエージェントはすべて muse を使うこと。
