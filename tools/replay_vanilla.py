#!/usr/bin/env python3
"""replay_vanilla.py — plan43 O-01 automation side: vanilla-equivalent request
sequence replay (requirement order + bursts a synthetic client can reproduce).

Scenario (mirrors plan43 §3 SCENARIO):
  login -> config (settings resend + pong + resource-pack + known-packs resend
  BEFORE finish-ack: W-12) -> play move (W-01) -> tab (W-04) -> signed-cmd
  n=0/1 (W-03) -> sign update (W-07) -> abilities (W-06).

What it canNOT verify (stays manual in docs/SPEC_OPS.md#real-client-and-gui-verification): rendering, colors,
lighting look, GUI feel, 1h random-walk rare disconnects.

Usage:
  python3 tools/replay_vanilla.py --binary ./build/cppfm [--port 0]
Exit 0 = all steps PASS; exit 1 = any FAIL (with step name).
"""
from __future__ import annotations
import argparse, io, socket, struct, subprocess, sys, tempfile, time, os, signal, shutil
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "tests"))
from mcproto import Conn, write_varint, read_varint, pack_string  # noqa: E402

STEPS: list[tuple[bool, str]] = []

def step(ok: bool, name: str):
    STEPS.append((bool(ok), name))
    print(f"  {'ok' if ok else 'FAIL'}  {name}")

def find_free_port() -> int:
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

def launch(binary: str, port: int, world_dir: str):
    logf = open(os.path.join(world_dir, "replay.log"), "wb")
    proc = subprocess.Popen([binary, f"--port={port}", f"--world-dir={world_dir}",
                             "--view-distance=4", "--online-mode=false"],
                            stdout=logf, stderr=subprocess.STDOUT,
                            start_new_session=True)
    proc._logf = logf
    return proc

