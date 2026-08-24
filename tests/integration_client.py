"""Integration test: joins our C++ server and validates the whole flow."""
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

# ------------------------------------------------------------------ helpers
def _sign(v, bits): return v - (1 << bits) if v >= (1 << (bits - 1)) else v
def pack_pos(x, y, z):
    return struct.pack(">q", ((x & 0x3FFFFFF) << 38) | ((z & 0x3FFFFFF) << 12) | ((y & 0xFFF) << 0))

def skip_nbt(bio):
    def payload(t):
        if t == 1: bio.read(1)
        elif t == 2: bio.read(2)
        elif t in (3, 5): bio.read(4)
        elif t in (4, 6): bio.read(8)
        elif t == 7: bio.read(struct.unpack(">i", bio.read(4))[0])
        elif t == 8: bio.read(struct.unpack(">h", bio.read(2))[0])
        elif t == 9:
            et = bio.read(1)[0]; n = struct.unpack(">i", bio.read(4))[0]
            for _ in range(n): payload(et)
        elif t == 10:
            while True:
                et = bio.read(1)[0]
                if et == 0: break
                bio.read(struct.unpack(">h", bio.read(2))[0]); payload(et)
        elif t == 11: bio.read(4 * struct.unpack(">i", bio.read(4))[0])
        elif t == 12: bio.read(8 * struct.unpack(">i", bio.read(4))[0])
        else: raise ValueError(f"tag {t}")
    t = bio.read(1)[0]
    if t != 10: raise ValueError("root not compound")
    payload(10)

def read_container(bio):
    """returns list of 4096 values"""
    bits = bio.read(1)[0]
    if bits == 0:
        val, _ = read_varint(bio)
        lc, _ = read_varint(bio)          # long count present even when single!
        assert lc == 0, lc
        return [val] * 4096
    n, _ = read_varint(bio)
    pal = []
    for _ in range(n):
        v, _ = read_varint(bio); pal.append(v)
    nl, _ = read_varint(bio)
    per = 64 // bits
    vals = []
    raw = bio.read(8 * nl)
    for li in range(nl):
        word = int.from_bytes(raw[li*8:(li+1)*8], "big")
        for e in range(per):
            if len(vals) < 4096:
                vals.append(pal[(word >> (e * bits)) & ((1 << bits) - 1)])
    while len(vals) < 4096: vals.append(0)
    return vals

def parse_chunk_blocks(data):
    """-> dict {(lx,y,lz)->state} is too big; instead return callable lookup via sections"""
    bio = io.BytesIO(data)
    x = struct.unpack(">i", bio.read(4))[0]
    z = struct.unpack(">i", bio.read(4))[0]
    skip_nbt(bio)
    size, _ = read_varint(bio)
    blob = bio.read(size)
    sbio = io.BytesIO(blob)
    sections = []
    for s in range(24):
        cnt = struct.unpack(">h", sbio.read(2))[0]
        blocks = read_container(sbio)
        biomes = read_container(sbio)
        sections.append((cnt, blocks))
    return x, z, sections

def block_at(sections, wx, wy, wz):
    sec = (wy + 64) >> 4
    yi = (wy + 64) & 15
    return sections[sec][1][yi * 256 + (wz & 15) * 16 + (wx & 15)]

# ---------------------------------------------------------------- status
print("[1] status ping")
c = Conn(HOST, PORT)
js = c.status()
c.close()
check(js.get("version", {}).get("protocol") == 769, "status protocol == 769")
check(js.get("version", {}).get("name") == "1.21.4", "status version name")
check("players" in js and "description" in js, "players/description present")

# ---------------------------------------------------------------- login+config
print("[2] login & configuration")
c = Conn(HOST, PORT)
c.login("TestBot")
registries = {}
tags = None
brand = False
def sink(pid, data):
    global tags, brand
    if pid == 0x07:
        registries[unpack_string(io.BytesIO(data))] = 1
    elif pid == 0x0d:
        tags = data
    elif pid == 0x01:
        if unpack_string(io.BytesIO(data)) == "minecraft:brand": brand = True
