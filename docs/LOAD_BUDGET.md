# LOAD_BUDGET — Entity-1000 / Clock-Circuit / View-32 Budgets (plan46 O-12)

> Scope: assessment-6 **O-12** (entity 1000+ / redstone-active tick budget,
> Medium). Related: O-05/O-11 procedures in `docs/SOAK_24H.md`.
> Budget: **P95 MSPT < 50ms** (20 TPS). Existing guards:
> `Redstone.cpp` starve guard, `BlockTickScheduler` sim cull.

## 1. How to measure (reproducible scenarios)

### A. Entity 1000 (fixed arena, no despawn drift)

Despawn interferes (≤60-block rule clears strays), so the arena pins mobs:

```bash
build/cppfm --port 25569 --world-dir /tmp/load-entity --view-distance 6 &
# RCON/console:
#   /summon ×1000 inside a 48×48 walled arena at spawn (same chunk ticket)
#   observe 300s, sample MSPT P50/P95 from server log
```

Record: entity count at t=0/150/300 (must stay ≥950 — else the arena leaks
and the number is invalid), MSPT P50/P95, tick drops.

### B. Clock circuit (redstone-active worst case)

- 64-hopper clock + 256-lamp display + 8 Etho-style piston clocks in one
  chunk, chunk-forced. Record P95 MSPT + whether the starve guard
  (`Redstone.cpp`) engaged (guard engaged ⇒ MSPT drops but behavior is
  throttled — record BOTH, per plan46 §3 note).
- Reference harness for the engine side (no server):
  `test_redstone_engine_full` (7 categories) + `RedstoneRig::step()`.

### C. View-32 peak RSS (offline, fast)

```bash
timeout --foreground --kill-after=5 60 python3 tools/bench_chunk_gen.py \
  --view-distance 32 --chunks 4225 --dry --strict
```

Live vd=32 full-generation peak RSS is measured during the nightly soak
window (separate machine or `nice` isolation) and recorded below.

## 2. Results (fill per run — numbers, not adjectives)

| date | scenario | setup | P50 MSPT | P95 MSPT | peak RSS | verdict |
|------|----------|-------|----------|----------|----------|---------|
| _pending nightly_ | entity-1000 | arena 48×48, vd 6 | — | — | — | — |
| _pending nightly_ | clock-64 | forced chunk | — | — | — | — |
| 2026-09-04 | view-32 dry | bench_chunk_gen vd32/4225 dry strict | 0.107ms/chunk | 2.331ms/chunk | ~95MB | PASS (p50<5/p95<10/hit 84.6%/OOM 0/kick 0) |
| _pending nightly_ | view-32 live | 4225 chunks | — | — | — | — |

Dry-gate evidence already in CI: `bench_chunk_gen --dry --strict`
(p50<5/p95<10 synthetic) + `test_redstone_engine_full` engine asserts.

## 3. Rules for honest numbers

- Entity count is fixed by construction (arena) — never "≈1000 observed".
- If the starve guard fires, the MSPT number is reported WITH the guard flag.
- RSS is post-warmup (30 min) peak, same definition as `SOAK_24H.md` §4.
- A scenario that can't run yet stays "pending nightly" — never estimated.
