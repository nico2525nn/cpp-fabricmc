"""Stress: many concurrent sessions; verify server stays healthy.

plan45 O-04 (100+ simultaneous connections) / O-02 (login burst):
  python3 tests/stress_test.py --clients 120 --binary ./build/cppfm   # O-04 dry (spawns server)
  python3 tests/stress_test.py                                         # legacy attach mode (CPPFM_HOST/PORT/N)

Exit 0 on PASS (all bots join + server alive), 1 on FAIL. Cleans up server subprocess.
"""
import argparse
import io
import os
import shutil
import socket
import struct
import subprocess
import sys
import signal
import threading
import time
import tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mcproto
from mcproto import Conn, read_varint

HOST = os.environ.get("CPPFM_HOST", "127.0.0.1")
PORT = int(os.environ.get("CPPFM_PORT", "25577"))
DEFAULT_N = int(os.environ.get("CPPFM_N", "32"))


def wait_for_server(proc, host, port, timeout=30):
    """Wait for a status response and fail early if the owned child exits."""
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"server exited before readiness probe (exit={proc.returncode})")
        client = None
        try:
            client = Conn(host, port, timeout=1)
            client.status()
            return
        except (OSError, EOFError, ValueError, RuntimeError) as error:
            last_error = error
            time.sleep(0.1)
        finally:
            if client is not None:
                client.close()
    detail = f": {last_error}" if last_error else ""
    raise TimeoutError(f"server readiness probe timed out on {host}:{port}{detail}")


def stop_process(proc, timeout=10):
    """Terminate only the server process owned by this test.

    The server is started in its own session so the escalation path cannot
    affect the stress runner or another test.  Every signal is followed by a
    wait, which prevents the short-lived zombie/orphan window that used to
    make the outer timeout report a crash after a successful run.
    """
    if proc is None:
        return
    if proc.poll() is None:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    try:
        proc.wait(timeout=timeout)
        return
    except subprocess.TimeoutExpired:
        pass
    if proc.poll() is None:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        raise RuntimeError(f"server pid {proc.pid} did not exit after SIGKILL")


def pick_free_port():
    port = 26100 + (os.getpid() % 3000)
    for _ in range(40):
        s = socket.socket()
        try:
            s.bind(("127.0.0.1", port))
            s.close()
            return port
        except OSError:
            s.close()
            port += 1
    return port


results = []
lock = threading.Lock()
STAGE = {}


def make_bot(host, port, play_seconds=12, stop_event=None):
    stop_event = stop_event or threading.Event()
    def bot(i):
        ok = {"join": False, "chunks": 0}
        c = None
        try:
            STAGE[i] = "connect"
            c = Conn(host, port, timeout=float(os.environ.get("CPPFM_TMO", "30")))
            STAGE[i] = "login"
            # NOTE plan45: 03d — 02d overflows at Bot100 (W-13 duplicate-name interference)
            c.login(f"Bot{i:03d}")
            STAGE[i] = "config"
            c.config_finish(sink=lambda p, d: None, max_seconds=10)
            STAGE[i] = "play"
            c.sock.settimeout(1.0)
            deadline = time.monotonic() + play_seconds
            sent_chat = False
            while not stop_event.is_set() and time.monotonic() < deadline:
                try:
                    pid, data = c.recv_packet()
                except socket.timeout:
                    continue
                if pid == 0x2c:
                    ok["join"] = True
                    c.send_packet_raw(0x2a, b"")
                    # report position like real clients do -> server marks us active
                    c.send_packet_raw(0x1c, struct.pack(">ddd", 8.5, -60.0, 8.5) + b"\x01")
                elif pid == 0x42:
                    bio = io.BytesIO(data)
                    tid, _ = read_varint(bio)
                    c.send_packet_raw(0x00, mcproto.write_varint(tid))
                elif pid == 0x28:
                    ok["chunks"] += 1
                elif pid == 0x0c:
                    c.send_packet_raw(0x09, struct.pack(">f", 8.0))
                elif pid == 0x27:
                    c.send_packet_raw(0x1a, data)
                if not sent_chat and ok["chunks"] > 3:
                    sent_chat = True
                    c.send_packet_raw(0x07, mcproto.pack_string(f"bot {i} here") +
                                      struct.pack(">qq", 0, 0) + b"\x00" +
                                      mcproto.write_varint(0) + b"\x00\x00\x00")
                if not sent_chat and time.monotonic() > deadline - 6 and i % 4 == 0:
                    # keep moving so chunks flow
                    try:
                        c.send_packet_raw(0x1c, struct.pack(">ddd", 8.5, -60.0, 8.5) + b"\x01")
                    except OSError:
                        break
        except Exception as e:
            with lock:
                results.append((i, False, f"stage={STAGE.get(i)} {e!r}"))
            return
        finally:
            if c is not None:
                try:
                    c.close()
                except OSError:
                    pass
        with lock:
            results.append((i, ok["join"] and ok["chunks"] > 0, f"chunks={ok['chunks']}"))
    return bot


def ramp_duration(n, ramp, ramp_interval):
    waves = (n + ramp - 1) // ramp
    return max(0.0, (waves - 1) * ramp_interval + waves * 0.05 * ramp)


