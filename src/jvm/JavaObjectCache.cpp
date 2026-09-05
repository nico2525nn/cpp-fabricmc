#include "JavaObjectCache.hpp"

#if defined(CPPFM_HAS_JNI)
#include <jni.h>
#endif

namespace cppfm::jvm {

void JavaObjectCache::put(std::uint64_t handle, void* globalReference) {
    if (!handle || !globalReference) return;
    std::lock_guard lock(mutex_);
    references_[handle] = globalReference;
}

void* JavaObjectCache::get(std::uint64_t handle) const {
    std::lock_guard lock(mutex_);
    const auto it = references_.find(handle);
    return it == references_.end() ? nullptr : it->second;
}

void JavaObjectCache::erase(std::uint64_t handle) {
    std::lock_guard lock(mutex_);
    references_.erase(handle);
}

std::size_t JavaObjectCache::size() const {
    std::lock_guard lock(mutex_);
    return references_.size();
}

void JavaObjectCache::clear(void* rawEnv) {
    std::lock_guard lock(mutex_);
#if defined(CPPFM_HAS_JNI)
    auto* env = static_cast<JNIEnv*>(rawEnv);
    if (env) {
        for (const auto& [_, ref] : references_)
            if (ref) env->DeleteGlobalRef(static_cast<jobject>(ref));
    }
#else
    (void)rawEnv;
#endif
    references_.clear();
}

} // namespace cppfm::jvm
