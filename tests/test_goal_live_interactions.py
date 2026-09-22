#!/usr/bin/env python3
"""Drive block, menu, player-state, and dimension interactions over TCP."""

from __future__ import annotations

import argparse
import json
import signal
import struct
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))

from mcproto import read_varint, unpack_position, write_varint  # noqa: E402
from test_goal_live_features import (  # noqa: E402
    LiveClient,
    PLAY_ACK_BLOCK_CHANGE,
    PLAY_BLOCK_UPDATE,
    PLAY_ENTITY_EFFECT,
    PLAY_SET_ENTITY_METADATA,
    start_server,
)

PLAY_OPEN_SCREEN = 0x35
PLAY_CONTAINER_SET_CONTENT = 0x13
PLAY_CONTAINER_SET_SLOT = 0x15
PLAY_SET_EQUIPMENT = 0x60
PLAY_RESPAWN = 0x4C
PLAY_GAME_EVENT = 0x23
PLAY_PLAYER_POSITION = 0x42
PLAY_REMOVE_ENTITIES = 0x47


def skip_slot(payload: bytes, offset: int) -> int:
    count, offset = read_varint(payload, offset)
    if count <= 0:
        return offset
    _, offset = read_varint(payload, offset)
    added, offset = read_varint(payload, offset)
    removed, offset = read_varint(payload, offset)
    for _ in range(added):
        _, offset = read_varint(payload, offset)
        length, offset = read_varint(payload, offset)
        offset += length
    for _ in range(removed):
        _, offset = read_varint(payload, offset)
    return offset


def parse_content(payload: bytes) -> tuple[int, int, int]:
    window_id, offset = read_varint(payload, 0)
    state_id, offset = read_varint(payload, offset)
    count, offset = read_varint(payload, offset)
    if count < 0 or count > 128:
        raise AssertionError(f"invalid container slot count: {count}")
    for _ in range(count):
        offset = skip_slot(payload, offset)
    skip_slot(payload, offset)
    return window_id, state_id, count


def parse_block_update(payload: bytes) -> tuple[tuple[int, int, int], int]:
    if len(payload) < 8:
        raise AssertionError("block update is truncated")
    position = unpack_position(struct.unpack(">q", payload[:8])[0])
    state, _ = read_varint(payload, 8)
    return position, state


def parse_metadata(payload: bytes) -> tuple[int, dict[int, int]]:
    entity_id, offset = read_varint(payload, 0)
    values: dict[int, int] = {}
    while offset < len(payload):
        index = payload[offset]
        offset += 1
        if index == 0xFF:
            break
        type_id, offset = read_varint(payload, offset)
        if index == 0:
            if type_id != 0 or offset >= len(payload):
                raise AssertionError("unexpected flags metadata type")
            values[index] = payload[offset]
            offset += 1
        elif index == 6:
            if type_id != 1:
                raise AssertionError("unexpected pose metadata type")
            values[index], offset = read_varint(payload, offset)
        else:
            raise AssertionError(f"unparsed metadata index {index}")
    return entity_id, values


def parse_effect(payload: bytes) -> tuple[int, int, int, int, int]:
    entity_id, offset = read_varint(payload, 0)
    effect_id, offset = read_varint(payload, offset)
    if offset + 2 > len(payload):
        raise AssertionError("entity effect is truncated")
    amplifier = payload[offset]
    flags = payload[offset + 1]
    duration, _ = read_varint(payload, offset + 2)
    return entity_id, effect_id, amplifier, duration, flags


def parse_equipment(payload: bytes) -> tuple[int, list[tuple[int, int]]]:
    entity_id, offset = read_varint(payload, 0)
    entries: list[tuple[int, int]] = []
    while offset < len(payload):
        marker, offset = read_varint(payload, offset)
        slot = marker & 0x7F
        offset = skip_slot(payload, offset)
        entries.append((slot, marker))
        if marker & 0x80 == 0:
            break
    return entity_id, entries


def ack_sequences(packets: list[tuple[int, bytes]]) -> list[int]:
    return [read_varint(payload, 0)[0]
            for packet_id, payload in packets if packet_id == PLAY_ACK_BLOCK_CHANGE]


