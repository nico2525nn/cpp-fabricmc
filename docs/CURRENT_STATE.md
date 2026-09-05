# CURRENT_STATE.md — cpp-fabricmc final-gates state tracker

> This is the stable tracker for the plan50 runtime follow-up/final-gates snapshot. It
> records the integrated main checkout and exact rerun evidence. It is not a release
> sign-off: `publication_status` remains `BLOCKED` by declared scope/evidence
> boundaries, and a historical taxonomy `DONE` or a focused PASS does not become a
> universal parity claim.

## 1. Snapshot

| field | value |
|---|---|
| `updated` | `2026-09-05` |
| `implementation_baseline` | `17ab09f5220bf99203d2aea2b2c9d65f763f433b` |
| `implementation_baseline_short` | `17ab09f` |
| `documentation_commit` | `working tree; finalize after evidence closes` |
| `main_integration_merge` | `d1c6a7fa2263da669c670e7bc9c6182113cd299c` |
| `plan` | `plan50` |
| `phase` | `long-run-follow-up` |
| `phase_status` | `GREEN_WITH_DECLARED_LIMITATIONS` |
| `publication_status` | `BLOCKED` |
| `runtime_reference_snapshot` | `17ab09f5220bf99203d2aea2b2c9d65f763f433b` |
| `canonical_workflow` | `docs/DEVELOPMENT.md#research-workflow` |
| `research_entrypoint` | `docs/research-prompt.md` is a legacy redirect only |
| `research_viewpoints` | `16` current viewpoints; old `13` wording is historical |
| `taxonomy_snapshot` | MISSING `#1–#90`; historical matrix counts `DONE=90, PARTIAL=0, TODO=0` |
| `strict_assessment_1` | `78 gaps`; `HISTORICAL` archive label, not a current aggregate |
| `next_plan` | `plan51` only if a new plan is authorized for the remaining declared boundaries; not started |

The baseline is the plan50 runtime follow-up after the plan49 implementation integration and cleanup commit
`db12df96093a0869e958f62b11f9a9cd68ba3ef1` and safety commit
`4526dfe4f7112b1fe744a83484ef5ef40176d481`. Its four no-ff merges are recorded in
the main graph: docs `e978dc6`, network `7894a4c`, block `20113bf`, and entity
`e0ca08e` (historical plan49 merges). The current documentation update records fresh
evidence against the runtime snapshot; it does not change generated data or the
`docs-legacy/` archive.

## 2. Prior plan48 and cleanup record

| item | state at this tracker | evidence / scope |
|---|---|---|
| canonical specifications | `REFRESHED` | canonical index and WIRE/GAMEPLAY/OPS/DEVELOPMENT/VERIFICATION docs point at the merge baseline |
| archive | `DONE` | historical assessment files are present under `docs-legacy/` and their index links resolve |
| stable tracker and fixture | `CURRENT` | this tracker and `docs/mob_stats_149.csv` retain their stable paths and checksum |
| final-gates evidence | `RECORDED` | exact results in §4; no failure is hidden or averaged away |
| publication | `BLOCKED` | no unexpected test failure remains; E-14, L3, and long-run/real-client evidence remain explicit boundaries |

The cleanup commit `db12df96093a0869e958f62b11f9a9cd68ba3ef1` removed the legacy
Structures API: 10 files, `+22/-787`, with source/test legacy-reference grep `0`.
The safety commit `4526dfe4f7112b1fe744a83484ef5ef40176d481` introduced a
self-nonmatching replay cleanup pattern. The current `tools/replay_vanilla.py`,
`tests/test_server_full.py`, and `tests/run_plan43_suite.py` now use PID-scoped
cleanup so one harness cannot terminate an unrelated concurrent server.

## 3. Residual cleanup and parity boundaries

Plan47 cleanup is not a complete vanilla-parity or release audit. Residuals are kept
explicit rather than being converted into a broad PASS:

