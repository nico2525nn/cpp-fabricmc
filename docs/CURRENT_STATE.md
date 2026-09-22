# CURRENT_STATE.md — cpp-fabricmc final-gates state tracker

> This is the stable tracker for the plan51 embedded-JVM boundary and the current
> cleanup worktree. It records the integrated main checkout and exact rerun evidence. Paths under the
> ignored `build/` tree are local run outputs, not tracked/public evidence or release
> assets unless separately published. It is not a release
> sign-off: `publication_status` remains `BLOCKED` by declared scope/evidence
> boundaries, and a historical taxonomy `DONE` or a focused PASS does not become a
> universal parity claim.

## 0. Current handoff

This tracked document is the canonical current-state entrypoint. It records the
verification baseline, active compatibility boundaries, and publication status;
`docs/VERIFICATION.md` holds gate definitions and reproducible evidence. Local
handoff notes are not part of the public documentation set.

## 1. Snapshot

| field | value |
|---|---|
| `updated` | `2026-09-23` |
| `implementation_baseline` | integrated `main` HEAD `335fca5` from clean checkpoint `65a7c69` |
| `implementation_baseline_short` | `335fca5` (`65a7c69` + plan53/54 merges and fixes) |
| `documentation_commit` | final documentation commit sequence after source integration `335fca5` |
| `main_integration_merge` | `335fca5` (all validated plan53/54 workstreams integrated) |
| `plan` | plan53 settings/lifecycle matrix + plan54 adversarial cleanup |
| `phase` | `plan53-plan54-final-gates` |
| `phase_status` | `FINAL_GATES_CONFIRMED_REFACTOR_PARTIAL_WITH_DECLARED_BOUNDARIES` |
| `publication_status` | `BLOCKED` |
| `runtime_reference_snapshot` | source integration `335fca5` + final documentation sequence |
| `canonical_workflow` | `docs/DEVELOPMENT.md#research-workflow` |
| `research_entrypoint` | `docs/research-prompt.md` is a legacy redirect only |
| `research_viewpoints` | `16` current viewpoints; old `13` wording is historical |
| `taxonomy_snapshot` | MISSING `#1–#90`; historical matrix counts `DONE=90, PARTIAL=0, TODO=0` |
| `strict_assessment_1` | `78 gaps`; `HISTORICAL` archive label, not a current aggregate |
| `next_plan` | close current PR #1 gates; the local plan55 protocol-primitive follow-up is implemented, while plan56 server.properties audit research remains to be implemented and verified |

The previous baseline was the plan50 runtime follow-up after the plan49 implementation integration and cleanup commit
`db12df96093a0869e958f62b11f9a9cd68ba3ef1` and safety commit
`4526dfe4f7112b1fe744a83484ef5ef40176d481`. Its four no-ff merges are recorded in
the main graph: docs `e978dc6`, network `7894a4c`, block `20113bf`, and entity
`e0ca08e` (historical plan49 merges). Plan51 adds a default-on embedded JVM boundary
for binaries built with the required JNI inputs, without changing generated data,
protocol 769, or the `docs-legacy/` archive. The
plan51 implementation and its no-ff integration are recorded above.

## 1A. Current working-tree verification

The following evidence is from the current uncommitted working tree after the
settings/security/authority hardening pass. It supersedes neither the historical
`335fca5` baseline above nor the declared release boundaries below.

