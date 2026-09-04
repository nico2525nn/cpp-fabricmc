#pragma once
#include <cstdint>

// Game constants — plan31 phase3 R9: centralize magic numbers that were scattered as literals.
// All values are wire / gameplay behavior preserving (constexpr replacement only).
namespace cppfm::constants {

// WorldBorder — Yarn WorldBorder.DEFAULT 5.9999968E7, diameter 59999968 (plan13 W13, 29999984 was half)
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
// Protocol angle scale — yaw/pitch degrees to byte. Kept as numerator/denominator
// pair (not a precomputed quotient) so `deg * kAngleScaleNum / kAngleScaleDen`
// stays bit-identical to the previous `deg * 256.f / 360.f` (float rounding).
constexpr float kAngleScaleNum = 256.0f;
constexpr float kAngleScaleDen = 360.0f;

} // namespace cppfm::constants
