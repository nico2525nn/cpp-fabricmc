# CURRENT_STATE.md — cpp-fabricmc final-gates state tracker

> This is the stable tracker for the plan48 documentation refresh. It records the
> implementation baseline and the exact final-gates evidence supplied for the main
> checkout. It is not a release sign-off: `publication_status` remains `BLOCKED`, and
> a historical taxonomy `DONE` or a focused PASS does not become a universal parity
> claim.

## 1. Snapshot

| field | value |
|---|---|
| `updated` | `2026-09-04` |
| `implementation_baseline` | `f21e42327342fe1e8486960f2c43805711280ffd` |
| `implementation_baseline_short` | `f21e423` |
| `documentation_commit` | `4fa4845d40035e503ec6c54d83acc14e1c138674` |
| `main_integration_merge` | `8bb716beefe6c6eebb55e6024af23241be9c411e` |
| `plan` | `plan48` |
| `phase` | `final-gates` |
| `phase_status` | `RECORDED` |
| `publication_status` | `BLOCKED` |
| `runtime_reference_snapshot` | `f21e42327342fe1e8486960f2c43805711280ffd` |

The baseline is the implementation merge that includes cleanup commit
`db12df96093a0869e958f62b11f9a9cd68ba3ef1` and safety commit
`4526dfe4f7112b1fe744a83484ef5ef40176d481`. The documentation refresh is
`4fa4845d40035e503ec6c54d83acc14e1c138674`, integrated into main by merge
`8bb716beefe6c6eebb55e6024af23241be9c411e`. The refresh changes canonical Markdown
only; it does not change source, tests, tools, CMake, generated data, or the
`docs-legacy/` archive.

## 2. Plan48 and cleanup record

| item | state at this tracker | evidence / scope |
|---|---|---|
| canonical specifications | `REFRESHED` | canonical index and WIRE/GAMEPLAY/OPS/DEVELOPMENT/VERIFICATION docs point at the merge baseline |
| archive | `DONE` | historical assessment files are present under `docs-legacy/` and their index links resolve |
| stable tracker and fixture | `CURRENT` | this tracker and `docs/mob_stats_149.csv` retain their stable paths and checksum |
| final-gates evidence | `RECORDED` | exact results in §4; no failure is hidden or averaged away |
| publication | `BLOCKED` | `soak_bot` failed; E-14 is the intentional allowed gameplay failure, not a general waiver |

The cleanup commit `db12df96093a0869e958f62b11f9a9cd68ba3ef1` removed the legacy
Structures API: 10 files, `+22/-787`, with source/test legacy-reference grep `0`.
The safety commit `4526dfe4f7112b1fe744a83484ef5ef40176d481` made cleanup patterns
self-safe with `cppfm --por[t]` in `tests/test_server_full.py` and
`tools/replay_vanilla.py`.

## 3. Residual cleanup and parity boundaries

Plan47 cleanup is not a complete vanilla-parity or release audit. Residuals are kept
explicit rather than being converted into a broad PASS:

| residual | status | reason / next owner |
|---|---|---|
| `Structures.hpp` legacy API | `RESOLVED` | removed by `db12df96093a0869e958f62b11f9a9cd68ba3ef1`; `StructureManager`/`StructurePlacer` remain the current structure owners |
| session mining versus `MiningCalculator` | `OPEN` / `DECLARED-LIMITATION` | measured semantic differences remain; unification requires a gameplay plan, not a cleanup-only edit |
| `MobBehaviorSpec` coverage | `PARTIAL` / `OPEN` | the table is only partially wired to live behavior; broader species parity remains future work |
| retained marker/comment inventory | `NOT-FULLY-MEASURED` | the cleanup grep was `0` for legacy references, but no complete zero-marker inventory was proven; do not claim one |

The `RESOLVED` Structures API row does not close the structure-generation parity
boundary. In particular, vanilla Xoroshiro L3 byte parity is not independently
proven, and historical numbered-row `DONE` values are not universal parity claims.

## 4. Exact final-gates evidence

These are the exact main-checkout results supplied for baseline `f21e423` on
2026-09-04. They are not averages with older runs:

