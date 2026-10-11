#pragma once

#include "ChunkKey.hpp"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cppfm {

// The server's per-client view of world entities is coupled to which chunk
// snapshots have entered that client's ordered packet stream. Keep both facts
// together so chunk eviction cannot leave entity visibility behind.
class ClientEntityTracking {
public:
    using Chunk = chunk_io_detail::DimensionChunkKey;

    bool hasChunk(std::int8_t dimension, std::int32_t cx,
                  std::int32_t cz) const {
        const auto chunk = makeChunk(dimension, cx, cz);
        if (!chunk) return false;
        std::lock_guard lock(mutex_);
        return sentChunks_.contains(*chunk);
    }

    void markChunkSent(std::int8_t dimension, std::int32_t cx,
                       std::int32_t cz) {
        const auto chunk = makeChunk(dimension, cx, cz);
        if (!chunk) return;
        std::lock_guard lock(mutex_);
        sentChunks_.insert(*chunk);
    }

    std::vector<std::int32_t> forgetChunk(std::int8_t dimension,
                                          std::int32_t cx,
                                          std::int32_t cz) {
        const auto chunk = makeChunk(dimension, cx, cz);
        if (!chunk) return {};
        std::lock_guard lock(mutex_);
        if (!sentChunks_.contains(*chunk)) return {};

        std::vector<std::int32_t> removed;
        const auto matching = std::count_if(
            trackedEntities_.begin(), trackedEntities_.end(),
            [&](const auto& entry) { return entry.second == *chunk; });
        removed.reserve(static_cast<std::size_t>(matching));
        for (const auto& [entityId, entityChunk] : trackedEntities_)
            if (entityChunk == *chunk) removed.push_back(entityId);
        std::sort(removed.begin(), removed.end());
        sentChunks_.erase(*chunk);
        for (const auto entityId : removed) trackedEntities_.erase(entityId);
        return removed;
    }

    bool tryTrackEntity(std::int32_t entityId, std::int8_t dimension,
                        std::int32_t cx, std::int32_t cz) {
        const auto chunk = makeChunk(dimension, cx, cz);
        if (!chunk) return false;
        std::lock_guard lock(mutex_);
        if (!sentChunks_.contains(*chunk)) return false;
        return trackedEntities_.emplace(entityId, *chunk).second;
    }

    bool tracksEntity(std::int32_t entityId) const {
        std::lock_guard lock(mutex_);
        return trackedEntities_.contains(entityId);
    }

    bool forgetEntity(std::int32_t entityId) {
        std::lock_guard lock(mutex_);
        return trackedEntities_.erase(entityId) != 0;
    }

    bool updateTrackedEntityChunk(std::int32_t entityId,
                                  std::int8_t dimension,
                                  std::int32_t cx,
                                  std::int32_t cz) {
        const auto chunk = makeChunk(dimension, cx, cz);
        if (!chunk) return false;
        std::lock_guard lock(mutex_);
        const auto entity = trackedEntities_.find(entityId);
        if (entity == trackedEntities_.end() ||
            !sentChunks_.contains(*chunk))
            return false;
        entity->second = *chunk;
        return true;
    }

    std::vector<Chunk> sentChunks() const {
        std::lock_guard lock(mutex_);
        std::vector<Chunk> chunks(sentChunks_.begin(), sentChunks_.end());
        std::sort(chunks.begin(), chunks.end(), [](const Chunk& lhs,
                                                   const Chunk& rhs) {
            if (lhs.dimension != rhs.dimension)
                return lhs.dimension < rhs.dimension;
            if (lhs.cx != rhs.cx) return lhs.cx < rhs.cx;
            return lhs.cz < rhs.cz;
        });
        return chunks;
    }

    void clear() {
        std::lock_guard lock(mutex_);
        sentChunks_.clear();
        trackedEntities_.clear();
    }

private:
    // This server currently owns only the three vanilla worlds. Reject other
    // IDs instead of silently aliasing them to the Overworld through the
    // legacy dimension canonicalizer.
    static std::optional<Chunk> makeChunk(std::int8_t dimension,
                                         std::int32_t cx,
                                         std::int32_t cz) noexcept {
        if (dimension != -1 && dimension != 0 && dimension != 1)
            return std::nullopt;
        return Chunk{dimension, cx, cz};
    }

    mutable std::mutex mutex_;
    std::unordered_set<Chunk, chunk_io_detail::DimensionChunkKeyHash>
        sentChunks_;
    std::unordered_map<std::int32_t, Chunk> trackedEntities_;
};

} // namespace cppfm
