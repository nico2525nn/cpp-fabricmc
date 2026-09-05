// JVM global-reference cache keyed by the same opaque native handles used by
// the C++ bridge.  The implementation intentionally hides jobject/JNI from the
// rest of the game server.
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

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
    void* get(std::uint64_t handle) const;
    void* get(std::uint64_t handle, const std::string& typeName) const;
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
