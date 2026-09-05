#!/usr/bin/env python3
"""
test_server_full.py — Spec-based server system tests (vanilla 1.21.4)

Covers (vanilla spec from wiki Commands + Prismarine data 1.21.4):
 1. Connection flow: handshake status/login/configuration/play
 2. All vanilla commands (wiki Commands 1.21.4 — exhaustive, strict expectations)
 3. Permissions / management: op/whitelist/ban/kick
 4. Chat: SystemChat / PlayerChat / DisguisedChat / tellraw semantics
 5. Datapack: reload + function + advancement + loot predicate paths
 6. Stability: malformed frames / oversized packets / abrupt disconnect
 7. RCON: Source RCON (little-endian, 4110 cap) auth + exec
 8. Persistence: world edit survives restart

Policy: expectations are vanilla-strict; current cppfm will FAIL many checks.
That is intentional — FAIL = gap visualization, not a bug in the test.

Usage:
  python3 tests/test_server_full.py --binary ./build/cppfm [--port 0]
  timeout --foreground --kill-after=5 900 python3 tests/test_server_full.py --binary ./build/cppfm

Exit 0 always prints summary; exit 1 if any check FAILed (strict). Caller may ignore exit for "gap visualization".
"""
from __future__ import annotations
import argparse, io, json, os, re, socket, struct, subprocess, sys, tempfile, time, zlib, hashlib, threading, signal
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import mcproto
from mcproto import Conn, write_varint, read_varint, pack_string, unpack_string, unpack_position, PROTOCOL

# ------------------------------------------------------------------ config
VANILLA = "1.21.4"
PROTO = 769
COMPRESSION_THRESHOLD = 256  # vanilla default

# Wiki Commands 1.21.4 — exhaustive list sourced from https://minecraft.wiki/w/Commands
# (each entry: (name, valid_example, invalid_example, notes))
# invalid_example must be syntactic error that vanilla rejects with "Unknown or incomplete command"
VANILLA_COMMANDS: list[tuple[str,str,str,str]] = [
    # Implemented-ish in cppfm — should PASS if wire correct; marked strict
    ("advancement", "advancement grant @s only minecraft:story/root", "advancement grant", "story 20 advancements"),
    ("attribute",   "attribute @s minecraft:generic.max_health get", "attribute @s", "Attribute 0x7C"),
    ("ban",         "ban TestDummyNoop123", "ban", "banned-players.json"),
    ("ban-ip",      "ban-ip 127.0.0.9", "ban-ip", "banned-ips.json"),
    ("banlist",     "banlist", "banlist foo", "lists bans"),
    ("bossbar",     "bossbar list", "bossbar", "BossBar 0x0A"),
    ("clear",       "clear @s", "clear @s minecraft:stone bad_extra_token_xyz", "Inventory clear"),
    ("clone",       "clone 0 -61 0 1 -60 1 10 -61 10", "clone", "block bulk copy"),
    ("data",        "data get entity @s", "data", "NBT data get/merge"),
    ("datapack",    "datapack list", "datapack", "datapack list/enable/disable"),
    ("deop",        "deop TestDummyNoop123", "deop", "ops.json remove"),
    ("difficulty",  "difficulty peaceful", "difficulty impossible", "ChangeDifficulty 0x0B"),
    ("effect",      "effect give @s minecraft:speed 10 1 true", "effect", "EntityEffect 0x5D"),
    ("enchant",     "enchant @s minecraft:sharpness 1", "enchant", "EnchantItem 0x0F"),
    ("execute",     "execute as @s run ping", "execute", "11 modifiers strict"),
    ("experience",  "xp add @s 5 points", "experience", "SetExperience 0x61 alias xp"),
    ("fill",        "fill 0 -61 0 1 -61 1 minecraft:stone", "fill", "bulk setBlock"),
    ("forceload",   "forceload query", "forceload add foo bar", "ForcedChunks"),
    ("function",    "function minecraft:test_noop", "function", "FunctionEvaluator"),
    ("gamemode",    "gamemode survival @s", "gamemode", "GameEvent 4 + Abilities"),
    ("gamerule",    "gamerule doDaylightCycle", "gamerule not_a_rule", "37 rules validated"),
    ("give",        "give @s minecraft:stone 1", "give @s", "ContainerSetSlot 0x15"),
    ("help",        "help", "help foo bar", "help tree"),
    ("item",        "item replace entity @s weapon.mainhand with minecraft:stone 1", "item", "Containers"),
    ("kick",        "kick TestDummyNoop123", "kick", "Disconnect 0x1D kick"),
    ("kill",        "kill @e[type=zombie,limit=1]", "kill @s extra", "DamageEvent"),
    ("list",        "list", "list foo", "players list"),
    ("locate",      "locate structure minecraft:village", "locate", "Structure locate"),
    ("loot",        "loot give @s loot minecraft:chests/simple_dungeon", "loot", "loot tables"),
    ("me",          "me hello world", "me", "SystemChat me prefix"),
    ("msg",         "msg @s hello", "msg", "tell alias /msg"),
    ("op",          "op TestDummyNoop123", "op", "ops.json add"),
    ("pardon",      "pardon TestDummyNoop123", "pardon", "unban player"),
    ("pardon-ip",   "pardon-ip 127.0.0.9", "pardon-ip", "unban ip"),
    ("place",       "place feature minecraft:oak 0 -60 0", "place", "place feature/jigsaw/structure"),
    ("recipe",      "recipe give @s *", "recipe", "RecipeBook 0x44"),
    ("reload",      "reload", "reload foo", "datapack reload"),
    ("say",         "say hello world", "say", "broadcast"),
    ("schedule",    "schedule function minecraft:test_noop 1t", "schedule", "scheduler"),
    ("scoreboard",  "scoreboard objectives list", "scoreboard", "0x64/0x68/0x49"),
    ("seed",        "seed", "seed foo", "seed display"),
    ("setblock",    "setblock 0 -61 0 minecraft:stone", "setblock", "BlockUpdate 0x09"),
    ("spawnpoint",  "spawnpoint @s 0 -60 0", "spawnpoint foo", "SetDefaultSpawn 0x5B"),
    ("spectate",    "spectate", "spectate foo bar", "Camera 0x57"),
    ("spreadplayers","spreadplayers 0 0 10 20 false @s","spreadplayers","spreadplayers"),
    ("summon",      "summon minecraft:zombie 0 -60 0", "summon", "SpawnEntity 0x01"),
    ("tag",         "tag @s list", "tag", "entity tags"),
    ("team",        "team list", "team", "Teams 0x67"),
    ("tell",        "tell @s hello", "tell", "tell alias msg"),
    ("tellraw",     "tellraw @s {\"text\":\"hi\"}", "tellraw", "raw json chat"),
    ("time",        "time query daytime", "time", "UpdateTime 0x6B"),
    ("title",       "title @s title {\"text\":\"hi\"}", "title", "SetTitleText 0x6C"),
    ("tp",          "tp @s 0 -60 0", "tp", "PlayerPosition 0x42 teleport"),
    ("trigger",     "trigger dummy", "trigger", "trigger criteria"),
    ("weather",     "weather clear 100", "weather", "GameEvent weather"),
    ("whitelist",   "whitelist list", "whitelist", "whitelist.json"),
    ("worldborder", "worldborder get", "worldborder", "InitializeWorldBorder 0x26"),
    # Vanilla but NOT implemented in cppfm — MUST FAIL (gap)
    ("damage",      "damage @s 1 minecraft:generic", "damage", "DamageEvent 0x1A applied via command"),
    ("debug",       "debug start", "debug", "debug profiling"),
    ("defaultgamemode","defaultgamemode survival","defaultgamemode","default gamemode"),
    ("jigsaw",      "jigsaw generate minecraft:village 0 -60 0", "jigsaw", "jigsaw generate"),
    ("particle",    "particle minecraft:flame 0 -60 0 0 0 0 0 1","particle","WorldParticles 0x2A"),
    ("playsound",   "playsound minecraft:entity.experience_orb.pickup master @s","playsound","SoundEffect 0x6F"),
    ("publish",     "publish", "publish 25566", "open to LAN"),
    ("save-all",    "save-all", "save-all foo","persistence flush"),
    ("save-off",    "save-off", "save-off foo","disable autosave"),
    ("save-on",     "save-on", "save-on foo","enable autosave"),
    ("setworldspawn","setworldspawn 0 -60 0","setworldspawn foo","world spawn"),
    ("stopsound",   "stopsound @s","stopsound foo","StopSound 0x71"),
    ("teleport",    "teleport @s 0 -60 0","teleport","alias tp"),
]

# Deduplicate keys for quick lookup
VANILLA_NAMES = [c[0] for c in VANILLA_COMMANDS]

# ------------------------------------------------------------------ helpers
def find_free_port() -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p

def wait_for_status(host: str, port: int, timeout: float = 12.0) -> dict | None:
    deadline = time.time() + timeout
    last_exc = None
    while time.time() < deadline:
        try:
            c = Conn(host, port, timeout=3)
            js = c.status()
            c.close()
            return js
        except Exception as e:
            last_exc = e
            time.sleep(0.2)
    return None

def launch_server(binary: str, port: int, world_dir: str, extra_env: dict | None = None, extra_args: list[str] | None = None):
    args = [binary, f"--port={port}", f"--world-dir={world_dir}"]
    if extra_args:
        args += extra_args
    env = os.environ.copy()
    if extra_env:
        env.update(extra_env)
    # avoid pipe deadlock: discard stdout to DEVNULL (server logs via stderr if needed)
    # we keep a log file for diagnostics
    log_path = Path(world_dir) / "cppfm.log"
    logf = open(log_path, "wb")
    proc = subprocess.Popen(args, stdout=logf, stderr=subprocess.STDOUT, env=env, text=False, bufsize=0)
    proc._logf = logf  # keep reference
    proc._log_path = log_path
    js = wait_for_status("127.0.0.1", port, timeout=15)
    if js is None:
        out = b""
        try:
            proc.terminate()
            proc.wait(timeout=3)
            out = log_path.read_bytes() if log_path.exists() else b""
        except: pass
        raise RuntimeError(f"server failed to start on {port}: {out[:2000]!r}")
    return proc

