# cpp-fabricmc canonical documentation

This directory is the current specification index for the clean-room C++ server. The
canonical snapshot is for **Minecraft Java Edition 1.21.4**, **protocol 769**, and
**DataVersion 4189**. It describes the behavior of the implementation at commit
`f5987c585e81afdd78adb3ad818a0e43b1697bbe` as rechecked on 2026-09-04. Documentation
does not change the executable, packet registry, test assertions, or generated data.

## Version boundary and scope

| item | canonical value | boundary |
|---|---|---|
| Minecraft | `1.21.4` | Java Edition protocol behavior |
| protocol | `769` | Handshake, state/direction IDs, and field encodings |
| world data | `4189` | `level.dat`, Anvil, and chunk persistence |
| reference platform | Fabric Loader `0.16.9`, unmodded server behavior | Fabric API is provenance, not an in-process JVM runtime |
| excluded | 1.21.5 / protocol `776`, JVM Fabric mods, proven vanilla Xoroshiro byte parity | tracked as declared limitations, not silently supported |

`Fabric-compatible` means compatible with the protocol and observable behavior of an
unmodified Fabric 1.21.4 server. It does **not** mean that arbitrary Fabric JVM
bytecode can run inside `cppfm`.

## Read in this order

1. [SPEC_WIRE.md](SPEC_WIRE.md) — byte-level protocol and state machine.
2. [SPEC_GAMEPLAY.md](SPEC_GAMEPLAY.md) — world, blocks, entities, inventory,
   commands, combat, and the explicit parity boundary.
3. [SPEC_OPS.md](SPEC_OPS.md) — limits, persistence, recovery, load, and incident
   procedures.
4. [DEVELOPMENT.md](DEVELOPMENT.md) — module ownership, clean-room workflow, and
   extension rules.
5. [VERIFICATION.md](VERIFICATION.md) — reproducible gates and evidence semantics.

The existing [gap/status matrix](MISSING_FEATURES_1_21_4.md) and
[dynamic tracker](CURRENT_STATE.md) retain their stable paths. They are inputs to
the canonical documents, not replacements for the detailed contracts above. The
machine-readable fixture remains at [docs/mob_stats_149.csv](mob_stats_149.csv);
the default lookup in `tests/test_mob_stats_full.cpp::csvPath` must continue to find
that path.

## Evidence labels

Every normative statement in the canonical specifications should be read with one of
these provenance labels:

| label | meaning |
|---|---|
| `WIRE-ORACLE` | 1.21.4 protocol data plus a byte-lock test vector |
| `CAPTURED` | observation from a reference server/client exchange |
| `VANILLA-CONCEPT` | concept or rule cross-checked against Yarn, Fabric documentation, or the Minecraft Wiki |
| `IMPLEMENTATION` | a fact about the current C++ source at the snapshot commit |
| `DECLARED-LIMITATION` | an intentional difference or a claim not yet independently verified |
| `HISTORICAL` | an archived audit or old observation; never current authority by itself |

