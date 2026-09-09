# VERIFICATION — evidence and release gates

This document is the verification contract for the canonical snapshot of Minecraft
Java Edition **1.21.4**, protocol **769**, and DataVersion **4189**. The source snapshot
is `main` HEAD `574e67b` plus the current cleanup worktree, rechecked on
**2026-09-10**. No new commit is implied by this working-tree record.
Paths under the ignored `build/` tree are local outputs from named runs, not
tracked/public evidence or release assets unless separately published; the
tracked source, commands, and separately published attachments are the
reproduction boundary.
The test matrix verifies the current C++ implementation; it does not silently turn
an approximation into vanilla parity.

**MISSING targets:** all numbered rows **#1–#90** (base taxonomy #1–#80 plus
extensions #81–#90), the Fabric-specific rows, and the strict/deep/H1/B/C/E/W/G/O
audit identifiers. This document owns evidence and status interpretation; feature
behavior belongs in [SPEC_GAMEPLAY.md](SPEC_GAMEPLAY.md), packet bytes in
[SPEC_WIRE.md](SPEC_WIRE.md), and operational limits in [SPEC_OPS.md](SPEC_OPS.md).

**Status:** the recorded non-nightly CTest baseline, current clean extracted
one-file package lifecycle gate, and separate package JVM gate pass; publication
remains `BLOCKED` by the declared scope/evidence boundaries. **Limitations:** the embedded
JVM is default-on only in a binary whose
configure/build found the required JDK/JNI inputs. That JNI-capable binary still
needs a compatible runtime JDK/classes; a binary built without JNI remains
native-only even if a JDK is installed later, and the native fallback does not
close the JVM-mod boundary. The bounded fixture/bridge and structural-transformer
gate pass, while the Mojang
GameProvider and arbitrary Fabric JVM mods remain outside the supported boundary
(E-14). A separate offline probe covers the pinned official Loader/Knot/Mixin API
against the shadow provider; it is not a full Mojang runtime. Exact vanilla Xoroshiro
byte parity (world-generation L3), and real-client/24-hour evidence are separate
boundaries. Any unverified assertion uses `DECLARED-LIMITATION` rather than an
inferred pass. See [PLAN51_JVM.md](PLAN51_JVM.md).

## 1. Feature overview

Verification has six layers:

1. static documentation, path, link, schema, and scope checks;
2. configure/build and source/test registration checks;
3. primitive, wire, fixture, and focused gameplay tests;
4. server lifecycle and multi-client integration tests;
5. recovery, flood, RCON, load, and soak evidence; and
6. diff review, limitation review, and publication.

The acceptance rule is conjunctive: a new failure, an unexpected source diff, a
broken canonical link, a missing fixture, or an unowned child process blocks
publication. A historical PASS is evidence to reproduce, not permission to relax a
current assertion.

## 2. Reference specification and provenance

