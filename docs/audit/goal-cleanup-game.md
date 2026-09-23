# Game cleanup audit

Scope: `src/game/**`, `tests/test_goal_cleanup_game.cpp`, and this audit only.

## LOC measurement

The fixed measurement counts nonblank, non-comment-only lines in sorted C/C++
files under `src/game`:

```sh
find src/game -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 | sort -z | xargs -0 awk 'BEGIN{block=0} {t=$0; if(block){if(t ~ /\*\//){sub(/^.*\*\//,"",t); block=0}else next} if(t ~ /^[[:space:]]*\/\*/){if(t !~ /\*\/[[:space:]]*$/) block=1; next} if(t ~ /^[[:space:]]*\/\// || t ~ /^[[:space:]]*\*/) next; if(t ~ /[^[:space:]]/) n++} END{print n}'
```

| measurement | eligible LOC |
|---|---:|
| before | 42,720 |
| after | 42,715 |
| change | -5 |

The before value was recorded before this cleanup; the command is unchanged for
the after value. Existing dirty edits in `src/game` remain included in both
measurements. The reduction is from consolidating the three cancelable block
event paths and their repeated handler vector type; no feature, fixture,
assertion, generated registry, or evidence was removed.

The follow-up cleanup retained the same rule and removed only local duplication:

| file | fixed local measurement | delta |
|---|---:|---:|
| `src/game/MenuLogic.cpp` | `612 → 596` nonblank/noncomment lines | `-16` |
| `src/game/StairsHelper.hpp` | `176 → 150` nonblank/noncomment lines | `-26` |
| `src/game/GameServer_session.cpp` | `6,484 → 6,476` total lines | `-8` |

The whole checkout is intentionally dirty from the broader goal work, so these
per-file measurements are not presented as a synthetic aggregate. The requested
10,000-line deletion would remove feature-bearing code and is not claimed. The
follow-up pass removed only callsite-zero CombatManager methods, GameServer
compatibility wrappers, Brain/Bamboo helpers, and duplicate terrain accessors;
active damage, AI, dimension, and world-generation paths remain.
The final measurement transcript is retained at
`/tmp/grok-goal-e8af7c070433/implementer/loc-followup-final.log`.

The final pass also replaced the unreferenced legacy `World::fillTerrain` body
with a delegation to the canonical `fillTerrainV3` path. The public compatibility
entry point remains available, while the duplicate terrain implementation is gone.

## Cleanup and verification

`BlockEventDispatcher` now uses one private `dispatchCancelable` path for place,
break, and click events. It preserves legacy callback snapshots, typed hook
cancellation, and fail-closed exception handling. `tests/test_goal_cleanup_game.cpp`
checks field preservation, cancellation, and exception behavior.

Targeted verification:

```sh
timeout --foreground --kill-after=5 30 g++ -std=c++20 -Isrc tests/test_goal_cleanup_game.cpp -o /tmp/test_goal_cleanup_game
timeout --foreground --kill-after=5 30 /tmp/test_goal_cleanup_game
timeout --foreground --kill-after=5 300 cmake --build build -j2
```

Initial cleanup baseline: the focused test passed 9/9. `cppfm`, the settings matrix, and
the security/cleanup targets link successfully in the final incremental build.
The menu unit test passed `41/41`; the incremental production build also passed
after the stairs and session helper refactors.

PR #1 adversarial-review follow-up (2026-09-23): the expanded focused test now
passes **13/13**, including independent callable snapshots under concurrent
fires and a reentrant unsubscribe handshake that proves reset actually waits
for the other in-flight callback. `test_native` also passes the bounded
server-thread deadline and tick-callback self-stop lifecycle scenarios. The
opened crafting-table slot routing is covered by `test_wire_b6` (137/137).
These are local working-tree results; exact-head GitHub Actions remains the
merge gate.

## Residual candidates

- `World` retains public compatibility aliases such as `onBlockPlace` and
  `getForcedChunksSnapshot`; they are intentionally retained because they are
  API-facing wrappers even though current in-tree callers favor canonical names.
- `MenuLogic::setRenameText` is an unused no-op compatibility method. It was not
  removed in this pass because external callers may still compile against the
  header; removing it needs an API deprecation decision and a compatibility gate.
- The duplicated anvil rename recomputation in the session path is behavior
  sensitive (creative too-expensive handling), so it remains pending a dedicated
  regression matrix rather than being folded into this mechanical cleanup.
