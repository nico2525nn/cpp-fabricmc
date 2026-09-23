# Adversarial review — active goal

## Scope and verdict

Reviewed the dirty working tree, the current-state notes, goal plan, settings/security changes, secure chat, events, JVM bridge, persistence, networking, lifecycle, performance, duplication, and dependency boundaries. I did not modify product code or another specialist's path.

Final severity totals: **P0: 0, P1: 0, P2: 0, P3: 0**. The six lower-severity findings from the initial review were fixed and have focused regressions or source-order guards below. This does not waive the explicit product-scope limitations in the handoff.

## Bounded verification

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

### ADV-LIFE-004 — fixed — timed JNI mutation could commit after caller failure

`GameServer::runOnServerThread` now waits for a request already in `Running` state to reach a terminal result instead of returning false while side effects may still commit. Pending requests still cancel at the deadline. `tests/test_goal_security_guards.py` locks the running-state wait branch; the full blocked-server-thread timing case remains a long-running integration limitation.

### ADV-DATA-005 — fixed — random loot predicates were deterministic threshold stubs

`PredicateContext` now carries a deterministic seed and looting level. Both random predicate variants use a splitmix-derived value and clamp the looting-adjusted chance. `tests/test_goal_cleanup_game.cpp` checks both true and false seeded outcomes, while `tests/test_goal_security_guards.py` prevents a return to the threshold stub.

### ADV-EVENT-006 — fixed — legacy block handlers had no removal/lifetime contract

`BlockEventDispatcher::clearLegacyHandlers` now synchronously clears all legacy vectors and waits for in-flight callbacks, while `JvmRuntime::stop` invokes it before JVM teardown. `tests/test_goal_cleanup_game.cpp` proves a callback registered before unload does not run afterward and that unload waits for an active callback; the source guard protects the lifecycle call and all four vector clears. Individual subscription tokens remain an ABI-compatible future improvement.

## Handoff blockers and limitations

- P0/P1 findings: **none reproduced**.
- The six findings above are fixed; the remaining caveats are test-environment limitations, not open severity findings.
- The declared arbitrary-JVM-mod, RNG-L3, long-soak, GUI, signed-command, and moving-piston-NBT limitations recorded in `docs/CURRENT_STATE.md` remain applicable; this review does not silently waive them.
