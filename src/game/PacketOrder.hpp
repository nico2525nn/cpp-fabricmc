#pragma once

#include "../proto/Ids.hpp"

#include <cstdint>

namespace cppfm {

// Chunk snapshots and deltas share a FIFO so a client never receives a
// chunk-scoped update before the chunk state it depends on. Control packets
// may still overtake bulk chunk streaming.
inline bool requiresChunkPacketOrdering(std::uint8_t id) noexcept {
    return id == proto::pl::sc::LevelChunkWithLight ||
           id == proto::pl::sc::UpdateLight ||
           id == proto::pl::sc::BundleDelimiter ||
           id == proto::pl::sc::BlockUpdate ||
           id == proto::pl::sc::MultiBlockChange ||
           id == proto::pl::sc::BlockEntityData;
}

} // namespace cppfm
