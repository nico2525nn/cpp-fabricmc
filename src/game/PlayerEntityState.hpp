#pragma once

#include "MetadataTypes.hpp"
#include "Player.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace cppfm {

// Values mirrored to another client's player entity. Callers snapshot these
// fields while holding Player::stateMtx; packet construction then uses only
// this detached value object.
struct PlayerEntityMetadataState {
    std::uint8_t entityFlags = 0;
    std::int32_t pose = 0;
    std::uint8_t livingFlags = 0;
    float health = 20.0f;
    bool sleeping = false;
    std::array<std::int32_t, 3> sleepingPosition{};
    std::uint8_t skinParts = 0x7F;
    std::uint8_t mainArm = 1;
};

inline PlayerEntityMetadataState playerEntityMetadataState(
    const Player& player) {
    PlayerEntityMetadataState state;
    if (player.fireTicks > 0) state.entityFlags |= 0x01;
    if (player.isSneaking) state.entityFlags |= 0x02;
    if (player.isSprinting) state.entityFlags |= 0x08;
    if (player.isSwimming) state.entityFlags |= 0x10;
    if (isInvisible(player.effects)) state.entityFlags |= 0x20;
    if (isGlowing(player.effects)) state.entityFlags |= 0x40;
    state.pose = player.sleeping ? 2
        : player.isSwimming ? 3
        : player.isSneaking ? 5 : 0;
    if (player.usingItem) state.livingFlags |= 0x01;
    if (player.usingItem && player.usingItemOffhand)
        state.livingFlags |= 0x02;
    state.health = player.health;
    state.sleeping = player.sleeping;
    state.sleepingPosition = {player.bedX, player.bedY, player.bedZ};
    state.skinParts = player.clientSettings.skinParts;
    state.mainArm = static_cast<std::uint8_t>(
        player.clientSettings.mainHand == 0 ? 0 : 1);
    return state;
}

inline WriteBuffer makePlayerEntityMetadataPacketBody(
    const std::int32_t entityId, const PlayerEntityMetadataState& state) {
    WriteBuffer packet;
    packet.varint(entityId);
    meta::writeMetaByte(packet, 0, state.entityFlags);
    meta::writeMetaPose(packet, 6, state.pose);
    meta::writeMetaByte(packet, 8, state.livingFlags);
    meta::writeMetaFloat(packet, 9, state.health);
    if (state.sleeping)
        meta::writeMetaOptPosition(packet, 14, state.sleepingPosition);
    meta::writeMetaByte(packet, 17, state.skinParts);
    meta::writeMetaByte(packet, 18, state.mainArm);
    packet.u8(0xFF);
    return packet;
}

inline WriteBuffer makePlayerEntityEffectPacketBody(
    const std::int32_t entityId, const EffectInstance& effect) {
    WriteBuffer packet;
    packet.varint(entityId);
    packet.varint(effect.type);
    packet.varint(effect.amplifier);
    packet.varint(effect.infiniteDuration ? -1 : effect.durationTicks);
    packet.u8(static_cast<std::uint8_t>(effectFlags(effect)));
    return packet;
}

} // namespace cppfm
