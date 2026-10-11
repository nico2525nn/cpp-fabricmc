#include "game/ChunkBodyCache.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace cppfm {

struct ChunkBodyCacheTestAccess {
    static std::size_t orderingWaiters(ChunkBodyCache& cache,
                                       std::int8_t dimension,
                                       std::int32_t chunkX,
                                       std::int32_t chunkZ) {
        return cache.orderingMutexFor(
            ChunkBodyCache::key(dimension, chunkX, chunkZ)).waitingCount();
    }
};

} // namespace cppfm

namespace {

int assertions = 0;
int failures = 0;

void check(bool condition, const char* message) {
    ++assertions;
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", message);
}

cppfm::ChunkBodyCache::BodyRef body(std::initializer_list<std::uint8_t> bytes) {
    return std::make_shared<const std::vector<std::uint8_t>>(bytes);
}

cppfm::ChunkBodyCache::BodyRef bodyOfSize(std::size_t size) {
    return std::make_shared<const std::vector<std::uint8_t>>(size, 0x42);
}

cppfm::ChunkBodyCache::BodyRef bodyWithCapacity(std::size_t size,
                                                std::size_t capacity) {
    auto owned = std::make_shared<std::vector<std::uint8_t>>(size, 0x42);
    owned->reserve(capacity);
    return owned;
}

void testDimensionIsolationAndMetrics() {
    cppfm::ChunkBodyCache cache;
    const auto overworld = body({0x01});
    const auto nether = body({0x02, 0x03});
    cache.store(0, 8, -4, overworld);
    cache.store(-1, 8, -4, nether);

    cppfm::ChunkBodyCache::BodyRef found;
    check(cache.get(0, 8, -4, found) && found == overworld,
          "cache keeps the Overworld entry at a shared coordinate");
    check(cache.get(-1, 8, -4, found) && found == nether,
          "cache keeps the Nether entry at a shared coordinate");
    cache.invalidate(-1, 8, -4);
    check(!cache.get(-1, 8, -4, found),
          "dimension-scoped invalidation removes only the Nether entry");
    check(cache.get(0, 8, -4, found) && found == overworld,
          "dimension-scoped invalidation preserves the Overworld entry");

    const auto stats = cache.stats();
    check(stats.size == 1 && stats.hits == 3 && stats.misses == 1,
          "cache metrics count successful and missing lookups consistently");
    check(stats.hitRate() == 0.75,
          "cache hit-rate derives from the same lookup counters");
    check(cache.bytes() == overworld->capacity(),
          "cache byte accounting includes retained encoded-body capacity");
}

void testSnapshotInvalidationAndTicketLifetime() {
    cppfm::ChunkBodyCache cache;
    const auto original = body({0x11});
    const auto replacement = body({0x22});
    cache.store(0, 3, 5, original);

    auto first = cache.beginSnapshot(0, 3, 5);
    auto second = cache.beginSnapshot(0, 3, 5);
    first = cppfm::ChunkBodyCache::SnapshotTicket{};
    check(cache.storeForSnapshot(second, replacement),
          "releasing one ticket does not invalidate another active ticket");

    auto stale = cache.beginSnapshot(0, 3, 5);
    cache.invalidate(0, 3, 5);
    bool callbackRan = false;
    check(!cache.publish(stale, original,
                         [&](const auto&) { callbackRan = true; }) &&
              !callbackRan,
          "invalidation rejects stale snapshots before their publish callback");
    check(cache.size() == 0,
          "rejected stale publication does not reinsert the removed entry");

    auto cleared = cache.beginSnapshot(0, 3, 5);
    cache.clear();
    check(!cache.storeForSnapshot(cleared, original),
          "clear invalidates in-flight snapshots as well as cached bodies");

    cache.store(0, 3, 5, original);
    auto current = cache.beginSnapshot(0, 3, 5);
    bool publishedExpectedBody = false;
    check(cache.publishCached(current, [&](const auto& published) {
              publishedExpectedBody = published == original;
          }) && publishedExpectedBody,
          "current cached snapshot publishes the retained encoded body");
}

void testCallbackExceptionReleasesCacheLock() {
    cppfm::ChunkBodyCache cache;
    cache.store(0, 0, 0, body({0x7F}));
    auto ticket = cache.beginSnapshot(0, 0, 0);
    bool caught = false;
    try {
        (void)cache.publishCached(ticket, [](const auto&) {
            throw std::runtime_error("synthetic queue failure");
        });
    } catch (const std::runtime_error&) {
        caught = true;
    }
    check(caught, "a publication callback exception propagates to its owner");
    cache.invalidate(0, 0, 0);
    check(cache.size() == 0,
          "an exceptional callback releases the lock and permits invalidation");
}

void testPublisherDoesNotHoldGlobalCacheLock() {
    using namespace cppfm;
    ChunkBodyCache cache;
    const auto original = body({0x31});
    cache.store(0, 0, 0, original);
    auto ticket = cache.beginSnapshot(0, 0, 0);

    std::atomic<bool> callbackEntered{false};
    std::atomic<bool> releaseCallback{false};
    std::atomic<bool> callbackFinished{false};
    std::atomic<bool> published{false};
    std::thread publisher([&] {
        published.store(cache.publishCached(ticket, [&](const auto& found) {
            check(found == original,
                  "publisher receives the body captured under cache lock");
            callbackEntered.store(true, std::memory_order_release);
            while (!releaseCallback.load(std::memory_order_acquire))
                std::this_thread::yield();
            callbackFinished.store(true, std::memory_order_release);
        }), std::memory_order_release);
    });

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (!callbackEntered.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    std::atomic<bool> statsFinished{false};
    std::thread statsReader([&] {
        (void)cache.stats();
        statsFinished.store(true, std::memory_order_release);
    });
    const auto statsDeadline = std::chrono::steady_clock::now() +
                               std::chrono::seconds(2);
    while (!statsFinished.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < statsDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool unrelatedCacheReadProgressed =
        statsFinished.load(std::memory_order_acquire);

    std::atomic<bool> invalidationFinished{false};
    std::atomic<bool> invalidationSawCompletedPublish{false};
    std::thread invalidator([&] {
        cache.invalidate(0, 0, 0);
        invalidationSawCompletedPublish.store(
            callbackFinished.load(std::memory_order_acquire),
            std::memory_order_release);
        invalidationFinished.store(true, std::memory_order_release);
    });
    const auto invalidationDeadline = std::chrono::steady_clock::now() +
                                     std::chrono::seconds(2);
    while (ChunkBodyCacheTestAccess::orderingWaiters(cache, 0, 0, 0) == 0 &&
           std::chrono::steady_clock::now() < invalidationDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool invalidationReachedOrderingLock =
        ChunkBodyCacheTestAccess::orderingWaiters(cache, 0, 0, 0) != 0;
    const bool invalidationWaitedForPublisher =
        invalidationReachedOrderingLock &&
        !invalidationFinished.load(std::memory_order_acquire) &&
        !callbackFinished.load(std::memory_order_acquire);

    releaseCallback.store(true, std::memory_order_release);
    publisher.join();
    statsReader.join();
    invalidator.join();

    check(callbackEntered.load(std::memory_order_acquire),
          "publication callback was entered");
    check(unrelatedCacheReadProgressed,
          "cache statistics progress while the external publish callback is blocked");
    check(invalidationWaitedForPublisher &&
              invalidationSawCompletedPublish.load(std::memory_order_acquire),
          "same-chunk invalidation waits until full chunk publication finishes");
    check(published.load(std::memory_order_acquire) && cache.size() == 0,
          "serialized publication then invalidation leaves no stale cached body");
}

void testByteBudgetEvictsAndRejectsOversizedBodies() {
    using namespace cppfm;
    const auto first = bodyOfSize(2);
    const auto second = bodyOfSize(2);
    const auto incoming = bodyOfSize(3);
    const auto firstBytes = first->capacity();
    const auto secondBytes = second->capacity();
    const auto incomingBytes = incoming->capacity();
    const auto byteBudget = std::max(firstBytes + secondBytes,
                                     firstBytes + incomingBytes);
    ChunkBodyCache cache(3, byteBudget);
    cache.store(0, 0, 0, first);
    cache.store(0, 1, 0, second);

    ChunkBodyCache::BodyRef found;
    check(cache.get(0, 0, 0, found),
          "byte-budget fixture promotes its retained LRU entry");
    cache.store(0, 2, 0, incoming);
    check(cache.size() == 2 &&
              cache.bytes() == firstBytes + incomingBytes &&
              cache.get(0, 0, 0, found) &&
              !cache.contains(0, 1, 0) && cache.contains(0, 2, 0),
          "byte pressure evicts the least-recently-used body under both cache caps");

    const auto oversized = bodyWithCapacity(1, byteBudget + 1);
    check(oversized->size() <= byteBudget &&
              oversized->capacity() > byteBudget,
          "oversize fixture has a small payload but capacity beyond budget");
    cache.store(0, 3, 0, oversized);
    check(!cache.contains(0, 3, 0) &&
              cache.bytes() == firstBytes + incomingBytes,
          "a single body larger than the cache budget is published uncached");
    cache.store(0, 2, 0, oversized);
    check(!cache.contains(0, 2, 0) && cache.bytes() == firstBytes,
          "oversized replacement evicts stale cached content for that key");
    check(cache.stats().bytes == cache.bytes(),
          "cache statistics expose the same bounded byte accounting");
}

void testSnapshotTicketMayOutliveCache() {
    using cppfm::ChunkBodyCache;
    ChunkBodyCache::SnapshotTicket ticket;
    bool ticketBoundWhileCacheAlive = false;
    {
        auto cache = std::make_unique<ChunkBodyCache>();
        ticket = cache->beginSnapshot(0, 4, -2);
        ticketBoundWhileCacheAlive = ticket.ownerAlive();
    }

    const bool expiresWithItsCache = !ticket.ownerAlive();
    ticket = ChunkBodyCache::SnapshotTicket{};
    check(ticketBoundWhileCacheAlive && expiresWithItsCache,
          "snapshot ticket safely detaches when its cache is destroyed first");
}

void testLruCapacityAndRecency() {
    cppfm::ChunkBodyCache cache;
    for (std::int32_t x = 0;
         x < static_cast<std::int32_t>(cppfm::ChunkBodyCache::Stats::kMax);
         ++x) {
        cache.store(0, x, 0, body({0x01}));
    }

    cppfm::ChunkBodyCache::BodyRef found;
    check(cache.get(0, 0, 0, found),
          "reading a retained body promotes it to most-recently-used");
    cache.store(0, static_cast<std::int32_t>(cppfm::ChunkBodyCache::Stats::kMax),
                0, body({0x02}));
    check(cache.size() == cppfm::ChunkBodyCache::Stats::kMax,
          "cache size remains bounded at its documented capacity");
    check(cache.get(0, 0, 0, found),
          "LRU eviction retains the recently touched body");
    check(!cache.get(0, 1, 0, found),
          "LRU eviction removes the least recently used body");
}

} // namespace

int main() {
    testDimensionIsolationAndMetrics();
    testSnapshotInvalidationAndTicketLifetime();
    testCallbackExceptionReleasesCacheLock();
    testPublisherDoesNotHoldGlobalCacheLock();
    testByteBudgetEvictsAndRejectsOversizedBodies();
    testSnapshotTicketMayOutliveCache();
    testLruCapacityAndRecency();
    std::printf("ChunkBodyCache: %d assertions, %d failures\n", assertions,
                failures);
    return failures == 0 ? 0 : 1;
}
