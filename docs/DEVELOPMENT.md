# DEVELOPMENT — implementation and documentation workflow

This guide is for the clean-room C++ implementation of Minecraft Java 1.21.4,
protocol 769, DataVersion 4189. The source snapshot for this canonical document is
the current working tree (2026-09-19, integrated HEAD `335fca5`). Fabric Loader 0.16.9 is a
version/reference boundary; the executable provides a default-on bounded embedded
JVM, a version-locked class-file transformer, and a separate offline official
Loader/Knot probe. The production path does not ship the Mojang GameProvider/server
jar.

**Status:** development map and extension contract. **Limitations:** this file does
not grant permission to change runtime behavior, alter test assertions, or expand the
plan51 JVM boundary. Further Fabric API, transformer coverage, or arbitrary-mod work
requires a separately versioned plan and fresh evidence.

## 1. Feature overview

Development has five responsibilities:

1. preserve the current wire/gameplay/operations contracts;
2. keep source/data/generated inputs distinguishable;
3. provide a safe research → implementation → test → review workflow;
4. make extension points discoverable without inventing a second registry; and
5. record limitations and evidence honestly against MISSING **#1–#90** and Fabric
   rows.

The canonical specification owners are [SPEC_WIRE.md](SPEC_WIRE.md),
[SPEC_GAMEPLAY.md](SPEC_GAMEPLAY.md), [SPEC_OPS.md](SPEC_OPS.md), and
[VERIFICATION.md](VERIFICATION.md).

