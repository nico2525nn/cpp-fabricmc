# PERF_LIMITS.md — Performance limits (plan46 §2, O-10/O-11/O-12 notes)

> Scope of this worktree (recovery): measure + document the server's
> documented operating limits with reproducible commands. Gameplay-level
> tuning (mob AI, density fns, brewing) belongs to the longterm worktree.

## 1. View-distance 32 full generation (O-11 procedure + dry evidence)

- Scale: view-distance 32 ⇒ `(2*32+1)^2 = 4225` chunks.
- Dry bench (synthetic chunk pipeline, no clients):
  `python3 tools/bench_chunk_gen.py --view-distance 32 --chunks 4225 --dry --strict`
- Measured 2026-09-04 (this worktree, HEAD `8ff124a`):

| metric | value | budget |
|---|---|---|
| chunks | 4225 | — |
| total | 1.7 s | — |
| p50 gen | 0.107 ms | < 5 ms |
| p95 gen | 2.332 ms | < 10 ms |
| cache hit | 84.6 % | > 80 % |
| peak RSS | ~95 MB | < 1500 MB |
| OOM / kick | 0 / 0 | 0 |

- Live-server variant (needs binary + port):
  `python3 tools/bench_chunk_gen.py --view-distance 32 --chunks 4225 --binary ./build/cppfm`
  plus storm burst `--storm` (100-block straight flight, 16 req/tick).
- Config guard: `main.cpp` `autoCap = max(8192, vd*vd*4)` bounds the
  in-flight chunk queue so a login burst cannot OOM the server.

## 2. Entity / redstone tick budget (O-12)

- Budget: P95 MSPT < 50 ms (20 TPS). Guards already in code:
  `Redstone.cpp` starvation guard, `BlockTickScheduler` simulation cull.
- Load procedure (live server, manual or soak harness):
  1. `summon` ×1000 in a contained area (despawn rule: >128 m = instant
     despawn, so keep them aggregated — cactus-style or fenced range).
  2. Large clock circuit (repeater loop) active.
  3. Sample MSPT P50/P95 over ≥10 min from the tick log; record starvation-
     guard trips alongside (a lower MSPT *with* trips is throttled, not free).
- Soak coupling: the 24 h soak (longterm worktree, `docs/SOAK_24H.md`)
  reuses this budget as its TPS gate; post-soak integrity is verified with
  `check_world` (recovery worktree deliverable).

## 3. RCON / admin-plane isolation (O-09)

- RCON runs on a dedicated worker thread (one thread per connection);
  the 10× wrong-password flood shows <1 s post-flood response and no tick
  impact (`test_rcon_multi`). Admin-plane floods cannot stall the tick loop
  by construction; the game-plane rate limits are the defense worktree's
  scope (`docs/RATE_LIMITS.md`).

## 4. Persistence costs (O-06/O-08 coupling)

- `level.dat` periodic save: every 1200 ticks (~60 s) + `save-all` on demand.
- Region flush: background `Persistence` worker, 3 s cadence for dirty chunks.
- `check_world` full scan is O(chunks + players), offline-only.
