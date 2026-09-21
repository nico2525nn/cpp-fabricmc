# Runtime cleanup audit

Scope: `src/core/**`, `src/worldgen/**`, `src/brigadier/**`, `src/platform/**`,
`src/api/**`, `src/jvm/**`, and the scoped regression test.

## Fixed measurement

The eligible production LOC command is fixed to nonblank C/C++ source lines,
excluding generated subdirectories:

```sh
find src/core src/worldgen src/brigadier src/platform src/api src/jvm -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.cc' -o -name '*.cxx' \) ! -path '*/generated/*' -print0 | sort -z | xargs -0 awk 'NF{n++} END{print n+0}'
```

| Measurement | Eligible LOC |
|---|---:|
| Before cleanup (existing dirty tree) | 11,096 |
| After cleanup | 11,092 |
| Delta | **-4** |

The 10,000-line target is not safely reachable in this owned scope: it would
remove roughly 90% of the shipped runtime implementation. Generated data,
fixtures, assertions, evidence, public APIs, and ABI-facing behavior are not
eligible deletion targets.

## Changes

- `src/brigadier/Arguments.hpp`: consolidated repeated balanced `[]`/`{}`
  scanners into `consumeBalanced`, preserving cursor and error behavior.
- `src/worldgen/StructurePlacer.*`: consolidated duplicated random-spread
  origin math; `shouldPlaceAt` and `findOrigin` now share one candidate path.
- `src/worldgen/StructureManager.cpp`: consolidated the two pending-queue drain
  loops without changing locking or ownership.
- `src/jvm/JavaObjectCache.cpp`: routed the untyped erase through the existing
  env-aware erase path; a null environment preserves the previous no-release
  behavior while removing the duplicate map walk.
- `tests/test_goal_cleanup_runtime.cpp`: added nested parser, placement-origin,
  and native/JVM handle-cache regression coverage.

## Follow-up refactors

After the initial scoped measurement, four small production-only cleanups were
performed in separate files. Their local before/after measurements are retained
here because the checkout also contains unrelated dirty edits:

| file | fixed local measurement | delta |
|---|---:|---:|
| `src/physics/Redstone.cpp` piston collection helper | `1,785 → 1,752` total lines | `-33` |
| `src/game/MenuLogic.cpp` (reported with the game cleanup) | `612 → 596` nonblank/noncomment lines | `-16` |
| `src/game/StairsHelper.hpp` (reported with the game cleanup) | `176 → 150` nonblank/noncomment lines | `-26` |
| `src/jvm/JvmRuntime.cpp` | `2,954 → 2,950` nonblank/noncomment lines | `-4` |
| `src/game/GameServer_session.cpp` | `6,484 → 6,476` total lines | `-8` |

These are behavior-preserving helper consolidations and callsite-zero compatibility
removals, not deletions of features, fixtures, assertions, or generated registries.
The removed helpers were not part of the shipped executable's external ABI; source
compatibility for out-of-tree includers is not promised. The repository-wide
10,000-line deletion target remains infeasible under those constraints.
The final measurement transcript is retained at
`/tmp/grok-goal-e8af7c070433/implementer/loc-followup-final.log`.

The final cleanup pass also removed three confirmed unused standard-library
includes from `ByteBuffer.hpp`, `NBT.hpp`, and `PacketBatcher.cpp`, then removed
callsite-zero core, packet, JVM, registry, ticket, world, and structure aliases.
The unreachable fire and density branches were removed as well. This pass also
removed the unreferenced alternate pending-queue copy/count/clear API while
retaining `drainPendingMobs`/`drainPendingLoot`, and removed the raw level-data
testing convenience method while retaining `loadWithRecovery`. The repeated
whole-tree measurement is `308` files / `101,045` lines against the frozen
`302` / `100,991` baseline (`+54`); the protected manifest has zero mismatches.
The exact latest transcript is `/tmp/grok-goal-e8af7c070433/implementer/cleanup-pending-loadraw-measure.log`.

## Verification

- Standalone scoped regression: `goal-cleanup-runtime: PASS`.
- `git diff --check`: passed.
- `cmake --build build --target cppfm test_settings_matrix test_goal_cleanup_game
  -j2`: completed after the focused cleanup and security changes; the production
  binary and goal regressions link successfully.
- `ctest -R '^(gameplay_full|redstone_engine_full)$'`: `2/2 PASS` after the
  piston helper refactor; `jvm_runtime`, `test_menu_logic`, `goal_security_guards`,
  and `native` also passed in their focused reruns.
- The final `StructurePlacer::stateFor` removal rebuilt all targets and passed
  `test_gameplay_full` (`806/806`), full non-nightly CTest (`54/54`), and the
  focused ASan/UBSan set (`4/4`).
- The pending-queue and raw-load removal passed `test_gameplay_full` (`806/806`),
  `test_native` (all pass), `test_recovery` (`55/55`), and `test_smoke_80`
  (`224/224`); the retained output is
  `/tmp/grok-goal-e8af7c070433/implementer/pending-loadraw-target-tests.log`.
- The first serialized post-change CTest run saw one timing-sensitive
  `randomTickSpeed reset command accepted` smoke assertion (`223/1`); the
  standalone retry, `smoke80` CTest retry, and final full CTest all passed.
  The initial failure is retained at
  `/tmp/grok-goal-e8af7c070433/implementer/full-ctest-pending-loadraw.log`.
