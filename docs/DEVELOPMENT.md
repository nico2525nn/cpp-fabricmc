# DEVELOPMENT — implementation and documentation workflow

This guide is for the clean-room C++ implementation of Minecraft Java 1.21.4,
protocol 769, DataVersion 4189. The source snapshot for this canonical document is
merge baseline `f21e42327342fe1e8486960f2c43805711280ffd` (2026-09-04). Fabric Loader
0.16.9 is a version/reference boundary; it is not embedded as a JVM runtime.

**Status:** development map and extension contract. **Limitations:** this file does
not grant permission to change runtime behavior, alter test assertions, claim
protocol 776 support, or implement Fabric JVM mods. Such work requires a separate
approved plan and fresh evidence.

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

## 2. Reference and clean-room provenance

Use public, version-pinned material only for the clean-room contract:

- Prismarine 1.21.4 protocol JSON for packet type/ID/field shape;
- Minecraft Wiki for supplemental encoding explanations;
- Fabric 1.21.4 release notes and Fabric Loader 0.16.9 documentation for the
  platform/version boundary; and
- Yarn 1.21.4 concept names, not unverified line-number or implementation claims.

Current source and executable tests outrank a stale comment. Every new claim needs a
source path/symbol, test or capture, provenance label, version boundary, and status.
Use `DECLARED-LIMITATION` when a claim has not been independently verified.

## 3. Classes and data structures

| layer | primary paths/symbols | contract |
|---|---|---|
| core | `src/core/ByteBuffer.hpp`, `NBT.*`, `Json.*`, `Zlib.hpp` | primitive, NBT, JSON, compression utilities |
| protocol | `src/proto/Ids.hpp` | protocol 769 state/direction constants |
| network | `src/net/Connection.hpp`, `PacketEncoder/Decoder`, `Crypto`, `PacketBatcher`, `RateLimiter`, `Rcon` | frame, encryption, batch, limits, administration |
| world | `src/game/World.hpp`, `WorldManager.hpp`, `ChunkCodec.hpp` | 24 sections, blocks, biomes, light and tickets |
| worldgen | `src/worldgen/`, `src/game/WorldGen.cpp` | density, climate, structures, placement |
| gameplay | `src/game/Entities`, `BehaviorTree`, `AiBrain`, `CombatManager`, `HungerManager` | entities, AI, damage, survival |
| data/UI | `Items`, `Containers`, `MenuInteraction`, `Recipes`, `DatapackManager`, `src/brigadier` | components, menus, recipes, commands |
| persistence | `WorldDataManager`, `Persistence`, `Anvil`, `RegionFile`, `SessionLock` | DataVersion 4189 and recovery |

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
`LevelChunkWithLight 0x28`, `UpdateLight 0x2B`, `KeepAlive 0x27`, `OpenScreen 0x35`,
`TradeList 0x2E`, `ContainerSetContent 0x13`, and MultiBlockChange
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
   one worktree.
5. Implement the smallest source-owned change, add focused evidence, then build.
6. Run static → unit → wire/gameplay → integration/ops gates.
7. Review behavior diff, docs references, and declared limitations before commit.

The final plan48 documentation refresh is restricted to the permitted canonical
Markdown files; it does not archive legacy docs, move the CSV, or change
source/tests/tools/CMake.

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
`4526dfe4f7112b1fe744a83484ef5ef40176d481` (self-safe `cppfm --por[t]` cleanup in
`test_server_full.py` and `replay_vanilla.py`). A future plan must not re-propose
those completed refactors as documentation work.

## 10. Module split and ownership rules

| concern | owner | cross-link |
|---|---|---|
| byte/ID/field encoding | `src/core`, `src/proto`, `src/net` | WIRE |
| gameplay cause/effect | `src/game`, `src/physics`, `src/worldgen` | GAMEPLAY |
| limits/save/recovery/load | `src/game` persistence + `src/net` limits + tools | OPS |
| extension workflow | this guide | all three specs |
| evidence and release gate | `tests`, `CMakeLists.txt`, tools | VERIFICATION |

One claim has one canonical owner. A link is preferable to a copied table.

## 11. Cautions

- Preserve MISSING numbering: base 80 plus extension 10 equals 90 numbered rows.
- Never copy old IDs, HEADs, PASS counts, or line-only citations without rechecking
  the current source and test.
- Keep `docs/mob_stats_149.csv` at its stable default runtime path.
- Do not change `CHECK(false, "E-14 HONEST GAP...")` to make a test exit zero.
- Do not call protocol 776 Bundle item, JVM mod execution, or vanilla RNG L3 parity
  “supported” without a new versioned contract and evidence.
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
- E-14, protocol 776, seed RNG L3, and any test with an intentional allowed failure.

## 15. Test method

Use the CMake/CTest registration as authority. Representative commands (all
timeout-wrapped) are listed in [VERIFICATION.md#reproducible-commands](VERIFICATION.md#reproducible-commands).
At minimum, a source extension should add or update a focused test before relying on
`test_native` or `test_smoke_80` as evidence.

Required evidence classes are:

1. static source/schema/link checks;
2. configure/build;
3. focused unit/wire/fixture tests;
4. integration/server tests; and
5. operational recovery/limit/load evidence.

## 16. Priority and status

**Priority: high.** Preserve the current contracts before adding breadth. A feature
is not current merely because a class or enum exists; it needs a source path, an
observable behavior, evidence, and a truthful status. If evidence is missing, use
`DECLARED-LIMITATION` and schedule research rather than weakening the gate.
