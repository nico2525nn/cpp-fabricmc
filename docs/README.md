# cpp-fabricmc canonical documentation

This directory is the current specification index for the clean-room C++ server. The
canonical snapshot is for **Minecraft Java Edition 1.21.4**, **protocol 769**, and
**DataVersion 4189**. It describes the behavior of the implementation baseline at
merge commit `f21e42327342fe1e8486960f2c43805711280ffd` as rechecked on 2026-09-04.
Documentation does not change the executable, packet registry, test assertions, or
generated data.

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
that path. It remains byte-identical to `docs-legacy/mob_stats_149.csv`, with SHA-256
`b75697102502385b6aee913f0aca80b86cce323a4994b16a29baf408b5ef2f6f` and `149` data
rows / `11` columns.

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

## Current measured evidence — final-gates snapshot

The following exact results were freshly recorded in the main checkout at the named
baseline. Counts are not inherited from stale prose and must not be inflated or
averaged to make a gate pass.

| command/target | result |
|---|---|
| configure/build | completed after a filesystem-slow initial 300s outer timeout; resumed build completed `104/104` |
| incremental Ninja build | `ninja: no work to do` in `0.05s` |
| `test_native` | `ALL PASS` in `2.33s` |
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
| `test_smoke_80` | `212 PASS 0 FAIL` |
| `test_gameplay_full` | `734 PASS / 1 intentional E-14 FAIL / 735`, exit 1 |
| `test_server_full` | `234 PASS 0 FAIL` in `273.79s` |
| `multi_client` | `ALL PASS` in `17.83s` |
| `bot_smoke` | `ALL PASS` in `20.65s` |
| view32 dry benchmark | `PASS` in `1.74s`; 4,225 chunks, p50 0.108 ms, p95 2.331 ms, peak RSS ~95 MB, hit rate 84.6% |
| 120-client stress | `120/120 joined; PASS` in `69.11s` |
| `tests/soak_test.py --duration 300` | `PASS` in `301.18s`; 150 keepalives, 0 disconnects, actions 2930, post-fill RSS growth 14.5% |
| `tools/soak_bot.py --duration 300` | `FAIL` in `301.89s`; keepAlives 3 (<7), kicks 0, chunks 182, time updates 23 |
| focused CTest | 20 tests excluding `soak2h`, `soak_bot`, and `smoke80`: 19 passed plus gameplay_full's intentional E-14 failure; rc 8 |
| accepted 2h/24h artifact | none |
| current real-client/GUI artifact | none |

`test_gameplay_full` is deliberately not changed to hide E-14. Publication remains
`BLOCKED` because `soak_bot` failed; the pass from `tests/soak_test.py` is not a
long-run substitute. See
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

The `Status` cells in `MISSING_FEATURES_1_21_4.md` retain the historical taxonomy
classification. `DONE` is not a promise that every unlisted Fabric JVM or future
protocol feature is supported; the declared limitations and final-gate failures
remain explicit.

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

The audit index at [audit/README.md](audit/README.md) contains history pointers for
assessment 1–6, and the archive files now exist under `docs-legacy/`; all six relative
history links resolve locally. Historical assessment text must not be treated as
current packet or gameplay authority.
