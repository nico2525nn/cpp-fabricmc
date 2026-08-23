"""Capture ground-truth protocol data from the reference Fabric server."""
import io
import json
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import mcproto
from mcproto import Conn, read_varint, unpack_position, parse_login_success

OUT = os.environ.get("CPPFM_CAPTURES", "./captures")
os.makedirs(OUT, exist_ok=True)
saved = set()


def save(name: str, data: bytes):
    if name in saved:
        return
    saved.add(name)
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(data)
    print(f"  saved {name} ({len(data)} bytes)")


def capture_status():
    print("== status ==")
    c = Conn()
    js = c.status()
    c.close()
    with open(os.path.join(OUT, "status.json"), "w") as f:
        json.dump(js, f, ensure_ascii=False, indent=1)
    print("  version:", js.get("version"))


CFG_NAMES = {0x01: "cfg_custom_payload", 0x02: "cfg_disconnect",
             0x03: "cfg_finish", 0x04: "cfg_keep_alive", 0x05: "cfg_ping",
             0x06: "cfg_reset_chat", 0x07: "cfg_registry_data",
             0x08: "cfg_remove_resource_pack", 0x09: "cfg_add_resource_pack",
             0x0a: "cfg_store_cookie", 0x0b: "cfg_transfer",
             0x0c: "cfg_feature_flags", 0x0d: "cfg_tags",
             0x0e: "cfg_select_known_packs", 0x0f: "cfg_custom_report_details",
             0x10: "cfg_server_links"}


def capture_config_and_play():
    print("== login / configuration / play ==")

    class CapConn(Conn):
        def recv_packet(self):
            pid, data = super().recv_packet()
            self.log.append((pid, data))
            return pid, data

    c = CapConn()
    c.log = []
    # manual login so we can capture login_success bytes
    c.handshake(2)
    uuid = mcproto.offline_uuid("RefBot")
    c.send_packet_raw(0x00, mcproto.pack_string("RefBot") + bytes.fromhex(uuid))
    while True:
        pid, data = c.recv_packet()
        if pid == 0x03:
            th, _ = read_varint(io.BytesIO(data))
            c.compression_threshold = th
        elif pid == 0x02:
            save("login_success.bin", data)
            info = parse_login_success(data)
            print("  login success:", info["name"], info["uuid"][:8])
            break
        elif pid == 0x00:
            raise RuntimeError("kicked at login")
        elif pid == 0x04:
            raise RuntimeError("login plugin request? " + repr(data[:64]))
    c.send_packet_raw(0x03, b"")   # login acknowledged -> CONFIGURATION

    registries = {}
    t_end = time.time() + 30
    while time.time() < t_end:
        pid, data = c.recv_packet()
        if pid == 0x03:
            c.send_packet_raw(0x03, b"")
            print("  -> sent finish_configuration ack; entering PLAY")
            break
        elif pid == 0x04:
            c.send_packet_raw(0x04, data)
        elif pid == 0x05:
            c.send_packet_raw(0x05, data)
        elif pid == 0x0e:               # select_known_packs -> reply "I know nothing"
            c.send_packet_raw(0x07, b"\x00")
        nm = CFG_NAMES.get(pid, f"cfg_unk_{pid:02x}")
        if pid == 0x07:
            bio = io.BytesIO(data)
            key = mcproto.unpack_string(bio)
            n, _ = read_varint(bio)
            registries[key] = (n, data)
            save(f"registry_{key.replace(':', '__').replace('/', '_')}.bin", data)
        elif pid in (0x01, 0x02, 0x06, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e):
            save(nm + ".bin", data)

    for k, (n, _) in sorted(registries.items()):
        print(f"  registry {k}: {n} entries")

    # ---------------- PLAY ----------------
    join_seen = False
    n_chunks = 0
    chat_saved = 0
    pos_x, pos_y, pos_z = 8.5, -60.0, 8.5
    teleports = 0
    t_end = time.time() + 30
    last_move = 0.0
    other_files = {"0x40": "play_player_info", "0x11": "play_declare_commands",
                   "0x58": "play_update_view_position", "0x69": "play_simulation_distance",
                   "0x3a": "play_abilities", "0x62": "play_update_health",
                   "0x5b": "play_spawn_position", "0x26": "play_initialize_border"}
    while time.time() < t_end:
        try:
            pid, data = c.recv_packet()
        except EOFError:
            break
        if pid == 0x27:                                    # keep alive
            c.send_packet_raw(0x1a, data)
        elif pid == 0x42:                                  # position/teleport
            bio = io.BytesIO(data)
            tid, _ = read_varint(bio)
            c.send_packet_raw(0x00, mcproto.write_varint(tid))
            teleports += 1
        elif pid == 0x2c and not join_seen:
            join_seen = True
            save("play_join_game.bin", data)
            c.send_packet_raw(0x2a, b"")                   # player_loaded
        elif pid == 0x28:                                  # map_chunk
            if n_chunks < 8:
                save(f"play_chunk_{n_chunks}.bin", data)
            n_chunks += 1
        elif pid == 0x0c:                                  # chunk_batch_finished
            # reply with an arbitrary throughput estimate
            c.send_packet_raw(0x09, struct.pack(">f", 8.0))
        elif pid == 0x73 and chat_saved < 3:               # system chat samples
            save(f"play_system_chat_{chat_saved}.bin", data)
            chat_saved += 1
        else:
            k = other_files.get(f"0x{pid:02x}")
            if k:
                save(k + ".bin", data)
        if time.time() - last_move > 0.4:
            last_move = time.time()
            flags = b"\x01"
            body = struct.pack(">ddd", pos_x, pos_y, pos_z) + flags
            c.send_packet_raw(0x1c if teleports % 2 == 0 else 0x1f, body)
    print(f"  join seen: {join_seen}, chunks received: {n_chunks}, teleports confirmed: {teleports}")
    c.close()
    return n_chunks


if __name__ == "__main__":
    capture_status()
    capture_config_and_play()
