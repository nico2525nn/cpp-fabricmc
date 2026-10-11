// Thread-safe membership and enumeration index for block-entity record keys.
// The owning BlockEntityStore serializes payload publication with this index.
#pragma once

#include "BlockEntityPosition.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cppfm {

class BlockEntityChunkIndex final {
public:
    BlockEntityChunkIndex(std::size_t maxRecordsPerChunk,
                          std::size_t maxRecords)
        : maxRecordsPerChunk_(maxRecordsPerChunk),
          maxRecords_(maxRecords) {
        if (maxRecordsPerChunk_ == 0 || maxRecords_ == 0 ||
            maxRecordsPerChunk_ > maxRecords_)
            throw std::invalid_argument(
                "invalid block-entity chunk-index limits");
    }

    static std::int64_t chunkKey(std::int32_t chunkX,
                                 std::int32_t chunkZ) noexcept {
        return posKey(chunkX, 0, chunkZ);
    }

    static std::int64_t chunkKey(std::int64_t recordKey) noexcept {
        return chunkKey(posKeyUnpackX(recordKey) >> 4,
                        posKeyUnpackZ(recordKey) >> 4);
    }

    bool contains(std::int64_t recordKey) const {
        std::lock_guard lock(mutex_);
        return containsLocked(recordKey);
    }

    std::size_t recordCount(std::int64_t chunk) const {
        std::lock_guard lock(mutex_);
        const auto records = chunks_.find(chunk);
        return records == chunks_.end() ? 0 : records->second.liveCount;
    }

    std::size_t liveRecordCount() const {
        std::lock_guard lock(mutex_);
        return liveRecordCount_;
    }

    bool canAddRecord(std::int64_t recordKey) const {
        std::lock_guard lock(mutex_);
        return canAddRecordLocked(recordKey);
    }

    // Reserve every potentially allocating membership/vector operation before
    // BlockEntityStore starts its no-partial-publication commit.
    void reserveForKey(std::int64_t recordKey) {
        std::lock_guard lock(mutex_);
        reserveForKeyLocked(recordKey);
    }

    void reserveForKeys(const std::unordered_set<std::int64_t>& recordKeys) {
        std::lock_guard lock(mutex_);
        std::unordered_map<std::int64_t, std::size_t> additionsByChunk;
        additionsByChunk.reserve(recordKeys.size());
        std::vector<std::pair<std::int64_t, std::int64_t>> additions;
        additions.reserve(recordKeys.size());
        for (const auto recordKey : recordKeys) {
            const auto chunk = chunkKey(recordKey);
            const auto records = chunks_.find(chunk);
            if (records != chunks_.end() &&
                records->second.membership.contains(recordKey))
                continue;
            ++additionsByChunk[chunk];
            additions.emplace_back(chunk, recordKey);
        }
        if (additions.empty()) return;

        std::vector<std::int64_t> createdChunks;
        createdChunks.reserve(additionsByChunk.size());
        chunks_.reserve(chunks_.size() + additionsByChunk.size());
        try {
            for (const auto& [chunk, count] : additionsByChunk) {
                auto [records, inserted] = chunks_.try_emplace(chunk);
                if (inserted) createdChunks.push_back(chunk);
                auto& chunkRecords = records->second;
                const auto reservedCount =
                    chunkRecords.membership.size() - chunkRecords.liveCount;
                chunkRecords.orderedKeys.reserve(
                    chunkRecords.orderedKeys.size() + reservedCount + count);
                chunkRecords.membership.reserve(
                    chunkRecords.membership.size() + count);
            }

            // Placeholder nodes make add() allocation-free. If node
            // allocation fails partway, remove only this call's placeholders
            // before exposing the exception.
            for (const auto& [chunk, recordKey] : additions) {
                auto& membership = chunks_.find(chunk)->second.membership;
                membership.try_emplace(recordKey, Entry{});
            }
        } catch (...) {
            for (const auto& [chunk, recordKey] : additions) {
                const auto records = chunks_.find(chunk);
                if (records == chunks_.end()) continue;
                const auto entry = records->second.membership.find(recordKey);
                if (entry != records->second.membership.end() &&
                    !entry->second.live)
                    records->second.membership.erase(entry);
            }
            for (const auto chunk : createdChunks) {
                const auto records = chunks_.find(chunk);
                if (records != chunks_.end() &&
                    records->second.membership.empty() &&
                    records->second.orderedKeys.empty())
                    chunks_.erase(records);
            }
            throw;
        }
    }

    // Discard uncommitted capacity after the owning store's later allocations
    // fail. Live records are deliberately untouched; the owner serializes
    // these rollback calls with reserve/add publication.
    void cancelReservation(std::int64_t recordKey) {
        std::lock_guard lock(mutex_);
        cancelReservationLocked(recordKey);
    }

    void cancelReservations(
        const std::unordered_set<std::int64_t>& recordKeys) {
        std::lock_guard lock(mutex_);
        for (const auto recordKey : recordKeys)
            cancelReservationLocked(recordKey);
    }