| gate | measured result | scope / limitation |
|---|---|---|
| configure/build | `RC=0` | RelWithDebInfo Ninja configure and full build; all commands timeout-wrapped |
| settings matrix | `27 PASS / 0 FAIL` | typed properties, CLI precedence, Java String.hashCode/textual seeds, secure-profile/chat separation, invalid-input retention, and unsupported-key reporting |
| properties | `33 PASS / 0 FAIL` | table-driven properties/CLI and help/version side-effect checks |
| recovery | `55 PASS / 0 FAIL` | `CPPFM_RECOVERY_WORLD_PREFIX=/dev/shm`; level/player/region recovery and session-lock cases |
| core safety | `45 PASS / 0 FAIL` | parser, NBT, compression, packet, Anvil, whitelist, and scoped-event callback boundaries |
| plan43 protocol matrix | `87 PASS / 0 FAIL` | command authority, signed-command offset parsing, abilities, horse/use-entity, movement/fall, sign persistence, and liveness |
| smoke80 | `224 PASS / 0 FAIL` | rebuilt strict source-tree matrix with isolated operator fixture and owned cleanup |
| live protocol/authority | `240 PASS / 0 FAIL / 240 total` | full `tests/test_server_full.py`; resource-pack UUID/ack, command authority, secure-chat paths, persistence/restart, RCON, and wire replays |
| non-nightly CTest | full run `54/54 PASS` after final cleanup | `ctest --test-dir build -LE 'nightly|package' --output-on-failure` passed all registered non-package tests, including the three live fixtures |
| 120-client stress | `120/120 joined PASS` in `68.5s` | `tests/stress_test.py --clients 120`; server tick remained alive and owned cleanup completed |
| 300-second soak | `PASS` | 150 keepalives, 0 disconnects, 2,895 actions, post-fill RSS growth `1.0%` in the retained goal comparison run |
| view-distance 32 dry benchmark | `PASS` | 4,225 chunks; p50 `0.107ms`, p95 `2.332ms`, peak RSS ~`95MB`, hit rate `84.6%`, OOM/kick `0` |
| output isolation | `PASS` | simulation mutations remain under the recursive dispatch gate; frames are encoded under the gate and handed to a per-connection writer with a 4 MiB cap; encrypted frames remain FIFO; graceful close drains for at most 100 ms |
| piston persistence barrier | `PASS` | source/destination pending piston commits are flushed under the simulation gate before synchronous or worker snapshots; full moving-piston serialization remains intentionally omitted from persistent NBT |
| secure profile rejection | `PASS` | invalid profile sessions are terminal when secure profile or secure chat enforcement is enabled; Mojang profile certificates use SHA1withRSA, chat messages use SHA256; signed command argument transcripts remain fail-closed |

The live, focused, stress, soak, benchmark, and CTest runs prove the named paths
only; they do not close the accepted 2-hour/24-hour soak, arbitrary JVM-mod
compatibility, full worldgen call-order/NBT parity, or retained real-client
artifact boundaries.

## 1B. Protocol-primitive follow-up (2026-09-23)

| gate | result | evidence / boundary |
|---|---|---|
| Official primitive oracle | `INSPECTED` | SHA-1-pinned Mojang 1.21.4 server artifact; VarInt, VarLong, and packed BlockPos decoder/packing bytecode reviewed, not executed |
| Primitive and stream regressions | `PASS` | `test_goal_network_bugs` 77/77; independent signed endpoints, non-minimal forms, Java terminal-payload truncation, sixth/eleventh-byte rejection/consumption, plaintext/encrypted stream alignment, big-endian fixed-width fields, and packed Position axis limits |
| Related CTest targets | `6/6 PASS` | `native`, `spec_wire`, `jvm_native_bridge`, `core_safety`, `goal_network_bugs`, and `wire_full` |
| Smoke regression after tick-based chat pacing fix | `1/1 PASS` | `ctest --test-dir build -R smoke80 --output-on-failure --timeout 450`; 180.76 seconds. The preceding PR Actions run for SHA `185e91b` failed this ordinary test with two gamerule feedback timeouts; those assertions remain and now wait for 120 observed server ticks before the reset command. |
| PR Actions gate | `REQUIRED / SHA-SPECIFIC` | Query PR #1 checks for the exact current head SHA; evidence from an earlier commit does not transfer to later commits. |

MISSING #71's row-specific evidence is now `PASS` in the 90-row coverage ledger
(`14 PASS`, `38 PARTIAL`, `38 UNVERIFIED`). This does not remove the independent
publication boundaries below or imply universal protocol parity.

## 2. Prior plan48 and cleanup record

