#!/usr/bin/env python3
"""Generate exact protocol-769 block-state collision boxes.

Reproduce from the pinned local input with:
    npm --prefix tests/mineflayer ci
    python3 tools/gen_block_collision_shapes.py

Use --check in CI to verify the generated header is current. The generator
validates block-state spans and quantizes Minecraft's voxel coordinates in
units of 1/32 block, which represents every pinned 1.21.4 shape exactly.
"""

import argparse
import json
import math
from pathlib import Path

from atomic_write import write_text_atomic


ROOT = Path(__file__).resolve().parent.parent
DATA_PACKAGE = ROOT / "tests/mineflayer/node_modules/minecraft-data"
DATA_ROOT = DATA_PACKAGE / "minecraft-data/data/pc/1.21.4"
OUTPUT = ROOT / "src/generated/BlockCollisionShapes.hpp"
EXPECTED_PACKAGE_VERSION = "3.117.0"
COORDINATE_SCALE = 32

PARSER = argparse.ArgumentParser(description=__doc__)
PARSER.add_argument("--check", action="store_true")
ARGS = PARSER.parse_args()


def read_inputs():
    package = json.loads((DATA_PACKAGE / "package.json").read_text(
        encoding="utf-8"))
    if package.get("version") != EXPECTED_PACKAGE_VERSION:
        raise SystemExit(
            f"expected minecraft-data {EXPECTED_PACKAGE_VERSION}, "
            f"found {package.get('version')!r}")
    blocks = json.loads((DATA_ROOT / "blocks.json").read_text(
        encoding="utf-8"))
    collision = json.loads((DATA_ROOT / "blockCollisionShapes.json").read_text(
        encoding="utf-8"))
    return blocks, collision


def quantize_coordinate(value, shape_id):
    scaled = value * COORDINATE_SCALE
    rounded = round(scaled)
    if (not math.isfinite(value) or abs(scaled - rounded) > 1e-7 or
            not -128 <= rounded <= 127):
        raise SystemExit(
            f"shape {shape_id} has unrepresentable coordinate {value!r}")
    return rounded