c.config_finish(sink=sink, max_seconds=15)
check(len(registries) == 12, f"12 registry packets (got {len(registries)})")
check("minecraft:worldgen/biome" in registries, "biome registry sent")
check(tags is not None and len(tags) > 1000, "update_tags payload replayed")
check(brand, "brand custom payload sent")

# ---------------------------------------------------------------- play join
print("[3] play: join game, chunks, chat, dig")
join = None
chunks = {}          # (cx,cz) -> latest data
teleports = []
chat = []
block_updates = []
acks = []
keepalives = 0
sent_pos = 0

deadline = time.time() + 25
last_move = 0.0
chat_sent = False
dig_sent = False
while time.time() < deadline:
    try:
        pid, data = c.recv_packet()
    except (EOFError, OSError):
        break
    if pid == 0x2c and join is None:
        join = data
        c.send_packet_raw(0x2a, b"")                       # player_loaded
    elif pid == 0x42:
        bio = io.BytesIO(data); tid, _ = read_varint(bio)
        teleports.append(tid)
        c.send_packet_raw(0x00, mcproto.write_varint(tid))
    elif pid == 0x28:
        bio = io.BytesIO(data)
        cx = struct.unpack(">i", bio.read(4))[0]
        cz = struct.unpack(">i", bio.read(4))[0]
        chunks[(cx, cz)] = data
    elif pid == 0x0c:
        c.send_packet_raw(0x09, struct.pack(">f", 8.0))
    elif pid == 0x27:
        keepalives += 1
        c.send_packet_raw(0x1a, data)
    elif pid == 0x73: chat.append(data)
    elif pid == 0x09: block_updates.append(data)
    elif pid == 0x05: acks.append(data)

    if chunks and not chat_sent:
        chat_sent = True
        c.send_packet_raw(0x07, mcproto.pack_string("hello from test") +
                          struct.pack(">qq", int(time.time() * 1000), 0) + b"\x00" +
                          mcproto.write_varint(0) + b"\x00\x00\x00")
    if chat_sent and not dig_sent:
        dig_sent = True
        c.send_packet_raw(0x27, mcproto.write_varint(0) + pack_pos(0, -61, 0) +
                          bytes([1]) + mcproto.write_varint(7))
    if time.time() - last_move > 0.35:
        last_move = time.time()
        body = struct.pack(">ddd", 8.5, -60.0, 8.5) + b"\x01"
        c.send_packet_raw(0x1c if sent_pos % 2 == 0 else 0x1f, body)
        sent_pos += 1
    if did_all := (keepalives >= 1 and dig_sent and block_updates and acks
                   and chat and len(chunks) >= 9):
        break

check(join is not None, "received join game (login)")
if join:
    bio = io.BytesIO(join)
    eid = struct.unpack(">i", bio.read(4))[0]
    hardcore = bio.read(1)[0]
    nw, _ = read_varint(bio)
    worlds = [unpack_string(bio) for _ in range(nw)]
    maxp, _ = read_varint(bio)
    vd, _ = read_varint(bio)
    sd, _ = read_varint(bio)
    reduced = bio.read(1)[0]; respawn = bio.read(1)[0]; limited = bio.read(1)[0]
    dt, _ = read_varint(bio)
    dname = unpack_string(bio)
    seed = struct.unpack(">q", bio.read(8))[0]
    gm = struct.unpack(">b", bio.read(1))[0]
    pgm = bio.read(1)[0]
    dbg = bio.read(1)[0]; flat = bio.read(1)[0]; hasdeath = bio.read(1)[0]
    portal, _ = read_varint(bio)
    sea, _ = read_varint(bio)
    secure = bio.read(1)[0]
    consumed = bio.tell() == len(join)
    check(len(worlds) == 1 and worlds[0] == "minecraft:overworld", "worlds list correct")
    check(dt == 0, f"dimension type index 0 (got {dt})")
    check(dname == "minecraft:overworld", "dimension name overworld")
    check(gm == 0, "gamemode survival (default)")
    check(flat == 1, "isFlat true")
    check(sea == -63, f"sea level -63 (got {sea})")
    check(consumed, "join game fully consumed (layout exact)")

