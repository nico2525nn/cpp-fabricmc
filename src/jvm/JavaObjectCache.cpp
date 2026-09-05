#include "JavaObjectCache.hpp"

#include <vector>

#if defined(CPPFM_HAS_JNI)
#include <jni.h>
#endif

namespace cppfm::jvm {

namespace {
void deleteGlobal(void* rawEnv, void* reference) noexcept {
#if defined(CPPFM_HAS_JNI)
    if (rawEnv && reference)
        static_cast<JNIEnv*>(rawEnv)->DeleteGlobalRef(static_cast<jobject>(reference));
#else
    (void)rawEnv;
    (void)reference;
#endif
}
}

void JavaObjectCache::put(std::uint64_t handle, void* globalReference) {
    if (!handle || !globalReference) return;
    std::lock_guard lock(mutex_);
    references_[Key{handle, {}}] = globalReference;
}

void JavaObjectCache::put(void* rawEnv, std::uint64_t handle,
                          void* globalReference) {
    put(rawEnv, handle, {}, globalReference);
}

void JavaObjectCache::put(void* rawEnv, std::uint64_t handle,
                          const std::string& typeName, void* globalReference) {
    if (!handle || !globalReference) return;
    void* previous = nullptr;
    {
        std::lock_guard lock(mutex_);
        const auto it = references_.find(Key{handle, typeName});
        if (it != references_.end()) previous = it->second;
        references_[Key{handle, typeName}] = globalReference;
    }
    if (previous && previous != globalReference) deleteGlobal(rawEnv, previous);
}

void* JavaObjectCache::get(std::uint64_t handle) const {
    std::lock_guard lock(mutex_);
    for (const auto& [key, reference] : references_)
        if (key.handle == handle) return reference;
    return nullptr;
}

void* JavaObjectCache::get(std::uint64_t handle,
                           const std::string& typeName) const {
    std::lock_guard lock(mutex_);
    const auto it = references_.find(Key{handle, typeName});
    return it == references_.end() ? nullptr : it->second;
}

void JavaObjectCache::erase(std::uint64_t handle) {
    std::lock_guard lock(mutex_);
    for (auto it = references_.begin(); it != references_.end();) {
        if (it->first.handle == handle) it = references_.erase(it);
        else ++it;
    }
}

void JavaObjectCache::erase(void* rawEnv, std::uint64_t handle) {
    std::vector<void*> removed;
    {
        std::lock_guard lock(mutex_);
        for (auto it = references_.begin(); it != references_.end();) {
            if (it->first.handle == handle) {
                removed.push_back(it->second);
                it = references_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto reference : removed) deleteGlobal(rawEnv, reference);
}

std::size_t JavaObjectCache::size() const {
    std::lock_guard lock(mutex_);
    return references_.size();
}

void JavaObjectCache::clear(void* rawEnv) {
    std::unordered_map<Key, void*, KeyHash> references;
    {
        std::lock_guard lock(mutex_);
        references.swap(references_);
    }
    for (const auto& [_, ref] : references) deleteGlobal(rawEnv, ref);
}

} // namespace cppfm::jvm