    bool add(std::int64_t recordKey) {
        std::lock_guard lock(mutex_);
        if (containsLocked(recordKey) || !canAddRecordLocked(recordKey))
            return false;
        const auto chunk = chunkKey(recordKey);
        const auto records = chunks_.find(chunk);
        if (records == chunks_.end()) return false;
        const auto entry = records->second.membership.find(recordKey);
        if (entry == records->second.membership.end()) return false;

        auto& chunkRecords = records->second;
        const auto orderedIndex = chunkRecords.orderedKeys.size();
        chunkRecords.orderedKeys.push_back(recordKey);
        entry->second = Entry{orderedIndex, true};
        ++chunkRecords.liveCount;
        ++liveRecordCount_;
        return true;
    }

    bool remove(std::int64_t recordKey) {
        std::lock_guard lock(mutex_);
        const auto records = chunks_.find(chunkKey(recordKey));
        if (records == chunks_.end()) return false;
        auto& chunkRecords = records->second;
        const auto entry = chunkRecords.membership.find(recordKey);
        if (entry == chunkRecords.membership.end() || !entry->second.live)
            return false;

        const auto removedIndex = entry->second.orderedIndex;
        const auto lastKey = chunkRecords.orderedKeys.back();
        if (removedIndex + 1 != chunkRecords.orderedKeys.size()) {
            chunkRecords.orderedKeys[removedIndex] = lastKey;
            chunkRecords.membership.find(lastKey)->second.orderedIndex =
                removedIndex;
        }
        chunkRecords.orderedKeys.pop_back();
        chunkRecords.membership.erase(entry);
        --chunkRecords.liveCount;
        --liveRecordCount_;
        if (chunkRecords.membership.empty()) chunks_.erase(records);
        return true;
    }

    std::vector<std::int64_t> keysForChunk(std::int32_t chunkX,
                                           std::int32_t chunkZ) const {
        std::lock_guard lock(mutex_);
        const auto records = chunks_.find(chunkKey(chunkX, chunkZ));
        return records == chunks_.end()
            ? std::vector<std::int64_t>{} : records->second.orderedKeys;
    }

    std::optional<std::vector<std::int64_t>> takeChunk(
        std::int32_t chunkX, std::int32_t chunkZ) {
        std::lock_guard lock(mutex_);
        const auto records = chunks_.find(chunkKey(chunkX, chunkZ));
        if (records == chunks_.end()) return std::nullopt;
        auto keys = std::move(records->second.orderedKeys);
        liveRecordCount_ -= records->second.liveCount;
        chunks_.erase(records);
        return keys;
    }

private:
    struct Entry {
        std::size_t orderedIndex = 0;
        bool live = false;
    };

    struct ChunkRecords {
        std::vector<std::int64_t> orderedKeys;
        std::unordered_map<std::int64_t, Entry> membership;
        std::size_t liveCount = 0;
    };

    bool containsLocked(std::int64_t recordKey) const {
        const auto records = chunks_.find(chunkKey(recordKey));
        if (records == chunks_.end()) return false;
        const auto entry = records->second.membership.find(recordKey);
        return entry != records->second.membership.end() && entry->second.live;
    }

    bool canAddRecordLocked(std::int64_t recordKey) const {
        const auto records = chunks_.find(chunkKey(recordKey));
        const auto chunkCount = records == chunks_.end()
            ? std::size_t{0} : records->second.liveCount;
        return chunkCount < maxRecordsPerChunk_ &&
               liveRecordCount_ < maxRecords_;
    }

    void cancelReservationLocked(std::int64_t recordKey) {
        const auto records = chunks_.find(chunkKey(recordKey));
        if (records == chunks_.end()) return;
        auto& chunkRecords = records->second;
        const auto entry = chunkRecords.membership.find(recordKey);
        if (entry == chunkRecords.membership.end() || entry->second.live)
            return;
        chunkRecords.membership.erase(entry);
        if (chunkRecords.membership.empty() &&
            chunkRecords.orderedKeys.empty())
            chunks_.erase(records);
    }

    void reserveForKeyLocked(std::int64_t recordKey) {
        const auto chunk = chunkKey(recordKey);
        auto [records, inserted] = chunks_.try_emplace(chunk);
        auto& chunkRecords = records->second;
        if (chunkRecords.membership.contains(recordKey)) return;

        try {
            const auto reservedCount =
                chunkRecords.membership.size() - chunkRecords.liveCount;
            chunkRecords.orderedKeys.reserve(
                chunkRecords.orderedKeys.size() + reservedCount + 1);
            chunkRecords.membership.reserve(
                chunkRecords.membership.size() + 1);
            chunkRecords.membership.try_emplace(recordKey, Entry{});
        } catch (...) {
            if (inserted && chunkRecords.membership.empty() &&
                chunkRecords.orderedKeys.empty())
                chunks_.erase(records);
            throw;
        }
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::int64_t, ChunkRecords> chunks_;
    std::size_t liveRecordCount_ = 0;
    const std::size_t maxRecordsPerChunk_;
    const std::size_t maxRecords_;
};

} // namespace cppfm
