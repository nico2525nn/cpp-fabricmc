"""Stress: many concurrent sessions; verify server stays healthy."""
import io, os, struct, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mcproto
from mcproto import Conn, read_varint

HOST = os.environ.get("CPPFM_HOST", "127.0.0.1")
PORT = int(os.environ.get("CPPFM_PORT", "25577"))
N = int(os.environ.get("CPPFM_N", "16"))

results = []
lock = threading.Lock()

STAGE = {}
def bot(i):
    ok = {"join": False, "chunks": 0}
    try:
        STAGE[i] = "connect"
        c = Conn(HOST, PORT, timeout=float(os.environ.get("CPPFM_TMO", "30")))
        STAGE[i] = "login"
        c.login(f"Bot{i:02d}")
        STAGE[i] = "config"
        c.config_finish(sink=lambda p, d: None, max_seconds=10)
        STAGE[i] = "play"
        deadline = time.time() + 12
        sent_chat = False
        while time.time() < deadline:
            pid, data = c.recv_packet()
            if pid == 0x2c:
                ok["join"] = True
                c.send_packet_raw(0x2a, b"")
                # report position like real clients do -> server marks us active
                c.send_packet_raw(0x1c, struct.pack(">ddd", 8.5, -60.0, 8.5) + b"\x01")
            elif pid == 0x42:
                bio = io.BytesIO(data); tid, _ = read_varint(bio)
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
            if not sent_chat and time.time() > deadline - 6 and i % 4 == 0:
                # keep moving so chunks flow
                try:
                    c.send_packet_raw(0x1c, struct.pack(">ddd", 8.5, -60.0, 8.5) + b"\x01")
                except OSError:
                    break
        c.close()
    except Exception as e:
        with lock:
            results.append((i, False, f"stage={STAGE.get(i)} {e!r}"))
        return
    with lock:
        results.append((i, ok["join"] and ok["chunks"] > 0, f"chunks={ok['chunks']}"))

threads = [threading.Thread(target=bot, args=(i,)) for i in range(N)]
t0 = time.time()
for i, t in enumerate(threads):
    t.start()
    time.sleep(0.05)   # gentle stagger; still a heavy burst
for t in threads: t.join()
dt = time.time() - t0

passed = sum(1 for _, ok, _ in results if ok)
print(f"{passed}/{N} bots fully joined within {dt:.1f}s")
for i, ok, info in sorted(results):
    if not ok:
        print(f"  FAIL bot{i}: {info}")

# server still alive?
c = Conn(HOST, PORT, timeout=float(os.environ.get("CPPFM_TMO", "30")))
js = c.status()
c.close()
print("status after stress:", js.get("players", {}).get("online"), "online reported")
sys.exit(0 if passed == N else 1)