def wait_ready(proc: subprocess.Popen, port: int, timeout=15.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            return False
        c = None
        try:
            c = Conn("127.0.0.1", port, timeout=2)
            c.status()
            return True
        except (OSError, EOFError, TimeoutError, ValueError, RuntimeError):
            time.sleep(0.3)
        finally:
            if c is not None:
                c.close()
    return False


def stop_process(proc, timeout=10):
    """Terminate, escalate, and reap the server process group we own."""
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

def drain(c: Conn, secs=1.0):
    out = []
    t_end = time.monotonic() + secs
    c.sock.settimeout(0.3)
    while time.monotonic() < t_end:
        try:
            pid, data = c.recv_packet()
        except socket.timeout:
            continue
        except (OSError, EOFError):
            break
        if pid == 0x27:
            try: c.send_packet_raw(0x1A, data)
            except (OSError, EOFError): break
        elif pid == 0x42:
            try:
                bio = io.BytesIO(data); tid, _ = read_varint(bio)
                c.send_packet_raw(0x00, write_varint(tid))
            except (OSError, EOFError, ValueError): break
        else:
            out.append((pid, data))
    try: c.sock.settimeout(8)
    except OSError: pass
    return out

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default="./build/cppfm")
    ap.add_argument("--port", type=int, default=0)
    args = ap.parse_args()
    binary = str(args.binary)
    port = args.port or find_free_port()
    world_dir = tempfile.mkdtemp(prefix="p43replay_")
    proc = None
    c = None
    try:
        proc = launch(binary, port, world_dir)
        if not wait_ready(proc, port):
            print("FAIL server did not start"); return 1
        # ---- login + contaminated config (W-12) ----
        c = Conn("127.0.0.1", port, timeout=8)
        try:
            c.login("ReplayVanilla")
        except Exception as e:
            step(False, f"config login ({e})"); return 1
        c.sock.settimeout(8)
        t_end = time.monotonic() + 15
        finished = False
        while time.monotonic() < t_end and not finished:
            pid, data = c.recv_packet()
            if pid == 0x0E: c.send_packet_raw(0x07, write_varint(0))
            elif pid == 0x03: finished = True
            elif pid == 0x04: c.send_packet_raw(0x04, data)
            elif pid == 0x05: c.send_packet_raw(0x05, data)
        step(finished, "config FinishConfiguration received (W-12 setup)")
        if not finished: return 1
        # contaminate then ack
        c.send_packet_raw(0x00, pack_string("en_us") + b"\x08" + write_varint(0) + b"\x01\x7f" + write_varint(0) + b"\x00\x01")
        c.send_packet_raw(0x05, struct.pack(">i", 77))
        c.send_packet_raw(0x06, b"\x00" * 16 + write_varint(0))
        c.send_packet_raw(0x07, write_varint(0))
        c.send_packet_raw(0x03, b"")
        joined = False
        t_end = time.monotonic() + 12
        while time.monotonic() < t_end and not joined:
            try: pid, data = c.recv_packet()
            except Exception: break
            if pid == 0x2C: joined = True
        step(joined, "W-12 contaminated finish -> play JoinGame")
        if not joined:
            return 1
        # ---- W-01 normal move ----
        c.send_packet_raw(0x1C, struct.pack(">ddd", 0.5, -60.0, 0.5) + b"\x01")
        pkts = drain(c, 1.0)
        step(not any(p == 0x1D for p, _ in pkts), "W-01 flags=0x01 move accepted")
        # ---- W-04 tab ----
        c.send_packet_raw(0x0D, write_varint(11) + pack_string("/gam"))
        got10 = None
        t_end = time.monotonic() + 5
        while time.monotonic() < t_end and got10 is None:
            for pid, data in drain(c, 0.8):
                if pid == 0x10:
                    bio = io.BytesIO(data)
                    tid, _ = read_varint(bio)
                    if tid == 11: got10 = data
        step(got10 is not None, "W-04 tab -> CommandSuggestions echo")
        # ---- W-03 signed n=0/1 ----
        for n, cmd in ((0, "seed"), (1, "list")):
            p = pack_string(cmd) + struct.pack(">qq", 0, 0) + write_varint(n)
            for i in range(n):
                p += pack_string(f"arg{i}") + bytes([0xAB]) * 256
            p += write_varint(0) + b"\x00\x00\x00"
            c.send_packet_raw(0x06, p)
            needle = "Seed:" if cmd == "seed" else "Players online"
            ok = False
            t_end = time.monotonic() + 4
            while time.monotonic() < t_end and not ok:
                for pid, data in drain(c, 0.8):
                    if pid == 0x73 and needle.encode() in data:
                        ok = True
            step(ok, f"W-03 signed n={n} executed ('{needle}')")
        # ---- W-07 sign ----
        c.send_packet_raw(0x05, pack_string("setblock 10 -60 8 minecraft:oak_sign"))
        drain(c, 1.5)
        v = ((10 & 0x3FFFFFF) << 38) | ((8 & 0x3FFFFFF) << 12) | ((-60) & 0xFFF)
        pay = struct.pack(">Q", v) + b"\x01" + b"".join(
            pack_string(l) for l in ["RP-1", "RP-2", "RP-3", "RP-4"])
        c.send_packet_raw(0x39, pay)
        ok07 = False
        t_end = time.monotonic() + 5
        while time.monotonic() < t_end and not ok07:
            ok07 = any(p == 0x07 and b"RP-1" in d for p, d in drain(c, 0.8))
        step(ok07, "W-07 sign -> BlockEntityData with text")
        # ---- W-06 abilities follow gamemode ----
        c.send_packet_raw(0x05, pack_string("gamemode survival"))
        ok00 = False
        t_end = time.monotonic() + 5
        while time.monotonic() < t_end and not ok00:
            ok00 = any(p == 0x3A and d and d[0] == 0x00 for p, d in drain(c, 0.8))
        step(ok00, "W-06 survival -> abilities flags 0x00")
    finally:
        if c is not None:
            c.close()
        if proc is not None:
            stop_process(proc)
            proc._logf.close()
        shutil.rmtree(world_dir, ignore_errors=True)
    npass = sum(1 for ok, _ in STEPS if ok)
    nfail = len(STEPS) - npass
    print(f"\n=== REPLAY_VANILLA: {npass} PASS {nfail} FAIL / {len(STEPS)} ===")
    return 1 if nfail else 0

if __name__ == "__main__":
    sys.exit(main())
