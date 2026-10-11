#pragma once

#include "ChunkKey.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

#ifdef CPPFM_WORLD_MUTATION_TEST
namespace cppfm {
struct WorldMutationContractTestAccess;
}
#endif

namespace cppfm::chunk_io_detail {

// Coordinates in this object are dimension-qualified. A stamp serializes
// reads and writes for one chunk while its generation invalidates snapshots
// that overlap newer save work.
class ChunkSaveCoordinator {
#ifdef CPPFM_WORLD_MUTATION_TEST
    friend struct ::cppfm::WorldMutationContractTestAccess;
#endif

public:
    struct Stamp {
        std::mutex ioMutex;
        std::mutex stateMutex;
        std::atomic<std::uint64_t> latestSaveId{0};
        std::uint64_t loadGeneration = 0;
        std::size_t pendingWrites = 0;
        bool loadInFlight = false;
    };

    struct LoadTicket {
        std::shared_ptr<Stamp> stamp;
        std::uint64_t generation = 0;
    };

    struct SaveTicket {
        std::shared_ptr<Stamp> stamp;
        std::uint64_t saveId = 0;
    };

    std::optional<LoadTicket> beginLoad(const DimensionChunkKey& key) {
        std::lock_guard coordinatorLock(mutex);
        auto stamp = getOrCreateLocked(key);
        std::lock_guard stateLock(stamp->stateMutex);
        if (stamp->pendingWrites != 0 || stamp->loadInFlight)
            return std::nullopt;
        stamp->loadInFlight = true;
        const auto generation = stamp->loadGeneration;
        return LoadTicket{std::move(stamp), generation};
    }

    SaveTicket beginSave(const DimensionChunkKey& key) {
        std::lock_guard coordinatorLock(mutex);
        auto stamp = getOrCreateLocked(key);
        std::lock_guard stateLock(stamp->stateMutex);
        ++stamp->loadGeneration; // invalidate reads predating this snapshot
        ++stamp->pendingWrites;
        const auto saveId =
            stamp->latestSaveId.fetch_add(1, std::memory_order_acq_rel) + 1;
        return SaveTicket{std::move(stamp), saveId};
    }

    bool lockCurrentLoad(const DimensionChunkKey& key,
                         const LoadTicket& ticket,
                         std::unique_lock<std::mutex>& stateLock) {
        std::unique_lock coordinatorLock(mutex);
        const auto it = latest.find(key);
        if (it == latest.end() || it->second.lock() != ticket.stamp)
            return false;
        std::unique_lock candidateLock(ticket.stamp->stateMutex);
        coordinatorLock.unlock();
        const bool current =
            ticket.stamp->loadGeneration == ticket.generation &&
            ticket.stamp->pendingWrites == 0 &&
            ticket.stamp->loadInFlight;
        if (!current) return false;
        stateLock = std::move(candidateLock);
        return true;
    }

    void finishLoad(const DimensionChunkKey& key,
                    const std::shared_ptr<Stamp>& stamp) {
        std::lock_guard coordinatorLock(mutex);
        std::lock_guard stateLock(stamp->stateMutex);
        stamp->loadInFlight = false;
        if (stamp->pendingWrites == 0) {
            const auto it = latest.find(key);
            if (it != latest.end() && it->second.lock() == stamp)
                latest.erase(it);
        }
    }

    bool hasPendingWrite(const DimensionChunkKey& key) {
        std::lock_guard coordinatorLock(mutex);
        const auto it = latest.find(key);
        if (it == latest.end()) return false;
        const auto stamp = it->second.lock();
        if (!stamp) return false;
        std::lock_guard stateLock(stamp->stateMutex);
        return stamp->pendingWrites != 0;
    }

    template <typename OnPersisted>
    void finishSave(const DimensionChunkKey& key,
                    const std::shared_ptr<Stamp>& stamp,
                    std::uint64_t saveId, bool persisted,
                    OnPersisted&& onPersisted) {
        bool completePersistedSave = false;
        {
            std::lock_guard coordinatorLock(mutex);
            std::lock_guard stateLock(stamp->stateMutex);
            completePersistedSave = persisted &&
                stamp->latestSaveId.load(std::memory_order_acquire) == saveId;
            ++stamp->loadGeneration; // reject reads overlapping the write
            if (stamp->pendingWrites > 0) --stamp->pendingWrites;
            if (stamp->pendingWrites == 0 && !stamp->loadInFlight) {
                const auto it = latest.find(key);
                if (it != latest.end() && it->second.lock() == stamp)
                    latest.erase(it);
            }
        }

        // Callbacks can acquire persistence, world, or entity locks. Invoke
        // them after releasing coordinator locks; their dirty-version check
        // rejects a stale completion if a newer mutation raced this handoff.
        if (!completePersistedSave) return;
        try {
            std::forward<OnPersisted>(onPersisted)();
        } catch (const std::exception& error) {
            std::fprintf(stderr,
                         "[ChunkSaveCoordinator] post-save completion failed: %s\n",
                         error.what());
        } catch (...) {
            std::fprintf(stderr,
                         "[ChunkSaveCoordinator] post-save completion failed\n");
        }
    }

private:
    std::mutex mutex;

    void pruneExpiredLocked() {
        constexpr std::size_t kPruneThreshold = 4096;
        if (latest.size() < kPruneThreshold) return;
        for (auto it = latest.begin(); it != latest.end();) {
            if (it->second.expired()) it = latest.erase(it);
            else ++it;
        }
    }

    std::shared_ptr<Stamp> getOrCreateLocked(const DimensionChunkKey& key) {
        pruneExpiredLocked();
        auto& weak = latest[key];
        auto stamp = weak.lock();
        if (!stamp) {
            stamp = std::make_shared<Stamp>();
            weak = stamp;
        }
        return stamp;
    }

    std::unordered_map<DimensionChunkKey, std::weak_ptr<Stamp>,
                       DimensionChunkKeyHash> latest;
};

}  // namespace cppfm::chunk_io_detail
