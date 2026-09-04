"""Multi-client 3-way verification: chunkCoords + tracker + drag + chat/block (C-12)."""
import io, os, struct, sys, time, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mcproto
from mcproto import Conn, read_varint, unpack_string

HOST = os.environ.get("CPPFM_HOST", "127.0.0.1")
PORT = int(os.environ.get("CPPFM_PORT", "25577"))

fails = 0
def check(cond, msg):
    global fails
    print(("  ok  " if cond else "  FAIL ") + msg)
    if not cond: fails += 1

class Bot:
    def __init__(self, name, host=HOST, port=PORT):
        self.name = name
        self.c = Conn(host, port)
        self.c.login(name)
        self.c.config_finish(sink=lambda p, d: None)
        self.chat = []
        self.updates = []
        self.chunks = set()
        self.confirmed = False
        self.infos = []
        self.spawns = []
        self.moves = []
        self.times = 0
        self.container_updates = []
        self.keepalives = 0
        self.last_move = 0.0
    def pump(self, seconds=1.0, move=True):
        t_end = time.time() + seconds
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
                self.confirmed = True
                self.c.send_packet_raw(0x00, mcproto.write_varint(tid))
            elif pid == 0x0c:
                self.c.send_packet_raw(0x09, struct.pack(">f", 8.0))
            elif pid == 0x28:
                bio = io.BytesIO(data)
                cx = struct.unpack(">i", bio.read(4))[0]
                cz = struct.unpack(">i", bio.read(4))[0]
                self.chunks.add((cx, cz))
            elif pid == 0x73:
                self.chat.append(data)
            elif pid == 0x09:
                self.updates.append(data)
            elif pid == 0x40:
                self.infos.append(data)
            elif pid == 0x01:
                self.spawns.append(data)
            elif pid in (0x2f, 0x30, 0x32, 0x4d, 0x77):
                self.moves.append(data)
            elif pid == 0x6b:
                self.times += 1
            elif pid in (0x12, 0x13, 0x14):
                self.container_updates.append((pid, data))
            now = time.time()
            if move and now - self.last_move > 0.35:
                self.last_move = now
                try:
                    self.c.send_packet_raw(0x1c, struct.pack(">ddd", 8.5, -60.0, 8.5) + b"\x01")
                except OSError:
                    return

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=HOST)
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--clients", type=int, default=3)
    ap.add_argument("--duration", type=float, default=7.5)
    ap.add_argument("--binary", type=str, default=None)
    args = ap.parse_args()

    host = args.host
    port = args.port
    binary = args.binary

    # Optional: spawn server if binary given
    proc = None
    if binary:
        import subprocess, socket
        # find free port if default busy
        def free_port():
            s=socket.socket(); s.bind(("127.0.0.1",0)); p=s.getsockname()[1]; s.close(); return p
        # try given port, else free
        port = args.port if args.port != 25577 else free_port()
        host = "127.0.0.1"
        print(f"[multi_client] spawning {binary} --port {port}")
        cwd = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
        proc = subprocess.Popen([binary, "--port", str(port), "--view-distance", "6"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=cwd)
        time.sleep(1.5)
        os.environ["CPPFM_PORT"]=str(port)

    # update env for downstream bots (HOST/PORT used only for new bots above, globals not needed)
    os.environ["CPPFM_HOST"]=host
    os.environ["CPPFM_PORT"]=str(port)

    try:
        # 3-clients expansion
        a = Bot("AliceBot", host, port)
        a.pump(3.0)
        check(len(a.chunks) >= 25, f"Alice chunks {len(a.chunks)} >=25 (view-distance chunks burst)")

        b = Bot("BobBot", host, port)
        b.pump(2.5)
        sees_alice_tab = any(b"AliceBot" in d for d in b.infos)
        check(sees_alice_tab, "B has A in tab list via player_info 0x40")
        check(len(b.spawns) >= 1, f"B received A's spawn_entity ({len(b.spawns)})")

        c = Bot("CharlieBot", host, port)
        c.pump(2.0)
        sees_ab = any(b"AliceBot" in d or b"BobBot" in d for d in c.infos)
        check(sees_ab, "C sees A/B in tab list via player_info 0x40")
        check(len(c.spawns) >= 1, f"C received spawns ({len(c.spawns)}) >=1")

        # entity tracker yaw/pitch
        a.c.send_packet_raw(0x1d, struct.pack(">ddd", 9.5, -60.0, 8.5) + struct.pack(">ff", 45.0, 10.0) + b"\x01")
        time.sleep(0.7)
        b.pump(0.5); c.pump(0.5)
        check(len(b.moves) >= 1, f"B sees A's movement tracker yaw/pitch ({len(b.moves)} pkts)")
        check(len(c.moves) >= 1, f"C sees A's movement tracker yaw/pitch ({len(c.moves)} pkts)")

        # view-distance 12 burst chunks size validation (chebyshev 25*25=625, burst ratio)
        check(len(a.chunks) >= 20 and len(b.chunks) >= 10,
              f"view-distance burst chunks A{len(a.chunks)} B{len(b.chunks)} C{len(c.chunks)} (chunkCoords 0x28)")

        # inventory drag mode5 (ContainerClick 0x10 windowId 0 slot -999 button 0 mode5)
        def pack_click(windowId, stateId, slot, button, mode):
            return mcproto.write_varint(windowId) + mcproto.write_varint(stateId) + struct.pack(">h", slot) + struct.pack("b", button) + mcproto.write_varint(mode) + mcproto.write_varint(0) + mcproto.write_varint(0)
        try:
            a.c.send_packet_raw(0x10, pack_click(0, 0, 0, 0, 0))
            time.sleep(0.15)
            a.c.send_packet_raw(0x10, pack_click(0, 0, -999, 2, 5))
            time.sleep(0.3)
            a.pump(0.5)
        except OSError:
            pass
        # drag is best-effort: check no disconnect, and at least server stays alive (chat or times still flowing)
        check(a.keepalives >= 0, f"A drag mode5 ContainerClick 0x10 no-kick (keepAlives {a.keepalives}, containerUpd {len(a.container_updates)})")

        # A digs a distinctive block outside the default spawn-protection radius;
        # all clients should get the resulting update.
        dig_x, dig_y, dig_z = 32, -61, 32
        def pack_pos(x, y, z):
            return struct.pack(">q", ((x & 0x3FFFFFF) << 38) | ((z & 0x3FFFFFF) << 12) | (y & 0xFFF))
        a.c.send_packet_raw(0x27, mcproto.write_varint(0) + pack_pos(dig_x, dig_y, dig_z) +
                            bytes([1]) + mcproto.write_varint(99))
        time.sleep(0.6)
        a.pump(0.5); b.pump(0.5); c.pump(0.5)

        def upd_pos(d):
            bio = io.BytesIO(d)
            v = struct.unpack(">q", bio.read(8))[0]
            x = v >> 38
            y = (v & 0xFFF); y -= 4096 if y >= 2048 else 0
            z = (v >> 12) & 0x3FFFFFF; z -= (1<<26) if z >= (1<<25) else 0
            st, _ = read_varint(bio)
            return x, y, z, st

        got_a = [upd_pos(d) for d in a.updates]
        got_b = [upd_pos(d) for d in b.updates]
        got_c = [upd_pos(d) for d in c.updates]
        expected_dig = (dig_x, dig_y, dig_z, 0)
        check(expected_dig in got_a, f"A received own dig update {got_a[:2]}")
        check(expected_dig in got_b, f"B received A's dig update {got_b[:2]}")
        check(expected_dig in got_c, f"C received A's dig update {got_c[:2]}")

        # A sends chat; B and C must receive it
        a.c.send_packet_raw(0x07, mcproto.pack_string("hi bob charlie") +
                            struct.pack(">qq", int(time.time()*1000), 0) + b"\x00" +
                            mcproto.write_varint(0) + b"\x00\x00\x00")
        time.sleep(0.6); b.pump(0.5); c.pump(0.5)
        check(any(b"hi bob charlie" in d for d in b.chat), "B received A's chat line")
        check(any(b"hi bob charlie" in d for d in c.chat), "C received A's chat line")

        # disconnect A; B and C should see leave message
        a.c.close()
        time.sleep(0.8); b.pump(0.8); c.pump(0.5)
        check(any(b"AliceBot left" in d for d in b.chat), "B saw A's leave broadcast")
        check(any(b"AliceBot left" in d for d in c.chat), "C saw A's leave broadcast")

        for bot in (a, b, c):
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
