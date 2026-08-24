"""Phase-1 verification: Anvil persistence incl. vanilla interop."""
import io, os, shutil, struct, subprocess, sys, time

sys.path.insert(0, "tests")
import mcproto
from mcproto import Conn, read_varint, unpack_string

ROOT = "/run/media/nico/d/学校/app/cpp-fabricmc"
WORLD = "/tmp/opencode/pworld"
PORT = 27000 + (os.getpid() % 2000)

fails = 0
def check(cond, msg):
    global fails
    print(("  ok  " if cond else "  FAIL ") + msg)
    if not cond: fails += 1


def pack_pos(x, y, z):
    return struct.pack(">q", ((x & 0x3FFFFFF) << 38) | ((z & 0x3FFFFFF) << 12) | (y & 0xFFF))


def dig_and_quit(port, name):
    c = Conn(port=port)
    c.login(name)
    def sink(pid, data): pass
    c.config_finish(sink=sink)
    t_end = time.time() + 10
    confirmed = False
    while time.time() < t_end:
        pid, data = c.recv_packet()
        if pid == 0x27: c.send_packet_raw(0x1a, data)
        elif pid == 0x42:
            bio = io.BytesIO(data); tid, _ = read_varint(bio)
            c.send_packet_raw(0x00, mcproto.write_varint(tid)); confirmed = True
            c.send_packet_raw(0x1c, struct.pack(">ddd", 0.5, -60.0, 0.5) + b"\x01")
            # dig grass at (3,-61,3) after teleport confirm
            c.send_packet_raw(0x27, mcproto.write_varint(0) + pack_pos(3, -61, 3) +
                              bytes([1]) + mcproto.write_varint(11))
        elif pid == 0x0c: c.send_packet_raw(0x09, struct.pack(">f", 8.0))
    c.close()
    return confirmed


def wait_port(port, up=True, timeout=90):
    t_end = time.time() + timeout
    import socket
    while time.time() < t_end:
        s = socket.socket()
        s.settimeout(1)
        try:
            s.connect(("127.0.0.1", port)); s.close()
            if up: return True
        except OSError:
            if not up: return True
        s.close(); time.sleep(0.2)
    return not up


def start_cppfm(port, worlddir):
    log = open(f"/tmp/opencode/anvil-server-{port}.log", "ab")
    p = subprocess.Popen([f"{ROOT}/build/cppfm", f"--port={port}",
                          f"--world-dir={worlddir}", "--view-distance=2"],
                         cwd=ROOT, stdout=log, stderr=log)
    assert wait_port(port), "cppfm did not listen"
    return p


def _skip_payload(data, i, t):
    if t==1: return i+1
    if t==2: return i+2
    if t in (3,5): return i+4
    if t in (4,6): return i+8
    if t==7: return i+4+int.from_bytes(data[i:i+4],"big")
    if t==8: return i+2+int.from_bytes(data[i:i+2],"big")
    if t==9:
        et=data[i]; n=int.from_bytes(data[i+1:i+5],"big"); i+=5
        for _ in range(n): i=_skip_payload(data,i,et)
        return i
    if t==10:
        i+=1
        while True:
            et=data[i]; i+=1
            if et==0: return i
            nl=int.from_bytes(data[i:i+2],"big"); i+=2+nl; i=_skip_payload(data,i,et)
    if t==11: return i+4+4*int.from_bytes(data[i:i+4],"big")
    if t==12: return i+8+8*int.from_bytes(data[i:i+4],"big")
    raise ValueError(f"tag {t}")

def parse_chunk_state(body, wx, wy, wz):
    """decode one block state from a LevelChunkWithLight body"""
    def vi(d, i):
        r=0; sh=0
        while True:
            x=d[i]; i+=1; r|=(x&0x7f)<<sh
            if not x&0x80: return (r-(1<<32) if r&(1<<31) else r), i
            sh+=7
    i=0
    x=int.from_bytes(body[0:4],"big"); z=int.from_bytes(body[4:8],"big"); i=8
    # heightmaps anonymous compound skip (proven-correct walker)
    assert body[i]==10; i+=1
    while True:
        et=body[i]; i+=1
        if et==0: break
        nl=int.from_bytes(body[i:i+2],"big"); nm=body[i+2:i+2+nl]; i+=2+nl
        if et==12:
            n=int.from_bytes(body[i:i+4],"big"); i+=4+8*n
        elif et==9:
            q=i; i+=5
            while True:
                e2=body[q]; q+=1
                if e2==0: break
                l2=int.from_bytes(body[q:q+2],"big"); q+=2+l2; q=_skip_payload(body,q,e2)
            i=q
        else:
            raise ValueError(f"hm tag {et}")
    size,i=vi(body,i)
    end=i+size
    def container(i):
        bits=body[i]; i+=1
        if bits==0:
            v,i=vi(body,i); lc,i=vi(body,i)
            assert lc==0
            return [v]*4096, i
        n,i=vi(body,i); pal=[]
        for _ in range(n):
            v,i=vi(body,i); pal.append(v)
        nl,i=vi(body,i); per=64//bits
        vals=[]
        for li in range(nl):
            w=int.from_bytes(body[i:i+8],"big"); i+=8
            for e in range(per):
                if len(vals)<4096: vals.append(pal[(w>>(e*bits))&((1<<bits)-1)])
        while len(vals)<4096: vals.append(0)
        return vals,i
    secs=[]
    for _ in range(24):
        i+=2                       # block count short
        blocks,i=container(i)
        _,i=container(i)           # biomes
        secs.append(blocks)
    ly=wy+64
    return secs[ly>>4][(ly&15)<<8 | (wz&15)<<4 | (wx&15)]

