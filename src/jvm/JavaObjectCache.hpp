// JVM global-reference cache keyed by the same opaque native handles used by
// the C++ bridge.  The implementation intentionally hides jobject/JNI from the
// rest of the game server.
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace cppfm::jvm {

class JavaObjectCache {
public:
    // The two-argument form is retained for native-only tests and callers that
    // transfer ownership of an already-created reference.  Runtime code should
    // use the env-aware form so replacement releases the old global reference.
    void put(std::uint64_t handle, void* globalReference);
    void put(void* rawEnv, std::uint64_t handle, void* globalReference);
    void put(void* rawEnv, std::uint64_t handle, const std::string& typeName,
             void* globalReference);
    // Execute a short operation while the cache entry is protected from
    // replacement/deletion.  Returning a jobject from a plain get() would
    // leave a use-after-DeleteGlobalRef window between the mutex unlock and
    // the JNI call that consumes it.
    template <typename Callback>
    bool with(std::uint64_t handle, const std::string& typeName,
              Callback&& callback) const {
        std::lock_guard lock(mutex_);
        const auto it = references_.find(Key{handle, typeName});
        if (it == references_.end()) return false;
        std::forward<Callback>(callback)(it->second);
        return true;
    }
    void erase(std::uint64_t handle);
    void erase(void* rawEnv, std::uint64_t handle);
    std::size_t size() const;
    void clear(void* jniEnv);

private:
    struct Key {
        std::uint64_t handle = 0;
        std::string typeName;
        bool operator==(const Key& other) const noexcept {
            return handle == other.handle && typeName == other.typeName;
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept {
            return std::hash<std::uint64_t>{}(key.handle) ^
                   (std::hash<std::string>{}(key.typeName) << 1);
        }
    };

    mutable std::mutex mutex_;
    std::unordered_map<Key, void*, KeyHash> references_;
};

} // namespace cppfm::jvm
