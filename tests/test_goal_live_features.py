#!/usr/bin/env python3
"""Exercise shipped gameplay/settings through a real protocol session.

The runner deliberately starts ``cppfm`` and drives its command and play
packet entrypoints.  It does not call native helpers or substitute a fake
server response.  A small command transcript is retained in the JSON result
so the feature ledger can name the exact observation behind each row.
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import socket
import struct
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))

from mcproto import Conn, pack_string, read_varint, write_varint  # noqa: E402
from test_goal_live_matrix import OwnedServer, free_port, wait_status  # noqa: E402


PLAY_KEEP_ALIVE = 0x27
PLAY_KEEP_ALIVE_RESPONSE = 0x1A
PLAY_DISCONNECT = 0x1D
PLAY_SYSTEM_CHAT = 0x73
PLAY_SPAWN_ENTITY = 0x01
PLAY_ACK_BLOCK_CHANGE = 0x05
PLAY_SET_CONTENT = 0x13
PLAY_TEAMS = 0x67
PLAY_DECLARE_COMMANDS = 0x11
PLAY_BLOCK_UPDATE = 0x09
PLAY_SECTION_BLOCKS_UPDATE = 0x4E
PLAY_SET_DEFAULT_SPAWN = 0x5B
PLAY_CHANGE_DIFFICULTY = 0x0B
PLAY_GAME_EVENT = 0x23
PLAY_UPDATE_TIME = 0x6B
PLAY_BOSSBAR = 0x0A
PLAY_SET_HEALTH = 0x62
PLAY_SET_EXPERIENCE = 0x61
PLAY_ENTITY_EFFECT = 0x7D
PLAY_SET_PASSENGERS = 0x65
PLAY_SET_ENTITY_METADATA = 0x5D
PLAY_MOVE_ENTITY_POS = 0x2F
PLAY_MOVE_ENTITY_POS_ROT = 0x30


class LiveClient:
    def __init__(self, port: int, name: str) -> None:
        self.conn = Conn("127.0.0.1", port, timeout=15)
        self.name = name
        self.packet_ids: list[int] = []
        self.packet_lengths: list[int] = []
        self.packet_payloads: list[tuple[int, bytes]] = []
        self.spawn_entity_ids: list[int] = []
        self.spawn_entity_records: list[tuple[int, int]] = []
        self.entity_id: int | None = None
        self.disconnected = False
        self.conn.login(name)
        self.conn.config_finish(max_seconds=30)

    def close(self) -> None:
        self.conn.close()

    def pump(self, seconds: float = 0.25, max_packets: int = 512) -> list[tuple[int, bytes]]:
        deadline = time.monotonic() + max(0.0, seconds)
        packets: list[tuple[int, bytes]] = []
        while len(packets) < max_packets and time.monotonic() < deadline:
            remaining = max(0.02, min(0.25, deadline - time.monotonic()))
            self.conn.sock.settimeout(remaining)
            try:
                packet_id, payload = self.conn.recv_packet()
            except socket.timeout:
                continue
            except EOFError:
                self.disconnected = True
                break
            self.packet_ids.append(packet_id)
            self.packet_lengths.append(len(payload))
            self.packet_payloads.append((packet_id, payload))
            packets.append((packet_id, payload))
            if packet_id == 0x2C and len(payload) >= 4:
                self.entity_id = struct.unpack(">i", payload[:4])[0]
            if packet_id == PLAY_SPAWN_ENTITY:
                entity_id, entity_offset = read_varint(payload, 0)
                self.spawn_entity_ids.append(entity_id)
                entity_type, _ = read_varint(payload[entity_offset + 16:], 0)
                self.spawn_entity_records.append((entity_id, entity_type))
            if packet_id == 0x42:
                teleport_id, _ = read_varint(payload, 0)
                self.conn.send_packet_raw(0x00, write_varint(teleport_id))
            if packet_id == PLAY_KEEP_ALIVE:
                if len(payload) != 8:
                    raise AssertionError("server keep-alive was not an 8-byte id")
                self.conn.send_packet_raw(PLAY_KEEP_ALIVE_RESPONSE, payload)
            elif packet_id == PLAY_DISCONNECT:
                self.disconnected = True
        return packets

    def command(self, text: str, expected: str | tuple[str, ...], delay: float = 0.8) -> dict[str, object]:
        # The server's spam guard is intentionally part of this real-path
        # test.  Keep commands below its threshold instead of disabling it.
        time.sleep(delay)
        self.pump(0.05)
        self.conn.send_packet_raw(0x05, pack_string(text))
        expected_values = (expected,) if isinstance(expected, str) else expected
        encoded = tuple(value.encode("utf-8") for value in expected_values)
        packets: list[dict[str, object]] = []
        deadline = time.monotonic() + 3.0
        matched = False
        while time.monotonic() < deadline:
            batch = self.pump(min(0.25, deadline - time.monotonic()))
            for packet_id, payload in batch:
                packets.append({"id": packet_id, "length": len(payload)})
                if any(token in payload for token in encoded):
                    matched = True
            if matched:
                break
            if self.disconnected:
                break
        if not matched:
            raise AssertionError(
                f"command {text!r} did not produce {expected_values!r}; "
                f"packets={[item['id'] for item in packets]}"
            )
        return {"command": text, "expected": list(expected_values), "packets": packets}

    def tab_complete(self, text: str) -> dict[str, object]:
        self.conn.send_packet_raw(0x0D, write_varint(1) + pack_string(text))
        deadline = time.monotonic() + 3.0
        packets: list[dict[str, object]] = []
        while time.monotonic() < deadline:
            for packet_id, payload in self.pump(min(0.25, deadline - time.monotonic())):
                packets.append({"id": packet_id, "length": len(payload)})
                if packet_id == 0x10:
                    if len(payload) < 5:
                        raise AssertionError("tab completion response was truncated")
                    return {"text": text, "packets": packets}
        raise AssertionError(f"tab completion did not return for {text!r}: {packets}")

    def use_entity(self, entity_id: int, mouse: int = 0) -> list[tuple[int, bytes]]:
        time.sleep(0.05)
        payload = write_varint(entity_id) + write_varint(mouse)
        if mouse == 2:
            payload += struct.pack(">fff", 0.0, 0.0, 0.0)
        payload += write_varint(0) + b"\x00"
        self.conn.send_packet_raw(0x18, payload)
        return self.pump(1.0)

    def set_held_slot(self, slot: int) -> list[tuple[int, bytes]]:
        if not 0 <= slot <= 8:
            raise ValueError(f"invalid hotbar slot: {slot}")
        self.conn.send_packet_raw(0x33, struct.pack(">h", slot))
        return self.pump(0.35)

    def move_player_pos_rot(self, x: float, y: float, z: float,
                            yaw: float = 0.0, pitch: float = 0.0) -> list[tuple[int, bytes]]:
        payload = struct.pack(">dddffB", x, y, z, yaw, pitch, 0x01)
        self.conn.send_packet_raw(0x1D, payload)
        return self.pump(0.5)

    def use_item(self, sequence: int, yaw: float = 0.0,
                 pitch: float = 0.0) -> list[tuple[int, bytes]]:
        payload = write_varint(0) + write_varint(sequence) + struct.pack(">ff", yaw, pitch)
        self.conn.send_packet_raw(0x3D, payload)
        return self.pump(1.5)

    def use_item_on(self, x: int, y: int, z: int, face: int,
                    sequence: int) -> list[tuple[int, bytes]]:
        packed = ((x & 0x3FFFFFF) << 38) | ((z & 0x3FFFFFF) << 12) | (y & 0xFFF)
        payload = (write_varint(0) + struct.pack(">q", packed) + write_varint(face) +
                   struct.pack(">fff", 0.5, 0.5, 0.5) + b"\x00\x00" +
                   write_varint(sequence))
        self.conn.send_packet_raw(0x3C, payload)
        return self.pump(1.0)

    def player_action(self, status: int, x: int, y: int, z: int,
                      face: int, sequence: int) -> list[tuple[int, bytes]]:
        packed = ((x & 0x3FFFFFF) << 38) | ((z & 0x3FFFFFF) << 12) | (y & 0xFFF)
        payload = (write_varint(status) + struct.pack(">q", packed) +
                   struct.pack(">b", face) + write_varint(sequence))
        self.conn.send_packet_raw(0x27, payload)
        return self.pump(0.25)

    def entity_action(self, action: int) -> list[tuple[int, bytes]]:
        if self.entity_id is None:
            raise AssertionError("play Login did not expose the player entity id")
        payload = write_varint(self.entity_id) + write_varint(action) + write_varint(0)
        self.conn.send_packet_raw(0x28, payload)
        return self.pump(0.5)

    def window_click(self, window_id: int, state_id: int, slot: int,
                     button: int = 0, mode: int = 0) -> list[tuple[int, bytes]]:
        payload = (write_varint(window_id) + write_varint(state_id) +
                   struct.pack(">h", slot) + struct.pack(">b", button) +
                   write_varint(mode) + write_varint(0) + write_varint(0))
        self.conn.send_packet_raw(0x10, payload)
        return self.pump(0.75)

    def close_container(self, window_id: int) -> list[tuple[int, bytes]]:
        self.conn.send_packet_raw(0x11, write_varint(window_id))
        return self.pump(0.4)

    def player_input(self, flags: int) -> list[tuple[int, bytes]]:
        self.conn.send_packet_raw(0x29, struct.pack(">ffB", 0.0, 0.0, flags & 0xFF))
        return self.pump(0.75)


def start_server(binary: Path, root: Path, world: Path) -> tuple[OwnedServer, int]:
    root.mkdir(parents=True, exist_ok=True)
    world.mkdir(parents=True, exist_ok=True)
    # loadOps() is intentionally relative to the server root, matching a
    # normal dedicated-server launch rather than a test-only API.
    (root / "ops.txt").write_text("GoalFeatureOp\n", encoding="utf-8")
    (world / "datapacks" / "goal" / "data" / "goal" / "function").mkdir(parents=True)
    (world / "datapacks" / "goal" / "pack.mcmeta").write_text(
        '{"pack":{"pack_format":61,"description":"goal live feature pack"}}\n',
        encoding="utf-8",
    )
    (world / "datapacks" / "goal" / "data" / "goal" / "function" / "entry.mcfunction").write_text(
        "say goal function executed\n",
        encoding="utf-8",
    )
    port = free_port()
    command = [
        str(binary),
        f"--port={port}",
        f"--world-dir={world}",
        "--level-type=flat",
        "--level-seed=goal-live-feature-seed",
        "--view-distance=3",
        "--simulation-distance=3",
        "--jvm=false",
        "--online-mode=false",
        "--enforce-secure-profile=false",
    ]
    server = OwnedServer(command, root, root / "server.log")
    wait_status(server, port)
    return server, port


def run(binary: Path, artifact_root: Path) -> dict[str, object]:
    artifact_root.mkdir(parents=True, exist_ok=True)
    root = artifact_root / "server-root"
    world = artifact_root / "world"
    server, port = start_server(binary, root, world)
    client: LiveClient | None = None
    transcript: list[dict[str, object]] = []
    observations: dict[str, object] = {}
    try:
        client = LiveClient(port, "GoalFeatureOp")
        initial = client.pump(1.5)
        observations["initial_packet_ids"] = sorted(set(client.packet_ids))
        observations["initial_nonempty_packets"] = sum(1 for _, payload in initial if payload)
        required_initial = {
            PLAY_DECLARE_COMMANDS,
            PLAY_UPDATE_TIME,
        }
        missing = sorted(required_initial.difference(client.packet_ids))
        if missing:
            raise AssertionError(f"initial play declaration/time packets missing: {missing}")
        observations["tab_complete"] = client.tab_complete("/gi")

        commands = [
            ("time set day", "Time set to day"),
            ("gamerule randomTickSpeed 3", "Gamerule randomTickSpeed is now 3"),
            ("weather rain 5", "Set weather to rain"),
            ("difficulty peaceful", "Difficulty set to peaceful"),
            ("gamemode creative", "Set own game mode to creative"),
            ("setblock 40 -60 40 minecraft:stone", "Changed the block"),
            ("setblock 2 -59 15 minecraft:stone", "Changed the block"),
            ("fill 42 -60 42 44 -60 44 minecraft:glass", "Filled 9 blocks"),
            ("execute as @s run setblock 45 -60 45 minecraft:gold_block", "Changed the block"),
            ("forceload add 2 2", "Added chunk"),
            ("forceload query", "Forced chunks"),
            ("forceload remove 2 2", "Removed chunk"),
            ("summon minecraft:pig 41 -60 40", "Summoned minecraft:pig"),
            ("tp @s 2 -60 2", "Teleported 1 entity"),
            ("give @s minecraft:diamond 1", "Given 1 x minecraft:diamond"),
            ("give @s minecraft:bow 1", "Given 1 x minecraft:bow"),
            ("give @s minecraft:arrow 8", "Given 8 x minecraft:arrow"),
            ("item replace entity @s weapon.mainhand with minecraft:bow", "Replaced entity slot"),
            ("item replace entity @s hotbar.1 with minecraft:arrow 8", "Replaced entity slot"),
            ("tag @s add goal_probe", "Added tag"),
            ("team add goal_team", "Created team goal_team"),
            ("scoreboard objectives add goal_obj dummy", "Created objective"),
            ("scoreboard players set GoalFeatureOp goal_obj 7", "goal_obj"),
            ("worldborder set 64", "Set world border to 64.000000 blocks wide"),
            ("worldborder center 0 0", "Set world border center to 0.000000, 0.000000"),
            ("worldborder get", "The world border is currently"),
            ("setworldspawn 0 -60 0", "Set world spawn to 0, -60, 0"),
            ("spawnpoint @s 0 -60 0", "players' spawn point"),
            ("bossbar add goalbar goal_bar", "Created bossbar goalbar"),
            ("effect give @s speed 5", "Applied minecraft:speed"),
            ("xp add @s 5 points", "Gave 5 xp"),
            ("gamemode survival", "Set own game mode to survival"),
            ("damage @s 1 minecraft:generic", "Dealt 1.000000 generic damage"),
            ("data get entity @s", "entity data:"),
            ("datapack list", "Enabled"),
            ("function goal:entry", "Executed function goal:entry"),
            ("schedule function goal:entry 2t", "Scheduled goal:entry"),
            ("reload", "Reload complete"),
            ("time query daytime", "The time is"),
        ]
        summoned_entity_ids: list[int] = []
        for text, expected in commands:
            spawn_count_before = len(client.spawn_entity_ids)
            transcript.append(client.command(text, expected))
            if text == "summon minecraft:pig 41 -60 40":
                summoned_entity_ids = client.spawn_entity_ids[spawn_count_before:]
            if text == "tp @s 2 -60 2":
                if not summoned_entity_ids:
                    raise AssertionError("summon did not produce a real SpawnEntity packet")
                mount_packets = client.use_entity(summoned_entity_ids[-1])
                observations["use_entity_mount"] = {
                    "entity_id": summoned_entity_ids[-1],
                    "entity_records": client.spawn_entity_records,
                    "packet_ids": [packet_id for packet_id, _ in mount_packets],
                    "set_passengers": PLAY_SET_PASSENGERS in client.packet_ids,
                    "connection_continued": not client.disconnected,
                }
                if client.disconnected:
                    raise AssertionError("UseEntity interaction disconnected the live client")
                if PLAY_SET_PASSENGERS in client.packet_ids:
                    dismount_packets = client.player_input(0x02)
                    if PLAY_SET_PASSENGERS not in client.packet_ids:
                        raise AssertionError(
                            f"PlayerInput sneak did not produce dismount SetPassengers; "
                            f"got {[packet_id for packet_id, _ in dismount_packets]}"
                        )
            if text == "item replace entity @s hotbar.1 with minecraft:arrow 8":
                client.set_held_slot(0)
                client.move_player_pos_rot(2.0, -60.0, 2.0, 0.0, 0.0)
                before = len(client.packet_ids)
                spawn_before = len(client.spawn_entity_records)
                projectile_packets = client.use_item(1, 0.0, 0.0)
                new_spawn_records = client.spawn_entity_records[spawn_before:]
                projectile_ids = [entity_id for entity_id, _ in new_spawn_records]
                if not projectile_ids:
                    raise AssertionError("bow UseItem did not produce a projectile SpawnEntity")
                projectile_id = projectile_ids[-1]
                motion_ids = []
                motion_entity_ids = []
                for packet_id, payload in projectile_packets:
                    if packet_id in {PLAY_MOVE_ENTITY_POS, PLAY_MOVE_ENTITY_POS_ROT}:
                        moved_id, _ = read_varint(payload, 0)
                        motion_entity_ids.append(moved_id)
                        if moved_id == projectile_id:
                            motion_ids.append(packet_id)
                observations["projectile_entry"] = {
                    "entity_id": projectile_id,
                    "spawn_records": new_spawn_records,
                    "packet_ids": [packet_id for packet_id, _ in projectile_packets],
                    "motion_packet_ids": motion_ids,
                    "motion_entity_ids": motion_entity_ids,
                    "ack_seen": PLAY_ACK_BLOCK_CHANGE in client.packet_ids[before:],
                    "connection_continued": not client.disconnected,
                }
                if not motion_ids:
                    raise AssertionError(
                        f"projectile {projectile_id} had no real movement packet: "
                        f"packets={[packet_id for packet_id, _ in projectile_packets]} "
                        f"motion_ids={motion_entity_ids} "
                        f"spawn_records={client.spawn_entity_records}"
                    )
                if client.disconnected:
                    raise AssertionError("UseItem projectile launch disconnected the live client")

        # The command actions above must have traversed their actual wire
        # consequences, not only feedback chat.
        client.pump(1.0)
        observations["packet_ids"] = sorted(set(client.packet_ids))
        observations["packet_counts"] = {
            str(packet_id): client.packet_ids.count(packet_id)
            for packet_id in sorted(set(client.packet_ids))
        }
        required_wire = {
            PLAY_SPAWN_ENTITY,
            PLAY_SET_CONTENT,
            PLAY_TEAMS,
            PLAY_BLOCK_UPDATE,
            PLAY_SECTION_BLOCKS_UPDATE,
            PLAY_SET_DEFAULT_SPAWN,
            PLAY_CHANGE_DIFFICULTY,
            PLAY_BOSSBAR,
            PLAY_SET_HEALTH,
            PLAY_SET_EXPERIENCE,
            PLAY_ENTITY_EFFECT,
        }
        observations["wire_required_seen"] = {
            str(packet_id): packet_id in client.packet_ids for packet_id in sorted(required_wire)
        }
        missing_wire = sorted(packet_id for packet_id in required_wire if packet_id not in client.packet_ids)
        if missing_wire:
            raise AssertionError(f"command wire consequences missing: {missing_wire}")

        level_dat = world / "level.dat"
        if not level_dat.is_file() or level_dat.stat().st_size < 64:
            raise AssertionError("live command session did not write a non-trivial level.dat")
        observations["level_dat"] = {
            "present": True,
            "bytes": level_dat.stat().st_size,
        }
        (world / "goal-live-state.json").write_text(
            json.dumps({"transcript": transcript, "observations": observations}, sort_keys=True),
            encoding="utf-8",
        )
        return {
            "status": "PASS",
            "matrix": "GOAL-LIVE-FEATURE-ENTRY",
            "protocol": 769,
            "commands": transcript,
            "observations": observations,
            "artifact_root": str(artifact_root),
        }
    finally:
        if client is not None:
            client.close()
        stop_result = server.stop()
        if stop_result["returncode"] not in (0, -signal.SIGTERM):
            raise RuntimeError(f"server shutdown failed: {stop_result}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=REPO / "build" / "cppfm")
    parser.add_argument("--artifact-root", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    artifact_root = args.artifact_root or Path(tempfile.mkdtemp(prefix="cppfm-goal-live-features-"))
    try:
        result = run(binary, artifact_root)
    except BaseException as error:
        result = {
            "status": "FAIL",
            "matrix": "GOAL-LIVE-FEATURE-ENTRY",
            "error": f"{type(error).__name__}: {error}",
            "artifact_root": str(artifact_root),
        }
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
