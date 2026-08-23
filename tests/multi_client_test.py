"""Two clients online simultaneously: cross-visibility of chat and block edits."""
import io, os, struct, sys, time
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
    def __init__(self, name):
        self.name = name
        self.c = Conn(HOST, PORT)
        self.c.login(name)
        self.c.config_finish(sink=lambda p, d: None)
        self.chat = []
        self.updates = []
        self.chunks = set()
        self.confirmed = False
        self.infos = []
        self.last_move = 0.0
    def pump(self, seconds=1.0, move=True):
        t_end = time.time() + seconds
        while time.time() < t_end:
            try:
                pid, data = self.c.recv_packet()
            except OSError:
                return
            if pid == 0x27:
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
            now = time.time()
            if move and now - self.last_move > 0.35 and not self.confirmed:
                pass
            if move and now - self.last_move > 0.35:
                self.last_move = now
                try:
                    self.c.send_packet_raw(0x1c, struct.pack(">ddd", 8.5, -60.0, 8.5) + b"\x01")
                except OSError:
                    return

a = Bot("AliceBot")
a.pump(3.0)
b = Bot("BobBot")
b.pump(2.5)

# B should have seen A's join message among chat packets
sees_alice_tab = any(b"AliceBot" in d for d in b.infos)
check(sees_alice_tab, "B has A in tab list via player_info")

# A digs a distinctive block; both should get the update
def pack_pos(x, y, z):
    return struct.pack(">q", ((x & 0x3FFFFFF) << 38) | ((z & 0x3FFFFFF) << 12) | (y & 0xFFF))
a.c.send_packet_raw(0x27, mcproto.write_varint(0) + pack_pos(3, -61, 3) +
                    bytes([1]) + mcproto.write_varint(99))
time.sleep(0.6)
a.pump(0.5); b.pump(0.5)

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
check((3, -61, 3, 0) in got_a, f"A received own dig update {got_a}")
check((3, -61, 3, 0) in got_b, f"B received A's dig update {got_b}")

# A sends chat; B must receive it
a.c.send_packet_raw(0x07, mcproto.pack_string("hi bob") +
                    struct.pack(">qq", int(time.time()*1000), 0) + b"\x00" +
                    mcproto.write_varint(0) + b"\x00\x00\x00")
time.sleep(0.6); b.pump(0.5)
check(any(b"<AliceBot> hi bob" in d for d in b.chat), "B received A's chat line")

# disconnect A; B should see leave message + player_remove
a.c.close()
time.sleep(0.8); b.pump(0.8)
check(any(b"AliceBot left" in d for d in b.chat), "B saw A's leave broadcast")

for bot in (a, b):
    try: bot.c.close()
    except Exception: pass

print(f"\n{'FAILURES' if fails else 'ALL PASS'} ({fails} failures)")
sys.exit(1 if fails else 0)
