// Selective native/JVM execution routing for methods affected by a Mixin.
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cppfm::jvm {

enum class DispatchPath { NativeFast, JvmTransformed };

struct MethodKey {
    std::string owner;
    std::string name;
    std::string descriptor;
    bool operator==(const MethodKey& other) const noexcept {
        return owner == other.owner && name == other.name &&
               descriptor == other.descriptor;
    }
};

struct MethodKeyHash {
    std::size_t operator()(const MethodKey& key) const noexcept;
};

struct MethodRoute {
    DispatchPath path = DispatchPath::NativeFast;
    std::uint64_t baselineHash = 0;
    std::uint64_t transformedHash = 0;
};

class ModRoutingTable {
public:
    // Owners are canonicalized to JVM internal names (slash-separated).
    static std::string canonicalOwner(std::string owner);
    static std::uint64_t stableHash(const std::string& owner,
                                    const std::string& name,
                                    const std::string& descriptor) noexcept;

    void markTransformed(std::string owner, std::string name,
                         std::string descriptor,
                         std::uint64_t transformedHash = 0);
    void markNative(std::string owner, std::string name,
                    std::string descriptor,
                    std::uint64_t baselineHash = 0);
    void markBaseline(std::string owner, std::string name,
                      std::string descriptor, std::uint64_t baselineHash);
    DispatchPath path(const std::string& owner, const std::string& name,
                      const std::string& descriptor) const;
    std::optional<MethodRoute> route(const std::string& owner,
                                     const std::string& name,
                                     const std::string& descriptor) const;
    std::uint64_t hash(const std::string& owner, const std::string& name,
                       const std::string& descriptor) const;
    std::vector<MethodKey> transformedMethods() const;
    std::size_t transformedCount() const;
    std::size_t nativeCount() const;
    std::size_t size() const;
    void clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<MethodKey, MethodRoute, MethodKeyHash> paths_;
};

} // namespace cppfm::jvm