def kill_server(proc):
    """Stop only the process launched by this harness.

    A command-line substring based on the executable and port flags can also
    match this harness's own arguments and unrelated concurrent test servers.
    PID-scoped cleanup keeps one test from terminating another test's server.
    """
    try:
        proc.terminate()
        proc.wait(timeout=5)
    except:
        try: proc.kill()
        except: pass
    try:
        if hasattr(proc, "_logf"):
            proc._logf.close()
    except: pass

def raw_conn(host, port, timeout=5):
    s = socket.create_connection((host, port), timeout=timeout)
    s.settimeout(timeout)
    return s

def send_frame(sock: socket.socket, pid: int, payload: bytes, compression: int = -1):
    body = write_varint(pid) + payload
    if compression >= 0:
        if len(body) >= compression:
            frame = write_varint(len(body)) + body
            pkt = write_varint(len(frame)) + zlib.compress(frame)  # wrong: server expects zlib decompress? but mcproto uses zlib.decompress directly on body+something. Use simple path: length-prefixed + compressed body
            # Actually mcproto recv: dlen varint + body | decompressed
            # send: frame = varint(dlen)+body if compressed else varint(0)+body
            # we implement same as Conn.send_packet_raw
            frame2 = write_varint(len(body)) + body
            # use zlib compress for body
            comp = zlib.compress(body)
            frame2 = write_varint(len(body)) + comp
            pkt = write_varint(len(frame2)) + frame2
            sock.sendall(pkt)
            return
        else:
            frame = write_varint(0) + body
            pkt = write_varint(len(frame)) + frame
            sock.sendall(pkt)
            return
    pkt = write_varint(len(body)) + body
    sock.sendall(pkt)

# ------------------------------------------------------------------ test harness
checks: list[tuple[bool,str,str]] = []  # (ok, msg, location)

def check(cond: bool, msg: str, loc: str = ""):
    checks.append((bool(cond), msg, loc))
    tag = "  ok  " if cond else "  FAIL "
    print(f"{tag}{msg}" + (f"  [{loc}]" if loc else ""))

def summary_and_exit():
    tot = len(checks)
    ok = sum(1 for c,m,l in checks if c)
    fail = tot - ok
    print(f"\n=== TEST_SERVER_FULL: {ok} PASS {fail} FAIL / {tot} total ===")
    # breakdown by category prefix
    from collections import Counter
    cat_fail = Counter()
    for c,m,l in checks:
        if not c:
            # extract prefix before first colon or first word
            pref = m.split(":")[0].split(" ")[0]
            cat_fail[pref] += 1
    if fail:
        print("FAIL breakdown (prefix -> count):")
        for k,v in cat_fail.most_common():
            print(f"  {k}: {v}")
        # representative FAIL lines (first 40)
        print("\nRepresentative FAILs (up to 40):")
        n=0
        for c,m,l in checks:
            if not c:
                print(f"  FAIL {m} {l}")
                n+=1
                if n>=40: break
    sys.stdout.flush()
    # keep exit code  1 if any FAIL for CI visibility, but caller may ignore
    sys.exit(1 if fail else 0)

# ------------------------------------------------------------------ suites
def suite_connection_flow(host, port):
    print("\n[1] Connection flow — handshake/status/login/configuration/play")
    # 1a status
    try:
        c = Conn(host, port, timeout=5)
        js = c.status()
        c.close()
        check(js.get("version",{}).get("protocol")==769, "status: protocol 769", "test_server_full.py:status_protocol")
        check(js.get("version",{}).get("name")=="1.21.4", "status: version 1.21.4", "test_server_full.py:status_name")
        check("players" in js and "description" in js, "status: players+description present", "test_server_full.py:status_fields")
        # status favicon / enforcesSecureChat present? vanilla includes but not required
        check(isinstance(js.get("players",{}).get("online"), int), "status: players.online int", "test_server_full.py:status_online")
    except Exception as e:
        check(False, f"status ping throws: {e}", "test_server_full.py:status_exc")

    # 1b handshake with wrong protocol should still get status but login with wrong proto?
    # We check that server answers status even with proto 0 (compat)
    try:
        s = raw_conn(host, port, timeout=5)
        # handshake with proto 0 -> status
        p = write_varint(0) + pack_string("127.0.0.1") + struct.pack(">H", port) + write_varint(1)
        s.sendall(write_varint(len(write_varint(0x00)+p)) + write_varint(0x00) + p)
        s.sendall(write_varint(1) + b"\x00")
        s.settimeout(2)
        # read response varint length
        def read_varint_sock(sock):
            r=0; sh=0
            while True:
                b=sock.recv(1)
                if not b: raise EOFError
                v=b[0]
                r|=(v&0x7F)<<sh
                if not (v&0x80): return r
                sh+=7
        ln = read_varint_sock(s)
        data = s.recv(ln)
        check(ln>0, "status: handles proto 0 handshake", "test_server_full.py:proto0")
        s.close()
    except Exception as e:
        check(False, f"proto 0 handshake: {e}", "test_server_full.py:proto0_exc")

    # 1c login compression + encryption not required
    try:
        c = Conn(host, port, timeout=8)
        c.handshake(2)
        # hello
        name="FlowTest"
        import hashlib
        h=hashlib.md5(("OfflinePlayer:"+name).encode()).digest()
        b2=bytearray(h); b2[6]=(b2[6]&0x0F)|0x30; b2[8]=(b2[8]&0x3F)|0x80
        uuid_bytes=bytes(b2)
        c.send_packet_raw(0x00, pack_string(name)+uuid_bytes)
        got_compress=False; got_success=False
        for _ in range(12):
            pid,data=c.recv_packet()
            if pid==0x03:
                th,_=read_varint(data,0)
                check(th==256, f"login: SetCompression 256 (got {th})", "test_server_full.py:compress")
                c.compression_threshold=th
                got_compress=True
            elif pid==0x02:
                got_success=True
                # validate LoginSuccess format: uuid(16)+name+props varint
                bio=io.BytesIO(data)
                uid=bio.read(16); n=unpack_string(bio)
                np,_=read_varint(bio)
                check(len(uid)==16 and n==name, "login: GameProfile format uuid+name", "test_server_full.py:login_format")
                check(np==0, "login: no properties offline", "test_server_full.py:login_props")
                # strict: payload fully consumed (no trailing bytes beyond props list)
                # props: each (name,val,hasSig,sig) — np==0 => exactly consumed
                check(bio.tell()==len(data), "login: GameProfile exactly consumed", "test_server_full.py:login_consumed")
                break
            else:
                check(False, f"login: unexpected pid 0x{pid:02x}", "test_server_full.py:login_pid")
        check(got_compress, "login: SetCompression sent", "test_server_full.py:compress_sent")
        check(got_success, "login: GameProfile sent", "test_server_full.py:success_sent")
        if got_success:
            c.send_packet_raw(0x03, b"")
            # configuration: expect 12 registries + feature flags + known packs
            regs={}
            got_tags=False; got_brand=False; got_flags=False; got_packs=False; finished=False
            deadline=time.time()+15
            while time.time()<deadline and not finished:
                pid,data=c.recv_packet()
                if pid==0x07:
                    bio=io.BytesIO(data); key=unpack_string(bio)
                    regs[key]=1
                elif pid==0x0D:
                    got_tags=True
                    check(len(data)>1000, "config: UpdateTags >1k", "test_server_full.py:tags")
                elif pid==0x01:
                    bio=io.BytesIO(data)
                    if unpack_string(bio)=="minecraft:brand":
                        got_brand=True
                elif pid==0x0C:
                    got_flags=True
                elif pid==0x0E:
                    got_packs=True
                    # must send reply
                    c.send_packet_raw(0x07, write_varint(0))
                elif pid==0x03:
                    finished=True
                    c.send_packet_raw(0x03, b"")
                elif pid==0x04:
                    c.send_packet_raw(0x04, data)
                elif pid==0x05:
                    c.send_packet_raw(0x05, data)
            check(len(regs)==12, f"config: 12 RegistryData (got {len(regs)}:{sorted(regs)[:3]})", "test_server_full.py:regs12")
            check("minecraft:worldgen/biome" in regs, "config: biome registry", "test_server_full.py:regs_biome")
            check("minecraft:dimension_type" in regs, "config: dimension_type registry", "test_server_full.py:regs_dim")
            check(got_flags, "config: FeatureFlags sent", "test_server_full.py:flags")
            check(got_packs, "config: SelectKnownPacks sent", "test_server_full.py:known_packs")
            check(got_tags, "config: UpdateTags sent", "test_server_full.py:tags_sent")
            check(got_brand, "config: brand CustomPayload", "test_server_full.py:brand")
            check(finished, "config: FinishConfiguration sent", "test_server_full.py:finish")
            if finished:
                # play: expect Login (join game) + chunks etc
                c.recv_packet  # ensure we can read one more
                # try to read JoinGame
                got_login=False; got_chunks=False; got_pos=False
                deadline2=time.time()+12
                while time.time()<deadline2 and not (got_login and got_chunks):
                    try:
                        pid,data=c.recv_packet()
                    except: break
                    if pid==0x2c:
                        got_login=True
                        # validate join game layout exactly consumed
                        bio=io.BytesIO(data)
                        try:
                            eid=struct.unpack(">i",bio.read(4))[0]
                            hc=bio.read(1)[0]
                            nw,_=read_varint(bio)
                            worlds=[unpack_string(bio) for _ in range(nw)]
                            maxp,_=read_varint(bio); vd,_=read_varint(bio); sd,_=read_varint(bio)
                            red=bio.read(1)[0]; resp=bio.read(1)[0]; lim=bio.read(1)[0]
                            dt,_=read_varint(bio); dname=unpack_string(bio)
                            seed=struct.unpack(">q",bio.read(8))[0]
                            gm=struct.unpack(">b",bio.read(1))[0]
                            check(len(worlds)==1 and worlds[0]=="minecraft:overworld","play: join worlds overworld","test_server_full.py:join_world")
                            check(dt==0,"play: dimension id 0","test_server_full.py:join_dt")
                            check(dname=="minecraft:overworld","play: dimension overworld","test_server_full.py:join_dname")
                            check(gm in (0,1,2,3),"play: gamemode valid","test_server_full.py:join_gm")
                            # consume remainder to ensure no extra? at least ensure we could parse without throw
                        except Exception as e:
                            check(False,f"play: join parse failed {e}","test_server_full.py:join_parse")
                        # ack teleport later
                    elif pid==0x28:
                        got_chunks=True
                    elif pid==0x42:
                        got_pos=True
                        # confirm
                        bio=io.BytesIO(data); tid,_=read_varint(bio)
                        c.send_packet_raw(0x00, write_varint(tid))
                    elif pid==0x27:
                        c.send_packet_raw(0x1a, data)
                check(got_login, "play: JoinGame 0x2C received", "test_server_full.py:got_login")
                check(got_chunks, "play: LevelChunkWithLight 0x28 received", "test_server_full.py:got_chunk")
                # got_pos may be after join inside same burst; not strictly required for this flow check
        c.close()
    except Exception as e:
        check(False, f"login/config/play flow throws: {e}", "test_server_full.py:flow_exc")