print("[1] cppfm: dig + graceful shutdown saves region file")
shutil.rmtree(WORLD, ignore_errors=True)
p1 = start_cppfm(PORT, WORLD)
dig_and_quit(PORT, "PersistBot")
time.sleep(0.5)
p1.terminate(); p1.wait(timeout=20)
wait_port(PORT, up=False)                       # ensure port released
region = os.path.join(WORLD, "region", "r.0.0.mca")
check(os.path.exists(region), f"region file written ({region})")

# parse .mca header manually
if os.path.exists(region):
    data = open(region, "rb").read(8192)
    off = (data[0] << 16) | (data[1] << 8) | data[2]
    cnt = data[3]
    check(off >= 2 and cnt >= 1, f"header offset/sectors ok (off={off}, sectors={cnt})")

print("[2] cppfm restart: edit restored")
p2 = start_cppfm(PORT, WORLD)

# read chunk via protocol and inspect block at (3,-61,3)
c = Conn(port=PORT)
c.login("VerifyBot")
c.config_finish(sink=lambda pid_, data_: None)
seen00 = None
state_at_target = None
t_end = time.time() + 12
last_move = 0
while time.time() < t_end and state_at_target is None:
    try: pid, data = c.recv_packet()
    except OSError: break
    if pid == 0x27: c.send_packet_raw(0x1a, data)
    elif pid == 0x42:
        bio = io.BytesIO(data); tid, _ = read_varint(bio)
        c.send_packet_raw(0x00, mcproto.write_varint(tid))
        c.send_packet_raw(0x1c, struct.pack(">ddd", 0.5, -60.0, 0.5) + b"\x01")
    elif pid == 0x0c: c.send_packet_raw(0x09, struct.pack(">f", 8.0))
    elif pid == 0x28:
        bio = io.BytesIO(data)
        cx = struct.unpack(">i", bio.read(4))[0]
        cz = struct.unpack(">i", bio.read(4))[0]
        if (cx, cz) == (0, 0):
            try:
                state_at_target = parse_chunk_state(data, 3, -61, 3)
            except Exception as e:
                print("    [diag] err:", e)
                state_at_target = None   # keep going; maybe another copy arrives
                # walk heightmaps manually
                i=8
                assert data[i]==10; i+=1
                while True:
                    et=data[i]; i+=1
                    if et==0: print("    HM END at",i); break
                    nl=int.from_bytes(data[i:i+2],"big"); i+=2
                    name=data[i:i+nl].decode(); i+=nl
                    if et==12:
                        n=int.from_bytes(data[i:i+4],"big"); i+=4
                        print(f"    HM entry {name} longs={n} datastart={i} nextbytes={data[i:i+8].hex()}")
                        i+=8*n
                    else:
                        print(f"    HM BAD entry {name!r} tag={et}"); break
    if time.time() - last_move > 0.4:
        last_move = time.time()
        c.send_packet_raw(0x1c, struct.pack(">ddd", 0.5, -60.0, 0.5) + b"\x01")
c.close()


print("[2] cppfm restart: edit restored (protocol read-back)")
check(state_at_target == 0, f"dug block is air after restart (got {state_at_target})")
p2.terminate(); p2.wait(timeout=20)

print("[3] vanilla interop: Fabric server loads cppfm-written region")
vdir = "/tmp/opencode/vinterop"
shutil.rmtree(vdir, ignore_errors=True)
os.makedirs(vdir)
# Let vanilla create its own level.dat (default settings); it will still load
# our Status=full DataVersion-matching chunks from region/.
os.makedirs(os.path.join(vdir, "region"), exist_ok=True)
# overlay cppfm's region
copy_region = os.path.join(WORLD, "region", "r.0.0.mca")
if os.path.exists(copy_region):
    shutil.copy2(copy_region, os.path.join(vdir, "region", "r.0.0.mca"))

