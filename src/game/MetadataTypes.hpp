#pragma once
#include "../core/ByteBuffer.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace cppfm::meta {

// Prismarine proto.yml entityMetadata type ids (1.21.4):
// 0 Byte, 1 VarInt, 2 VarLong, 3 Float, 4 String, 5 Chat, 6 OptChat, 7 Slot, 8 Boolean,
// 9 Rotations, 10 Position, 11 OptPosition, 12 Direction, 13 OptUUID,
// 14 BlockState, 15 OptionalBlockState, 16 CompoundTag, 17 Particle, 18 Particles,
// 19 VillagerData, 20 OptUnsignedInt, 21 Pose
enum class Type : int {
    Byte = 0,
    VarInt = 1,
    VarLong = 2,
    Float = 3,
    String = 4,
    Chat = 5,
    OptChat = 6,
    Slot = 7,
    Boolean = 8,
    Rotations = 9,
    Position = 10,
    OptPosition = 11,
    Direction = 12,
    OptUUID = 13,
    BlockState = 14,
    OptBlockState = 15,
    CompoundTag = 16,
    Particle = 17,
    Particles = 18,
    VillagerData = 19,
    OptUnsignedInt = 20,
    Pose = 21
};

inline void writeMetaBool(WriteBuffer& out, std::uint8_t idx, bool v) {
    out.u8(idx);
    out.varint(static_cast<int>(Type::Boolean));
    out.boolean(v);
}

inline void writeMetaByte(WriteBuffer& out, std::uint8_t idx, std::uint8_t v) {
    out.u8(idx);
    out.varint(static_cast<int>(Type::Byte));
    out.u8(v);
}

inline void writeMetaVarInt(WriteBuffer& out, std::uint8_t idx, std::int32_t v) {
    out.u8(idx);
    out.varint(static_cast<int>(Type::VarInt));
    out.varint(v);
}

inline void writeMetaOptBlockState(WriteBuffer& out, std::uint8_t idx, std::optional<std::uint32_t> state) {
    out.u8(idx);
    out.varint(static_cast<int>(Type::OptBlockState));
    if (state) {
        out.boolean(true);
        out.varint(static_cast<std::int32_t>(*state));
    } else {
        out.boolean(false);
    }
}

inline void writeMetaOptBlockState(WriteBuffer& out, std::uint8_t idx, std::uint32_t stateId) {
    writeMetaOptBlockState(out, idx, std::optional<std::uint32_t>{stateId});
}

} // namespace cppfm::meta
