# CURRENT_STATE.md — cpp-fabricmc 動的状態トラッカー

> **位置づけ:** `AGENTS.md` (静的ルール) に対する動的状態ファイル。HEAD・テスト実績・plan 番号・進行中作業・Next Steps を集約。**ループ完了ごとに本ファイルを更新すること。**

最終更新: 2026-09-03 / HEAD `9b26524`

## 1. HEAD

- `9b26524` — `docs: AGENTS.md pkill safety rule (ambiguous pkill c++ killed all c-procs)`
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
| `test_wire_full` | 337 | 26 |
| `test_gameplay_full` | 244 | 9 |
| `test_server_full` | 162 | 32 |
| **計** | — | **67 FAIL** |

## 3. Plan Numbering

- `ls plan/plan*.md | sort -V | tail -1` で最大 X を取得 → 次は `X+1`。
- 現在最大: `plan/plan42.md` (assessment-5 E-series: 90→100 の研究・3 ラウンド)。次は `plan/plan43.md`。
- 生成後は `ls -lh plan/planX.md && wc -l plan/planX.md` で確認。

## 4. 進行中作業

- **plan42 R1 (wire: E-01〜E-08)** — 2 worktree がビルド中:
  - `/tmp/opencode/wt42b/r1-network` (実装 6 パケット)
  - `/tmp/opencode/wt42b/r1-test` (`test_wire_full` 405/0 化)
- この後に R2 (gameplay) → R3 (server) が続く。
- `plan/` フォルダは `.gitignore:10` で一括無視 (`plan/` 1行)。`plan/*.md` は追跡対象外 — `git add -f` 不要。

## 5. Next Steps

1. **assessment-5 (E-01〜E-19) 全 FIXED = 大規模テスト 67 FAIL 全消し = 100 点達成宣言。**
2. その後は次監査 (assessment-6) を検討。
3. Fabric JVM mod は by design 非対応。

## 6. 過去の成果 (簡潔)

- assessment-1 (S 系 wire 78/78) FIXED、assessment-2 (D 系 32/32) FIXED、assessment-3 (B 系 14/14・78→85) FIXED、assessment-4 (C 系 12/12・87→90) FIXED。
- assessment-5 (E 系 19 項目・90→100) 監査完了、plan42 で実装中。
- `plan/` に plan1〜42 が蓄積。旧 worktree は全削除済み (現行の wt42b/* を除く)。
- `docs/MISSING_FEATURES_1_21_4.md` 80/80 DONE、strict 78/78 FIXED、deep 31/31 FIXED、H1 32/32 FIXED。