| residual | status | reason / next owner |
|---|---|---|
| `Structures.hpp` legacy API | `RESOLVED` | removed by `db12df96093a0869e958f62b11f9a9cd68ba3ef1`; `StructureManager`/`StructurePlacer` remain the current structure owners |
| session mining versus `MiningCalculator` | `RESOLVED (plan49 scope)` | session start/finish and tick completion now share `MiningCalculator` context/results; `test_mining_full` is `59/59` and live smoke/server paths pass |
| `MobBehaviorSpec` coverage | `RESOLVED (plan49 scope)` / `DECLARED-LIMITATION` | 12 descriptor rows are wired to live AI and gameplay assertions; broader species-wide vanilla equivalence remains outside this targeted plan |
| retained marker/comment inventory | `NOT-FULLY-MEASURED` | the cleanup grep was `0` for legacy references, but no complete zero-marker inventory was proven; do not claim one |
| `tools/soak_bot.py --duration 300` | `RESOLVED` | three integrated main runs passed; each had KeepAlive `30`, chunks `182`, time updates `300`, kicks/EOF/server-exit/transport/protocol errors `0`, and owned cleanup PASS |
| chunk generation/save/unload memory | `IMPLEMENTED; 30M DIAGNOSTIC PASS / 2H NOT-ACCEPTED` | generation is serialized per world; async save no longer copies a full `Chunk`; eviction no longer adds an extra 32-block ring; bounded allocation reuse is in `17ab09f`; the 1800s run passes at `114504→128868kB` (`+12.5%`), while the earlier 7200s attempt on parent `d1c6a7f` was not accepted at `+19.5%` |
| accepted 2-hour/24-hour evidence | `INTERRUPTED / ABSENT` | the 7200s synthetic attempt was not completed or accepted; no accepted 2-hour or 24-hour artifact exists; procedures are not results |
| current real-client/GUI evidence | `ABSENT` / `DECLARED-LIMITATION` | no current official-client capture is available; bot/synthetic output is not a real-client proof |
| `wt48/cleanup` worktree | `DIRTY` / `PRESERVE-REVIEW` | branch `wt48/cleanup`, HEAD `5f82ac0b4448f76f98753d18c83bbcd9736da61c`, 19 changed paths, `+74/-840`; contains source/tests/tools and is not an approved merge or removal target |

The `RESOLVED` Structures API row does not close the structure-generation parity
boundary. In particular, vanilla Xoroshiro L3 byte parity is not independently
proven, and historical numbered-row `DONE` values are not universal parity claims.

## 4. Exact final-gates evidence

These are the exact main-checkout results recorded for runtime snapshot `17ab09f` on
2026-09-05. The post-fix wide soak is listed separately from the carried-forward
focused gates:

| target | result | status / consequence |
|---|---|---|
| configure/build | configure and integrated RelWithDebInfo build completed; incremental Ninja was then clean | `PASS` |
| incremental Ninja build | `ninja: no work to do` | `PASS` |
| `test_scoreboard_reset` | `22 PASS 0 FAIL` | `PASS` |
| `test_spec_wire` | `392 PASS 0 FAIL 0 SKIP` | `PASS` |
| `test_wire_full` | `405 PASS 0 FAIL 0 SKIP` | `PASS` |
| `test_wire_b6` | `133 PASS 0 FAIL` | `PASS` |
| `test_fuzz` | `23 PASS 0 FAIL` | `PASS` |
| `test_gameplay_full` | `803 PASS / 1 intentional E-14 FAIL / 804` (exit 1) | `EXPECTED-FAIL-E14`; remains visible |
| `test_seed_parity` | `201 PASS 0 FAIL` | L1/L2 evidence only; L3 remains unproven |
| `test_mining_full` | `59/59` | `PASS`; plan49 authoritative session/tick mining |
| `test_block_hardness_full` | `16/16`, `1095 mismatch=0` | `PASS` |
| `test_mob_stats_full` | `131 PASS 0 FAIL` | `PASS` |
| `test_redstone_engine_full` | `29 PASS 0 FAIL` | `PASS` |
| `test_recipes_mirror` | `76 PASS 0 FAIL` | `PASS` |
| `test_native` | `ALL PASS` in `2.33s` | `PASS`; no invented aggregate count |
| `test_plan43` | `82 PASS 0 FAIL` in `25.01s` | `PASS` |
| `test_smoke_80` | `212 PASS 0 FAIL` | `PASS` |
| `test_server_full` | `234 PASS 0 FAIL` | `PASS` |
| multi-client | `ALL PASS` in `17.83s` | `PASS` |
| bot smoke | `ALL PASS` in `20.65s` | `PASS` |
| view32 dry benchmark | `PASS` for 4,225 chunks; p50 `0.108ms`, p95 `2.333ms`, peak RSS ~`95MB`, hit rate `84.6%` | synthetic dry result |
| 120-client stress | `120/120 joined PASS` in `68.0s` | `PASS` |
| `tests/soak_test.py --duration 300` | `PASS`; 150 keepalives, 0 disconnects, actions `2932`, post-fill RSS growth `7.6%` | short synthetic soak; not 2h/24h |
| `tests/soak_test.py --duration 600 --movement-range 3000` | `PASS`; 300 keepalives, 0 disconnects, actions `5707`, post-fill RSS growth `6.6%` | post-fix wide synthetic soak; not 2h/24h |
| `tests/soak_test.py --duration 1800 --movement-range 3000` | `PASS`; 900 keepalives, 0 disconnects, actions `17493`, post-fill baseline `114504kB`, max `128868kB`, growth `12.5%` | `17ab09f` bounded allocation-reuse diagnostic; not 2h/24h |
| `tests/soak_test.py --duration 7200 --movement-range 3000` (parent `d1c6a7f`) | interrupted/not accepted at recorded `t=3361s`; post-fill baseline `160388kB`, max `191612kB`, growth `19.5%` | exceeded the `15%` post-fill gate before completion; retain as a negative/diagnostic artifact |
| `tools/soak_bot.py --duration 300` | `3/3 PASS`; each run KeepAlive `30`, chunks `182`, time updates `300`, kicks/EOF/server-exit/transport/protocol errors `0`; cleanup PASS | resolved 300s bot gate |
| focused executable regression suite | wire/gameplay/ops executables all pass except gameplay's single intentional E-14; no unexpected FAIL | no aggregate invented |