The legacy entry point [research-prompt.md](research-prompt.md) has one
responsibility: redirect readers to this guide's canonical
[research workflow](DEVELOPMENT.md#research-workflow). It is not a second workflow,
plan format, or evidence authority. Current research plans and canonical documents
use the sixteen viewpoints in §§1–16; an older ``13 viewpoints`` label is historical
wording and is not a current acceptance count.

## 2. Reference and clean-room provenance

Use public, version-pinned material only for the clean-room contract:

- Prismarine 1.21.4 protocol JSON for packet IDs and ordinary field shapes;
- Minecraft Wiki for supplemental encoding explanations;
- Fabric 1.21.4 release notes and Fabric Loader 0.16.9 documentation for the
  platform/version boundary; and
- [Fabric developer documentation](https://docs.fabricmc.net/develop/index) and
  Fabric API `0.119.4+1.21.4` for the public server/common extension surface; and
- Yarn `1.21.4+build.8` mappings/Javadocs for versioned names and descriptors, not
  unverified line-number or implementation claims. Client, datagen, renderer, and
  internal-only API surfaces are audited separately from the server boundary.

When a version-pinned community table disagrees with the official client model,
the versioned Yarn contract and a real-client decode take precedence. The current
1.21.4 `teleport_entity` entry is one such exception: use the
`EntityPositionS2CPacket`/`PlayerPosition` layout recorded in
[SPEC_WIRE](SPEC_WIRE.md#packet-contract-table), not the stale short entry in
the raw Prismarine JSON.

Current source and executable tests outrank a stale comment. Every new claim needs a
source path/symbol, test or capture, provenance label, version boundary, and status.
Use `DECLARED-LIMITATION` when a claim has not been independently verified.

## 3. Classes and data structures

| layer | primary paths/symbols | contract |
|---|---|---|
| core | `src/core/ByteBuffer.hpp`, `NBT.*`, `Json.*`, `Zlib.hpp` | primitive, NBT, JSON, compression utilities |
| protocol | `src/proto/Ids.hpp` | protocol 769 state/direction constants |
| network | `src/net/Connection.hpp`, `PacketEncoder/Decoder`, `Crypto`, `PacketBatcher`, `RateLimiter`, `Rcon` | frame, encryption, batch, limits, administration |
| world | `src/game/World.hpp`, `ChunkTicket.hpp`, `ChunkCodec.hpp` | 24 sections, blocks, biomes, light and tickets |
| worldgen | `src/worldgen/`, `src/game/WorldGen.cpp` | density, climate, structures, placement |
| gameplay | `src/game/Entities`, `BehaviorTree`, `AiBrain`, `CombatManager`, `HungerManager` | entities, AI, damage, survival |
| data/UI | `Items`, `Containers`, `MenuInteraction`, `Recipes`, `DatapackManager`, `src/brigadier` | components, menus, recipes, commands |
| persistence | `WorldDataManager`, `Persistence`, `Anvil`, `RegionFile`, `SessionLock` | DataVersion 4189 and recovery |
| configuration | `src/game/ServerConfig.*`, `ServerProperties.hpp`, `src/net/RconConfig.hpp`, `src/main.cpp` | defaults, typed properties/CLI precedence, diagnostics, and RCON settings |
| JVM boundary | `src/jvm/`, `jvm/java/`, `jvm/shadow_api.json`, `jvm/vendor/` | default-on JNI/HotSpot bridge, structural transformer, and pinned official-loader probe; [PLAN51_JVM.md](PLAN51_JVM.md) |
| distribution/runtime layout | `src/core/RuntimeLayout.*`, `tools/embed_runtime.py`, `cmake/verify_self_contained_package.cmake.in`, `tests/package_jvm_smoke.py`, `CMakeLists.txt` | embeds repository-owned assets/classes in the executable, fail-closes the one-file CPack package when its resource pack is unavailable, creates the server directory tree, and separately verifies the package JVM boundary |

Generated IDs under `src/generated/` and assets under `assets/` are inputs, not
handwritten canonical tables.

## 4. Packets and public contracts

When an extension emits or consumes bytes, update the WIRE contract first:

1. verify the 1.21.4 JSON ID and state/direction;
2. add a field-order/type vector to `tests/test_spec_wire.cpp` or the appropriate
   exhaustive test;
3. route it through the existing `PacketEncoder`, `PacketDecoder`, `Connection`, or
   session path; and
4. document gameplay cause in GAMEPLAY and operational limits in OPS without copying
   the field table.

Do not add a parallel constexpr ID registry. In particular, preserve
`LevelChunkWithLight 0x28`, `UpdateLight 0x2B`, `KeepAlive S→C 0x27`,
`KeepAlive C→S 0x1A`, `OpenScreen 0x35`, `TradeList 0x2E`,
`ContainerSetContent 0x13`, and MultiBlockChange
`(state << 12) | (x << 8) | (z << 4) | y`.

## 5. Events and review checkpoints

Implementation changes are reviewed at these boundaries:

| checkpoint | required artifact |
|---|---|
| research | version, sources, MISSING target, and limitation note |
| source edit | path/symbol and ownership decision |
| packet/gameplay event | test vector or behavior test |
| persistence/ops event | recovery/limit/cleanup test or run record |
| review | diff scope, `git diff --check`, and no unrelated user-change loss |

The C++ block/event hooks in `World` and tick/session callbacks are implementation
events; they do not imply a Fabric event bus.

### Current hardening checkpoint

The current working-tree verification records the following implementation
contracts:

- `SimulationDispatchGuard` owns all mutating tick/session/console transitions.
  `Connection` encodes frames while that gate is held, then queues them to one
  bounded writer thread per connection; it does not unlock the simulation gate
  around a state mutation. Queues are capped at 4 MiB per client, and graceful
  teardown waits at most 100 ms for a short protocol tail before closing.
- Block place/break/click callbacks run before the world mutation and can cancel
  it. Scoped event removal waits for in-flight callbacks. Item command sources
  retain the originating dimension.
- Persistence snapshot barriers flush source/destination pending piston commits
  under the simulation gate, including background persistence workers. Moving-piston
  transient NBT remains intentionally omitted; the barrier materializes the commit
  before the snapshot instead.
- Secure profile rejection is terminal when either secure-profile or enforced
  secure-chat policy requires it. Signed command argument transcripts are not
  reconstructed and therefore fail closed instead of dispatching.

The 2026-09-19 final-gate baseline recorded `test_settings_matrix` (27/27),
`test_properties` (33/33), `test_recovery` (55/55), `test_core_safety` (45/45),
Plan43 (87/87), smoke80 (224/224), the full live matrix (240/240), and the
non-nightly CTest aggregate (46/46). The 120-client stress, 300-second soak,
and strict view-distance-32 dry benchmark also pass. Review artifacts and exact
commands are recorded in `docs/audit/adversarial-review-2026-09-19.md`.

## 6. State model

```text
research-only → approved plan → one writer/worktree → implementation
              → focused test → full gate → review → merge
```

For a docs-only migration:

```text
snapshot → canonical draft → static/link/fixture checks → build/test evidence
         → scope review → commit
```

A failed state cannot be promoted by changing prose, counts, or assertions.

<a id="research-workflow"></a>
## 7. Implementation flow

1. Read `AGENTS.md`, `docs/CURRENT_STATE.md`, the current gap matrix, and the
   applicable canonical contract.
2. Reconfirm current HEAD and any pre-existing user changes.
3. Research externally without writing source during research.
4. Split independent work only with separate worktrees; never place two writers in
   one worktree. Temporary worktrees are optional; when one is created, remove that
   exact worktree after the task only after confirming it is clean, and preserve any
   dirty worktree for its owner.
5. Implement the smallest source-owned change, add focused evidence, then build.
6. Run static → unit → wire/gameplay → integration/ops gates.
7. Reproduce `.github/workflows/ci.yml` locally for every PR: configure a fresh
   Ninja build, compile all targets, run `git diff --check`, run the focused
   settings/properties/recovery/core-safety gates, and run the non-nightly CTest
   aggregate. Attach those logs plus the independent feature/quality review
   artifact before merge.
8. Review behavior diff, docs references, and declared limitations before commit.

For a research plan, put the applicable MISSING target at the start of each chapter
and cover all sixteen viewpoints: feature overview, vanilla/reference specification,
classes and data structures, packets or observable I/O, events/checkpoints, state
transitions, implementation/reproduction flow, C++ design example, class/source
composition, module split and ownership, cautions, performance, thread safety,
edge cases, test method, and implementation priority/status. A plan that says
``13 viewpoints`` is using the old schema and must not be treated as complete.

The plan50 runtime follow-up snapshot was the previous baseline. Plan51 is the
authorized JVM-boundary implementation described in
[PLAN51_JVM.md](PLAN51_JVM.md); its focused evidence does not promote a failed gate
or convert the bounded fixture into arbitrary Fabric compatibility.

The authorized `AGENTS.md` handover update now points to this canonical workflow,
the current 16-viewpoint research schema, protocol-769 packet IDs, and timeout-safe
process cleanup. Use this guide, the WIRE contract, and CURRENT_STATE for new work;
the handover remains an entrypoint rather than a second specification.

## 8. C++ design example

```cpp
// Documentation-only ownership record; not a new runtime type.
struct ContractEvidence {
    std::string missingTarget;
    std::string sourcePathAndSymbol;
    std::string testOrCapture;
    std::string provenance;
    std::string status;
};
```

For a real feature, the preferred record is a Markdown table row using that same
shape. Do not build a runtime reflection system solely to make the documentation
look structured.

## 9. Class/source composition

The current source map is intentionally modular:

```text
core → proto → net → game/session
                  ├→ world / physics / worldgen
                  ├→ entities / combat / survival
                  └→ inventory / commands / persistence
generated/assets → game and protocol data consumers
tests/tools       → evidence and operational harnesses
```

The plan47 cleanup already split command/session helpers and removed dead code. The
merge baseline also records `db12df96093a0869e958f62b11f9a9cd68ba3ef1` (legacy
Structures API removed, 10 files, +22/-787, source/test legacy-reference grep 0) and
`4526dfe4f7112b1fe744a83484ef5ef40176d481` (self-nonmatching cleanup pattern in
`replay_vanilla.py`). The current `replay_vanilla.py`, `test_server_full.py`, and
`run_plan43_suite.py` use PID-scoped cleanup as the follow-up ownership rule. A
future plan must not re-propose those completed refactors as documentation work.
Plan54 additionally records a bounded cleanup pass: item comparisons, owned flood
process setup, login rejection paths, nested command sources, and JNI bridge
boundary helpers were consolidated without changing fixtures or wire vectors. The
overall mutable ledger is still `PARTIAL`, because new configuration and lifecycle
evidence outweigh those deletions.

## 10. Module split and ownership rules

| concern | owner | cross-link |
|---|---|---|
| byte/ID/field encoding | `src/core`, `src/proto`, `src/net` | WIRE |
| gameplay cause/effect | `src/game`, `src/physics`, `src/worldgen` | GAMEPLAY |
| limits/save/recovery/load | `src/game` persistence + `src/net` limits + tools | OPS |
| extension workflow | this guide | all three specs |
| JVM linkage/runtime diagnostics | `tools/scan_mod_linkage.py`, `tools/compare_real_mod_corpus.py`, `tests/real_mod_corpus/` | VERIFICATION |
| evidence and release gate | `tests`, `CMakeLists.txt`, tools | VERIFICATION |

The `GameServer` implementation is intentionally split by ownership.  Use the
following map when tracing a current source path; older matrix entries may
still contain pre-split `GameServer.cpp:<line>` citations as historical
evidence.

| current concern | current source owner |
|---|---|
| lifecycle, server-thread dispatch, JVM/entity hooks, advancements, cookies | `src/game/GameServer_core.cpp` |
| tick scheduling, simulation distance, survival, mob spawning, block batches | `src/game/GameServer_tick.cpp` |
| combat, effects, weather, particles, TNT, lightning, movement broadcasts | `src/game/GameServer_combat.cpp` |
| items, containers, vehicles, trades, projectiles, XP, mob/object ticking | `src/game/GameServer_items.cpp` |
| world persistence, player data, operators, bans, whitelist, world border | `src/game/GameServer_world.cpp` |
| protocol sessions, handshake/login/configuration/play packet handlers | `src/game/GameServer_session.cpp` |

One claim has one canonical owner. A link is preferable to a copied table.

## 11. Cautions

- Preserve MISSING numbering: base 80 plus extension 10 equals 90 numbered rows.
- Keep the historical assessment-1 strict-audit label of 78 gaps separate from the
  current 90-row taxonomy and its historical `DONE=90` count; neither is a current
  release sign-off by itself.
- Never copy old IDs, HEADs, PASS counts, or line-only citations without rechecking
  the current source and test.
- Keep `docs/mob_stats_149.csv` at its stable default runtime path.
- Do not encode the E-14 boundary as an intentional failing assertion or alter an
  assertion to make a test exit zero.
- Do not call arbitrary JVM mod execution, Mojang GameProvider execution, or full
  vanilla world-generation RNG L3 parity “supported” without a new versioned
  contract and evidence. `test_rng_parity` covers the primitive/splitter contract;
  the official Loader/Knot result is an offline probe against the shadow provider.
- Do not use broad process-kill patterns in development or test cleanup.

## 12. Performance

Development performance work must measure before changing:

- configure/build elapsed time;
- chunk generation at the declared view-distance workload;
- MSPT/TPS/RSS under entities, redstone, connections, and persistence; and
- static checker time and documentation scope.

Performance changes are source changes, not a reason to edit a specification number.
Record commit, host, options, warm-up, sample count, and run ID in OPS/VERIFICATION.

## 13. Thread safety

The code's existing model is part of the extension contract:

- shared world reads use shared locking and writes use unique locking;
- entity and world lock ownership must not be inverted through callbacks;
- connection writes are serialized by `Connection::tx_`;
- persistence and RCON workers have explicit shutdown ownership; and
- docs generation is single-writer even when read-only research is parallel.

An extension proposal that changes thread ownership needs a separate concurrency
review and stress evidence.

## 14. Edge cases

Review explicitly for:

- signed VarInt/VarLong, negative coordinates and Y, optional NBT and UUID forms;
- single-palette zero longs, 24 chunk sections, 64 biome entries, and live registry
  size changes;
- unloaded/forced/spawn chunks, cross-chunk light and simulation culling;
- component IDs and empty/removed component lists;
- malformed/oversize frames, zlib trailing bytes, slow peers, RCON auth flood;
- corrupt level/region/player files, stale locks, child process orphans; and
- E-14 boundary, full world-generation seed RNG L3, and any deliberately non-gating diagnostic.

## 15. Test method

Use the CMake/CTest registration as authority. Representative commands (all
timeout-wrapped) are listed in [VERIFICATION.md#reproducible-commands](VERIFICATION.md#reproducible-commands).
At minimum, a source extension should add or update a focused test before relying on
`test_native` or `test_smoke_80` as evidence.

Published counts are named snapshots from [CURRENT_STATE.md](CURRENT_STATE.md) and
the corresponding run output. Do not import the old `test_spec_wire` count of 328;
the current snapshot is 417, and `test_native` is recorded as `ALL PASS` without
inventing an aggregate count.

Required evidence classes are:

1. static source/schema/link checks, including the conservative JVM class-file
   linkage preflight and fail-closed process-diagnostic classifier;
2. configure/build;
3. focused unit/wire/fixture tests;
4. integration/server tests; and
5. operational recovery/limit/load evidence.

## 16. Priority and status

**Priority: high.** Preserve the current contracts before adding breadth. A feature
is not current merely because a class or enum exists; it needs a source path, an
observable behavior, evidence, and a truthful status. If evidence is missing, use
`DECLARED-LIMITATION` and schedule research rather than weakening the gate.
