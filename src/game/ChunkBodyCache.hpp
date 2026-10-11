// Owns the bounded encoded-chunk cache and the snapshot/invalidation ordering
// contract shared by asynchronous chunk construction and network publication.
#pragma once

#include "ChunkKey.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cppfm {

struct ChunkBodyCacheTestAccess;

class ChunkBodyCache {
    friend struct ChunkBodyCacheTestAccess;

public:
    using Key = chunk_io_detail::DimensionChunkKey;
    // Callers must transfer exclusive mutable ownership before publishing a
    // body. The cache accounts vector capacity at store time and requires the
    // bytes (including capacity) to remain immutable afterwards.
    using BodyRef = std::shared_ptr<const std::vector<std::uint8_t>>;

private:
    struct ActiveSnapshot {
        std::uint64_t generation = 0;
        std::size_t count = 0;
    };

    // Tickets only weakly reference this control block. If the cache is
    // destroyed first, ticket destruction becomes a no-op instead of
    // dereferencing the former owning ChunkBodyCache object.
    struct SnapshotState {
        std::mutex mutex;
        std::unordered_map<Key, ActiveSnapshot,
                           chunk_io_detail::DimensionChunkKeyHash>
            activeSnapshots;
    };

public:

    struct Stats {
        std::size_t hits = 0;
        std::size_t misses = 0;
        std::size_t size = 0;
        // Retained vector capacity, not just the serialized payload length.
        std::size_t bytes = 0;
        static constexpr std::size_t kMax = 1024;
        static constexpr std::size_t kMaxBytes = 64u * 1024u * 1024u;

        double hitRate() const noexcept {
            const auto total = hits + misses;
            return total == 0 ? 0.0
                : static_cast<double>(hits) / static_cast<double>(total);
        }
    };

    // Publish callbacks are ordered against mutations for the same chunk,
    // but independent chunks need not wait for an unrelated queue insertion.
    static constexpr std::size_t kOrderingStripeCount = 256;

    class SnapshotTicket {
    public:
        SnapshotTicket() = default;
        SnapshotTicket(const SnapshotTicket&) = delete;
        SnapshotTicket& operator=(const SnapshotTicket&) = delete;

        SnapshotTicket(SnapshotTicket&& other) noexcept
            : state_(std::move(other.state_)), key_(other.key_),
              generation_(other.generation_) {
            other.state_.reset();
        }

        SnapshotTicket& operator=(SnapshotTicket&& other) noexcept {
            if (this == &other) return *this;
            release();
            state_ = std::move(other.state_);
            other.state_.reset();
            key_ = other.key_;
            generation_ = other.generation_;
            return *this;
        }

        ~SnapshotTicket() { release(); }

        bool ownerAlive() const noexcept { return !state_.expired(); }

    private:
        friend class ChunkBodyCache;
        SnapshotTicket(std::weak_ptr<SnapshotState> state, Key key,
                       std::uint64_t generation) noexcept
            : state_(std::move(state)), key_(key), generation_(generation) {}

        void release() noexcept {
            const auto state = state_.lock();
            state_.reset();
            if (!state) return;
            ChunkBodyCache::finishSnapshot(state, key_);
        }

        std::weak_ptr<SnapshotState> state_;
        Key key_{};
        std::uint64_t generation_ = 0;
    };

    explicit ChunkBodyCache(std::size_t maxEntries = Stats::kMax,
                            std::size_t maxBytes = Stats::kMaxBytes)
        : maxEntries_(maxEntries), maxBytes_(maxBytes) {
        if (maxEntries_ == 0 || maxBytes_ == 0)
            throw std::invalid_argument(
                "chunk-body cache limits must be non-zero");
    }
    ChunkBodyCache(const ChunkBodyCache&) = delete;
    ChunkBodyCache& operator=(const ChunkBodyCache&) = delete;