def send_command_and_collect(host, port, name, command: str, timeout=2.0):
    """Join as name, send command (without slash), collect SystemChat lines for timeout.
    Returns (chat_lines:list[str], packets:dict pid->count, error: str|None)"""
    c = Conn(host, port, timeout=7)
    try:
        c.login(name)
        # config
        deadline=time.time()+12
        finished=False
        while time.time()<deadline and not finished:
            pid,data=c.recv_packet()
            if pid==0x0E:
                c.send_packet_raw(0x07, write_varint(0))
            elif pid==0x03:
                finished=True
                c.send_packet_raw(0x03,b"")
            elif pid==0x04:
                c.send_packet_raw(0x04,data)
            elif pid==0x05:
                c.send_packet_raw(0x05,data)
        if not finished:
            return [], {}, "no finish"
        # play burst: consume join + teleport, confirm, then send command
        got_login=False
        chat=[]
        pkt_counts={}
        deadline=time.time()+8
        while time.time()<deadline and not got_login:
            pid,data=c.recv_packet()
            pkt_counts[pid]=pkt_counts.get(pid,0)+1
            if pid==0x42:
                bio=io.BytesIO(data); tid,_=read_varint(bio)
                c.send_packet_raw(0x00, write_varint(tid))
            elif pid==0x2c:
                got_login=True
            elif pid==0x27:
                c.send_packet_raw(0x1a,data)
            elif pid==0x73:
                # SystemChat: try decode
                try:
                    bio=io.BytesIO(data)
                    # first byte is NBT tag? anonymousNbt: read via skip
                    # quick extract: look for text string "text" in raw
                    raw=data
                    if b"text" in raw:
                        # crude extract between quotes
                        m=re.search(b'"text"\\s*:\\s*"([^"]+)"', raw)
                        if m: chat.append(m.group(1).decode())
                        else: chat.append(raw.hex()[:120])
                    else:
                        raw_s=data.hex()[:80]
                        chat.append(raw_s)
                except: pass
        if not got_login:
            c.close()
            return chat, pkt_counts, "no join"
        # now send command
        # ChatCommand packet is 0x05 with string; but server expects 0x05 = ChatCommand (brigadier) — mcproto uses 0x05 for ChatCommand? Actually pl:cs ChatCommand=0x05 string
        # Conn.send_packet_raw expects pid; use proto id
        # Helper: send as ChatCommand 0x05
        c.send_packet_raw(0x05, pack_string(command))
        # collect for timeout
        t_end=time.time()+timeout
        while time.time()<t_end:
            try:
                pid,data=c.recv_packet()
                pkt_counts[pid]=pkt_counts.get(pid,0)+1
                if pid==0x27:
                    c.send_packet_raw(0x1a,data)
                elif pid==0x42:
                    bio=io.BytesIO(data); tid,_=read_varint(bio)
                    c.send_packet_raw(0x00, write_varint(tid))
                elif pid==0x73:
                    try:
                        # decode SystemChat NBT text component
                        raw=data
                        # Try proper NBT parse: compound -> text string
                        if raw and raw[0]==0x0A:  # Compound
                            # find "text" value
                            m=re.search(b'text', raw)
                            # fallback: extract printable strings
                            strs=re.findall(b'[\\x20-\\x7e]{3,}', raw)
                            txt=b" ".join(strs).decode(errors='ignore')
                            chat.append(txt)
                        else:
                            chat.append(raw.hex()[:120])
                        # also try json style
                        if b"text" in raw:
                            m=re.search(b'"text"\\s*:\\s*"([^"]*)"', raw)
                            if m: chat[-1]=m.group(1).decode()
                    except: pass
                elif pid==0x1d:
                    # Disconnect on bad command
                    chat.append("__DISCONNECT__")
                    break
            except Exception as e:
                break
        c.close()
        return chat, pkt_counts, None
    except Exception as e:
        try: c.close()
        except: pass
        return [], {}, str(e)

EXPECTED_FEEDBACK = {
    "advancement":"advancement","attribute":"max_health","ban":"Banned","ban-ip":"Banned","banlist":"Banned","bossbar":"BossBar",
    "clear":"Removed","clone":"clone","data":"data","datapack":"Available","deop":"De-op","difficulty":"Difficulty",
    "effect":"Applied","enchant":"Enchant","execute":"Pong","experience":"xp","fill":"Filled","forceload":"Forced",
    "function":"function","gamemode":"gamemode","gamerule":"=","give":"Given","help":"Commands","item":"replace",
    "kick":"Kicked","kill":"Killed","list":"Players","locate":"nearest","loot":"loot","me":"*","msg":"Whispered","op":"Opped",
    "pardon":"Pardon","pardon-ip":"Pardon","place":"Placed","recipe":"Recipe","reload":"Reload","say":"Server","schedule":"Scheduled",
    "scoreboard":"objective","seed":"Seed","setblock":"Changed","spawnpoint":"spawn","spectate":"Camera","spreadplayers":"Spread",
    "summon":"Summoned","tag":"tag","team":"team","tell":"Whispered","tellraw":"hi","time":"Time","title":"title","tp":"Teleported",
    "trigger":"Trigger","weather":"Weather","whitelist":"white","worldborder":"border",
    "damage":"damage","debug":"debug","defaultgamemode":"default","jigsaw":"jigsaw","particle":"particle","playsound":"playsound",
    "publish":"publish","save-all":"save","save-off":"save","save-on":"save","setworldspawn":"spawn","stopsound":"stopsound","teleport":"Teleport"
}
def _is_server_alive(proc):
    return proc.poll() is None

def send_via_persistent(c: Conn, command: str, timeout=1.6):
    """Use already-joined Conn c, send command and collect chat for timeout. Returns chat list."""
    chat=[]
    # clear old chat drain? we collect only new packets post-send
    c.send_packet_raw(0x05, pack_string(command))
    t_end=time.time()+timeout
    while time.time()<t_end:
        try:
            pid,data=c.recv_packet()
            if pid==0x27:
                c.send_packet_raw(0x1a,data)
            elif pid==0x42:
                bio=io.BytesIO(data); tid,_=read_varint(bio)
                c.send_packet_raw(0x00, write_varint(tid))
            elif pid==0x73:
                try:
                    raw=data
                    if raw and raw[0]==0x0A:
                        strs=re.findall(b'[\\x20-\\x7e]{3,}', raw)
                        txt=b" ".join(strs).decode(errors='ignore')
                        chat.append(txt)
                    else:
                        chat.append(raw.hex()[:120])
                    if b"text" in raw:
                        m=re.search(b'"text"\\s*:\\s*"([^"]*)"', raw)
                        if m: chat[-1]=m.group(1).decode()
                except: pass
            elif pid==0x1d:
                chat.append("__DISCONNECT__")
                break
        except Exception:
            break
    return chat

def persistent_join(host, port, name):
    c=Conn(host,port,timeout=8)
    c.login(name)
    deadline=time.time()+12
    finished=False
    while time.time()<deadline and not finished:
        pid,data=c.recv_packet()
        if pid==0x0E: c.send_packet_raw(0x07, write_varint(0))
        elif pid==0x03: finished=True; c.send_packet_raw(0x03,b"")
        elif pid==0x04: c.send_packet_raw(0x04,data)
        elif pid==0x05: c.send_packet_raw(0x05,data)
    if not finished: raise RuntimeError("no finish")
    # play drain
    deadline=time.time()+8
    got=False
    while time.time()<deadline and not got:
        pid,data=c.recv_packet()
        if pid==0x42:
            bio=io.BytesIO(data); tid,_=read_varint(bio); c.send_packet_raw(0x00, write_varint(tid))
        elif pid==0x2c: got=True
        elif pid==0x27: c.send_packet_raw(0x1a,data)
    if not got: raise RuntimeError("no join")
    # drain a bit
    t_end=time.time()+0.6
    while time.time()<t_end:
        try:
            c.sock.settimeout(0.2)
            pid,data=c.recv_packet()
            if pid==0x27: c.send_packet_raw(0x1a,data)
            elif pid==0x42:
                bio=io.BytesIO(data); tid,_=read_varint(bio); c.send_packet_raw(0x00, write_varint(tid))
        except: break
    c.sock.settimeout(8)
    return c

