// JVM global-reference cache keyed by the same opaque native handles used by
// the C++ bridge.  The implementation intentionally hides jobject/JNI from the
// rest of the game server.
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace cppfm::jvm {

class JavaObjectCache {
public:
    void put(std::uint64_t handle, void* globalReference);
    void* get(std::uint64_t handle) const;
    void erase(std::uint64_t handle);
    std::size_t size() const;
    void clear(void* jniEnv);

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, void*> references_;
};

} // namespace cppfm::jvm
