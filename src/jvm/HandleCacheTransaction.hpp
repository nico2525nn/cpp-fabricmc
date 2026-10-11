#pragma once

#include <mutex>
#include <utility>

namespace cppfm::jvm {

// Linearize invalidation with wrapper-cache reads and writes. The caller must
// acquire its runtime-lifecycle lease before entering this transaction.
template <typename Invalidate, typename Erase>
auto invalidateHandleAndEraseCache(std::mutex& objectCacheMutex,
                                   Invalidate&& invalidate,
                                   Erase&& erase) {
    std::lock_guard cacheLock(objectCacheMutex);
    auto handle = std::forward<Invalidate>(invalidate)();
    if (handle) std::forward<Erase>(erase)(*handle);
    return handle;
}

} // namespace cppfm::jvm
