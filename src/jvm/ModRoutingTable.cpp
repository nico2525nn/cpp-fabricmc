#include "ModRoutingTable.hpp"

namespace cppfm::jvm {

std::size_t MethodKeyHash::operator()(const MethodKey& key) const noexcept {
    const auto h = std::hash<std::string>{}(key.owner);
    const auto n = std::hash<std::string>{}(key.name);
    const auto d = std::hash<std::string>{}(key.descriptor);
    return h ^ (n << 1) ^ (d << 2);
}

void ModRoutingTable::markTransformed(std::string owner, std::string name,
                                      std::string descriptor) {
    std::lock_guard lock(mutex_);
    paths_[MethodKey{std::move(owner), std::move(name), std::move(descriptor)}] =
        DispatchPath::JvmTransformed;
}

void ModRoutingTable::markNative(std::string owner, std::string name,
                                 std::string descriptor) {
    std::lock_guard lock(mutex_);
    paths_[MethodKey{std::move(owner), std::move(name), std::move(descriptor)}] =
        DispatchPath::NativeFast;
}

DispatchPath ModRoutingTable::path(const std::string& owner,
                                   const std::string& name,
                                   const std::string& descriptor) const {
    std::lock_guard lock(mutex_);
    const auto it = paths_.find(MethodKey{owner, name, descriptor});
    if (it != paths_.end()) return it->second;
    const auto wildcard = paths_.find(MethodKey{owner, name, "*"});
    return wildcard == paths_.end() ? DispatchPath::NativeFast : wildcard->second;
}

std::vector<MethodKey> ModRoutingTable::transformedMethods() const {
    std::lock_guard lock(mutex_);
    std::vector<MethodKey> result;
    for (const auto& [key, pathValue] : paths_)
        if (pathValue == DispatchPath::JvmTransformed) result.push_back(key);
    return result;
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