| item | state at this tracker | evidence / scope |
|---|---|---|
| canonical specifications | `REFRESHED` | canonical index and WIRE/GAMEPLAY/OPS/DEVELOPMENT/VERIFICATION docs point at the merge baseline |
| archive | `DONE` | historical assessment files are present under `docs-legacy/` and their index links resolve |
| stable tracker and fixture | `CURRENT` | this tracker and `docs/mob_stats_149.csv` retain their stable paths and checksum |
| final-gates evidence | `CONFIRMED` | current full non-nightly CTest and clean extracted-package harness both pass locally; ignored `build/` outputs are local evidence, not tracked/public release artifacts; declared scope/evidence boundaries remain below |
| publication | `BLOCKED` | E-14, full world-generation L3, accepted long-run evidence, and retained release artifacts remain explicit boundaries even though the executable gates and bounded local client probe pass |

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
| plan51 embedded JVM boundary | `IMPLEMENTED-PARTIAL` | default-on HotSpot/JNI bridge when configure/build finds the required JDK/JNI inputs; a compatible runtime JDK/classes are still required, and a binary built without JNI remains native-only even if a JDK is installed later; shadow ABI, selected events, version-locked pre-definition transformer, selective routing, 25/25 corpus, and official Loader/Knot probe pass; arbitrary JVM mods and universal bytecode compatibility remain declared limitations; see [PLAN51_JVM.md](PLAN51_JVM.md) |
| one-file distribution/runtime layout | `IMPLEMENTED-PARTIAL` | embedded repository-owned assets/classes, fresh-directory tree creation, user-file preservation, fail-closed CPack resource preflight, separate package JVM smoke when JNI/classes are available, and CPack one-executable ZIP; the Linux package gate was verified locally from ignored `build/` output, not a tracked/public release artifact; Windows source path requires a native Windows build and host native libraries/JDK remain external |
| retained marker/comment inventory | `NOT-FULLY-MEASURED` | the cleanup grep was `0` for legacy references, but no complete zero-marker inventory was proven; do not claim one |
| `tools/soak_bot.py --duration 300` | `RESOLVED` | three integrated main runs passed; each had KeepAlive `30`, chunks `182`, time updates `300`, kicks/EOF/server-exit/transport/protocol errors `0`, and owned cleanup PASS |
| chunk generation/save/unload memory | `IMPLEMENTED; 300S PASS / 2H NOT-ACCEPTED` | generation is serialized per world; async save no longer copies a full `Chunk`; the retained 300s goal run passes with post-fill RSS growth `1.0%`, while the requested 7200s run stopped at `t=1200s` with exit `-9` |
| accepted 2-hour/24-hour evidence | `INTERRUPTED / ABSENT` | the retained 7200s artifact stopped at `t=1200s`; no accepted 2-hour or 24-hour artifact exists; procedures are not results |
| current real-client/GUI evidence | `UNAVAILABLE / CURRENT HOST` | The retained capability probe found no `DISPLAY`/`WAYLAND_DISPLAY`, no installed vanilla launcher/client, and no `glxinfo`; protocol tests are not rendering evidence. See [goal-gui-soak.md](audit/goal-gui-soak.md). Earlier local-client notes remain historical and have no retained release artifact. |
| official Fabric API/Yarn ABI audit | `PASS / INFORMATIONAL` | Fabric API `0.119.4+1.21.4` common/server-side surface cross-checked against Yarn `1.21.4+build.8`: 206 top-level classes and 1,699 public members; no exact class/member descriptor gap in the selected surface; client/datagen/renderer/internal-only classes excluded |
| locked real public-mod corpus | `PASS / BOUNDED` | Lithium, FerriteCore, and Carpet plus the combined run pass with the explicit Java 21 launcher; latest local ignored report `build/real-mod-corpus/real-mod-corpus-report-after-diagnostics-20260908-v1.json` records zero classified process diagnostics; it is not a tracked/public artifact and does not establish arbitrary-mod compatibility |
| wider Modrinth candidate probe | `PASS / BOUNDED` | 12 pinned entries classify as 8 target-compatible/runtime passes and 4 explicit non-target/invalid cases; latest corrected local ignored report `build/real-mod-candidates/compatibility-candidates-report-final-20260908.json`; it is not a tracked/public artifact and provides bootstrap/clean-shutdown evidence only |
| structural provider linkage preflight | `DIAGNOSTIC (raw FAIL)` | official server/libraries with Tiny namespace mapping were scanned; raw Mixin target/injected-member references remain (Lithium 2 classes/220 members, FerriteCore 19/15, Carpet 49/149), so this intentionally non-gating result is a conservative preflight signal rather than a runtime failure |

