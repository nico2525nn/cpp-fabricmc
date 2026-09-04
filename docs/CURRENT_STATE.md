# CURRENT_STATE.md — cpp-fabricmc dynamic state tracker

> This is the stable, current tracker for the plan48 documentation migration. It
> records the repository state at the start of the state-gap-fixture phase and the
> evidence that is actually current. It is not a release sign-off, and it does not
> promote a historical PASS or a declared limitation to a green final gate.

## 1. Snapshot

| field | value |
|---|---|
| `updated` | `2026-09-04` |
| `parent_head` | `5f82ac0b4448f76f98753d18c83bbcd9736da61c` |
| `parent_head_short` | `5f82ac0` |
| `plan` | `plan48` |
| `phase` | `state-gap-fixture` |
| `phase_status` | `ACTIVE` |
| `publication_status` | `BLOCKED` |
| `runtime_reference_snapshot` | `f5987c585e81afdd78adb3ad818a0e43b1697bbe` |

`parent_head` is the HEAD before this commit. The runtime reference snapshot is
the plan48 canonical-document source snapshot; the parent HEAD additionally contains
the plan48 archive, canonical-document, and reference-path migration commits. This
phase adds only the two stable trackers and the stable CSV fixture.

## 2. Plan48 documentation migration

| phase | state at `parent_head` | state after this phase |
|---|---|---|
| plan48 archive | `DONE` — legacy material is under `docs-legacy/` | unchanged |
| plan48 canonical specifications | `DONE` — `docs/README.md`, `SPEC_*.md`, `DEVELOPMENT.md`, `VERIFICATION.md`, and `audit/README.md` exist | unchanged |
| plan48 reference-path updates | `DONE` — callers point at canonical documentation | unchanged |
| plan48 state-gap-fixture | `ACTIVE` — `docs/CURRENT_STATE.md`, `docs/MISSING_FEATURES_1_21_4.md`, and `docs/mob_stats_149.csv` were missing at entry | restore stable files and verify them |

The archive is read-only for this phase. No file under `docs-legacy/`, and no source,
test, assertion, README, or compatibility stub, is part of this change.

## 3. Plan47 residual cleanup

Plan47 cleanup is merged, but its cleanup audit is not equivalent to a complete
vanilla-parity or release gate. The following residuals remain explicitly open:

| residual | status | reason / next owner |
|---|---|---|
| `Structures.hpp` legacy table | `DECLARED-LIMITATION` | runtime and tests still consume it; deleting it would change behavior rather than perform a safe cleanup |
| session mining versus `MiningCalculator` | `DECLARED-LIMITATION` | the two paths have measured semantic differences; unifying them requires a gameplay plan, not a cleanup-only edit |
| `MobBehaviorSpec` coverage | `PARTIAL` | the table is only partially wired to live behavior; broader species parity remains future work |
| retained marker/comment inventory | `HISTORICAL` | plan47 triage kept wire-justified and historical comments; a zero-text-marker result is not evidence of zero limitations |

Plan47’s behavior-preserving claims are retained as historical evidence below. They
do not close these residuals or make the current publication status green.

## 4. Evidence ledger

### 4.1 `HISTORICAL` baseline evidence

The following numbers were recorded by the plan47/plan48 canonical work before this
state tracker was restored. They are retained for comparison only; they are not fresh
results for `parent_head` and do not override the failures in §4.2.

| target | recorded result | evidence_status |
|---|---|---|
| `test_spec_wire` | `392 PASS 0 FAIL 0 SKIP` | `HISTORICAL` |
| `test_wire_full` | `405 PASS 0 FAIL 0 SKIP` | `HISTORICAL` |
| `test_wire_b6` | `133 PASS 0 FAIL` | `HISTORICAL` |
| `test_scoreboard_reset` | `22 PASS 0 FAIL` | `HISTORICAL` |
| `test_fuzz` | `23 PASS 0 FAIL` | `HISTORICAL` |
| `test_native` | all displayed checks passed; no stable aggregate total | `HISTORICAL` |
| `test_smoke_80` | `212 PASS 0 FAIL` | `HISTORICAL` |
| `test_gameplay_full` | `734 PASS 1 FAIL / 735`; the one failure was E-14 | `HISTORICAL` |
| `test_seed_parity` | `201 PASS 0 FAIL` for L1/L2; L3 is not proven | `HISTORICAL` |
| `test_mob_stats_full` | `131 PASS 0 FAIL` | `HISTORICAL` |
| `test_redstone_engine_full` | `29 PASS 0 FAIL` | `HISTORICAL` |
| `test_recovery` | `45 PASS 0 FAIL` | `HISTORICAL` |
| view-distance-32 dry benchmark | `PASS`; 4,225 chunks, p50 0.108 ms, p95 2.332 ms | `HISTORICAL` |
| 120-client stress | `120/120 joined` | `HISTORICAL` |