def generate_header():
    blocks, collision = read_inputs()
    shape_data = collision["shapes"]
    shape_ids = sorted(int(key) for key in shape_data)
    if shape_ids != list(range(shape_ids[-1] + 1)):
        raise SystemExit("collision shape IDs are not contiguous from zero")

    shapes = []
    boxes = []
    max_height = 0
    min_offsets = [0, 0, 0]
    max_offsets = [0, 0, 0]
    for shape_id in shape_ids:
        first_box = len(boxes)
        for box in shape_data[str(shape_id)]:
            if len(box) != 6:
                raise SystemExit(f"shape {shape_id} has a malformed box")
            quantized = tuple(quantize_coordinate(value, shape_id)
                              for value in box)
            if (quantized[0] > quantized[3] or
                    quantized[1] > quantized[4] or
                    quantized[2] > quantized[5]):
                raise SystemExit(f"shape {shape_id} has invalid box bounds")
            max_height = max(max_height, quantized[4])
            for axis in range(3):
                min_offsets[axis] = min(min_offsets[axis], quantized[axis])
                max_offsets[axis] = max(max_offsets[axis],
                                        quantized[axis + 3])
            boxes.append(quantized)
        box_count = len(boxes) - first_box
        if box_count > 0xffff:
            raise SystemExit(f"shape {shape_id} has too many boxes")
        shapes.append((first_box, box_count))

    state_shape_ids = [0] * (max(block["maxStateId"] for block in blocks) + 1)
    collision_blocks = collision["blocks"]
    known_blocks = set()
    for block in blocks:
        name = block["name"]
        if name not in collision_blocks:
            raise SystemExit(f"missing collision shape mapping for {name}")
        known_blocks.add(name)
        block_shapes = collision_blocks[name]
        state_count = block["maxStateId"] - block["minStateId"] + 1
        if isinstance(block_shapes, list):
            if len(block_shapes) != state_count:
                raise SystemExit(
                    f"{name} maps {len(block_shapes)} shapes for "
                    f"{state_count} states")
            per_state = block_shapes
        else:
            per_state = [block_shapes] * state_count
        for offset, shape_id in enumerate(per_state):
            if not isinstance(shape_id, int) or shape_id not in shape_ids:
                raise SystemExit(f"{name} references invalid shape {shape_id!r}")
            state_shape_ids[block["minStateId"] + offset] = shape_id
    extra_blocks = set(collision_blocks) - known_blocks
    if extra_blocks:
        raise SystemExit(
            "collision data references unknown blocks: " +
            ", ".join(sorted(extra_blocks)[:8]))

    def array_lines(values, format_value, per_line):
        lines = []
        for start in range(0, len(values), per_line):
            chunk = values[start:start + per_line]
            lines.append("    " + ", ".join(format_value(value)
                                               for value in chunk) + ",")
        return "\n".join(lines)

    shape_lines = array_lines(
        shapes, lambda entry: f"{{{entry[0]}u, {entry[1]}u}}", 5)
    box_lines = array_lines(
        boxes,
        lambda box: "{" + ", ".join(str(value) for value in box) + "}",
        4)
    state_lines = array_lines(state_shape_ids, lambda value: f"{value}u", 20)
    max_height_literal = f"{max_height / COORDINATE_SCALE:g}f"
    min_offsets_literal = ", ".join(str(value) for value in min_offsets)
    max_offsets_literal = ", ".join(str(value) for value in max_offsets)
    return f'''// Generated by tools/gen_block_collision_shapes.py. Do not edit.
// Source: minecraft-data {EXPECTED_PACKAGE_VERSION}, Minecraft 1.21.4.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cppfm::gen {{

inline constexpr std::uint8_t kCollisionCoordinateUnitsPerBlock =
    {COORDINATE_SCALE};
inline constexpr float kMaxCollisionShapeHeight = {max_height_literal};
inline constexpr std::array<std::int8_t, 3> kMinCollisionShapeOffsets =
    {{{min_offsets_literal}}};
inline constexpr std::array<std::int8_t, 3> kMaxCollisionShapeOffsets =
    {{{max_offsets_literal}}};

struct CollisionBox {{
    std::int8_t minX, minY, minZ, maxX, maxY, maxZ;
}};

struct CollisionShapeRange {{
    std::uint32_t firstBox;
    std::uint16_t boxCount;
}};

inline constexpr std::array<CollisionShapeRange, {len(shapes)}> kCollisionShapes = {{{{
{shape_lines}
}}}};

inline constexpr std::array<CollisionBox, {len(boxes)}> kCollisionBoxes = {{{{
{box_lines}
}}}};

inline constexpr std::array<std::uint16_t, {len(state_shape_ids)}> kBlockStateCollisionShapeIds = {{{{
{state_lines}
}}}};

inline constexpr CollisionBox kUnknownStateCollisionBox{{
    0, 0, 0, {COORDINATE_SCALE}, {COORDINATE_SCALE}, {COORDINATE_SCALE}}};

inline std::span<const CollisionBox> collisionBoxesForState(
    std::uint32_t stateId) noexcept {{
    if (stateId >= kBlockStateCollisionShapeIds.size())
        return {{&kUnknownStateCollisionBox, 1}};
    const auto shapeId = kBlockStateCollisionShapeIds[stateId];
    const auto& range = kCollisionShapes[shapeId];
    return {{kCollisionBoxes.data() + range.firstBox, range.boxCount}};
}}

}} // namespace cppfm::gen
'''


generated = generate_header()
if ARGS.check:
    if not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != generated:
        raise SystemExit(
            f"{OUTPUT.relative_to(ROOT)} is stale; "
            "run tools/gen_block_collision_shapes.py")
    print(f"verified {OUTPUT.relative_to(ROOT)}")
else:
    write_text_atomic(OUTPUT, generated)
    print(f"wrote {OUTPUT.relative_to(ROOT)}")
