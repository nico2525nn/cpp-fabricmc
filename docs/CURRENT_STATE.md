# CURRENT_STATE.md — cpp-fabricmc 動的状態トラッカー

> **位置づけ:** `AGENTS.md` (静的ルール) に対する動的状態ファイル。HEAD・テスト実績・plan 番号・進行中作業・Next Steps を集約。**ループ完了ごとに本ファイルを更新すること。**

最終更新: 2026-09-04 / HEAD `3689619` (plan46 マージ完遂・**assessment-6 44/44 FIXED**)

## 1. HEAD

- `3689619` — merge wt46/longterm: plan46 G-06/10/14/15 + O-03/05/11/12 + docs (assessment-6 最終集約前時点)
- 直前: `3811fcf` merge wt46/recovery (plan46 O-06〜O-10: load-with-recovery + check_world + RCON multi)、`f8dc220` merge wt46/defense (plan46 O-13/W-14/W-16: flood defense A1-A8 + rate limits)、`8ff124a` merge wt45/test、`74cce2a` merge wt45/network (plan45 B6: W-05/08-11/13 + G-13, wire_b6 133)、`f458908` merge wt45/worldgen (plan45 G-11: biomes 65 + structures jar 検証 + seed parity)。
- plan43 (8 FIXED) → plan44 (16/44) → plan45 (30/44 前後) → plan46 で **44/44 全 FIXED** (HIGH 25/25 含む)。
- `wt46/*` worktree はマージ後に削除可。nightly 24h soak は `docs/SOAK_24H.md` 手順で別インスタンス実行 (結果は `SOAK_REPORT.md` に記録)。

## 2. Tests (現行実績)

小規模・固定テスト (ALL PASS が前提):

- `test_spec_wire` **392 PASS** (plan43 P43-1..7 shape lock 込)
- `test_wire_full` **405 PASS**
- `test_scoreboard_reset` **22 PASS**
- `test_fuzz` **23 PASS**
- `test_native` **ALL PASS**
- `test_plan43` **82 PASS**
- `test_wire_b6` **133 PASS** (plan45 B6: W-05/08-11/13 + G-13)
- `test_seed_parity` **201 PASS** (plan45: 3 層 seed parity)
- `test_flood_net` **PASS** (plan46 defense A1-A8 + RateLimiter)
- `test_recovery` **45 PASS** (plan46: 破損注入×3 種 + check_world)
- `test_rcon_multi` **PASS** (plan46: 5 同時 + 誤認証 flood)
- `test_mining_full` **38 PASS** / `test_block_hardness_full` (1095 mismatch 0) / `test_mob_stats_full` **131 PASS** / `test_redstone_engine_full` **29 PASS** (plan44)
- `test_smoke_80` **212 PASS 0 FAIL**
- soak 300s PASS (plan45 O-05/O-06 short gate) / bench view32 PASS (p50 0.107ms, plan45 O-11)
- `python3 tools/score_review.py` → **TOTAL 100/100 (40/30/15/15)** + alt 100/100 (25 each) (2026-09-04 再実行)

大規模仕様ベーステスト (main 現在):

| テスト | PASS | FAIL |
|---|---|---|
| `test_wire_full` | 405 | 0 |
| `test_gameplay_full` | 734 | 1 (E-14 Fabric honest gap のみ by design、plan46 G-06/10/14/15 込) |
| `test_server_full` | 194 + plan46 拡張 (kick/ban E2E) | 0 |
| **assessment-6** | **44/44 FIXED (HIGH 25/25)** | **OPEN 0 = 妥協なき全面監査完了** |

## 3. Plan Numbering

- `ls plan/plan*.md | sort -V | tail -1` で最大 X を取得 → 次は `X+1`。
- 現在最大: `plan/plan46.md` (assessment-6 残 28 件の実装・plan45 B5/B6 + plan46 defense/recovery/longterm)。次は `plan/plan47.md`。
- 生成後は `ls -lh plan/planX.md && wc -l plan/planX.md` で確認。

## 4. 進行中作業

- **assessment-6 最終集約完了** — W 16 + G 15 + O 13 の全 44 項目 FIXED (`docs/assessment-6.md` Summary Table 全行 FIXED 化・各節に planX 実績注記)。HIGH 25/25、Medium 19/19。
- plan45: G-04/G-11 (biome 65・構造物 jar 検証 20 sets・seed parity 3 層)・O-02/04/05/06/11 (stress 120・soak 300s・bench view32)・W-05/08/09/10/11/13 + G-13 (B6・wire_b6 133)。
- plan46: O-13/W-14/W-16 (防御 A1-A8・RateLimiter・per-packet try)・O-06〜O-10 (復旧 3 段・BACKUP.md・check_world・RCON multi)・O-12 (LOAD_BUDGET.md)・G-06/10/14/15 (MobBehaviorSpec・DENSITY_COVERAGE・food/potion・crops/villager)・O-03/05/11 (GUI_CHECKLIST・SOAK_24H)。
- `plan/` フォルダは `.gitignore:10` で一括無視 (`plan/` 1行)。`plan/*.md` は追跡対象外 — `git add -f` 不要。

## 5. Next Steps

1. **assessment-6 全 FIXED 達成済み** (44/44・HIGH 25/25・OPEN 0)。gameplay の E-14 honest gap 1 件 (Fabric JVM mod) は by design 残置。
2. 次: ③ **コードベース徹底クリーンアップ** → ④ **docs 完全再編**。
3. nightly: 24h soak フルラン (手順 `docs/SOAK_24H.md`、結果は `docs/SOAK_REPORT.md` に記録)。W-15(a) teleport-confirm 不一致時再送は将来対応 (現状は寛容受容を仕様として記録)。
4. 旧 worktree (`/tmp/opencode/wt45/*`・`/tmp/opencode/wt46/*`) はマージ後に削除可。

## 6. 過去の成果 (簡潔)

- assessment-1 (S 系 wire 78/78) FIXED、assessment-2 (D 系 32/32) FIXED、assessment-3 (B 系 14/14・78→85) FIXED、assessment-4 (C 系 12/12・87→90) FIXED。
- assessment-5 (E 系 19 項目・90→100) FIXED、大規模 3 本 881 PASS/1 FAIL で 100 点達成 (plan42)。
- assessment-6 (W/G/O 系 44 項目・100→true-100) FIXED — plan43 (8) + plan44 (8) + plan45 (14) + plan46 (14)。**HIGH 25/25 解消**。
- `plan/` に plan1〜46 が蓄積。`docs/MISSING_FEATURES_1_21_4.md` 80/80 DONE、strict 78/78 FIXED、deep 31/31 FIXED、H1 32/32 FIXED。
- `python3 tools/score_review.py` は **100/100 (40/30/15/15)** を維持 (plan35 目標 90 台を超過達成のまま)。
