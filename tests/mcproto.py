"""Minimal Minecraft Java Edition protocol client library (clean-room).

Implements just enough of the protocol (version 769 / 1.21.4) to:
 - query status/ping
 - complete offline-mode login
 - walk through the configuration phase
 - enter play phase and observe/reply basic packets

All numeric layouts verified against PrismarineJS minecraft-data 1.21.4.
"""
from __future__ import annotations
import json
import hashlib
import io
import socket
import struct
import time
import zlib

PROTOCOL = 769


# ---------------------------------------------------------------- primitives
def write_varint(value: int) -> bytes:
    out = bytearray()
    v = value & 0xFFFFFFFF
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def read_varint(data: io.BytesIO | bytes, offset: int = 0) -> tuple[int, int]:
    """returns (value, new_offset); raises ValueError on overflow"""
    if isinstance(data, (bytes, bytearray)):
        bio = io.BytesIO(data)
    else:
        bio = data
    result = 0
    shift = 0
    while True:
        byte = bio.read(1)
        if not byte:
            raise ValueError("varint truncated")
        offset += 1
        result |= (byte[0] & 0x7F) << shift
        if not (byte[0] & 0x80):
            # sign extend 32-bit
            if result & (1 << 31):
                result -= 1 << 32
            return result, offset
        shift += 7
        if shift >= 35:
            raise ValueError("varint too big")


def pack_string(s: str) -> bytes:
    b = s.encode("utf-8")
    return write_varint(len(b)) + b


def unpack_string(bio: io.BytesIO) -> str:
    n, _ = read_varint(bio)
    return bio.read(n).decode("utf-8")


def offline_uuid(name: str) -> str:
    h = hashlib.md5(("OfflinePlayer:" + name).encode()).digest()
    b = bytearray(h)
    b[6] = (b[6] & 0x0F) | 0x30
    b[8] = (b[8] & 0x3F) | 0x80
    u = int.from_bytes(bytes(b), "big")
    return f"{u:032x}"


def _sign(v: int, bits: int) -> int:
    return v - (1 << bits) if v >= (1 << (bits - 1)) else v

def unpack_position(v: int) -> tuple[int, int, int]:
    x = _sign(v >> 38, 26)
    y = _sign(v & 0xFFF, 12)
    z = _sign((v >> 12) & 0x3FFFFFF, 26)
    return x, y, z


# ---------------------------------------------------------------- connection
class Conn:
    def __init__(self, host="127.0.0.1", port=25565, timeout=15):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.compression_threshold = -1
        self.rxbuf = b""

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    # -- low level -----------------------------------------------------
    def _recv_exact(self, n: int) -> bytes:
        while len(self.rxbuf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise EOFError("socket closed")
            self.rxbuf += chunk
        out, self.rxbuf = self.rxbuf[:n], self.rxbuf[n:]
        return out

    def recv_frame(self) -> tuple[int, bytes]:
        """returns (packet_id, payload) fully decompressed"""
        ln, p = read_varint(self._recv_exact(5), 0)
        # careful: read_varint above may have consumed from a fresh buffer;
        # implement manual incremental varint instead
        raise RuntimeError("unused")

    def recv_packet(self) -> tuple[int, bytes]:
        # read frame length (incremental varint across recv boundary)
        length = self._read_varint_stream()
        data = self._recv_exact(length)
        if self.compression_threshold >= 0:
            bio = io.BytesIO(data)
            dlen, off = read_varint(data, 0)
            body = data[off:]
            if dlen == 0:
                pass
            else:
                body = zlib.decompress(body)
                assert len(body) == dlen, "bad uncompressed size"
            data = body
        pid, off = read_varint(data, 0)
        return pid, data[off:]

    def _read_varint_stream(self) -> int:
        result = 0
        shift = 0
        while True:
            b = self._recv_exact(1)[0]
            result |= (b & 0x7F) << shift
            if not (b & 0x80):
                return result
            shift += 7

    def send_packet_raw(self, pid: int, payload: bytes):
        body = write_varint(pid) + payload
        if self.compression_threshold >= 0:
            if len(body) >= self.compression_threshold:
                frame = write_varint(len(body)) + body
                pkt = write_varint(len(frame)) + frame
            else:
                frame = write_varint(0) + body
                pkt = write_varint(len(frame)) + frame
        else:
            pkt = write_varint(len(body)) + body
        self.sock.sendall(pkt)

    # -- handshake/status ----------------------------------------------
    def handshake(self, next_state: int):
        p = (write_varint(PROTOCOL) + pack_string("127.0.0.1")
             + struct.pack(">H", 25565) + write_varint(next_state))
        self.send_packet_raw(0x00, p)

    def status(self) -> dict:
        self.handshake(1)
        self.send_packet_raw(0x00, b"")           # request
        pid, data = self.recv_packet()             # response
        assert pid == 0x00
        bio = io.BytesIO(data)
        js = json.loads(unpack_string(bio))
        # ping
        t = int(time.time() * 1000) & 0xFFFFFFFFFFFFFFFF
        self.send_packet_raw(0x01, struct.pack(">q", t))
        pid, data = self.recv_packet()
        assert pid == 0x01 and struct.unpack(">q", data)[0] == t
        return js

    def login(self, name: str) -> None:
        """offline login through configuration ack. returns after entering PLAY."""
        self.handshake(2)
        uuid = offline_uuid(name)
        self.send_packet_raw(0x00, pack_string(name) + uuid.encode().hex().encode()
                             if False else pack_string(name) + bytes.fromhex(uuid))
        while True:
            pid, data = self.recv_packet()
            if pid == 0x03:      # set compression
                bio = io.BytesIO(data)
                th, _ = read_varint(bio)
                self.compression_threshold = th
            elif pid == 0x02:    # success
                break
            elif pid == 0x00:    # disconnect
                bio = io.BytesIO(data)
                raise RuntimeError("kicked at login: " + repr(bio.read()))
            else:
                raise RuntimeError(f"unexpected login packet 0x{pid:02x}: {data.hex()}")
        self.send_packet_raw(0x03, b"")   # login acknowledged

    # -- configuration ---------------------------------------------------
    def config_finish(self, sink=None, max_seconds=30):
        """consume config packets until Finish Configuration, replying as needed.
        sink(pid, payload) is called for every packet received."""
        deadline = time.time() + max_seconds
        while time.time() < deadline:
            pid, data = self.recv_packet()
            if sink:
                sink(pid, data)
            if pid == 0x03:      # finish configuration
                self.send_packet_raw(0x03, b"")
                return
            elif pid == 0x04:    # keep alive
                self.send_packet_raw(0x04, data)
            elif pid == 0x05:    # ping
                self.send_packet_raw(0x05, data)
            elif pid == 0x0e:    # select_known_packs -> claim none
                self.send_packet_raw(0x07, b"\x00")
            elif pid == 0x02:    # disconnect
                raise RuntimeError("kicked at config: " + repr(data[:400]))
        raise TimeoutError("configuration never finished")


def parse_login_success(data: bytes):
    bio = io.BytesIO(data)
    u = bio.read(16).hex()
    name = unpack_string(bio)
    nprops, _ = read_varint(bio)
    props = []
    for _ in range(nprops):
        pn = unpack_string(bio)
        pv = unpack_string(bio)
        has_sig = bio.read(1)[0]
        sig = unpack_string(bio) if has_sig else None
        props.append((pn, pv, sig))
    return {"uuid": u, "name": name, "props": props}