    static Key key(std::int8_t dimension, std::int32_t chunkX,
                   std::int32_t chunkZ) noexcept {
        return chunk_io_detail::dimensionChunkKey(dimension, chunkX, chunkZ);
    }

    double hitRate() const noexcept {
        const auto hits = hits_.load(std::memory_order_relaxed);
        const auto misses = misses_.load(std::memory_order_relaxed);
        const auto total = hits + misses;
        return total == 0 ? 0.0
            : static_cast<double>(hits) / static_cast<double>(total);
    }

    Stats stats() const {
        std::lock_guard lock(snapshotState_->mutex);
        return {hits_.load(std::memory_order_relaxed),
                misses_.load(std::memory_order_relaxed), entries_.size(),
                cachedBytes_};
    }

    bool get(std::int8_t dimension, std::int32_t chunkX,
             std::int32_t chunkZ, BodyRef& out) {
        const auto chunkKey = key(dimension, chunkX, chunkZ);
        std::lock_guard lock(snapshotState_->mutex);
        auto entry = entries_.find(chunkKey);
        if (entry == entries_.end()) {
            misses_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        hits_.fetch_add(1, std::memory_order_relaxed);
        touchLocked(entry);
        out = entry->second.body;
        return true;
    }

    bool contains(std::int8_t dimension, std::int32_t chunkX,
                  std::int32_t chunkZ) const {
        const auto chunkKey = key(dimension, chunkX, chunkZ);
        std::lock_guard lock(snapshotState_->mutex);
        return entries_.find(chunkKey) != entries_.end();
    }

    SnapshotTicket beginSnapshot(std::int8_t dimension,
                                 std::int32_t chunkX,
                                 std::int32_t chunkZ) {
        const auto chunkKey = key(dimension, chunkX, chunkZ);
        std::lock_guard orderingLock(orderingMutexFor(chunkKey));
        std::lock_guard lock(snapshotState_->mutex);
        auto& active = snapshotState_->activeSnapshots[chunkKey];
        ++active.count;
        return SnapshotTicket(snapshotState_, chunkKey, active.generation);
    }

    template <typename Publish>
    bool publishCached(const SnapshotTicket& ticket, Publish&& publish) {
        std::lock_guard orderingLock(orderingMutexFor(ticket.key_));
        BodyRef publishedBody;
        {
            std::lock_guard lock(snapshotState_->mutex);
            if (!snapshotCurrentLocked(ticket)) return false;

            auto entry = entries_.find(ticket.key_);
            if (entry == entries_.end()) {
                misses_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

            hits_.fetch_add(1, std::memory_order_relaxed);
            touchLocked(entry);
            publishedBody = entry->second.body;
        }
        // Keep only the per-key ordering stripe while invoking external code.
        // Callbacks must be bounded queue insertions and must not re-enter this
        // cache; the global metadata lock is intentionally not held here.
        std::invoke(std::forward<Publish>(publish),
                    std::move(publishedBody));
        return true;
    }

    template <typename Publish>
    bool publish(const SnapshotTicket& ticket, BodyRef body,
                 Publish&& publishBody) {
        std::lock_guard orderingLock(orderingMutexFor(ticket.key_));
        BodyRef publishedBody;
        {
            std::lock_guard lock(snapshotState_->mutex);
            if (!snapshotCurrentLocked(ticket)) return false;

            auto entry = entries_.find(ticket.key_);
            if (entry == entries_.end()) {
                publishedBody = body;
                storeLocked(ticket.key_, std::move(body));
            } else {
                hits_.fetch_add(1, std::memory_order_relaxed);
                touchLocked(entry);
                publishedBody = entry->second.body;
            }
        }

        // Mutations and other publications for this key wait until the full
        // body is enqueued, preserving same-connection FIFO ordering.
        std::invoke(std::forward<Publish>(publishBody),
                    std::move(publishedBody));
        return true;
    }

    bool storeForSnapshot(const SnapshotTicket& ticket, BodyRef body) {
        std::lock_guard orderingLock(orderingMutexFor(ticket.key_));
        std::lock_guard lock(snapshotState_->mutex);
        if (!snapshotCurrentLocked(ticket)) return false;
        storeLocked(ticket.key_, std::move(body));
        return true;
    }

    void store(std::int8_t dimension, std::int32_t chunkX,
               std::int32_t chunkZ, BodyRef body) {
        const auto chunkKey = key(dimension, chunkX, chunkZ);
        std::lock_guard orderingLock(orderingMutexFor(chunkKey));
        std::lock_guard lock(snapshotState_->mutex);
        invalidateActiveSnapshotsLocked(chunkKey);
        storeLocked(chunkKey, std::move(body));
    }

    void invalidate(std::int8_t dimension, std::int32_t chunkX,
                    std::int32_t chunkZ) {
        const auto chunkKey = key(dimension, chunkX, chunkZ);
        std::lock_guard orderingLock(orderingMutexFor(chunkKey));
        std::lock_guard lock(snapshotState_->mutex);
        invalidateActiveSnapshotsLocked(chunkKey);
        const auto entry = entries_.find(chunkKey);
        if (entry == entries_.end()) return;
        eraseEntryLocked(entry);
    }

    void clear() {
        std::array<std::unique_lock<OrderingMutex>, kOrderingStripeCount>
            orderingLocks;
        for (std::size_t i = 0; i < orderingMutexes_.size(); ++i)
            orderingLocks[i] =
                std::unique_lock<OrderingMutex>(orderingMutexes_[i]);
        std::lock_guard lock(snapshotState_->mutex);
        for (auto& [key, active] : snapshotState_->activeSnapshots) {
            (void)key;
            ++active.generation;
        }
        entries_.clear();
        lru_.clear();
        cachedBytes_ = 0;
    }

    std::size_t size() const {
        std::lock_guard lock(snapshotState_->mutex);
        return entries_.size();
    }

    std::size_t bytes() const {
        std::lock_guard lock(snapshotState_->mutex);
        return cachedBytes_;
    }

    std::size_t hits() const noexcept {
        return hits_.load(std::memory_order_relaxed);
    }

    std::size_t misses() const noexcept {
        return misses_.load(std::memory_order_relaxed);
    }

private:
    class OrderingMutex {
    public:
        void lock() {
            if (mutex_.try_lock()) return;
            waiters_.fetch_add(1, std::memory_order_acq_rel);
            try {
                mutex_.lock();
            } catch (...) {
                waiters_.fetch_sub(1, std::memory_order_release);
                throw;
            }
            waiters_.fetch_sub(1, std::memory_order_release);
        }

        bool try_lock() { return mutex_.try_lock(); }
        void unlock() { mutex_.unlock(); }

        std::size_t waitingCount() const noexcept {
            return waiters_.load(std::memory_order_acquire);
        }

    private:
        std::mutex mutex_;
        std::atomic<std::size_t> waiters_{0};
    };

    struct CachedChunk {
        BodyRef body;
        std::list<Key>::iterator lruPosition;
    };

    using EntryMap = std::unordered_map<Key, CachedChunk,
                                        chunk_io_detail::DimensionChunkKeyHash>;

    OrderingMutex& orderingMutexFor(const Key& chunkKey) noexcept {
        const auto stripe = chunk_io_detail::DimensionChunkKeyHash{}(chunkKey) %
                            kOrderingStripeCount;
        return orderingMutexes_[stripe];
    }

    void touchLocked(EntryMap::iterator entry) {
        lru_.splice(lru_.begin(), lru_, entry->second.lruPosition);
        entry->second.lruPosition = lru_.begin();
    }

    bool snapshotCurrentLocked(const SnapshotTicket& ticket) const {
        const auto state = ticket.state_.lock();
        if (!state || state != snapshotState_) return false;
        const auto active = snapshotState_->activeSnapshots.find(ticket.key_);
        return active != snapshotState_->activeSnapshots.end() &&
               active->second.count != 0 &&
               active->second.generation == ticket.generation_;
    }

    void invalidateActiveSnapshotsLocked(const Key& chunkKey) {
        const auto active = snapshotState_->activeSnapshots.find(chunkKey);
        if (active != snapshotState_->activeSnapshots.end())
            ++active->second.generation;
    }

    static void finishSnapshot(const std::shared_ptr<SnapshotState>& state,
                               const Key& chunkKey) noexcept {
        std::lock_guard lock(state->mutex);
        const auto active = state->activeSnapshots.find(chunkKey);
        if (active == state->activeSnapshots.end()) return;
        if (active->second.count > 1) {
            --active->second.count;
            return;
        }
        state->activeSnapshots.erase(active);
    }

    void eraseEntryLocked(EntryMap::iterator entry) {
        if (entry->second.body)
            cachedBytes_ -= entry->second.body->capacity();
        lru_.erase(entry->second.lruPosition);
        entries_.erase(entry);
    }

    bool evictLeastRecentlyUsedExceptLocked(const Key& protectedKey) {
        for (auto candidate = lru_.rbegin(); candidate != lru_.rend();
             ++candidate) {
            if (*candidate == protectedKey) continue;
            const auto entry = entries_.find(*candidate);
            if (entry == entries_.end()) continue;
            eraseEntryLocked(entry);
            return true;
        }
        return false;
    }

    void evictLeastRecentlyUsedLocked() {
        if (lru_.empty()) return;
        const auto entry = entries_.find(lru_.back());
        if (entry != entries_.end()) eraseEntryLocked(entry);
    }

    void storeLocked(const Key& chunkKey, BodyRef body) {
        const auto bodyBytes = body ? body->capacity() : 0;
        auto entry = entries_.find(chunkKey);
        if (bodyBytes > maxBytes_) {
            // A body that cannot fit must not leave older content cached for
            // the same key, since that value is now stale.
            if (entry != entries_.end()) eraseEntryLocked(entry);
            return;
        }

        if (entry != entries_.end()) {
            const auto oldBytes = entry->second.body
                ? entry->second.body->capacity() : 0;
            while (cachedBytes_ - oldBytes > maxBytes_ - bodyBytes) {
                if (!evictLeastRecentlyUsedExceptLocked(chunkKey))
                    throw std::logic_error(
                        "chunk cache byte accounting cannot satisfy replacement");
            }
            entry->second.body = std::move(body);
            cachedBytes_ = cachedBytes_ - oldBytes + bodyBytes;
            touchLocked(entry);
            return;
        }

        // Allocate both metadata nodes before evicting old entries. If either
        // allocation fails, the new LRU node is rolled back and old cache
        // entries remain intact.
        lru_.push_front(chunkKey);
        try {
            const auto [inserted, didInsert] = entries_.emplace(
                chunkKey, CachedChunk{std::move(body), lru_.begin()});
            (void)inserted;
            if (!didInsert) {
                lru_.pop_front();
                return;
            }
        } catch (...) {
            lru_.pop_front();
            throw;
        }

        while (cachedBytes_ > maxBytes_ - bodyBytes)
            evictLeastRecentlyUsedExceptLocked(chunkKey);
        cachedBytes_ += bodyBytes;
        while (entries_.size() > maxEntries_)
            evictLeastRecentlyUsedLocked();
    }

    std::shared_ptr<SnapshotState> snapshotState_ =
        std::make_shared<SnapshotState>();
    std::array<OrderingMutex, kOrderingStripeCount> orderingMutexes_{};
    EntryMap entries_;
    std::list<Key> lru_; // MRU at front; LRU at back.
    std::size_t cachedBytes_ = 0;
    const std::size_t maxEntries_;
    const std::size_t maxBytes_;
    std::atomic<std::size_t> hits_{0};
    std::atomic<std::size_t> misses_{0};
};

} // namespace cppfm
