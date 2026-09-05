// Stable, generation-checked handles used at the C++/JVM boundary.
// Java never receives a raw C++ pointer.  The table owns only the association;
// the pointed-to object remains owned by the game server.
#pragma once

#include <cstdint>
#include <cstddef>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace cppfm::jvm {

enum class HandleKind : std::uint8_t {
    Unknown = 0,
    Server = 1,
    World = 2,
    Player = 3,
    Entity = 4,
    BlockState = 5,
    ItemStack = 6,
};

struct HandleRecord {
    void* address = nullptr; // C++-only; never serialized or exposed to Java.
    HandleKind kind = HandleKind::Unknown;
    std::uint16_t generation = 0;
};

class NativeHandleTable {
public:
    // Holds the table mutex for the complete native borrow.  Invalidation
    // therefore waits for an in-flight C++ access before the owning game
    // shared_ptr is allowed to disappear.  The raw address remains private to
    // native code and is never returned to Java.
    class ScopedResolution {
    public:
        ScopedResolution() = default;
        ScopedResolution(const ScopedResolution&) = delete;
        ScopedResolution& operator=(const ScopedResolution&) = delete;
        ScopedResolution(ScopedResolution&&) noexcept = default;
        ScopedResolution& operator=(ScopedResolution&&) noexcept = default;

        void* get() const noexcept { return address_; }
        HandleKind kind() const noexcept { return kind_; }
        explicit operator bool() const noexcept { return address_ != nullptr; }

    private:
        friend class NativeHandleTable;
        ScopedResolution(std::unique_lock<std::mutex>&& lock,
                         const HandleRecord& record) noexcept
            : lock_(std::move(lock)), address_(record.address), kind_(record.kind) {}

        std::unique_lock<std::mutex> lock_;
        void* address_ = nullptr;
        HandleKind kind_ = HandleKind::Unknown;
    };

    // The returned value is opaque to Java.  Registering the same address and
    // kind while it is alive returns the same handle.
    std::uint64_t registerObject(void* address, HandleKind kind);
    bool invalidate(void* address, HandleKind kind);
    bool invalidateHandle(std::uint64_t handle);
    std::optional<std::uint64_t> findHandle(void* address,
                                            HandleKind kind) const;
    ScopedResolution acquire(std::uint64_t handle,
                             HandleKind expected = HandleKind::Unknown) const;

    void* resolve(std::uint64_t handle,
                  HandleKind expected = HandleKind::Unknown) const;
    std::optional<HandleRecord> describe(std::uint64_t handle) const;
    bool valid(std::uint64_t handle,
               HandleKind expected = HandleKind::Unknown) const;
    HandleKind kind(std::uint64_t handle) const noexcept;
    std::size_t size() const;
    void clear();

private:
    struct Key {
        void* address = nullptr;
        HandleKind kind = HandleKind::Unknown;
        bool operator==(const Key& other) const noexcept {
            return address == other.address && kind == other.kind;
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept {
            const auto p = reinterpret_cast<std::uintptr_t>(key.address);
            return std::hash<std::uintptr_t>{}(p) ^
                   (std::hash<unsigned>{}(static_cast<unsigned>(key.kind)) << 1);
        }
    };

    static std::uint64_t encode(std::uint64_t id, HandleKind kind,
                                std::uint16_t generation) noexcept;
    static std::uint64_t idOf(std::uint64_t handle) noexcept;
    static HandleKind kindOf(std::uint64_t handle) noexcept;
    static std::uint16_t generationOf(std::uint64_t handle) noexcept;

    mutable std::mutex mutex_;
    std::unordered_map<Key, std::uint64_t, KeyHash> byAddress_;
    std::unordered_map<std::uint64_t, HandleRecord> byHandle_;
    // Keep a generation per address/kind even though object ids are monotonic.
    // This makes the stale-handle invariant explicit and protects a future id
    // allocator change from silently re-validating an old Java wrapper.
    std::unordered_map<Key, std::uint16_t, KeyHash> generations_;
    std::uint64_t nextId_ = 1;
};

} // namespace cppfm::jvm
