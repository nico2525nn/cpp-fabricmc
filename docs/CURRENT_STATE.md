# CURRENT_STATE.md — cpp-fabricmc final-gates state tracker

> This is the stable tracker for the plan51 embedded-JVM boundary and the current
> cleanup worktree. It records the integrated main checkout and exact rerun evidence. Paths under the
> ignored `build/` tree are local run outputs, not tracked/public evidence or release
> assets unless separately published. It is not a release
> sign-off: `publication_status` remains `BLOCKED` by declared scope/evidence
> boundaries, and a historical taxonomy `DONE` or a focused PASS does not become a
> universal parity claim.

## 0. Current handoff

This tracked document is the canonical current-state entrypoint. It records the
verification baseline, active compatibility boundaries, and publication status;
`docs/VERIFICATION.md` holds gate definitions and reproducible evidence. Local
handoff notes are not part of the public documentation set.

## 1. Snapshot

| field | value |
|---|---|
| `updated` | `2026-10-02` |
| `implementation_baseline` | integrated `main` HEAD `4756a5b0` (2026-09-26; PR #5 DimensionRuntime ownership) |
| `implementation_baseline_short` | `4756a5b0` (`81b6f4f` plus PR #5) |
| `documentation_commit` | current architecture-quality refactor and verification updates are uncommitted on `codex/deep-refactor-quality` |
| `main_integration_merge` | `4756a5b0` (PR #5 DimensionRuntime ownership slice) |
| `plan` | compatibility baseline plan53/54; active architecture/storage compatibility work in §1F |
| `phase` | `behavior-preserving-architecture-refactor-and-recovery-compatibility` |
| `phase_status` | `IN_PROGRESS_DEEP_REFACTOR; STORAGE_COMPATIBILITY_PARTIAL; FRESH_STRICT_REVIEW_PENDING; QUALITY_GOAL_NOT_MET` |
| `publication_status` | `BLOCKED` |
| `runtime_reference_snapshot` | integrated main `4756a5b0`; current unmerged world-mutation work is listed separately in §1E |
| `canonical_workflow` | `docs/DEVELOPMENT.md#research-workflow` |
| `research_entrypoint` | `docs/research-prompt.md` is a legacy redirect only |
| `research_viewpoints` | `16` current viewpoints; old `13` wording is historical |
| `taxonomy_snapshot` | MISSING `#1–#90`; historical matrix counts `DONE=90, PARTIAL=0, TODO=0` |
| `strict_assessment_1` | `78 gaps`; `HISTORICAL` archive label, not a current aggregate |
| `next_plan` | finish the three parallel adversarial domain reviews and cover the remaining JVM/build/docs/test changes; resolve material findings, continue cleanup until an independent strict code-quality review is at least 6/10, then rerun gates, submit a PR, and require exact-head Actions before any merge |

The previous baseline was the plan50 runtime follow-up after the plan49 implementation integration and cleanup commit
`db12df96093a0869e958f62b11f9a9cd68ba3ef1` and safety commit
`4526dfe4f7112b1fe744a83484ef5ef40176d481`. Its four no-ff merges are recorded in
the main graph: docs `e978dc6`, network `7894a4c`, block `20113bf`, and entity
`e0ca08e` (historical plan49 merges). Plan51 adds a default-on embedded JVM boundary
for binaries built with the required JNI inputs, without changing generated data,
protocol 769, or the `docs-legacy/` archive. The
plan51 implementation and its no-ff integration are recorded above.

## 1A. Working-tree verification baseline (2026-09-19)

The following evidence is from the 2026-09-19 working-tree snapshot after the
settings/security/authority hardening pass. It supersedes neither the historical
`335fca5` baseline above nor the declared release boundaries below, and it is not
the result of the separate plan56 compatibility follow-up in §1C.

| gate | measured result | scope / limitation |
|---|---|---|
| configure/build | `RC=0` | RelWithDebInfo Ninja configure and full build; all commands timeout-wrapped |
| settings matrix | `27 PASS / 0 FAIL` | typed properties, CLI precedence, Java String.hashCode/textual seeds, secure-profile/chat separation, invalid-input retention, and unsupported-key reporting |
| properties | `33 PASS / 0 FAIL` | table-driven properties/CLI and help/version side-effect checks |
| recovery | `55 PASS / 0 FAIL` | `CPPFM_RECOVERY_WORLD_PREFIX=/dev/shm`; level/player/region recovery and session-lock cases |
| core safety | `45 PASS / 0 FAIL` | parser, NBT, compression, packet, Anvil, whitelist, and scoped-event callback boundaries |
| plan43 protocol matrix | `87 PASS / 0 FAIL` | command authority, signed-command offset parsing, abilities, horse/use-entity, movement/fall, sign persistence, and liveness |
| smoke80 | `225 PASS / 0 FAIL` | rebuilt strict source-tree matrix with isolated operator fixture and owned cleanup |
| live protocol/authority | `240 PASS / 0 FAIL / 240 total` | full `tests/test_server_full.py`; resource-pack UUID/ack, command authority, secure-chat paths, persistence/restart, RCON, and wire replays |
| non-nightly CTest | full run `54/54 PASS` after final cleanup | `ctest --test-dir build -LE 'nightly|package' --output-on-failure` passed all registered non-package tests, including the three live fixtures |
| 120-client stress | `120/120 joined PASS` in `68.5s` | `tests/stress_test.py --clients 120`; server tick remained alive and owned cleanup completed |
| 300-second soak | `PASS` | 150 keepalives, 0 disconnects, 2,895 actions, post-fill RSS growth `1.0%` in the retained goal comparison run |
| view-distance 32 dry benchmark | `PASS` | 4,225 chunks; p50 `0.107ms`, p95 `2.332ms`, peak RSS ~`95MB`, hit rate `84.6%`, OOM/kick `0` |
| output isolation | `PASS` | simulation mutations remain under the recursive dispatch gate; frames are encoded under the gate and handed to a per-connection writer with a 4 MiB cap; encrypted frames remain FIFO; graceful close drains for at most 100 ms |
| piston persistence barrier | `PASS` | source/destination pending piston commits are flushed under the simulation gate before synchronous or worker snapshots; full moving-piston serialization remains intentionally omitted from persistent NBT |
| secure profile rejection | `PASS` | invalid profile sessions are terminal when secure profile or secure chat enforcement is enabled; Mojang profile certificates use SHA1withRSA, chat messages use SHA256; signed command argument transcripts remain fail-closed |

The live, focused, stress, soak, benchmark, and CTest runs prove the named paths
only; they do not close the accepted 2-hour/24-hour soak, arbitrary JVM-mod
compatibility, full worldgen call-order/NBT parity, or retained real-client
artifact boundaries.

## 1B. Protocol-primitive follow-up (2026-09-23)

| gate | result | evidence / boundary |
|---|---|---|
| Official primitive oracle | `INSPECTED` | SHA-1-pinned Mojang 1.21.4 server artifact; VarInt, VarLong, Varint21 frame splitter, and packed BlockPos decoder/packing bytecode reviewed, not executed |
| Primitive and stream regressions | `PASS` | `test_goal_network_bugs` 79/79; independent signed endpoints, non-minimal forms, Java terminal-payload truncation, generic sixth/eleventh-byte rejection/consumption, strict three-byte Varint21 frame rejection, plaintext/encrypted stream alignment, big-endian fixed-width fields, and packed Position axis limits |
| Related CTest targets | `7/7 PASS` | `native`, `spec_wire`, `jvm_native_bridge`, `core_safety`, `goal_network_bugs`, `wire_full`, and `fuzz` |
| Smoke regression after tick-based chat pacing fix | `PASS LOCALLY; CI HARDENING IN FOLLOW-UP` | `ctest --test-dir build -R smoke80 --output-on-failure --timeout 600`; last recorded local run was 181.26 seconds, `225 PASS / 0 FAIL`. Reset feedback has a 30-second bounded wait and prints chat/disconnect diagnostics only on failure. A later Actions run exposed that the target-specific `TIMEOUT 450` overrode the workflow-wide `--timeout 600`; the follow-up removes that override and lowers the random-tick stress value while keeping it above the vanilla default. |
| PR Actions gate | `PASS / SHA-SPECIFIC` | Combined head `bac90dfbce70a8d1c09ddf525b339fac479ff350` passed the full build and non-nightly CTest ([run 35828644414](https://github.com/nico2525nn/cpp-fabricmc/actions/runs/35828644414)); subsequent review changes in this branch still require a new exact-head run. |

MISSING #71's row-specific evidence is now `PASS` in the 90-row coverage ledger
(`14 PASS`, `38 PARTIAL`, `38 UNVERIFIED`). This does not remove the independent
publication boundaries below or imply universal protocol parity.

## 1C. Vanilla 1.21.4 settings compatibility follow-up (2026-09-23)

This follow-up was developed on `plan56/compatibility` from PR #1 head
`26e399c0`, submitted as stacked PR #2, and squash-merged into the PR #1 branch
as `bac90dfb` on 2026-09-23. It is not yet part of `main`. The result changes
vanilla-facing defaults and property interpretation while keeping unsupported
properties declared as unsupported.

| gate | measured result | scope / limitation |
|---|---|---|
| RelWithDebInfo configure/full build | `PASS` | Full Ninja build completed in the integration worktree; all commands had explicit timeouts |
| defaults, whitelist key, difficulty IDs, and signed integers | `49 PASS / 0 FAIL` | `settings_matrix`; vanilla online/profile defaults, view distance, MOTD, difficulty, random new-world seed, canonical `white-list`, source-order alias behavior, difficulty IDs `0..3`, leading `+`, and malformed doubled signs are covered |
| Java Properties parsing and configuration | `64 PASS / 0 FAIL` | `properties`; separators, escapes, continuations, key case, UTF-8/ISO-8859-1 fallback, Java boolean semantics, CLI precedence, and rejection of malformed `+-` integers; isolated UTF-16 surrogates map to U+FFFD in the UTF-8 API |
| effective secure-chat policy | `12 PASS / 0 FAIL` | `secure_chat_policy`; status and Join Game share the effective policy, `enforce-secure-profile || enforces-secure-chat`; unsigned-after-session behavior remains unverified |
| fake-client authentication options | `PASS` | source guard and Python syntax checks cover local fake-client launchers; each offline launcher explicitly disables both online mode and secure-profile enforcement |
| exact-head full non-nightly CTest | `55/55 PASS` | 544.84s in GitHub Actions on combined head `bac90dfbce70a8d1c09ddf525b339fac479ff350`; see the [exact-head Actions run](https://github.com/nico2525nn/cpp-fabricmc/actions/runs/35828644414). Later PR #1 review changes are not covered by this run. |
| server child cleanup | `PASS` | post-suite `pgrep -a -f 'cppfm --por[t]'` found no remaining `cppfm` server process |
| remaining property parity | `PARTIAL` | `enforce-whitelist`, query/status/network, permission, datapack, and several world/pack/operations effects are not fully implemented; `ServerProperties::save` is not `Properties.store`-compatible |

Additional exploratory offline Python runs reported failures identical on the
unmodified parent binary: replay `5/8` (signed `n=0` command, sign block-entity
update, survival ability flags), `run_plan43_suite.py` `10/23` (signed chat,
damage, horse IDs, join flags, and sign placement/text), and short soak-bot runs
with `0` chunks against an expected minimum of `10`. These checks have no
retained per-run logs and are not part of the passing CTest aggregate; the C++
`plan43` target separately passes `87/87`.

## 1D. PR #1 adversarial-review follow-up (2026-09-23)

The PR #1 review branch completed five hostile review rounds using two
independent sub-agents, followed by a final independent read-only pass and
local fixes. The latest source changes include a stable JVM mutation-generation
fence, a long bounded deadline in its teardown regression, deferred full stop
when requested from a session callback, separate pending and Status worker
admission, completed session-thread reaping, atomic login-slot reservation, and
vanilla-matched zero-capacity rejection for ordinary profiles. A live online-
mode regression confirms a stalled unauthenticated peer cannot hold a player
slot ahead of an authenticated client.

The `ops.json` `bypassesPlayerLimit` exception remains unsupported and is
explicitly recorded as a partial permission boundary. The network audit records
the official Mojang 1.21.4 artifact/hash used to confirm the zero-capacity
branch. An oversize live Play frame may yield either a Disconnect packet or a
transport close: Linux can reset the TCP connection when the rejected frame's
unread body is still queued, so the test does not require a best-effort
Disconnect to survive that reset.

The focused CTest group (`native`, `flood_net`, `fuzz`, `goal_network_bugs`,
`goal_cleanup_runtime`, `goal_cleanup_game`, `goal_security_guards`, and
`wire_b6`) passed **8/8**. Within that run, the network bug harness was **99/99**,
the live flood matrix **108/108**, cleanup game **13/13**, wire B6 **137/137**,
and fuzz **25/25**. Native integration passed with both session-callback
self-stop and JVM generation-fence cases; the source/security guards passed.
The full local non-nightly CTest sweep passed 54/55; its only failure was the
quality-audit requirement that the new close-or-Disconnect assertion carry an
inline `[liveness]` label. After adding that label, both affected targets,
`flood_net` and `quality_audit`, passed again (2/2); no other full-sweep target
failed. This records final per-target evidence without claiming a second
55-test run after the comment-only audit fix.
The previous Actions result covers only
`bac90dfbce70a8d1c09ddf525b339fac479ff350`; it does not validate this review
delta. The exact PR head must pass Actions before merge.

The subsequent exact-head Actions run on pre-fix SHA `9e17bbe676a512400bbf97b682ded843d0faab77`
([run 35856254690](https://github.com/nico2525nn/cpp-fabricmc/actions/runs/35856254690))
failed in `smoke80`: `GameServer::addToInventory` read beyond the inventory
array while `InvTester` exercised `/give`. Reproduction with GCC 11.4 and JDK 17
showed a dangling backing array from a conditional `std::initializer_list`
range; the stale value became slot index 256. The implementation now iterates
the two contiguous inventory ranges directly (hotbar 36–44, main inventory
9–35). On the fixed source, the full GCC 11.4/JDK 17 smoke run completed with
**225 PASS / 0 FAIL** on rerun (an earlier post-fix run had one non-reproduced
failure); local `native` and `wire_b6` CTest targets passed **2/2**. This local
evidence does not replace Actions for the updated PR head.

## 1E. Behavior-preserving architecture refactor (2026-09-26)

### Merged DimensionRuntime ownership slice

PR #5 is merged into `main` at `4756a5b0cc660de714f1971e8a6c080d1ea02640`.
Its source commits are `62691578` (group per-dimension ownership in
`GameServer::DimensionRuntime`) and `c56c4f09` (route state selection through one
dimension-to-runtime mapping). Each runtime groups its `World`, local dispenser
and detector-rail bookkeeping, `BlockEntityStore`, per-dimension simulation
services, and persistence. Persistence is declared after callback targets so
reverse member destruction stops it first. The service-construction and
callback-wiring path remains in `GameServer::init()`; this slice did not finish
lifecycle composition or establish repository code-quality 6/10.

The merged source passed the configured RelWithDebInfo build and the recorded
non-nightly/non-package CTest run (`55/55`, `530.73s`, including smoke80 and live
goal scenarios) before merge. Reviews of that slice scored `6.5/10` and `5/10`;
these are slice scores, not an overall repository score.

### Current world-mutation boundary (unmerged)

The current branch is `codex/deep-refactor-quality`, based on merged `main`
`4756a5b0`. It narrows detached chunk construction and NBT decode to a
value-only `ChunkBuildWriter`, hides live block and skylight storage, separates
bulk loaded-chunk publication from gameplay writes, serializes block mutation
callbacks in revision order, and records pre-refactor chunk, wire-body, and NBT
hashes for all three dimensions. The soak bot now has a protocol-level position
acknowledgement path and a focused dispatcher test. Recent targeted corrections
make invalid `/place feature` edits fail closed and repair its coordinate-free
origin, but do not promote any compatibility-matrix status. The command still
uses fixed patterns rather than executing vanilla's configured-feature data.
The latest Mineflayer restart scenario exposed and fixed a real spawn-area
world-load bug: `GameServer::init()` generated the 5×5 spawn-ticket area before
registering Persistence's chunk loader, so saved spawn chunks were treated as
new terrain. Persistence now starts before that area is ensured, and the live
client verifies sign text and block state after a full restart.

| gate | result | scope |
|---|---|---|
| RelWithDebInfo configure | `PASS` | `cmake -S . -B /tmp/cppfm-deep-refactor-build-qBzLbe -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo` |
| full build | `PASS` | Timeout-wrapped `cmake --build build/deep-refactor -j4` completed after extracting Anvil admission from `GameServer.hpp`; latest broad rebuild completed 112 Ninja steps. |
| latest-source focused CTest | `7/7 PASS` | `native`, `plan43`, `world_mutation_contract`, `persistence_recovery`, `block_entity_contract`, `redstone_engine_full`, and `quality_audit`; `42.76s`. The clone capacity regression now calls the production block-state and opaque Block Entity projection helpers and checks the real 255-sector RegionFile limit; piston save-barrier and rejected-candidate immutability also pass. |
| post-startup-order-fix CTest | `6/6 PASS` | `native`, `jvm_native_bridge`, `world_mutation_contract`, `persistence_recovery`, `world_data_contract`, and `block_entity_contract`; `15.53s` after the spawn-chunk loader ordering fix. |
| latest normal CTest | `61/61 PASS` | `759.75s`; `ctest -LE 'nightly|package' --output-on-failure --timeout 600`. Includes Smoke80 (`370.79s`), JVM compatibility/corpus, three live goal scenarios, world/persistence contracts, multi-client, bot smoke, and quality/tautology audits. Nightly soaks and package-labeled gates were excluded; this is not full package or long-soak evidence. |
| earlier full current-source CTest | `62/62 PASS` | `736.08s`; predates the latest `/time set` branch fix, bot timing correction, and CTest working-directory cleanup. It excluded `soak2h`, `soak_bot`, and `soak_bot_protocol`; the protocol-only bot test was run separately then. Superseded as the latest-source result by the 61/61 run above. |
| full CTest before final projection extraction | `63/63 PASS` | `735.97s`, excluding both `soak2h` and the five-minute `soak_bot`; included `smoke80` (`356.70s`), JVM corpus, all three live goal scenarios, `package_archive`, and `package_jvm_smoke`. The final small projection extraction was followed by the focused 7/7 rerun, but not another full-suite rerun. This is not 2h/24h soak evidence. |
| Mineflayer live probe | `PASS` | Two consecutive clean post-fix runs with Mineflayer `4.25.0` / minecraft-data `3.117.0` covered two-client 1.21.4 login/spawn, chat, movement (measured `0.35` blocks), digging, filtered clone, chest transfer, feature overwrite/support guards, coordinate-free command origin, chest/shulker destruction drops, edited sign text across a full restart, and unloaded-chunk inventory persistence. One earlier run failed to observe chest-drop pickup within its bounded wait; subsequent two runs passed, so pickup is not yet treated as robust soak evidence. This remains bounded gameplay evidence, not GUI or full client/mod parity. |
| harness checks / cleanup | `PASS` | `node --check tests/mineflayer_live_smoke.cjs`, `git diff --check`, Python `py_compile` checks, and post-run `pgrep -a -x cppfm` passed; no owned server remained. An isolated temporary world retained by the failed run was removed after inspection. |
| independent hostile reviews | `5.1/10` quality, `5.0/10` compatibility (last pre-fix scores) | Those reviews identified the Anvil-capacity bypass in sign/item mutations, undrained scheduled piston work at save barriers, and a clone test that duplicated production projection. These paths have since been addressed and tested as above; a fresh complete-diff score is still pending, so no current `≥6` claim is justified. |
| PR / exact-head Actions | `PENDING` | not submitted; local results do not replace required Actions |

The architecture-quality goal remains active. The bounded Mineflayer scenario
and local full CTest are black-box/regression evidence for the named paths; they
do not themselves satisfy the independent ≥6/10 quality gate, establish GUI
compatibility, or prove broad vanilla/mod parity. Do not claim an overall score
until both independent reviewers have assessed the current complete diff and
any material findings are resolved.

## 1F. Deep quality-refactor and real-use verification (2026-09-30)

The active worktree remains `codex/deep-refactor-quality` on integrated base
`4756a5b0`; this section records the current unmerged source state. The refactor
is intentionally broad: it separates session interaction/menu handling,
transactional clone operations, block-entity value/codec/projection/index/wire
responsibilities, chunk snapshot/cache and mutation coordination, staged region
recovery, and shared parsing/bit utilities. It also tightens ownership and
failure paths in persistence, generated block IDs, and test process management.
The added regression code is not counted as source deletion or as proof of
universal parity. Untracked root `package.json` and `bun.lock` are unrelated
user-owned files and are excluded from this work's staging scope.

| gate | current result | exact scope / boundary |
|---|---|---|
| full RelWithDebInfo build | `PASS` | `timeout --foreground --kill-after=10 1200 cmake --build build -j8`; latest rebuild after the recovery and StructurePlacer fixes completed all 31 remaining Ninja steps |
| full non-nightly/non-package CTest (before the self-movement echo fix) | `72/72 PASS` | `timeout --foreground --kill-after=15 3600 ctest --test-dir build -LE 'nightly|package' --output-on-failure --timeout 600 -j1`; `788.20s`. Nightly soaks and package-labeled gates were excluded. This is not Actions or package/long-soak evidence; rerun after the latest C++ movement change remains required. |
| focused live gameplay | `3/3 PASS` | `goal_live_features` `57.54s`, `goal_live_interactions` `30.49s`, and `goal_live_remaining` `54.16s`; includes complete 7×7 (49 chunk) teleport return-view observation, menus, block interaction, portal, and gameplay commands |
| recovery regression | `120 PASS / 0 FAIL` | Includes fallback after seed rejection without leaking spawn/difficulty/border/forced-chunk/consumer state; a post-commit consumer exception propagates and leaves primary plus `.new` candidates intact rather than trying to combine them |
| configured-structure ownership regression | `29 PASS / 0 FAIL`; CTest repeated `5/5 PASS` | `StructurePlacer::load()` now preserves the feature name before moving the configuration into its map; regression exercises one-time loot/mob publication at negative coordinates and requires the fixture to load exactly once |
| Mineflayer live smoke (after self-movement echo fix) | `2 consecutive PASS / EXIT 0` | Mineflayer `4.25.0`, minecraft-data `3.117.0`; two 1.21.4 offline clients covered login/spawn, TP acknowledgement, chest GUI, movement, mining/loot, placement, filtered clone/tags, concurrent container edits, gamemodes, sign/block-entity data, player-data preservation, and restart persistence. The movement probe confirms the observer tracks the moving player and the moving player receives zero self entity-tracking packets. The fixture waits for support/headroom updates; the movement input is held for 500 ms to exceed its 0.2-block observation threshold reliably. This is bounded offline-client evidence, not authenticated real-client GUI or universal mod/client parity. |
| Mineflayer harness unit/syntax | `1/1 PASS`; Node syntax `PASS` | RCON out-of-order/timeout reply test and `node --check tests/mineflayer_live_smoke.cjs` |
| worktree/process hygiene | `PASS` | `git diff --check`; post-run `ps -C cppfm ...` found no remaining server/test processes. Root `package.json`/`bun.lock` remain untracked, user-owned, and untouched |
| post-fix production build | `PASS` | `timeout --foreground --kill-after=5 300 cmake --build build --target cppfm -j12`; 34 Ninja steps including relink after the self-movement broadcast fix. A full all-target build and non-nightly CTest rerun after this change remain required. |
| independent strict quality score | `PENDING` | Earlier numeric scores and findings were not a current whole-diff sign-off. Two fresh independent complete-diff reviews are in progress; one scoped review's post-commit recovery exception finding has been fixed and tested. A separate scoped clone review identified possible callback/observer atomicity concerns on rollback; current server wiring shows no client block packet before commit, but full reader isolation and dirty-generation behavior remain under review. No `≥6/10` claim is made. |
| PR and exact-head Actions | `PENDING` | No PR has been submitted for this worktree; local tests do not replace the required branch PR and exact-head GitHub Actions. |

The latest full suite initially exposed a test-environment dependency and a
real configured-feature registration bug. `goal_gameplay_bugs` is now run from
the source root because it intentionally reads checked-in structure fixtures;
`StructurePlacer::load()` copies the feature key before moving the parsed value,
so its configured and placed registries agree. The same loop also made level
metadata decoding transactional before candidate acceptance. Failures after
state application begins now abort startup instead of being misclassified as a
corrupt candidate and falling through to another save. Those changes were
followed by the full build and `72/72` CTest pass above. The subsequent
self-movement echo correction has a fresh `cppfm` target build and two clean
Mineflayer full runs; the full CTest aggregate is explicitly pending again for
that latest C++ change.

The latest review-driven compatibility corrections make empty player `Tags`
NBT serialize as `TAG_End` and load both Vanilla-empty and prior empty-string
forms; `/tag` now retains `PlayerRef`, enforces the Vanilla 1,024-tag limit,
and reports no-op add/remove as failures. Those details are covered by the
player-data transaction test and the live feature scenario. The [Yarn 1.21.4
`NbtList` reference](https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.1/net/minecraft/nbt/NbtList.html)
confirms empty lists report `END_TYPE`; the [Yarn 1.21.4 `Entity`
reference](https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.8/net/minecraft/entity/Entity.html)
documents the 1,024 command-tag limit.

### Storage, item-codec, and current-binary follow-up (2026-10-01)

Two earlier whole-diff hostile reviews scored code quality `5/10` and `4/10`,
and compatibility `4.5/10` and `3.5/10`. Their scores predate the fixes below
and remain the latest completed whole-diff scores until the current independent
reviews finish. They identified the LZ4 checksum mismatch, gzip Anvil rejection,
missing valid item component IDs 13/40, Vanilla in-flight piston resume, and
external Anvil sidecar transaction gaps. No repository-wide `6/10` claim is
made while fresh reviews and full validation are pending.

Pinned 1.21.4 research established LZ4 codec ID 4 as lz4-java 1.8.0's
LZ4Block stream (not LZ4 Frame), with default XXHash32 seed `0x9747b28c` and a
low-28-bit checksum value. A golden stream generated by lz4-java 1.8.0 now tests
the exact bytes. Anvil compression type 1 now uses a bounded RFC 1952 decoder
that validates trailers, accepts concatenated members, rejects trailing junk,
and shares RAII/error/budget handling with the zlib decoder. Region external
chunks read inline or sidecar raw/LZ4/gzip payloads when canonical, but staged
`.mca.new` generations with external chunks fail closed until MCA and `.mcc`
promotion can be coordinated. This remains **read support only** for external
sidecar data: writes, pair journaling, quarantine, and crash-pair recovery remain
incomplete; do not claim large-chunk Anvil parity.

The protocol/NBT codecs now cover vanilla 1.21.4 item component 13
`attribute_modifiers` and 40 `bundle_contents`, including nested non-empty
ItemStacks, protocol round trips, and bounds. Attribute raw IDs use the vanilla
registry table only; they cannot be assumed for modded Fabric registries. The
bundle element/payload/depth limits are project safety policy rather than a
vanilla component limit. Bundle selection accepts the vanilla `-1` deselection
sentinel. Vanilla in-flight piston block-entity resume is still unimplemented.

`WorldDataManager::tryLoadFile` now uses a bounded regular-file reader which
opens POSIX paths nonblocking and rejects FIFOs before parsing. Level and region
recovery results now expose post-rename directory-sync uncertainty rather than
logging it as an ordinary durable promotion. Exact fault-injection regressions
cover the level and region paths.

| latest gate | result | scope / boundary |
|---|---|---|
| focused RelWithDebInfo target build | `PASS` | Current `cppfm`, item-codec, storage, and recovery targets compiled and linked after gzip, LZ4, and item component edits; a clean complete all-target build is running below. |
| focused storage/recovery/item-codec CTest | `7/7 PASS` | `persistence_recovery`, `lz4_block`, `storage_file_ops`, `region_external`, `recovery`, `item_codec_components`, `item_block_predicate`; covers independent LZ4/gzip fixtures, recursive ItemStacks, and attribute payloads. |
| latest Mineflayer full live smoke | `PASS / EXIT 0 (1 final-source run)` | `npm --prefix tests/mineflayer run live-smoke`; two offline 1.21.4 clients exercised gameplay, restart persistence, Fabric callbacks, exact-entity `discard()` removal, PvP damage veto, callback re-entry, and concurrent callback delivery. During this rerun, the old test incorrectly followed a mutable “last loaded pig” handle and matched any pig's unload; the fixture now pins handle/ID at request time and waits for that entity's unload plus committed roster/handle invalidation. The initial 5s RCON timeout was raised to a bounded 15s after one cold callback-server run timed out. One final-source full run passes; repeated final-source stability is still pending. This is headless evidence, not official GUI/authenticated-client or broad Fabric-mod parity. |
| Mineflayer callback-only and harness checks | `13/13 PASS`; `1/1 PASS` | `node tests/mineflayer/run.cjs --jvm-callback-only`; RCON out-of-order/timeout unit test; `node --check tests/mineflayer_live_smoke.cjs`. |
| current-source focused gameplay/JVM CTest | `3/3 PASS` | `jvm_native_bridge`, `goal_gameplay_bugs`, and `gameplay_full`; `1.69s`. |
| focused RelWithDebInfo targets | `PASS` | `cppfm`, `test_native`, and `test_jvm_native_bridge` built or confirmed up to date after the current native changes; Java fixture regenerated after its test-only edits. |
| full all-target RelWithDebInfo build | `PENDING` | A previous all-target `-j2` attempt timed out; current successful build was target-scoped, so this is not an all-target pass. |
| full current-source CTest aggregate | `PENDING` | The recorded `72/72` non-nightly/non-package run predates the latest source changes; current verification is the focused `3/3` group above. |
| current whole-diff hostile reviews | `PENDING` | Two read-only independent reviewers are checking refactor/ownership/recovery and protocol/Anvil/item codecs. Prior scores are not current scores. |

The latest new CTest targets and source edits remain local/unmerged. A full
all-target build and complete non-nightly CTest are pending, as are
package/Actions gates, current whole-diff adversarial reviews, PR submission,
and exact-head Actions. External `.mcc` *write/atomic pair recovery* and
vanilla in-flight piston resume remain P1 storage/behavior work; Mineflayer
passing does not close those gaps.

This evidence is limited to the exercised implementation paths. Arbitrary JVM
mods, full RNG L3 world-generation call-order parity, accepted 2h/24h soak,
authenticated real-client/GUI coverage, and package-labeled release gates
remain declared boundaries. The quality goal stays active until two fresh
reviews score the complete source diff at least 6/10 and any material findings
are fixed and reverified.

### Fresh non-nightly test audit and login-capacity regression correction (2026-10-02)

The previous complete non-nightly/non-package CTest run was **73/81 PASS**.
Its eight failures were `smoke80`, `jvm_compatibility`, `jvm_corpus`, `plan43`,
`flood_net`, `goal_security_guards`, `mob_stats_full`, and `recipes_mirror`.
Several were stale test/data expectations rather than product regressions:
the security source guard expected a renamed dispatcher, the Camel breeding
row omitted its cactus item, and the recipe test counted an intentionally
rejected `minecraft:air` recipe as loaded. Those expectations/data are now
corrected; focused checks pass (`goal_security_guards`, `mob_stats_full`
131/131, `recipes_mirror` 77/77).

The `flood_net` capacity test also expected a server-full packet immediately
after Login Start, but this server reserves capacity when the client completes
Configuration and acknowledges the transition to Play. The test now races two
full protocol clients through that admission point, and the test client
recognizes a Play Disconnect during startup. Focused `flood_net` now passes.
The JVM API health-boundary fixture builds and `jvm_api` passes 1/1.

At the time of this audit, horse taming remained a flaky/state-sensitive
`plan43` failure: one isolated rerun passed, while a repeated run rejected the
19 golden-carrot interactions and lacked the expected re-mount/tame metadata.
The later 2026-10-02 CTest run passed `plan43`, `smoke80` passed separately,
and both JVM corpus and strict compatibility tests passed in the 83-test
selection recorded below. Two additional isolated `plan43` reruns then passed
in `23.59s` and `18.41s`; the earlier failure has not reproduced across these
three latest current-source runs. Keep the earlier flake in the historical
record, but the old `73/81` aggregate is superseded by the newer evidence.

### Post-lifecycle, player-API, and Mineflayer verification (2026-10-02)

The JVM lifecycle bridge now separates Fabric `SERVER_STARTING` from the
post-world-load `SERVER_STARTED` phase. `PlayerManager` and world access remain
unavailable during the early callback; world/player lookup helpers and entity
selectors return empty results rather than dereferencing that unavailable
service. Shutdown retains `SERVER_STOPPING` → world unload → persistence close
→ `SERVER_STOPPED` → JVM stop ordering. The verifier distinguishes the JVM null
verification type from ordinary references when merging stack frames. An
independent lifecycle/verifier review found no high-confidence defect in that
scoped diff; this is not a complete repository review.

An independent Mineflayer-harness review found that stdout and stderr fragments
were combined when recognizing fixture events, which could fabricate a marker
across streams. The output observer now tracks chunk fragments and complete
fixture lines per stream; regression tests reject cross-stream assembly and
prove same-stream evidence remains latched after the diagnostic tail is
truncated. The review also found the callback-thread final assertion was using
`.equal` on the harness's local assertion function; the assertion is now called
in the correct form. One initial probe then failed during that assertion and
reported an unclean shutdown; isolated callback-only and complete live reruns
both exited successfully after the fix. A whole-diff reviewer also noted the
combined gameplay-plus-callback watchdog was shorter than the separate
workflow budget; the in-process limit is now 540 seconds within the CI command's
600-second timeout. CI dependency installation is independently bounded to
600 seconds plus a 10-second kill grace, with an 11-minute step limit.

A second bounded review confirmed a vanilla ABI mismatch: the local
`FishEntity` inherited from `AnimalEntity`, while Yarn 1.21.4 defines it under
`WaterCreatureEntity` → `PathAwareEntity`; the official
[FishEntity reference](https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.1/net/minecraft/entity/passive/FishEntity.html)
and [WaterCreatureEntity reference](https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.1/net/minecraft/entity/mob/WaterCreatureEntity.html)
document that inheritance chain. The JVM shadow hierarchy now matches it, and
an API contract test checks both assignability and the non-Animal relationship.
The same review found live checks still reading the
combined stdout/stderr diagnostic tail. Evidence checks now use per-stream
buffers and complete fixture lines; callback ordering is only accepted when
the full sequence appears in one stream. The Java-created entity insertion
path (`ServerWorld.spawnEntity` returns false until a real native registration
boundary exists) remains a declared compatibility limitation, not a test
failure.

Both reviewers returned bounded, not exhaustive, whole-diff assessments. Their
pre-fix provisional strict scores were code quality `5.5/10` and compatibility
`~4/10`; neither signed off at 6/10, and the compatibility score predates the
Fish hierarchy and log-evidence corrections. They reviewed substantial
high-risk slices but explicitly did not inspect every changed line. Treat
these as warnings and not a current final score.

| gate | latest result | scope / boundary |
|---|---|---|
| full RelWithDebInfo build | `PASS` | All targets built with timeout-wrapped `cmake --build build -j4`; the latest incremental all-target build regenerated the Fish/WaterCreature hierarchy, JVM fixtures, and embedded resources. |
| JVM runtime/API/corpus/compatibility/package | `5/5 PASS` | Latest focused run passed `jvm_api` (including pre-world-load and fish-hierarchy contracts), `jvm_compatibility`, all 25 `jvm_corpus` fixtures, `package_archive`, and `package_jvm_smoke`. |
| smoke80 | `1/1 PASS` | Latest rerun `ctest --test-dir build -R '^smoke80$' --output-on-failure --timeout 600`; `385.82s`, after the Fish/WaterCreature hierarchy and Mineflayer evidence-helper changes. |
| non-soak CTest | `83/83 PASS` | Latest broad run `ctest --test-dir build --output-on-failure --timeout 600 -E '^(smoke80|soak2h)$' -j4`; `301.21s`, includes the 300-second `soak_bot` and both package tests, and was run after the Fish hierarchy and initial per-stream evidence changes. The final diagnostic-tail separator change was followed by fresh Mineflayer unit and full live runs. The two-hour `soak2h` was not run. |
| Mineflayer full live smoke | `PASS / EXIT 0` | Latest final-source run after the per-stream diagnostic-tail separator change; Mineflayer `4.25.0`, minecraft-data `3.117.0`, two offline 1.21.4 clients, gameplay plus strict embedded-JVM/Fabric callbacks. Covers login/spawn, recipes and menus, food/effects, combat cancellation, mining/placement, commands/datapacks, movement, restart persistence, entity load/unload, native re-entry, and serialized callbacks. No authenticated online-mode client, official graphical client, or arbitrary third-party Fabric mod was tested. |
| Mineflayer callback-only and harness unit tests | `PASS / EXIT 0`; `3/3 PASS` | Isolated callback probe cleanly stops its server; Node tests cover RCON response ordering, rejection of cross-stream fabricated evidence, and truncation-resistant mismatch detection. The 3/3 unit run, `node --check`, and CI workflow YAML syntax check all pass after the final diagnostic-tail change. |
| source hygiene | `PASS` | `git diff --check`; post-run process inspection found no remaining cppfm/Mineflayer process. The exact 16 MiB temporary world retained by the initial failed probe was removed after successful reruns. |
| strict whole-diff review / PR / Actions | `PENDING` | Earlier independent bounded reviews found/fixed issues but did not inspect every changed line, with pre-fix scores below target. Three fresh parallel adversarial domain reviews are in progress; JVM/build/docs/test review and a current whole-diff score still remain. No PR has been submitted and no exact-head Actions run exists; do not claim the repository-wide quality target or an overall ≥6 score yet. |

This rerun supersedes the pending aggregate statement above for the listed
current gates. `plan43` also passed in two isolated consecutive reruns after
the full CTest selection, giving three current-source passes since its earlier
state-sensitive failure. The quality goal remains active. Three domain reviews
are currently in flight; after they return, cover the remaining JVM/build/docs/test
diff, fix and retest material findings, then obtain fresh independent strict
scores. After review converges, repeat the broad CTest gates, create the
required PR, and pass exact-head Actions. Online auth, official GUI,
arbitrary-mod behavior, and the two-hour soak remain open.

## 2. Prior plan48 and cleanup record

| item | state at this tracker | evidence / scope |
|---|---|---|
| canonical specifications | `REFRESHED` | canonical index and WIRE/GAMEPLAY/OPS/DEVELOPMENT/VERIFICATION docs point at the merge baseline |
| archive | `DONE` | historical assessment files are present under `docs-legacy/` and their index links resolve |
| stable tracker and fixture | `CURRENT` | this tracker and `docs/mob_stats_149.csv` retain their stable paths and checksum |
| final-gates evidence | `CONFIRMED` | current full non-nightly CTest and clean extracted-package harness both pass locally; ignored `build/` outputs are local evidence, not tracked/public release artifacts; declared scope/evidence boundaries remain below |
| publication | `BLOCKED` | E-14, full world-generation L3, accepted long-run evidence, and retained release artifacts remain explicit boundaries even though the executable gates and bounded local client probe pass |

The cleanup commit `db12df96093a0869e958f62b11f9a9cd68ba3ef1` removed the legacy
Structures API: 10 files, `+22/-787`, with source/test legacy-reference grep `0`.
The safety commit `4526dfe4f7112b1fe744a83484ef5ef40176d481` introduced a
self-nonmatching replay cleanup pattern. The current `tools/replay_vanilla.py`,
`tests/test_server_full.py`, and `tests/run_plan43_suite.py` now use PID-scoped
cleanup so one harness cannot terminate an unrelated concurrent server.

## 3. Residual cleanup and parity boundaries

Plan47 cleanup is not a complete vanilla-parity or release audit. Residuals are kept
explicit rather than being converted into a broad PASS:

| residual | status | reason / next owner |
|---|---|---|
| `Structures.hpp` legacy API | `RESOLVED` | removed by `db12df96093a0869e958f62b11f9a9cd68ba3ef1`; `StructureManager`/`StructurePlacer` remain the current structure owners |
| session mining versus `MiningCalculator` | `RESOLVED (plan49 scope)` | session start/finish and tick completion now share `MiningCalculator` context/results; `test_mining_full` is `59/59` and live smoke/server paths pass |
| `MobBehaviorSpec` coverage | `RESOLVED (plan49 scope)` / `DECLARED-LIMITATION` | 12 descriptor rows are wired to live AI and gameplay assertions; broader species-wide vanilla equivalence remains outside this targeted plan |
| plan51 embedded JVM boundary | `IMPLEMENTED-PARTIAL` | default-on HotSpot/JNI bridge when configure/build finds the required JDK/JNI inputs; a compatible runtime JDK/classes are still required, and a binary built without JNI remains native-only even if a JDK is installed later; shadow ABI, selected events, version-locked pre-definition transformer, selective routing, 25/25 corpus, and official Loader/Knot probe pass; arbitrary JVM mods and universal bytecode compatibility remain declared limitations; see [PLAN51_JVM.md](PLAN51_JVM.md) |
| one-file distribution/runtime layout | `IMPLEMENTED-PARTIAL` | embedded repository-owned assets/classes, fresh-directory tree creation, user-file preservation, fail-closed CPack resource preflight, separate package JVM smoke when JNI/classes are available, and CPack one-executable ZIP; the Linux package gate was verified locally from ignored `build/` output, not a tracked/public release artifact; Windows source path requires a native Windows build and host native libraries/JDK remain external |
| retained marker/comment inventory | `NOT-FULLY-MEASURED` | the cleanup grep was `0` for legacy references, but no complete zero-marker inventory was proven; do not claim one |
| `tools/soak_bot.py --duration 300` | `RESOLVED` | three integrated main runs passed; each had KeepAlive `30`, chunks `182`, time updates `300`, kicks/EOF/server-exit/transport/protocol errors `0`, and owned cleanup PASS |
| chunk generation/save/unload memory | `IMPLEMENTED; 300S PASS / 2H NOT-ACCEPTED` | generation is serialized per world; async save no longer copies a full `Chunk`; the retained 300s goal run passes with post-fill RSS growth `1.0%`, while the requested 7200s run stopped at `t=1200s` with exit `-9` |
| accepted 2-hour/24-hour evidence | `INTERRUPTED / ABSENT` | the retained 7200s artifact stopped at `t=1200s`; no accepted 2-hour or 24-hour artifact exists; procedures are not results |
| current real-client/GUI evidence | `UNAVAILABLE / CURRENT HOST` | The retained capability probe found no `DISPLAY`/`WAYLAND_DISPLAY`, no installed vanilla launcher/client, and no `glxinfo`; protocol tests are not rendering evidence. See [goal-gui-soak.md](audit/goal-gui-soak.md). Earlier local-client notes remain historical and have no retained release artifact. |
| official Fabric API/Yarn ABI audit | `PASS / INFORMATIONAL` | Fabric API `0.119.4+1.21.4` common/server-side surface cross-checked against Yarn `1.21.4+build.8`: 206 top-level classes and 1,699 public members; no exact class/member descriptor gap in the selected surface; client/datagen/renderer/internal-only classes excluded |
| locked real public-mod corpus | `PASS / BOUNDED` | Lithium, FerriteCore, and Carpet plus the combined run pass with the explicit Java 21 launcher; latest local ignored report `build/real-mod-corpus/real-mod-corpus-report-after-diagnostics-20260908-v1.json` records zero classified process diagnostics; it is not a tracked/public artifact and does not establish arbitrary-mod compatibility |
| wider Modrinth candidate probe | `PASS / BOUNDED` | 12 pinned entries classify as 8 target-compatible/runtime passes and 4 explicit non-target/invalid cases; latest corrected local ignored report `build/real-mod-candidates/compatibility-candidates-report-final-20260908.json`; it is not a tracked/public artifact and provides bootstrap/clean-shutdown evidence only |
| structural provider linkage preflight | `DIAGNOSTIC (raw FAIL)` | official server/libraries with Tiny namespace mapping were scanned; raw Mixin target/injected-member references remain (Lithium 2 classes/220 members, FerriteCore 19/15, Carpet 49/149), so this intentionally non-gating result is a conservative preflight signal rather than a runtime failure |

The `RESOLVED` Structures API row does not close the structure-generation parity
boundary. The RNG primitive and splitter contract is now independently covered by
`test_rng_parity`; full vanilla Xoroshiro call-order and structure-NBT parity is
still not proven, and historical numbered-row `DONE` values are not universal
parity claims.

## 4. Exact final-gates evidence

These records combine prior named evidence with the final results confirmed for
the integrated working tree on 2026-09-19. Results are identified by their target
names; package evidence is explicitly identified as a clean extracted-directory
run rather than being conflated with the source-tree harness. Any `build/` path
below is an ignored local output from that run, not a tracked/public evidence or
release artifact unless separately published:

| target | result | status / consequence |
|---|---|---|
| configure/build | current timeout-wrapped full Ninja rebuild and package target completed; the earlier clean RelWithDebInfo baseline was `129/129` targets | `PASS` |
| `runtime_layout` | `PASS` | fresh-directory tree/resource extraction and sentinel preservation |
| CPack package | `PASS` | CPack produced ignored local `build/packages/cppfabricmc-1.21.4-Linux-x86_64.zip`; ZIP contains exactly one `cppfm` executable, archive size `54999329` bytes, SHA-256 `61b19c83100b755b06431c2568e5277e4251867b4b25df98c27ab44118d84b8b`; CPack resource preflight and clean extraction tested; not a tracked/public release asset |
| `package_jvm_smoke` | `PASS` | exact CPack ZIP extracted without checkout assets/classes overrides; default-on strict JVM startup, 1,457 embedded class files, registry assets, and owned clean shutdown verified |
| incremental Ninja build | `ninja: no work to do` | `PASS` |
| `test_scoreboard_reset` | `22 PASS 0 FAIL` | `PASS` |
| `test_spec_wire` | `417 PASS 0 FAIL` | `PASS`; official 1.21.4 EntityTeleport field layout is covered |
| `test_wire_full` | `399 PASS 0 FAIL` | `PASS` |
| `test_wire_b6` | `136 PASS 0 FAIL` | `PASS` |
| `test_fuzz` | `25 PASS 0 FAIL` | `PASS` |
| `test_gameplay_full` | `806 PASS 0 FAIL 806` | `PASS`; arbitrary JVM-mod boundary remains informational |
| `test_seed_parity` | `201 PASS 0 FAIL` | L1/L2 deterministic evidence |
| `test_rng_parity` | `25 PASS 0 FAIL` | Java LocalRandom, Minecraft Xoroshiro seed expansion, primitive outputs, and long/coordinate/string splitter vectors; full worldgen call-order/NBT parity remains open |
| `test_mining_full` | `59/59` | `PASS`; plan49 authoritative session/tick mining |
| `test_block_hardness_full` | `16/16`, `1095 mismatch=0` | `PASS` |
| `test_mob_stats_full` | `131 PASS 0 FAIL` | `PASS` |
| `test_redstone_engine_full` | `42 PASS 0 FAIL` | `PASS` |
| `test_fluids` | `23 PASS 0 FAIL` | `PASS`; directional water/lava interaction, waterlogging, falling states, Nether evaporation, and queue deduplication |
| `test_menu_logic` | `41 PASS 0 FAIL` | `PASS`; bounded enchanting offers and atomic crafter redstone crafting |
| `test_recipes_mirror` | `76 PASS 0 FAIL` | `PASS` |
| `test_native` | `ALL PASS` | `PASS`; includes bounded foreign-thread mutation routing, stop-time cancellation, and dimension-aware entity World handles; no invented aggregate count |
| `test_recovery` | `55 PASS 0 FAIL` | `PASS`; includes valid `level.dat.new` promotion, corrupt-primary quarantine, byte preservation, player-data quarantine, region recovery, and session-lock cases |
| `test_plan43` | `87 PASS 0 FAIL` in the final rerun | `PASS`; command authority, signed-command offset, abilities, horse/use-entity, movement/fall, sign persistence, and liveness |
| `test_smoke_80` | `225 PASS 0 FAIL` | `PASS`; isolated operator fixture and owned cleanup |
| `test_server_full` | `240 PASS 0 FAIL / 240 total` | `PASS` from the source-tree full live matrix; package/source evidence remain distinct |
| `properties` | `33 PASS 0 FAIL` | `PASS`; table-driven properties/CLI matrix and informational-flag side-effect checks |
| `lifecycle_matrix` | `8/8 PASS` | `PASS`; fail-closed readiness, signal, restart, lock, malformed-input, port-reuse, and process-cleanup matrix |
| multi-client | `ALL PASS` in `17.48s` | `PASS` |
| bot smoke | `ALL PASS` in `20.82s` | `PASS` |
| full non-nightly CTest regression | final full run `54/54 PASS` | latest registered set includes smoke80, properties, lifecycle, all three live feature fixtures, `tautology_lint`, and `mcproto_framing`; the separate release-only `package_jvm_smoke` gate is not folded into this aggregate |
| cleanup-hardening performance comparison | `HISTORICAL` | prior 45-test comparison retained as historical context; current aggregate is the separate 54-test rerun above |
| view32 dry benchmark | `PASS` for 4,225 chunks; p50 `0.107ms`, p95 `2.332ms`, peak RSS ~`95MB`, hit rate `84.6%`, OOM/kick `0` | strict synthetic dry result |
| 120-client stress | `120/120 joined PASS` in `68.5s` | `PASS`; server tick remained alive and owned cleanup completed |
| `tests/soak_test.py --duration 60` | `PASS`; 30 keepalives, 0 disconnects, 590 actions, post-fill RSS growth `1.0%` | latest short post-review concurrency/cleanup smoke; not 2h/24h |
| `tests/soak_test.py --duration 300` | `PASS`; 150 keepalives, 0 disconnects, actions `2899`, post-fill RSS growth `0.2%` | final bounded synthetic soak; not 2h/24h |
| `tests/soak_test.py --duration 600 --movement-range 3000` | `PASS`; 300 keepalives, 0 disconnects, actions `5707`, post-fill RSS growth `6.6%` | post-fix wide synthetic soak; not 2h/24h |
| `tests/soak_test.py --duration 1800 --movement-range 3000` | `PASS`; 900 keepalives, 0 disconnects, actions `17493`, post-fill baseline `114504kB`, max `128868kB`, growth `12.5%` | `17ab09f` bounded allocation-reuse diagnostic; not 2h/24h |
| `tests/soak_test.py --duration 7200 --movement-range 3000` (parent `d1c6a7f`) | interrupted/not accepted at recorded `t=3361s`; post-fill baseline `160388kB`, max `191612kB`, growth `19.5%` | exceeded the `15%` post-fill gate before completion; retain as a negative/diagnostic artifact |
| `tools/soak_bot.py --duration 300` | `3/3 PASS`; each run KeepAlive `30`, chunks `182`, time updates `300`, kicks/EOF/server-exit/transport/protocol errors `0`; cleanup PASS | resolved 300s bot gate |
| focused executable regression suite | wire/gameplay/ops executables pass; the JVM boundary is declared rather than encoded as a test failure | no aggregate invented |
| `test_jvm_handles` | `PASS` | generation-safe opaque handle invalidation/address reuse and selective native/JVM routing |
| `cppfm_jvm_classes` / `cppfm_jvm_fixture` | `PASS` | Java shadow ABI and deterministic server-side fixture compile |
| JVM fixture ABI invalidation | `PASS` | CMake now depends on the Java classes stamp, so a changed shadow annotation/API recompiles the fixture instead of reusing stale bytecode |
| `jvm_runtime` | `PASS` | embedded HotSpot, entrypoint, command registration, World API, lifecycle, Mixin HEAD/RETURN/Overwrite, tick, and owned clean shutdown |
| `jvm_transformer` | `PASS` | pre-definition transformer contract, verifier-safe bytecode rewrite, callback/local preservation, transform-order checks, MixinExtras operations, `@Share`, `@Local` selectors, and acronym accessor inflection (`ROOT`/`URL`) |
| `jvm_api` | `PASS` | Fabric-style event, command, registry, and networking callback surface contract |
| `jvm_compatibility` | `PASS` | all 25 historical plan51 fixture cases pass in one `cppfm` process; the same harness also passes the auxiliary functional API fixture; report status `PASS`, 25/25 report fixtures, 0 errors; three consecutive direct reruns also passed |
| `jvm_corpus` | `PASS` | executable 25-case compatibility corpus passes end-to-end |
| `jvm_manifest` | `PASS` | declarative protocol-769 ABI manifest reproducibly generated; 94 methods (47 native + 47 wrapper), 9 structured methods, 10 injection points, 14 transformer names |
| `jvm_contract_audit` | `PASS` | every declared ABI method has exactly one native or wrapper backend classification |
| `shadow_abi` | `PASS` | standalone dependency-complete Shadow ABI compile/reflection gate; 906 source classes, 763 class files, and 8,297 declared/audited members |
| explicit no-JNI configure/build + regression | `PASS` | CMake fallback disabled with `CPPFM_ENABLE_JNI_FALLBACK=OFF`; native targets omit `CPPFM_HAS_JNI`, build cleanly, and the same-commit non-package CTest set passes `42/42`; relative source assets were linked for the external build directory |
| ASan/UBSan key regression set | `4/4 PASS` from the repository root; no sanitizer report | `build-sanitize` uses `-fsanitize=address,undefined`; the four targets were rebuilt and run directly with relative assets visible |
| static quality audit | `PASS` — 33 C++ test files, 156 production files, 48 Python files | rejects unconditional assertions, liveness-only fallbacks, bare Python exceptions, and unowned `Popen` launches |
| official Loader/Knot probe | `PASS / DECLARED-LIMITATION` | offline pinned Loader 0.16.9/Knot/Mixin probe records all seven expected markers; local ignored process output is `build/fabric-runtime/probe-evidence-after-fabric-docs-20260908-v1.json`; it is not a tracked/public artifact, and no Mojang server/provider is shipped |
| embedded official-provider probe | `PASS / DECLARED-LIMITATION` | C++-owned HotSpot starts the pinned official Loader/Knot target and records the handoff/mixin markers; local ignored output is `build/fabric-runtime/embedded-evidence-after-fabric-docs-20260908-v1.json`, not a tracked/public artifact |
| `mod_linkage` / `real_mod_harness` / `real_mod_candidates_harness` | `PASS` | synthetic nested-JAR/Tiny-mapping linkage cases, fail-closed runtime-diagnostic classifier, locked candidate manifest, and offline missing-cache no-false-PASS contract pass |
| `real_mod_corpus` | `PASS / BOUNDED` | Lithium, FerriteCore, Carpet, and combined runtime probes pass with explicit Java 21; latest local ignored report is `build/real-mod-corpus/real-mod-corpus-report-after-diagnostics-20260908-v1.json` with zero classified process diagnostics; it is not a tracked/public artifact and provides no arbitrary-mod or client/GUI claim |
| wider Modrinth candidate probe | `PASS / BOUNDED` | 12 locked entries: 8 Fabric 1.21.4 target-compatible candidates pass JVM/mod bootstrap and owned clean shutdown under Java 21; 2 Create entries are target-incompatible, C2ME requires Java 22+, and Debugify has invalid metadata; corrected local ignored report is `build/real-mod-candidates/compatibility-candidates-report-final-20260908.json`, not a tracked/public artifact |

The three `soak_bot` runs close the former bot-soak blocker. The chunk memory/generation
follow-up is covered by the passing 600-second wide soak and the new 1800-second
allocation-reuse diagnostic pass, but the attempted 7200-second
run was interrupted at the recorded `t=3361s` after exceeding the post-fill RSS gate
(`160388→191612kB`, `+19.5%`). No accepted 2-hour/24-hour run artifact or retained
real-client/GUI release artifact exists, although bounded local mc-pilot and
PrismLauncher probes pass. The structural provider linkage scan is intentionally
conservative: it exposes raw Mixin target/injected-member gaps, while the runtime
corpus report records zero classified process diagnostics for the three locked mods
and their combined run. The recorded full CTest baseline and the separate clean
extracted-package gates are green; the package test remains intentionally
excluded from the non-nightly aggregate and is recorded as its own release
gate. Publication remains blocked only by the declared boundaries below.

## 5. Declared limitations

- **E-14 Fabric JVM-mod boundary:** plan51 now executes a bounded dependency-free
  shadow ABI through default-on embedded HotSpot/JNI when configure/build finds the
  required JDK/JNI inputs. A JNI-capable binary still needs a compatible runtime JDK
  and classes; a binary built without JNI remains native-only even if a JDK is
  installed later, so native fallback does not retrofit JVM support. The selected
  callbacks, version-locked pre-definition class-file transformer, MixinExtras operation
  support, and selective native/JVM routing remain bounded. The 25-case dependency-free corpus,
  pinned official Loader/Knot stack, and three locked real server-side mod cases
  pass. The production path is still not the Mojang GameProvider; arbitrary Fabric
  JVM mods and universal bytecode compatibility remain unsupported. The boundary
  is reported as a limitation; it is not represented by an intentional test failure.
- **Vanilla Xoroshiro L3:** `test_rng_parity` independently covers the Java/Minecraft
  primitive algorithms, seed expansion, bounded outputs, and splitters (`25 PASS /
  0 FAIL`). The complete world-generation call ordering and structure-NBT output
  still lack retained vanilla vectors, so the full L3 claim remains a declared
  boundary.
- **Long-run evidence:** the three 300-second bot runs, the 300-second synthetic soak,
  and the post-fix 600-second wide soak pass. The attempted 7200-second run was
  interrupted at the recorded `t=3361s` after post-fill RSS reached `191612kB` from a
  `160388kB` baseline (`+19.5%`, above the `15%` gate); no accepted 2-hour/24-hour
  artifact exists.
- **Real-client evidence:** bounded local mc-pilot and PrismLauncher checks pass.
  The mc-pilot-managed Fabric 1.21.4 client passes login, world entry, stability,
  chat/command/block/status/screenshot checks; PrismLauncher 11.1.0 launches a
  Fabric 1.21.4 client through its CLI and joins cppfm with an existing
  authenticated account. Logs and screenshots were temporary local evidence,
  not retained release artifacts; first-time Microsoft interactive login, every
  gameplay path, and all PrismLauncher instance configurations remain untested.

## 6. Fixture

- `docs/mob_stats_149.csv` remains the runtime-default fixture and is byte-identical
  to `docs-legacy/mob_stats_149.csv`.
- `fixture_sha256: b75697102502385b6aee913f0aca80b86cce323a4994b16a29baf408b5ef2f6f`.
- `fixture_shape: PASS` — `149` data rows / `11` columns.

## 7. Final documentation checks

The timeout-wrapped documentation validation passed for Markdown links and anchors
(including explicit `<a id="…">` anchors), required files and schema, the fixture
checksum/shape, stale-hash and stale-claim grep, scope, and `git diff --check`.
These checks validate publication hygiene only; they do not turn E-14, missing L3
proof, missing accepted long-run artifact, or the lack of a retained client artifact
into a universal compatibility claim.

The Plan54 primary measurement counts `98,648` lines across `298` files in
`src/`, `tests/`, and `tools/` (C++/header/Python/Java suffixes), versus the clean
`65a7c69` baseline at `96,654` lines across `293` files. The protected manifest is
`80` files / `8,939` lines with zero hash drift. Mutable lines are
`87,715 → 89,709`, a net **increase of 1,994 (+2.28%)**. The strict 18,341-line
reduction target is therefore `PARTIAL`; deleting feature code, fixtures,
assertions, or evidence would not be an acceptable substitute for a safe refactor.
That Plan54 count is historical. The current dirty-tree repeat after the three
owned live fixtures and goal regressions is `308` files / `101,045` lines; against
the goal freeze of `302` / `100,991`, this is `+54` lines. The current protected
manifest is `80` files / `5,994` lines with `0` hash mismatches (see the retained
measurement logs under `/tmp/grok-goal-e8af7c070433/implementer`).

The final cleanup pass removed the unreferenced legacy `World::fillTerrain` body
by delegating the compatibility entry point to `fillTerrainV3`, the dead Redstone
registry, obsolete CombatManager damage wrappers, unused packet conversion helpers,
and several unreferenced compatibility accessors. The protected manifest remains
unchanged.

The accepted cleanup ledger is: items `-5`, native process harness `-81`, session
login paths `-13`, command policy `-3`, and JVM bridge `+12`; configuration and
properties evidence added `+671`, and the lifecycle matrix added `+1,352` within
the primary scope. The final cleanup-hardening assertions/source-policy checks
add `+61` lines. The baseline registration diff is `46 → 50` add-test entries;
the current configuration contains `57` total tests (`54` after excluding
`nightly` and `package`). The added gates are `properties`, POSIX-only
`lifecycle_matrix`, `goal_live_interactions`, and `goal_live_remaining`, with no
existing target removed.
The net matches the reproducible path-by-path measurement.

The timeout investigation found two independent causes of misleading outer
`timeout` failures: parent-only termination left descendants holding pipes,
and readiness/output loops did not always observe the owned child state. The
Python/diagnostic harnesses create process groups, use monotonic deadlines,
probe actual server status, terminate and reap the owned group with bounded
escalation, and report cleanup failure explicitly. The C++ `ServerProcess`
owner now checks `kill`, `waitpid`, and temporary-world removal and aborts on
destructor cleanup failure. The disconnected-session path also marks the player inactive before slow persistence/hooks. The last completed
live-server runs left no `cppfm` process behind. The prior full CTest baseline
passed `53/53` in `469.29s`; the latest 54-test non-package run passed `54/54`
under cumulative load;
the release-only package JVM gate is tracked
separately. The final 120-client stress run joined `120/120` clients in `68.5s`,
and the final 300-second soak completed with 150 keepalives, zero disconnects,
2,899 actions, and `0.2%` post-fill RSS growth.

The earlier one-file package verification passed locally: the Linux ZIP in the
ignored `build/` output contains only `cppfm`, and its clean extracted-directory
`test_server_full` run passed `234/234`; the separate `package_jvm_smoke` gate
also passed strict default-on JVM startup against that ZIP. The broader source-tree
matrix's historical `240/240` result remains separate; the latest selected
`conn,commands,permissions,chat,datapack,persistence,restart` suites passed
`198/198` in `/tmp/grok-goal-e8af7c070433/implementer/server-full-final.log`.
The ZIP is not a tracked/public release asset.
The interrupted 7200s soak remains a negative diagnostic artifact. The accepted
long-run and retained-release-artifact boundaries remain explicit even though the
bounded local real-client probe passes. The active broader
compatibility goal can continue expanding API, constructor/verifier-state,
behavioral, and real-mod coverage; the structural linkage report should be used to
prioritize those changes, not presented as a universal runtime verdict.

## 8. Plan49 implementation and evidence handoff

Plan49 research, implementation, integration, and focused gates are complete for the
targeted issues. The following rules remain in force for future work:

| handoff item | current state | update rule |
|---|---|---|
| workflow authority | `RECORDED` | keep `DEVELOPMENT.md#research-workflow` canonical; the legacy stub remains a pointer |
| packet map | `RECORDED` | recheck `src/proto/Ids.hpp` and named wire vectors; do not change IDs by prose |
| evidence counts | `UPDATED` | replace only with a closed run artifact; never infer PASS from a procedure or old count |
| strict 78-gap history | `HISTORICAL` | do not merge it into the 90-row taxonomy count |
| `AGENTS.md` handover | `UPDATED` | the authorized handover change points to the canonical workflow, current 16-viewpoint research, protocol-769 IDs, and timeout-safe process cleanup |
| publication | `BLOCKED` | executable gates pass; declared arbitrary-mod/E-14, full world-generation L3, long-run, and real-client boundaries still prevent universal release sign-off |

## 9. Active bounded goal evidence

The goal-specific audit files under `docs/audit/` record a fixed dirty-tree
baseline and protected manifest, a 90-row coverage ledger (`14 PASS`, `38 PARTIAL`,
`38 UNVERIFIED`), three owned real-client fixtures, real command/entity/menu transcripts, and 23 reproducible network/gameplay defects
with focused regression tests. The PR #1 review completed five hostile rounds
and a final follow-up;
the current verdict is `P0=0, P1=0, P2=0, P3=0` unresolved after the focused
fixes and 8/8 local CTest rerun. The exact PR-head Actions run
[`35865145988`](https://github.com/nico2525nn/cpp-fabricmc/actions/runs/35865145988)
passed before PR #1 was merged. Source-order
guards cover security fixes where no authenticated client or external JVM fixture
is available. Scoped production cleanup measured `-9`
lines; the additional refactors are recorded in `goal-cleanup-runtime.md` and
`goal-cleanup-game.md`. The requested 10,000-line reduction was not reached; the
fixed production GC sweep found only small dead entrypoints after these removals,
so no
protected, generated, fixture, assertion, or feature code was deleted to
manufacture that metric. GUI, arbitrary-mod, worldgen-L3, signed-command,
moving-piston-NBT, and accepted long-soak boundaries remain explicit.
The earlier focused feature-entry CTest passed `2/2` in `76.45s`; the additional
remaining-entry fixture passed `1/1` in `63.93s`, and the final two-launch
protocol matrix passed with both owned processes returning `0` and no escalation.
The requested 7200-second soak failed at `t=1200s` with server exit `-9`; no
accepted long-soak result is claimed.

The first post-merge main Actions run
[`35868844643`](https://github.com/nico2525nn/cpp-fabricmc/actions/runs/35868844643)
timed out `smoke80`; a same-SHA rerun completed the suite but exposed one
`goal_live_remaining` failure. That fixture shrank the world border while the
player was still far outside it, then issued a teleport; on a slower runner,
border damage could remove the command target before the teleport feedback was
observed. Locally, `smoke80` passed all 225 cases in `178.25s`, and the full
pre-fix non-nightly CTest run passed `55/55` in `522.97s`. The follow-up makes
the fixture teleport to its damage position before shrinking the border, then
checks the same border wire and damage effects; the targeted post-change test
passed `5/5` consecutive launches (`263.24s`).

The PR #3 Actions run
[`35877465112`](https://github.com/nico2525nn/cpp-fabricmc/actions/runs/35877465112)
then exposed a separate `smoke80` issue: CTest reported `***Timeout 450.01 sec`
even though the workflow supplies `--timeout 600`, because the test had its own
`TIMEOUT 450` property. Its captured output reached the world-management cases,
then the block-tick scenario using `randomTickSpeed 100`; subsequent disconnect
and assertion failures were cascading output after the timeout. The follow-up
removes the target-specific timeout so the shared CTest limit applies and uses
`randomTickSpeed 10` (still above vanilla's default 3) with the existing 16×16
crop field. This keeps the real random-tick behavior check while avoiding a
whole-simulation stress setting in the ordinary regression suite.

The exact-head follow-up Actions run
[`35883033964`](https://github.com/nico2525nn/cpp-fabricmc/actions/runs/35883033964)
then completed the expanded CTest suite without the smoke timeout; one separate
`plan43` assertion (`W-02 m2/h0/s1 window opens`) failed, leaving the other 54
tests passing. Investigation found that `summonHorseNear` accepted any horse
spawn observed after its snapshot, including an older horse from a delayed
initial entity stream. The test now snapshots the highest observed entity ID
and accepts only a horse with a larger ID. The revised `plan43` test passed five
consecutive local launches (`130.37s` total); the new PR head still requires its
own GitHub Actions run before merge.

The following exact-head Actions run
[`35886973563`](https://github.com/nico2525nn/cpp-fabricmc/actions/runs/35886973563)
passed configure, build, static checks, and deterministic focused gates, but the
full CTest regression failed only in `plan43` (`W-02 atk0 lands (HurtAnimation
eid)`); the other 54 tests passed. This attack-only miss is distinct from the
earlier window assertion. The W-02 fixture previously began interaction as soon
as `join()` observed Join Game, before the initial chunk batch marked client
startup complete. It now waits for `ChunkBatchFinished` before selecting a
summoned horse and issuing interaction/attack packets, and exits that case
cleanly if setup fails. This is test-readiness hardening, not a production combat
change or proof of the failure's server-side cause. The revised `plan43` passed
10 consecutive local launches (`260.95s` total); the new PR head still requires
its own Actions run before merge.

Final PR #3 status (supersedes the pending-at-the-time notes above): PR #3 was
merged to `main` as `f840324827b795a0efbf69ab2eba0ea73571772d` on 2026-09-24
(JST). Exact PR-head Actions run
[`35891999280`](https://github.com/nico2525nn/cpp-fabricmc/actions/runs/35891999280)
passed the full non-nightly CTest gate (`55/55`) and all preceding required
checks. The push-triggered main Actions run
[`35895033589`](https://github.com/nico2525nn/cpp-fabricmc/actions/runs/35895033589)
also completed successfully on the merge commit.