The source-of-truth rule is simple: current definitions and fresh tests outrank stale
comments or historical prose. In particular, current `src/proto/Ids.hpp` and the
wire tests establish `LevelChunkWithLight 0x28`, `UpdateLight 0x2B`, `KeepAlive
0x27`, `OpenScreen 0x35`, `TradeList 0x2E`, `ContainerSetContent 0x13`, and the
MultiBlockChange packing documented in [SPEC_WIRE.md](SPEC_WIRE.md#bundle-and-block-updates).

## Server lifecycle

```text
HANDSHAKING
  ├─ Status intention → STATUS → request/ping → response/pong → disconnect
  └─ Login intention  → LOGIN → (optional encryption/compression)
                              → CONFIGURATION → known-packs response
                              → registries/tags/finish
                              → PLAY → tick, packets, persistence
                              → DISCONNECT
```

Operational publication has a separate lifecycle:

```text
preflight → evidence snapshot → canonical review → static/link gate
          → build/test gate → scope review → commit
```

Failure stops publication; it does not turn an unverified claim into `DONE`.

## Source and domain map

| domain | primary implementation paths | canonical owner |
|---|---|---|
| encoding and IDs | `src/core/ByteBuffer.hpp`, `src/proto/Ids.hpp` | [SPEC_WIRE.md](SPEC_WIRE.md) |
| connection and packets | `src/net/Connection.hpp`, `PacketEncoder.hpp`, `PacketDecoder.hpp`, `PacketBatcher.cpp` | [SPEC_WIRE.md](SPEC_WIRE.md) |
| world and persistence | `src/game/World.hpp`, `WorldDataManager.*`, `Persistence.hpp`, `src/game/RegionFile.hpp` | [SPEC_GAMEPLAY.md](SPEC_GAMEPLAY.md), [SPEC_OPS.md](SPEC_OPS.md) |
| physics and world generation | `src/physics/`, `src/worldgen/`, `src/game/WorldGen.cpp` | [SPEC_GAMEPLAY.md](SPEC_GAMEPLAY.md) |
| entities and inventory | `src/game/Entities.hpp`, `Items.hpp`, `Containers.hpp`, `Recipes.*`, `BehaviorTree.*` | [SPEC_GAMEPLAY.md](SPEC_GAMEPLAY.md) |
| limits and administration | `src/net/RateLimiter.hpp`, `src/net/Rcon.hpp`, `src/game/SessionLock.hpp`, `tools/`, `tests/` | [SPEC_OPS.md](SPEC_OPS.md) |
| build and evidence | `CMakeLists.txt`, `tests/`, `tools/` | [DEVELOPMENT.md](DEVELOPMENT.md), [VERIFICATION.md](VERIFICATION.md) |

This is a documentation map, not a new C++ class hierarchy or a second packet-ID
registry.

## Current measured evidence

The following results were freshly recorded for this canonical snapshot. Counts are
not inherited from the stale `CURRENT_STATE.md` prose and must not be inflated to
make a gate pass.

| command/target | result |
|---|---|
| configure, Ninja build (`-j2`) | completed successfully |
| `test_spec_wire` | `392 PASS 0 FAIL 0 SKIP` |
| `test_wire_full` | `405 PASS 0 FAIL 0 SKIP` |
| `test_wire_b6` | `133 PASS 0 FAIL` |
| `test_scoreboard_reset` | `22 PASS 0 FAIL` |
| `test_fuzz` | `23 PASS 0 FAIL` |
| `test_mob_stats_full` | `131 PASS 0 FAIL` |
| `test_block_hardness_full` | `16/16 passed; 1095 mismatch=0` |
| `test_mining_full` | `38/38 passed` |
| `test_redstone_engine_full` | `29 PASS 0 FAIL` |
| `test_seed_parity` | `201 PASS 0 FAIL` |
| `test_recipes_mirror` | `76 PASS 0 FAIL` |
| `test_plan43` | `82 PASS 0 FAIL` |
| `test_native` | all displayed checks passed; this binary prints no aggregate total |
| `test_smoke_80` | `212 PASS 0 FAIL` |
| `test_gameplay_full` | `734 PASS 1 FAIL / 735`; the one failure is the intentional E-14 Fabric JVM-mod gap |
| `test_flood_net` | `57 PASS 0 FAIL` |
| `test_recovery` | `45 PASS 0 FAIL` |
| `test_rcon_multi` | `6 PASS 0 FAIL` |
| view32 dry benchmark | `PASS`; 4,225 chunks, p50 0.108 ms, p95 2.332 ms, peak RSS ~95 MB, hit rate 84.6% |
| 120-client stress | `120/120 joined; PASS` |
| 60-second soak | `PASS`; 30 keepalives, 0 disconnects, post-fill RSS growth 0.7% |
| 300-second soak | `FAIL` on this run (31 keepalives, 5 disconnects); long-run status remains `DECLARED-LIMITATION` |

`test_gameplay_full` is deliberately not changed to hide E-14. See
[SPEC_GAMEPLAY.md#declared-limitations](SPEC_GAMEPLAY.md#declared-limitations) and
[VERIFICATION.md#gameplay-gate](VERIFICATION.md#gameplay-gate).

## Gap-number convention

The existing matrix has **base taxonomy #1–#80** plus **extension rows #81–#90**;
there are 90 numbered rows without renumbering the original taxonomy. The canonical
owners cite the original numbers:

| range | primary document |
|---|---|
| #1–#70, #80–#90 | [SPEC_GAMEPLAY.md](SPEC_GAMEPLAY.md) |
| #71–#79 and packet columns of related rows | [SPEC_WIRE.md](SPEC_WIRE.md) |
| #7–#10, #71–#79 operational controls and Fabric server-property rows | [SPEC_OPS.md](SPEC_OPS.md) |
| test/audit meaning for all rows | [VERIFICATION.md](VERIFICATION.md) |

The `Status` cells in `MISSING_FEATURES_1_21_4.md` are not rewritten by this
canonical-doc commit. `DONE` is not a promise that every unlisted Fabric JVM or
future protocol feature is supported; the declared limitations remain explicit.

## Plan48 viewpoint coverage

The five detailed canonical documents each use the same sixteen viewpoint sections,
so a claim can be reviewed consistently:

| # | viewpoint | owner sections |
|---:|---|---|
| 1 | feature overview | each document §1 |
| 2 | vanilla/reference specification | each document §2 |
| 3 | classes and data structures | each document §3 |
| 4 | packets or observable I/O | each document §4 |
| 5 | events/checkpoints | each document §5 |
| 6 | state transitions | each document §6 |
| 7 | implementation/reproduction flow | each document §7 |
| 8 | C++ design example | each document §8 |
| 9 | class/source composition | each document §9 |
| 10 | module split and ownership | each document §10 |
| 11 | cautions | each document §11 |
| 12 | performance | each document §12 |
| 13 | thread safety | each document §13 |
| 14 | edge cases | each document §14 |
| 15 | test method | each document §15 |
| 16 | implementation priority/status | each document §16 |

## History boundary

The audit index at [audit/README.md](audit/README.md) contains only history pointers
for assessment 1–6. The archive paths are intentionally not created or moved by
this canonical-only change; a separate archive migration must create them before
those six relative history links can resolve locally. Historical assessment text
must not be treated as current packet or gameplay authority.