| target | result | status / consequence |
|---|---|---|
| configure/build | completed after a filesystem-slow initial 300s outer timeout; resumed build completed `104/104` | `PASS` |
| incremental Ninja build | `ninja: no work to do` in `0.05s` | `PASS` |
| `test_scoreboard_reset` | `22 PASS 0 FAIL` | `PASS` |
| `test_spec_wire` | `392 PASS 0 FAIL 0 SKIP` | `PASS` |
| `test_wire_full` | `405 PASS 0 FAIL 0 SKIP` | `PASS` |
| `test_wire_b6` | `133 PASS 0 FAIL` | `PASS` |
| `test_fuzz` | `23 PASS 0 FAIL` | `PASS` |
| `test_gameplay_full` | `734 PASS / 1 intentional E-14 FAIL / 735` (exit 1) | `EXPECTED-FAIL-E14`; remains visible |
| `test_seed_parity` | `201 PASS 0 FAIL` | L1/L2 evidence only; L3 remains unproven |
| `test_mining_full` | `38/38` | `PASS` |
| `test_block_hardness_full` | `16/16`, `1095 mismatch=0` | `PASS` |
| `test_mob_stats_full` | `131 PASS 0 FAIL` | `PASS` |
| `test_redstone_engine_full` | `29 PASS 0 FAIL` | `PASS` |
| `test_recipes_mirror` | `76 PASS 0 FAIL` | `PASS` |
| `test_native` | `ALL PASS` in `2.33s` | `PASS`; no invented aggregate count |
| `test_plan43` | `82/0` in `25.01s` | `PASS` |
| `test_smoke_80` | `212/0` in `161.33s` | `PASS` |
| `test_server_full` | `234/0` in `273.79s` | `PASS` |
| multi-client | `ALL PASS` in `17.83s` | `PASS` |
| bot smoke | `ALL PASS` in `20.65s` | `PASS` |
| view32 dry benchmark | `PASS` for 4,225 chunks in `1.74s`; p50 `0.108ms`, p95 `2.331ms`, peak RSS ~`95MB`, hit rate `84.6%` | synthetic dry result |
| 120-client stress | `120/120 joined PASS` in `69.11s` | `PASS` |
| `tests/soak_test.py --duration 300` | `PASS` in `301.18s`; 150 keepalives, 0 disconnects, actions `2930`, post-fill RSS growth `14.5%` | short synthetic soak; not 2h/24h |
| `tools/soak_bot.py --duration 300` | `FAIL` in `301.89s`; keepAlives `3 (<7)`, kicks `0`, chunks `182`, time updates `23` | blocks publication |
| focused CTest | 20 tests excluding `soak2h`, `soak_bot`, and `smoke80`: 19 passed plus gameplay_full's single intentional E-14 failure; CTest rc `8` | not a green aggregate |

The result for `tests/soak_test.py` does not override the failing `tools/soak_bot.py`
gate. No accepted 2-hour or 24-hour run artifact exists, and no current real-client
or GUI artifact exists.

## 5. Declared limitations

- **E-14 Fabric JVM-mod boundary:** arbitrary Fabric JVM bytecode and the Fabric
  event-bus runtime cannot execute inside `cppfm`; the one gameplay failure is
  intentional and must remain an expected failure.
- **Vanilla Xoroshiro L3:** `test_seed_parity` proves the stated L1/L2 evidence, but
  exact vanilla Xoroshiro byte parity is not independently proven.
- **Protocol 776:** 1.21.5 and its later Bundle-item behavior are outside the
  protocol-769/1.21.4 scope.
- **Long-run evidence:** no accepted 2-hour or 24-hour run exists. The 300-second
  bot-soak failure remains a blocker even though the separate `soak_test.py` run
  passed.
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
These checks validate publication hygiene only; they do not turn the runtime
`soak_bot` failure, E-14, missing L3 proof, missing long-run artifact, or missing
real-client artifact into a pass.

Next actions are to investigate `soak_bot`, keep the E-14 assertion unchanged, and
address the mining/MobBehaviorSpec residuals and long-run/real-client evidence only
under separately owned work.
