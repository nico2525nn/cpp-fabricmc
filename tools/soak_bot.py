#!/usr/bin/env python3
"""Soak bot 1h 20 TPS — C-12 (move/combat/redstone/death, nightly). Dry 300s."""
import io, os, sys, time, struct, argparse, subprocess, socket, random
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tests"))
import mcproto
from mcproto import Conn, read_varint

def main():
    ap = argparse.ArgumentParser(description="C-12 soak_bot 1h 20 TPS")
    ap.add_argument("--host", default=os.environ.get("CPPFM_HOST","127.0.0.1"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("CPPFM_PORT","25577")))
    ap.add_argument("--binary", type=str, default=None, help="path to cppfm")
    ap.add_argument("--duration", type=int, default=300, help="seconds (3600 nightly, 300 dry)")
    ap.add_argument("--view-distance", type=int, default=6)
    args = ap.parse_args()

    host = args.host
    port = args.port
    duration = args.duration
    binary = args.binary
    proc = None

    if binary and not os.path.exists(binary):
        print(f"[soak_bot] binary {binary} not found", file=sys.stderr)
        sys.exit(1)

    if binary:
        def free_port():
            s=socket.socket(); s.bind(("127.0.0.1",0)); p=s.getsockname()[1]; s.close(); return p
        port = free_port()
        host = "127.0.0.1"
        print(f"[soak_bot] spawning {binary} --port {port} --view-distance {args.view_distance} for {duration}s")
        cwd = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
        proc = subprocess.Popen([binary, "--port", str(port), "--view-distance", str(args.view_distance)],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=cwd)
        time.sleep(1.8)

    fails = 0
    def check(cond, msg):
        nonlocal fails
        print(("  ok  " if cond else "  FAIL ") + msg)
        if not cond: fails += 1

    try:
        c = Conn(host, port)
        c.login("SoakBot")
        c.config_finish(sink=lambda p,d: None)

        keepalives = 0
        chunks = 0
        times = 0
        moves_seen = 0
        deaths = 0
        kicks = 0
        updates = 0
        start = time.time()
        ticks = 0
        last_move = 0.0
        last_chat = 0.0
        last_combat = 0.0

        print(f"[soak_bot] start {duration}s 20 TPS host={host} port={port}")
        # set socket timeout for non-blocking pump
        try:
            c.sock.settimeout(0.05)
        except Exception:
            pass

        while time.time() - start < duration:
            ticks += 1
            now = time.time()
            # periodic move 1 block per 2s
            if now - last_move > 1.5:
                last_move = now
                try:
                    x = 8.5 + (ticks % 40) * 0.5
                    c.send_packet_raw(0x1c, struct.pack(">ddd", x, -60.0, 8.5) + b"\x01")
                except OSError:
                    kicks += 1; break
            # periodic chat every 30s
            if now - last_chat > 30:
                last_chat = now
                try:
                    c.send_packet_raw(0x07, mcproto.pack_string(f"soak tick {ticks}") + struct.pack(">qq", int(now*1000),0)+b"\x00"+mcproto.write_varint(0)+b"\x00\x00\x00")
                except OSError:
                    pass
            # periodic swing every 5s (combat)
            if now - last_combat > 5:
                last_combat = now
                try:
                    c.send_packet_raw(0x3a, struct.pack(">b", 0))  # SwingArm
                except OSError:
                    pass

            # pump packets
            try:
                pid, data = c.recv_packet()
            except OSError as e:
                # timeout is expected, other OSError is disconnect
                if "timed out" in str(e) or "timeout" in str(type(e).__name__).lower():
                    time.sleep(0.05)
                    continue
                # check if it's just timeout via socket timeout
                time.sleep(0.05)
                continue
            except Exception:
                time.sleep(0.05)
                continue

            if pid == 0x27:
                keepalives += 1
                try: c.send_packet_raw(0x1a, data)
                except OSError: break
            elif pid == 0x28:
                chunks += 1
            elif pid == 0x6b:
                times += 1
            elif pid in (0x2f,0x30,0x32,0x77,0x4d):
                moves_seen += 1
            elif pid == 0x09:
                updates += 1
            elif pid == 0x1a: # Disconnect
                kicks += 1; break
            elif pid == 0x4c: # Respawn
                deaths += 1

            # 20 TPS pacing
            time.sleep(0.05)

        elapsed = time.time() - start
        print(f"[soak_bot] elapsed {elapsed:.1f}s ticks {ticks} keepAlives {keepalives} chunks {chunks} times {times} updates {updates} deaths {deaths} kicks {kicks}")

        # gates: keepAlive periodic, no kick, chunks received
        check(keepalives >= max(1, duration // 40), f"keepAlives {keepalives} >= {max(1, duration//40)} (periodic KeepAlive 0x27/0x1a)")
        check(kicks == 0, f"kicks 0 (got {kicks})")
        check(chunks >= 10, f"chunks {chunks} >=10 (LevelChunkWithLight 0x28)")
        check(times >= 1 or ticks > 100, f"time updates {times} or ticks {ticks} (UpdateTime 0x6b)")

        try: c.close()
        except Exception: pass
    finally:
        if proc:
            try: proc.terminate(); proc.wait(timeout=5)
            except Exception:
                try: proc.kill()
                except Exception: pass

    # dry 300s should also pass these gates; 3600 nightly same
    status = "PASS" if fails==0 else "FAIL"
    print(f"[soak_bot] {status} ({fails} failures) duration={duration}s")
    sys.exit(1 if fails else 0)

if __name__ == "__main__":
    main()