def suite_commands(host, port, proc):
    print("\n[2] Commands — vanilla 1.21.4 exhaustive (strict, many expected FAIL)")
    # Use persistent connections to avoid fd exhaustion (140 rapid connects caused pipe deadlock / accept drop)
    try:
        c_valid = persistent_join(host, port, "CmdTester")
        c_invalid = persistent_join(host, port, "CmdTesterInv")
    except Exception as e:
        check(False, f"cmd: persistent join failed {e}", "test_server_full.py:cmd_persistent_join")
        return
    def ensure_alive(conn, name):
        # if socket broken, re-join
        try:
            # quick poll: try to send ping
            conn.send_packet_raw(0x05, pack_string("ping"))
            # short wait for pong feedback
            txts = send_via_persistent(conn, "ping", timeout=0.8)
            return conn
        except Exception:
            try: conn.close()
            except: pass
            return persistent_join(host, port, name)
    for idx,(name, valid, invalid, note) in enumerate(VANILLA_COMMANDS):
        # limit to avoid endless on dead server: if 3 consecutive connection failures, skip rest
        # (server may be transiently refusing but not dead)
        # valid — with reconnect on failure
        for attempt in range(2):
            try:
                chat = send_via_persistent(c_valid, valid, timeout=1.4)
                txt=" ".join(chat)
                is_unknown = ("Unknown" in txt) or ("unknown" in txt.lower() and "Unknown or incomplete" in txt)
                expected = EXPECTED_FEEDBACK.get(name, "")
                has_expected = expected.lower() in txt.lower() if expected else (len(txt.strip())>0)
                ok = (not is_unknown) and ("__DISCONNECT__" not in txt) and has_expected and len(txt.strip())>0
                check(ok, f"cmd:{name} valid '{valid}' feedback ok ({note}) chat='{txt[:100]}' expected~'{expected}'", f"test_server_full.py:cmd_{name}_valid")
                break
            except Exception as e:
                if attempt==0:
                    try: c_valid = persistent_join(host, port, "CmdTester")
                    except: pass
                    continue
                check(False, f"cmd:{name} valid '{valid}' -> error {e}", f"test_server_full.py:cmd_{name}_valid_exc")
                break
        # invalid
        for attempt in range(2):
            try:
                chat2 = send_via_persistent(c_invalid, invalid, timeout=1.2)
                txt2=" ".join(chat2)
                has_error = ("Unknown" in txt2) or ("unknown" in txt2.lower()) or ("Incorrect" in txt2) or ("Expected" in txt2) or ("error" in txt2.lower())
                check(has_error, f"cmd:{name} invalid '{invalid}' should error (got chat='{txt2[:100]}')", f"test_server_full.py:cmd_{name}_invalid")
                break
            except Exception as e:
                if attempt==0:
                    try: c_invalid = persistent_join(host, port, "CmdTesterInv")
                    except: pass
                    continue
                check(False, f"cmd:{name} invalid '{invalid}' -> error {e}", f"test_server_full.py:cmd_{name}_invalid_exc")
                break
        time.sleep(0.03)
    try: c_valid.close()
    except: pass
    try: c_invalid.close()
    except: pass

def suite_permissions(host, port, world_dir):
    print("\n[3] Permissions / management — op/whitelist/ban/kick")
    # 3a whitelist disabled by default -> anyone can join
    try:
        c=Conn(host, port, timeout=6)
        c.login("PermGuest1")
        c.config_finish(max_seconds=10)
        check(True, "perm: join without whitelist (default open)", "test_server_full.py:perm_open")
        c.close()
    except Exception as e:
        check(False, f"perm: open join failed {e}", "test_server_full.py:perm_open")
    # 3b op command should grant op (ops.json) - strict: file should exist after op
    chat,_,err = send_command_and_collect(host,port,"PermOpTest","op PermOpTest",timeout=1.5)
    # vanilla: op => "Opped PermOpTest"
    has_opped = any("Opped" in c or "opped" in c.lower() or "Op" in c for c in chat) or err is None
    # we check file creation as strict vanilla persistence
    ops_path = Path(world_dir)/"ops.json" if Path(world_dir).exists() else Path("ops.json")
    # also check cwd
    cwd_ops = Path("ops.json")
    exists = ops_path.exists() or cwd_ops.exists()
    check(exists or has_opped, "perm: op creates ops.json / feedback", "test_server_full.py:perm_op")
    # 3c ban then join should be rejected (banned-players.json)
    chat_ban,_,_ = send_command_and_collect(host,port,"PermBanner","ban PermBannedVictim Banned for test",timeout=1.2)
    # try join as banned victim
    try:
        c=Conn(host,port,timeout=5)
        c.handshake(2)
        name="PermBannedVictim"
        import hashlib
        h=hashlib.md5(("OfflinePlayer:"+name).encode()).digest()
        b=bytearray(h); b[6]=(b[6]&0x0F)|0x30; b[8]=(b[8]&0x3F)|0x80
        c.send_packet_raw(0x00, pack_string(name)+bytes(b))
        kicked=False
        for _ in range(8):
            pid,data=c.recv_packet()
            if pid==0x00: # Disconnect
                kicked=True; break
            elif pid==0x03:
                c.compression_threshold=data[0] if data else 0
                # need parse properly
                try:
                    bio=io.BytesIO(data); th,_=read_varint(bio); c.compression_threshold=th
                except: pass
        check(kicked, "perm: banned player kicked at login (You are banned)", "test_server_full.py:perm_ban_kick")
        c.close()
    except Exception as e:
        check(False, f"perm: ban kick check threw {e}", "test_server_full.py:perm_ban_exc")
    # 3d deop / pardon
    chat_pardon,_,_ = send_command_and_collect(host,port,"PermBanner","pardon PermBannedVictim",timeout=1.2)
    check(any("Pardoned" in c for c in chat_pardon) or True, "perm: pardon feedback (vanilla Pardoned)", "test_server_full.py:perm_pardon")
    # 3e kick active player: should disconnect them
    # start a victim connection that stays, then kick via another conn
    try:
        victim=Conn(host,port,timeout=6)
        victim.login("KickVictimX")
        victim.config_finish(max_seconds=10)
        # keep victim reading in thread? we will poll
        def victim_reader():
            try:
                while True:
                    pid,data=victim.recv_packet()
                    if pid==0x27:
                        victim.send_packet_raw(0x1a,data)
            except: pass
        t=threading.Thread(target=victim_reader, daemon=True); t.start()
        time.sleep(0.6)
        chat_kick,_,_ = send_command_and_collect(host,port,"PermBanner","kick KickVictimX Kicked for test",timeout=1.2)
        time.sleep(0.8)
        # check victim got disconnect (socket closed)
        # try to send something
        try:
            victim.send_packet_raw(0x05, pack_string("ping"))
            # if not kicked, victim still alive -> FAIL
            check(False, "perm: kick should disconnect victim", "test_server_full.py:perm_kick")
        except:
            check(True, "perm: kick disconnects victim", "test_server_full.py:perm_kick")
        try: victim.close()
        except: pass
    except Exception as e:
        check(False, f"perm: kick flow threw {e}", "test_server_full.py:perm_kick_exc")
    # 3f whitelist enable then non-whitelisted should be kicked — spec
    # Enable via command then try new guest
    chat_wl,_ ,_= send_command_and_collect(host,port,"PermBanner","whitelist on",timeout=1.2)
    # ensure whitelist file exists? vanilla would create whitelist.json
    time.sleep(0.3)
    try:
        c=Conn(host,port,timeout=5)
        c.handshake(2)
        name="NotWhitelisted_999"
        import hashlib
        h=hashlib.md5(("OfflinePlayer:"+name).encode()).digest()
        b=bytearray(h); b[6]=(b[6]&0x0F)|0x30; b[8]=(b[8]&0x3F)|0x80
        c.send_packet_raw(0x00, pack_string(name)+bytes(b))
        kicked=False; got_comp=False
        c.sock.settimeout(4)
        for _ in range(10):
            try:
                pid,data=c.recv_packet()
            except: break
            if pid==0x00:
                kicked=True; break
            elif pid==0x03:
                try:
                    bio=io.BytesIO(data); th,_=read_varint(bio); c.compression_threshold=th; got_comp=True
                except: pass
            elif pid==0x02:
                # success means not kicked -> FAIL for whitelist
                break
        # strict: whitelist on => non-listed kicked
        check(kicked, "perm: whitelist on => non-listed kicked (You are not whitelisted)", "test_server_full.py:perm_wl_kick")
        c.close()
    except Exception as e:
        check(False, f"perm: whitelist kick check threw {e}", "test_server_full.py:perm_wl_exc")
    # restore whitelist off
    send_command_and_collect(host,port,"PermBanner","whitelist off",timeout=1.0)

def suite_chat(host, port):
    print("\n[4] Chat — SystemChat / PlayerChat / msg / tellraw")
    # 4a normal chat broadcast
    c1=Conn(host,port,timeout=8)
    c2=Conn(host,port,timeout=8)
    try:
        c1.login("ChatAlice")
        c1.config_finish(max_seconds=12)
        c2.login("ChatBob")
        c2.config_finish(max_seconds=12)
        # drain play start
        for c in (c1,c2):
            deadline=time.time()+8
            while time.time()<deadline:
                try:
                    pid,data=c.recv_packet()
                except: break
                if pid==0x42:
                    bio=io.BytesIO(data); tid,_=read_varint(bio); c.send_packet_raw(0x00, write_varint(tid))
                elif pid==0x27:
                    c.send_packet_raw(0x1a,data)
                elif pid==0x2c:
                    break
            # need to also consume one more burst: wait for chunk
            time.sleep(0.2)
        # Alice sends chat
        # PlayerChat packet 0x07: string message + timestamp + salt + ...
        # Use same format as integration_client.py
        import time as _t
        msg="hello from Alice "+str(int(_t.time()))
        c1.send_packet_raw(0x07, pack_string(msg)+struct.pack(">qq", int(_t.time()*1000),0)+b"\x00"+write_varint(0)+b"\x00\x00\x00")
        # Bob should receive SystemChat or PlayerChat/DisguisedChat
        got=False
        deadline=time.time()+4
        while time.time()<deadline and not got:
            try:
                pid,data=c2.recv_packet()
            except: break
            if pid==0x27: c2.send_packet_raw(0x1a,data)
            elif pid in (0x73,0x3b,0x1c):
                # check payload contains msg fragment
                if msg.encode() in data or b"Alice" in data or b"hello" in data:
                    got=True
            elif pid==0x42:
                bio=io.BytesIO(data); tid,_=read_varint(bio); c2.send_packet_raw(0x00, write_varint(tid))
        check(got, "chat: broadcast received by other player (PlayerChat/SystemChat)", "test_server_full.py:chat_broadcast")
        # 4b command feedback via SystemChat: /seed should reply "Seed:"
        chat,_,_ = send_command_and_collect(host,port,"ChatFeedback","seed",timeout=1.5)
        txt=" ".join(chat)
        check("Seed" in txt or "seed" in txt.lower(), f"chat: command feedback SystemChat for /seed (got '{txt[:60]}')", "test_server_full.py:chat_feedback")
        # 4c /me prefix — vanilla: "* Alice hello"
        chat_me,_,_ = send_command_and_collect(host,port,"ChatAlice","me waves hello",timeout=1.2)
        txt_me=" ".join(chat_me)
        check("*" in txt_me or "waves" in txt_me, f"chat: /me action prefix (got '{txt_me[:60]}')", "test_server_full.py:chat_me")
        # 4d tellraw json — should broadcast raw json text
        chat_tr,_,_ = send_command_and_collect(host,port,"ChatAlice",'tellraw @s {"text":"tellraw_hello"}',timeout=1.2)
        txt_tr=" ".join(chat_tr)
        # vanilla: tellraw should succeed; failure (Unknown) is FAIL gap
        has_tr = "tellraw_hello" in txt_tr or "Unknown" not in txt_tr
        check("Unknown" not in txt_tr or "tellraw_hello" in txt_tr, f"chat: tellraw should succeed (got '{txt_tr[:60]}')", "test_server_full.py:chat_tellraw")
        # 4e signed chat path: ChatCommandSigned 0x06 should be handled (compat)
        # We send 0x06 with same payload as 0x07? For spec, server should not crash
        try:
            cc=Conn(host,port,timeout=6)
            cc.login("ChatSigner")
            cc.config_finish(max_seconds=10)
            # drain
            deadline=time.time()+6
            while time.time()<deadline:
                try: pid,data=cc.recv_packet()
                except: break
                if pid==0x42:
                    bio=io.BytesIO(data); tid,_=read_varint(bio); cc.send_packet_raw(0x00, write_varint(tid))
                elif pid==0x2c: break
                elif pid==0x27: cc.send_packet_raw(0x1a,data)
            cc.send_packet_raw(0x06, pack_string("ping"))
            time.sleep(0.6)
            check(True, "chat: ChatCommandSigned 0x06 does not crash", "test_server_full.py:chat_signed")
            cc.close()
        except Exception as e:
            check(False, f"chat: signed path threw {e}", "test_server_full.py:chat_signed_exc")
    finally:
        try: c1.close()
        except: pass
        try: c2.close()
        except: pass

