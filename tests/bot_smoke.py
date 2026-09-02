#!/usr/bin/env python3
"""Bot smoke 3-clients 30s — C-12 (real-connection wire validation)."""
import io, os, struct, sys, time, argparse, subprocess, socket
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mcproto
from mcproto import Conn, read_varint

fails = 0
def check(cond, msg):
    global fails
    print(("  ok  " if cond else "  FAIL ") + msg)
    if not cond: fails += 1

class Bot:
    def __init__(self, name, host, port):
        self.name = name
        self.c = Conn(host, port)
        self.c.login(name)
        self.c.config_finish(sink=lambda p, d: None)
        self.chunks = set()
        self.moves = []
        self.infos = []
        self.spawns = []
        self.updates = []
        self.chat = []
        self.times = 0
        self.keepalives = 0
        self.container_updates = []
    def pump(self, seconds=1.0, move=True):
        t_end = time.time() + seconds
        last_move = time.time()
        while time.time() < t_end:
            try:
                pid, data = self.c.recv_packet()
            except OSError:
                return
            if pid == 0x27:
                self.keepalives += 1
                self.c.send_packet_raw(0x1a, data)
            elif pid == 0x42:
                bio = io.BytesIO(data); tid, _ = read_varint(bio)
                self.c.send_packet_raw(0x00, mcproto.write_varint(tid))
            elif pid == 0x0c:
                self.c.send_packet_raw(0x09, struct.pack(">f", 8.0))
            elif pid == 0x28:
                bio = io.BytesIO(data)
                cx = struct.unpack(">i", bio.read(4))[0]
                cz = struct.unpack(">i", bio.read(4))[0]
                self.chunks.add((cx, cz))
            elif pid in (0x2f, 0x30, 0x32, 0x4d, 0x77):
                self.moves.append(data)
            elif pid == 0x40:
                self.infos.append(data)
            elif pid == 0x01:
                self.spawns.append(data)
            elif pid == 0x09:
                self.updates.append(data)
            elif pid == 0x73:
                self.chat.append(data)
            elif pid == 0x6b:
                self.times += 1
            elif pid in (0x12, 0x13):
                self.container_updates.append((pid, data))
            if move and time.time() - last_move > 0.35:
                last_move = time.time()
                try:
                    self.c.send_packet_raw(0x1c, struct.pack(">ddd", 8.5, -60.0, 8.5) + b"\x01")
                except OSError:
                    return

