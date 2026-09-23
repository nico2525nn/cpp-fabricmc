# Adversarial review — PR #1 review record

## Scope and verdict

Reviewed PR #1's combined head and working diff across settings/security, secure chat, events, JVM bridge, persistence, networking, lifecycle, performance, duplication, and dependency boundaries. Two independent sub-agents performed five hostile review rounds, followed by a final independent read-only pass; findings were checked against the code and converted into focused fixes/tests on the PR branch.

Current unresolved severity totals: **P0: 0, P1: 0, P2: 0, P3: 0**. This is a local review verdict for the named paths, not a universal compatibility claim. It does not waive the explicit product-scope limitations in the handoff, and it is not merge approval until exact-head Actions passes.

## Earlier bounded verification snapshot

The following commands record an earlier review snapshot; the current focused
rerun after rounds 4–5 is recorded under [network audit](goal-bugs-network.md)
and [current-state evidence](../CURRENT_STATE.md#1d-pr-1-adversarial-review-follow-up).

The following shipped and goal-specific tests completed with zero failures:

```text
env TMPDIR=/dev/shm timeout --foreground --kill-after=5 60 ./build/test_settings_matrix  # 27 PASS
timeout --foreground --kill-after=5 60 ./build/test_core_safety                       # 45 PASS
timeout --foreground --kill-after=5 60 ./build/test_flood_net                         # 32 PASS
timeout --foreground --kill-after=5 90 ./build/test_plan43 ./build/cppfm              # 87 PASS
timeout --foreground --kill-after=5 60 ./build/test_properties ./build/cppfm          # 33 PASS
timeout --foreground --kill-after=5 120 ./build/test_native ./build/cppfm             # ALL PASS
timeout --foreground --kill-after=3 30 git diff --check                               # clean
timeout --foreground --kill-after=5 60 ./build/test_goal_network_bugs                 # 36 PASS
timeout --foreground --kill-after=5 60 ./build/test_goal_gameplay_bugs               # 15 PASS
timeout --foreground --kill-after=5 60 ./build/test_goal_cleanup_runtime             # PASS
timeout --foreground --kill-after=5 30 ./build/test_goal_cleanup_game                 # 9 PASS
python3 tests/test_goal_security_guards.py                                           # 8 PASS
./build/test_settings_matrix                                                         # 27 PASS
```

The goal tests are retained as regression gates; the source-order guard intentionally complements runtime coverage where constructing a full authenticated session or JNI VM would require an unavailable external fixture.

## Findings

### ADV-SET-001 — reclassified — secure-profile and secure-chat configuration

`src/game/ServerConfig.cpp` keeps `enforceSecureProfile` and cppfm's
`enforcesSecureChat` extension as separate stored settings. The effective
secure-chat advertisement and unsigned-message policy use their logical OR,
matching vanilla 1.21.4 secure-profile enforcement. The parser does not mutate
the extension setting when the vanilla profile property is applied.
`tests/test_settings_matrix.cpp` verifies the stored settings remain
independent; `test_secure_chat_policy` covers the effective policy.

### ADV-CHAT-002 — fixed — unverified chat reached callbacks

`Session::onChatMessage` performs signature and replay checks before
`PlayerChatEvent` or `JvmRuntime::onChat`. With effective secure-chat
enforcement enabled, unsigned or invalidly signed messages are rejected; when
it is disabled, vanilla's unsigned path without a player session is accepted.
The packet disposition is covered by `test_secure_chat_policy`, and
`tests/test_goal_security_guards.py` locks callback ordering and policy use.
Unsigned-message behavior after a player session has been established remains
an explicitly unverified vanilla edge case. A full signed-session callback
counter also remains environment-dependent because the repository has no
authenticated client fixture.

### ADV-JVM-003 — fixed — JNI world mutation accepted unregistered block states

`JvmRuntime::nativeWorldSetBlock` rejects states absent from `gen::blockByState` before enqueueing and repeats the guard inside the server-thread closure. `World::setBlockInternal` applies the same registry check as a defense in depth. The source guard is covered by `tests/test_goal_security_guards.py`; valid-state JNI integration remains covered by the existing native bridge suite.

### ADV-LIFE-004 — fixed — JNI mutation could outlive an unbounded caller wait

Foreign callers of `GameServer::runOnServerThread` return at the configured
deadline. A still-pending request is cancelled; a request that has begun stays
owned by the server thread and may finish after the caller receives `false`, so
the result is indeterminate rather than proof of no side effect. JNI mutation
closures own their captured operation/result state. `test_native` covers both
deadline paths, while `tests/test_goal_security_guards.py` guards the bounded
wait contract.

### ADV-DATA-005 — fixed — random loot predicates were deterministic threshold stubs

`PredicateContext` now carries a deterministic seed and looting level. Both random predicate variants use a splitmix-derived value and clamp the looting-adjusted chance. `tests/test_goal_cleanup_game.cpp` checks both true and false seeded outcomes, while `tests/test_goal_security_guards.py` prevents a return to the threshold stub.

### ADV-EVENT-006 — fixed — legacy block handlers had no removal/lifetime contract

`BlockEventDispatcher::clearLegacyHandlers` now synchronously clears all legacy vectors and waits for in-flight callbacks, while `JvmRuntime::stop` invokes it before JVM teardown. `tests/test_goal_cleanup_game.cpp` proves a callback registered before unload does not run afterward and that unload waits for an active callback; the source guard protects the lifecycle call and all four vector clears. Individual subscription tokens remain an ABI-compatible future improvement.

### ADV-LIFE-007 — fixed — a tick callback could tear down its own server

Calling `GameServer::stop()` on the tick thread now only requests shutdown,
cancels pending server-thread work, and wakes waiters. Resource teardown and
joining are left to an external owner after the callback returns.
`scenarioStopFromTickCallback` in `test_native` verifies clean loop exit,
completed external teardown, and temporary-world cleanup.

### ADV-EVENT-008 — fixed — concurrent fires shared one mutable callback object

Each event invocation again copies the registered callable before invoking it,
preserving the prior snapshot semantics and avoiding concurrent calls through
one mutable `std::function` target. Scoped reset still waits for other active
invocations while allowing its own reentrant stack frames to return.
`test_goal_cleanup_game` covers concurrent fires and the reentrant drain path.

### ADV-LIFE-009 — fixed — a stale JNI mutation could adopt a restarted runtime generation

`runServerMutation` now snapshots the mutation generation before checking the
admission flag and rechecks the generation before enqueueing. A caller spanning
stop/start cannot accidentally capture the new generation and make its old
closure valid again. `test_goal_security_guards.py` locks the source ordering;
`test_native` destroys an inactive runtime on the tick thread while a queued
mutation caller is confirmed still blocked at the teardown fence.

### ADV-TEST-010 — fixed — teardown tests could pass without reaching the intended wait path

The JVM mutation regression now proves the caller remains pending when the tick
thread destroys the transient runtime; its test accessor counts only requests
whose state is still `Pending`, not cancelled entries left in the queue. The
reentrant EventBus reset test observes the entry's actual drain-waiter state
before asserting that reset is blocked. `test_native` and
`test_goal_cleanup_game` cover these paths without relying on a timing-only
sleep to infer synchronization.

### ADV-NET-011 — fixed — Status probes could starve Login workers

Pending handshake/login/configuration workers and Status requests now use
separate admission gates. A Status connection releases its general pending
slot after the Status handshake and holds one of 16 dedicated Status slots
until it completes. `test_flood_net` leaves Status clients waiting for Ping,
rejects the excess probe, and verifies that a normal Login still succeeds.
Completed session-thread handles are reaped rather than retained indefinitely.

### ADV-NET-012 — fixed — concurrent logins could exceed `max-players`

The old check observed only already-registered Play players, leaving a window
for concurrent logins to pass before either registered. Login now reserves a
slot under a dedicated admission mutex; Play registration consumes that
reservation, and teardown releases any unused reservation. `test_flood_net`
holds the first `max-players=1` login before configuration completes and proves
the concurrent second login is rejected. The same fixture proves that
`max-players=0` rejects an ordinary login, matching the condition in Mojang's
1.21.4 `PlayerList.canPlayerLogin` bytecode.

### ADV-LIFE-013 — fixed — a PlayerJoin callback could destroy its own session owner

`GameServer::stop()` now recognizes session-worker context and only requests
shutdown there, matching the tick-thread self-stop rule. The external owner
performs resource teardown after `runForever()` exits, when it can join the
session worker instead of retaining a joinable self-thread handle. The native
integration case logs in through a real session, calls `stop()` from
`PlayerJoinEvent`, and verifies the callback returns and external teardown
completes without process termination.

### ADV-TEST-014 — fixed — the JVM generation-fence test could pass via timeout

The mutation deadline is now part of `JvmConfig` (default 250 ms), and the
teardown regression sets it to 30 seconds so the tick callback's runtime
destruction cannot accidentally lose a scheduling race to the normal caller
timeout. The test uses the already-captured runtime pointer while the queued
operation is fenced by its weak generation lifetime. Native integration and
`goal_security_guards` cover the intended pending-request path.

## Handoff blockers and limitations

- P0/P1 findings: **none unresolved**.
- All reproduced findings in the five hostile rounds and final follow-up have a fix or a test correction and a focused regression/guard. The local focused CTest group is 8/8. The complete 55-target non-nightly sweep had one quality-label failure, which was fixed; `flood_net` and `quality_audit` then passed together (2/2). The exact PR head must pass CI before merge.
- The declared arbitrary-JVM-mod, RNG-L3, long-soak, GUI, signed-command,
  operator player-limit bypass, and moving-piston-NBT limitations recorded in
  `docs/CURRENT_STATE.md` remain applicable; this review does not silently waive
  them.
