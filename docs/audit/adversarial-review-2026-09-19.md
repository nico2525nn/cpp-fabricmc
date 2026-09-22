# Adversarial review — 2026-09-19 working tree

This report records the final current-tree review of the settings, authority,
secure-chat, persistence, event, and simulation-dispatch hardening pass. It is
not a release sign-off and does not replace the numbered MISSING taxonomy.

## Evidence inputs

All commands were run from the repository root with timeout wrappers and the
repository's `/dev/shm` temporary directory policy where applicable.

```text
env TMPDIR=/dev/shm timeout --foreground --kill-after=10 120 \
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
env TMPDIR=/dev/shm timeout --foreground --kill-after=10 900 \
  cmake --build build -j2
env TMPDIR=/dev/shm CPPFM_RECOVERY_WORLD_PREFIX=/dev/shm \
  timeout --foreground --kill-after=10 1800 \
  ctest --test-dir build -LE 'nightly|package' --output-on-failure --timeout 600

env TMPDIR=/dev/shm timeout --foreground --kill-after=10 60 ./build/test_settings_matrix
env TMPDIR=/dev/shm timeout --foreground --kill-after=10 60 ./build/test_properties ./build/cppfm
env TMPDIR=/dev/shm CPPFM_RECOVERY_WORLD_PREFIX=/dev/shm \
  timeout --foreground --kill-after=10 30 ./build/test_recovery
env TMPDIR=/dev/shm timeout --foreground --kill-after=10 60 ./build/test_core_safety
env TMPDIR=/dev/shm timeout --foreground --kill-after=10 450 \
  ./build/test_plan43 ./build/cppfm
env TMPDIR=/dev/shm CPPFM_SMOKE_WORLD_PREFIX=/dev/shm/smoke80-final- \
  timeout --foreground --kill-after=10 450 ./build/test_smoke_80 ./build/cppfm
env TMPDIR=/dev/shm timeout --foreground --kill-after=10 600 \
  python3 tests/test_server_full.py --binary ./build/cppfm
env TMPDIR=/dev/shm timeout --foreground --kill-after=10 600 \
  python3 tests/stress_test.py --clients 120 --binary ./build/cppfm
env TMPDIR=/dev/shm timeout --foreground --kill-after=10 400 \
  python3 tests/soak_test.py --duration 300 --binary ./build/cppfm
env TMPDIR=/dev/shm timeout --foreground --kill-after=10 60 \
  python3 tools/bench_chunk_gen.py --view-distance 32 --chunks 4225 --dry --strict
```

Measured results from the final run:

| check | result |
|---|---:|
| configure/build | `RC=0` |
| non-nightly CTest | `46/46 PASS` in `394.82s` |
| settings matrix | `25 PASS / 0 FAIL` |
| properties | `33 PASS / 0 FAIL` |
| recovery | `55 PASS / 0 FAIL` |
| core safety | `45 PASS / 0 FAIL` |
| Plan43 | `87 PASS / 0 FAIL` |
| smoke80 | `224 PASS / 0 FAIL` |
| full live matrix | `240 PASS / 0 FAIL / 240 total` |
| 120-client stress | `120/120 joined PASS` in `68.5s` |
| 300-second soak | `PASS`; 150 keepalives, 0 disconnects, 2,899 actions, `0.2%` post-fill RSS growth |
| view-distance 32 dry benchmark | `PASS`; 4,225 chunks, p50 `0.107ms`, p95 `2.332ms`, RSS ~`95MB`, hit rate `84.6%`, OOM/kick `0` |

## Feature adversarial review

**Final score: 10/10.** No unresolved P0, P1, or P2 finding.

Reviewed contracts:

- all mutating tick/session/console transitions remain serialized by the
  recursive simulation dispatch gate;
- framed packets are encoded while the gate is held and handed to a dedicated
  per-connection writer, so socket backpressure cannot unlock the middle of a
  mutation;
- each writer has a 4 MiB output cap and disconnects a client that exhausts it;
  graceful teardown drains a short protocol tail for at most 100 ms;
- block callbacks can cancel before place/break/click mutation, scoped event
  removal waits for in-flight callbacks, and item command sources retain their
  source dimension;
- source/destination pending piston commits are flushed under the simulation
  gate before synchronous or background chunk snapshots;
- invalid secure profiles terminate the play session when secure-profile or
  enforced secure-chat policy requires it; all configured Mojang profile keys
  are tried; and signed command packets fail closed until their argument
  transcript is implemented.

Earlier interim concerns about whole-bootstrap gate release, worker-side piston
races, generic socket gate release, queued-disconnect loss, encrypted-frame
reordering, callback mutation overwrite, and 32 MiB/client output memory are
explicitly retracted in the final review: the current tree
keeps bootstrap state serialized, runs persistence barriers under the simulation
gate, queues framed output without unlocking mutation, bounds close at 100 ms,
and caps output at 4 MiB/client.

## Code-quality adversarial review

**Final score: 9.5/10.** No unresolved P0 or P1 finding.

One low residual advisory, `DOS-CONSOLE-001`, remains documented: authenticated
RCON `/reload` and `/function` CPU work executes under the serialized simulation
domain (`src/game/GameServer_world.cpp`). The bounded socket writer and send
limits prevent network starvation; the command remains intentionally serialized
rather than being moved to an unsafe worker. This is a low P2 operational tradeoff,
not a correctness or security bypass in the reviewed scope.

## Declared limitation

Signed chat-message verification is implemented for inbound messages. In enforced
secure-chat mode accepted signed chat is relayed as `SystemChat`; invalid/missing
sessions are rejected. Signed command argument signatures are not reconstructed,
so `ChatCommandSigned` fails closed rather than dispatching unverified arguments.
This limitation is intentionally retained in `CURRENT_STATE.md`,
`MISSING_FEATURES_1_21_4.md`, and the canonical operations/gameplay contracts.
The packet-level chat verifier follows the official 1.21.4 signed-message
layout (version, MessageLink UUID/session/index, salt, epoch-second timestamp,
UTF-8 byte length/content, and empty last-seen signature list); profile
certificates use SHA1withRSA and message signatures use SHA256.

## CI and PR review path

`.github/workflows/ci.yml` runs on pushes, pull requests, and manual dispatch.
It configures a fresh Ninja build, compiles the full target set, checks
`git diff --check`, runs focused settings/properties/recovery/core-safety gates,
and executes the non-nightly CTest aggregate with bounded timeouts. The review
path is: reproduce the workflow locally, attach the focused/full logs, obtain
independent feature and quality adversarial scores, then merge only with zero
P0/P1 findings and the canonical docs updated.
