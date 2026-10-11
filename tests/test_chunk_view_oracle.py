#!/usr/bin/env python3
"""Unit checks for the live 1.21.4 chunk-view packet oracle."""

from __future__ import annotations

import struct
import unittest
from collections.abc import Iterable

from mcproto import write_varint
from test_goal_live_interactions import (
    PLAY_LEVEL_CHUNK_WITH_LIGHT,
    PLAY_SET_CENTER_CHUNK,
    wait_for_chunk_view,
)


class FakeClient:
    def __init__(self, packets: Iterable[tuple[int, bytes]]) -> None:
        self.packet_payloads = list(packets)
        self.disconnected = False

    def pump(self, _seconds: float) -> list[tuple[int, bytes]]:
        return []


def view_packets(center: tuple[int, int],
                 chunks: Iterable[tuple[int, int]]) -> list[tuple[int, bytes]]:
    packets = [(
        PLAY_SET_CENTER_CHUNK,
        write_varint(center[0]) + write_varint(center[1]),
    )]
    packets.extend((PLAY_LEVEL_CHUNK_WITH_LIGHT, struct.pack(">ii", x, z))
                   for x, z in chunks)
    return packets


class ChunkViewOracleTests(unittest.TestCase):
    def setUp(self) -> None:
        self.center = (3, -2)
        self.expected = {
            (self.center[0] + dx, self.center[1] + dz)
            for dx in range(-3, 4)
            for dz in range(-3, 4)
        }

    def test_accepts_exact_centered_seven_by_seven_view(self) -> None:
        client = FakeClient(view_packets(self.center, sorted(self.expected)))
        result = wait_for_chunk_view(
            client, 0, self.center, self.center, 49, "fixture",
            timeout_seconds=0.1, settle_seconds=0.0)
        self.assertEqual(result["center"], self.center)
        self.assertEqual(result["unique_chunks"], 49)

    def test_rejects_wrong_last_center_even_if_expected_center_was_seen(self) -> None:
        packets = view_packets(self.center, sorted(self.expected))
        packets.append((PLAY_SET_CENTER_CHUNK,
                        write_varint(self.center[0] + 1) +
                        write_varint(self.center[1])))
        with self.assertRaisesRegex(AssertionError, "did not publish"):
            wait_for_chunk_view(
                FakeClient(packets), 0, self.center, self.center, 49,
                "wrong center", timeout_seconds=0.01, settle_seconds=0.0)

    def test_rejects_wrong_chunk_set_with_the_right_count(self) -> None:
        chunks = set(self.expected)
        chunks.remove((self.center[0] + 3, self.center[1] + 3))
        chunks.add((self.center[0] + 4, self.center[1] + 3))
        with self.assertRaisesRegex(AssertionError, "non-vanilla chunk view"):
            wait_for_chunk_view(
                FakeClient(view_packets(self.center, sorted(chunks))), 0,
                self.center, self.center, 49, "wrong set",
                timeout_seconds=0.1, settle_seconds=0.0)


if __name__ == "__main__":
    unittest.main()
