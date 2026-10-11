#include "game/BlockEntityChunkIndex.hpp"

#include <cstdint>
#include <cstdio>
#include <unordered_set>

namespace {

int assertions = 0;
int failures = 0;

void check(bool condition, const char* message) {
    ++assertions;
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", message);
}

void testChunkGroupingAndRecordAccounting() {
    using namespace cppfm;
    BlockEntityChunkIndex index(2, 3);
    const auto first = posKey(1, 64, 1);
    const auto second = posKey(2, 70, 3);
    const auto third = posKey(17, 64, 1);
    std::unordered_set<std::int64_t> keys{first, second, third};
    index.reserveForKeys(keys);
    check(!index.contains(first),
          "capacity reservations are not visible as published records");

    check(index.add(first) && index.add(second),
          "reserved record keys enter the chunk index");
    check(index.contains(first) && index.contains(second),
          "index finds records in their owning chunk");
    check(index.recordCount(BlockEntityChunkIndex::chunkKey(0, 0)) == 2,
          "record count is scoped to one chunk");
    check(!index.canAddRecord(posKey(3, 64, 1)),
          "per-chunk record limit blocks another record");
    check(!index.add(first),
          "adding an already indexed key does not duplicate accounting");

    check(index.add(third), "another chunk has its own record capacity");
    check(!index.canAddRecord(posKey(33, 64, 1)),
          "global record limit applies across chunks");
    check(index.liveRecordCount() == 3,
          "global live count equals the sum of indexed records");

    check(index.remove(second) && !index.contains(second),
          "removal erases a record from its chunk index");
    check(!index.remove(second),
          "removing an absent record leaves live accounting unchanged");
    check(index.canAddRecord(posKey(3, 64, 1)),
          "removal returns one slot to the chunk and global budgets");
}

void testMaximumChunkCardinality() {
    using namespace cppfm;
    constexpr std::size_t kMaximumRecords = 16384;
    BlockEntityChunkIndex index(kMaximumRecords, kMaximumRecords);
    std::unordered_set<std::int64_t> keys;
    keys.reserve(kMaximumRecords);
    for (std::int32_t y = -64; y < 0; ++y) {
        for (std::int32_t z = 0; z < 16; ++z) {
            for (std::int32_t x = 0; x < 16; ++x)
                keys.insert(posKey(x, y, z));
        }
    }
    index.reserveForKeys(keys);
    std::size_t inserted = 0;
    for (const auto key : keys)
        inserted += static_cast<std::size_t>(index.add(key));

    const auto chunk = BlockEntityChunkIndex::chunkKey(0, 0);
    const auto taken = index.takeChunk(0, 0);
    check(keys.size() == kMaximumRecords && inserted == kMaximumRecords &&
              index.recordCount(chunk) == 0 && taken &&
              taken->size() == kMaximumRecords &&
              index.liveRecordCount() == 0,
          "membership and capacity accounting scale to the configured per-chunk maximum");
}

void testChunkTakeAndCoordinateIsolation() {
    using namespace cppfm;
    BlockEntityChunkIndex index(4, 8);
    const auto origin = posKey(-1, 0, -1);
    const auto adjacent = posKey(-17, 0, -1);
    index.reserveForKey(origin);
    index.reserveForKey(adjacent);
    check(index.add(origin) && index.add(adjacent),
          "negative-coordinate records can be indexed");

    auto taken = index.takeChunk(-1, -1);
    check(taken && taken->size() == 1 && taken->front() == origin,
          "taking a chunk returns only its record keys");
    check(index.liveRecordCount() == 1 && !index.contains(origin) &&
              index.contains(adjacent),
          "taking a chunk decrements only that chunk's live records");
    check(!index.takeChunk(-1, -1).has_value(),
          "an absent chunk is distinct from an indexed empty chunk");

    const auto emptyChunk = BlockEntityChunkIndex::chunkKey(5, 5);
    index.reserveForKey(posKey(80, 0, 80));
    auto empty = index.takeChunk(5, 5);
    check(empty && empty->empty() &&
              index.recordCount(emptyChunk) == 0,
          "reserved empty chunk entry can be removed without changing counts");
}

void testCancelledReservationsReleaseOnlyUnpublishedKeys() {
    using namespace cppfm;
    BlockEntityChunkIndex index(4, 8);
    const auto first = posKey(2, 64, 3);
    const auto second = posKey(18, 64, 3);
    const auto live = posKey(34, 64, 3);

    index.reserveForKey(first);
    index.cancelReservation(first);
    check(index.recordCount(BlockEntityChunkIndex::chunkKey(first)) == 0 &&
              index.keysForChunk(0, 0).empty() &&
              !index.takeChunk(0, 0).has_value(),
          "cancelling a failed single-key publication removes its empty chunk");

    const std::unordered_set<std::int64_t> batch{first, second};
    index.reserveForKeys(batch);
    index.cancelReservations(batch);
    check(index.liveRecordCount() == 0 &&
              index.recordCount(BlockEntityChunkIndex::chunkKey(first)) == 0 &&
              index.recordCount(BlockEntityChunkIndex::chunkKey(second)) == 0,
          "batch rollback removes all unpublished placeholders across chunks");

    index.reserveForKey(live);
    check(index.add(live), "live control record publishes from its reservation");
    index.cancelReservation(live);
    check(index.contains(live) &&
              index.recordCount(BlockEntityChunkIndex::chunkKey(live)) == 1,
          "reservation rollback never removes an already-published record");
}

} // namespace

int main() {
    testChunkGroupingAndRecordAccounting();
    testMaximumChunkCardinality();
    testChunkTakeAndCoordinateIsolation();
    testCancelledReservationsReleaseOnlyUnpublishedKeys();
    std::printf("BlockEntityChunkIndex: %d assertions, %d failures\n",
                assertions, failures);
    return failures == 0 ? 0 : 1;
}
