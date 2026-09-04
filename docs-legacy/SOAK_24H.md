# SOAK_24H — 24h Soak Day-0 Procedure + Tick-Limit Gates (plan46 O-05/O-06/O-11)

> Scope: assessment-6 **O-06** (24h soak, HIGH), **O-05** (tick delay / gen burst,
> HIGH), **O-11** (view-distance 32 memory cap, HIGH). Harness:
> `tests/soak_test.py` (already supports `--duration 86400`) +
> `tools/bench_chunk_gen.py --view-distance 32 --chunks 4225 --dry --strict`.
> Companion: `docs/SOAK_MANUAL.md` (vanilla-client manual side),
> `docs/SOAK_REPORT.md` (results log), `docs/LOAD_BUDGET.md` (O-12 numbers).

## 1. Day-0 startup (do this FIRST — wall-clock dominates plan46)

The 24h run must start on day 0 on a **fixed commit** in an **isolated
instance** (own `--port`, own `--world-dir`). Never restart the soak instance
to pick up merges — develop on a separate instance.

```bash
# 1. pin the commit and build it
git rev-parse HEAD > /tmp/soak24h/COMMIT
timeout --foreground --kill-after=5 150 cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
timeout 900 cmake --build build -j4   # -j2 on /tmp tmpfs if OOM

# 2. launch the 24h nightly soak (detached; ctest-excluded, nightly only)
mkdir -p /tmp/soak24h
nohup python3 tests/soak_test.py --duration 86400 --binary ./build/cppfm \
  --port 25566 > /tmp/soak24h/soak24h.log 2>&1 &
echo $! > /tmp/soak24h/PID

# 3. confirm it is alive (bots logging in, ticks flowing)
sleep 30; grep -c "Soak" /tmp/soak24h/soak24h.log
```

## 2. 300s gate (PR gate — every commit, ~5 min)

```bash
timeout --foreground --kill-after=5 450 ./build/test_smoke_80 ./build/cppfm 2>&1 | tail -2
# (smoke itself is long; the soak-flavoured short gate is:)
timeout --foreground --kill-after=5 400 python3 tests/soak_test.py \
  --duration 300 --binary ./build/cppfm --port 25567
echo EXIT:$?
pkill -9 -f "cppfm --port" 2>/dev/null; sleep 1
```

PASS = exit 0 (tick p99 within budget, RSS stable, keepalive OK, no
disconnect storms). This 300s gate is the merge gate; the 24h run is the
FIXED evidence for O-06.

## 3. Mid-run checkpoints (early anomaly detection)

| t | check | action on anomaly |
|---|-------|-------------------|
| 6h | RSS vs baseline, MSPT P95, bot disconnect count | RSS > +10% → capture `/proc/<pid>/status`, keep running |
| 12h | same + world save size growth | save growth > 2× → note in SOAK_REPORT, keep running |
| 24h | final report + `check_world` (§5 recovery tool) on the world copy | any FAIL → root-cause, fix, re-soak (another 24h) |

## 4. Judgment formulas (O-06 FIXED bar)

- **RSS**: exclude the first 1800s (warmup: chunk cache + JIT fill), then
  linear-regress RSS(t). PASS if slope < **+5% / 24h** relative to the
  post-warmup baseline.
- **TPS**: MSPT P95 < **50ms** for every 1h window (20 TPS sustained).
- **Integrity**: after shutdown, run the world-integrity check
  (`tools/check_world` — recovery worktree) on the soak world: PASS = exit 0
  (level.dat DataVersion 4189 + region CRC + playerdata NBT parse).
- **Ghost players**: bot connect/disconnect cycling must not accumulate
  duplicate sessions (W-13 watch item — record session count at 6h/12h/24h).

## 5. O-05 tick-delay / generation-burst limit (300s, flight bot)

```bash
# un-generated direction flight + MSPT aggregation (harness: soak bot with large mv_range)
timeout --foreground --kill-after=5 400 python3 tests/soak_test.py \
  --duration 300 --binary ./build/cppfm --port 25568 --clients 3
```

Gate: spawn-area **1000 new chunks** during the run with **P95 MSPT < 50** and
**TPS 20±1**. First half of the soak (see §6 phasing) covers the
already-generated-area case; this gate covers the burst case.

## 6. O-11 view-distance 32 memory cap (offline bench, fast)

view-distance 32 = (2·32+1)² = **4225 chunks**. Relation to the chunk cap:
`main.cpp` `autoCap = max(8192, vd·vd·4)` → vd=32 gives 8192 (above 4225, so
no eviction churn from the cap itself).

```bash
timeout --foreground --kill-after=5 60 python3 tools/bench_chunk_gen.py \
  --view-distance 32 --chunks 4225 --dry --strict
```

Gate (`--strict`): p50 < 5ms, p95 < 10ms per chunk synthetic, hit-rate > 80%.
The live-server vd=32 RSS peak is recorded in `docs/LOAD_BUDGET.md`
(O-12 budgets) once measured — the number, not an assertion, is the O-11
deliverable (OOM/kick-free completion + documented peak).

## 7. Soak phasing (MSPT separation, plan46 §3 note)

- **Phase A (0–12h)**: bots wander the already-generated region
  (`mv_range` small) — measures steady-state tick + leak rate.
- **Phase B (12–24h)**: bots fly toward un-generated terrain
  (`mv_range 3000` for `--duration >= 7200`, see `soak_test.py` Bot) —
  measures generation burst on top of steady state.
- MSPT is logged per phase so generation cost (O-05) never contaminates the
  leak regression (O-06).

## 8. Cleanup (mandatory — pkill safety rule)

```bash
pgrep -a "cppfm --port"          # confirm targets BEFORE killing
pkill -9 -f "cppfm --port"       # full-pattern only, never bare pkill
sleep 1; pgrep -a "cppfm --port" || echo "all cppfm stopped"
```

Only `pkill -9 -f "cppfm --port"` is allowed (defense/recovery worktrees run
in parallel — never use bare `pkill`, see 2026-09-03 incident rule).
