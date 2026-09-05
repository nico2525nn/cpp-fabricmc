# SPEC_OPS — operations, limits, and recovery

This is the operational contract for Minecraft 1.21.4 / protocol 769 / DataVersion
4189 at runtime snapshot `74bd5ffbda03f30bc5af0c96a11ec3416bf1a827`, rechecked on
2026-09-05. It covers MISSING **#7–#10**, operational aspects of **#71–#79**,
**#88–#90**, Fabric server-property/RCON rows, and assessment history IDs
B-06/B-07/C-04/C-09/C-12/E-13/O-01–O-13/W-14/W-16.

**Status:** current runbook and declared-budget contract. **Limitations:** thresholds
are implementation safety budgets unless marked as a vanilla observable; a dry or
synthetic run is not a real-client/24-hour result. Plan51's optional embedded JVM is a
bounded compatibility layer with a structural transformer; its separate offline
official Loader/Knot/Mixin probe does not ship the Mojang GameProvider, and arbitrary
Fabric JVM mods remain outside the platform boundary. See [PLAN51_JVM.md](PLAN51_JVM.md).

## 1. Feature overview

The operations specification owns:

- startup/shutdown, `session.lock`, world save and backup/restore;
- frame, decompression, bandwidth, accept, chat-spam, and slow-peer limits;
- performance/load budgets, cache/generation measurements, and long-run soak;
- RCON/whitelist isolation, GUI/manual evidence, and incident response; and
- evidence records, cleanup ownership, and rollback conditions.

Packet field bytes are in [SPEC_WIRE.md](SPEC_WIRE.md), while game causes are in
[SPEC_GAMEPLAY.md](SPEC_GAMEPLAY.md).

## 2. Vanilla/operational reference

| source | use | label |
|---|---|---|
| `src/game/WorldDataManager.*`, `Persistence.hpp`, `SessionLock.hpp` | current persistence behavior | `IMPLEMENTATION` |
| `src/net/PacketDecoder.hpp`, `Connection.hpp`, `RateLimiter.hpp` | current safety limits | `IMPLEMENTATION` |
| vanilla 1.21.4/Yarn/Wiki concepts | save, tick, keepalive, and protocol semantics | `VANILLA-CONCEPT` |
| `tests/test_flood_net.cpp`, `test_recovery.cpp`, `test_rcon_multi.cpp` | executable operational evidence | `CAPTURED`/`IMPLEMENTATION` |
| `tests/soak_test.py`, `tools/bench_chunk_gen.py`, GUI/replay procedures | measurement procedures | `DECLARED-LIMITATION` until a run record exists |

The version boundary is not negotiable: `DataVersion=4189`, one world `level.dat` in
the root layout for this target, 20 TPS scheduling, and Play KeepAlive `0x27` are
1.21.4 facts. `SPEC_OPS` does not turn a chosen safety threshold into a claim about
all vanilla servers.

## 3. Classes and operational data

| area | implementation path/symbol | observable metric/state | evidence/status |
|---|---|---|---|
| save | `WorldDataManager::atomicWrite`, `Persistence::saveLevelData` | `.new`, `level.dat_old`, atomic rename | `test_recovery`; `IMPLEMENTATION` |
| recovery | `WorldDataManager::loadWithRecovery`, `tryLoadFile` | source `Dat`, `DatOld`, or `Fresh`; log lines/quarantine | `test_recovery`; source-backed |
| chunks | `RegionFile`, `Persistence::loadChunk`, `GameServer::saveChunkAsync`, `chunksUnloadTick` | corrupt entry isolated/regenerated; dirty snapshots saved asynchronously before configured-radius eviction | recovery matrix + wide soak |
| session ownership | `SessionLock::acquire/release` | PID/timestamp, live/stale warning | recovery matrix |
| frames | `Connection::readFrame`, `PacketDecoder::decodeFrame` | frame/declaration sizes and rejection | `test_flood_net` |
| throttles | `RateLimiter`, `SpamTracker`, `AcceptGate` | tokens, chat score, accepted/shed connections | flood tests |
| RCON | `src/net/Rcon.hpp::RconServer` | local listener, authentication, handler response | `test_rcon_multi` |
| measurement | `tests/stress_test.py`, `tests/soak_test.py`, `tools/bench_chunk_gen.py` | MSPT/TPS/RSS, queue depth, integrity | run ID required |
| JVM startup/cleanup | `src/jvm/JvmRuntime.*`, `tests/jvm_runtime_smoke.py` | VM start, owned process, clean shutdown | `jvm_runtime`; bounded fixture only |