def run(binary: Path, artifact_root: Path) -> dict[str, object]:
    artifact_root.mkdir(parents=True, exist_ok=True)
    server_root = artifact_root / "server-root"
    world = artifact_root / "world"
    server, port = start_server(binary, server_root, world)
    client: LiveClient | None = None
    observations: dict[str, object] = {}
    try:
        client = LiveClient(port, "GoalFeatureOp")
        client.pump(1.0)
        if client.entity_id is None:
            raise AssertionError("live Login entity id was not observed")

        client.command("gamemode creative", "Set own game mode to creative")
        client.command("tp @s 40 -60 40", "Teleported 1 entity")

        client.command("setblock 40 -60 42 minecraft:chest", "Changed the block")
        menu_packets = client.use_item_on(40, -60, 42, 1, 10)
        open_payloads = [payload for packet_id, payload in menu_packets
                         if packet_id == PLAY_OPEN_SCREEN]
        content_payloads = [payload for packet_id, payload in menu_packets
                            if packet_id == PLAY_CONTAINER_SET_CONTENT]
        if not open_payloads or not content_payloads:
            raise AssertionError("chest UseItemOn did not open and populate a menu")
        window_id, state_id, slot_count = parse_content(content_payloads[-1])
        if window_id == 0 or slot_count < 27:
            raise AssertionError(
                f"unexpected chest content: {window_id=} {state_id=} {slot_count=} "
                f"open={[payload.hex() for payload in open_payloads]} "
                f"content={[payload[:32].hex() for payload in content_payloads]}"
            )
        if 10 not in ack_sequences(menu_packets):
            raise AssertionError("UseItemOn chest acknowledgement sequence was lost")

        click_packets = client.window_click(window_id, state_id, 0)
        click_content = [payload for packet_id, payload in click_packets
                         if packet_id == PLAY_CONTAINER_SET_CONTENT]
        click_slots = [payload for packet_id, payload in click_packets
                       if packet_id == PLAY_CONTAINER_SET_SLOT]
        if not click_content and not click_slots:
            raise AssertionError("valid WindowClick produced no authoritative inventory update")
        stale_packets = client.window_click(window_id, state_id - 1, 0)
        stale_content = [payload for packet_id, payload in stale_packets
                         if packet_id == PLAY_CONTAINER_SET_CONTENT]
        if not stale_content:
            raise AssertionError("stale WindowClick was not rejected with authoritative content")
        client.close_container(window_id)
        observations["chest_menu"] = {
            "window_id": window_id,
            "initial_state_id": state_id,
            "slot_count": slot_count,
            "open_screen": True,
            "content_after_click": bool(click_content),
            "slot_updates_after_click": len(click_slots),
            "stale_resync": len(stale_content),
        }

        before = len(client.packet_payloads)
        client.command("effect give @s speed 10 1", "Applied minecraft:speed")
        effect_payloads = [payload for packet_id, payload in client.packet_payloads[before:]
                           if packet_id == PLAY_ENTITY_EFFECT]
        if not effect_payloads:
            raise AssertionError("effect command emitted no EntityEffect packet")
        effect = parse_effect(effect_payloads[-1])
        if effect[0] != client.entity_id or effect[2] != 1 or effect[3] <= 0:
            raise AssertionError(f"unexpected effect fields: {effect}")
        observations["effect"] = {
            "entity_id": effect[0],
            "effect_id": effect[1],
            "amplifier": effect[2],
            "duration": effect[3],
            "flags": effect[4],
        }

        sneak_start = client.entity_action(0)
        sneak_stop = client.entity_action(1)
        start_metadata = [parse_metadata(payload)[1] for packet_id, payload in sneak_start
                          if packet_id == PLAY_SET_ENTITY_METADATA]
        stop_metadata = [parse_metadata(payload)[1] for packet_id, payload in sneak_stop
                         if packet_id == PLAY_SET_ENTITY_METADATA]
        if not any(values.get(6) == 5 for values in start_metadata):
            raise AssertionError(f"sneak start pose metadata missing: {start_metadata}")
        if not any(values.get(6) == 0 for values in stop_metadata):
            raise AssertionError(f"sneak stop pose metadata missing: {stop_metadata}")
        observations["sneak"] = {
            "start_metadata": start_metadata,
            "stop_metadata": stop_metadata,
        }

        before = len(client.packet_payloads)
        client.command("item replace entity @s armor.head with minecraft:diamond_helmet",
                       "Replaced entity slot")
        equipment_payloads = [payload for packet_id, payload in client.packet_payloads[before:]
                              if packet_id == PLAY_SET_EQUIPMENT]
        if not equipment_payloads:
            raise AssertionError("armor replacement emitted no SetEquipment packet")
        equipment = parse_equipment(equipment_payloads[-1])
        if equipment[0] != client.entity_id or not any(slot == 5 for slot, _ in equipment[1]):
            raise AssertionError(f"head equipment was not serialized: {equipment}")
        observations["equipment"] = {
            "entity_id": equipment[0],
            "entries": equipment[1],
        }

        client.command("gamemode survival", "Set own game mode to survival")
        client.command("setblock 41 -60 40 minecraft:stone", "Changed the block")
        client.command("item replace entity @s weapon.mainhand with minecraft:stone",
                       "Replaced entity slot")
        place_packets = client.use_item_on(41, -60, 40, 1, 11)
        updates = []
        for packet_id, payload in place_packets:
            if packet_id == PLAY_BLOCK_UPDATE:
                updates.append(parse_block_update(payload))
        if 11 not in ack_sequences(place_packets) or not updates:
            raise AssertionError(f"UseItemOn placement lacked ack/update: {place_packets}")
        if not any(state != 0 for _, state in updates):
            raise AssertionError(f"placement updates were all air: {updates}")

        client.command("item replace entity @s weapon.mainhand with minecraft:iron_pickaxe",
                       "Replaced entity slot")
        start_packets = client.player_action(0, 41, -59, 40, 1, 12)
        if 0x06 not in [packet_id for packet_id, _ in start_packets]:
            raise AssertionError(
                f"PlayerAction start did not emit break animation: "
                f"{[packet_id for packet_id, _ in start_packets]}"
            )
        time.sleep(5.0)
        dig_packets = client.player_action(2, 41, -59, 40, 1, 13)
        dig_packets += client.pump(3.0)
        if 13 not in ack_sequences(client.packet_payloads):
            dig_packets += client.player_action(2, 41, -59, 40, 1, 14)
            dig_packets += client.pump(1.0)
        dig_acks = ack_sequences(client.packet_payloads)
        dig_updates = [parse_block_update(payload) for packet_id, payload in dig_packets
                       if packet_id == PLAY_BLOCK_UPDATE]
        if not ({13, 14} & set(dig_acks)) or not any(state == 0 for _, state in dig_updates):
            raise AssertionError(
                f"PlayerAction did not break the placed block: {dig_updates}; "
                f"packets={[packet_id for packet_id, _ in dig_packets]} "
                f"acks={ack_sequences(client.packet_payloads)}"
            )
        observations["block_interaction"] = {
            "placement_ack": 11,
            "placement_updates": updates,
            "dig_ack": 13 if 13 in dig_acks else 14,
            "dig_ack_sequences": dig_acks,
            "dig_updates": dig_updates,
        }

        client.command("gamemode creative", "Set own game mode to creative")
        client.command("setblock 40 -60 40 minecraft:nether_portal", "Changed the block")
        portal_packets = client.move_player_pos_rot(40.0, -60.0, 40.0, 0.0, 0.0)
        if not any(packet_id == PLAY_RESPAWN for packet_id, _ in portal_packets):
            raise AssertionError("portal movement emitted no Respawn packet")
        if not any(packet_id == PLAY_PLAYER_POSITION for packet_id, _ in portal_packets):
            raise AssertionError("portal movement emitted no PlayerPosition packet")
        observations["portal"] = {
            "packet_ids": [packet_id for packet_id, _ in portal_packets],
            "respawn": sum(packet_id == PLAY_RESPAWN for packet_id, _ in portal_packets),
            "game_event": sum(packet_id == PLAY_GAME_EVENT for packet_id, _ in portal_packets),
        }
        if client.disconnected:
            raise AssertionError("interaction fixture disconnected the live client")
        return {
            "status": "PASS",
            "matrix": "GOAL-LIVE-INTERACTIONS",
            "protocol": 769,
            "observations": observations,
            "artifact_root": str(artifact_root),
        }
    finally:
        if client is not None:
            client.close()
        result = server.stop()
        if result["returncode"] not in (0, -signal.SIGTERM):
            raise RuntimeError(f"server shutdown failed: {result}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=REPO / "build" / "cppfm")
    parser.add_argument("--artifact-root", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    artifact_root = args.artifact_root or Path(
        tempfile.mkdtemp(prefix="cppfm-goal-live-interactions-")
    )
    try:
        print(json.dumps(run(binary, artifact_root), sort_keys=True), flush=True)
        return 0
    except Exception as exc:
        print(json.dumps({"status": "FAIL", "error": f"{type(exc).__name__}: {exc}",
                          "artifact_root": str(artifact_root)}, sort_keys=True), flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
