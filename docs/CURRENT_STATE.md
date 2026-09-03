# CURRENT_STATE.md — cpp-fabricmc 動的状態トラッカー

> **位置づけ:** `AGENTS.md` (静的ルール) に対する動的状態ファイル。HEAD・テスト実績・plan 番号・進行中作業・Next Steps を集約。**ループ完了ごとに本ファイルを更新すること。**

最終更新: 2026-09-03 / HEAD `5bdb4b3` (+ wt42r2/test 統合中)

## 1. HEAD

- `5bdb4b3` — merge wt42r2/entity: plan42 R2 boat + horse + mob AI 139 (assets/entities 149)
- 直前: `be82e3b` merge wt42r2/world: plan42 R2 biomes 54 + structures 120 + async io、`90b27b6` entity 実装、`9e19da3` world 実装、`58f42eb` merge wt42b/r1-test (wire_full 405/0)。
- `wt42r2/test` は `44a3832 sync main` で合流済み、test_gameplay_full 最終集約 (282/1) まで完了。
- 直前: `be21979` merge wt42/crashfix (setblock string_view UAF + varint UB + harness 修正)、`747767e` docs: assessment-5 追加 (E-series, 90→100 exact-parity audit)、`7ca5bf8` merge wt41/test (plan41 C-12 + assessment-4 12/12, 90 到達)。

## 2. Tests (現行実績)

小規模・固定テスト (ALL PASS が前提):

- `test_spec_wire` **328 PASS**
- `test_scoreboard_reset` **22 PASS**
- `test_fuzz` **23 PASS**
- `test_native` **ALL PASS**
- `test_recipes_mirror` **76 PASS**
- bench PASS (p50 0.1ms)
- `test_smoke_80` **212 PASS 0 FAIL**

大規模仕様ベーステスト (main 現在 — **全 PASS = 完全互換 100 点の定義**):

| テスト | PASS | FAIL |
|---|---|---|
| `test_wire_full` | 405 | 0 (plan42 R1 で 26 FAIL 解消) |
| `test_gameplay_full` | 282 | 1 (E-14 Fabric honest gap のみ、plan42 R2) |
| `test_server_full` | 162 | 32 (R3 対象: E-15〜E-19) |
| **計** | — | **33 FAIL (67→33、うち honest 1)** |

## 3. Plan Numbering

- `ls plan/plan*.md | sort -V | tail -1` で最大 X を取得 → 次は `X+1`。
- 現在最大: `plan/plan42.md` (assessment-5 E-series: 90→100 の研究・3 ラウンド)。次は `plan/plan43.md`。
- 生成後は `ls -lh plan/planX.md && wc -l plan/planX.md` で確認。

## 4. 進行中作業

- **plan42 R1 (wire: E-01〜E-08) 完了** — `test_wire_full` 405/0 (merge `58f42eb`)。assessment-5 E-01〜E-08 FIXED。
- **plan42 R2 (gameplay: E-09〜E-14) 完了** — world (`be82e3b`: biome 54・structures 120・async) + entity (`5bdb4b3`: boat・horse・Mob AI 139) を main にマージ済み。`wt42r2/test` で `test_gameplay_full` **282 PASS 1 FAIL** (E-14 のみ) に集約。assessment-5 E-09〜E-14 FIXED (E 系 19 項目中 14 FIXED)。
- **次: plan42 R3 (server: E-15〜E-19)** — `test_server_full` 162/32 の 32 FAIL 解消 (clear/xp/loot/tp 等コマンド + kick/whitelist/RCON)。
- `plan/` フォルダは `.gitignore:10` で一括無視 (`plan/` 1行)。`plan/*.md` は追跡対象外 — `git add -f` 不要。

## 5. Next Steps

1. **assessment-5 残 5 (E-15〜E-19, R3/server) を FIXED = server_full 32 FAIL 全消し = 100 点達成宣言** (gameplay は E-14 honest gap 1 を除き達成済み 282/283)。
2. その後は次監査 (assessment-6) を検討。
3. Fabric JVM mod は by design 非対応。

## 6. 過去の成果 (簡潔)

- assessment-1 (S 系 wire 78/78) FIXED、assessment-2 (D 系 32/32) FIXED、assessment-3 (B 系 14/14・78→85) FIXED、assessment-4 (C 系 12/12・87→90) FIXED。
- assessment-5 (E 系 19 項目・90→100) 監査完了、plan42 で実装中 (R1: E-01〜E-08 wire_full 405/0、R2: E-09〜E-14 gameplay 282/1、残 R3: E-15〜E-19 server)。
- `plan/` に plan1〜42 が蓄積。旧 worktree は全削除済み (現行の wt42b/* を除く)。
- `docs/MISSING_FEATURES_1_21_4.md` 80/80 DONE、strict 78/78 FIXED、deep 31/31 FIXED、H1 32/32 FIXED。