def run_bots(host, port, n, ramp, ramp_interval, play_seconds):
    stop_event = threading.Event()
    bot = make_bot(host, port, play_seconds, stop_event)
    threads = [threading.Thread(target=bot, args=(i,)) for i in range(n)]
    t0 = time.monotonic()
    # Each bot owns its play deadline.  Keep the runner bounded by the same
    # deadline plus a small reaping allowance, but do not signal the bots
    # immediately after the final ramp wave: that would turn a sustain test
    # into a connect-only test.
    runner_deadline = t0 + ramp_duration(n, ramp, ramp_interval) + play_seconds + 5.0
    # wave ramp (plan45 §2: 10 bots / 2s per wave) — avoids SYN-flood collapse
    for w in range(0, n, ramp):
        wave = threads[w:w + ramp]
        for t in wave:
            t.start()
            time.sleep(0.05)  # gentle intra-wave stagger
        if w + ramp < n:
            time.sleep(ramp_interval)
    timed_out = False
    for t in threads:
        remaining = max(0.0, runner_deadline - time.monotonic())
        t.join(timeout=remaining)
        if t.is_alive():
            timed_out = True
            stop_event.set()
    if timed_out:
        for t in threads:
            t.join(timeout=5)
    for index, t in enumerate(threads):
        if t.is_alive():
            with lock:
                results.append((index, False, "bot thread did not stop within cleanup deadline"))
    stop_event.set()
    return time.monotonic() - t0


def main():
    ap = argparse.ArgumentParser(description="stress: N concurrent joins (plan45 O-04 120 dry)")
    ap.add_argument("--clients", type=int, default=DEFAULT_N)
    ap.add_argument("--binary", default=None, help="spawn this server binary (else attach CPPFM_HOST/PORT)")
    ap.add_argument("--bin", default=None, help="alias for --binary")
    ap.add_argument("--port", type=int, default=0, help="server port (0 = pick free, attach mode uses CPPFM_PORT)")
    ap.add_argument("--view-distance", type=int, default=6)
    ap.add_argument("--max-players", type=int, default=None,
                    help="server --max-players (default: clients+10; O-04 capacity test, not the full-kick gate)")
    ap.add_argument("--ramp", type=int, default=10, help="bots per wave")
    ap.add_argument("--ramp-interval", type=float, default=2.0, help="seconds between waves")
    ap.add_argument("--play-seconds", type=float, default=None,
                    help="per-bot play sustain (default: ramp_time+12 so all waves overlap)")
    args = ap.parse_args()
    if args.bin is not None:
        args.binary = args.bin
    n = args.clients
    if (n < 1 or args.ramp < 1 or
            (args.play_seconds is not None and args.play_seconds <= 0)):
        print("clients, ramp, and play-seconds must be positive", file=sys.stderr)
        return 2
    # auto sustain: all waves must overlap for a true simultaneous-connection test (O-04)
    waves = (n + args.ramp - 1) // args.ramp
    ramp_time = ramp_duration(n, args.ramp, args.ramp_interval)
    play_seconds = args.play_seconds if args.play_seconds is not None else (ramp_time + 12.0)
    max_players = args.max_players if args.max_players is not None else (n + 10)

    proc = None
    world_dir = None
    host, port = HOST, PORT
    if args.binary is not None:
        binary = args.binary
        if not os.path.exists(binary):
            alt = os.path.join(os.getcwd(), "build/cppfm")
            if os.path.exists(alt):
                binary = alt
            else:
                print(f"binary not found: {binary}", file=sys.stderr)
                return 1
        port = args.port if args.port != 0 else pick_free_port()
        host = "127.0.0.1"
        world_dir = tempfile.mkdtemp(prefix=f"cppfm-stress-{os.getpid()}-")
        cmd = [binary, f"--port={port}", f"--view-distance={args.view_distance}",
               f"--max-players={max_players}",
               f"--world-dir={world_dir}", "--online-mode=false"]
        print(f"[stress] starting server {' '.join(cmd)} for {n} clients")
        environment = os.environ.copy()
        environment["CPPFM_SERVER_DIR"] = world_dir
        try:
            proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL,
                                    env=environment,
                                    start_new_session=True)
        except OSError as error:
            print(f"FATAL: could not start server: {error}", file=sys.stderr)
            shutil.rmtree(world_dir, ignore_errors=True)
            return 2
        try:
            wait_for_server(proc, host, port)
        except (OSError, RuntimeError, TimeoutError) as error:
            print(f"FATAL: server not ready: {error}", file=sys.stderr)
            stop_process(proc)
            shutil.rmtree(world_dir, ignore_errors=True)
            return 2
    elif args.port != 0:
        port = args.port

    try:
        dt = run_bots(host, port, n, args.ramp, args.ramp_interval, play_seconds)
    except Exception:
        stop_process(proc)
        if world_dir is not None:
            shutil.rmtree(world_dir, ignore_errors=True)
        raise

    passed = sum(1 for _, ok, _ in results if ok)
    print(f"{passed}/{n} bots fully joined within {dt:.1f}s")
    for i, ok, info in sorted(results):
        if not ok:
            print(f"  FAIL bot{i}: {info}")

    # server still alive?
    try:
        c = Conn(host, port, timeout=float(os.environ.get("CPPFM_TMO", "30")))
        js = c.status()
        c.close()
        print("status after stress:", js.get("players", {}).get("online"), "online reported")
        alive = True
    except Exception as e:
        print(f"status after stress: FAILED ({e!r})")
        alive = False

    ok = (passed == n) and alive
    print(f"STRESS {'PASS' if ok else 'FAIL'}: login_rate={passed}/{n} "
          f"ramp={args.ramp}/{args.ramp_interval}s play={play_seconds:.0f}s max_players={max_players}")
    rc = 0 if ok else 1
    if proc is not None:
        stop_process(proc)
        shutil.rmtree(world_dir, ignore_errors=True)
    sys.exit(rc)


if __name__ == "__main__":
    main()
