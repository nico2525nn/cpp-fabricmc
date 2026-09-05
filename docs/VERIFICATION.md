# VERIFICATION — evidence and release gates

This document is the verification contract for the canonical snapshot of Minecraft
Java Edition **1.21.4**, protocol **769**, and DataVersion **4189**. The source snapshot
is integrated runtime `c3a5e49e41261dacb4b9454c538aa87575fa9546`, rechecked on
**2026-09-05**.
The test matrix verifies the current C++ implementation; it does not silently turn
an approximation into vanilla parity.

**MISSING targets:** all numbered rows **#1–#90** (base taxonomy #1–#80 plus
extensions #81–#90), the Fabric-specific rows, and the strict/deep/H1/B/C/E/W/G/O
audit identifiers. This document owns evidence and status interpretation; feature
behavior belongs in [SPEC_GAMEPLAY.md](SPEC_GAMEPLAY.md), packet bytes in
[SPEC_WIRE.md](SPEC_WIRE.md), and operational limits in [SPEC_OPS.md](SPEC_OPS.md).

**Status:** final-gates evidence is recorded against the named baseline, but
publication remains `BLOCKED`. **Limitations:** plan51's optional embedded JVM
passes a bounded fixture/bridge and structural-transformer gate, while the Mojang
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

Every operational claim must have a source symbol and an artifact or named test:

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
no artifact remains `DECLARED-LIMITATION`.

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

Do not change test assertions, expected-failure policy, packet IDs, or thresholds to
make a documentation gate green. The plan49 record reports implementation and fresh
evidence; it does not waive the intentional E-14 assertion.

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