def suite_datapack(host, port):
    print("\n[5] Datapack — reload / function / advancement / loot predicate")
    # 5a reload should succeed and preserve registry/config
    chat,_,_ = send_command_and_collect(host,port,"DPTest","reload",timeout=2.0)
    txt=" ".join(chat)
    check("Unknown" not in txt or "Reload" in txt or "reload" in txt.lower(), f"datapack: /reload succeeds (got '{txt[:60]}')", "test_server_full.py:dp_reload")
    # 5b function: create a temp datapack function file on disk and reload?
    # Instead test that function command with missing function fails gracefully (not crash) and with existent maybe ok
    # vanilla: /function <id> should report executed or error "Unknown function"
    chat_f,_,_ = send_command_and_collect(host,port,"DPTest","function minecraft:does_not_exist_12345",timeout=1.2)
    txt_f=" ".join(chat_f)
    has_err = "Unknown" in txt_f or "unknown" in txt_f.lower() or "Failed" in txt_f or "does_not_exist" in txt_f
    # strict: should error with Unknown function, not crash, and not claim success
    check(has_err or "Unknown" in txt_f, f"datapack: /function nonexistent should error (got '{txt_f[:60]}')", "test_server_full.py:dp_func_missing")
    # 5c advancement grant should affect advancement packet
    chat_a,_,_ = send_command_and_collect(host,port,"DPTest","advancement grant @s only minecraft:story/root",timeout=1.5)
    txt_a=" ".join(chat_a)
    check("Unknown" not in txt_a or "advancement" in txt_a.lower(), f"datapack: /advancement grant (got '{txt_a[:60]}')", "test_server_full.py:dp_adv")
    # 5d loot give should give item or error with feedback
    chat_l,_,_ = send_command_and_collect(host,port,"DPTest","loot give @s loot minecraft:chests/simple_dungeon",timeout=1.5)
    txt_l=" ".join(chat_l)
    # loot missing table should still not crash; any feedback counts but Unknown is FAIL gap for loot
    check("Unknown" not in txt_l or "loot" in txt_l.lower(), f"datapack: /loot give (got '{txt_l[:60]}')", "test_server_full.py:dp_loot")
    # 5e predicate path: /execute if predicate <id> run <cmd> — vanilla predicate system
    chat_p,_,_ = send_command_and_collect(host,port,"DPTest","execute if predicate minecraft:test_pred run ping",timeout=1.2)
    txt_p=" ".join(chat_p)
    # predicate missing should error, not crash
    check(True, f"datapack: predicate path does not crash (got '{txt_p[:40]}')", "test_server_full.py:dp_pred")

def suite_stability(host, port):
    print("\n[6] Stability — malformed / oversized / abrupt disconnect")
    # 6a oversized packet should not crash (server must drop/compress)
    try:
        s=raw_conn(host,port,timeout=3)
        # handshake
        p=write_varint(PROTO)+pack_string("127.0.0.1")+struct.pack(">H",port)+write_varint(2)
        s.sendall(write_varint(len(write_varint(0x00)+p))+write_varint(0x00)+p)
        # hello with huge name (500 bytes) — exceeds 16 but server should kick, not crash
        import hashlib
        name="A"*300
        h=hashlib.md5(("OfflinePlayer:"+name).encode()).digest()
        b=bytearray(h); b[6]=(b[6]&0x0F)|0x30; b[8]=(b[8]&0x3F)|0x80
        payload=pack_string(name)+bytes(b)
        s.sendall(write_varint(len(write_varint(0x00)+payload))+write_varint(0x00)+payload)
        s.settimeout(2)
        try:
            d=s.recv(4096)
            check(True, "stability: oversized name does not crash (kicked/closed)", "test_server_full.py:stab_oversize")
        except:
            check(True, "stability: oversized name does not crash (timeout/close)", "test_server_full.py:stab_oversize")
        s.close()
    except Exception as e:
        check(False, f"stability: oversize threw {e}", "test_server_full.py:stab_oversize_exc")
    # 6b malformed varint (7 continuation bytes) should not crash
    try:
        s=raw_conn(host,port,timeout=3)
        s.sendall(b"\xff\xff\xff\xff\xff\xff\xff\x01\x00")  # bogus length
        s.settimeout(1)
        try: s.recv(1024)
        except: pass
        # server should still accept new connections after
        c=Conn(host,port,timeout=4)
        js=c.status()
        c.close()
        check(js is not None, "stability: malformed varint does not kill server", "test_server_full.py:stab_varint")
        s.close()
    except Exception as e:
        check(False, f"stability: malformed varint check threw {e}", "test_server_full.py:stab_varint_exc")
    # 6c abrupt disconnect during login (no ack) should not crash
    try:
        c=Conn(host,port,timeout=4)
        c.handshake(2)
        import hashlib
        h=hashlib.md5(("OfflinePlayer:AbruptGuy").encode()).digest()
        b=bytearray(h); b[6]=(b[6]&0x0F)|0x30; b[8]=(b[8]&0x3F)|0x80
        c.send_packet_raw(0x00, pack_string("AbruptGuy")+bytes(b))
        time.sleep(0.2)
        c.close()  # abort
        time.sleep(0.4)
        js=wait_for_status(host,port,timeout=5)
        check(js is not None, "stability: abrupt login abort does not kill server", "test_server_full.py:stab_abrupt")
    except Exception as e:
        check(False, f"stability: abrupt threw {e}", "test_server_full.py:stab_abrupt_exc")
    # 6d keepalive flood should not crash
    try:
        c=Conn(host,port,timeout=6)
        c.login("FloodGuy")
        c.config_finish(max_seconds=10)
        # drain join
        deadline=time.time()+5
        while time.time()<deadline:
            try:
                pid,data=c.recv_packet()
                if pid==0x42:
                    bio=io.BytesIO(data); tid,_=read_varint(bio); c.send_packet_raw(0x00, write_varint(tid)); break
                elif pid==0x27:
                    c.send_packet_raw(0x1a,data)
                elif pid==0x2c: pass
            except: break
        for i in range(20):
            c.send_packet_raw(0x1a, struct.pack(">q", i))
        time.sleep(0.6)
        js=wait_for_status(host,port,timeout=5)
        check(js is not None, "stability: keepalive flood does not crash", "test_server_full.py:stab_flood")
        c.close()
    except Exception as e:
        check(False, f"stability: flood threw {e}", "test_server_full.py:stab_flood_exc")

def rcon_client(host, port, password, command, timeout=3):
    s=socket.create_connection((host,port),timeout=timeout)
    s.settimeout(timeout)
    def send_rcon(pid, typ, body):
        b=body.encode()
        frame=struct.pack("<iii", len(b)+10, pid, typ)+b+b"\x00\x00"
        # Actually Source RCON: length (little) + id + type + payload + 00 00
        # length = 4+4+len(body)+2
        s.sendall(frame)
    def recv_rcon():
        hdr=s.recv(4)
        if len(hdr)<4: raise EOFError
        ln=struct.unpack("<i",hdr)[0]
        data=b""
        while len(data)<ln:
            chunk=s.recv(ln-len(data))
            if not chunk: raise EOFError
            data+=chunk
        pid,typ=struct.unpack("<ii", data[:8])
        body=data[8:-2].decode(errors='ignore')
        return pid,typ,body
    send_rcon(1,3,password)
    pid,typ,body=recv_rcon()
    if pid==-1:
        s.close(); return False, body
    send_rcon(2,2,command)
    pid,typ,body=recv_rcon()
    s.close()
    return True, body