The three `soak_bot` runs close the former bot-soak blocker. The chunk memory/generation
follow-up is covered by the passing 600-second wide soak and the new 1800-second
allocation-reuse diagnostic pass, but the attempted 7200-second
run was interrupted at the recorded `t=3361s` after exceeding the post-fill RSS gate
(`160388→191612kB`, `+19.5%`). No accepted 2-hour/24-hour run artifact or current
real-client/GUI artifact exists.

## 5. Declared limitations

- **E-14 Fabric JVM-mod boundary:** arbitrary Fabric JVM bytecode and the Fabric
  event-bus runtime cannot execute inside `cppfm`; the one gameplay failure is
  intentional and must remain an expected failure.
- **Vanilla Xoroshiro L3:** `test_seed_parity` proves the stated L1/L2 evidence, but
  exact vanilla Xoroshiro byte parity is not independently proven.
- **Long-run evidence:** the three 300-second bot runs, the 300-second synthetic soak,
  and the post-fix 600-second wide soak pass. The attempted 7200-second run was
  interrupted at the recorded `t=3361s` after post-fill RSS reached `191612kB` from a
  `160388kB` baseline (`+19.5%`, above the `15%` gate); no accepted 2-hour/24-hour
  artifact exists.
- **Real-client evidence:** no current real-client/GUI artifact is available; bot and
  synthetic evidence is not a real-client capture.

## 6. Fixture

- `docs/mob_stats_149.csv` remains the runtime-default fixture and is byte-identical
  to `docs-legacy/mob_stats_149.csv`.
- `fixture_sha256: b75697102502385b6aee913f0aca80b86cce323a4994b16a29baf408b5ef2f6f`.
- `fixture_shape: PASS` — `149` data rows / `11` columns.

## 7. Final documentation checks

The timeout-wrapped pre-commit validation passed for Markdown links and anchors
(including explicit `<a id="…">` anchors), required files and schema, the fixture
checksum/shape, stale-hash and stale-claim grep, scope, and `git diff --check`.
These checks validate publication hygiene only; they do not turn E-14, missing L3
proof, missing long-run artifact, or missing real-client artifact into a pass.

Next actions are to keep the E-14 assertion unchanged, retain the interrupted 7200s
soak as a negative diagnostic artifact, and retain the long-run/real-client evidence
boundaries unless a future authorized plan closes them.

## 8. Plan49 implementation and evidence handoff

Plan49 research, implementation, integration, and focused gates are complete for the
targeted issues. The following rules remain in force for future work:

| handoff item | current state | update rule |
|---|---|---|
| workflow authority | `RECORDED` | keep `DEVELOPMENT.md#research-workflow` canonical; the legacy stub remains a pointer |
| packet map | `RECORDED` | recheck `src/proto/Ids.hpp` and named wire vectors; do not change IDs by prose |
| evidence counts | `UPDATED` | replace only with a closed run artifact; never infer PASS from a procedure or old count |
| strict 78-gap history | `HISTORICAL` | do not merge it into the 90-row taxonomy count |
| `AGENTS.md` handover | `UPDATED` | the authorized handover change points to the canonical workflow, current 16-viewpoint research, protocol-769 IDs, and timeout-safe process cleanup |
| wt48 residual | `PRESERVE-REVIEW` | review/preserve the dirty patch before any exact-path removal or selective reimplementation |
| publication | `BLOCKED` | no unexpected code/test blocker remains; declared E-14/L3/long-run/real-client boundaries still prevent universal release sign-off |
