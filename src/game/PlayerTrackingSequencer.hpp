#pragma once

#include <exception>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace cppfm {

// Entity spawn/removal packets describe a client's tracked-entity set. Keep a
// snapshot and its corresponding enqueue in the same serialized publication
// section so a delayed join snapshot cannot overtake a later removal.
class PlayerTrackingSequencer final {
public:
    class PublicationLease final {
    public:
        PublicationLease(const PublicationLease&) = delete;
        PublicationLease& operator=(const PublicationLease&) = delete;

        // External retirement callbacks may re-enter tracking publication.
        // Suspend only around that callback; callers resume the same ordered
        // publication before committing state or enqueueing related packets.
        template <typename Operation>
        void suspend(Operation&& operation) {
            if (!lock_.owns_lock())
                throw std::logic_error(
                    "player tracking publication lease is not held");
            lock_.unlock();
            try {
                std::forward<Operation>(operation)();
            } catch (...) {
                reacquireOrTerminate();
                throw;
            }
            reacquireOrTerminate();
        }

    private:
        friend class PlayerTrackingSequencer;
        explicit PublicationLease(std::mutex& mutex) : lock_(mutex) {}

        void reacquireOrTerminate() noexcept {
            try {
                lock_.lock();
            } catch (...) {
                // Continuing after irreversible retirement without restoring
                // publication ordering would corrupt the roster/wire state.
                std::terminate();
            }
        }

        std::unique_lock<std::mutex> lock_;
    };

    template <typename Operation>
    void serialize(Operation&& operation) const {
        PublicationLease lease(mutex_);
        if constexpr (std::is_invocable_v<Operation, PublicationLease&>) {
            std::forward<Operation>(operation)(lease);
        } else {
            std::forward<Operation>(operation)();
        }
    }

private:
    mutable std::mutex mutex_;
};

} // namespace cppfm