The `RESOLVED` Structures API row does not close the structure-generation parity
boundary. The RNG primitive and splitter contract is now independently covered by
`test_rng_parity`; full vanilla Xoroshiro call-order and structure-NBT parity is
still not proven, and historical numbered-row `DONE` values are not universal
parity claims.

## 4. Exact final-gates evidence

These records combine prior named evidence with the final results confirmed for
the integrated working tree on 2026-09-19. Results are identified by their target
names; package evidence is explicitly identified as a clean extracted-directory
run rather than being conflated with the source-tree harness. Any `build/` path
below is an ignored local output from that run, not a tracked/public evidence or
release artifact unless separately published:

| target | result | status / consequence |
|---|---|---|
| configure/build | current timeout-wrapped full Ninja rebuild and package target completed; the earlier clean RelWithDebInfo baseline was `129/129` targets | `PASS` |
| `runtime_layout` | `PASS` | fresh-directory tree/resource extraction and sentinel preservation |
| CPack package | `PASS` | CPack produced ignored local `build/packages/cppfabricmc-1.21.4-Linux-x86_64.zip`; ZIP contains exactly one `cppfm` executable, archive size `54999329` bytes, SHA-256 `61b19c83100b755b06431c2568e5277e4251867b4b25df98c27ab44118d84b8b`; CPack resource preflight and clean extraction tested; not a tracked/public release asset |
| `package_jvm_smoke` | `PASS` | exact CPack ZIP extracted without checkout assets/classes overrides; default-on strict JVM startup, 1,457 embedded class files, registry assets, and owned clean shutdown verified |
| incremental Ninja build | `ninja: no work to do` | `PASS` |
| `test_scoreboard_reset` | `22 PASS 0 FAIL` | `PASS` |
| `test_spec_wire` | `417 PASS 0 FAIL` | `PASS`; official 1.21.4 EntityTeleport field layout is covered |
| `test_wire_full` | `399 PASS 0 FAIL` | `PASS` |
| `test_wire_b6` | `136 PASS 0 FAIL` | `PASS` |
| `test_fuzz` | `25 PASS 0 FAIL` | `PASS` |
| `test_gameplay_full` | `806 PASS 0 FAIL 806` | `PASS`; arbitrary JVM-mod boundary remains informational |
| `test_seed_parity` | `201 PASS 0 FAIL` | L1/L2 deterministic evidence |
| `test_rng_parity` | `25 PASS 0 FAIL` | Java LocalRandom, Minecraft Xoroshiro seed expansion, primitive outputs, and long/coordinate/string splitter vectors; full worldgen call-order/NBT parity remains open |
| `test_mining_full` | `59/59` | `PASS`; plan49 authoritative session/tick mining |
| `test_block_hardness_full` | `16/16`, `1095 mismatch=0` | `PASS` |
| `test_mob_stats_full` | `131 PASS 0 FAIL` | `PASS` |
| `test_redstone_engine_full` | `42 PASS 0 FAIL` | `PASS` |
| `test_fluids` | `23 PASS 0 FAIL` | `PASS`; directional water/lava interaction, waterlogging, falling states, Nether evaporation, and queue deduplication |
| `test_menu_logic` | `41 PASS 0 FAIL` | `PASS`; bounded enchanting offers and atomic crafter redstone crafting |
| `test_recipes_mirror` | `76 PASS 0 FAIL` | `PASS` |
| `test_native` | `ALL PASS` | `PASS`; includes bounded foreign-thread mutation routing, stop-time cancellation, and dimension-aware entity World handles; no invented aggregate count |
| `test_recovery` | `55 PASS 0 FAIL` | `PASS`; includes valid `level.dat.new` promotion, corrupt-primary quarantine, byte preservation, player-data quarantine, region recovery, and session-lock cases |
| `test_plan43` | `87 PASS 0 FAIL` in the final rerun | `PASS`; command authority, signed-command offset, abilities, horse/use-entity, movement/fall, sign persistence, and liveness |
| `test_smoke_80` | `224 PASS 0 FAIL` | `PASS`; isolated operator fixture and owned cleanup |
| `test_server_full` | `240 PASS 0 FAIL / 240 total` | `PASS` from the source-tree full live matrix; package/source evidence remain distinct |
| `properties` | `33 PASS 0 FAIL` | `PASS`; table-driven properties/CLI matrix and informational-flag side-effect checks |
| `lifecycle_matrix` | `8/8 PASS` | `PASS`; fail-closed readiness, signal, restart, lock, malformed-input, port-reuse, and process-cleanup matrix |
| multi-client | `ALL PASS` in `17.48s` | `PASS` |
| bot smoke | `ALL PASS` in `20.82s` | `PASS` |
| full non-nightly CTest regression | final full run `54/54 PASS` | latest registered set includes smoke80, properties, lifecycle, all three live feature fixtures, `tautology_lint`, and `mcproto_framing`; the separate release-only `package_jvm_smoke` gate is not folded into this aggregate |
| cleanup-hardening performance comparison | `HISTORICAL` | prior 45-test comparison retained as historical context; current aggregate is the separate 54-test rerun above |
| view32 dry benchmark | `PASS` for 4,225 chunks; p50 `0.107ms`, p95 `2.332ms`, peak RSS ~`95MB`, hit rate `84.6%`, OOM/kick `0` | strict synthetic dry result |
| 120-client stress | `120/120 joined PASS` in `68.5s` | `PASS`; server tick remained alive and owned cleanup completed |
| `tests/soak_test.py --duration 60` | `PASS`; 30 keepalives, 0 disconnects, 590 actions, post-fill RSS growth `1.0%` | latest short post-review concurrency/cleanup smoke; not 2h/24h |
| `tests/soak_test.py --duration 300` | `PASS`; 150 keepalives, 0 disconnects, actions `2899`, post-fill RSS growth `0.2%` | final bounded synthetic soak; not 2h/24h |
| `tests/soak_test.py --duration 600 --movement-range 3000` | `PASS`; 300 keepalives, 0 disconnects, actions `5707`, post-fill RSS growth `6.6%` | post-fix wide synthetic soak; not 2h/24h |
| `tests/soak_test.py --duration 1800 --movement-range 3000` | `PASS`; 900 keepalives, 0 disconnects, actions `17493`, post-fill baseline `114504kB`, max `128868kB`, growth `12.5%` | `17ab09f` bounded allocation-reuse diagnostic; not 2h/24h |
| `tests/soak_test.py --duration 7200 --movement-range 3000` (parent `d1c6a7f`) | interrupted/not accepted at recorded `t=3361s`; post-fill baseline `160388kB`, max `191612kB`, growth `19.5%` | exceeded the `15%` post-fill gate before completion; retain as a negative/diagnostic artifact |
| `tools/soak_bot.py --duration 300` | `3/3 PASS`; each run KeepAlive `30`, chunks `182`, time updates `300`, kicks/EOF/server-exit/transport/protocol errors `0`; cleanup PASS | resolved 300s bot gate |
| focused executable regression suite | wire/gameplay/ops executables pass; the JVM boundary is declared rather than encoded as a test failure | no aggregate invented |
| `test_jvm_handles` | `PASS` | generation-safe opaque handle invalidation/address reuse and selective native/JVM routing |
| `cppfm_jvm_classes` / `cppfm_jvm_fixture` | `PASS` | Java shadow ABI and deterministic server-side fixture compile |
| JVM fixture ABI invalidation | `PASS` | CMake now depends on the Java classes stamp, so a changed shadow annotation/API recompiles the fixture instead of reusing stale bytecode |
| `jvm_runtime` | `PASS` | embedded HotSpot, entrypoint, command registration, World API, lifecycle, Mixin HEAD/RETURN/Overwrite, tick, and owned clean shutdown |
| `jvm_transformer` | `PASS` | pre-definition transformer contract, verifier-safe bytecode rewrite, callback/local preservation, transform-order checks, MixinExtras operations, `@Share`, `@Local` selectors, and acronym accessor inflection (`ROOT`/`URL`) |
| `jvm_api` | `PASS` | Fabric-style event, command, registry, and networking callback surface contract |
| `jvm_compatibility` | `PASS` | all 25 historical plan51 fixture cases pass in one `cppfm` process; the same harness also passes the auxiliary functional API fixture; report status `PASS`, 25/25 report fixtures, 0 errors; three consecutive direct reruns also passed |
| `jvm_corpus` | `PASS` | executable 25-case compatibility corpus passes end-to-end |
| `jvm_manifest` | `PASS` | declarative protocol-769 ABI manifest reproducibly generated; 94 methods (47 native + 47 wrapper), 9 structured methods, 10 injection points, 14 transformer names |
| `jvm_contract_audit` | `PASS` | every declared ABI method has exactly one native or wrapper backend classification |
| `shadow_abi` | `PASS` | standalone dependency-complete Shadow ABI compile/reflection gate; 906 source classes, 763 class files, and 8,297 declared/audited members |
| explicit no-JNI configure/build + regression | `PASS` | CMake fallback disabled with `CPPFM_ENABLE_JNI_FALLBACK=OFF`; native targets omit `CPPFM_HAS_JNI`, build cleanly, and the same-commit non-package CTest set passes `42/42`; relative source assets were linked for the external build directory |
| ASan/UBSan key regression set | `4/4 PASS` from the repository root; no sanitizer report | `build-sanitize` uses `-fsanitize=address,undefined`; the four targets were rebuilt and run directly with relative assets visible |
| static quality audit | `PASS` — 33 C++ test files, 156 production files, 48 Python files | rejects unconditional assertions, liveness-only fallbacks, bare Python exceptions, and unowned `Popen` launches |
| official Loader/Knot probe | `PASS / DECLARED-LIMITATION` | offline pinned Loader 0.16.9/Knot/Mixin probe records all seven expected markers; local ignored process output is `build/fabric-runtime/probe-evidence-after-fabric-docs-20260908-v1.json`; it is not a tracked/public artifact, and no Mojang server/provider is shipped |
| embedded official-provider probe | `PASS / DECLARED-LIMITATION` | C++-owned HotSpot starts the pinned official Loader/Knot target and records the handoff/mixin markers; local ignored output is `build/fabric-runtime/embedded-evidence-after-fabric-docs-20260908-v1.json`, not a tracked/public artifact |
| `mod_linkage` / `real_mod_harness` / `real_mod_candidates_harness` | `PASS` | synthetic nested-JAR/Tiny-mapping linkage cases, fail-closed runtime-diagnostic classifier, locked candidate manifest, and offline missing-cache no-false-PASS contract pass |
| `real_mod_corpus` | `PASS / BOUNDED` | Lithium, FerriteCore, Carpet, and combined runtime probes pass with explicit Java 21; latest local ignored report is `build/real-mod-corpus/real-mod-corpus-report-after-diagnostics-20260908-v1.json` with zero classified process diagnostics; it is not a tracked/public artifact and provides no arbitrary-mod or client/GUI claim |
| wider Modrinth candidate probe | `PASS / BOUNDED` | 12 locked entries: 8 Fabric 1.21.4 target-compatible candidates pass JVM/mod bootstrap and owned clean shutdown under Java 21; 2 Create entries are target-incompatible, C2ME requires Java 22+, and Debugify has invalid metadata; corrected local ignored report is `build/real-mod-candidates/compatibility-candidates-report-final-20260908.json`, not a tracked/public artifact |

