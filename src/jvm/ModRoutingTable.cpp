#include "ModRoutingTable.hpp"

#include <algorithm>

namespace {
void fnvAppend(std::uint64_t& hash, const std::string& value) noexcept {
    for (const unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    hash ^= 0xFFU;
    hash *= 1099511628211ULL;
}
}

namespace cppfm::jvm {

std::size_t MethodKeyHash::operator()(const MethodKey& key) const noexcept {
    const auto h = std::hash<std::string>{}(key.owner);
    const auto n = std::hash<std::string>{}(key.name);
    const auto d = std::hash<std::string>{}(key.descriptor);
    return h ^ (n << 1) ^ (d << 2);
}

std::string ModRoutingTable::canonicalOwner(std::string owner) {
    std::replace(owner.begin(), owner.end(), '.', '/');
    if (owner.size() >= 2 && owner.front() == 'L' && owner.back() == ';')
        owner = owner.substr(1, owner.size() - 2);
    return owner;
}

std::uint64_t ModRoutingTable::stableHash(const std::string& owner,
                                          const std::string& name,
                                          const std::string& descriptor) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    fnvAppend(hash, canonicalOwner(owner));
    fnvAppend(hash, name);
    fnvAppend(hash, descriptor);
    return hash;
}

void ModRoutingTable::markTransformed(std::string owner, std::string name,
                                      std::string descriptor,
                                      std::uint64_t transformedHash) {
    std::lock_guard lock(mutex_);
    MethodKey key{canonicalOwner(std::move(owner)), std::move(name), std::move(descriptor)};
    auto& route = paths_[key];
    route.path = DispatchPath::JvmTransformed;
    route.transformedHash = transformedHash ? transformedHash : stableHash(key.owner, key.name, key.descriptor);
}

void ModRoutingTable::markNative(std::string owner, std::string name,
                                 std::string descriptor,
                                 std::uint64_t baselineHash) {
    std::lock_guard lock(mutex_);
    MethodKey key{canonicalOwner(std::move(owner)), std::move(name), std::move(descriptor)};
    auto& route = paths_[key];
    route.path = DispatchPath::NativeFast;
    if (baselineHash) route.baselineHash = baselineHash;
}

void ModRoutingTable::markBaseline(std::string owner, std::string name,
                                   std::string descriptor,
                                   std::uint64_t baselineHash) {
    std::lock_guard lock(mutex_);
    MethodKey key{canonicalOwner(std::move(owner)), std::move(name), std::move(descriptor)};
    auto& route = paths_[key];
    route.baselineHash = baselineHash;
}

DispatchPath ModRoutingTable::path(const std::string& owner,
                                   const std::string& name,
                                   const std::string& descriptor) const {
    std::lock_guard lock(mutex_);
    const MethodKey exactKey{canonicalOwner(owner), name, descriptor};
    if (const auto it = paths_.find(exactKey); it != paths_.end())
        return it->second.path;
    const MethodKey wildcardKey{exactKey.owner, exactKey.name, "*"};
    if (const auto it = paths_.find(wildcardKey); it != paths_.end())
        return it->second.path;
    return DispatchPath::NativeFast;
}

std::optional<MethodRoute> ModRoutingTable::route(const std::string& owner,
                                                   const std::string& name,
                                                   const std::string& descriptor) const {
    std::lock_guard lock(mutex_);
    const MethodKey exactKey{canonicalOwner(owner), name, descriptor};
    const auto it = paths_.find(exactKey);
    if (it != paths_.end()) return it->second;
    const MethodKey wildcardKey{exactKey.owner, exactKey.name, "*"};
    const auto wildcard = paths_.find(wildcardKey);
    return wildcard == paths_.end() ? std::nullopt : std::optional<MethodRoute>(wildcard->second);
}

std::uint64_t ModRoutingTable::hash(const std::string& owner,
                                    const std::string& name,
                                    const std::string& descriptor) const {
    const auto routeValue = route(owner, name, descriptor);
    if (routeValue && routeValue->transformedHash) return routeValue->transformedHash;
    return stableHash(owner, name, descriptor);
}

std::vector<MethodKey> ModRoutingTable::transformedMethods() const {
    std::lock_guard lock(mutex_);
    std::vector<MethodKey> result;
    for (const auto& [key, routeValue] : paths_)
        if (routeValue.path == DispatchPath::JvmTransformed) result.push_back(key);
    return result;
}

std::size_t ModRoutingTable::transformedCount() const {
    std::lock_guard lock(mutex_);
    std::size_t count = 0;
    for (const auto& [_, routeValue] : paths_)
        if (routeValue.path == DispatchPath::JvmTransformed) ++count;
    return count;
}

std::size_t ModRoutingTable::nativeCount() const {
    std::lock_guard lock(mutex_);
    std::size_t count = 0;
    for (const auto& [_, routeValue] : paths_)
        if (routeValue.path == DispatchPath::NativeFast) ++count;
    return count;
}

std::size_t ModRoutingTable::size() const {
    std::lock_guard lock(mutex_);
    return paths_.size();
}

void ModRoutingTable::clear() {
    std::lock_guard lock(mutex_);
    paths_.clear();
}

} // namespace cppfm::jvm