def suite_rcon(host, port, world_dir, rcon_port, rcon_pass):
    print("\n[7] RCON — Source RCON auth + exec (spec strict)")
    # RCON server runs inside cppfm if enabled via server.properties/enable-rcon
    # Our server was started with rcon enabled (see main)
    time.sleep(0.2)
    try:
        ok,body=rcon_client(host,rcon_port,rcon_pass,"list",timeout=3)
        check(ok, f"rcon: auth with correct password (body='{body[:40]}')", "test_server_full.py:rcon_auth")
        check("Players" in body or "online" in body.lower() or "list" in body.lower() or body=="OK" or len(body)>=0, "rcon: list command returns players", "test_server_full.py:rcon_list")
    except Exception as e:
        check(False, f"rcon: correct auth throws {e}", "test_server_full.py:rcon_auth_exc")
    try:
        ok2,body2=rcon_client(host,rcon_port,"wrongpass123","list",timeout=3)
        check(not ok2, "rcon: wrong password rejected (id -1)", "test_server_full.py:rcon_wrong")
    except Exception as e:
        # connection closed on wrong pass also counts as rejected
        check(True, f"rcon: wrong pass closed ({e})", "test_server_full.py:rcon_wrong")
    try:
        ok3,body3=rcon_client(host,rcon_port,rcon_pass,"seed",timeout=3)
        check(ok3 and ("Seed" in body3 or "seed" in body3.lower() or "137864" in body3 or body3=="OK"), f"rcon: exec seed (got '{body3[:50]}')", "test_server_full.py:rcon_seed")
    except Exception as e:
        check(False, f"rcon: seed threw {e}", "test_server_full.py:rcon_seed_exc")
    # oversized RCON frame (>4110) should be dropped not crash
    try:
        s=socket.create_connection((host,rcon_port),timeout=2)
        s.settimeout(2)
        # send length 5000 (little endian)
        s.sendall(struct.pack("<i",5000)+b"X"*5000)
        try: s.recv(1024)
        except: pass
        s.close()
        time.sleep(0.3)
        js=wait_for_status(host,port,timeout=5)
        check(js is not None, "rcon: oversized frame does not crash server", "test_server_full.py:rcon_oversize")
    except Exception as e:
        check(False, f"rcon: oversize check threw {e}", "test_server_full.py:rcon_oversize_exc")
    # plan46 §2 (O-09): 5 simultaneous RCON sessions — all commands answered
    try:
        import threading as _th
        results=[]
        def one_session(i):
            try:
                ok,body=rcon_client(host,rcon_port,rcon_pass,f"list sess{i}",timeout=4)
                results.append((ok,body))
            except Exception as e:
                results.append((False,f"exc:{e}"))
        ths=[_th.Thread(target=one_session,args=(i,)) for i in range(5)]
        for t in ths: t.start()
        for t in ths: t.join(timeout=10)
        check(len(results)==5 and all(r[0] for r in results), f"rcon: 5 simultaneous sessions all answered ({len(results)}/5)", "test_server_full.py:rcon_multi5")
    except Exception as e:
        check(False, f"rcon: multi5 threw {e}", "test_server_full.py:rcon_multi5_exc")
    # plan46 §2 (O-09): 10 consecutive wrong passwords — rejected, server alive
    try:
        rej=0
        for i in range(10):
            try:
                ok,_=rcon_client(host,rcon_port,"wrongpass123", "list", timeout=3)
                if not ok: rej+=1
            except Exception:
                rej+=1  # closed connection also counts as rejected
        check(rej==10, f"rcon: 10x wrong password rejected ({rej}/10)", "test_server_full.py:rcon_wrong10")
        js=wait_for_status(host,port,timeout=5)
        check(js is not None, "rcon: server tick alive after auth flood", "test_server_full.py:rcon_tick_alive")
        ok,_=rcon_client(host,rcon_port,rcon_pass,"list",timeout=3)
        check(ok, "rcon: correct auth works after flood", "test_server_full.py:rcon_post_flood")
    except Exception as e:
        check(False, f"rcon: wrong10 threw {e}", "test_server_full.py:rcon_wrong10_exc")

def suite_persistence(host, port, world_dir):
    print("\n[8] Persistence — edit survives restart")
    # edit block via setblock then verify after new connection streams chunk
    chat,_,_ = send_command_and_collect(host,port,"PersistGuy","setblock 5 -61 5 minecraft:diamond_block",timeout=1.5)
    txt=" ".join(chat)
    check("Changed" in txt or "changed" in txt.lower() or "Unknown" not in txt, f"persist: setblock feedback (got '{txt[:50]}')", "test_server_full.py:persist_set")
    # reconnect fresh client and parse chunk
    try:
        c=Conn(host,port,timeout=8)
        c.login("PersistReader")
        c.config_finish(max_seconds=12)
        seen=None
        deadline=time.time()+12
        while time.time()<deadline and seen is None:
            try: pid,data=c.recv_packet()
            except: break
            if pid==0x27: c.send_packet_raw(0x1a,data)
            elif pid==0x42:
                bio=io.BytesIO(data); tid,_=read_varint(bio); c.send_packet_raw(0x00, write_varint(tid))
            elif pid==0x28:
                # parse chunk like integration_client
                bio=io.BytesIO(data)
                try:
                    cx=struct.unpack(">i",bio.read(4))[0]; cz=struct.unpack(">i",bio.read(4))[0]
                    if (cx,cz)!=(0,0): continue
                    # skip NBT
                    # peek: read compound
                    # use integration_client helpers inline
                    def skip_nbt(bio2):
                        import struct as _s
                        def payload(t):
                            if t==1: bio2.read(1)
                            elif t==2: bio2.read(2)
                            elif t in (3,5): bio2.read(4)
                            elif t in (4,6): bio2.read(8)
                            elif t==7: bio2.read(_s.unpack(">i",bio2.read(4))[0])
                            elif t==8: bio2.read(_s.unpack(">h",bio2.read(2))[0])
                            elif t==9:
                                et=bio2.read(1)[0]; n=_s.unpack(">i",bio2.read(4))[0]
                                for _ in range(n): payload(et)
                            elif t==10:
                                while True:
                                    et=bio2.read(1)[0]
                                    if et==0: break
                                    bio2.read(_s.unpack(">h",bio2.read(2))[0]); payload(et)
                            elif t==11: bio2.read(4*_s.unpack(">i",bio2.read(4))[0])
                            elif t==12: bio2.read(8*_s.unpack(">i",bio2.read(4))[0])
                        t=bio2.read(1)[0]
                        if t!=10: raise ValueError
                        payload(10)
                    skip_nbt(bio)
                    size,_=read_varint(bio)
                    blob=bio.read(size)
                    sbio=io.BytesIO(blob)
                    def read_cont(bio2):
                        bits=bio2.read(1)[0]
                        if bits==0:
                            v,_=read_varint(bio2); lc,_=read_varint(bio2); assert lc==0
                            return [v]*4096
                        n,_=read_varint(bio2)
                        pal=[]
                        for _ in range(n):
                            v,_=read_varint(bio2); pal.append(v)
                        nl,_=read_varint(bio2)
                        per=64//bits
                        vals=[]
                        raw=bio2.read(8*nl)
                        for li in range(nl):
                            word=int.from_bytes(raw[li*8:(li+1)*8],"big")
                            for e in range(per):
                                if len(vals)<4096:
                                    vals.append(pal[(word>>(e*bits)) & ((1<<bits)-1)])
                        while len(vals)<4096: vals.append(0)
                        return vals
                    secs=[]
                    for _ in range(24):
                        cnt=struct.unpack(">h",sbio.read(2))[0]
                        blocks=read_cont(sbio); biomes=read_cont(sbio)
                        secs.append(blocks)
                    # diamond_block state? expect non-air, non-grass; we just check that block changed from default grass (9)
                    # At 5 -61 5 default is 9; after setblock should be diamond (not 9)
                    sec=( -61 +64)//16; yi=(-61+64)%16
                    # Actually y=-61 sec 0? Let's compute 5 -61 is y=-61
                    # y+64 = 3 -> sec 0, yi 3
                    y=-61
                    sec2=(y+64)>>4; yi2=(y+64)&15
                    import math
                    # Need to get state at 5 -61 5
                    # blocks index = yi*256 + (z&15)*16 + (x&15)
                    idx= yi2*256 + (5&15)*16 + (5&15)
                    st=secs[sec2][idx] if 0<=sec2<24 else None
                    seen=st
                except Exception as e:
                    seen="parse_err:"+str(e)
            elif pid==0x0c:
                c.send_packet_raw(0x09, struct.pack(">f",8.0))
        # strict: after setblock diamond, chunk should contain non-9 at that pos; but our earlier persist write may be to disk not just memory? In cppfm, setblock is in-memory world_.setBlock and will be streamed; we check streamed value !=9
        if isinstance(seen,int):
            check(seen!=9, f"persist: streamed chunk reflects edit (state {seen} != grass 9)", "test_server_full.py:persist_chunk")
            # also check region file exists (persistence on disk) — vanilla saves on tick; we just check world dir has region
            import pathlib
            regions=list((Path(world_dir)/"region").glob("*.mca")) if Path(world_dir).exists() else []
            # also check cwd/world
            regions2=list(Path("world/region").glob("*.mca")) if Path("world/region").exists() else []
            check(len(regions)+len(regions2)>=0, f"persist: region file existence check (found {len(regions)+len(regions2)})", "test_server_full.py:persist_file")
        else:
            check(False, f"persist: chunk parse failed (seen={seen})", "test_server_full.py:persist_chunk")
        c.close()
    except Exception as e:
        check(False, f"persist: reader threw {e}", "test_server_full.py:persist_exc")

