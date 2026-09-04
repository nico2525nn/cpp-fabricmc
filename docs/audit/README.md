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

The six links intentionally target the future repository path
`docs-legacy/assessment-1.md` through `docs-legacy/assessment-6.md`. This
canonical-only commit does **not** create, move, or rewrite that archive. A local
Markdown checker must therefore allowlist exactly these six links while requiring
every other local target and anchor to exist. They are `HISTORICAL`, not current
evidence, and their absence is a declared archive-boundary limitation rather than a
reason to copy their contents into the canonical documents.

The old assessment files currently retained at `docs/assessment-1.md` through
`docs/assessment-6.md` are not modified by this commit. A future archive migration
must preserve their bytes and history before changing those paths.

## Evidence routing

- packet claims → [SPEC_WIRE.md#packet-contract-table](../SPEC_WIRE.md#packet-contract-table)
- gameplay claims → [SPEC_GAMEPLAY.md#declared-limitations](../SPEC_GAMEPLAY.md#declared-limitations)
- operational claims → [SPEC_OPS.md#performance-and-load](../SPEC_OPS.md#performance-and-load)
- test/status semantics → [VERIFICATION.md#wire-gate](../VERIFICATION.md#wire-gate)

An audit label such as “fixed” is historical until the current source path and named
test revalidate it. `DECLARED-LIMITATION` is required for an unverified or deferred
claim; it is not silently changed to `DONE` by an archive link.