## 4. Packet-facing operations

OPS never duplicates packet field tables. Its packet-facing contract is response
policy:

<a id="rate-limits-and-disconnect-policy"></a>
### Rate limits and disconnect policy

- oversize, forged, malformed, or timed-out input is rejected with the state-correct
  Disconnect path where the session can send one;
- Play KeepAlive is sent as `0x27` every 10 seconds and a pending unanswered ID is
  timed out after 30 seconds; an idle sweep is 60 seconds;
- Login/configuration waits and unknown-packet handling follow
  [SPEC_WIRE.md#state-transitions](SPEC_WIRE.md#state-transitions);
- RCON commands are handled outside the game tick and do not block packet encoding;
  and
- backup, recovery, rate, and soak success are separate evidence classes from a
  byte-identical packet test.

## 5. Events and checkpoints

| event | required observation |
|---|---|
| startup | properties parse, `session.lock`, level source, registry/data load, and warning logs |
| runtime | tick budget, periodic save, chunk unload, KeepAlive, RCON/admin action |
| incident | oversize/flood/slow peer, malformed frame, crash, corrupt level/chunk/player file |
| shutdown | stop flag/signal, tick flush, persistence flush, lock removal, owned child cleanup |
| verification | static gate, dry load, stress/soak checkpoint, manual GUI capture, report metadata |

Do not call an event “verified” without a source symbol and evidence artifact.

## 6. State transitions

<a id="backup-restore-and-recovery"></a>
### Backup and recovery

```text
level.dat valid → normal boot
level.dat bad + level.dat_old valid → quarantine bad → boot old
both bad → quarantine available files → fresh world + warning
chunk bad → isolate/quarantine chunk → regenerate that chunk
playerdata bad → quarantine file → fresh player state
```

### Backup procedure

```text
save-all/flush → copy world and administrator files
               → offline check_world → verify exit code and checksum
```

Restore is `stop → move live directory aside → place copy → check_world → boot →
verify recovery log`. A 300-second dry soak, nightly run, and 24-hour/manual run
are distinct evidence states.

## 7. Reproduction and implementation flow

1. Record branch, commit, host, options, world directory, and process ownership.
2. Run static/path checks before any server process.
3. Test unit limits and recovery without sharing a live world directory with another
   process.
4. Run wire/gameplay/integration gates, then stress/bench/soak as their own stages.
5. Capture logs, PASS/FAIL/SKIP counts, elapsed time, RSS/TPS/MSPT where applicable.
6. On failure, preserve the artifact and return to the previous saved state; never
   relax an assertion or overwrite a corrupt file silently.

All command examples in this file and VERIFICATION are timeout-wrapped. Before
cleanup, inspect exact command lines; use PID-specific termination rather than an
ambiguous `pkill` pattern.

## 8. C++ operational design example

```text
Oversize input
- implementation: Connection::readFrame / PacketDecoder::decodeFrame
- budget: outer frame ≤ 8 MiB; declared decompressed size ≤ 2 MiB
- ordering: charge/check before attacker-sized allocation or inflate
- response: state-correct Disconnect, then close
- evidence: test_flood_net A1/A2/A7/A8
- provenance: current code + WIRE framing contract
```

This records current policy; it is not a request to change the threshold.

## 9. Source/class composition

| operational class | source |
|---|---|
| server lifecycle | `src/game/GameServer_core.cpp`, `GameServer_tick.cpp`, `main.cpp` |
| persistence | `WorldDataManager.*`, `Persistence.hpp`, `Anvil.hpp`, `RegionFile.hpp` |
| ownership/recovery | `SessionLock.hpp`, `PlayerDataRecovery.hpp`, `tools/check_world.cpp` |
| network safety | `Connection.hpp`, `PacketDecoder.hpp`, `RateLimiter.hpp` |
| administration | `Rcon.hpp`, `ServerProperties.hpp`, whitelist/ops command handlers |
| measurement | `tests/stress_test.py`, `tests/soak_test.py`, `tools/bench_chunk_gen.py`, replay tools |

## 10. Module split and ownership

| module | owns | canonical section |
|---|---|---|
| backup/recovery | files, atomic save, quarantine, check_world | this document §6 |
| limits/security | frame/decompression/rate/slow-peer policy | this document §12 and §14 |
| performance/load | budgets and run metadata | this document §12 |
| soak/manual GUI | dry/nightly/24-hour/manual procedure | this document §15 |
| evidence/incident | reports, cleanup, rollback | this document §16 |

The only cross-domain dependencies are WIRE framing and GAMEPLAY event causes; no
packet-ID table is copied here.

## 11. Cautions

- `test_smoke_80` forks a child `cppfm`; an outer timeout that kills only the parent
  can orphan the server. Treat an orphan as a failed gate.
- Before a cleanup kill, inspect `pgrep -a -f 'cppfm --por[t]'`, then terminate the
  exact PIDs. Never use `pkill c++`, `pkill g++`, or a broad substring.
- A POSIX signal/cleanup recipe is not a Windows implementation claim; document the
  platform before using it.
- `level.dat` and `level.dat_old` are forensic inputs. Preserve a corrupt file as
  `.corrupt`; do not replace it without a log.
- Synthetic PASS, bot PASS, a real-client screenshot, and a 24-hour soak have
  different evidence strength.
- The `docs/mob_stats_149.csv` fixture and world data are separate; backup commands
  must not move the fixture to a new path.

<a id="performance-and-load"></a>
## 12. Performance and load

### Declared budgets

| workload | contract/measurement |
|---|---|
| view distance | bench `--view-distance 32 --chunks 4225 --dry --strict` |
| entity load | 1,000 entities: record P95 MSPT/TPS and no unbounded RSS growth |
| active redstone | record P95 MSPT/TPS under an active engine workload |
| cache | LRU target 1,024; record hit/miss and queue depth |
| long run | 300 s dry, nightly 2 h, and 24 h/manual with RSS/TPS/MSPT and NBT integrity |
| connections | stress target 120 clients, with process/port ownership recorded |

These are declared operational acceptance contracts. A number without run ID,
commit, host, options, warm-up, and sample count is not a fresh measurement.
The current source clamps configured view/simulation distance to `2..32`; the session
uses the minimum of server and client view distance for sending.

### Final-gates measurements

The following exact main-checkout results are recorded against runtime baseline
`17ab09f5220bf99203d2aea2b2c9d65f763f433b` on 2026-09-05. They are not averaged with
older runs:

| workload | result |
|---|---|
| configure/build | completed after a filesystem-slow initial 300s outer timeout; resumed build completed `104/104` |
| incremental Ninja build | `ninja: no work to do` in `0.05s` |
| view32 dry benchmark | `PASS` in `1.74s`; 4,225 chunks, p50 `0.108ms`, p95 `2.333ms`, peak RSS ~`95MB`, hit rate `84.6%` |
| 120-client stress | `120/120 joined; PASS` in `68.0s` |
| multi-client integration | `ALL PASS` in `17.83s` |
| bot smoke | `ALL PASS` in `20.65s` |
| `tests/soak_test.py --duration 300` | `PASS`; 150 keepalives, 0 disconnects, actions 2932, post-fill RSS growth `7.6%` |
| `tools/soak_bot.py --duration 300` | `3/3 PASS`; each KeepAlive 30, chunks 182, time updates 300, all error counters 0, cleanup PASS |
| `tests/soak_test.py --duration 1800 --movement-range 3000` | `PASS` on `17ab09f`; 900 keepalives, 0 disconnects, actions 17493, post-fill baseline `114504kB`, max `128868kB`, growth `12.5%`; diagnostic only |
| `tests/soak_test.py --duration 7200 --movement-range 3000` (parent `d1c6a7f`) | interrupted at recorded `t=3361s`; post-fill RSS `160388→191612kB` (`+19.5%`), above the `15%` gate; not accepted |
| accepted 2h/24h run | none; the 7200s attempt was not accepted and no 24-hour artifact exists |
| current real-client/GUI artifact | none |

The former `soak_bot` blocker is resolved by three integrated passes. The attempted
7200-second soak was interrupted above its RSS gate and is not a pass. No vanilla
Xoroshiro L3 parity claim is made, and the accepted long-run/real-client evidence
boundary remains explicit.

## 13. Thread safety

- one connection/session thread owns a connection's read side; `Connection::tx_`
  serializes concurrent writes;
- the game tick owns world mutation and packet-batch flush decisions;
- `Persistence` owns its worker/flush lifecycle and must finish I/O before shutdown;
- RCON accepts on a worker and handles each connection separately; and
- backup/check_world is offline and must not copy a world during an active save.

The canonical-doc writer is also single-writer. No runtime lock or worker is added
here.

<a id="limits-and-security"></a>
## 14. Edge cases and incident policy

| case | action |
|---|---|
| frame > 8 MiB | reject as oversize before allocation |
| declared decompressed > 2 MiB or negative/forged | reject/kick; do not inflate |
| threshold 0 with `dataLength=0` | reject as invalid compressed-mode input |
| trailing zlib data | strict inflate rejects it |
| 20-connection-per-second boundary | AcceptGate refuses excess immediately |
| chat score > 200 | SpamTracker marks the peer for spam disconnect |
| slow peer / receive timeout | kick/close according to session state |
| live `session.lock` | loud warning; current availability-first policy continues, so operators must investigate |
| stale lock | warn and rewrite with current PID/timestamp |
| both level files corrupt | quarantine and create fresh state with visible warning |
| leftover child/port | inspect exact PID, terminate only owned process, rerun gate |

The live-lock availability policy is an implementation fact, not permission to run
two servers on one world.

## 15. Test method and evidence

Operational evidence targets are:

<a id="soak-and-long-run"></a>
### Soak and long-run verification

The 300-second dry run, nightly run, 24-hour run, and real-client/manual run are
separate evidence classes. A procedure without its run metadata and artifact remains
`DECLARED-LIMITATION`.

<a id="real-client-and-gui-verification"></a>
### Real-client and GUI verification

Screenshots, replay captures, operator identity, client version, and timestamp are
required for a real-client claim. Bot or synthetic output is recorded separately.

<a id="evidence-log-and-reporting"></a>
### Evidence log and reporting

Each report records commit, date, host, options, run ID, command, exit status, output
summary, cleanup result, and any allowed limitation.

| target | purpose |
|---|---|
| `test_flood_net` A1–A8 | bandwidth, decoder budget, spam, accept gate, live kicks |
| `test_recovery` | 45-case level/player/region/session-lock recovery matrix |
| `test_rcon_multi` | five concurrent sessions, ten wrong passwords, post-flood command |
| `check_world` | offline integrity check and exit code |
| `test_native`, `test_plan43`, `test_smoke_80` | server lifecycle and integration |
| `tools/bench_chunk_gen.py` | view-distance 32/4,225 chunk budget |
| stress/soak scripts | 120-client and 300 s/nightly/24 h evidence |
| manual GUI/replay | screenshots and UpdateLight visual behavior, labeled separately |

The current canonical evidence table and exact timeout commands are maintained in
[VERIFICATION.md#operations-gate](VERIFICATION.md#operations-gate). This document
does not claim an accepted 2-hour or 24-hour result merely because a procedure exists.

## 16. Priority, status, and rollback

**Priority: high.** Static, fixture, recovery, and flood gates precede expensive load
runs. Any failure in scope, child cleanup, checksum, recovery, or a new test FAIL
blocks publication. The integrated final-gates status has no unexpected executable
failure; publication remains `BLOCKED` only for declared boundaries. E-14 remains the
intentional Fabric JVM-mod boundary. Roll back only a migration
commit owned by the operator, using an explicit inverse or `git revert`; never reset
or discard unrelated user changes.
