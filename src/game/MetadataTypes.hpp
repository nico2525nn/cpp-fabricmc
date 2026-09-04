#pragma once
// MetadataTypes: entity metadata type ids per 1.21.4 protocol.json entityMetadataLoop Verified against Prismarine minecraft-data 1.21.4
// proto.yml entityMetadataEntry type varint and Yarn 1.21.4 TrackedDataHandlerRegistry (BOOLEAN=8, BYTE=0, OPTIONAL_BLOCK_STATE=15). Used
// by Creeper (D13/D14) and Enderman (D15) fixes — plan24 §4/§5.
#include <cstdint>
#include <optional>
#include "../core/ByteBuffer.hpp"

namespace cppfm::meta {

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

} // namespace cppfm::meta