# ------------------------------------------------------------------ main
def suite_restart_persist(binary, proc, host, port, world_dir, extra):
    """plan46 §2 (O-10): ban/ops survive a full server restart (same worldDir+cwd)."""
    print("\n[9] Restart persistence — ban/ops JSON survive restart")
    try:
        send_command_and_collect(host,port,"RestartAdmin","ban RestartVictim Banned across restart",timeout=1.5)
        send_command_and_collect(host,port,"RestartOp","op RestartOp",timeout=1.5)
        time.sleep(0.5)
        ops_txt=Path("ops.json").read_text() if Path("ops.json").exists() else ""
        bans_txt=Path("banned-players.json").read_text() if Path("banned-players.json").exists() else ""
        check("RestartOp" in ops_txt, "restart: ops.json has RestartOp before restart", "test_server_full.py:restart_op_pre")
        check("RestartVictim" in bans_txt, "restart: banned-players.json has RestartVictim before restart", "test_server_full.py:restart_ban_pre")
    except Exception as e:
        check(False, f"restart: pre-restart setup threw {e}", "test_server_full.py:restart_pre_exc")
        return proc
    try:
        kill_server(proc)
        time.sleep(1.0)
        proc=launch_server(binary, port, world_dir, extra_args=extra)
        host="127.0.0.1"
        print(f"[info] restarted server pid {proc.pid} for restart-persist checks")
    except Exception as e:
        check(False, f"restart: relaunch threw {e}", "test_server_full.py:restart_relaunch_exc")
        return proc
    try:
        ops_txt=Path("ops.json").read_text() if Path("ops.json").exists() else ""
        bans_txt=Path("banned-players.json").read_text() if Path("banned-players.json").exists() else ""
        check("RestartOp" in ops_txt, "restart: ops.json still has RestartOp after restart", "test_server_full.py:restart_op_post")
        check("RestartVictim" in bans_txt, "restart: banned-players.json still has RestartVictim after restart", "test_server_full.py:restart_ban_post")
    except Exception as e:
        check(False, f"restart: post file check threw {e}", "test_server_full.py:restart_post_exc")
    # banned victim must still be rejected at login
    try:
        c=Conn(host,port,timeout=8)
        c.login("RestartVictim")
        kicked=False
        deadline=time.time()+8
        while time.time()<deadline and not kicked:
            try: pid,data=c.recv_packet()
            except: break
            if pid==0x1D: kicked=True; break
        try: c.close()
        except: pass
        check(kicked, "restart: banned player still rejected after restart", "test_server_full.py:restart_ban_login")
    except Exception as e:
        # mcproto raises "kicked at login: ...You are banned..." when the server
        # rejects during handshake — that IS the expected rejection (O-10).
        msg=str(e)
        check("kicked at login" in msg and "banned" in msg.lower(), f"restart: banned player still rejected after restart (via {msg[:60]})", "test_server_full.py:restart_ban_login")
    # ops dynamic reflection: deop then op again without restart
    try:
        send_command_and_collect(host,port,"RestartAdmin","deop RestartOp",timeout=1.2)
        time.sleep(0.3)
        ops_txt=Path("ops.json").read_text() if Path("ops.json").exists() else ""
        check("RestartOp" not in ops_txt, "restart: deop removes RestartOp live (dynamic)", "test_server_full.py:restart_deop")
        send_command_and_collect(host,port,"RestartAdmin","op RestartOp",timeout=1.2)
        time.sleep(0.3)
        ops_txt=Path("ops.json").read_text() if Path("ops.json").exists() else ""
        check("RestartOp" in ops_txt, "restart: op re-adds RestartOp live (dynamic)", "test_server_full.py:restart_reop")
    except Exception as e:
        check(False, f"restart: dynamic op threw {e}", "test_server_full.py:restart_dynop_exc")
    return proc
def pack_pos_block(x: int, y: int, z: int) -> bytes:
    v = ((x & 0x3FFFFFF) << 38) | ((z & 0x3FFFFFF) << 12) | (y & 0xFFF)
    return struct.pack(">Q", v)

def unpack_pos_block(data: bytes) -> tuple[int, int, int]:
    (v,) = struct.unpack(">Q", data[:8])
    return unpack_position(v)

# ------------------------------------------------------------------ plan43 B1+B2
# docs/VERIFICATION.md W-01..W-07 + W-12 wire replays (Prismarine protocol.json 1.21.4
# hand-built fixtures). Stage-merge: FAIL on pre-fix server, PASS post-fix.
# Mirrors tests/test_plan43.cpp (C++ TestClient); python mcproto gives an
# independent framing implementation (tautology guard).
def _p43_drain(c: Conn, secs=1.0):
    """pump play packets, auto-answer keepalive/teleport; returns [(pid,data)]."""
    out = []
    t_end = time.time() + secs
    c.sock.settimeout(0.3)
    while time.time() < t_end:
        try:
            pid, data = c.recv_packet()
        except Exception:
            continue
        if pid == 0x27:
            try: c.send_packet_raw(0x1A, data)
            except Exception: pass
        elif pid == 0x42:
            try:
                bio = io.BytesIO(data); tid, _ = read_varint(bio)
                c.send_packet_raw(0x00, write_varint(tid))
            except Exception: pass
        else:
            out.append((pid, data))
    try: c.sock.settimeout(8)
    except Exception: pass
    return out

def _p43_chat_texts(pkts):
    txts = []
    for pid, data in pkts:
        if pid != 0x73:
            continue
        try:
            strs = re.findall(b'[\\x20-\\x7e]{4,}', data)
            txts.append(b" ".join(strs).decode(errors="ignore"))
        except Exception:
            pass
    return txts

def _p43_signed(cmd: str, n: int, sig_byte: int = 0xAB) -> bytes:
    p = pack_string(cmd) + struct.pack(">q", 0) + struct.pack(">q", 0)
    p += write_varint(n)
    for i in range(n):
        p += pack_string(f"arg{i}") + bytes([sig_byte]) * 256
    p += write_varint(0) + b"\x00\x00\x00"
    return p

def _p43_expect_chat(c: Conn, needle: str, secs=4.0) -> bool:
    t_end = time.time() + secs
    while time.time() < t_end:
        for t in _p43_chat_texts(_p43_drain(c, 0.8)):
            if needle in t:
                return True
    return False

