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

    // Do not mask a wrapped allocator value back into the live id space.  A
    // reused id could make an old Java wrapper alias a new native object.
    constexpr std::uint64_t kMaxId = 0xFFFFFFFFFFULL;
    if (nextId_ == 0 || nextId_ > kMaxId) return 0;
    const std::uint64_t id = nextId_++;
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

std::optional<std::uint64_t> NativeHandleTable::findHandle(
    void* address, HandleKind kind) const {
    if (!address || kind == HandleKind::Unknown) return std::nullopt;
    std::lock_guard lock(mutex_);
    const auto it = byAddress_.find(Key{address, kind});
    if (it == byAddress_.end()) return std::nullopt;
    const auto record = byHandle_.find(it->second);
    if (record == byHandle_.end() || record->second.address != address ||
        record->second.kind != kind)
        return std::nullopt;
    return it->second;
}

NativeHandleTable::ScopedResolution NativeHandleTable::acquire(
    std::uint64_t handle, HandleKind expected) const {
    if (!handle) return {};
    std::unique_lock lock(mutex_);
    const auto it = byHandle_.find(handle);
    if (it == byHandle_.end()) return {};
    if (idOf(handle) == 0 || kindOf(handle) != it->second.kind ||
        generationOf(handle) != it->second.generation ||
        (expected != HandleKind::Unknown && it->second.kind != expected))
        return {};
    return ScopedResolution(std::move(lock), it->second);
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
    auto resolution = acquire(handle, expected);
    return resolution.get();
}

bool NativeHandleTable::valid(std::uint64_t handle, HandleKind expected) const {
    return static_cast<bool>(acquire(handle, expected));
}

HandleKind NativeHandleTable::kind(std::uint64_t handle) const noexcept {
    const auto resolution = acquire(handle);
    return resolution ? resolution.kind() : HandleKind::Unknown;
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