The three `soak_bot` runs close the former bot-soak blocker. The chunk memory/generation
follow-up is covered by the passing 600-second wide soak and the new 1800-second
allocation-reuse diagnostic pass, but the attempted 7200-second
run was interrupted at the recorded `t=3361s` after exceeding the post-fill RSS gate
(`160388→191612kB`, `+19.5%`). No accepted 2-hour/24-hour run artifact or retained
real-client/GUI release artifact exists, although bounded local mc-pilot and
PrismLauncher probes pass. The structural provider linkage scan is intentionally
conservative: it exposes raw Mixin target/injected-member gaps, while the runtime
corpus report records zero classified process diagnostics for the three locked mods
and their combined run. The recorded full CTest baseline and the separate clean
extracted-package gates are green; the package test remains intentionally
excluded from the non-nightly aggregate and is recorded as its own release
gate. Publication remains blocked only by the declared boundaries below.

## 5. Declared limitations

- **E-14 Fabric JVM-mod boundary:** plan51 now executes a bounded dependency-free
  shadow ABI through default-on embedded HotSpot/JNI when configure/build finds the
  required JDK/JNI inputs. A JNI-capable binary still needs a compatible runtime JDK
  and classes; a binary built without JNI remains native-only even if a JDK is
  installed later, so native fallback does not retrofit JVM support. The selected
  callbacks, version-locked pre-definition class-file transformer, MixinExtras operation
  support, and selective native/JVM routing remain bounded. The 25-case dependency-free corpus,
  pinned official Loader/Knot stack, and three locked real server-side mod cases
  pass. The production path is still not the Mojang GameProvider; arbitrary Fabric
  JVM mods and universal bytecode compatibility remain unsupported. The boundary
  is reported as a limitation; it is not represented by an intentional test failure.
