#!/usr/bin/env python3
"""Exercise additional survival, combat, item, and menu entry points."""

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

from mcproto import read_varint, write_varint  # noqa: E402
from test_goal_live_features import LiveClient, start_server  # noqa: E402
from test_goal_live_interactions import parse_content, parse_equipment  # noqa: E402


PLAY_SPAWN_ENTITY = 0x01
PLAY_SET_HEALTH = 0x62
PLAY_DAMAGE_EVENT = 0x1A
PLAY_HURT_ANIMATION = 0x25
PLAY_ENTITY_VELOCITY = 0x5F
PLAY_SET_COOLDOWN = 0x17
PLAY_SET_EQUIPMENT = 0x60
PLAY_BLOCK_UPDATE = 0x09
PLAY_OPEN_SCREEN = 0x35
PLAY_CONTAINER_SET_DATA = 0x14
PLAY_INITIALIZE_WORLD_BORDER = 0x26
PLAY_WORLD_BORDER_CENTER = 0x52
PLAY_WORLD_BORDER_SIZE = 0x54


def parse_health(payload: bytes) -> tuple[float, int, float]:
    if len(payload) < 9:
        raise AssertionError("SetHealth payload is truncated")
    health = struct.unpack(">f", payload[:4])[0]
    food, offset = read_varint(payload, 4)
    if offset + 4 > len(payload):
        raise AssertionError("SetHealth saturation is truncated")
    saturation = struct.unpack(">f", payload[offset:offset + 4])[0]
    return health, food, saturation


def parse_entity_id(payload: bytes) -> int:
    entity_id, _ = read_varint(payload, 0)
    return entity_id


def parse_cooldown(payload: bytes) -> tuple[int, int]:
    item_id, offset = read_varint(payload, 0)
    ticks, _ = read_varint(payload, offset)
    return item_id, ticks


def parse_open_screen(payload: bytes) -> tuple[int, int]:
    window_id, offset = read_varint(payload, 0)
    menu_type, _ = read_varint(payload, offset)
    return window_id, menu_type


def creative_stack(slot: int, item_id: int, count: int = 1) -> bytes:
    if count <= 0 or item_id <= 0:
        stack = write_varint(0)
    else:
        # ItemStack::write: count, item id, added-component count,
        # removed-component count.
        stack = (write_varint(count) + write_varint(item_id) +
                 write_varint(0) + write_varint(0))
    return struct.pack(">h", slot) + stack


def open_menu(client: LiveClient, x: int, y: int, z: int,
              block: str, sequence: int, expected_type: int,
              expected_slots: int, expected_data: int) -> tuple[int, int, int, int, list[tuple[int, bytes]]]:
    client.command(f"setblock {x} {y} {z} minecraft:{block}", "Changed the block")
    client.command(f"tp @s {x} {y} {z}", "Teleported 1 entity")
    packets = client.use_item_on(x, y, z, 1, sequence)
    contents = [payload for packet_id, payload in packets
                if packet_id == 0x13]
    if not contents:
        raise AssertionError(
            f"{block} UseItemOn did not emit ContainerSetContent: "
            f"packets={[ (packet_id, len(payload)) for packet_id, payload in packets ]}"
        )
    window_id, state_id, slot_count = parse_content(contents[-1])
    opens = [payload for packet_id, payload in packets
             if packet_id == PLAY_OPEN_SCREEN]
    if len(opens) != 1:
        raise AssertionError(
            f"{block} emitted {len(opens)} OpenScreen packets: "
            f"packets={[packet_id for packet_id, _ in packets]}"
        )
    open_window, menu_type = parse_open_screen(opens[0])
    data_packets = [payload for packet_id, payload in packets
                    if packet_id == PLAY_CONTAINER_SET_DATA]
    if (window_id == 0 or open_window != window_id or menu_type != expected_type or
            slot_count != expected_slots or len(data_packets) != expected_data):
        raise AssertionError(
            f"unexpected {block} menu: open_window={open_window} window={window_id} "
            f"type={menu_type} expected_type={expected_type} state={state_id} "
            f"slots={slot_count} expected_slots={expected_slots} "
            f"data={len(data_packets)} expected_data={expected_data}"
        )
    client.close_container(window_id)
    return window_id, state_id, slot_count, menu_type, packets