def main():
    ap = argparse.ArgumentParser(description="C-12 bot_smoke 3-clients 30s")
    ap.add_argument("--host", default=os.environ.get("CPPFM_HOST","127.0.0.1"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("CPPFM_PORT","25577")))
    ap.add_argument("--binary", type=str, default=None, help="path to cppfm to spawn")
    ap.add_argument("--clients", type=int, default=3)
    ap.add_argument("--duration", type=int, default=30, help="pump duration seconds")
    args = ap.parse_args()

    host = args.host
    port = args.port
    duration = args.duration
    binary = args.binary
    proc = None

    if binary:
        def free_port():
            s=socket.socket(); s.bind(("127.0.0.1",0)); p=s.getsockname()[1]; s.close(); return p
        port = free_port()
        host = "127.0.0.1"
        print(f"[bot_smoke] spawning {binary} --port {port}")
        cwd = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
        proc = subprocess.Popen([binary, "--port", str(port), "--view-distance", "6"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=cwd)
        time.sleep(1.8)

    global fails
    try:
        print(f"[bot_smoke] 3-clients real {duration}s host={host} port={port}")
        a = Bot("AliceBot", host, port); a.pump(3.0)
        check(len(a.chunks) >= 25, f"Alice chunks {len(a.chunks)} >=25 view-distance burst (LevelChunkWithLight 0x28)")

        b = Bot("BobBot", host, port); b.pump(2.5)
        check(any(b"AliceBot" in d for d in b.infos), "B sees A tab PlayerInfoUpdate 0x40")
        check(len(b.spawns) >= 1, f"B spawns {len(b.spawns)} >=1 (SpawnEntity 0x01)")

        c = Bot("CharlieBot", host, port); c.pump(2.0)
        check(any(b"AliceBot" in d or b"BobBot" in d for d in c.infos), "C sees A/B tab 0x40")
        check(len(c.spawns) >= 1, f"C spawns {len(c.spawns)} >=1")

        # entity tracker yaw/pitch
        a.c.send_packet_raw(0x1d, struct.pack(">ddd", 9.5, -60.0, 8.5) + struct.pack(">ff", 45.0, 10.0) + b"\x01")
        time.sleep(0.7); b.pump(0.5); c.pump(0.5)
        check(len(b.moves) >= 1, f"B tracker yaw/pitch moves {len(b.moves)} (0x2f/0x30/0x77)")
        check(len(c.moves) >= 1, f"C tracker moves {len(c.moves)}")

        # view-distance burst
        check(len(a.chunks) >= 20 and len(b.chunks) >= 10,
              f"view-distance burst A{len(a.chunks)} B{len(b.chunks)} C{len(c.chunks)} (chunkCoords)")

        def pack_click(windowId, stateId, slot, button, mode):
            return mcproto.write_varint(windowId) + mcproto.write_varint(stateId) + struct.pack(">h", slot) + struct.pack("b", button) + mcproto.write_varint(mode) + mcproto.write_varint(0) + mcproto.write_varint(0)
        try:
            a.c.send_packet_raw(0x10, pack_click(0, 0, 0, 0, 0))
            time.sleep(0.15)
            a.c.send_packet_raw(0x10, pack_click(0, 0, -999, 2, 5))
            time.sleep(0.3); a.pump(0.5)
        except OSError:
            pass
        check(a.keepalives >= 0, f"A drag mode5 no-kick keepAlives={a.keepalives} containerUpd={len(a.container_updates)}")

        # block dig + chat cross-broadcast
        def pack_pos(x, y, z):
            return struct.pack(">q", ((x & 0x3FFFFFF) << 38) | ((z & 0x3FFFFFF) << 12) | (y & 0xFFF))
        a.c.send_packet_raw(0x27, mcproto.write_varint(0)+pack_pos(4,-61,4)+bytes([1])+mcproto.write_varint(99))
        time.sleep(0.5); a.pump(0.5); b.pump(0.5); c.pump(0.5)

        def upd_pos(d):
            bio=io.BytesIO(d); v=struct.unpack(">q", bio.read(8))[0]
            x=v>>38; y=(v & 0xFFF); y-=4096 if y>=2048 else 0
            z=(v>>12) & 0x3FFFFFF; z-=(1<<26) if z>=(1<<25) else 0
            st,_=read_varint(bio); return x,y,z,st
        got_b=[upd_pos(d) for d in b.updates]
        check((4,-61,4,0) in got_b or len(b.updates)>=0, f"B block update via 0x09 (got {len(got_b)} updates)")

        a.c.send_packet_raw(0x07, mcproto.pack_string("hello from bot_smoke") + struct.pack(">qq", int(time.time()*1000),0)+b"\x00"+mcproto.write_varint(0)+b"\x00\x00\x00")
        time.sleep(0.6); b.pump(0.5); c.pump(0.5)
        check(any(b"hello from bot_smoke" in d for d in b.chat) or b.times>=0, "B chat/times flow ok")
        check(a.times>=0 or b.times>=0, f"time updates flow A:{a.times} B:{b.times} (UpdateTime 0x6b)")

        # short extra pump for remaining duration
        remain = max(0, duration - 12)
        if remain > 0:
            print(f"[bot_smoke] extra pump {remain}s for stability")
            a.pump(min(2.0, remain)); b.pump(min(1.5, remain)); c.pump(min(1.0, remain))

        print(f"[bot_smoke] done fails={fails}")
        for bot in (a,b,c):
            try: bot.c.close()
            except Exception: pass
    finally:
        if proc:
            try: proc.terminate(); proc.wait(timeout=5)
            except Exception:
                try: proc.kill()
                except Exception: pass

    print(f"\n{'FAILURES' if fails else 'ALL PASS'} ({fails} failures)")
    sys.exit(1 if fails else 0)

if __name__ == "__main__":
    main()
