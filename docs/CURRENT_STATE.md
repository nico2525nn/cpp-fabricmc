# CURRENT_STATE.md — cpp-fabricmc final-gates state tracker

> This is the stable tracker for the plan51 embedded-JVM boundary and the current
> cleanup worktree. It records the integrated main checkout and exact rerun evidence. Paths under the
> ignored `build/` tree are local run outputs, not tracked/public evidence or release
> assets unless separately published. It is not a release
> sign-off: `publication_status` remains `BLOCKED` by declared scope/evidence
> boundaries, and a historical taxonomy `DONE` or a focused PASS does not become a
> universal parity claim.

## 1. Snapshot

| field | value |
|---|---|
| `updated` | `2026-09-19` |
| `implementation_baseline` | integrated `main` HEAD `c857bfa` from clean checkpoint `65a7c69` |
| `implementation_baseline_short` | `c857bfa` (`65a7c69` + plan53/54 merges and fixes) |
| `documentation_commit` | final documentation commit sequence after source integration `c857bfa` |
| `main_integration_merge` | `c857bfa` (all validated plan53/54 workstreams integrated) |
| `plan` | plan53 settings/lifecycle matrix + plan54 adversarial cleanup |
| `phase` | `plan53-plan54-final-gates` |
| `phase_status` | `FINAL_GATES_CONFIRMED_REFACTOR_PARTIAL_WITH_DECLARED_BOUNDARIES` |
| `publication_status` | `BLOCKED` |
| `runtime_reference_snapshot` | source integration `c857bfa` + documentation commit `556569b` |
| `canonical_workflow` | `docs/DEVELOPMENT.md#research-workflow` |
| `research_entrypoint` | `docs/research-prompt.md` is a legacy redirect only |
| `research_viewpoints` | `16` current viewpoints; old `13` wording is historical |
| `taxonomy_snapshot` | MISSING `#1–#90`; historical matrix counts `DONE=90, PARTIAL=0, TODO=0` |
| `strict_assessment_1` | `78 gaps`; `HISTORICAL` archive label, not a current aggregate |
| `next_plan` | no new implementation plan authorized by this cleanup; future work must preserve the partial ledger and declared compatibility boundaries |

