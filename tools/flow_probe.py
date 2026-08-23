"""Observe raw play-phase packet sequence to understand spawn flow."""
import io, os, struct, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import mcproto
from mcproto import Conn, read_varint

def run(name="FlowBot", secs=30):
    c = Conn()
    c.handshake(2)
    uuid = mcproto.offline_uuid(name)
    c.send_packet_raw(0x00, mcproto.pack_string(name) + bytes.fromhex(uuid))
    while True:
        pid, data = c.recv_packet()
        if pid == 0x03:
            th, _ = read_varint(io.BytesIO(data)); c.compression_threshold = th
        elif pid == 0x02:
            break
    c.send_packet_raw(0x03, b"")
    # config passthrough
    while True:
        pid, data = c.recv_packet()
        if pid == 0x03:
            c.send_packet_raw(0x03, b""); break
        elif pid == 0x04: c.send_packet_raw(0x04, data)
        elif pid == 0x05: c.send_packet_raw(0x05, data)
        elif pid == 0x0e: c.send_packet_raw(0x07, b"\x00")

    print("-- PLAY phase --")
    t_end = time.time() + secs
    last_move = 0
    sent_loaded = False
    counts = {}
    seq = []
    while time.time() < t_end:
        try:
            pid, data = c.recv_packet()
        except EOFError:
            print("EOF"); break
        except OSError as e:
            print("ERR", e); break
        counts[pid] = counts.get(pid, 0) + 1
        if len(seq) < 40 or counts.get(pid,0) <= 2:
            seq.append(f"0x{pid:02x}({len(data)}b)")
        if pid == 0x27: c.send_packet_raw(0x1a, data)
        elif pid == 0x42:
            bio = io.BytesIO(data); tid,_ = read_varint(bio)
            c.send_packet_raw(0x00, mcproto.write_varint(tid))
            seq.append("CONFIRMED_TELEPORT")
        elif pid == 0x0c: c.send_packet_raw(0x09, struct.pack(">f", 8.0))
        elif pid == 0x2c and not sent_loaded:
            pass  # hold player_loaded this time
        if time.time() - last_move > 0.5:
            last_move = time.time()
            c.send_packet_raw(0x1c, struct.pack(">ddd", 8.5, -60.0, 8.5) + b"\x01")
    print("first packets:", " ".join(seq[:60]))
    print("counts:", {f"0x{k:02x}": v for k,v in sorted(counts.items())})
    c.close()

if __name__ == "__main__":
    run(secs=int(sys.argv[1]) if len(sys.argv)>1 else 30)
