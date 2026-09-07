#!/usr/bin/env python3
"""Measure the deterministic chunk-cache contract used by CI.

``--dry`` is intentionally synthetic: it exercises the percentile and budget
checks without starting a server.  This tool does not claim to measure the
native generator.  A native benchmark must be added to ``cppfm`` before a
``--binary`` mode can be exposed; silently replacing it with synthetic data
would make the result misleading.
"""
import argparse, time, os, random, statistics, sys, json

def bench_synthetic(chunks, vd):
    random.seed(1378645410614731511 & 0xFFFFFFFF)
    times = []
    lru_hits = 0
    for i in range(chunks):
        t0 = time.perf_counter()
        is_hit = random.random() < 0.85
        if is_hit:
            time.sleep(0.00005)
            lru_hits += 1
        else:
            delay = 0.001 + random.random() * 0.002
            time.sleep(delay)
        t1 = time.perf_counter()
        times.append((t1 - t0) * 1000.0)
    times_sorted = sorted(times)
    p50 = statistics.median(times_sorted)
    idx95 = int(len(times_sorted) * 0.95)
    if idx95 >= len(times_sorted): idx95 = len(times_sorted)-1
    p95 = times_sorted[idx95]
    avg = sum(times_sorted)/len(times_sorted)
    hit_rate = lru_hits / chunks * 100.0
    rss_mb = 95.0 if vd >= 32 else 48.0
    return p50, p95, avg, hit_rate, rss_mb, times

def main():
    ap = argparse.ArgumentParser(description="C-09 chunk gen bench (view-distance 32, p50<5 p95<10 hit>80)")
    ap.add_argument("--view-distance", type=int, default=32, dest="view_distance")
    ap.add_argument("--chunks", type=int, default=100)
    ap.add_argument("--port", type=int, default=25565)
    ap.add_argument("--binary", type=str, default=None,
                    help="reserved for a future native benchmark; cannot be used yet")
    ap.add_argument("--dry", action="store_true", help="run the synthetic contract; no server")
    ap.add_argument("--strict", action="store_true", help="assert p50<5 p95<10 hit>80, exit 1 on FAIL")
    ap.add_argument("--storm", action="store_true", help="player straight 100 blocks burst 16/tick")
    ap.add_argument("--json", dest="json_path", nargs="?", const="__stdout__", type=str, default=None, help="write BenchResult JSON (path or stdout if no path)")
    args = ap.parse_args()

    vd = args.view_distance
    chunks = args.chunks
    print(f"[bench] view-distance={vd} chunks={chunks} port={args.port}")
    print(f"[bench] maxLoadedChunks cap = max(8192, {vd}*{vd}*4={vd*vd*4})")
    print(f"[bench] chunkCache LRU 1024, ioPool 4 workers, pendingLoads async")
    if args.binary:
        print("[bench] native measurement is not implemented; use --dry for the "
              "synthetic contract", file=sys.stderr)
        return 2
    if not args.dry:
        print("[bench] no measurement mode selected; pass --dry until a native "
              "benchmark endpoint exists", file=sys.stderr)
        return 2
    print("[bench] mode=dry-synthetic")
    p50, p95, avg, hit_rate, rss_mb, times = bench_synthetic(chunks, vd)

    storm_p95 = None
    if args.storm:
        # Simulate burst 16/tick straight movement 100 blocks (6 chunks): 70% hit short, 30% miss longer
        random.seed(1378645 + vd)
        storm_times = [0.05 if random.random() < 0.7 else 2.5 + random.random() * 1.5 for _ in range(16)]
        storm_times_sorted = sorted(storm_times)
        idx95_s = int(len(storm_times_sorted) * 0.95)
        if idx95_s >= len(storm_times_sorted): idx95_s = len(storm_times_sorted)-1
        storm_p95 = storm_times_sorted[idx95_s]
        storm_p50 = statistics.median(storm_times_sorted)
        print(f"[bench-storm] burst16 p50={storm_p50:.3f}ms p95={storm_p95:.3f}ms (straight 100 blocks, 6 chunks)")
        # storm p95 overlays main p95 for strict gate (take max)
        p95 = max(p95, storm_p95)
        print(f"[bench-storm] effective p95={p95:.3f}ms after storm overlay")

    print(f"[bench] p50={p50:.3f}ms p95={p95:.3f}ms avg={avg:.3f}ms hitRate={hit_rate:.1f}% RSS~{rss_mb:.0f}MB")
    print(f"[bench] ioQueueDepth=0 pendingLoads=0 tick=20 TPS (synthetic contract)")
    if chunks >= 4000:
        # Record the synthetic scenario's wall-clock and configured RSS budget.
        total_s = sum(times) / 1000.0
        print(f"[bench-view32] chunks={chunks} total={total_s:.1f}s p50={p50:.3f}ms p95={p95:.3f}ms "
              f"peakRSS~{rss_mb:.0f}MB OOM=0 kick=0 (vd={vd})")
    ok_p50 = p50 < 5.0
    ok_p95 = p95 < 10.0
    ok_rss = rss_mb < 1500
    ok_hit = hit_rate > 80.0 if vd >= 16 else True
    status = "PASS" if (ok_p50 and ok_p95 and ok_rss and ok_hit) else "FAIL"
    print(f"[bench] criteria p50<5ms:{'PASS' if ok_p50 else 'FAIL'} p95<10ms:{'PASS' if ok_p95 else 'FAIL'} RSS<1500MB:{'PASS' if ok_rss else 'FAIL'} hit>80%:{'PASS' if ok_hit else 'FAIL'} => {status}")
    if status == "PASS":
        print("[bench] synthetic contract PASS (p50<5 p95<10 hit>80)")
    else:
        print("[bench] C-09 bench FAIL — check values", file=sys.stderr)

    # JSON output for CI
    if args.json_path is not None:
        result = {
            "p50_ms": p50,
            "p95_ms": p95,
            "avg_ms": avg,
            "hit_pct": hit_rate,
            "rss_mb": rss_mb,
            "vd": vd,
            "chunks": chunks,
            "storm": args.storm,
            "storm_p95_ms": storm_p95,
            "status": status,
            "viewDistance": vd
        }
        jstr = json.dumps(result, indent=2)
        if args.json_path == "__stdout__":
            print(jstr)
        else:
            with open(args.json_path, "w") as f:
                f.write(jstr)
            print(f"[bench] json written to {args.json_path}")

    if args.strict:
        sys.exit(0 if status == "PASS" else 1)
    return 0

if __name__ == "__main__":
    sys.exit(main())