The previous baseline was the plan50 runtime follow-up after the plan49 implementation integration and cleanup commit
`db12df96093a0869e958f62b11f9a9cd68ba3ef1` and safety commit
`4526dfe4f7112b1fe744a83484ef5ef40176d481`. Its four no-ff merges are recorded in
the main graph: docs `e978dc6`, network `7894a4c`, block `20113bf`, and entity
`e0ca08e` (historical plan49 merges). Plan51 adds a default-on embedded JVM boundary
for binaries built with the required JNI inputs, without changing generated data,
protocol 769, or the `docs-legacy/` archive. The
plan51 implementation and its no-ff integration are recorded above.

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
| chunk generation/save/unload memory | `IMPLEMENTED; 30M DIAGNOSTIC PASS / 2H NOT-ACCEPTED` | generation is serialized per world; async save no longer copies a full `Chunk`; eviction no longer adds an extra 32-block ring; bounded allocation reuse is in `17ab09f`; the 1800s run passes at `114504→128868kB` (`+12.5%`), while the earlier 7200s attempt on parent `d1c6a7f` was not accepted at `+19.5%` |
| accepted 2-hour/24-hour evidence | `INTERRUPTED / ABSENT` | the 7200s synthetic attempt was not completed or accepted; no accepted 2-hour or 24-hour artifact exists; procedures are not results |
| current real-client/GUI evidence | `PASS / LOCAL-ONLY` | mc-pilot-managed Fabric 1.21.4 client logged in offline, entered play, stayed connected for more than one minute, and completed chat/command/block/status/screenshot probes; PrismLauncher 11.1.0 also launched Fabric 1.21.4 through its CLI with an existing authenticated account and joined cppfm; temporary local logs/screenshot were not retained as release artifacts; see [MC_PILOT_REAL_TEST.md](MC_PILOT_REAL_TEST.md) |
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
| CPack package | `PASS` | CPack produced ignored local `build/packages/cppfabricmc-1.21.4-Linux-x86_64.zip`; ZIP contains exactly one `cppfm` executable, archive size `54377042` bytes, SHA-256 `07cbccb4552b50003eec71ef827c22435a6b6442d1039458df598e1de0a0d588`; CPack resource preflight and clean extraction tested; not a tracked/public release asset |
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
| `test_recovery` | `54 PASS 0 FAIL` | `PASS`; includes valid `level.dat.new` promotion, corrupt-primary quarantine, byte preservation, player-data quarantine, region recovery, and session-lock cases |
| `test_plan43` | `82 PASS 0 FAIL` in `28.16s` after the latest clean rebuild | `PASS` |
| `test_smoke_80` | `223 PASS 0 FAIL` | `PASS` |
| `test_server_full` | `234 PASS 0 FAIL` | `PASS` from the clean extracted Linux package; source-tree and package evidence are kept distinct |
| `properties` | `33 PASS 0 FAIL` | `PASS`; table-driven properties/CLI matrix and informational-flag side-effect checks |
| `lifecycle_matrix` | `8/8 PASS` | `PASS`; fail-closed readiness, signal, restart, lock, malformed-input, port-reuse, and process-cleanup matrix |
| multi-client | `ALL PASS` in `17.60s` | `PASS` |
| bot smoke | `ALL PASS` in `21.09s` | `PASS` |
| full non-nightly CTest regression | `45/45 PASS` in `395.76s` | latest rerun using `ctest --test-dir build -LE 'nightly|package' --output-on-failure --timeout 600`; includes properties, lifecycle, `tautology_lint`, and `mcproto_framing`; the separate release-only `package_jvm_smoke` gate is not folded into this aggregate |
| view32 dry benchmark | `PASS` for 4,225 chunks; p50 `0.108ms`, p95 `2.333ms`, peak RSS ~`95MB`, hit rate `84.6%` | synthetic dry result |
| 120-client stress | `120/120 joined PASS` in `68.0s` | `PASS` |
| `tests/soak_test.py --duration 60` | `PASS`; 30 keepalives, 0 disconnects, 590 actions, post-fill RSS growth `1.0%` | latest short post-review concurrency/cleanup smoke; not 2h/24h |
| `tests/soak_test.py --duration 300` | `PASS`; 150 keepalives, 0 disconnects, actions `2932`, post-fill RSS growth `7.6%` | short synthetic soak; not 2h/24h |
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
| explicit no-JNI configure/build | `PASS` | CMake fallback disabled with `CPPFM_ENABLE_JNI_FALLBACK=OFF`; `cppfm` builds cleanly under `CPPFM_HAS_JNI=0` and remains native-only |
| ASan/UBSan key regression set | `4/4 PASS` from the repository root; no sanitizer report | `test_core_safety`, `test_spec_wire`, `test_fuzz`, and `test_gameplay_full`; direct invocation keeps relative assets visible |
| static quality audit | `PASS` — 28 C++ test files, 156 production files, 43 Python files | rejects unconditional assertions, liveness-only fallbacks, bare Python exceptions, and unowned `Popen` launches |
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

The Plan54 primary measurement counts `98,587` lines across `298` files in
`src/`, `tests/`, and `tools/` (C++/header/Python/Java suffixes), versus the clean
`65a7c69` baseline at `96,654` lines across `293` files. The protected manifest is
`80` files / `8,939` lines with zero hash drift. Mutable lines are
`87,715 → 89,648`, a net **increase of 1,933 (+2.20%)**. The strict 18,341-line
reduction target is therefore `PARTIAL`; deleting feature code, fixtures,
assertions, or evidence would not be an acceptable substitute for a safe refactor.

The accepted cleanup ledger is: items `-5`, native process harness `-81`, session
login paths `-13`, command policy `-3`, and JVM bridge `+12`; configuration and
properties evidence added `+671`, and the lifecycle matrix added `+1,352` within
the primary scope. The net matches the reproducible path-by-path measurement.

The timeout investigation found two independent causes of misleading outer
`timeout` failures: parent-only termination left descendants holding pipes,
and readiness/output loops did not always observe the owned child state. The
current harnesses create process groups, use monotonic deadlines, probe actual
server status, terminate and reap the owned group with bounded escalation,
and report cleanup failure explicitly. The disconnected-session path also
marks the player inactive before slow persistence/hooks. The last completed
live-server runs left no `cppfm` process behind. The latest full CTest baseline
passed `45/45` in `395.76s`; the release-only package JVM gate is tracked
separately. A fresh 120-client
stress run joined `120/120` clients in `68.0s`, and the latest 60-second soak
completed with zero disconnects and `1.0%` post-fill RSS growth.

The current one-file package verification passed locally: the Linux ZIP in the
ignored `build/` output contains only `cppfm`, and its clean extracted-directory
`test_server_full` run passed `234/234`; the separate `package_jvm_smoke` gate
also passed strict default-on JVM startup against that ZIP. The ZIP is not a
tracked/public release asset.
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