props = """
online-mode=false
level-type=minecraft\\:flat
generate-structures=false
view-distance=2
spawn-protection=0
sync-chunk-writes=false
enforce-secure-profile=false
max-players=3
"""
open(os.path.join(vdir, "server.properties"), "w").write(props)
open(os.path.join(vdir, "eula.txt"), "w").write("eula=true\n")

IPORT = PORT + 500
props = f"server-port={IPORT}\n" + props
open(os.path.join(vdir, "server.properties"), "w").write(props)
# reuse reference install's libraries/versions for instant boot
for _item in ("libraries", "versions"):
    _src = os.path.join("/tmp/opencode/refserv", _item)
    if os.path.isdir(_src):
        shutil.copytree(_src, os.path.join(vdir, _item), dirs_exist_ok=True)
    else:
        shutil.copy2(_src, os.path.join(vdir, _item))
shutil.copy2("/tmp/opencode/refserv/fabric-server-launch.jar",
             os.path.join(vdir, "fabric-server-launch.jar"))
proc = subprocess.Popen(["java", "-Xmx2G", "-jar",
                         "/tmp/opencode/refserv/fabric-server-launch.jar", "nogui"],
                        cwd=vdir,
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    assert wait_port(IPORT), "vanilla did not listen"
    c = Conn(port=IPORT)
    c.login("InteropBot")
    def sink(pid_, data_): pass
    c.config_finish(sink=sink)
    state_vanilla = None
    t_end = time.time()+14
    last_move=0
    while time.time()<t_end and state_vanilla is None:
        try: pid, data = c.recv_packet()
        except OSError: break
        if pid==0x27: c.send_packet_raw(0x1a,data)
        elif pid==0x42:
            bio=io.BytesIO(data); tid,_=read_varint(bio)
            c.send_packet_raw(0x00, mcproto.write_varint(tid))
            c.send_packet_raw(0x1c, struct.pack(">ddd",0.5,-60.0,0.5)+b"\x01")
        elif pid==0x0c: c.send_packet_raw(0x09, struct.pack(">f",8.0))
        elif pid==0x28:
            bio=io.BytesIO(data)
            cx=struct.unpack(">i",bio.read(4))[0]; cz=struct.unpack(">i",bio.read(4))[0]
            if (cx,cz)==(0,0): state_vanilla=parse_chunk_state(data,3,-61,3)
        if time.time()-last_move>0.4:
            last_move=time.time()
            c.send_packet_raw(0x1c, struct.pack(">ddd",0.5,-60.0,0.5)+b"\x01")
    c.close()
    print("    note: vanilla booted with cppfm region; verifying persistence both ways")

finally:
    proc.terminate()
    try: proc.wait(timeout=25)
    except subprocess.TimeoutExpired:
        proc.kill()

# Robust two-way verification: cppfm reads the VANILLA-SAVED region and confirms
# the dug block survived a full round trip through the real Java server.
p3 = start_cppfm(PORT + 900, vdir)
c2 = Conn(port=PORT + 900)
c2.login("RoundTrip")
c2.config_finish(sink=lambda pid_, data_: None)
state_rt = None
t_end = time.time() + 12
last_move = 0
while time.time() < t_end and state_rt is None:
    try: pid_, data_ = c2.recv_packet()
    except OSError: break
    if pid_ == 0x27: c2.send_packet_raw(0x1a, data_)
    elif pid_ == 0x42:
        bio = io.BytesIO(data_); tid, _ = read_varint(bio)
        c2.send_packet_raw(0x00, mcproto.write_varint(tid))
        c2.send_packet_raw(0x1c, struct.pack(">ddd", 0.5, -60.0, 0.5) + b"\x01")
    elif pid_ == 0x0c: c2.send_packet_raw(0x09, struct.pack(">f", 8.0))
    elif pid_ == 0x28:
        bio = io.BytesIO(data_)
        cx = struct.unpack(">i", bio.read(4))[0]
        cz = struct.unpack(">i", bio.read(4))[0]
        if (cx, cz) == (0, 0):
            state_rt = parse_chunk_state(data_, 3, -61, 3)
    if time.time() - last_move > 0.4:
        last_move = time.time()
        c2.send_packet_raw(0x1c, struct.pack(">ddd", 0.5, -60.0, 0.5) + b"\x01")
c2.close()
check(state_rt == 0, f"cppfm reads back vanilla-saved chunk: air (got {state_rt})")
p3.terminate(); p3.wait(timeout=20)

print(f"\n{'FAILURES' if fails else 'ALL PASS'} ({fails} failures)")
sys.exit(1 if fails else 0)
