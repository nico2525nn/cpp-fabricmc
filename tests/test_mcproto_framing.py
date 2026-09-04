#!/usr/bin/env python3
"""Focused framing and Play packet-pump regression tests for plan49."""
from __future__ import annotations

import os
import socket
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))

import mcproto
from soak_bot import PlayPacketPump


TIMEOUT = object()


class ScriptedSocket:
    def __init__(self, events):
        self.events = list(events)
        self.sent = []

    def recv(self, _size):
        if not self.events:
            raise AssertionError("scripted socket unexpectedly exhausted")
        event = self.events.pop(0)
        if event is TIMEOUT:
            raise socket.timeout("scripted timeout")
        return event

    def sendall(self, data):
        self.sent.append(bytes(data))

    def close(self):
        pass


def make_conn(sock) -> mcproto.Conn:
    conn = mcproto.Conn.__new__(mcproto.Conn)
    conn.sock = sock
    conn.compression_threshold = -1
    conn.rxbuf = b""
    return conn


def frame(pid: int, payload: bytes = b"") -> bytes:
    body = mcproto.write_varint(pid) + payload
    return mcproto.write_varint(len(body)) + body


def test_partial_varint_and_frame_survive_timeout():
    # 128-byte body forces a two-byte frame-length VarInt.  Timeouts happen
    # once in the length prefix and once in the body.
    first_payload = bytes(range(127))
    stream = frame(mcproto.PLAY_CLIENTBOUND_LEVEL_CHUNK_WITH_LIGHT, first_payload)
    stream += frame(mcproto.PLAY_CLIENTBOUND_KEEP_ALIVE, struct.pack(">q", 41))
    sock = ScriptedSocket([stream[:1], TIMEOUT, stream[1:2], stream[2:9], TIMEOUT, stream[9:]])
    conn = make_conn(sock)

    try:
        conn.recv_packet()
    except socket.timeout:
        pass
    else:
        raise AssertionError("partial length timeout was not surfaced")
    assert conn.rxbuf == stream[:1]

    try:
        conn.recv_packet()
    except socket.timeout:
        pass
    else:
        raise AssertionError("partial body timeout was not surfaced")

    pid, payload = conn.recv_packet()
    assert pid == mcproto.PLAY_CLIENTBOUND_LEVEL_CHUNK_WITH_LIGHT
    assert payload == first_payload
    pid, payload = conn.recv_packet(wait=False)
    assert pid == mcproto.PLAY_CLIENTBOUND_KEEP_ALIVE
    assert payload == struct.pack(">q", 41)


def test_timeout_and_eof_are_distinct():
    conn = make_conn(ScriptedSocket([TIMEOUT]))
    try:
        conn.recv_packet()
    except socket.timeout:
        pass
    else:
        raise AssertionError("idle timeout was not surfaced")
    assert conn.rxbuf == b""

    conn = make_conn(ScriptedSocket([mcproto.write_varint(2), b"\x01", b""]))
    try:
        conn.recv_packet()
    except EOFError:
        pass
    else:
        raise AssertionError("EOF was not surfaced")


def test_pump_echoes_keepalive_and_classifies_disconnect():
    client, server = socket.socketpair()
    try:
        client.settimeout(0.2)
        server.settimeout(0.2)
        conn = make_conn(client)
        pump = PlayPacketPump(conn, max_packets=8, idle_timeout=0.2)
        keepalive_id = 123456789
        server.sendall(
            frame(mcproto.PLAY_CLIENTBOUND_KEEP_ALIVE, struct.pack(">q", keepalive_id))
            + frame(mcproto.PLAY_CLIENTBOUND_DISCONNECT, mcproto.pack_string("bye"))
        )

        seen = []
        result = pump.pump(on_packet=lambda pid, _data: seen.append(pid))
        assert result.status == "disconnect"
        assert pump.keepalives == 1
        assert pump.keepalive_echoes == 1
        assert pump.keepalive_ids == [keepalive_id]
        assert pump.disconnects == 1
        assert pump.disconnect_reason == "bye"
        assert pump.eof_count == 0
        assert seen == [mcproto.PLAY_CLIENTBOUND_KEEP_ALIVE, mcproto.PLAY_CLIENTBOUND_DISCONNECT]

        reply = make_conn(server).recv_packet()
        assert reply == (mcproto.PLAY_SERVERBOUND_KEEP_ALIVE, struct.pack(">q", keepalive_id))
    finally:
        client.close()
        server.close()


def test_pump_budget_is_bounded():
    client, server = socket.socketpair()
    try:
        client.settimeout(0.2)
        server.settimeout(0.2)
        conn = make_conn(client)
        pump = PlayPacketPump(conn, max_packets=2, idle_timeout=0.05)
        server.sendall(b"".join(frame(0x01) for _ in range(5)))

        first = pump.pump()
        second = pump.pump()
        third = pump.pump()
        assert first == type(first)("drain_limit", 2)
        assert second == type(second)("drain_limit", 2)
        assert third == type(third)("timeout", 1)
        assert pump.drain_limit_hits == 2
    finally:
        client.close()
        server.close()


class FramingTests(unittest.TestCase):
    def test_partial_varint_and_frame_survive_timeout(self):
        test_partial_varint_and_frame_survive_timeout()

    def test_timeout_and_eof_are_distinct(self):
        test_timeout_and_eof_are_distinct()

    def test_pump_echoes_keepalive_and_classifies_disconnect(self):
        test_pump_echoes_keepalive_and_classifies_disconnect()

    def test_pump_budget_is_bounded(self):
        test_pump_budget_is_bounded()


if __name__ == "__main__":
    test_partial_varint_and_frame_survive_timeout()
    test_timeout_and_eof_are_distinct()
    test_pump_echoes_keepalive_and_classifies_disconnect()
    test_pump_budget_is_bounded()
    print("framing/pump tests: PASS")