| evidence/source | purpose | provenance/status |
|---|---|---|
| `https://raw.githubusercontent.com/PrismarineJS/minecraft-data/master/data/pc/1.21.4/protocol.json` | packet IDs, states, directions, fields, and types | `WIRE-ORACLE` |
| reference-server/client captures and repository golden vectors | observable bytes and ordering | `CAPTURED` |
| Minecraft Wiki Java protocol page | supplemental encoding explanation | `VANILLA-CONCEPT` |
| Fabric 1.21.4 release note and Loader 0.16.9 docs | platform/version boundary | `VANILLA-CONCEPT` |
| [Fabric developer documentation](https://docs.fabricmc.net/develop/index), Fabric API `0.119.4+1.21.4`, and Yarn `1.21.4+build.8` Javadocs | server/common API names, signatures, lifecycle concepts, and versioned ABI audit | `OFFICIAL-API-REFERENCE` / informational audit |
| current source at the snapshot commit | implementation fact | `IMPLEMENTATION` |
| executable tests listed below | regression evidence | `IMPLEMENTATION`/`CAPTURED` as stated per test |

The priority order is current definitions and fresh executable results, then captured
bytes, then public protocol/concept references, then historical prose. The old
`docs/` audit files are not upgraded to current authority by this document. External
URL availability is a separate network check; failure to fetch an external source
does not invalidate a local byte-lock test.

## 3. Classes and data structures

The matrix below binds a claim to a source path/symbol and an evidence target. A class
or enum existing without an observable test is not counted as full verification.

| layer | implementation path/symbol | input/state | output/evidence | status/provenance |
|---|---|---|---|---|
| static docs | `docs/README.md`, `SPEC_*.md`, `DEVELOPMENT.md`, this file | canonical files, headings, links | required-file/schema result | `IMPLEMENTATION` |
| packet registry | `src/proto/Ids.hpp::cppfm::proto` | protocol 769, state/direction | current packet IDs | `IMPLEMENTATION` + `WIRE-ORACLE` |
| frame/codec | `src/net/PacketEncoder.hpp`, `PacketDecoder.hpp`, `Connection.hpp` | frame, compression, encryption | accepted/rejected frame and wire vectors | `IMPLEMENTATION` + `test_fuzz` |
| chunk/light | `src/game/ChunkCodec.hpp`, `src/physics/LightEngine.*` | 24 sections, masks, palettes | chunk/update-light bytes | `IMPLEMENTATION` + `test_spec_wire` |
| gameplay | `src/game/World.hpp`, `Entities.hpp`, `Containers.hpp`, `Recipes.*` | world/entity/menu/data state | behavior assertions and packet consequences | `IMPLEMENTATION` |
| persistence | `src/game/WorldDataManager.*`, `Persistence.hpp`, `RegionFile.hpp` | NBT, regions, session lock | recovery and integrity result | `IMPLEMENTATION` + `test_recovery` |
| JVM boundary | `src/jvm/`, `jvm/java/`, `jvm/shadow_api.json`, `jvm/vendor/` | VM, handles, shadow ABI, selected callbacks, structural transformer, official-loader probe | `test_jvm_handles`, `jvm_manifest`, `jvm_runtime`, `jvm_transformer`, `jvm_compatibility`, `verify_fabric_runtime.py --offline --probe` | `IMPLEMENTATION` / bounded fixture + probe |
| mod linkage preflight | `tools/scan_mod_linkage.py`, `tests/test_scan_mod_linkage.py` | mod/provider class files, nested JARs, Tiny v1/v2 namespace mappings | structural missing-class/member and Java-major report; raw Mixin targets remain diagnostic | `IMPLEMENTATION` / conservative preflight |
| fixture | `docs/mob_stats_149.csv`, `Entities.hpp::MobStats` | 149 data rows, 11 columns | row/schema/checksum result | `IMPLEMENTATION` |
| build registry | `CMakeLists.txt::add_executable/add_test` | source and test targets | configured/buildable target set | `IMPLEMENTATION` |

The CSV is a runtime fixture, not disposable documentation. Its default path and
`MOB_STATS_CSV` override are both part of the verification contract.
The final fixture check remains byte-identical to `docs-legacy/mob_stats_149.csv`:
SHA-256 `b75697102502385b6aee913f0aca80b86cce323a4994b16a29baf408b5ef2f6f`, with
`149` data rows and `11` columns.

## 4. Packet and wire evidence

The wire gate protects protocol 769 state/direction and field bytes. In particular,
the current definitions are:

```text
LevelChunkWithLight  = 0x28
UpdateLight          = 0x2B
Play KeepAlive       = 0x27
OpenScreen           = 0x35
TradeList            = 0x2E
ContainerSetContent  = 0x13
MultiBlockChange     = 0x4E
```

The byte-lock gate also covers the single-valued palette `longCount=0`,
`(state << 12) | (localX << 8) | (localZ << 4) | localY` MultiBlockChange packing,
slot components `damage=3`, `repair_cost=17`, `trim=45`, configuration registry
ordering, and UpdateLight masks. The field table and packet provenance are owned by
[SPEC_WIRE.md#packet-contract-table](SPEC_WIRE.md#packet-contract-table).

## 5. Events and evidence checkpoints

Every operational claim must have a source symbol and an artifact or named test.
An ignored `build/` path may identify the local output of that run, but it is not a
tracked/public artifact unless separately published:

| checkpoint | source/effect | required evidence |
|---|---|---|
| configuration wait | `GameServer_session.cpp::Session` | known-packs response and wire/native test |
| block/light emission | `World::setBlock`, `LightEngine`, `PacketBatcher` | gameplay + wire vector |
| menu synchronization | `MenuInteraction`, `Containers` | wire/gameplay assertion |
| persistence/recovery | `WorldDataManager::loadWithRecovery` | log, exit code, `test_recovery` |
| flood/limit response | `PacketDecoder`, `RateLimiter`, `Connection` | `test_flood_net` case and disconnect result |
| RCON isolation | `RconServer` | `test_rcon_multi` response and tick check |
| manual client | replay/GUI procedure | screenshot/capture with operator and time |

“Verified” is not assigned from a source path alone. A manual or long-run item with
no artifact remains `DECLARED-LIMITATION`. A local ignored build output does not
become retained/public evidence merely because its path is recorded here.

## 6. Gate state transitions

```text
research snapshot
  → canonical content review
  → path/link/schema/scope gate
  → configure/build gate
  → focused wire/gameplay gate
  → integration/operations gate
  → diff and limitation review
  → publish
```

Any failure stops the transition. A forked `cppfm` child left after a timeout is a
failed gate, even if the parent test returned a useful PASS line. Cleanup must be
performed only for confirmed test-owned PIDs before the gate is rerun from its first
step.

## 7. Reproduction and implementation flow

For every new or changed claim:

1. record the version boundary, commit, host, and existing user changes;
2. identify the MISSING target, source path/symbol, and canonical owner;
3. add a focused test, capture, fixture check, or an explicit limitation;
4. run static checks before starting a server process;
5. run unit/wire/gameplay, integration, and operations gates in that order; and
6. review the output, scope, cleanup, and provenance before committing.

Do not change test assertions, packet IDs, or thresholds to make a documentation
gate green. The plan49 record reports implementation and fresh evidence; the E-14
JVM boundary is documented as a limitation rather than encoded as a forced failure.

## 8. C++ evidence-record example

The following is a documentation model, not a runtime registry or new test framework:

```cpp
struct EvidenceRecord {
    std::string missingTarget;
    std::string sourcePathAndSymbol;
    std::string testOrArtifact;
    std::string provenance;
    std::string status;
};

// Example: #75 | PacketBatcher::tryFlushAsMultiBlockChange |
// test_wire_full | WIRE-ORACLE | PASS
```

The actual record may be a Markdown table row. Its minimum fields are MISSING target,
source path/symbol, evidence, provenance, version boundary, status, and limitation.

## 9. Class/source composition

| verification concern | source of truth |
|---|---|
| build/test registration | `CMakeLists.txt` and `ctest --test-dir build -N` |
| wire | `tests/test_spec_wire.cpp`, `test_wire_full.cpp`, `test_wire_b6.cpp` |
| gameplay | `tests/test_gameplay_full.cpp`, smoke/focused tests |
| persistence/security | `tests/test_recovery.cpp`, `test_flood_net.cpp`, `test_rcon_multi.cpp` |
| fixture | `tests/test_mob_stats_full.cpp` and `docs/mob_stats_149.csv` |
| load | `tests/stress_test.py`, `tests/soak_test.py`, `tools/bench_chunk_gen.py` |
| JVM fixture | `tests/jvm_runtime_smoke.py`, `tests/test_jvm_handles.cpp`, `tools/generate_shadow.py` |
| manual/replay | GUI checklist and replay tools, separately labelled |

CTest target names and executable names are recorded separately; renaming either is
outside this docs-only change.

## 10. Module split and ownership

| gate/domain | canonical owner | evidence owner |
|---|---|---|
| bytes and IDs | [SPEC_WIRE.md](SPEC_WIRE.md) | wire tests and captures |
| world and behavior | [SPEC_GAMEPLAY.md](SPEC_GAMEPLAY.md) | gameplay/focused tests |
| limits and recovery | [SPEC_OPS.md](SPEC_OPS.md) | ops tests and run records |
| extension workflow | [DEVELOPMENT.md](DEVELOPMENT.md) | review/scope checks |
| test meaning and release gate | this document | CMake/CTest and artifacts |

One claim has one canonical owner. Cross-links are preferred over copying packet
tables, thresholds, or long audit prose into another document.

## 11. Cautions

- `test_gameplay_full` reports the E-14 arbitrary Fabric JVM-mod boundary as an
  informational limitation. The separate plan51 fixture gate does not close E-14.
- `test_native` prints individual checks rather than a stable aggregate count; report
  its observed output rather than inventing a total.
- A stale `CURRENT_STATE.md`, old packet comment, or old README count is historical
  input, not a fresh result.
- Dry, synthetic, bot, real-client, nightly, and 24-hour evidence are different
  classes. A procedure is not a completed run.
- The six `docs-legacy/assessment-*.md` links in [audit/README.md](audit/README.md)
  point at the existing archive and must resolve locally; no missing archive-link
  exception is allowed.
- Never use a broad process-kill pattern. Inspect exact command lines and terminate
  only test-owned PIDs.

## 12. Performance and measurement

Verification records a measurement only with commit, date, host, options, warm-up,
sample count, and run ID. Generated files under ignored `build/` are local run
outputs; they are not tracked/public evidence unless separately published. The
operational contract is in
[SPEC_OPS.md#performance-and-load](SPEC_OPS.md#performance-and-load).

Older 2026-09-04 and 2026-09-07/08 timings retained elsewhere in this document
are explicitly `HISTORICAL` context. They must not override the canonical
current-working-tree values below. The CTest, package, multi-client, and bot
values were refreshed on 2026-09-10; stress and soak entries retain their own
recorded run dates. The final CTest and package results are recorded with their
exact target and local-output identity; the command and options identify each
sub-run.

| workload | gate/acceptance contract | status semantics |
|---|---|---|
| configure/build | timeout and successful target completion | last recorded clean RelWithDebInfo build completed `129/129`; the current bounded package-target rebuild also completed |
| incremental Ninja build | no source changes remain | `ninja: no work to do` in `0.05s` |
| runtime bootstrap | executable-owned resources and fresh server directory | `runtime_layout` and clean-directory launch checks; embedded assets/classes are extracted without overwriting a sentinel user file |
| one-file package | install tree contains only the server executable and its embedded native resources | `PASS`: CPack produced the ignored local `build/packages/cppfabricmc-1.21.4-Linux-x86_64.zip`, containing only `cppfm`; archive size `54395700` bytes, SHA-256 `c6ae183d4e527f1b75f4cae35a0bcbfb3dba1ab4552e7039ae0f129b378e5440`; clean extracted-directory harness `234 PASS / 0 FAIL`; the CPack preflight rejects missing embedded resources; not a tracked/public release asset |
| package JVM smoke | exact CPack ZIP starts the embedded Java boundary by default | `PASS`: `package_jvm_smoke` extracts only the packaged executable, supplies no classes/assets override, requires strict JVM startup, verifies embedded classes and registry assets, and reaps the owned process |
| view distance 32 | 4,225-chunk dry strict benchmark | `PASS` in `1.74s`: p50 0.108 ms, p95 2.333 ms, peak RSS ~95 MB, hit rate 84.6% |
| 120 clients | stress script completes with owned process cleanup | `CURRENT 2026-09-09`: `PASS` in `68.1s`, 120/120 joined; prior `68.0s` rerun is `HISTORICAL` |
| multi-client integration | cross-client visibility and state | `CURRENT 2026-09-10`: `ALL PASS` in `17.84s`; prior `20.28s` rerun is `HISTORICAL` |
| bot smoke | short bot lifecycle | `CURRENT 2026-09-10`: `ALL PASS` in `20.87s`; prior `23.59s` rerun is `HISTORICAL` |
| full non-nightly CTest regression | registered native, gameplay, operations, JVM, ABI, linkage-contract, quality, and integration tests | `PASS`: `42/42` registered tests passed in `397.54s` with `-LE 'nightly|package'`; the separate release-only `package_jvm_smoke` gate is recorded above and is not folded into this aggregate |
| entity/redstone load | P95 MSPT/TPS and bounded RSS | run-specific; no unlabelled claim |
| `tests/soak_test.py --duration 60` | short post-review concurrency/cleanup smoke | `PASS`: 30 keepalives, 0 disconnects, actions 590, post-fill RSS growth 1.0%; not a 2h/24h result |
| `tests/soak_test.py --duration 300` | short synthetic soak | `PASS`: 150 keepalives, 0 disconnects, actions 2932, post-fill RSS growth 7.6% |
| `tests/soak_test.py --duration 600 --movement-range 3000` | wide synthetic soak after chunk-memory fix | `PASS`: 300 keepalives, 0 disconnects, actions 5707, post-fill RSS growth 6.6% |
| `tools/soak_bot.py --duration 300` | bot soak gate | `3/3 PASS`: each run KeepAlive 30, chunks 182, time updates 300, all error counters 0, cleanup PASS |
| `tests/soak_test.py --duration 1800 --movement-range 3000` | allocation-reuse diagnostic soak | `PASS` on `17ab09f`: 900 keepalives, 0 disconnects, actions 17493, post-fill baseline `114504kB`, max `128868kB`, growth `12.5%`; not a 2h/24h result |
| `tests/soak_test.py --duration 7200 --movement-range 3000` (parent `d1c6a7f`) | dedicated long-run attempt with integrity logs | interrupted at recorded `t=3361s`; post-fill RSS `160388→191612kB` (`+19.5%`), above the `15%` gate; not accepted |
| accepted 2 h/24 h artifact | long-run completion and retained integrity log | none |
| real-client/GUI | manual capture with client metadata | no current artifact; `DECLARED-LIMITATION` |
| ASan/UBSan key regression set | core, wire, fuzz, and gameplay binaries from repository root | `4/4 PASS`; no sanitizer report |
| static quality audit | C++/Python test and process-harness review | `PASS`: 26 C++ test files, 153 production files, 42 Python files |
| `mod_linkage` / `real_mod_harness` / `real_mod_candidates_harness` | class-file linkage, candidate manifest, and fail-closed runtime-diagnostic contracts | `PASS`; synthetic nested-JAR/mapping cases, the locked 12-entry manifest, offline missing-cache `SKIP`, and known recoverable log-noise handling pass |

The former `soak_bot` blocker is resolved by three fresh integrated runs. The attempted
7200-second soak was interrupted above its RSS gate and is not a pass. Publication
remains `BLOCKED` by E-14, unproven vanilla Xoroshiro L3, and missing accepted
2-hour/24-hour/real-client evidence, despite the recorded non-package CTest baseline
and current package gates passing. The structural provider scan is intentionally a
diagnostic: its raw Mixin gaps do not override the zero-diagnostic runtime corpus result.

## 13. Thread safety and process ownership

- The static checker is a single-process, read-only operation.
- A session owns its connection read side; `Connection::tx_` serializes writes.
- The game tick owns world mutation and batch-flush decisions.
- Persistence and RCON workers have explicit shutdown ownership.
- A test harness owns any `cppfm` child and must verify that it exited.
- Test and diagnostic subprocesses create an owned process group, use a
  monotonic deadline, and are terminated/reaped with bounded escalation. A
  cleanup failure is a failed gate, not a pass based on the parent's output.
- The JVM compatibility harness captures stdout and stderr separately and
  combines complete lines only after shutdown; JVM/native INFO diagnostics use
  the process diagnostic stream, while post-lease Java shutdown markers retain
  their required lifecycle position. This prevents redirected-stream byte
  interleaving from creating false missing-phase failures.
- The last completed live-server runs left no `cppfm` process behind. The latest
  full non-package CTest baseline passed `42/42` in `397.54s`. The latest working-tree
  measurement is `95,314` lines across `292` files in `src/`, `tests/`, and
  `tools/` (C++/header/Python/Java/CMake source extensions), versus `80,967` across
  `272` tracked files at the same `HEAD`; the difference is a net increase of
  `14,347` lines and includes intentionally untracked review files and newly added
  implementation/tests. The earlier `78,735`/`80,223` snapshot was stale. The
  10,000-line reduction remains a later cleanup target and was not claimed by
  deleting feature code or evidence.
- A backup/check-world operation is offline and must not copy a world during an active
  save.

Before cleanup, inspect the exact process list. The safe pattern is a self-nonmatching
`cppfm --por[t]` search followed by PID-specific `kill`; broad `pkill` or compiler
process termination is forbidden.

## 14. Edge cases and allowed statuses

The checker must cover:

- missing canonical files, malformed Markdown links, missing anchors, and absolute
  local paths;
- duplicate or missing MISSING IDs, base80/extension10 summary mismatch, and invalid
  status vocabulary;
- CSV comments/header, exactly 149 data rows, exactly 11 columns, checksum drift, and
  `MOB_STATS_CSV` override;
- single-palette zero longs, negative Position/VarInt, compressed/uncompressed frames,
  zlib trailing data, and oversize declarations;
- stale/live session locks, corrupt level/region/player data, orphan servers, and port
  reuse; and
- E-14, official/arbitrary JVM Fabric mods beyond plan51, seed RNG L3, and unavailable
  external/manual/nightly evidence.

Allowed status vocabulary is:

| status | meaning |
|---|---|
| `PASS` | observed gate passed at the named snapshot/run |
| `FAIL` | observed in-scope failure; blocks publication |
| `SKIP` | intentionally not run, with a reason |
| `DECLARED-LIMITATION` | not independently verified, intentionally unsupported, or deferred |
| `DIAGNOSTIC` | a measured, intentionally non-gating preflight result whose raw failures remain visible |
| `HISTORICAL` | prior evidence retained for context only |

## 15. Test method and evidence matrix

### Static gate

The static gate checks required files, links/anchors (including explicit `<a id>`
anchors), source references, the stable CSV, MISSING consistency, and canonical-only
scope. The six `docs-legacy/assessment-*.md` archive links now have local targets;
every local link and anchor must resolve.

The JVM-side static preflight is a separate, fail-closed diagnostic. The standard-
library scanner in `tools/scan_mod_linkage.py` reads class-file declarations,
superclass/interface relationships, nested provider JARs, and the pinned Tiny
namespace mappings. Its synthetic contract is covered by `mod_linkage`. It is not a
replacement for executing a mod: raw Mixin classes can reference target members
that only exist after transformation, so an official-provider scan can report
conservative gaps even when the bounded runtime corpus passes.

### Wire gate

Wire-gate evidence was recorded on 2026-09-04 and remains a version-pinned
regression record; the native/session reruns were refreshed on 2026-09-10.

| target | result | source/evidence class |
|---|---|---|
| `test_spec_wire` | `395 PASS 0 FAIL` | byte-lock vectors, `WIRE-ORACLE` |
| `test_wire_full` | `399 PASS 0 FAIL` | version-pinned Play S→C ID locks and selected field/order vectors, `WIRE-ORACLE` |
| `test_wire_b6` | `136 PASS 0 FAIL` | login/settings/GUI/OP live shapes, `CAPTURED` |
| `test_scoreboard_reset` | `22 PASS 0 FAIL` | ResetScore round trips, `WIRE-ORACLE` |
| `test_fuzz` | `25 PASS 0 FAIL` | malformed frame/NBT/VarInt safety, `IMPLEMENTATION` |

The E-14 boundary is not an expected-failure exemption: any new wire FAIL blocks
publication.

### Gameplay gate

| target | recorded result | interpretation |
|---|---|---|
| `test_gameplay_full` | `807 PASS / 0 FAIL / 807` | known JVM boundary is informational and remains declared |
| `test_smoke_80` | `223 PASS 0 FAIL` | base taxonomy plus extension checks |
| `test_seed_parity` | `201 PASS 0 FAIL` | L1/L2 deterministic evidence; vanilla RNG L3 remains declared |
| `test_mining_full` | `59/59 passed` | shared authoritative mining behavior |
| `test_block_hardness_full` | `16/16 passed; 1095 mismatch=0` | generated block table |
| `test_mob_stats_full` | `131 PASS 0 FAIL` | fixture/stat checks |
| `test_redstone_engine_full` | `42 PASS 0 FAIL` | engine categories, including deterministic rail-shape resolution |
| `test_fluids` | `23 PASS 0 FAIL` | source/flowing/falling states, directional water/lava interactions, waterlogging, Nether evaporation, and queue deduplication |
| `test_menu_logic` | `41 PASS 0 FAIL` | bounded enchanting offers and atomic crafter redstone crafting |
| `test_recipes_mirror` | `76 PASS 0 FAIL` | recipe mirror/offset checks |
| `test_plan43` | `82 PASS 0 FAIL` in `28.16s` after the latest clean rebuild | plan43 integration assertions |

The gameplay table does not claim exact vanilla behavior for an untested internal. Any
new in-scope failure is a publication blocker; E-14 is a declared boundary, not an
allowed test failure.

### JVM boundary gate

The JDK/JNI boundary has separate build-time and runtime conditions. JDK 17+
and JNI headers are needed when configuring/building the JNI bridge and embedded
shadow classes. A JNI-capable binary then needs a compatible runtime JDK and
class tree; a binary built without JNI stays native-only until rebuilt. Without
strict startup, a missing or incompatible runtime falls back to native behavior;
that fallback does not claim JVM-mod compatibility.

| target | recorded result | interpretation |
|---|---|---|
| `test_jvm_handles` | `PASS` | opaque handle invalidation/address-reuse and selective routing invariants |
| `jvm_manifest` | `PASS` | protocol-769 shadow ABI manifest is reproducible; 94 methods (47 native + 47 wrapper), 9 structured methods, 10 injection points, and 14 transformer names are declared |
| `jvm_runtime` | `PASS` | embedded HotSpot, fixture entrypoint, World API, command registration, lifecycle, selected Mixin hooks, and owned clean shutdown |
| `package_jvm_smoke` | `PASS` | exact CPack ZIP, clean extraction, default-on strict JVM startup without a classes/assets override, embedded classes/assets, and owned clean shutdown |
| `jvm_transformer` | `PASS` | pre-definition class-file transformation, verifier-safe stack/local preservation, transform-order contract, MixinExtras operations, `@Share`, and `@Local` selectors |
| `jvm_compatibility` | `PASS` | all 25 historical dependency-free fixture cases pass in one `cppfm` process; the same harness also passes the auxiliary functional API fixture; three consecutive direct reruns passed after the stream-capture hardening |
| `jvm_corpus` | `PASS` | the 25-case compatibility report and auxiliary functional API evidence pass through the executable corpus harness |
| `shadow_abi` | `PASS` | standalone dependency-complete compile/reflection gate; 906 source classes, 763 class files, and 8,297 declared/audited members |
| `jvm_contract_audit` | `PASS` | every declared ABI method has exactly one native or wrapper backend classification |
| official Fabric API/Yarn ABI audit | `PASS / INFORMATIONAL` | Fabric API `0.119.4+1.21.4` common/server-side surface cross-checked against Yarn `1.21.4+build.8`: 206 top-level classes and 1,699 public members audited; no exact class/member descriptor gap in the selected surface; client, datagen, renderer, and internal-only classes are excluded |
| official Loader/Knot probe | `PASS / DECLARED-LIMITATION` | pinned Loader 0.16.9/Knot/Mixin/ASM/intermediary starts with the shadow provider and emits all seven expected markers; local ignored process output is `build/fabric-runtime/probe-evidence-after-fabric-docs-20260908-v1.json`, not a tracked/public artifact |
| embedded official-provider probe | `PASS / DECLARED-LIMITATION` | the C++-owned HotSpot starts the pinned official Loader/Knot target and records the expected handoff/mixin markers; local ignored output is `build/fabric-runtime/embedded-evidence-after-fabric-docs-20260908-v1.json`, not a tracked/public artifact; it is not the Mojang GameProvider |
| locked real public-mod corpus | `PASS / BOUNDED` | Lithium, FerriteCore, Carpet, and the combined runtime probe pass with the explicit Java 21 launcher; latest report is the ignored local output `build/real-mod-corpus/real-mod-corpus-report-after-diagnostics-20260908-v1.json`, not a tracked/public artifact; arbitrary mods and client/GUI remain outside the claim |
| real-mod process diagnostics | `PASS / BOUNDED` | the locked Lithium/FerriteCore/Carpet individual and combined processes produced zero classified JVM linkage/bootstrap/uncaught-exception diagnostics; report is the ignored local output `build/real-mod-corpus/real-mod-corpus-report-after-diagnostics-20260908-v1.json`, not a tracked/public artifact |
| structural provider linkage preflight | `DIAGNOSTIC (raw FAIL)` | official namespace-mapped scans are fail-closed and retain raw Mixin target/injected-member gaps (Lithium 2 classes/220 members, FerriteCore 19/15, Carpet 49/149); this is intentionally non-gating and not a runtime corpus failure or universal compatibility result |
| wider Modrinth candidate probe | `PASS / BOUNDED` | 12 locked entries: 8 Fabric 1.21.4 target-compatible candidates pass JVM/mod bootstrap and owned clean shutdown under Java 21; 2 Create entries are target-incompatible, C2ME requires Java 22+, and Debugify has invalid metadata; corrected report is the ignored local output `build/real-mod-candidates/compatibility-candidates-report-final-20260908.json`, not a tracked/public artifact |

This gate proves only the bounded plan51 compatibility layer and its pinned offline
official-loader probe. It does not prove Mojang GameProvider behavior, arbitrary mod
loading, universal JVM bytecode compatibility, client/GUI behavior, or protocol/RNG
parity.

### Operations gate

The following are required operational checks for a release candidate. The exact
final-gates results below are tied to the named baseline/run; a result without its
metadata and cleanup artifact is not a new claim. The canonical current values are
the named values in this table and §12; CTest, package, multi-client, and bot
results were refreshed on 2026-09-10, while stress and soak entries retain their
recorded dates. Older duplicate timings are labeled `HISTORICAL` and never
override them.

| target/procedure | purpose | status at this document snapshot |
|---|---|---|
| `test_flood_net` / `test_recovery` / `test_rcon_multi` | frame, recovery, and RCON focused targets | `57/0`, `54/0`, and `6/0` respectively |
| `check_world` | offline NBT/world integrity | run-specific; no standalone run recorded here |
| view32 dry benchmark | 4,225 chunk load contract | `PASS` in 1.74s: p50 0.108 ms, p95 2.333 ms, peak RSS ~95 MB, hit rate 84.6% |
| stress 120 | concurrent connection load | `CURRENT 2026-09-09`: `PASS` in `68.1s`, 120/120 joined; prior `68.0s` rerun is `HISTORICAL` |
| multi-client integration | cross-client behavior | `CURRENT 2026-09-10`: `ALL PASS` in `17.84s`; prior `20.28s` rerun is `HISTORICAL` |
| bot smoke | short bot lifecycle | `CURRENT 2026-09-10`: `ALL PASS` in `20.87s`; prior `23.59s` rerun is `HISTORICAL` |
| `tests/soak_test.py --duration 300` | short synthetic stability | `PASS`: 150 keepalives, 0 disconnects, actions 2932, post-fill RSS growth 7.6% |
| `tests/soak_test.py --duration 600 --movement-range 3000` | wide synthetic stability | `PASS`: 300 keepalives, 0 disconnects, actions 5707, post-fill RSS growth 6.6% |
| `tools/soak_bot.py --duration 300` | extended bot stability | `3/3 PASS`: each KeepAlive 30, chunks 182, time updates 300, all error counters 0, cleanup PASS |
| `tests/soak_test.py --duration 1800 --movement-range 3000` | allocation-reuse diagnostic stability | `PASS` on `17ab09f`: 900 keepalives, 0 disconnects, actions 17493, post-fill baseline `114504kB`, max `128868kB`, growth `12.5%`; not a 2h/24h result |
| `tests/soak_test.py --duration 7200 --movement-range 3000` (parent `d1c6a7f`) | long-run stability attempt | interrupted at recorded `t=3361s`; post-fill RSS `160388→191612kB` (`+19.5%`), above the `15%` gate; not accepted |
| accepted soak 2 h/24 h | completed long-run artifact | none |
| real-client/GUI | manual client evidence | `DECLARED-LIMITATION`; no current artifact |

The former `soak_bot` failure is closed by three fresh integrated passes. The
7200-second soak attempt was interrupted above its RSS gate and is not a pass. The
canonical documentation keeps the E-14 boundary, missing vanilla Xoroshiro L3 proof,
and missing long-run/real-client artifacts explicitly limited.

### Reproducible commands

Run from the repository root. Every command is timeout-wrapped; the outer timeout is
intentional even when CTest has a per-test timeout.

The release checks are intentionally two gates. The `package` target builds the
CPack archive and runs its fail-closed embedded-resource preflight. The separate
`package_jvm_smoke` test consumes that exact archive, extracts only its executable
into a clean temporary directory, and verifies default-on strict JVM startup
without a source-tree assets or classes override. It is registered only when the
configure/build found both JNI and compiled shadow classes; a native-only build
retains the ordinary non-strict fallback and simply has no JVM package gate.

```bash
timeout --foreground --kill-after=5 30 git status --short --untracked-files=all
timeout --foreground --kill-after=5 30 git diff --check
timeout --foreground --kill-after=5 30 test -s docs/README.md
timeout --foreground --kill-after=5 30 test -s cmake/verify_self_contained_package.cmake.in
timeout --foreground --kill-after=5 30 test -s tests/package_jvm_smoke.py
timeout --foreground --kill-after=5 30 test -s docs/SPEC_WIRE.md
timeout --foreground --kill-after=5 30 test -s docs/SPEC_GAMEPLAY.md
timeout --foreground --kill-after=5 30 test -s docs/SPEC_OPS.md
timeout --foreground --kill-after=5 30 test -s docs/DEVELOPMENT.md
timeout --foreground --kill-after=5 30 test -s docs/VERIFICATION.md
timeout --foreground --kill-after=5 30 test -s docs/CURRENT_STATE.md
timeout --foreground --kill-after=5 30 test -s docs/MISSING_FEATURES_1_21_4.md
timeout --foreground --kill-after=5 30 test -s docs/audit/README.md
timeout --foreground --kill-after=5 30 test -s docs/mob_stats_149.csv
timeout --foreground --kill-after=5 30 awk -F, 'BEGIN { n=0; bad=0 } /^[[:space:]]*#/ || NF==0 { next } /^name,/ { next } { if (NF != 11) bad=1; n++ } END { if (n != 149 || bad) exit 1; print n " rows / 11 columns" }' docs/mob_stats_149.csv
timeout --foreground --kill-after=5 30 sha256sum docs/mob_stats_149.csv
timeout --foreground --kill-after=5 30 python3 tests/test_scan_mod_linkage.py
timeout --foreground --kill-after=5 120 python3 tests/real_mod_corpus/test_harness.py
timeout --foreground --kill-after=5 120 python3 tools/quality_audit.py --root tests --source-root src --python-root tests --python-root tools
timeout --foreground --kill-after=5 30 python3 tools/tautology_lint.py --strict
timeout --foreground --kill-after=5 30 python3 tests/test_mcproto_framing.py
timeout --foreground --kill-after=5 30 python3 tools/compare_real_mod_corpus.py --validate-report build/real-mod-corpus/real-mod-corpus-report-after-diagnostics-20260908-v1.json
timeout --foreground --kill-after=5 300 cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
timeout --foreground --kill-after=5 300 cmake --build build -j2
timeout --foreground --kill-after=5 30 ninja -C build
timeout --foreground --kill-after=5 180 cmake --build build --target package
timeout --foreground --kill-after=5 120 ctest --test-dir build -R '^package_jvm_smoke$' --output-on-failure --timeout 60
timeout --foreground --kill-after=5 90 cmake --build build --target cppfm_jvm_classes cppfm_jvm_fixture test_jvm_handles -j4
timeout --foreground --kill-after=5 90 python3 tests/jvm_runtime_smoke.py --binary ./build/cppfm --classes ./build/jvm/classes --mods ./build/jvm/fixture-mods
timeout --foreground --kill-after=5 90 ctest --test-dir build -R 'jvm_handles|jvm_manifest|jvm_runtime' --output-on-failure --timeout 60
timeout --foreground --kill-after=5 240 python3 tests/shadow_abi/test_shadow_abi.py --repo .
timeout --foreground --kill-after=5 60 ./build/test_native ./build/cppfm
timeout --foreground --kill-after=5 30 ./build/test_scoreboard_reset
timeout --foreground --kill-after=5 30 ./build/test_spec_wire
timeout --foreground --kill-after=5 60 ./build/test_wire_full
timeout --foreground --kill-after=5 60 ./build/test_wire_b6
timeout --foreground --kill-after=5 30 ./build/test_fuzz
timeout --foreground --kill-after=5 30 ./build/test_runtime_layout
timeout --foreground --kill-after=5 60 ./build/test_gameplay_full
timeout --foreground --kill-after=5 120 ./build/test_seed_parity
timeout --foreground --kill-after=5 60 ./build/test_mining_full
timeout --foreground --kill-after=5 60 ./build/test_block_hardness_full
timeout --foreground --kill-after=5 30 ./build/test_mob_stats_full
timeout --foreground --kill-after=5 60 ./build/test_redstone_engine_full
timeout --foreground --kill-after=5 60 ./build/test_fluids
timeout --foreground --kill-after=5 60 ./build/test_menu_logic
timeout --foreground --kill-after=5 30 ./build/test_recipes_mirror
timeout --foreground --kill-after=5 300 ./build/test_plan43 ./build/cppfm
timeout --foreground --kill-after=5 450 ./build/test_smoke_80 ./build/cppfm
timeout --foreground --kill-after=5 60 python3 tools/bench_chunk_gen.py --view-distance 32 --chunks 4225 --dry --strict
timeout --foreground --kill-after=5 600 python3 tests/stress_test.py --clients 120 --binary ./build/cppfm
timeout --foreground --kill-after=5 400 python3 tests/soak_test.py --duration 300 --binary ./build/cppfm
timeout --foreground --kill-after=5 700 python3 tests/test_server_full.py --binary ./build/cppfm
timeout --foreground --kill-after=5 120 python3 tests/multi_client_test.py --binary ./build/cppfm
timeout --foreground --kill-after=5 120 python3 tests/bot_smoke.py --binary ./build/cppfm --duration 30
timeout --foreground --kill-after=5 400 python3 tools/soak_bot.py --duration 300 --binary ./build/cppfm
timeout --foreground --kill-after=5 1200 ctest --test-dir build -LE 'nightly|package' --output-on-failure --timeout 450
timeout --foreground --kill-after=5 300 ctest --test-dir build -R 'native|scoreboard_reset|spec_wire|plan43|flood_net|fuzz|wire_full|gameplay_full|seed_parity|block_hardness_full|redstone_engine_full|mob_stats_full|mining_full|quality_audit|tautology_lint|mcproto_framing|bench|multi_client|bot_smoke|recipes_mirror|recovery|rcon_multi' --output-on-failure --timeout 120
timeout --foreground --kill-after=5 600 ctest --test-dir build -R smoke80 --output-on-failure --timeout 450
```

For cleanup, first inspect and then use exact PIDs; do not paste an unreviewed broad
`pkill` into a gate script:

```bash
timeout --foreground --kill-after=5 30 pgrep -a -f 'cppfm --por[t]' || true
timeout --foreground --kill-after=5 30 sh -c 'for pid in $(pgrep -f "cppfm --por[t]" || true); do kill -TERM "$pid"; done'
timeout --foreground --kill-after=5 30 sleep 1
timeout --foreground --kill-after=5 30 pgrep -a -f 'cppfm --por[t]' || true
```

If a confirmed test-owned PID remains, repeat the command-line inspection and issue
`kill -KILL <pid>` to that PID only, inside a timeout-wrapped command.

## 16. Priority, status, and rollback

**Priority: highest for publication.** Static/schema/scope checks precede expensive
server runs. The canonical snapshot is acceptable only when the named runtime,
test-harness, comment-only, and canonical-documentation changes are reviewed, the
CSV is unchanged, all archive links resolve, and no assertion is weakened to hide a
failure. The final-gates status is `GREEN_WITH_DECLARED_BOUNDARIES`: the recorded
CTest baseline and current clean extracted-package checks pass. E-14, L3,
long-run, and real-client boundaries remain informational/declared rather than
test results.

Rollback applies only to a migration commit owned by the operator. Use an explicit
inverse or `git revert` of that commit; never reset, checkout, delete broadly, or
discard unrelated user changes. If evidence is missing, retain the claim with
`DECLARED-LIMITATION`, record the missing artifact, and do not report the gate as
green.