check(len(chunks) >= 9, f"received chunks ({len(chunks)})")
check(len(teleports) >= 1, "position sync received & confirmed")
check(len(chat) >= 2, f"system chat received ({len(chat)}: welcome+broadcast)")
check(len(block_updates) >= 1, f"block update after dig ({len(block_updates)})")
check(len(acks) >= 1, f"block change ack received ({len(acks)})")

def unpack_pos(v):
    return (_sign(v >> 38, 26), _sign(v & 0xFFF, 12), _sign((v >> 12) & 0x3FFFFFF, 26))

if block_updates:
    bio = io.BytesIO(block_updates[0])
    v = struct.unpack(">q", bio.read(8))[0]
    xx, yy, zz = unpack_pos(v)
    st, _ = read_varint(bio)
    check((xx, yy, zz) == (0, -61, 0), f"dug position echoed ({xx},{yy},{zz})")
    check(st == 0, f"dug block became air (state {st})")

if (0, 0) in chunks:
    try:
        cx, cz, sections = parse_chunk_blocks(chunks[(0, 0)])
        got_air = block_at(sections, 0, -60, 0)
        got_grass = block_at(sections, 5, -61, 5)
        check(got_air == 0, f"(0,-60,0)==air in streamed chunk (got {got_air})")
        check(got_grass == 9, f"(5,-61,5)==grass_block(9) (got {got_grass})")
    except Exception as e:
        check(False, f"chunk self-parse failed: {e}")

c.close()

# ---------------------------------------------------------------- persistence
print("[4] edits persist across reconnect (read back from chunk bytes)")
c = Conn(HOST, PORT)
c.login("TestBot2")
buf = {}
def sink2(pid, data): buf[pid] = buf.get(pid, 0) + 1
c.config_finish(sink=sink2)
persist_ok = None
t_end = time.time() + 15
seen = {}
last_move = 0.0
confirmed = False
while time.time() < t_end:
    try: pid, data = c.recv_packet()
    except OSError: break
    if pid == 0x27: c.send_packet_raw(0x1a, data)
    elif pid == 0x42:
        bio = io.BytesIO(data); tid, _ = read_varint(bio)
        confirmed = True
        c.send_packet_raw(0x00, mcproto.write_varint(tid))
    elif pid == 0x0c: c.send_packet_raw(0x09, struct.pack(">f", 8.0))
    elif pid == 0x28:
        bio = io.BytesIO(data)
        cx = struct.unpack(">i", bio.read(4))[0]
        cz = struct.unpack(">i", bio.read(4))[0]
        seen[(cx, cz)] = data
        if (cx, cz) == (0, 0):
            try:
                _, _, secs = parse_chunk_blocks(seen[(0, 0)])
                persist_ok = (block_at(secs, 0, -60, 0) == 0 and
                              block_at(secs, 5, -61, 5) == 9)
            except Exception as e:
                print("   parse err:", e)
                persist_ok = False
    if persist_ok is not None and confirmed: break
    if time.time() - last_move > 0.35:
        last_move = time.time()
        c.send_packet_raw(0x1c, struct.pack(">ddd", 8.5, -60.0, 8.5) + b"\x01")
check(persist_ok is True, "previous dig visible in freshly streamed chunk")
c.close()

print(f"\n{'FAILURES' if fails else 'ALL PASS'} ({fails} failures)")
sys.exit(1 if fails else 0)