- **Vanilla Xoroshiro L3:** `test_rng_parity` independently covers the Java/Minecraft
  primitive algorithms, seed expansion, bounded outputs, and splitters (`25 PASS /
  0 FAIL`). The complete world-generation call ordering and structure-NBT output
  still lack retained vanilla vectors, so the full L3 claim remains a declared
  boundary.
- **Long-run evidence:** the three 300-second bot runs, the 300-second synthetic soak,
  and the post-fix 600-second wide soak pass. The attempted 7200-second run was
  interrupted at the recorded `t=3361s` after post-fill RSS reached `191612kB` from a
  `160388kB` baseline (`+19.5%`, above the `15%` gate); no accepted 2-hour/24-hour
  artifact exists.
- **Real-client evidence:** bounded local mc-pilot and PrismLauncher checks pass.
  The mc-pilot-managed Fabric 1.21.4 client passes login, world entry, stability,
  chat/command/block/status/screenshot checks; PrismLauncher 11.1.0 launches a
  Fabric 1.21.4 client through its CLI and joins cppfm with an existing
  authenticated account. Logs and screenshots were temporary local evidence,
  not retained release artifacts; first-time Microsoft interactive login, every
  gameplay path, and all PrismLauncher instance configurations remain untested.

## 6. Fixture

