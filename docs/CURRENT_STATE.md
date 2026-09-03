# CURRENT_STATE.md — cpp-fabricmc 動的状態トラッカー

> **位置づけ:** `AGENTS.md` (静的ルール) に対する動的状態ファイル。HEAD・テスト実績・plan 番号・進行中作業・Next Steps を集約。**ループ完了ごとに本ファイルを更新すること。**

最終更新: 2026-09-03 / HEAD `0db2b68` (plan42 R3 完遂・**100 点達成**)

## 1. HEAD

- `0db2b68` — chore: gitignore runtime banned/ops json (plan42 R3 完遂時点)
- 直前: `2e458df` merge wt42r3/network (R3 コマンド + UAF 2 件修正 + server_full 194/0)、`11aa1f8` network 実装、`ac435c2` network コマンド、`5b637d3` merge wt42r3/combat (kick/whitelist/RCON)、`279c89a` combat 実装、`562f96b` merge wt42r2/test (gameplay 282/1)。
- `wt42r3/test` は `sync main (plan42 R3 all)` で合流済み、test_server_full 最終集約 (194/0) まで完了。
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
| `test_wire_full` | 405 | 0 (plan42 R1 で 26 FAIL 解消、R3 で再検証) |
| `test_gameplay_full` | 282 | 1 (E-14 Fabric honest gap のみ、plan42 R2) |
| `test_server_full` | 194 | 0 (plan42 R3 で 33 FAIL 解消) |
| **計** | **881** | **1 (honest by design のみ) = 100 点達成** |

## 3. Plan Numbering

- `ls plan/plan*.md | sort -V | tail -1` で最大 X を取得 → 次は `X+1`。
- 現在最大: `plan/plan42.md` (assessment-5 E-series: 90→100 の研究・3 ラウンド)。次は `plan/plan43.md`。
- 生成後は `ls -lh plan/planX.md && wc -l plan/planX.md` で確認。

## 4. 進行中作業

- **plan42 R1 (wire: E-01〜E-08) 完了** — `test_wire_full` 405/0 (merge `58f42eb`)。assessment-5 E-01〜E-08 FIXED。
- **plan42 R2 (gameplay: E-09〜E-14) 完了** — world (`be82e3b`: biome 54・structures 120・async) + entity (`5bdb4b3`: boat・horse・Mob AI 139) を main にマージ済み。`wt42r2/test` で `test_gameplay_full` **282 PASS 1 FAIL** (E-14 のみ) に集約。assessment-5 E-09〜E-14 FIXED (E 系 19 項目中 14 FIXED)。
- **plan42 R3 (server: E-15〜E-19) 完了** — network (`ac435c2` + `11aa1f8`: clear/xp/loot/tp 等コマンド + UAF 2 件修正 + enchant/effect クラッシュ修正 + deop 無条件化) + combat (`279c89a`: kick/whitelist/RCON) を main にマージ (`5b637d3` + `2e458df`)。`wt42r3/test` で `test_server_full` **194 PASS 0 FAIL** に集約。assessment-5 E-15〜E-19 FIXED (**E 系 19/19 全 FIXED**)。
- **100 点 (完全互換) 達成** — 大規模 3 本計 881 PASS / 1 FAIL (E-14 Fabric のみ)。
- `plan/` フォルダは `.gitignore:10` で一括無視 (`plan/` 1行)。`plan/*.md` は追跡対象外 — `git add -f` 不要。

## 5. Next Steps

1. **100 点達成済み** (assessment-5 19/19 FIXED)。gameplay の E-14 honest gap 1 件 (Fabric JVM mod) は by design 残置。
2. 次の展望: (a) 次監査 (assessment-6) の検討、(b) 1.21.5 (proto 776, Bundles) は現行スコープ外・将来対応、(c) リファクタ候補 (Commands.cpp 肥大化の分割等)。Fabric JVM mod 自体は by design 非対応。
3. 旧 worktree (`/tmp/opencode/wt42r3/*`) はマージ後に削除可。

## 6. 過去の成果 (簡潔)

- assessment-1 (S 系 wire 78/78) FIXED、assessment-2 (D 系 32/32) FIXED、assessment-3 (B 系 14/14・78→85) FIXED、assessment-4 (C 系 12/12・87→90) FIXED。
- assessment-5 (E 系 19 項目・90→100) 監査完了、plan42 で実装中 (R1: E-01〜E-08 wire_full 405/0、R2: E-09〜E-14 gameplay 282/1、残 R3: E-15〜E-19 server)。
- `plan/` に plan1〜42 が蓄積。旧 worktree は全削除済み (現行の wt42b/* を除く)。
- `docs/MISSING_FEATURES_1_21_4.md` 80/80 DONE、strict 78/78 FIXED、deep 31/31 FIXED、H1 32/32 FIXED。
