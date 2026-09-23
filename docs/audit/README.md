# Audit history index

This index separates historical audits from the current canonical specifications.
The repository target is Minecraft Java Edition **1.21.4**, protocol **769**, and
DataVersion **4189**. Audit identifiers refer to MISSING **#1–#90** and the strict,
deep, H1, B, C, E, W, G, and O history matrices; they do not change the current
matrix status.

The current source of truth is the [canonical documentation index](../README.md):
[SPEC_WIRE.md](../SPEC_WIRE.md), [SPEC_GAMEPLAY.md](../SPEC_GAMEPLAY.md),
[SPEC_OPS.md](../SPEC_OPS.md), and [VERIFICATION.md](../VERIFICATION.md).
Historical text must not override current source definitions or fresh test output.

## Current adversarial review

The current final-gates pass is recorded in
[adversarial-review-2026-09-19.md](adversarial-review-2026-09-19.md). It records
the exact configure/build/focused/CTest/live/stress/soak/benchmark commands,
feature review `10/10`, quality review `9.5/10`, the bounded asynchronous
writer, secure-profile termination, piston persistence barrier, callback
revalidation, correct profile/chat signature algorithms, and the remaining
signed-command transcript limitation.
The report is current working-tree evidence, not a release artifact or a claim of
universal Minecraft/Fabric compatibility.

## Active goal audit

The current bounded goal run is indexed by:

- [goal-baseline.md](goal-baseline.md) — dirty-tree snapshot, eligible LOC baseline, and protected manifest.
- [goal-feature-ledger.md](goal-feature-ledger.md) — 90-row coverage ledger with current row-specific status counts.
- [goal-followup-evidence-ledger.md](goal-followup-evidence-ledger.md) — additive action ledger for every remaining P0/P1 assertion and release boundary.
- [goal-live-matrix.md](goal-live-matrix.md) — two real `cppfm` launches covering status, login, configuration, play, CLI/properties, restart, and cleanup.
- [goal-live-features.md](goal-live-features.md) — real command, tab-completion, entity, wire-consequence, datapack, and persistence transcript.
- [goal-live-interactions.md](goal-live-interactions.md) — real menu, block-action, metadata, equipment, projectile, and portal packet transcript.
- [goal-live-remaining.md](goal-live-remaining.md) — real combat, item, menu, creative-slot, hazard, and world-border packet transcript.
- [goal-gui-soak.md](goal-gui-soak.md) — headless GUI capability and the failed 1,200-second point of the requested two-hour soak.
- [goal-bugs-network.md](goal-bugs-network.md) and [goal-bugs-gameplay.md](goal-bugs-gameplay.md) — 23 fixed reproducible defects with focused regressions.
- [goal-known-vanilla-bugs.md](goal-known-vanilla-bugs.md) — official Mojang issue oracle, classified separately from local defects.
- [goal-adversarial-review.md](goal-adversarial-review.md) — final P0/P1/P2/P3 review and security guard results.
- [goal-cleanup-runtime.md](goal-cleanup-runtime.md) and [goal-cleanup-game.md](goal-cleanup-game.md) — safe cleanup measurements and residual candidates.

These files are current working-tree evidence. They preserve explicit GUI, arbitrary
mod, world-generation L3, signed-command, moving-piston-NBT, and accepted long-soak
limitations rather than converting unverified rows into compatibility claims.

## Assessment history

| audit | historical archive link | scope | status |
|---|---|---|---|
| assessment-1 | [docs-legacy/assessment-1.md](../../docs-legacy/assessment-1.md) | strict wire audit, 78 gaps | `HISTORICAL` |
| assessment-2 | [docs-legacy/assessment-2.md](../../docs-legacy/assessment-2.md) | deep wire/inventory audit, 31 gaps | `HISTORICAL` |
| assessment-3 | [docs-legacy/assessment-3.md](../../docs-legacy/assessment-3.md) | gameplay/B-series audit | `HISTORICAL` |
| assessment-4 | [docs-legacy/assessment-4.md](../../docs-legacy/assessment-4.md) | C-series and parity audit | `HISTORICAL` |
| assessment-5 | [docs-legacy/assessment-5.md](../../docs-legacy/assessment-5.md) | E-series and performance audit | `HISTORICAL` |
| assessment-6 | [docs-legacy/assessment-6.md](../../docs-legacy/assessment-6.md) | W/G/O final audit matrix | `HISTORICAL` |

## Archive boundary

The six links intentionally target the immutable repository path
`docs-legacy/assessment-1.md` through `docs-legacy/assessment-6.md`. The
compatibility entry points at `docs/assessment-1.md` through
`docs/assessment-6.md` point to these archive files and to the current canonical
contracts; they do not duplicate historical bodies. A local Markdown checker must
require every target and anchor to exist. The archived documents are `HISTORICAL`,
not current evidence, and must not override the canonical specifications.

## Evidence routing

- packet claims → [SPEC_WIRE.md#packet-contract-table](../SPEC_WIRE.md#packet-contract-table)
- gameplay claims → [SPEC_GAMEPLAY.md#declared-limitations](../SPEC_GAMEPLAY.md#declared-limitations)
- operational claims → [SPEC_OPS.md#performance-and-load](../SPEC_OPS.md#performance-and-load)
- test/status semantics → [VERIFICATION.md#wire-gate](../VERIFICATION.md#wire-gate)

An audit label such as “fixed” is historical until the current source path and named
test revalidate it. `DECLARED-LIMITATION` is required for an unverified or deferred
claim; it is not silently changed to `DONE` by an archive link.