def run(binary: Path, artifact_root: Path) -> dict[str, object]:
    artifact_root.mkdir(parents=True, exist_ok=True)
    server_root = artifact_root / "server-root"
    world = artifact_root / "world"
    server = None
    client: LiveClient | None = None
    observations: dict[str, object] = {}
    try:
        server, port = start_server(binary, server_root, world)
        client = LiveClient(port, "GoalFeatureOp")
        client.pump(1.0)
        if client.entity_id is None:
            raise AssertionError("live Login entity id was not observed")

        client.command("gamemode creative", "Set own game mode to creative")
        client.command("tp @s 50 -60 50", "Teleported 1 entity")

        # A real player attack must reach the entity interaction path and
        # produce both damage and knockback wire consequences.
        before_spawn = len(client.spawn_entity_records)
        client.command("summon minecraft:pig 51 -60 50", "Summoned minecraft:pig")
        pig_records = client.spawn_entity_records[before_spawn:]
        if not pig_records:
            raise AssertionError("summon pig emitted no SpawnEntity")
        pig_id = pig_records[-1][0]
        attack_packets = client.use_entity(pig_id, mouse=1)
        attack_ids = [packet_id for packet_id, _ in attack_packets]
        damage_ids = [parse_entity_id(payload) for packet_id, payload in attack_packets
                      if packet_id in (PLAY_DAMAGE_EVENT, PLAY_HURT_ANIMATION)]
        hurt_ids = [parse_entity_id(payload) for packet_id, payload in attack_packets
                    if packet_id == PLAY_HURT_ANIMATION]
        velocity_ids = [parse_entity_id(payload) for packet_id, payload in attack_packets
                        if packet_id == PLAY_ENTITY_VELOCITY]
        if pig_id not in hurt_ids or pig_id not in velocity_ids:
            raise AssertionError(
                f"PVP attack lacked hurt/knockback for {pig_id}: "
                f"ids={attack_ids} hurt={hurt_ids} velocity={velocity_ids}"
            )
        observations["pvp"] = {
            "target_entity": pig_id,
            "packet_ids": attack_ids,
            "damage_entity_ids": damage_ids,
            "hurt_entity_ids": hurt_ids,
            "velocity_entity_ids": velocity_ids,
        }

        # Spawn eggs use UseItemOn rather than a command-only shortcut.
        client.command("give @s minecraft:pig_spawn_egg 1", "Given 1 x minecraft:pig_spawn_egg")
        client.command("item replace entity @s weapon.mainhand with minecraft:pig_spawn_egg",
                       "Replaced entity slot")
        client.command("setblock 54 -60 50 minecraft:stone", "Changed the block")
        client.command("tp @s 54 -60 50", "Teleported 1 entity")
        spawn_before = len(client.spawn_entity_records)
        egg_packets = client.use_item_on(54, -60, 50, 1, 20)
        egg_records = client.spawn_entity_records[spawn_before:]
        if not any(entity_type == 94 for _, entity_type in egg_records):
            raise AssertionError(
                f"spawn egg produced no pig SpawnEntity: records={egg_records} "
                f"packets={[packet_id for packet_id, _ in egg_packets]}"
            )
        observations["spawn_egg"] = {
            "spawn_records": egg_records,
            "packet_ids": [packet_id for packet_id, _ in egg_packets],
        }

        # Ender pearl launch is checked at the item-use wire boundary.  The
        # cooldown is independent of whether the short fixture reaches a hit.
        client.command("give @s minecraft:ender_pearl 1", "Given 1 x minecraft:ender_pearl")
        client.command("item replace entity @s weapon.mainhand with minecraft:ender_pearl",
                       "Replaced entity slot")
        pearl_before = len(client.spawn_entity_records)
        pearl_packets = client.use_item(21, 0.0, 0.0)
        pearl_records = client.spawn_entity_records[pearl_before:]
        cooldowns = [parse_cooldown(payload) for packet_id, payload in pearl_packets
                     if packet_id == PLAY_SET_COOLDOWN]
        if (not any(entity_type == 42 for _, entity_type in pearl_records) or
                not any(item_id == 1042 and ticks == 20 for item_id, ticks in cooldowns)):
            raise AssertionError(
                f"ender pearl launch lacked pearl spawn/exact cooldown: records={pearl_records} "
                f"cooldowns={cooldowns} packets={[packet_id for packet_id, _ in pearl_packets]}"
            )
        observations["ender_pearl"] = {
            "spawn_records": pearl_records,
            "cooldowns": cooldowns,
            "packet_ids": [packet_id for packet_id, _ in pearl_packets],
        }

        # Exercise menu types through UseItemOn and parse their authoritative
        # slot counts.  The counts include the 36 player inventory slots.
        client.command("item replace entity @s weapon.mainhand with minecraft:air",
                       "Replaced entity slot")
        client.command("tp @s 56 -60 50", "Teleported 1 entity")
        menu_observations: dict[str, object] = {}
        menu_expectations = {
            "chest": (2, 63, 0),
            "barrel": (2, 63, 0),
            "shulker_box": (20, 63, 0),
            "hopper": (16, 41, 0),
            "anvil": (8, 39, 1),
            "brewing_stand": (11, 41, 2),
        }
        for sequence, block in enumerate(menu_expectations, start=30):
            expected_type, expected_slots, expected_data = menu_expectations[block]
            _, state_id, slot_count, menu_type, packets = open_menu(
                client, 56 + sequence - 30, -60, 50, block, sequence,
                expected_type, expected_slots, expected_data)
            menu_observations[block] = {
                "state_id": state_id,
                "menu_type": menu_type,
                "slot_count": slot_count,
                "set_data_count": sum(packet_id == PLAY_CONTAINER_SET_DATA
                                       for packet_id, _ in packets),
                "packet_ids": [packet_id for packet_id, _ in packets],
            }
        observations["menus"] = menu_observations

        # Creative SetCreativeModeSlot is an owned serverbound packet, not a
        # command alias.  The server's inventory slot 8 maps to protocol
        # equipment marker 5 (head) in the authoritative response.
        client.command("gamemode creative", "Set own game mode to creative")
        client.conn.send_packet_raw(0x36, creative_stack(8, 899))  # diamond_helmet
        creative_packets = client.pump(0.8)
        equipment = [payload for packet_id, payload in creative_packets
                     if packet_id == PLAY_SET_EQUIPMENT]
        if not equipment:
            raise AssertionError(
                f"SetCreativeModeSlot did not sync armor: "
                f"packets={[packet_id for packet_id, _ in creative_packets]}"
            )
        equipment_record = parse_equipment(equipment[-1])
        if equipment_record[0] != client.entity_id or not any(slot == 5 for slot, _ in equipment_record[1]):
            raise AssertionError(f"unexpected creative equipment packet: {equipment_record}")
        observations["creative_slot"] = {
            "entity_id": equipment_record[0],
            "entries": equipment_record[1],
            "packet_ids": [packet_id for packet_id, _ in creative_packets],
        }

        # Fire damage and world-border damage are observed as health/damage
        # packets after a real movement into the affected area.
        client.command("gamemode survival", "Set own game mode to survival")
        client.command("setblock 63 -60 50 minecraft:fire", "Changed the block")
        client.command("tp @s 63 -60 50", "Teleported 1 entity")
        fire_packets = client.pump(2.2)
        fire_health = [parse_health(payload) for packet_id, payload in fire_packets
                       if packet_id == PLAY_SET_HEALTH]
        fire_damage = [parse_entity_id(payload) for packet_id, payload in fire_packets
                       if packet_id == PLAY_DAMAGE_EVENT]
        if (client.entity_id not in fire_damage or
                not any(health < 20.0 for health, _, _ in fire_health)):
            raise AssertionError(
                f"fire entry lacked player damage and reduced health: "
                f"health={fire_health} damage={fire_damage}"
            )
        observations["fire"] = {
            "health": fire_health,
            "damage_entity_ids": fire_damage,
        }

        client.command("setblock 63 -60 50 minecraft:air", "Changed the block")
        border_before = len(client.packet_payloads)
        client.command("worldborder center 0 0", "Set world border center to 0.000000, 0.000000")
        # Move to the eventual damage position while the default border is
        # still large. Shrinking it first leaves the player far outside the
        # new border and can kill them before the following teleport command
        # is processed on a slower runner.
        client.command("tp @s 10 -60 50", "Teleported 1 entity")
        client.command("worldborder set 2", "Set world border to 2.000000 blocks wide")
        border_packets = client.packet_payloads[border_before:]
        border_packets += client.pump(2.2)
        border_health = [parse_health(payload) for packet_id, payload in border_packets
                         if packet_id == PLAY_SET_HEALTH]
        border_damage = [parse_entity_id(payload) for packet_id, payload in border_packets
                         if packet_id == PLAY_DAMAGE_EVENT]
        border_wire = [packet_id for packet_id, _ in border_packets
                       if packet_id in (PLAY_INITIALIZE_WORLD_BORDER,
                                        PLAY_WORLD_BORDER_CENTER,
                                        PLAY_WORLD_BORDER_SIZE)]
        if (not border_wire or client.entity_id not in border_damage or
                not any(health < 20.0 for health, _, _ in border_health)):
            raise AssertionError(
                f"world-border entry lacked wire/player damage/reduced health: "
                f"wire={border_wire} health={border_health} damage={border_damage}"
            )
        observations["world_border_damage"] = {
            "health": border_health,
            "damage_entity_ids": border_damage,
            "border_wire": border_wire,
            "damage_observed": bool(border_health or border_damage),
        }

        if client.disconnected:
            raise AssertionError("remaining-entry fixture disconnected the live client")
        result = {
            "status": "PASS",
            "matrix": "GOAL-LIVE-REMAINING",
            "protocol": 769,
            "observations": observations,
            "artifact_root": str(artifact_root),
        }
        (artifact_root / "remaining-state.json").write_text(
            json.dumps(result, sort_keys=True), encoding="utf-8"
        )
        return result
    finally:
        if client is not None:
            client.close()
        if server is not None:
            stopped = server.stop()
            if stopped["returncode"] not in (0, -signal.SIGTERM):
                raise RuntimeError(f"server shutdown failed: {stopped}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=REPO / "build" / "cppfm")
    parser.add_argument("--artifact-root", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    artifact_root = args.artifact_root or Path(
        tempfile.mkdtemp(prefix="cppfm-goal-live-remaining-")
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