- `test_gameplay_full` contains one intentional E-14 failure for arbitrary Fabric
  JVM-mod execution. It must remain visible and must not be converted into a passing
  assertion; the separate plan51 fixture gate does not close E-14.
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
sample count, and run ID. The operational contract is in
[SPEC_OPS.md#performance-and-load](SPEC_OPS.md#performance-and-load).

The 2026-09-04 local reruns below use host `nico`, runtime snapshot
`17ab09f5220bf99203d2aea2b2c9d65f763f433b`, UTC date `2026-09-05`, and run ID
`plan49-integrated-20260904`. The command and options identify each sub-run.

| workload | gate/acceptance contract | status semantics |
|---|---|---|
| configure/build | timeout and successful target completion | completed after a filesystem-slow initial 300s outer timeout; resumed build completed `104/104` |
| incremental Ninja build | no source changes remain | `ninja: no work to do` in `0.05s` |
| view distance 32 | 4,225-chunk dry strict benchmark | `PASS` in `1.74s`: p50 0.108 ms, p95 2.333 ms, peak RSS ~95 MB, hit rate 84.6% |
| 120 clients | stress script completes with owned process cleanup | `PASS` in `68.0s`: 120/120 joined |
| multi-client integration | cross-client visibility and state | `ALL PASS` in `17.83s` |
| bot smoke | short bot lifecycle | `ALL PASS` in `20.65s` |
| entity/redstone load | P95 MSPT/TPS and bounded RSS | run-specific; no unlabelled claim |
| `tests/soak_test.py --duration 300` | short synthetic soak | `PASS`: 150 keepalives, 0 disconnects, actions 2932, post-fill RSS growth 7.6% |
| `tests/soak_test.py --duration 600 --movement-range 3000` | wide synthetic soak after chunk-memory fix | `PASS`: 300 keepalives, 0 disconnects, actions 5707, post-fill RSS growth 6.6% |
| `tools/soak_bot.py --duration 300` | bot soak gate | `3/3 PASS`: each run KeepAlive 30, chunks 182, time updates 300, all error counters 0, cleanup PASS |
| `tests/soak_test.py --duration 1800 --movement-range 3000` | allocation-reuse diagnostic soak | `PASS` on `17ab09f`: 900 keepalives, 0 disconnects, actions 17493, post-fill baseline `114504kB`, max `128868kB`, growth `12.5%`; not a 2h/24h result |
| `tests/soak_test.py --duration 7200 --movement-range 3000` (parent `d1c6a7f`) | dedicated long-run attempt with integrity logs | interrupted at recorded `t=3361s`; post-fill RSS `160388→191612kB` (`+19.5%`), above the `15%` gate; not accepted |
| accepted 2 h/24 h artifact | long-run completion and retained integrity log | none |
| real-client/GUI | manual capture with client metadata | no current artifact; `DECLARED-LIMITATION` |

The former `soak_bot` blocker is resolved by three fresh integrated runs. The attempted
7200-second soak was interrupted above its RSS gate and is not a pass. Publication
remains `BLOCKED` only for E-14, unproven vanilla Xoroshiro L3, and missing accepted
2-hour/24-hour/real-client evidence.

## 13. Thread safety and process ownership

- The static checker is a single-process, read-only operation.
- A session owns its connection read side; `Connection::tx_` serializes writes.
- The game tick owns world mutation and batch-flush decisions.
- Persistence and RCON workers have explicit shutdown ownership.
- A test harness owns any `cppfm` child and must verify that it exited.
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
- intentional E-14, official/arbitrary JVM Fabric mods beyond plan51, seed RNG L3, and unavailable
  external/manual/nightly evidence.

Allowed status vocabulary is:

| status | meaning |
|---|---|
| `PASS` | observed gate passed at the named snapshot/run |
| `FAIL` | observed failure; blocks publication unless explicitly the E-14 case |
| `SKIP` | intentionally not run, with a reason |
| `EXPECTED-FAIL-E14` | the single intentional Fabric JVM-mod boundary assertion |
| `DECLARED-LIMITATION` | not independently verified, intentionally unsupported, or deferred |
| `HISTORICAL` | prior evidence retained for context only |

## 15. Test method and evidence matrix

### Static gate

The static gate checks required files, links/anchors (including explicit `<a id>`
anchors), source references, the stable CSV, MISSING consistency, and canonical-only
scope. The six `docs-legacy/assessment-*.md` archive links now have local targets;
every local link and anchor must resolve.

### Wire gate

Fresh snapshot evidence recorded on 2026-09-04:

| target | result | source/evidence class |
|---|---|---|
| `test_spec_wire` | `392 PASS 0 FAIL 0 SKIP` | byte-lock vectors, `WIRE-ORACLE` |
| `test_wire_full` | `405 PASS 0 FAIL 0 SKIP` | complete Play matrix, `WIRE-ORACLE` |
| `test_wire_b6` | `133 PASS 0 FAIL` | login/settings/GUI/OP live shapes, `CAPTURED` |
| `test_scoreboard_reset` | `22 PASS 0 FAIL` | ResetScore round trips, `WIRE-ORACLE` |
| `test_fuzz` | `23 PASS 0 FAIL` | malformed frame/NBT/VarInt safety, `IMPLEMENTATION` |

The expected-failure policy does not apply to wire tests: any new wire FAIL blocks
publication.

### Gameplay gate

| target | recorded result | interpretation |
|---|---|---|
| `test_gameplay_full` | `803 PASS / 1 intentional E-14 FAIL / 804` (exit 1) | the one failure is intentional E-14; no other failure allowed |
| `test_smoke_80` | `212 PASS 0 FAIL` | base taxonomy plus extension checks |
| `test_seed_parity` | `201 PASS 0 FAIL` | L1/L2 deterministic evidence; vanilla RNG L3 remains declared |
| `test_mining_full` | `59/59 passed` | shared authoritative mining behavior |
| `test_block_hardness_full` | `16/16 passed; 1095 mismatch=0` | generated block table |
| `test_mob_stats_full` | `131 PASS 0 FAIL` | fixture/stat checks |
| `test_redstone_engine_full` | `29 PASS 0 FAIL` | engine categories |
| `test_recipes_mirror` | `76 PASS 0 FAIL` | recipe mirror/offset checks |
| `test_plan43` | `82 PASS 0 FAIL` in 25.01s | plan43 integration assertions |

The gameplay table does not claim exact vanilla behavior for an untested internal. A
new failure beyond E-14 is a publication blocker.

### JVM boundary gate

| target | recorded result | interpretation |
|---|---|---|
| `test_jvm_handles` | `PASS` | opaque handle invalidation/address-reuse and selective routing invariants |
| `jvm_manifest` | `PASS` | protocol-769 shadow ABI manifest is reproducible; 82 methods (52 native + 30 wrapper), 9 structured methods, 10 injection points, and 9 transformer names are declared |
| `jvm_runtime` | `PASS` | embedded HotSpot, fixture entrypoint, World API, command registration, lifecycle, selected Mixin hooks, and owned clean shutdown |
| `jvm_transformer` | `PASS` | pre-definition class-file transformation, verifier-safe stack/local preservation, and transform-order contract |
| `jvm_compatibility` | `PASS` | all 25 dependency-free fixture cases pass in one `cppfm` process |
| `jvm_corpus` | `PASS` | the 25-case compatibility corpus passes through the executable corpus harness |
| `jvm_contract_audit` | `PASS` | every declared ABI method has exactly one native or wrapper backend classification |
| official Loader/Knot probe | `PASS / DECLARED-LIMITATION` | pinned Loader 0.16.9/Knot/Mixin/ASM/intermediary starts with the shadow provider and emits all seven expected markers |
| locked real public-mod corpus | `SKIP / DECLARED-LIMITATION` | cache metadata verifies for Lithium, FerriteCore, and Carpet, but execution is not attempted because available Java 25 is outside the lock's Java 21 range |

This gate proves only the bounded plan51 compatibility layer and its pinned offline
official-loader probe. It does not prove Mojang GameProvider behavior, arbitrary mod
loading, universal JVM bytecode compatibility, client/GUI behavior, or protocol/RNG
parity.

### Operations gate

The following are required operational checks for a release candidate. The exact
final-gates results below are tied to the named baseline/run; a result without its
metadata and cleanup artifact is not a new claim.

| target/procedure | purpose | status at this document snapshot |
|---|---|---|
| `test_flood_net` / `test_recovery` / `test_rcon_multi` | frame, recovery, and RCON focused targets | `57/0`, `45/0`, and `6/0` respectively |
| `check_world` | offline NBT/world integrity | run-specific; no standalone run recorded here |
| view32 dry benchmark | 4,225 chunk load contract | `PASS` in 1.74s: p50 0.108 ms, p95 2.333 ms, peak RSS ~95 MB, hit rate 84.6% |
| stress 120 | concurrent connection load | `PASS` in 68.0s: 120/120 joined |
| multi-client integration | cross-client behavior | `ALL PASS` in 17.83s |
| bot smoke | short bot lifecycle | `ALL PASS` in 20.65s |
| `tests/soak_test.py --duration 300` | short synthetic stability | `PASS`: 150 keepalives, 0 disconnects, actions 2932, post-fill RSS growth 7.6% |
| `tests/soak_test.py --duration 600 --movement-range 3000` | wide synthetic stability | `PASS`: 300 keepalives, 0 disconnects, actions 5707, post-fill RSS growth 6.6% |
| `tools/soak_bot.py --duration 300` | extended bot stability | `3/3 PASS`: each KeepAlive 30, chunks 182, time updates 300, all error counters 0, cleanup PASS |
| `tests/soak_test.py --duration 1800 --movement-range 3000` | allocation-reuse diagnostic stability | `PASS` on `17ab09f`: 900 keepalives, 0 disconnects, actions 17493, post-fill baseline `114504kB`, max `128868kB`, growth `12.5%`; not a 2h/24h result |
| `tests/soak_test.py --duration 7200 --movement-range 3000` (parent `d1c6a7f`) | long-run stability attempt | interrupted at recorded `t=3361s`; post-fill RSS `160388→191612kB` (`+19.5%`), above the `15%` gate; not accepted |
| accepted soak 2 h/24 h | completed long-run artifact | none |
| real-client/GUI | manual client evidence | `DECLARED-LIMITATION`; no current artifact |

The former `soak_bot` failure is closed by three fresh integrated passes. The
7200-second soak attempt was interrupted above its RSS gate and is not a pass. The
canonical documentation keeps the E-14 expected failure, missing vanilla Xoroshiro
L3 proof and missing long-run/real-client artifacts explicitly limited.

### Reproducible commands

Run from the repository root. Every command is timeout-wrapped; the outer timeout is
intentional even when CTest has a per-test timeout.

```bash
timeout --foreground --kill-after=5 30 git status --short --untracked-files=all
timeout --foreground --kill-after=5 30 git diff --check
timeout --foreground --kill-after=5 30 test -s docs/README.md
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
timeout --foreground --kill-after=5 300 cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
timeout --foreground --kill-after=5 300 cmake --build build -j2
timeout --foreground --kill-after=5 30 ninja -C build
timeout --foreground --kill-after=5 90 cmake --build build --target cppfm_jvm_classes cppfm_jvm_fixture test_jvm_handles -j4
timeout --foreground --kill-after=5 90 python3 tests/jvm_runtime_smoke.py --binary ./build/cppfm --classes ./build/jvm/classes --mods ./build/jvm/fixture-mods
timeout --foreground --kill-after=5 90 ctest --test-dir build -R 'jvm_handles|jvm_manifest|jvm_runtime' --output-on-failure --timeout 60
timeout --foreground --kill-after=5 60 ./build/test_native ./build/cppfm
timeout --foreground --kill-after=5 30 ./build/test_scoreboard_reset
timeout --foreground --kill-after=5 30 ./build/test_spec_wire
timeout --foreground --kill-after=5 60 ./build/test_wire_full
timeout --foreground --kill-after=5 60 ./build/test_wire_b6
timeout --foreground --kill-after=5 30 ./build/test_fuzz
timeout --foreground --kill-after=5 60 ./build/test_gameplay_full  # expected exit 1: E-14 only
timeout --foreground --kill-after=5 120 ./build/test_seed_parity
timeout --foreground --kill-after=5 60 ./build/test_mining_full
timeout --foreground --kill-after=5 60 ./build/test_block_hardness_full
timeout --foreground --kill-after=5 30 ./build/test_mob_stats_full
timeout --foreground --kill-after=5 60 ./build/test_redstone_engine_full
timeout --foreground --kill-after=5 30 ./build/test_recipes_mirror
timeout --foreground --kill-after=5 300 ./build/test_plan43 ./build/cppfm
timeout --foreground --kill-after=5 450 ./build/test_smoke_80 ./build/cppfm
timeout --foreground --kill-after=5 60 python3 tools/bench_chunk_gen.py --view-distance 32 --chunks 4225 --dry --strict
timeout --foreground --kill-after=5 600 python3 tests/stress_test.py --clients 120 --binary ./build/cppfm
timeout --foreground --kill-after=5 400 python3 tests/soak_test.py --duration 300 --binary ./build/cppfm
timeout --foreground --kill-after=5 450 python3 tests/test_server_full.py --binary ./build/cppfm
timeout --foreground --kill-after=5 120 python3 tests/multi_client_test.py --binary ./build/cppfm
timeout --foreground --kill-after=5 120 python3 tests/bot_smoke.py --binary ./build/cppfm --duration 30
timeout --foreground --kill-after=5 400 python3 tools/soak_bot.py --duration 300 --binary ./build/cppfm
timeout --foreground --kill-after=5 120 ctest --test-dir build -R 'native|scoreboard_reset|spec_wire|plan43|flood_net|fuzz|wire_full|gameplay_full|seed_parity|block_hardness_full|redstone_engine_full|mob_stats_full|mining_full|weak_zero|bench|multi_client|bot_smoke|recipes_mirror|recovery|rcon_multi' --output-on-failure --timeout 120  # 20 tests; rc 8 from expected E-14
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
failure. The final-gates status is
`BLOCKED` only by declared E-14, L3, long-run, and real-client boundaries; E-14
remains the single intentional gameplay expected failure.

Rollback applies only to a migration commit owned by the operator. Use an explicit
inverse or `git revert` of that commit; never reset, checkout, delete broadly, or
discard unrelated user changes. If evidence is missing, retain the claim with
`DECLARED-LIMITATION`, record the missing artifact, and do not report the gate as
green.
