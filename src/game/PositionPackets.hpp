#pragma once

#include "../core/ByteBuffer.hpp"

namespace cppfm {

// 1.21.4 PlayerPositionLookS2CPacket body:
// teleport id, PlayerPosition (position + delta movement + rotation), then
// PositionFlagSet. Unlike the entity-position packet, this record has no
// on-ground field.
inline WriteBuffer makePlayerPositionLookBody(
    std::int32_t teleportId,
    double x, double y, double z,
    double deltaX = 0.0, double deltaY = 0.0, double deltaZ = 0.0,
    float yaw = 0.0f, float pitch = 0.0f,
    std::int32_t relativeFlags = 0) {
    WriteBuffer out;
    out.varint(teleportId);
    out.f64(x);
    out.f64(y);
    out.f64(z);
    out.f64(deltaX);
    out.f64(deltaY);
    out.f64(deltaZ);
    out.f32(yaw);
    out.f32(pitch);
    out.i32(relativeFlags);
    return out;
}

inline WriteBuffer makePlayerRotationBody(const float yaw,
                                          const float pitch) {
    WriteBuffer out;
    out.f32(yaw);
    out.f32(pitch);
    return out;
}

} // namespace cppfm
