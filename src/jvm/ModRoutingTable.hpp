// Selective native/JVM execution routing for methods affected by a Mixin.
#pragma once

#include <cstddef>
#include <mutex>
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

class ModRoutingTable {
public:
    void markTransformed(std::string owner, std::string name,
                         std::string descriptor);
    void markNative(std::string owner, std::string name,
                    std::string descriptor);
    DispatchPath path(const std::string& owner, const std::string& name,
                      const std::string& descriptor) const;
    std::vector<MethodKey> transformedMethods() const;
    std::size_t size() const;
    void clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<MethodKey, DispatchPath, MethodKeyHash> paths_;
};

} // namespace cppfm::jvm
