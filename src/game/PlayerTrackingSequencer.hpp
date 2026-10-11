#pragma once

#include <mutex>
#include <utility>

namespace cppfm {

// Entity spawn/removal packets describe a client's tracked-entity set. Keep a
// snapshot and its corresponding enqueue in the same serialized publication
// section so a delayed join snapshot cannot overtake a later removal.
class PlayerTrackingSequencer final {
public:
    template <typename Operation>
    void serialize(Operation&& operation) const {
        std::lock_guard lock(mutex_);
        std::forward<Operation>(operation)();
    }

private:
    mutable std::mutex mutex_;
};

} // namespace cppfm
