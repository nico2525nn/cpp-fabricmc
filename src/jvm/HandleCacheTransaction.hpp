#pragma once

#include <mutex>
#include <utility>

namespace cppfm::jvm {

// Serialize handle invalidation and removal of its cached Java wrapper with
// wrapperForHandle's validity/cache transactions. Callers acquire any runtime
// lifecycle lease before entering this function to preserve lease -> cache
// lock order.
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