- `docs/mob_stats_149.csv` remains the runtime-default fixture and is byte-identical
  to `docs-legacy/mob_stats_149.csv`.
- `fixture_sha256: b75697102502385b6aee913f0aca80b86cce323a4994b16a29baf408b5ef2f6f`.
- `fixture_shape: PASS` — `149` data rows / `11` columns.

## 7. Final documentation checks

The timeout-wrapped documentation validation passed for Markdown links and anchors
(including explicit `<a id="…">` anchors), required files and schema, the fixture
checksum/shape, stale-hash and stale-claim grep, scope, and `git diff --check`.
These checks validate publication hygiene only; they do not turn E-14, missing L3
proof, missing accepted long-run artifact, or the lack of a retained client artifact
into a universal compatibility claim.

The Plan54 primary measurement counts `98,648` lines across `298` files in
`src/`, `tests/`, and `tools/` (C++/header/Python/Java suffixes), versus the clean
`65a7c69` baseline at `96,654` lines across `293` files. The protected manifest is
`80` files / `8,939` lines with zero hash drift. Mutable lines are
`87,715 → 89,709`, a net **increase of 1,994 (+2.28%)**. The strict 18,341-line
reduction target is therefore `PARTIAL`; deleting feature code, fixtures,
assertions, or evidence would not be an acceptable substitute for a safe refactor.
That Plan54 count is historical. The current dirty-tree repeat after the three
owned live fixtures and goal regressions is `308` files / `101,045` lines; against
the goal freeze of `302` / `100,991`, this is `+54` lines. The current protected
manifest is `80` files / `5,994` lines with `0` hash mismatches (see the retained
measurement logs under `/tmp/grok-goal-e8af7c070433/implementer`).

The final cleanup pass removed the unreferenced legacy `World::fillTerrain` body
by delegating the compatibility entry point to `fillTerrainV3`, the dead Redstone
registry, obsolete CombatManager damage wrappers, unused packet conversion helpers,
and several unreferenced compatibility accessors. The protected manifest remains
unchanged.

