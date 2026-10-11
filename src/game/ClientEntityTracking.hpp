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

    enum class EntityTransition : std::uint8_t {
        Unchanged,
        Spawned,
        Despawned,
        Moved,
    };

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
        const auto entities = trackedEntitiesByChunk_.find(*chunk);
        if (entities != trackedEntitiesByChunk_.end()) {
            removed.assign(entities->second.begin(), entities->second.end());
        }
        std::sort(removed.begin(), removed.end());
        sentChunks_.erase(*chunk);
        for (const auto entityId : removed) trackedEntities_.erase(entityId);
        if (entities != trackedEntitiesByChunk_.end())
            trackedEntitiesByChunk_.erase(entities);
        return removed;
    }

    bool tryTrackEntity(std::int32_t entityId, std::int8_t dimension,
                        std::int32_t cx, std::int32_t cz) {
        const auto chunk = makeChunk(dimension, cx, cz);
        if (!chunk) return false;
        std::lock_guard lock(mutex_);
        if (!sentChunks_.contains(*chunk)) return false;
        if (trackedEntities_.contains(entityId)) return false;
        trackEntityLocked(entityId, *chunk);
        return true;
    }

    bool tracksEntity(std::int32_t entityId) const {
        std::lock_guard lock(mutex_);
        return trackedEntities_.contains(entityId);
    }

    bool forgetEntity(std::int32_t entityId) {
        std::lock_guard lock(mutex_);
        const auto entity = trackedEntities_.find(entityId);
        if (entity == trackedEntities_.end()) return false;
        untrackEntityLocked(entityId, entity->second);
        trackedEntities_.erase(entity);
        return true;
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
        if (entity->second == *chunk) return true;
        const auto previous = entity->second;
        trackedEntitiesByChunk_[*chunk].insert(entityId);
        entity->second = *chunk;
        untrackEntityLocked(entityId, previous);
        return true;
    }

    // Atomically reconcile one entity against the viewer's current sent-chunk
    // set. Callers emit packets only for Spawned/Despawned; Moved is a local
    // bookkeeping change because the client already has the entity.
    EntityTransition reconcileEntity(std::int32_t entityId, bool visible,
                                     std::int8_t dimension,
                                     std::int32_t cx,
                                     std::int32_t cz) {
        const auto chunk = makeChunk(dimension, cx, cz);
        std::lock_guard lock(mutex_);
        const auto existing = trackedEntities_.find(entityId);
        const bool shouldTrack = visible && chunk &&
                                 sentChunks_.contains(*chunk);
        if (!shouldTrack) {
            if (existing == trackedEntities_.end())
                return EntityTransition::Unchanged;
            untrackEntityLocked(entityId, existing->second);
            trackedEntities_.erase(existing);
            return EntityTransition::Despawned;
        }
        if (existing == trackedEntities_.end()) {
            trackEntityLocked(entityId, *chunk);
            return EntityTransition::Spawned;
        }
        if (existing->second == *chunk)
            return EntityTransition::Unchanged;
        const auto previous = existing->second;
        trackedEntitiesByChunk_[*chunk].insert(entityId);
        existing->second = *chunk;
        untrackEntityLocked(entityId, previous);
        return EntityTransition::Moved;
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
        trackedEntitiesByChunk_.clear();
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

    void trackEntityLocked(std::int32_t entityId, const Chunk& chunk) {
        const auto [entity, inserted] = trackedEntities_.emplace(entityId, chunk);
        if (!inserted) return;
        try {
            trackedEntitiesByChunk_[chunk].insert(entityId);
        } catch (...) {
            trackedEntities_.erase(entity);
            throw;
        }
    }

    void untrackEntityLocked(std::int32_t entityId, const Chunk& chunk) {
        const auto entities = trackedEntitiesByChunk_.find(chunk);
        if (entities == trackedEntitiesByChunk_.end()) return;
        entities->second.erase(entityId);
        if (entities->second.empty()) trackedEntitiesByChunk_.erase(entities);
    }

    mutable std::mutex mutex_;
    std::unordered_set<Chunk, chunk_io_detail::DimensionChunkKeyHash>
        sentChunks_;
    std::unordered_map<std::int32_t, Chunk> trackedEntities_;
    std::unordered_map<Chunk, std::unordered_set<std::int32_t>,
                       chunk_io_detail::DimensionChunkKeyHash>
        trackedEntitiesByChunk_;
};

} // namespace cppfm
