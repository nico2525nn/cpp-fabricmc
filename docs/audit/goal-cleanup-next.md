# Follow-up cleanup audit

This pass searched the remaining large runtime translation units after the
pending-queue and raw-level-load cleanup. The search is intentionally limited
to production C/C++ callsites; tests, fixtures, generated data, protocol
evidence, and feature implementations are not deletion targets.

## Results

- `src/game/Persistence.hpp`: `flushOnce()` and `flushChunk()` serialize the
  same chunk shape, but their ownership and failure contracts differ. The
  background path swaps a dirty batch and retries failed entries; the explicit
  path evicts one key, returns success, and retries on a different failure
  condition. Combining them would change persistence behavior, so no code was
  removed.
- `src/game/GameServer_session.cpp`: `handlePlaceGhostRecipe()` is an
  unreferenced 54-line feature entrypoint, but it owns stonecutter/ghost-recipe
  state and packet behavior. It is not a safe dead-code deletion.
- `src/game/GameServer_items.cpp`: repeated container insertion blocks cover
  crafter, hopper, furnace, and brewing semantics. They differ in locking,
  dirty-state updates, and result packets; bulk consolidation would require
  feature-level tests and is outside a callsite-zero cleanup.
- A `--gc-sections` link map was inspected as a diagnostic only. Discarded
  sections are mostly COMDAT/template/header-inline duplicates, so linker
  garbage bytes are not source LOC that can be deleted safely.

The only additional callsite-zero production cleanup that met the current
policy was the alternate `StructureManager` pending API already removed in
the PR. Canonical `drainPendingMobs`/`drainPendingLoot` remain in
`src/game/GameServer_tick.cpp`. No new feature, test, fixture, evidence, or
generated-file deletion is justified by this pass.

## Boundary

The repository-wide 10,000-line reduction target remains unmet and cannot be
reached safely by deleting the remaining owned runtime without removing
shipped features or protected evidence. This is recorded as a limitation, not
hidden by counting linker garbage, comments, generated data, or unrelated
dirty-tree changes as eligible cleanup.

No build or test was run for this documentation-only update. The PR's existing
GitHub Actions run remains the authoritative verification for its code changes.

## Continuation verification

The follow-up implementation pass added only callsite-audited or behavior-neutral
refactors. It removed the unreachable Redstone behavior registry, unused core
serialization and packet conversion wrappers, unreferenced game/JVM helpers,
legacy combat broadcast wrappers, unused item trim readers, dead world aliases,
and the uncalled terrain helpers plus an unreachable density branch. It also
centralized menu layouts and dimension-store selection without deleting feature
paths.

Against `origin/main`, the branch currently contains 1,253 source deletions and
90 source insertions (net source reduction: 1,163 lines). The repository-wide
10,000-line target remains deliberately unmet; no protected feature, test,
fixture, generated data, evidence, or virtual/plugin ABI surface was deleted to
inflate the count.

Focused post-change checks passed locally:

- `test_gameplay_full`: 806/806
- `test_native`: all checks passed
- `test_menu_logic`: 41/41
- `test_redstone_engine_full`: 42/42
- `test_core_safety`: 45/45
- `test_spec_wire`: 417/417
- `test_goal_network_bugs`: 36/36
- `test_fuzz`: 25/25
- `test_jvm_native_bridge` and `test_jvm_handles`: passed

These local results supplement, rather than replace, the required GitHub
Actions result for the pull request. The remaining public callsite-zero methods
are ABI/source-compatibility candidates and are intentionally documented as
conditional rather than removed without an API decision.