The accepted cleanup ledger is: items `-5`, native process harness `-81`, session
login paths `-13`, command policy `-3`, and JVM bridge `+12`; configuration and
properties evidence added `+671`, and the lifecycle matrix added `+1,352` within
the primary scope. The final cleanup-hardening assertions/source-policy checks
add `+61` lines. The baseline registration diff is `46 → 50` add-test entries;
the current configuration contains `57` total tests (`54` after excluding
`nightly` and `package`). The added gates are `properties`, POSIX-only
`lifecycle_matrix`, `goal_live_interactions`, and `goal_live_remaining`, with no
existing target removed.
The net matches the reproducible path-by-path measurement.

The timeout investigation found two independent causes of misleading outer
`timeout` failures: parent-only termination left descendants holding pipes,
and readiness/output loops did not always observe the owned child state. The
Python/diagnostic harnesses create process groups, use monotonic deadlines,
probe actual server status, terminate and reap the owned group with bounded
escalation, and report cleanup failure explicitly. The C++ `ServerProcess`
owner now checks `kill`, `waitpid`, and temporary-world removal and aborts on
destructor cleanup failure. The disconnected-session path also marks the player inactive before slow persistence/hooks. The last completed
live-server runs left no `cppfm` process behind. The prior full CTest baseline
passed `53/53` in `469.29s`; the latest 54-test non-package run passed `54/54`
under cumulative load;
the release-only package JVM gate is tracked
separately. The final 120-client stress run joined `120/120` clients in `68.5s`,
and the final 300-second soak completed with 150 keepalives, zero disconnects,
2,899 actions, and `0.2%` post-fill RSS growth.

The earlier one-file package verification passed locally: the Linux ZIP in the
ignored `build/` output contains only `cppfm`, and its clean extracted-directory
`test_server_full` run passed `234/234`; the separate `package_jvm_smoke` gate
also passed strict default-on JVM startup against that ZIP. The broader source-tree
matrix's historical `240/240` result remains separate; the latest selected
`conn,commands,permissions,chat,datapack,persistence,restart` suites passed
`198/198` in `/tmp/grok-goal-e8af7c070433/implementer/server-full-final.log`.
The ZIP is not a tracked/public release asset.
The interrupted 7200s soak remains a negative diagnostic artifact. The accepted
long-run and retained-release-artifact boundaries remain explicit even though the
bounded local real-client probe passes. The active broader
compatibility goal can continue expanding API, constructor/verifier-state,
behavioral, and real-mod coverage; the structural linkage report should be used to
prioritize those changes, not presented as a universal runtime verdict.

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
| publication | `BLOCKED` | executable gates pass; declared arbitrary-mod/E-14, full world-generation L3, long-run, and real-client boundaries still prevent universal release sign-off |

## 9. Active bounded goal evidence

The goal-specific audit files under `docs/audit/` record a fixed dirty-tree
baseline and protected manifest, a 90-row coverage ledger (`14 PASS`, `38 PARTIAL`,
`38 UNVERIFIED`), three owned real-client fixtures, real command/entity/menu transcripts, and 23 reproducible network/gameplay defects
with focused regression tests. The adversarial review is now `P0=0, P1=0, P2=0,
P3=0`; source-order guards cover the security fixes where no authenticated client
or external JVM fixture is available. Scoped production cleanup measured `-9`
lines; the additional refactors are recorded in `goal-cleanup-runtime.md` and
`goal-cleanup-game.md`. The requested 10,000-line reduction was not reached; the
fixed production GC sweep found only small dead entrypoints after these removals,
so no
protected, generated, fixture, assertion, or feature code was deleted to
manufacture that metric. GUI, arbitrary-mod, worldgen-L3, signed-command,
moving-piston-NBT, and accepted long-soak boundaries remain explicit.
The earlier focused feature-entry CTest passed `2/2` in `76.45s`; the additional
remaining-entry fixture passed `1/1` in `63.93s`, and the final two-launch
protocol matrix passed with both owned processes returning `0` and no escalation.
The requested 7200-second soak failed at `t=1200s` with server exit `-9`; no
accepted long-soak result is claimed.