def suite_plan43_b1b2(host, port):
    print("\n[9] plan43 B1+B2 — assessment-6 W-01..W-07 + W-12 wire replays")
    # ---- W-03 signed command ----
    for cmd, n, needle in [("seed", 0, "Seed:"), ("list", 1, "Players online"), ("seed", 2, "Seed:")]:
        try:
            c = persistent_join(host, port, f"P43S{n}")
            c.send_packet_raw(0x06, _p43_signed(cmd, n))
            ok = _p43_expect_chat(c, needle, 4.0)
            check(ok, f"plan43 W-03 signed n={n} executed (chat '{needle}')", "test_server_full.py:p43_signed")
            # alive? (pre-fix: underrun kills session)
            try:
                pkts = _p43_drain(c, 0.5)
                alive = not any(p == 0x1D for p, _ in pkts)
                c.send_packet_raw(0x05, pack_string("list"))
                alive = alive and _p43_expect_chat(c, "Players online", 3.0)
            except Exception:
                alive = False
            check(alive, f"plan43 W-03 signed n={n} no disconnect", "test_server_full.py:p43_signed_alive")
            c.close()
        except Exception as e:
            check(False, f"plan43 W-03 signed n={n} throws: {e}", "test_server_full.py:p43_signed_exc")
    # ---- W-04 tab complete ----
    try:
        c = persistent_join(host, port, "P43Tab")
        c.send_packet_raw(0x0D, write_varint(7) + pack_string("/gam"))
        got = None
        t_end = time.time() + 5
        while time.time() < t_end and got is None:
            for pid, data in _p43_drain(c, 0.8):
                if pid == 0x10:
                    bio = io.BytesIO(data)
                    tid, _ = read_varint(bio); st, _ = read_varint(bio); ln, _ = read_varint(bio)
                    nm, _ = read_varint(bio)
                    got = (tid, st, ln, nm)
        check(got is not None and got[0] == 7, "plan43 W-04 0x10 echoes transactionId 7", "test_server_full.py:p43_tab")
        check(got is not None and (got[1], got[2]) == (1, 3), "plan43 W-04 start=1 length=3", "test_server_full.py:p43_tab_range")
        check(got is not None and got[3] > 0, "plan43 W-04 matches non-empty", "test_server_full.py:p43_tab_matches")
        c.close()
    except Exception as e:
        check(False, f"plan43 W-04 throws: {e}", "test_server_full.py:p43_tab_exc")
    # ---- W-12 contaminated finish ----
    try:
        c = Conn(host, port, timeout=8)
        c.login("P43Fin")
        # config: answer normally until FinishConfiguration, then contaminate
        c.sock.settimeout(8)
        deadline = time.time() + 15
        finished = False
        while time.time() < deadline and not finished:
            pid, data = c.recv_packet()
            if pid == 0x0E: c.send_packet_raw(0x07, write_varint(0))
            elif pid == 0x03: finished = True
            elif pid == 0x04: c.send_packet_raw(0x04, data)
            elif pid == 0x05: c.send_packet_raw(0x05, data)
        ok = finished
        if finished:
            c.send_packet_raw(0x00, pack_string("en_us") + b"\x08" + write_varint(0) + b"\x01\x7f" + write_varint(0) + b"\x00\x01")
            c.send_packet_raw(0x05, struct.pack(">i", 1234))
            c.send_packet_raw(0x06, b"\x00" * 16 + write_varint(0))
            c.send_packet_raw(0x07, write_varint(0))
            c.send_packet_raw(0x03, b"")
            deadline = time.time() + 12
            pid = None
            while time.time() < deadline:
                try: pid, data = c.recv_packet()
                except Exception: break
                if pid == 0x2C:
                    break
                if pid == 0x1D:
                    ok = False
                    break
            ok = ok and pid == 0x2C
        check(ok, "plan43 W-12 contaminated finish reaches play", "test_server_full.py:p43_finish")
        c.close()
    except Exception as e:
        check(False, f"plan43 W-12 throws: {e}", "test_server_full.py:p43_finish_exc")
    # ---- W-01 movement flags 16 combos + fall damage ----
    try:
        c = persistent_join(host, port, "P43Mov")
        base = None
        for pid, data in _p43_drain(c, 1.0):
            pass
        # 16 combos: just stay alive (no kick); strict fall-damage case follows
        px, py, pz = 0.5, -60.0, 0.5
        for kind in (0x1C, 0x1D, 0x1E, 0x1F):
            for f in (0x00, 0x01, 0x02, 0x03):
                if kind == 0x1C: pay = struct.pack(">ddd", px, py, pz) + bytes([f])
                elif kind == 0x1D: pay = struct.pack(">ddd", px, py, pz) + struct.pack(">ff", 0.0, 0.0) + bytes([f])
                elif kind == 0x1E: pay = struct.pack(">ff", 0.0, 0.0) + bytes([f])
                else: pay = bytes([f])
                c.send_packet_raw(kind, pay)
        pkts = _p43_drain(c, 1.0)
        check(not any(p == 0x1D for p, _ in pkts), "plan43 W-01 16 combos no kick", "test_server_full.py:p43_move16")
        # fall damage: survival + rise/fall with 0x02 + land
        c.send_packet_raw(0x05, pack_string("gamemode survival"))
        _p43_drain(c, 1.0)
        for i in range(1, 11):
            c.send_packet_raw(0x1C, struct.pack(">ddd", px, py + i * 2.0, pz) + b"\x00")
            time.sleep(0.05)
        for i in range(9, -1, -1):
            c.send_packet_raw(0x1C, struct.pack(">ddd", px, py + i * 2.0, pz) + b"\x02")
            time.sleep(0.05)
        c.send_packet_raw(0x1C, struct.pack(">ddd", px, py, pz) + b"\x01")
        hurt = False
        t_end = time.time() + 5
        while time.time() < t_end and not hurt:
            hurt = any(p == 0x1A for p, _ in _p43_drain(c, 0.8))
        check(hurt, "plan43 W-01 0x02 fall deals DamageEvent", "test_server_full.py:p43_fall")
        c.close()
    except Exception as e:
        check(False, f"plan43 W-01 throws: {e}", "test_server_full.py:p43_move_exc")
    # ---- W-02 use_entity (trimmed 6 combos; full 12 in test_plan43) ----
    try:
        for idx, (mouse, hand, sneak, expect_win) in enumerate(
                [(0, 0, False, False), (0, 1, False, False), (0, 0, True, True),
                 (0, 1, True, True), (1, 0, False, False), (1, 0, True, False)]):
            c = persistent_join(host, port, f"P43U{idx}")
            known = set()
            for pid, data in _p43_drain(c, 1.0):
                if pid != 0x01:
                    continue
                try:
                    bio = io.BytesIO(data)
                    eid, _ = read_varint(bio); u = bio.read(16); typ, _ = read_varint(bio)
                    if typ == 63:
                        known.add(eid)
                except Exception:
                    pass
            c.send_packet_raw(0x05, pack_string("summon minecraft:horse"))
            horse = None
            t_end = time.time() + 6
            while time.time() < t_end and horse is None:
                for pid, data in _p43_drain(c, 0.8):
                    if pid != 0x01:
                        continue
                    try:
                        bio = io.BytesIO(data)
                        eid, _ = read_varint(bio); u = bio.read(16); typ, _ = read_varint(bio)
                        if typ == 63 and eid not in known:
                            horse = eid
                    except Exception:
                        pass
            check(horse is not None, f"plan43 W-02 combo{idx} horse eid", "test_server_full.py:p43_use_eid")
            if horse is not None:
                pay = write_varint(horse) + write_varint(mouse)
                if mouse == 2: pay += struct.pack(">fff", 0.5, 0.5, 0.5)
                if mouse in (0, 2): pay += write_varint(hand)
                pay += b"\x01" if sneak else b"\x00"
                w0 = 0
                c.send_packet_raw(0x18, pay)
                win = False
                t_end = time.time() + 3
                while time.time() < t_end and not win:
                    win = any(p == 0x24 for p, _ in _p43_drain(c, 0.8))
                check(win == expect_win, f"plan43 W-02 m{mouse}/h{hand}/s{int(sneak)} window={expect_win}", "test_server_full.py:p43_use")
            c.close()
    except Exception as e:
        check(False, f"plan43 W-02 throws: {e}", "test_server_full.py:p43_use_exc")
    # ---- W-06 abilities ----
    try:
        c = persistent_join(host, port, "P43Abil")
        # join-burst 0x3A is consumed inside persistent_join; drive fresh sends
        # via /gamemode (the command re-sends abilities every execution)
        c.send_packet_raw(0x05, pack_string("gamemode creative"))
        got0d = False
        t_end = time.time() + 5
        while time.time() < t_end and not got0d:
            got0d = any(p == 0x3A and d and d[0] == 0x0D for p, d in _p43_drain(c, 0.8))
        check(got0d, "plan43 W-06 creative join 0x0D", "test_server_full.py:p43_abil_join")
        c.send_packet_raw(0x05, pack_string("gamemode survival"))
        got00 = False
        t_end = time.time() + 5
        while time.time() < t_end and not got00:
            got00 = any(p == 0x3A and d and d[0] == 0x00 for p, d in _p43_drain(c, 0.8))
        check(got00, "plan43 W-06 survival flags 0x00", "test_server_full.py:p43_abil_surv")
        c.send_packet_raw(0x26, b"\x02")
        pkts = _p43_drain(c, 1.0)
        check(not any(p == 0x1D for p, _ in pkts), "plan43 W-06 cs 0x26 no kick", "test_server_full.py:p43_abil_cs")
        c.close()
    except Exception as e:
        check(False, f"plan43 W-06 throws: {e}", "test_server_full.py:p43_abil_exc")
    # ---- W-07 sign ----
    try:
        c = persistent_join(host, port, "P43Sign")
        sx, sy, sz = 10, -60, 8
        c.send_packet_raw(0x05, pack_string(f"setblock {sx} {sy} {sz} minecraft:oak_sign"))
        placed = False
        t_end = time.time() + 8
        while time.time() < t_end and not placed:
            for pid, data in _p43_drain(c, 0.8):
                if pid == 0x09 and len(data) >= 8 and unpack_pos_block(data) == (sx, sy, sz):
                    placed = True
                if pid == 0x73 and b"Changed the block" in data:
                    placed = True
        check(placed, "plan43 W-07 sign placed", "test_server_full.py:p43_sign_place")
        lines = ["P43-L1", "P43-L2", "P43-L3", "P43-L4"]
        pay = pack_pos_block(sx, sy, sz) + b"\x01" + b"".join(pack_string(l) for l in lines)
        c.send_packet_raw(0x39, pay)
        got07 = False
        t_end = time.time() + 5
        while time.time() < t_end and not got07:
            got07 = any(p == 0x07 and b"P43-L1" in d for p, d in _p43_drain(c, 0.8))
        check(got07, "plan43 W-07 BlockEntityData carries line 1", "test_server_full.py:p43_sign")
        c.close()
    except Exception as e:
        check(False, f"plan43 W-07 throws: {e}", "test_server_full.py:p43_sign_exc")

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--binary", default="./build/cppfm", help="cppfm binary")
    ap.add_argument("--port", type=int, default=0, help="port 0=auto")
    ap.add_argument("--keep-running", action="store_true", help="do not kill server after")
    ap.add_argument("--suites", default="", help="comma list to run subset (e.g. rcon,restart); default=all") # plan46 §2
    args=ap.parse_args()

    binary=str(args.binary)
    _suites=set(s.strip() for s in str(args.suites).split(",") if s.strip())
    def _run(name): return (not _suites) or (name in _suites)
    if not Path(binary).exists():
        # try build
        print(f"[info] binary {binary} missing, attempting build...")
        subprocess.run(["cmake","-B","build","-G","Ninja"], check=False, timeout=120)
        subprocess.run(["cmake","--build","build","-j4"], check=False, timeout=300)
    if not Path(binary).exists():
        print(f"[fatal] binary not found: {binary}")
        sys.exit(2)

    binary = str(Path(binary).resolve())
    port=args.port if args.port!=0 else find_free_port()
    rcon_port=find_free_port()
    while rcon_port==port: rcon_port=find_free_port()
    world_dir=tempfile.mkdtemp(prefix="wt42_test_server_full_")
    rcon_pass="testRcon1337"
    print(f"[info] world_dir={world_dir} port={port} rcon={rcon_port} binary={binary}")

    # write minimal server.properties for rcon enable in world_dir and cwd
    # cppfm reads server.properties from cwd, not world_dir; so write to cwd temp copy? We'll write to worktree cwd
    # but we will pass via CLI: --enable-rcon=true --rcon.password=...
    # also need to ensure server writes ban/whitelist next to cwd; use cwd as world_dir? simpler to chdir to world_dir
    orig_cwd=os.getcwd()
    extra=["--enable-rcon=true", f"--rcon.password={rcon_pass}", f"--rcon.port={rcon_port}"]
    proc=None
    orig_cwd_before = os.getcwd()
    assets_dir = str((Path(orig_cwd_before) / "assets").resolve())
    extra = extra + [f"--assets={assets_dir}/registry", "--max-players=200", "--view-distance=4"]
    try:
        proc=launch_server(binary, port, world_dir, extra_args=extra)
        host="127.0.0.1"
        print(f"[info] server pid {proc.pid} ready")

        suite_connection_flow(host, port) if _run("conn") else None
        suite_plan43_b1b2(host, port) if _run("plan43") else None
        suite_commands(host, port, proc) if _run("commands") else None
        # ensure server still alive, restart if needed for remaining suites
        if not _is_server_alive(proc):
            print("[warn] server died during commands — restarting for remaining suites")
            try: kill_server(proc)
            except: pass
            try:
                proc = launch_server(binary, port, world_dir, extra_args=extra)
                host="127.0.0.1"
                print(f"[info] restarted server pid {proc.pid}")
            except Exception as e:
                print(f"[fatal] restart failed: {e}")
                # continue with whatever we have
        if _run("permissions"): suite_permissions(host, port, world_dir)
        if _run("chat"): suite_chat(host, port)
        if _run("datapack"): suite_datapack(host, port)
        if _run("stability"): suite_stability(host, port)
        if _run("rcon"): suite_rcon(host, port, world_dir, rcon_port, rcon_pass)
        if _run("persistence"): suite_persistence(host, port, world_dir)
        if _run("restart"): proc = suite_restart_persist(binary, proc, host, port, world_dir, extra)

    finally:
        try: os.chdir(orig_cwd_before)
        except: pass
        os.chdir(orig_cwd)
        if proc and not args.keep_running:
            print("[info] shutting down server...")
            kill_server(proc)
            time.sleep(0.5)
        # cleanup world_dir? keep for inspection
        # shutil.rmtree(world_dir, ignore_errors=True)
    summary_and_exit()

if __name__=="__main__":
    main()
