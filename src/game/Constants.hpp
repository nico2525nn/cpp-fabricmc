#pragma once
#include <bit>
#include <cmath>
#include <cstdint>

// All values are wire / gameplay behavior preserving (constexpr replacement only).
namespace cppfm::constants {

constexpr std::int64_t kWorldBorderDiameter = 59999968;
constexpr std::int64_t kWorldBorderRadius   = kWorldBorderDiameter / 2; // 29999984

// Compression — vanilla default threshold 256 (PacketEncoder threshold <0 disables, >=0 enables when total>=threshold)
constexpr int kCompressionThresholdDefault = 256;

// Packet batching — Bundle/MultiBlockChange coalesce window (PacketBatcher + GameServer tick)
constexpr int kBlockBatchFlushMs = 50;      // ms window for queueBlockChange batch
constexpr int kBlockBatchMaxPackets = 64;   // flush when size>=64

// Chunk tickets — vanilla ticket levels (lower = higher priority, Spawn/Forced = 31)
constexpr int kTicketLevelSpawn = 31;
constexpr int kTicketLevelPlayer = 33;

// View / simulation distance — clamp for simulation culling (GameServer::isChunkInSimulationDistance)
constexpr int kViewDistanceCap = 12;        // max(12) for simulation checks
constexpr int kViewDistanceDefault = 6;
constexpr int kViewDistanceMin = 2;
constexpr int kViewDistanceMax = 32;

// Protocol / metadata
constexpr uint8_t kMetadataTerminator = 0xFF; // entityMetadataLoop terminator index 0xFF
constexpr int kMaxStringLength = 256;          // ReadBuffer string max (in.string(256))

// Light / chunk (Y geometry canonical home is cppfm::kMinY/kMaxY/kSectionsPerChunk
// in World.hpp — kept single-sourced there, not duplicated here.)

// Misc gameplay
constexpr int kMaxRenameLength = 50; // anvil rename limit
// Forced chunks — Yarn ForcedChunkState vanilla cap (World + WorldDataManager).
constexpr int kMaxForcedChunks = 256;
// Chat signatures — fixed bytes per signature entry (ChatCommandSigned).
constexpr int kChatSignatureBytes = 256;
// Protocol angle scale — yaw/pitch degrees to byte. Kept as numerator/denominator pair (not a precomputed quotient) so `deg *
// kAngleScaleNum / kAngleScaleDen` stays bit-identical to the previous `deg * 256.f / 360.f` (float rounding).
constexpr float kAngleScaleNum = 256.0f;
constexpr float kAngleScaleDen = 360.0f;

// Convert finite degrees to the protocol's wrapping signed/unsigned angle
// byte without an out-of-range floating-to-integer conversion. The modulo is
// also what makes equivalent rotations one full turn apart encode alike.
inline std::uint8_t protocolAngleByte(float degrees) noexcept {
    if (!std::isfinite(degrees)) return 0;
    const float wrapped = std::fmod(degrees, kAngleScaleDen);
    const float scaled = wrapped * kAngleScaleNum / kAngleScaleDen;
    const auto wholeAngle = static_cast<std::int16_t>(scaled);
    return static_cast<std::uint8_t>(wholeAngle);
}

inline std::int8_t protocolSignedAngleByte(float degrees) noexcept {
    return std::bit_cast<std::int8_t>(protocolAngleByte(degrees));
}

} // namespace cppfm::constants
