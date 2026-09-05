#include "NativeHandleTable.hpp"

namespace cppfm::jvm {

std::uint64_t NativeHandleTable::encode(std::uint64_t id, HandleKind kind,
                                        std::uint16_t generation) noexcept {
    // 40 bits id, 8 bits type, 16 bits generation.
    return (id & 0xFFFFFFFFFFULL) |
           (static_cast<std::uint64_t>(kind) << 40) |
           (static_cast<std::uint64_t>(generation) << 48);
}

std::uint64_t NativeHandleTable::idOf(std::uint64_t handle) noexcept {
    return handle & 0xFFFFFFFFFFULL;
}

HandleKind NativeHandleTable::kindOf(std::uint64_t handle) noexcept {
    return static_cast<HandleKind>((handle >> 40) & 0xFFU);
}

std::uint16_t NativeHandleTable::generationOf(std::uint64_t handle) noexcept {
    return static_cast<std::uint16_t>(handle >> 48);
}

std::uint64_t NativeHandleTable::registerObject(void* address, HandleKind kind) {
    if (!address || kind == HandleKind::Unknown) return 0;
    std::lock_guard lock(mutex_);
    const Key key{address, kind};
    if (const auto it = byAddress_.find(key); it != byAddress_.end())
        return it->second;

    const std::uint64_t id = nextId_++ & 0xFFFFFFFFFFULL;
    if (id == 0) return 0; // the opaque id space is exhausted; fail closed
    // IDs are not recycled during a server lifetime.  This makes a stale Java
    // handle fail closed even if an allocator reuses the same C++ address.
    auto& lastGeneration = generations_[key];
    const std::uint16_t generation = lastGeneration == 0 || lastGeneration == 0xFFFF
        ? 1 : static_cast<std::uint16_t>(lastGeneration + 1);
    lastGeneration = generation;
    const std::uint64_t handle = encode(id, kind, generation);
    byAddress_.emplace(key, handle);
    byHandle_.emplace(handle, HandleRecord{address, kind, generation});
    return handle;
}

bool NativeHandleTable::invalidate(void* address, HandleKind kind) {
    if (!address) return false;
    std::lock_guard lock(mutex_);
    const Key key{address, kind};
    const auto it = byAddress_.find(key);
    if (it == byAddress_.end()) return false;
    byHandle_.erase(it->second);
    byAddress_.erase(it);
    return true;
}

bool NativeHandleTable::invalidateHandle(std::uint64_t handle) {
    if (!handle) return false;
    std::lock_guard lock(mutex_);
    const auto it = byHandle_.find(handle);
    if (it == byHandle_.end()) return false;
    byAddress_.erase(Key{it->second.address, it->second.kind});
    byHandle_.erase(it);
    return true;
}

std::optional<HandleRecord> NativeHandleTable::describe(std::uint64_t handle) const {
    if (!handle) return std::nullopt;
    std::lock_guard lock(mutex_);
    const auto it = byHandle_.find(handle);
    if (it == byHandle_.end()) return std::nullopt;
    if (idOf(handle) == 0 || kindOf(handle) != it->second.kind ||
        generationOf(handle) != it->second.generation)
        return std::nullopt;
    return it->second;
}

void* NativeHandleTable::resolve(std::uint64_t handle, HandleKind expected) const {
    const auto record = describe(handle);
    if (!record || (expected != HandleKind::Unknown && record->kind != expected))
        return nullptr;
    return record->address;
}

bool NativeHandleTable::valid(std::uint64_t handle, HandleKind expected) const {
    return resolve(handle, expected) != nullptr;
}

HandleKind NativeHandleTable::kind(std::uint64_t handle) const noexcept {
    const auto record = describe(handle);
    return record ? record->kind : HandleKind::Unknown;
}

std::size_t NativeHandleTable::size() const {
    std::lock_guard lock(mutex_);
    return byHandle_.size();
}

void NativeHandleTable::clear() {
    std::lock_guard lock(mutex_);
    byAddress_.clear();
    byHandle_.clear();
    generations_.clear();
}

} // namespace cppfm::jvm