The historical assessment-6 result was `44/44 FIXED` (`HIGH 25/25`). That is an
archived baseline, not a current assertion that all audit or operational gates pass.

### 4.2 Latest cleanup-audit failures and blockers

These failures remain visible and are intentionally not rewritten as PASS:

| gate | latest observed result | status | consequence |
|---|---|---|---|
| extended 300-second soak | `31 keepalives, 5 disconnects` | `FAIL` | blocks the extended operations gate; investigate before release publication |
| `test_gameplay_full` | `734 PASS, 1 FAIL / 735` at E-14 | `EXPECTED-FAIL-E14` | intentional Fabric JVM-mod boundary; the assertion must remain visible |
| nightly/24-hour soak | no accepted run artifact | `DECLARED-LIMITATION` | procedure existence is not a completed long-run result |
| real-client/GUI evidence | no current artifact in this phase | `DECLARED-LIMITATION` | bot/synthetic evidence is not a real-client capture |
| vanilla Xoroshiro L3 parity | not independently proven | `DECLARED-LIMITATION` | L1/L2 determinism must not be called vanilla byte parity |

At phase entry, the static migration audit also reported missing stable files:
`docs/CURRENT_STATE.md`, `docs/MISSING_FEATURES_1_21_4.md`, and
`docs/mob_stats_149.csv`. Restoring them is the scope of this phase; it is not
evidence that the runtime, integration, or long-run gates are green. The current
publication status therefore remains `BLOCKED` until the required gates are rerun
and the 300-second failure is resolved or explicitly accepted by a later plan.

## 5. Stable gap matrix and fixture

- `docs/MISSING_FEATURES_1_21_4.md` retains the original IDs: base taxonomy `#1–#80`
  plus extension rows `#81–#90`. Matrix status and declared limitations are separate
  concepts; the matrix does not silently turn an operational failure into `DONE`.
- `docs/mob_stats_149.csv` is the runtime-default fixture, copied byte-for-byte from
  `docs-legacy/mob_stats_149.csv`. It must contain 149 data rows and 11 columns.
- `fixture_sha256: b75697102502385b6aee913f0aca80b86cce323a4994b16a29baf408b5ef2f6f`.
- `fixture_shape: PASS` — `149` data rows / `11` columns after the phase check.
- The fixture check for this phase is current only after the checksum, row/column,
  and scope commands in §7 complete successfully.

## 6. Next steps

1. Re-run the canonical static/link/schema checks and all required runtime gates from
   `docs/VERIFICATION.md`, preserving exact process ownership and artifacts.
2. Investigate the latest 300-second soak disconnects; do not weaken assertions or
   relabel the E-14 expected failure.
3. Finish plan47 residual cleanup only under a separately approved behavior-safe or
   gameplay plan.
4. Keep plan48’s canonical documents, stable trackers, and fixture paths synchronized
   without rewriting `docs-legacy/`.

## 7. Checks performed for this phase

All commands used for this phase are timeout-wrapped. The final commit is permitted
only when the following are successful:

```bash
timeout --foreground --kill-after=5 30 git diff --check
timeout --foreground --kill-after=5 30 cmp -- docs/mob_stats_149.csv docs-legacy/mob_stats_149.csv
timeout --foreground --kill-after=5 30 sha256sum docs/mob_stats_149.csv
timeout --foreground --kill-after=5 30 awk -F, 'BEGIN { n=0; bad=0 } /^[[:space:]]*#/ || NF==0 { next } /^name,/ { next } { if (NF != 11) bad=1; n++ } END { if (n != 149 || bad) exit 1; print n " rows / 11 columns" }' docs/mob_stats_149.csv
timeout --foreground --kill-after=5 30 git status --short --untracked-files=all
```

These are state/fixture checks only. They do not constitute a green final runtime
gate; the `FAIL`, `EXPECTED-FAIL-E14`, and `DECLARED-LIMITATION` entries above remain
part of the current state.
